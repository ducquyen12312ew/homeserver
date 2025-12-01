# Smart Home IoT System - C11 + GTK + ESP32

## Kiến trúc
```
GTK Client (C) ←→ Ubuntu C11 Server ←→ ESP32 (Arduino)
   (GUI)              (Gateway)          (Hardware)
```

## Cài đặt

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

## Chạy hệ thống

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

## Cấu hình phần cứng

### ESP32 Pinout

- **OLED SSD1306:**
  - SDA: GPIO 21
  - SCL: GPIO 22
  - VCC: 3.3V
  - GND: GND

- **LED Status (Optional):**
  - LED Green: GPIO 12 (Online)
  - LED Red: GPIO 13 (Offline/Error)

## Cấu hình mạng

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

## Giao thức JSON

```json
{
  "type": "request | response | notify",
  "from": "sender_id",
  "to": "receiver_id",
  "action": "register | login | control | status | list_devices | heartbeat",
  "timestamp": 0,
  "data": { }
}
```

## Checkpoint
| Điểm | Chức năng          | File chính                         | Hàm chính                                                                 |
|------|---------------------|------------------------------------|---------------------------------------------------------------------------|
| 1    | Xử lý truyền dòng  | `server.c`, `main.c`, `device.ino` | `handle_conn()`, `send_message()`, `println()`                            |
| 2    | Socket I/O         | `server.c`                         | `srv_init()`, `srv_start()`, `pthread`                                    |
| 1    | Xác thực           | `main.c`, `server.c`               | `on_connect_clicked()`, `ACT_LOGIN`                                       |
| 1    | Khởi tạo thiết bị  | `device.ino`, `server.c`           | `sendRegister()`, `ACT_REGISTER`                                          |
| 2    | Quét thiết bị      | `main.c`, `server.c`               | `on_scan_clicked()`, `handle_list_devices()`                              |
| 2    | Kết nối thiết bị   | `device.ino`, `server.c`           | `connectServer()`, `ConnList`                                             |
| 1    | Điều khiển         | `main.c`, `server.c`, `device.ino` | `on_control_clicked()`, `route_msg()`, `handleMessage()`                  |
| 1    | Lấy thông tin      | `device.ino`                       | `sendStatusResponse()`                                                    |



