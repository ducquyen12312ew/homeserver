# Smart Home IoT System - C11 + GTK + ESP32

Hệ thống IoT điều khiển thiết bị thông minh với kiến trúc 3 tầng: GTK Client - C11 Server - ESP32 Hardware.

## 📋 Tổng quan

- **Server:** C11 multi-threaded TCP server (Ubuntu)
- **Client:** GTK3 GUI application (Ubuntu)
- **Device:** ESP32 với OLED SSD1306 display
- **Protocol:** JSON over TCP
- **Port:** 6666

## 🏗️ Kiến trúc
```
GTK Client (C) ←→ Ubuntu C11 Server ←→ ESP32 (Arduino)
   (GUI)              (Gateway)          (Hardware)
```

## ✨ Tính năng

### Đã triển khai (11/20 điểm)

- ✅ Xử lý truyền dòng (JSON delimiter)
- ✅ Socket I/O với multi-threading
- ✅ Xác thực người dùng (login)
- ✅ Đăng ký thiết bị tự động
- ✅ Quét và liệt kê thiết bị
- ✅ Kết nối và quản lý nhiều thiết bị
- ✅ Điều khiển bật/tắt thiết bị
- ✅ Truy vấn trạng thái thiết bị
- ✅ Heartbeat monitoring (30s)

### Có thể mở rộng

- Đổi mật khẩu thiết bị
- Điều khiển tốc độ quạt (PWM)
- Chế độ điều hòa (cool/heat/dry)
- Tính toán điện năng tiêu thụ
- Hẹn giờ bật/tắt
- Ghi log hoạt động
- Quản lý nhà/phòng/thiết bị

## 🛠️ Công nghệ

**Server:**
- C11 standard
- pthread (multi-threading)
- json-c (JSON parsing)
- POSIX socket API

**Client:**
- C11 standard
- GTK+ 3.0 (GUI framework)
- json-c (JSON parsing)

**ESP32:**
- Arduino C++
- WiFi library
- ArduinoJson
- Adafruit SSD1306 (OLED)

## 📦 Cài đặt

### Yêu cầu

**Ubuntu:**
```bash
sudo apt update
sudo apt install -y build-essential libgtk-3-dev libjson-c-dev
```

**Arduino IDE:**
- ESP32 Board Manager
- ArduinoJson library
- Adafruit SSD1306 library
- Adafruit GFX library

### Build Server
```bash
cd server
make
```

### Build Client
```bash
cd client
make
```

### Upload ESP32

1. Mở Arduino IDE
2. Chọn Board: ESP32 Dev Module
3. Sửa WiFi SSID/Password và Server IP trong code
4. Upload

## 🚀 Chạy hệ thống

### 1. Chạy Server
```bash
cd server
make run
```

### 2. Chạy Client
```bash
cd client
make run
```

### 3. ESP32

- Cấp nguồn qua USB
- ESP32 tự động kết nối WiFi và đăng ký với Server

### 4. Điều khiển

- Click **Connect** để kết nối Server
- Click **Scan Devices** để quét thiết bị
- Chọn thiết bị từ dropdown
- Click **Turn ON/OFF** để điều khiển

## 📡 Giao thức JSON

### Register (ESP32 → Server)
```json
{
  "type": "request",
  "from": "ESP32_eef4e9d4",
  "to": "server",
  "action": "register",
  "timestamp": 12345,
  "data": {
    "device_type": "light",
    "password": "123456"
  }
}
```

### Control (Client → Server → ESP32)
```json
{
  "type": "request",
  "from": "gtk_client",
  "to": "ESP32_eef4e9d4",
  "action": "control",
  "timestamp": 67890,
  "data": {
    "device_type": "light",
    "state": true
  }
}
```

### Status Response (ESP32 → Server → Client)
```json
{
  "type": "response",
  "from": "ESP32_eef4e9d4",
  "to": "gtk_client",
  "action": "status",
  "timestamp": 11111,
  "data": {
    "device_type": "light",
    "state": "on",
    "power": 10,
    "uptime_today": 2.5
  }
}
```

## 🔌 Cấu hình phần cứng

### ESP32 Pinout

- **OLED SSD1306:**
  - SDA: GPIO 21
  - SCL: GPIO 22
  - VCC: 3.3V
  - GND: GND

- **LED Status (Optional):**
  - LED Green: GPIO 12 (Online)
  - LED Red: GPIO 13 (Offline/Error)

## 🌐 Cấu hình mạng

### VMware (nếu dùng Ubuntu trong VM)

1. **Network Adapter:** NAT
2. **Port Forwarding:** 
   - Host Port: 6666
   - VM IP: 192.168.92.130
   - VM Port: 6666

### IP Address

- Server (Ubuntu): 192.168.92.130:6666
- Client (Ubuntu): 127.0.0.1 hoặc 192.168.92.130
- ESP32: DHCP (ví dụ: 172.11.23.110)

## 📁 Cấu trúc thư mục
```
homeserver/
├── server/
│   ├── inc/
│   │   ├── protocol.h
│   │   └── server.h
│   ├── src/
│   │   ├── protocol.c
│   │   ├── server.c
│   │   └── main.c
│   ├── build/
│   └── Makefile
├── client/
│   ├── src/
│   │   └── main.c
│   ├── build/
│   └── Makefile
├── esp32/
│   └── device.ino
└── README.md
```

## 🐛 Troubleshooting

### ESP32 không kết nối Server

- Kiểm tra WiFi SSID/Password
- Kiểm tra Server IP đúng
- Kiểm tra Server đang chạy
- Kiểm tra firewall/port forwarding

### Server compile lỗi
```bash
sudo apt install -y libjson-c-dev
```

### Client compile lỗi
```bash
sudo apt install -y libgtk-3-dev
```

### Brownout detector error (ESP32)

- Đổi cáp USB chất lượng tốt
- Cắm vào cổng USB 3.0
- Dùng adapter 5V 2A

## Tech

- Server: C11 socket programming + JSON protocol
- Client: GTK3 GUI design + network integration
- ESP32: Arduino firmware + OLED display


