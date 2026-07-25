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

**Luu y:** Tren LuckFox Pico, `/etc/localtime` mac dinh tro ve `/usr/share/zoneinfo/Asia/Shanghai`. Can sua ca hai file de cai dat ben vung qua reboot.

## 4. Dong Bo Thoi Gian Qua NTP

### 4.1. Van de voi timedatectl set-ntp

LuckFox Pico khong ho tro NTP truc tiep qua timedatectl:

```bash
sudo timedatectl set-ntp true
```

```
Failed to set ntp: NTP not supported
```

### 4.2. Su dung ntpdate voi flag -u

Dung ntpdate voi flag -u de tranh xung dot voi tien trinh ntpd dang chay nen:

```bash
sudo ntpdate -u pool.ntp.org
```

Ket qua dong bo thanh cong:

```
19 Mar 16:07:30 ntpdate[3802]: adjust time server 103.72.56.71 offset +0.009649 sec
```

**Luu y:** Loi 'NTP socket is in use' xay ra khi ntpd dang chiem port 123. Dung flag -u (unprivileged port) de bypass, hoac dung ntpd truoc: `sudo systemctl stop ntpd`

## 5. Luu Thoi Gian Vao RTC

Sau khi dong bo NTP, luu vao dong ho phan cung (RTC) de giu gio dung khi reboot:

```bash
sudo hwclock -w
```

## 6. Xac Nhan Ket Qua

Ket qua sau khi hoan tat cai dat:

```
Local time: Thu 2026-03-19 15:10:10 +07
Universal time: Thu 2026-03-19 08:10:10 UTC
RTC time: Thu 2026-03-19 08:10:11
Time zone: Asia/Ho_Chi_Minh (+07, +0700)
System clock synchronized: yes
NTP service: n/a
RTC in local TZ: no
```

- [OK] Time zone: Asia/Ho_Chi_Minh (+07, +0700) – Mui gio da dung
- [OK] System clock synchronized: yes – Dong ho da duoc dong bo
- [OK] Local time hien thi +07 thay vi CST

## 7. Tom Tat Toan Bo Lenh

Thuc hien theo thu tu cac buoc sau:

```bash
# Buoc 1: Doi mui gio
sudo timedatectl set-timezone Asia/Ho_Chi_Minh

# Buoc 2: Sua symlink va /etc/timezone (phong reset)
sudo ln -sf /usr/share/zoneinfo/Asia/Ho_Chi_Minh /etc/localtime
echo "Asia/Ho_Chi_Minh" | sudo tee /etc/timezone

# Buoc 3: Dong bo thoi gian NTP
sudo ntpdate -u pool.ntp.org

# Buoc 4: Luu vao RTC
sudo hwclock -w

# Buoc 5: Kiem tra
date && timedatectl
```

## 8. Bang Mui Gio Tham Khao

| **Quoc gia / Khu vuc** | **Timezone ID** | **UTC Offset** |
| --- | --- | --- |
| **Viet Nam [Dang dung]** | Asia/Ho_Chi_Minh | +07:00 |
| Thai Lan | Asia/Bangkok | +07:00 |
| Trung Quoc | Asia/Shanghai | +08:00 |
| Nhat Ban | Asia/Tokyo | +09:00 |
| Han Quoc | Asia/Seoul | +09:00 |
| Singapore | Asia/Singapore | +08:00 |
| UTC | Etc/UTC | +00:00 |

*Tai lieu duoc tao tu phien lam viec thuc te tren thiet bi LuckFox Pico – 19/3/2026*
