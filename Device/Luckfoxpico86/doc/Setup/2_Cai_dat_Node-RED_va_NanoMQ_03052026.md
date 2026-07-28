# HƯỚNG DẪN CÀI ĐẶT & CẤU HÌNH NODE-RED + NANOMQ (LUCKFOX PICO)

## 1. Cập nhật hệ thống và cài ntp

Trước tiên, hãy đảm bảo hệ thống có quyền quản trị và danh sách gói mới nhất và time mới nhất:

```bash
date
sudo apt-get update
sudo apt install ntp -y
sudo systemctl status ntp
ntpq -p
```

## 2. Cài đặt Node-RED

Sử dụng script cài đặt tự động cho các dòng máy Linux ARM:

```bash
# Chạy lệnh cài đặt (mất khoảng 5-10 phút)
sudo su
bash <(curl -sL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered)

# Thiết lập tự khởi động cùng board
sudo systemctl enable nodered.service
sudo systemctl start nodered.service
```

- **Truy cập UI:** http://172.32.0.70:1880/

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

- **Kiểm tra trạng thái:** `sudo systemctl status nanomq`
- **Theo dõi log trực tiếp:** `sudo journalctl -u nanomq -f`
- **Kiểm tra phiên bản:** `nanomq --version`
- **Kiểm tra cổng 1883 (MQTT):** `netstat -tln | grep 1883`

## 5. Kết nối từ Node-RED

Trong giao diện Node-RED, bạn sử dụng node **mqtt in** hoặc **mqtt out** với cấu hình sau:

- **Server:** 127.0.0.1
- **Port:** 1883
- **Topic:** luckfox/data (hoặc tùy ý)

### Lưu ý cho Luckfox Pico:

- Nếu bạn dùng bản **Static Binary** (.tar.gz), hãy đảm bảo đã cấp quyền chạy bằng lệnh: `sudo chmod +x /usr/local/bin/nanomq`.
- Luôn dùng sudo khi can thiệp vào các file trong thư mục /etc/ hoặc /usr/.
