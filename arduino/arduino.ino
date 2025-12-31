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
#define DEVICE_PASSWORD "123456"

#define RELAY_FAN 5
#define LED_LIGHT 27
#define RESET_BTN 26
#define BTN_FAN_SPEED 25
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

WiFiClient client;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String deviceId_fan = "fan_001";
String deviceId_light = "light_001";
IPAddress serverIP;

bool fan_state = false;
int fan_speed = 0;

bool light_state = false;

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

void drawFanSpeedBar() {
    int x = 0;
    int y = 34;
    int barWidth = 128;
    int barHeight = 10;

    display.drawRect(x, y, barWidth, barHeight, SSD1306_WHITE);

    if (!fan_state || fan_speed == 0) return;

    int fillWidth = (barWidth / 3) * fan_speed;

    display.fillRect(
        x + 1,
        y + 1,
        fillWidth - 2,
        barHeight - 2,
        SSD1306_WHITE
    );
}

void showFanOLED() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("DEVICE: FAN");

    display.setCursor(0, 12);
    display.print("STATE : ");
    display.println(fan_state ? "ON" : "OFF");

    display.setCursor(0, 24);
    display.print("SPEED : ");
    display.println(fan_speed);

    drawFanSpeedBar();

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

    if (!MDNS.begin("esp32-fan")) return false;

    serverIP = MDNS.queryHost(SERVER_HOSTNAME);
    if (!serverIP) return false;

    if (client.connect(serverIP, SERVER_PORT)) {
        StaticJsonDocument<256> doc;
        doc["type"] = "request";
        doc["from"] = deviceId_fan;
        doc["to"] = "server";
        doc["action"] = "register";
        doc["timestamp"] = millis();
        doc["data"]["device_type"] = "fan";
        doc["data"]["password"] = DEVICE_PASSWORD;
        
        String json;
        serializeJson(doc, json);
        client.println(json);
        
        Serial.println("[SERVER] Connected and registered as fan");

        {
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

            Serial.println("[SERVER] Registered light device");
        }

        displayOLED("Server", "Connected", serverIP.toString(), "");
        return true;
    }
    return false;
}

void setLight(bool on) {
    light_state = on;

    if (on) {
        digitalWrite(LED_LIGHT, HIGH);
        Serial.println("[LIGHT] LED ON");
    } else {
        digitalWrite(LED_LIGHT, LOW);
        Serial.println("[LIGHT] LED OFF");
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

    String json;
    serializeJson(res, json);
    client.println(json);

    Serial.print("[LIGHT STATUS] Sent to ");
    Serial.println(to);
}

void setFan(bool on, int speed) {
    fan_state = on;

    if (!on) {
        digitalWrite(RELAY_FAN, LOW);
        fan_speed = 0;
        Serial.println("[FAN] Turned OFF");
    } else {
        digitalWrite(RELAY_FAN, HIGH);
        
        if (speed < 1) speed = 1;
        if (speed > 3) speed = 3;
        
        fan_speed = speed;
        
        Serial.print("[FAN] Turned ON - Speed: ");
        Serial.println(speed);
    }
    
    showFanOLED();
}

void sendFanStatus(const char* to) {
    StaticJsonDocument<256> res;
    res["type"] = "response";
    res["from"] = deviceId_fan;
    res["to"] = to;
    res["action"] = "status";

    JsonObject data = res.createNestedObject("data");
    data["status"] = "success";
    data["state"] = fan_state ? "on" : "off";
    data["speed"] = fan_speed;

    String json;
    serializeJson(res, json);
    client.println(json);
    
    Serial.print("[STATUS] Sent to ");
    Serial.println(to);
}

void sendResetNotify() {
    StaticJsonDocument<256> doc;
    doc["type"] = "notify";
    doc["from"] = deviceId_fan;
    doc["to"] = "server";
    doc["action"] = "reset";
    doc["timestamp"] = millis();
    
    JsonObject data = doc.createNestedObject("data");
    data["reason"] = "button_pressed";
    data["state"] = "off";
    data["speed"] = 0;

    String json;
    serializeJson(doc, json);
    client.println(json);
}

void resetAllDevices() {
    Serial.println("[RESET] All devices");

    fan_state = false;
    fan_speed = 0;
    digitalWrite(RELAY_FAN, LOW);

    light_state = false;
    digitalWrite(LED_LIGHT, LOW);

    showFanOLED();

    if (client.connected()) {
        sendResetNotify();
    }
}

void sendHeartbeat() {
    StaticJsonDocument<128> doc;
    doc["type"] = "notify";
    doc["from"] = deviceId_fan;
    doc["to"] = "server";
    doc["action"] = "heartbeat";
    doc["timestamp"] = millis();
    doc["data"]["status"] = "alive";

    String json;
    serializeJson(doc, json);
    client.println(json);
    
    Serial.println("[HEARTBEAT] Sent");
}

void handleMessage(String json) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[ERROR] Failed to parse JSON");
        return;
    }

    const char* action = doc["action"];
    String to = doc["to"];

    Serial.print("[MSG] Action: ");
    Serial.print(action);
    Serial.print(" | To: ");
    Serial.println(to);

    if (strcmp(action, "control") == 0) {
        if (to == deviceId_fan) {
            bool state = doc["data"]["state"];
            int speed = doc["data"]["speed"] | 1;
            
            Serial.print("[CONTROL] State: ");
            Serial.print(state ? "ON" : "OFF");
            Serial.print(" | Speed: ");
            Serial.println(speed);
            
            setFan(state, speed);
            sendFanStatus(doc["from"]);
        }
        else if (to == deviceId_light) {
            bool state = doc["data"]["state"];
            setLight(state);
            sendLightStatus(doc["from"]);
        }
    }
    else if (strcmp(action, "status") == 0) {
        if (to == deviceId_fan) {
            Serial.println("[STATUS] Request received");
            sendFanStatus(doc["from"]);
        }
        else if (to == deviceId_light) {
            sendLightStatus(doc["from"]);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[SETUP] ESP32 Fan Control Starting...");

    pinMode(RELAY_FAN, OUTPUT);
    digitalWrite(RELAY_FAN, LOW);
    Serial.println("[SETUP] Relay initialized (OFF)");

    pinMode(LED_LIGHT, OUTPUT);
    digitalWrite(LED_LIGHT, LOW);
    Serial.println("[SETUP] LED Light initialized (OFF)");

    pinMode(RESET_BTN, INPUT_PULLUP);
    Serial.println("[SETUP] Reset button initialized");

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[ERROR] OLED initialization failed");
    } else {
        Serial.println("[SETUP] OLED initialized");
    }

    showFanOLED();

    connectWiFi();
    connectServerMDNS();
    
    Serial.println("[SETUP] Complete!");
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Disconnected, reconnecting...");
        connectWiFi();
        return;
    }

    if (!client.connected()) {
        Serial.println("[SERVER] Disconnected, reconnecting...");
        connectServerMDNS();
        delay(3000);
        return;
    }

    if (client.available()) {
        String msg = client.readStringUntil('\n');
        msg.trim();
        if (msg.length()) {
            Serial.println("[RX] " + msg);
            handleMessage(msg);
        }
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