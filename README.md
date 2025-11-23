# Smart Home IoT Server

Server TCP/IP bằng C11 để điều khiển thiết bị ESP32 qua JSON.

## Tính năng

- TCP Socket Server (port 8888)
- Multi-threading (pthread)
- Giao thức JSON (json-c)
- Hỗ trợ: Đèn, Quạt, Điều hòa

## Yêu cầu

- Ubuntu 20.04+
- GCC 9.x+ (C11 standard)
- libjson-c-dev

## Cài đặt
```bash
# Clone
git clone https://github.com/username/homeserver.git
cd homeserver

# Cài dependencies
sudo apt update
sudo apt install -y build-essential gcc make libjson-c-dev

# Biên dịch
make
```

## Chạy Server
```bash
make run
```

Server lắng nghe tại `0.0.0.0:8888`

## Giao thức

### 1. Đăng ký thiết bị

Request (ESP32 → Server):
```json
{
  "type": "request",
  "from": "ESP32_ABC123",
  "to": "server",
  "action": "register",
  "data": {
    "device_type": "light",
    "password": "123456"
  }
}
```

Response (Server → ESP32):
```json
{
  "type": "response",
  "from": "server",
  "to": "ESP32_ABC123",
  "action": "register",
  "data": {
    "status": "success",
    "device_id": "ESP32_ABC123"
  }
}
```

### 2. Điều khiển thiết bị

Đèn:
```json
{
  "action": "control",
  "data": {
    "device_type": "light",
    "state": true
  }
}
```

Quạt:
```json
{
  "action": "control",
  "data": {
    "device_type": "fan",
    "state": true,
    "speed": 2
  }
}
```
Speed: 1-3

Điều hòa:
```json
{
  "action": "control",
  "data": {
    "device_type": "ac",
    "state": true,
    "mode": "cool",
    "temperature": 24
  }
}
```
Mode: cool, heat, dry
Temperature: 18-30

### 3. Lấy trạng thái

Request:
```json
{
  "type": "request",
  "from": "client_001",
  "to": "ESP32_ABC123",
  "action": "status"
}
```

Response:
```json
{
  "data": {
    "device_type": "fan",
    "state": "on",
    "speed": 2,
    "power": 45,
    "uptime_today": 3.5
  }
}
```

## Công suất tiêu thụ

| Thiết bị | Trạng thái | Công suất |
|----------|-----------|-----------|
| Đèn | ON | 10W |
| Quạt Speed 1 | ON | 30W |
| Quạt Speed 2 | ON | 45W |
| Quạt Speed 3 | ON | 60W |
| AC Cool 24°C | ON | 1200W |
| AC Heat 24°C | ON | 1500W |
| AC Dry | ON | 800W |

## Cấu trúc thư mục
```
homeserver/
├── inc/              # Header files
│   ├── protocol.h
│   └── server.h
├── src/              # Source files
│   ├── protocol.c
│   ├── server.c
│   └── main.c
├── build/            # Build output
├── Makefile
├── GIAO_THUC.md      # Chi tiết giao thức
└── README.md
```

## Test
```bash
# Test với netcat
echo '{"type":"request","from":"test","to":"server","action":"register","data":{}}' | nc localhost 8888
```

## Kết nối ESP32

ESP32 kết nối qua WiFi:
```cpp
const char* SERVER_IP = "192.168.1.100";  // IP máy chạy server
const int SERVER_PORT = 8888;
```

