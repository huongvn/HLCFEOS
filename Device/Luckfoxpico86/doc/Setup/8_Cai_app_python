## Python BMS Engine - Backend Logic

Python BMS Engine chạy như một systemd service và tự động khởi động cùng hệ thống.

## 1. Cập nhật hệ thống và cài ntp

Trước tiên, hãy đảm bảo hệ thống có quyền quản trị và danh sách gói mới nhất và time mới nhất:

```bash
date
sudo apt-get update
sudo apt install ntp -y
sudo systemctl status ntp
ntpq -p
```

## 2. Cài đặt Python BMS Engine

Python BMS Engine thay thế Node-RED, sử dụng ít tài nguyên hơn (~30MB RAM so với ~100MB RAM).

### 2.1. Copy Python BMS Engine vào Luckfox

Có 3 cách để copy thư mục `python_engine` vào Luckfox:

#### Cách 1: Qua USB

```bash
# Trên máy tính: Copy thư mục python_engine vào USB
# Trên Luckfox: Mount USB và copy
sudo mkdir -p /mnt/usb
sudo mount /dev/sda1 /mnt/usb  # Điều chỉnh theo tên thiết bị USB thực tế
sudo cp -r /mnt/usb/python_engine /home/pico/
sudo umount /mnt/usb
```

#### Cách 2: Qua SSH/SCP

```bash
# Trên máy tính (từ thư mục chứa python_engine):
scp -r python_engine pico@192.168.1.124:/home/pico/

# Hoặc từ thư mục HLCFEOS:
scp -r Device/Luckfoxpico86/code/python_engine pico@192.168.1.124:/home/pico/
```

#### Cách 3: Qua SFTP

```bash
# Trên máy tính:
sftp pico@192.168.1.124
# Sau khi kết nối:
put -r python_engine /home/pico/
exit
```

Hoặc dùng FileZilla/WinSCP để kéo thả thư mục `python_engine` vào `/home/pico/`

### 2.2. Copy devices.yaml

```bash
# Copy devices.yaml từ lvgl_project
cp /home/pico/lvgl_project/devices.yaml /home/pico/python_engine/
```

Hoặc nếu đã có lvgl_project:

```bash
cd /home/pico/python_engine
ln -s ../lvgl_project/devices.yaml devices.yaml
```

### 2.3. Cài đặt dependencies

```bash
cd /home/pico/python_engine

# Cài đặt Python dependencies
pip3 install -r requirements.txt
```

### 2.4. Cấu hình

Chỉnh sửa file cấu hình:

```bash
nano config/config.yaml
```

Đảm bảo các đường dẫn đúng:
- `devices_file`: `/home/pico/python_engine/devices.yaml`
- `database.path`: Đường dẫn đến SQLite database
- `ota.ota_url`: URL của OTA server

### 2.5. Deploy service

```bash
sudo ./deploy.sh
```

Script sẽ:
- Cài đặt Python dependencies
- Tạo log file tại `/var/log/bms-engine.log`
- Cài đặt systemd service `bms-engine.service`
- Khởi động service

### 2.6. Kiểm tra service

```bash
# Kiểm tra trạng thái
sudo systemctl status bms-engine

# Xem logs
sudo journalctl -u bms-engine -f

# Hoặc xem log file
sudo tail -f /var/log/bms-engine.log
```
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
