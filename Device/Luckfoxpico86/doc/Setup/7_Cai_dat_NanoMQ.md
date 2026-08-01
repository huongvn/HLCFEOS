# HƯỚNG DẪN CÀI ĐẶT NANOMQ (LUCKFOX PICO)


## Cài đặt NanoMQ (MQTT Broker)

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
sudo systemctl status nanomq
```

kết quả mong đợi 
```
pico@luckfox:~$ sudo systemctl status nanomq
● nanomq.service
     Loaded: loaded (/etc/systemd/system/nanomq.service; enabled; vendor preset: enabled)
     Active: active (running) since Sun 2026-08-02 00:36:21 +07; 18s ago
   Main PID: 4018 (nanomq)
        CPU: 30ms
     CGroup: /system.slice/nanomq.service
             └─4018 /usr/local/bin/nanomq start

```

## 4. Các lệnh kiểm tra & Theo dõi (Log)

Đây là các lệnh quan trọng để bạn quản lý hệ thống sau này:

### NanoMQ

- **Kiểm tra trạng thái:** `sudo systemctl status nanomq`
- **Theo dõi log trực tiếp:** `sudo journalctl -u nanomq -f`
- **Kiểm tra phiên bản:** `nanomq --version`
- **Kiểm tra cổng 1883 (MQTT):** `netstat -tln | grep 1883`
- 
## 5. Kiểm tra kết nối MQTT

```bash
# Subscribe tất cả topics để xem messages
mosquitto_sub -h localhost -p 1883 -t "#" -v

# Subscribe chỉ topics từ Tasmota gateway
mosquitto_sub -h localhost -p 1883 -t "tele/tasmota_6DCAA8/#" -v

# Test publish command
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/power/set" -m "ON"
```

### Lưu ý cho Luckfox Pico:

- Nếu bạn dùng bản **Static Binary** (.tar.gz) cho NanoMQ, hãy đảm bảo đã cấp quyền chạy bằng lệnh: `sudo chmod +x /usr/local/bin/nanomq`.
- Luôn dùng sudo khi can thiệp vào các file trong thư mục /etc/ hoặc /usr/.
- Python BMS Engine tự động kết nối đến NanoMQ tại `localhost:1883`.

## 7. Tài liệu tham khảo

Xem thêm tài liệu chi tiết tại:
- [python_engine/README.md](../../code/python_engine/README.md) - Hướng dẫn sử dụng Python BMS Engine
- [python_engine/PlanAndDoc/ARCHITECTURE.md](../../code/python_engine/PlanAndDoc/ARCHITECTURE.md) - Kiến trúc hệ thống
- [python_engine/PlanAndDoc/OTA_PLAN.md](../../code/python_engine/PlanAndDoc/OTA_PLAN.md) - Kế hoạch OTA update
