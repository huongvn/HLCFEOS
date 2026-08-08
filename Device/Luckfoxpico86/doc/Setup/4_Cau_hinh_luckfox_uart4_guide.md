# Hướng Dẫn Enable UART4_M0

Luckfox Pico Ultra W — Không qua GUI

Thiết bị: Luckfox Pico Ultra W | Interface: SSH

## Tổng Quan

Mục tiêu: Enable UART4_M0 trên Luckfox Pico Ultra W bằng dòng lệnh SSH, không cần thao tác qua giao diện luckfox-config GUI.

UART4_M0 sử dụng các chân vật lý sau:

- **GPIO1_B0** — UART4_M0_RX (nhận dữ liệu)
- **GPIO1_B1** — UART4_M0_TX (gửi dữ liệu)

Sau khi enable thành công, device node `/dev/ttyS4` sẽ xuất hiện trong hệ thống.

## Bước 1 — Ghi Cấu Hình vào /etc/luckfox.cfg

File `/etc/luckfox.cfg` lưu trữ trạng thái các peripheral. Ghi đè toàn bộ file với nội dung sạch, bao gồm `UART4_M0_STATUS=1`:

```bash
sudo bash -c 'cat > /etc/luckfox.cfg << EOF
SPI0_M0_CS_ENABLE=1
SPI0_M0_MODE=1
TS_ENABLE=1
UART4_M0_STATUS=1
EOF'
```

> ⚠ **Lưu ý:** Chỉ giữ các entry cần thiết trong file. Không thêm trùng lặp. Không thêm `I2C3_M0_STATUS` nếu `TS_ENABLE=1` vì sẽ gây conflict.

## Bước 2 — Load Cấu Hình

Chạy lệnh load để áp dụng cấu hình vào hệ thống ngay lập tức (không cần reboot):

```bash
sudo luckfox-config load
```

Kết quả mong đợi:

```
/usr/bin/luckfox-config: line 234: warning: command substitution: ignored null byte in input
Complete configuration loading
```

> ⚠ **Lưu ý:** Warning về null byte là bình thường, không ảnh hưởng đến kết quả.

## Bước 3 — Kiểm Tra Kết Quả

Xác nhận device node `/dev/ttyS4` đã xuất hiện:

```bash
ls /dev/ttyS*
```

Kết quả mong đợi:

```
/dev/ttyS1  /dev/ttyS4
```

Kiểm tra nội dung file cấu hình:

```bash
cat /etc/luckfox.cfg
```
Kết quả mong đợi
```
SPI0_M0_CS_ENABLE=1
SPI0_M0_MODE=1
TS_ENABLE=1
UART4_M0_STATUS=1
```
## Tự Động Enable Khi Khởi Động

Lệnh `luckfox-config load` đã được gọi sẵn trong `/etc/rc.local` của Luckfox Pico Ultra W. Không cần thêm gì:

```bash
# Dòng đã có sẵn trong /etc/rc.local:
luckfox-config load
```

Mỗi lần reboot, hệ thống sẽ tự đọc `/etc/luckfox.cfg` và enable UART4_M0 tự động. Có thể xác nhận sau reboot:

```bash
sudo reboot
# Sau khi SSH lại:
ls /dev/ttyS*
```
Kết quả mong đợi: 

`/dev/ttyS4` sẵn sàng sử dụng.

## Xử Lý Lỗi Thường Gặp

### Lỗi: binary file matches

Nguyên nhân: File `/etc/luckfox.cfg` bị corrupt do null byte (thường do dùng `echo >>` nhiều lần).

Giải pháp: Ghi lại toàn bộ file bằng `cat >` (Bước 1) thay vì dùng `>>`.

### Lỗi: TouchScreen is enable, Can't config I2C3

Nguyên nhân: Khi `TS_ENABLE=1`, script sẽ block bất kỳ cấu hình I2C3 nào.

Giải pháp: Xóa dòng `I2C3_M0_STATUS` khỏi file cấu hình nếu đang dùng TouchScreen.

### Không thấy /dev/ttyS4

Kiểm tra lại file cấu hình không bị binary và load thành công với thông báo "Complete configuration loading".

### GUI hiện ra khi chạy lệnh

Không dùng `luckfox-config` trực tiếp không có tham số. Luôn dùng `luckfox-config load` để chạy ở chế độ non-GUI.

## Tóm Tắt Nhanh

Toàn bộ quá trình chỉ cần 2 lệnh:

```bash
# Lệnh 1: Ghi config
sudo bash -c 'cat > /etc/luckfox.cfg << EOF
SPI0_M0_CS_ENABLE=1
SPI0_M0_MODE=1
TS_ENABLE=1
UART4_M0_STATUS=1
EOF'

# Lệnh 2: Apply
sudo luckfox-config load
```

Kết quả: `/dev/ttyS4` sẵn sàng sử dụng.
