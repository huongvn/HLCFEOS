# HUONG DAN CAI DAT MUI GIO

*LuckFox Pico – He dieu hanh Linux*

Ngay: 19/3/2026

## 1. Muc Tieu

Tai lieu nay huong dan cach thay doi mui gio he thong tren thiet bi LuckFox Pico tu Asia/Shanghai (CST +0800) sang Asia/Ho_Chi_Minh (+0700) – mui gio chuan cua Viet Nam.

| **Thiet bi** | LuckFox Pico |
| --- | --- |
| **He dieu hanh** | Linux (Buildroot) |
| **Mui gio ban dau** | Asia/Shanghai (CST, +0800) |
| **Mui gio dich** | Asia/Ho_Chi_Minh (+07, +0700) |
| **Ngay thuc hien** | 19/3/2026 |

## 2. Kiem Tra Trang Thai Ban Dau

Truoc khi thay doi, kiem tra mui gio hien tai bang lenh:

```bash
timedatectl
```

Ket qua hien thi mui gio goc (Asia/Shanghai):

```
Local time: Thu 2026-03-19 16:03:38 CST
Universal time: Thu 2026-03-19 08:03:38 UTC
RTC time: Thu 2026-03-19 08:03:38
Time zone: Asia/Shanghai (CST, +0800)
System clock synchronized: no
NTP service: n/a
RTC in local TZ: no
```

**Luu y:** Mui gio mac dinh la Asia/Shanghai – can doi sang Asia/Ho_Chi_Minh cho phu hop voi gio Viet Nam.

## 3. Thay Doi Mui Gio

### 3.1. Su dung timedatectl (Cach uu tien)

Chay lenh sau de doi mui gio:

```bash
sudo timedatectl set-timezone Asia/Ho_Chi_Minh
```

### 3.2. Sua truc tiep symlink (Phong khi bi reset)

Kiem tra symlink hien tai:

```bash
ls -la /etc/localtime
```

Neu symlink van tro ve Asia/Shanghai, cap nhat bang:

```bash
# Cap nhat symlink
sudo ln -sf /usr/share/zoneinfo/Asia/Ho_Chi_Minh /etc/localtime

# Cap nhat file /etc/timezone
echo "Asia/Ho_Chi_Minh" | sudo tee /etc/timezone
```

Kiem tra lại:

```bash
ls -la /etc/localtime
```

Hiện như dưới là thành công 

```
lrwxrwxrwx 1 root root 36 Aug  1 16:48 /etc/localtime -> /usr/share/zoneinfo/Asia/Ho_Chi_Minh
```


**Luu y:** Tren LuckFox Pico, `/etc/localtime` mac dinh tro ve `/usr/share/zoneinfo/Asia/Shanghai`. Can sua ca hai file de cai dat ben vung qua reboot.

## 4. Dong Bo Thoi Gian Qua NTP

1.Cài đặt chrony:Cập nhật danh sách gói và cài đặt chrony:
```
sudo apt update
sudo apt install chrony -y
```
2.Bật và kiểm tra dịch vụ:Kích hoạt service để chrony tự động chạy mỗi khi khởi động:
```
sudo systemctl enable --now chrony
```
Kiểm tra trạng thái đồng bộ thời gian:
```
chronyc tracking
```

kết quả mogn đợi 
```
Reference ID    : 2DFCFABD (45.252.250.189)
Stratum         : 4
Ref time (UTC)  : Sat Aug 01 15:08:43 2026
System time     : 0.000158123 seconds slow of NTP time
Last offset     : -0.000583984 seconds
RMS offset      : 0.000583984 seconds
Frequency       : 42.321 ppm slow
Residual freq   : -17.691 ppm
Skew            : 1000000.000 ppm
Root delay      : 0.064349540 seconds
Root dispersion : 43.661857605 seconds
Update interval : 57.1 seconds
Leap status     : Normal
```


## 5. Luu Thoi Gian Vao RTC

Sau khi dong bo NTP, luu vao dong ho phan cung (RTC) de giu gio dung khi reboot:

```
sudo hwclock -w
```

## 6. Xac Nhan Ket Qua
```
date && timedatectl
```

## 8. Bang Mui Gio Tham Khao

| **Quoc gia / Khu vuc** | **Timezone ID** | **UTC Offset** |
| --- | --- | --- |
| **Viet Nam [Dang dung]** | Asia/Ho_Chi_Minh | +07:00 |

