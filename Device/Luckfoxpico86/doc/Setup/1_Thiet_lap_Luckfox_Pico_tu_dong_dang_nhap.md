# HƯỚNG DẪN HOÀN THIỆN HỆ THỐNG LUCKFOX PICO

*(Cập nhật ngày 05/03/2026)*

Cắm dây USB-C vào board, sử dụng tool hoặc cmd để ssh vào board.

- **IP:** 172.32.0.70
- **Name:** pico
- **Pass:** luckfox@1234

## 1. Tự động đăng nhập (Auto-login)

Để hệ thống bỏ qua màn hình yêu cầu Username/Password và vào thẳng giao diện dòng lệnh:

```bash
# Tạo thư mục cấu hình nếu chưa có
sudo mkdir -p /etc/systemd/system/getty@tty1.service.d/

# Ghi nội dung trực tiếp vào file override.conf
echo -e "[Service]\nExecStart=\nExecStart=-/sbin/agetty --autologin pico --noclear %I \$TERM" | sudo tee /etc/systemd/system/getty@tty1.service.d/override.conf

# Load lại cấu hình và khởi động lại dịch vụ
sudo systemctl daemon-reload
sudo systemctl restart getty@tty1
```

## (Option) Tự động chạy ứng dụng (htop Kiosk Mode)

Để biến màn hình thành bảng giám sát ngay khi khởi động:

1. Thêm đoạn mã sau vào cuối file bashrc:

```bash
sudo nano ~/.bashrc

#if [ $(tty) = "/dev/tty1" ]; then
# htop
#fi

! Bỏ dấu # để thực thi lệnh
```

2. **Lưu ý:** Nếu muốn thoát htop để gõ lệnh, nhấn phím **F10** hoặc **q**.
