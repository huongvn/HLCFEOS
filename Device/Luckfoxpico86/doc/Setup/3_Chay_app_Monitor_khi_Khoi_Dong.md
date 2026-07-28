# CHẠY APP MONITOR VÀ BMS ENGINE KHI KHỞI ĐỘNG

## 1. LVGL App (C++ Binary) - Màn hình cảm ứng

Copy app vào folder `/home/pico/`

Thử chạy app với câu lệnh:

```bash
sudo ./app
```

Xem kết quả trên màn hình của board, sau đó dừng app.

### Các bước để áp dụng thay đổi:

1. **Cập nhật file service:**

```bash
sudo nano /etc/systemd/system/myapp.service
```

Dưới đây là file cấu hình đã được tối ưu:

```ini
[Unit]
Description=My IoT App
# Đảm bảo mạng đã sẵn sàng
After=network.target

[Service]
# --- Cấu hình delay 30 giây ---
ExecStartPre=/bin/sleep 30

# Đường dẫn đến app của bạn
ExecStart=/home/pico/app
WorkingDirectory=/home/pico/

# Quản lý output ra console (tty)
StandardOutput=inherit
StandardError=inherit

# Tự động khởi động lại nếu app bị crash
Restart=always
# Khoảng thời gian chờ giữa các lần restart (ví dụ 5 giây)
RestartSec=5s

# Chạy với quyền cao nhất để truy cập GPIO/I2C/SPI trên Luckfox
User=root

[Install]
WantedBy=multi-user.target
```

> *(Xóa nội dung cũ và dán nội dung mới này vào)*.

2. **Tải lại cấu hình Systemd:**

Mỗi khi bạn sửa file .service, bạn **bắt buộc** phải chạy lệnh này để hệ thống nhận diện thay đổi:

```bash
sudo systemctl daemon-reload
```

3. **Kích hoạt và chạy thử:**

```bash
sudo systemctl enable myapp.service
sudo systemctl restart myapp.service
```

4. **Kiểm tra trạng thái:**

Bạn có thể xem app đang ở trạng thái "vừa boot" hay "đang chạy" bằng lệnh:

```bash
systemctl status myapp.service
```

> Nhìn thấy dòng **Active: active (running)** là app đã chạy ok

**5. Để dừng chạy app.**

```bash
sudo systemctl stop myapp.service
```

### Một vài lưu ý nhỏ cho bạn:

- **Quyền thực thi:** Hãy chắc chắn bạn đã cấp quyền chạy cho file app bằng lệnh `chmod +x /home/pico/app`.
- **Log lỗi:** Nếu sau 30 giây mà app vẫn không chạy, hãy kiểm tra log hệ thống để biết lý do:

```bash
journalctl -u myapp.service -f
```

Việc delay 30 giây này rất hữu ích nếu app của bạn cần khởi tạo màn hình LCD hoặc kết nối đến các cảm biến qua giao thức I2C/Modbus, vì đôi khi các driver này cần thời gian để "nổi" lên hoàn toàn sau khi kernel khởi động.

## 2. Python BMS Engine - Backend Logic

Python BMS Engine chạy như một systemd service và tự động khởi động cùng hệ thống.

### Cài đặt service

```bash
cd /home/pico/HLCFEOS/Device/Luckfoxpico86/code/python_engine
sudo ./deploy.sh
```

Script sẽ tự động:
- Cài đặt Python dependencies
- Tạo service `bms-engine.service`
- Enable service để tự khởi động
- Khởi động service

### Kiểm tra service

```bash
# Kiểm tra trạng thái
sudo systemctl status bms-engine

# Xem logs
sudo journalctl -u bms-engine -f

# Hoặc xem log file
sudo tail -f /var/log/bms-engine.log
```

### Quản lý service

```bash
# Restart service
sudo systemctl restart bms-engine

# Stop service
sudo systemctl stop bms-engine

# Disable auto-start
sudo systemctl disable bms-engine

# Enable auto-start
sudo systemctl enable bms-engine
```

### File service cấu hình

File service được tạo tại `/etc/systemd/system/bms-engine.service`:

```ini
[Unit]
Description=BMS Engine - Python Building Management System
After=network.target mosquitto.service
Wants=mosquitto.service

[Service]
Type=simple
User=pico
Group=pico
WorkingDirectory=/home/pico/HLCFEOS/Device/Luckfoxpico86/code/python_engine
ExecStart=/usr/bin/python3 /home/pico/HLCFEOS/Device/Luckfoxpico86/code/python_engine/src/main.py
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal
SyslogIdentifier=bms-engine

# Environment variables
Environment=PYTHONUNBUFFERED=1

# Security settings
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

### So sánh 2 services

| Aspect | myapp.service (LVGL) | bms-engine.service (Python) |
|--------|----------------------|------------------------------|
| Mục đích | UI trên màn hình | Backend logic, MQTT, SQLite |
| Delay khởi động | 30 giây | Không |
| Auto-restart | Có | Có (on-failure) |
| User | root | pico |
| Logs | journalctl | journalctl + /var/log/bms-engine.log |

### Kiểm tra cả 2 services

```bash
# Xem trạng thái tất cả services
sudo systemctl status myapp.service bms-engine.service

# Xem logs của cả 2
sudo journalctl -u myapp.service -u bms-engine.service -f
```
