# HƯỚNG DẪN CÀI ĐẶT — OTA Update Server

Nginx Static File Server trên Ubuntu

| **Hệ điều hành** | Ubuntu 22.04 / 24.04 |
| --- | --- |
| **Web server** | Nginx 1.24+ |
| **Mục đích** | Serve firmware binary và metadata cho LuckFox Pico |
| **Ngày tạo** | 24/3/2026 |

## 1. Mục Tiêu

Tài liệu này hướng dẫn cách cài đặt và cấu hình một Static File Server bằng Nginx trên máy Ubuntu. Server này có nhiệm vụ host 2 file phục vụ cho quá trình OTA update của thiết bị LuckFox Pico:

- **check.json** — File metadata chứa version và SHA256 hash của firmware mới nhất
- **app_v1.2.3** — File binary C++ đã build, LuckFox tải về khi có bản mới

| **Ubuntu (OTA Server)** | **LuckFox Pico (Client)** |
| --- | --- |
| Nginx serve file tại: `/ota/check.json`, `/ota/app_v1.2.3` | OTA Agent poll định kỳ: `GET /ota/check.json`, `wget /ota/app_vX.X.X` |

## 2. Yêu Cầu

### 2.1. Phần cứng / Phần mềm

- Máy Ubuntu 22.04 hoặc 24.04 (có kết nối mạng LAN với LuckFox)
- Quyền sudo trên máy Ubuntu
- File binary C++ đã build (ví dụ: app_v1.2.3)
- File metadata check.json đã chuẩn bị sẵn

## 3. Các Bước Cài Đặt

### 3.1. Cài Nginx

Chạy 2 lệnh sau để cài Nginx:

```bash
sudo apt update
sudo apt install nginx -y
```

Kiểm tra Nginx đã chạy chưa:

```bash
sudo systemctl status nginx
```

**Lưu ý:** Nếu thấy dòng 'Active: active (running)' là Nginx đã khởi động thành công.

### 3.2. Tạo Thư Mục Chứa File OTA

Tạo cấu trúc thư mục để Nginx serve file:

```bash
sudo mkdir -p /var/www/ota_root/ota
sudo chown -R $USER:$USER /var/www/ota_root
```

| **Lệnh** | **Giải thích** |
| --- | --- |
| `mkdir -p /var/www/ota_root/ota` | Tạo thư mục, -p tự tạo thư mục cha nếu chưa có |
| `chown -R $USER:$USER ...` | Đổi chủ sở hữu về user hiện tại để không cần sudo khi copy file |

### 3.3. Copy File OTA vào Thư Mục

Sau khi tạo thư mục, copy 2 file vào:

```bash
cp app_v1.2.3 /var/www/ota_root/ota/
cp check.json /var/www/ota_root/ota/
```

Kiểm tra file đã có đúng chỗ:

```bash
ls /var/www/ota_root/ota/
# Kết quả mong đợi:
# app_v1.2.3  check.json
```

### 3.4. Cấu Hình check.json

File check.json phải có đúng định dạng sau. LuckFox đọc file này để quyết định có cần update không:

```json
{
    "version"  : "1.2.3",
    "filename" : "app_v1.2.3",
    "sha256"   : "9d23da93c8ff3b70dfc05c5524b34515...",
    "url"      : "http://192.168.1.171/ota/app_v1.2.3"
}
```

Lấy SHA256 của file binary bằng lệnh:

```bash
sha256sum app_v1.2.3
```

**Lưu ý:** Mỗi lần build bản mới, phải cập nhật cả 3 field: version, filename, sha256 và url trong check.json.

### 3.5. Cấu Hình Nginx

Tạo file config cho Nginx:

```bash
sudo nano /etc/nginx/sites-available/ota
```

Dán nội dung sau vào:

```nginx
server {
    listen 80;
    server_name _;
    root /var/www/ota_root;
    autoindex on;
    location / {
        try_files $uri =404;
    }
}
```

Lưu file: Ctrl+O → Enter → Ctrl+X

Kích hoạt config và khởi động lại Nginx:

```bash
sudo ln -s /etc/nginx/sites-available/ota /etc/nginx/sites-enabled/
sudo rm /etc/nginx/sites-enabled/default
sudo nginx -t
sudo systemctl restart nginx
```

## 4. Kiểm Tra Sau Cài Đặt

### 4.1. Kiểm Tra Trên Máy Ubuntu

Chạy lần lượt các lệnh sau trên máy Ubuntu:

```bash
# Kiểm tra check.json
curl http://localhost/ota/check.json

# Kiểm tra file binary (chỉ lấy header, không tải cả file)
curl -I http://localhost/ota/app_v1.2.3
```

**Kết quả mong đợi:**
- `curl check.json` → hiển thị nội dung JSON
- `curl -I app_v1.2.3` → HTTP/1.1 200 OK, Content-Type: application/octet-stream

### 4.2. Kiểm Tra Từ LuckFox Pico

Trên LuckFox, thay 192.168.1.171 bằng IP thực của máy Ubuntu:

```bash
wget http://192.168.1.171/ota/check.json -O -
```

**Lưu ý:** Dùng lệnh `ip addr` trên Ubuntu để xem IP thực của máy.

## 5. Cấu Trúc Thư Mục Hoàn Chỉnh

```
/var/www/ota_root
└── ota/
    ├── check.json        ← metadata: version, sha256, url
    └── app_v1.2.3        ← binary C++ đã build
```

URL tương ứng từ LuckFox:

```
http://192.168.1.171/ota/check.json
http://192.168.1.171/ota/app_v1.2.3
```

## 6. Quy Trình Deploy Bản Firmware Mới

Mỗi khi có bản firmware mới, thực hiện theo thứ tự sau:

| **STT** | **Hành động** | **Lệnh** |
| --- | --- | --- |
| 1 | Build binary mới | `g++ main.cpp -o app_v1.2.4` |
| 2 | Lấy SHA256 của file mới | `sha256sum app_v1.2.4` |
| 3 | Cập nhật check.json | Sửa version, filename, sha256, url |
| 4 | Copy file lên server | `cp app_v1.2.4 /var/www/ota_root/ota/` |
| 5 | Copy check.json mới lên | `cp check.json /var/www/ota_root/ota/` |
| 6 | Xác nhận server OK | `curl http://localhost/ota/check.json` |

## 7. Xử Lý Lỗi Thường Gặp

| **Lỗi** | **Nguyên nhân** | **Cách sửa** |
| --- | --- | --- |
| 404 Not Found | Config Nginx chưa đúng hoặc file chưa copy vào đúng thư mục | Kiểm tra root trong config, chạy `nginx -t` |
| 403 Forbidden | Quyền truy cập file bị sai | `chmod 644 /var/www/ota_root/ota/*` |
| nginx -t báo lỗi | Config file có lỗi cú pháp | Xem dòng báo lỗi, kiểm tra lại dấu ; và {} |
| Kết nối từ LuckFox bị từ chối | Firewall Ubuntu chặn port 80 | `sudo ufw allow 80` |

*Tài liệu được tạo từ phiên làm việc thực tế — OTA Server Setup — 24/3/2026*
