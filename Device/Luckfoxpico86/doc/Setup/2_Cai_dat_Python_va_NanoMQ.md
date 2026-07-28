# HƯỚNG DẪN CÀI ĐẶT & CẤU HÌNH PYTHON BMS ENGINE + NANOMQ (LUCKFOX PICO)

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

## 3. Cài đặt NanoMQ (MQTT Broker)

Dành cho Luckfox Pico (ARMv7l) chạy GLIBC 2.35, phiên bản **0.21.9-10** là bản tương thích tốt nhất.

```bash
# Thêm Repo EMQX
curl -s https://assets.emqx.com/scripts/install-nanomq-deb.sh | sudo bash

# Cài đặt phiên bản chỉ định để tránh lỗi GLIBC
sudo apt-get install nanomq=0.21.11

# Kiểm tra vị trí file thực thi (thường là /usr/local/bin/nanomq)
which nanomq
```

## Thiết lập Chạy ngầm (Systemd Service)

Để khắc phục lỗi status=203/EXEC, file cấu hình phải trỏ chính xác vào thư mục /usr/local/bin/.

1. **Tạo/Sửa file service:**

```bash
sudo nano /etc/systemd/system/nanomq.service
```

2. **Nội dung chuẩn (Copy & Paste):**

```ini
[Unit]
Description=NanoMQ Broker
After=network.target

[Service]
Type=simple
# Đường dẫn chính xác trên Luckfox
ExecStart=/usr/local/bin/nanomq start
Restart=on-failure
User=root

[Install]
WantedBy=multi-user.target
```

3. **Kích hoạt và chạy:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable nanomq
sudo systemctl restart nanomq
```

## 4. Các lệnh kiểm tra & Theo dõi (Log)

Đây là các lệnh quan trọng để bạn quản lý hệ thống sau này:

### NanoMQ

- **Kiểm tra trạng thái:** `sudo systemctl status nanomq`
- **Theo dõi log trực tiếp:** `sudo journalctl -u nanomq -f`
- **Kiểm tra phiên bản:** `nanomq --version`
- **Kiểm tra cổng 1883 (MQTT):** `netstat -tln | grep 1883`

### Python BMS Engine

- **Kiểm tra trạng thái:** `sudo systemctl status bms-engine`
- **Theo dõi log trực tiếp:** `sudo journalctl -u bms-engine -f`
- **Xem log file:** `sudo tail -f /var/log/bms-engine.log`
- **Restart service:** `sudo systemctl restart bms-engine`
- **Stop service:** `sudo systemctl stop bms-engine`

## 5. Kiểm tra kết nối MQTT

```bash
# Subscribe tất cả topics để xem messages
mosquitto_sub -h localhost -p 1883 -t "#" -v

# Subscribe chỉ topics từ Tasmota gateway
mosquitto_sub -h localhost -p 1883 -t "tele/tasmota_6DCAA8/#" -v

# Test publish command
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/power/set" -m "ON"
```

## 6. So sánh với Node-RED (cũ)

| Aspect | Node-RED (cũ) | Python BMS Engine (mới) |
|--------|---------------|-------------------------|
| RAM usage | ~100MB | ~30MB |
| Deploy | Import flow qua UI | Copy qua USB/SSH/SFTP + `./deploy.sh` |
| Debug | Debug tab trong UI | `journalctl -f` |
| Version control | JSON files | Python files |
| Maintenance | UI-based | Code-based |
| Performance | Medium | High |

### Lưu ý cho Luckfox Pico:

- Nếu bạn dùng bản **Static Binary** (.tar.gz) cho NanoMQ, hãy đảm bảo đã cấp quyền chạy bằng lệnh: `sudo chmod +x /usr/local/bin/nanomq`.
- Luôn dùng sudo khi can thiệp vào các file trong thư mục /etc/ hoặc /usr/.
- Python BMS Engine tự động kết nối đến NanoMQ tại `localhost:1883`.

## 7. Tài liệu tham khảo

Xem thêm tài liệu chi tiết tại:
- [python_engine/README.md](../../code/python_engine/README.md) - Hướng dẫn sử dụng Python BMS Engine
- [python_engine/PlanAndDoc/ARCHITECTURE.md](../../code/python_engine/PlanAndDoc/ARCHITECTURE.md) - Kiến trúc hệ thống
- [python_engine/PlanAndDoc/OTA_PLAN.md](../../code/python_engine/PlanAndDoc/OTA_PLAN.md) - Kế hoạch OTA update
