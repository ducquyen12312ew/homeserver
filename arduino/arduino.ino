#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* WIFI_SSID = "Duy Anh";
const char* WIFI_PASS = "02042009";

const int SERVER_PORT = 6666;
const char* SERVER_HOSTNAME = "quyen";
//const char* SERVER_HOSTNAME = "quyen";
#define DEVICE_PASSWORD "123456"
//mdns
//file txt, chạy thì sửa file txt đấy...
#define RELAY_LIGHT 27
#define RESET_BTN 26
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

WiFiClient client;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String deviceId_light = "light_001";
IPAddress serverIP;

bool light_state = false;
int light_power = 0;
unsigned long lastHeartbeat = 0;

void displayOLED(String l1, String l2, String l3, String l4) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);  display.println(l1);
    display.setCursor(0, 16); display.println(l2);
    display.setCursor(0, 32); display.println(l3);
    display.setCursor(0, 48); display.println(l4);
    display.display();
}

void showLightOLED() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("DEVICE: LIGHT");

    display.setCursor(0, 16);
    display.print("STATE: ");
    display.println(light_state ? "ON" : "OFF");

    display.setCursor(0, 32);
    display.print("POWER: ");
    display.print(light_power);
    display.println(" W");

    display.display();
}

void connectWiFi() {
    displayOLED("WiFi", "Connecting...", WIFI_SSID, "");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        displayOLED("WiFi Connected", WiFi.localIP().toString(), "", "");
        delay(1500);
    } else {
        ESP.restart();
    }
}

bool connectServerMDNS() {
    displayOLED("Server", "Finding via mDNS", SERVER_HOSTNAME, "");

    if (!MDNS.begin("esp32-light")) return false;

    serverIP = MDNS.queryHost(SERVER_HOSTNAME);
    if (!serverIP) return false;

    if (client.connect(serverIP, SERVER_PORT)) {
        StaticJsonDocument<256> doc;
        doc["type"] = "request";
        doc["from"] = deviceId_light;
        doc["to"] = "server";
        doc["action"] = "register";
        doc["timestamp"] = millis();
        doc["data"]["device_type"] = "light";
        doc["data"]["password"] = DEVICE_PASSWORD;
        String json;
        serializeJson(doc, json);
        client.println(json);
        displayOLED("Server", "Connected", serverIP.toString(), "");
        return true;
    }
    return false;
}

void setLight(bool on) {
    light_state = on;
    digitalWrite(RELAY_LIGHT, on ? HIGH : LOW);
    light_power = on ? 10 : 0;
    showLightOLED();
}

void sendResetNotify() {
    StaticJsonDocument<256> doc;
    doc["type"] = "notify";
    doc["from"] = deviceId_light;
    doc["to"] = "server";
    doc["action"] = "reset";
    doc["timestamp"] = millis();
    JsonObject data = doc.createNestedObject("data");
    data["reason"] = "button_pressed";
    data["state"] = "off";
    data["power"] = 0;

    String json;
    serializeJson(doc, json);
    client.println(json);
}


void resetAllDevices() {
    Serial.println("[RESET] All devices");

    light_state = false;
    light_power = 0;
    digitalWrite(RELAY_LIGHT, LOW);

    showLightOLED();

    if (client.connected()) {
        sendResetNotify();
    }
}


void sendLightStatus(const char* to) {
    StaticJsonDocument<256> res;
    res["type"] = "response";
    res["from"] = deviceId_light;
    res["to"] = to;
    res["action"] = "status";

    JsonObject data = res.createNestedObject("data");
    data["status"] = "success";
    data["state"] = light_state ? "on" : "off";
    data["power"] = light_power;

    String json;
    serializeJson(res, json);
    client.println(json);
}

void sendHeartbeat() {
    StaticJsonDocument<128> doc;
    doc["type"] = "notify";
    doc["from"] = deviceId_light;
    doc["to"] = "server";
    doc["action"] = "heartbeat";
    doc["timestamp"] = millis();
    doc["data"]["status"] = "alive";

    String json;
    serializeJson(doc, json);
    client.println(json);
}

void handleMessage(String json) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, json)) return;

    const char* action = doc["action"];
    String to = doc["to"];

    if (strcmp(action, "control") == 0 && to == deviceId_light) {
        bool state = doc["data"]["state"];
        setLight(state);
        sendLightStatus(doc["from"]);
    }
    else if (strcmp(action, "status") == 0 && to == deviceId_light) {
        sendLightStatus(doc["from"]);
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(RELAY_LIGHT, OUTPUT);
    digitalWrite(RELAY_LIGHT, LOW);

    pinMode(RESET_BTN, INPUT_PULLUP);

    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

    showLightOLED();

    connectWiFi();
    connectServerMDNS();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        return;
    }

    if (!client.connected()) {
        connectServerMDNS();
        delay(3000);
        return;
    }

    if (client.available()) {
        String msg = client.readStringUntil('\n');
        msg.trim();
        if (msg.length()) handleMessage(msg);
    }

    static bool lastBtn = HIGH;
    bool curBtn = digitalRead(RESET_BTN);

    if (lastBtn == HIGH && curBtn == LOW) {
        Serial.println("[BUTTON] Reset pressed");
        resetAllDevices();
        delay(300);
    }
    lastBtn = curBtn;

    if (millis() - lastHeartbeat > 30000) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }
}
