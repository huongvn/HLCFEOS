# TÀI LIỆU KỸ THUẬT: CẤU HÌNH WIFI ACCESS POINT TRÊN LUCKFOX (AIC8800)

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
