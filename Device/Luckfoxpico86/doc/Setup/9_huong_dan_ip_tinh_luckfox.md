# HƯỚNG DẪN CÀI ĐẶT IP TĨNH

LuckFox Pico – Buildroot Linux

Ngày: 25/3/2026

| Thiết bị | LuckFox Pico |
| --- | --- |
| Hệ điều hành | Linux (Buildroot) |
| Card mạng | eth0 |
| IP tĩnh | 192.168.1.124/24 |
| Gateway | 192.168.1.1 |

## A. Cài Đặt IP Tĩnh

### Bước 1: Xóa IP cũ và đặt IP tĩnh thủ công

Xóa toàn bộ cấu hình IP hiện tại trên eth0, sau đó gán IP tĩnh:

```bash
sudo ip addr flush dev eth0
sudo ip link set eth0 up
sudo ip addr add 192.168.1.124/24 dev eth0
sudo ip route add default via 192.168.1.1
```

**⚠️ Lưu ý:** Thay 192.168.1.1 bằng gateway thực tế. Kiểm tra bằng: `ip route | grep default`

### Bước 2: Sửa file /etc/network/interfaces

Mở file cấu hình mạng:

```bash
sudo nano /etc/network/interfaces
```

Nội dung file sau khi chỉnh sửa:

```
# Include files from /etc/network/interfaces.d:
source /etc/network/interfaces.d/*

auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address 192.168.1.124
    netmask 255.255.255.0
    gateway 192.168.1.1
    dns-nameservers 8.8.8.8 8.8.4.4
```

**⚠️ Lưu ý:** File rất nhạy cảm với thụt lề — dùng space, không dùng Tab. Trên Buildroot dùng vi nếu không có nano.

### Bước 3: Xác nhận kết nối

Kiểm tra IP đã được gán:

```bash
ip addr show eth0
ping -c 3 8.8.8.8
```

### Bước 4: Reboot kiểm tra

Khởi động lại để xác nhận IP tĩnh được giữ sau reboot:

```bash
sudo reboot
```

Sau khi boot lại, kiểm tra lại:

```bash
ip addr show eth0
```

## B. Chuyển Lại Dùng DHCP

### Bước 1: Sửa file /etc/network/interfaces

Mở file cấu hình:

```bash
sudo nano /etc/network/interfaces
```

Đổi cấu hình eth0 thành DHCP:

```
# Include files from /etc/network/interfaces.d:
source /etc/network/interfaces.d/*

auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp
```

### Bước 2: Xóa IP tĩnh cũ và xin IP mới

```bash
sudo ip addr flush dev eth0
sudo ip link set eth0 up
sudo udhcpc -i eth0
```

### Bước 3: Xác nhận

```bash
ip addr show eth0
ping -c 3 8.8.8.8
```

### Bước 4: Reboot kiểm tra

```bash
sudo reboot
ip addr show eth0
```

## C. Tổng Hợp Lệnh

### Cài IP tĩnh – Tất cả lệnh

```bash
# Bước 1: Xóa IP cũ
sudo ip addr flush dev eth0
sudo ip link set eth0 up
sudo ip addr add 192.168.1.124/24 dev eth0
sudo ip route add default via 192.168.1.1

# Bước 2: Cấu hình file interfaces (xem nội dung phần A)
sudo nano /etc/network/interfaces

# Bước 3: Xác nhận
ip addr show eth0 && ping -c 3 8.8.8.8

# Bước 4: Reboot kiểm tra
sudo reboot
```

### Chuyển về DHCP – Tất cả lệnh

```bash
# Bước 1: Sửa file interfaces (đổi inet static → inet dhcp)
sudo nano /etc/network/interfaces

# Bước 2: Áp dụng
sudo ip addr flush dev eth0
sudo ip link set eth0 up
sudo udhcpc -i eth0

# Bước 3: Xác nhận
ip addr show eth0 && ping -c 3 8.8.8.8
```

## D. Bảng So Sánh IP Tĩnh vs DHCP

| **Tiêu chí** | **IP Tĩnh** | **DHCP** |
| --- | --- | --- |
| File interfaces | inet static | inet dhcp |
| IP sau reboot | Cố định | Tự động cấp |
| Cần gateway | Điền thủ công | Tự động |
| DNS | Điền thủ công | Tự động |
| Phù hợp cho | Server, IoT | Môi trường dev |

## E. Lưu Ý Quan Trọng

- Nếu đang SSH vào thiết bị, lệnh flush IP có thể ngắt kết nối — nên có cổng Serial/UART dự phòng.
- Kiểm tra gateway thực tế bằng lệnh: `ip route | grep default`
- Trên Buildroot dùng vi nếu không có nano.
- `networking.service` hiển thị 'active (exited)' là bình thường — script chạy xong và thoát thành công.

*Tài liệu được tạo từ phiên làm việc thực tế trên thiết bị LuckFox Pico – 25/3/2026*
