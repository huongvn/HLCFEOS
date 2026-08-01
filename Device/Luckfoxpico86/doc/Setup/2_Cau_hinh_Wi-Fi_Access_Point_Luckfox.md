# TÀI LIỆU KỸ THUẬT: CẤU HÌNH Wi-Fi Luckfox Client Mode

## Mục đích
Chuyển Luckfox Core1106 Smart 86 Box từ chế độ phát Wi-Fi Access Point (hotspot) sang chế độ Wi-Fi Client, cho phép thiết bị kết nối tới mạng Wi-Fi có sẵn (ví dụ: Wi-Fi quán cafe, Wi-Fi văn phòng) để truy cập internet và giao tiếp với backend.

---

## 1. Kiểm tra trạng thái hiện tại

```bash
# Xem trạng thái các interface mạng
nmcli device status

# Kiểm tra hostapd có đang chạy không
ps aux | grep hostapd

# Xem file cấu hình mạng
cat /etc/network/interfaces

# Kiểm tra rc.local (nơi chứa script AP tự khởi động)
cat /etc/rc.local
```

Dấu hiệu đang ở AP mode:
- `wlan0` có IP tĩnh `192.168.10.1`
- Tiến trình `hostapd` và `dnsmasq` đang chạy
- Trong `/etc/rc.local` có block "Wi-Fi Access Point Setup"

---


## 3. Bước 2: Cấu hình giữ IP tĩnh cho eth0 (cáp LAN)

Sửa file `/etc/network/interfaces`:

```bash
sudo nano /etc/network/interfaces
```

Nội dung chuẩn:

```
# interfaces(5) file used by ifup(8) and ifdown(8)
source /etc/network/interfaces.d/*

auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address 192.168.1.124
    netmask 255.255.255.0
    dns-nameservers 8.8.8.8 8.8.4.4
```

> **Quan trọng:** Bỏ dòng `gateway 192.168.1.1` khỏi eth0 nếu có. Nếu giữ gateway trên eth0, toàn bộ traffic internet sẽ bị đẩy qua cáp LAN thay vì Wi-Fi, gây lỗi `Destination Host Unreachable`.

eth0 vẫn giữ IP tĩnh `192.168.1.124`, dùng để:
- SSH vào Luckfox từ máy trong cùng mạng LAN `192.168.1.x`
- Truy cập Node-RED, MQTT broker, các service nội bộ
- Debug và bảo trì khi Wi-Fi gặp sự cố

---

## 4. Bước 3: Kết nối Wi-Fi bằng NetworkManager

Trên Luckfox, **NetworkManager** đang quản lý các interface mạng. Dùng `nmcli` để kết nối nhanh và lưu profile vĩnh viễn.

### 4.1. Quét danh sách Wi-Fi khả dụng

```bash
nmcli dev wifi list
```

### 4.2. Kết nối tới Wi-Fi (tự động lưu profile)

```bash
sudo nmcli dev wifi connect "TEN_WIFI" password "MAT_KHAU"
```

Ví dụ:

```bash
sudo nmcli dev wifi connect "htlhome" password "1357924680"
```

NetworkManager sẽ:
- Quét và kết nối tới SSID được chỉ định
- Tự động lưu connection profile → **tự động reconnect sau reboot**
- Nhận IP từ DHCP của router Wi-Fi

### 4.3. Kiểm tra kết nối

# Xem trạng thái interface
```
nmcli device status
```

Kết quả mong đợi:
```
DEVICE  TYPE      STATE                   CONNECTION
eth0    ethernet  connected (externally)  eth0
wlan0   wifi      connected               htlhome
```

# Xem IP đã nhận
```
ip addr show wlan0
```

3: wlan0: <BROADCAST,MULTICAST,UP,LOWER_UP> ...
```
    inet 192.168.10.51/24 ...
```

# Test internet
```
ping 8.8.8.8
```
kêt quả như dưới là đã có internet
```
PING 8.8.8.8 (8.8.8.8): 56 data bytes
64 bytes from 8.8.8.8: icmp_seq=0 ttl=108 time=46.784 ms
64 bytes from 8.8.8.8: icmp_seq=1 ttl=108 time=47.126 ms
64 bytes from 8.8.8.8: icmp_seq=2 ttl=108 time=50.668 ms
64 bytes from 8.8.8.8: icmp_seq=3 ttl=108 time=46.476 ms
64 bytes from 8.8.8.8: icmp_seq=4 ttl=108 time=47.371 ms
```
---

## 5. Bước 4: Reboot và xác nhận tự động kết nối

```bash
sudo reboot
```

Sau khi reboot, SSH lại và kiểm tra:

```bash
nmcli device status
ping 8.8.8.8
```

Nếu `wlan0` hiển thị `connected` và ping thành công → cấu hình đã bền vững.

---

## 6. Chuyển sang Wi-Fi khác

NetworkManager tự động lưu tất cả các Wi-Fi đã từng kết nối. Để chuyển sang Wi-Fi khác:

```bash
sudo nmcli dev wifi connect "TEN_WIFI_MOI" password "MAT_KHAU_MOI"
```

---

## 7. Các lệnh quản lý Wi-Fi hữu ích

| Nhu cầu | Lệnh |
|---------|------|
| Quét Wi-Fi xung quanh | `nmcli dev wifi list` |
| Xem danh sách kết nối đã lưu | `nmcli connection show` |
| Chuyển về Wi-Fi đã lưu trước đó | `sudo nmcli connection up "htlhome"` |
| Xóa một kết nối đã lưu | `sudo nmcli connection delete "htlhome"` |
| Xem thông tin chi tiết kết nối | `nmcli connection show "htlhome"` |
| Ngắt kết nối Wi-Fi | `sudo nmcli device disconnect wlan0` |
| Kết nối lại Wi-Fi đã lưu | `sudo nmcli device connect wlan0` |
| Xem trạng thái tất cả thiết bị | `nmcli device status` |

---


## 8. Xử lý sự cố

### 8.1. `dhclient` treo không lấy được IP

Nguyên nhân: wlan0 chưa kết nối tới AP nào, không có DHCP server trả lời.

```bash
# Nhấn Ctrl+C để thoát dhclient
# Kiểm tra kết nối Wi-Fi
iw dev wlan0 link

# Nếu hiện "Not connected" → kiểm tra lại SSID/mật khẩu
```

### 8.2. Có IP nhưng không ping được internet

Nguyên nhân: Default route đang đi qua eth0 thay vì wlan0.

```bash
# Kiểm tra bảng định tuyến
ip route show

# Nếu thấy "default via 192.168.1.1 dev eth0" → sửa:
sudo ip route del default via 192.168.1.1
sudo ip route add default via 192.168.10.1 dev wlan0
```

Giải pháp lâu dài: Bỏ dòng `gateway` khỏi eth0 trong `/etc/network/interfaces` (xem Bước 2).

### 8.3. Sau reboot, Wi-Fi không tự kết nối

```bash
# Kiểm tra block AP trong rc.local đã được comment chưa
cat /etc/rc.local | grep -A 10 "Access Point"

# Kiểm tra NetworkManager connection
nmcli connection show

# Nếu không có Wi-Fi connection → tạo lại bằng nmcli
sudo nmcli dev wifi connect "TEN_WIFI" password "MAT_KHAU"
```

### 8.4. Lỗi `wpa_supplicant` conflict với NetworkManager

Hiện tượng: `ctrl_iface exists and seems to be in use`

```bash
# Không cần chạy wpa_supplicant thủ công nếu đã dùng NetworkManager
# Nếu cần xóa file lock cũ:
sudo rm /var/run/wpa_supplicant/wlan0
```

> **Khuyến nghị:** Dùng NetworkManager (`nmcli`) để quản lý Wi-Fi, không cần cấu hình `wpa_supplicant.conf` thủ công nếu NetworkManager đang chạy.

---

## Tóm tắt nhanh

| Bước | Lệnh |
|------|------|
| 1. Tắt AP | Comment block trong `/etc/rc.local`, `sudo killall hostapd dnsmasq` |
| 2. Sửa interfaces | Bỏ `gateway` khỏi eth0, giữ IP tĩnh |
| 3. Kết nối Wi-Fi | `sudo nmcli dev wifi connect "SSID" password "PASS"` |
| 4. Kiểm tra | `ping 8.8.8.8` |
| 5. Reboot test | `sudo reboot` |

---
---
---

# (OPTION) TÀI LIỆU KỸ THUẬT: CẤU HÌNH WIFI ACCESS POINT TRÊN LUCKFOX (AIC8800)

Tài liệu này hướng dẫn chi tiết cách thiết lập điểm phát sóng Wi-Fi từ cấp độ driver đến tầng ứng dụng, bao gồm cả việc gán định danh duy nhất (Unique ID) cho từng bo mạch.

## 1. Thành phần hệ thống (Stack Overview)

Để một thiết bị Linux nhúng phát được Wi-Fi, cần sự phối hợp của 4 tầng:

1. **Hardware/Driver:** Chip AIC8800 giao tiếp qua bus SDIO (aicwf_sdio).
2. **Network Interface:** Giao diện wlan0 được cấu hình IP tĩnh làm Gateway.
3. **Hostapd (Layer 2):** Quản lý xác thực WPA2 và đóng gói khung tin 802.11.
4. **Dnsmasq (Layer 3):** Cấp phát địa chỉ IP (DHCP) cho các Client kết nối vào.

## 2. Chi tiết cấu hình dịch vụ

### 2.0 Cài đặt các gói phần mềm

```bash
sudo apt update && sudo apt install iw hostapd dnsmasq
hostapd -v
dnsmasq -v
```

### 2.1. Cấu hình tầng vật lý & bảo mật (hostapd.conf)

Tạo file tại:

```bash
sudo nano /home/pico/luckfox_ap.conf
```

Chèn vào dưới cùng:

```ini
interface=wlan0
driver=nl80211
ssid=TEMP_SSID
hw_mode=g
channel=6
wpa=2
wpa_passphrase=12345678
wpa_key_mgmt=WPA-PSK
wpa_pairwise=CCMP
rsn_pairwise=CCMP
```

### 2.2. Cấu hình tầng mạng & IP (dnsmasq.conf)

Tạo file tại:

```bash
sudo nano /home/pico/dnsmasq.conf
```

Chèn vào dưới cùng:

```ini
port=0
interface=wlan0
dhcp-range=192.168.10.10,192.168.10.50,255.255.255.0,12h
dhcp-option=3,192.168.10.1
dhcp-option=6,8.8.8.8,8.8.4.4
```

## 3. Script khởi động và tùy biến SSID theo MAC

Đây là phần quan trọng nhất để quản lý nhiều bo mạch. Script này thực hiện lấy phần định danh vật lý của chip Wi-Fi để làm tên sóng.

### 3.1. Chèn vào /etc/rc.local

Sử dụng quyền root để sửa file:

```bash
sudo nano /etc/rc.local
```

Chèn vào dưới cùng:

```bash
(
sleep 10
MAC_SUFFIX=$(cat /sys/class/net/wlan0/address | sed 's/://g' | tail -c 7)
NEW_SSID="Luckfox_$MAC_SUFFIX"
sed -i "s/^ssid=.*/ssid=$NEW_SSID/" /home/pico/luckfox_ap.conf
ifconfig wlan0 192.168.10.1 netmask 255.255.255.0 up
hostapd -B /home/pico/luckfox_ap.conf
dnsmasq -C /home/pico/dnsmasq.conf
) &
exit 0
```

## 4. Các lệnh quản trị thực tế (Cheat Sheet)

| **Tác vụ** | **Lệnh thực hiện** |
| --- | --- |
| Kiểm tra trạng thái phát sóng | `iwconfig wlan0` |
| Xem danh sách thiết bị đã kết nối | `cat /var/lib/misc/dnsmasq.leases` |
| Xem log của Hostapd (nếu lỗi) | `sudo hostapd /home/pico/luckfox_ap.conf` (bỏ -B) |
| Kiểm tra cường độ tín hiệu Client | `iw dev wlan0 station dump` |
| Khởi động lại dịch vụ Wi-Fi | `sudo killall hostapd dnsmasq && sudo /etc/rc.local` |

## 5. Xử lý sự cố (Troubleshooting)

1. **Điện thoại thấy SSID nhưng báo "IP Configuration Failure":**
   - Nguyên nhân: dnsmasq chưa chạy hoặc xung đột port 53.
   - Xử lý: Kiểm tra `ps aux | grep dnsmasq`. Đảm bảo file config có `port=0`.

2. **SSID không xuất hiện sau khi Reboot:**
   - Nguyên nhân: File /etc/rc.local chưa có quyền thực thi.
   - Xử lý: `sudo chmod +x /etc/rc.local`.

3. **Tín hiệu Wi-Fi yếu (chỉ thấy khi ở gần):**
   - Nguyên nhân: Chưa gắn Antenna vào cổng IPEX trên bo mạch Luckfox.
   - Xử lý: Gắn antenna 2.4GHz phù hợp.
  
