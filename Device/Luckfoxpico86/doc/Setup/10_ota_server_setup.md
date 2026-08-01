# HƯỚNG DẪN CÀI ĐẶT — OTA Update Server cho LVGL App và Python BMS Engine

Nginx Static File Server trên Ubuntu - Dùng chung cho cả 2 ứng dụng

| **Hệ điều hành** | Ubuntu 22.04 / 24.04 |
| --- | --- |
| **Web server** | Nginx 1.24+ |
| **Mục đích** | Serve OTA packages cho LVGL App và Python BMS Engine trên LuckFox Pico |
| **Ngày tạo** | 24/3/2026 |
| **Cập nhật** | 28/7/2026 - Hỗ trợ cả LVGL App và Python BMS Engine |

## 1. Mục Tiêu

Tài liệu này hướng dẫn cách cài đặt và cấu hình một Static File Server bằng Nginx trên máy Ubuntu. Server này host OTA packages cho **cả 2 ứng dụng** trên LuckFox Pico:

### LVGL App (C++ Binary)
- **check.json** — File metadata chứa version, SHA256 hash, và URL của binary mới nhất
- **app_v1.2.3** — LVGL App binary, LuckFox tải về khi có bản mới

### Python BMS Engine
- **bms/check.json** — File metadata chứa version, SHA256 hash, và URL của package mới nhất
- **bms/bms_v1.0.0.tar.gz** — Python BMS Engine package (tarball), LuckFox tải về khi có bản mới

| **Ubuntu (OTA Server)** | **LuckFox Pico (Client)** |
| --- | --- |
| Nginx serve file tại: `/ota/check.json`, `/ota/app_v1.2.3`, `/ota/bms/check.json`, `/ota/bms/bms_v1.0.0.tar.gz` | OTA Agent poll định kỳ: `GET /ota/check.json` (LVGL), `GET /ota/bms/check.json` (BMS) |

**Lợi ích của việc dùng chung server:**
- ✅ Đơn giản hóa quản lý - Chỉ 1 Nginx server, 1 IP
- ✅ Tiết kiệm tài nguyên - Không cần chạy 2 server riêng
- ✅ Dễ backup - Tất cả OTA files ở cùng 1 chỗ
- ✅ Unified access - Cùng firewall rules, cùng SSL cert (nếu dùng HTTPS)

## 2. Yêu Cầu

### 2.1. Phần cứng / Phần mềm

- Máy Ubuntu 22.04 hoặc 24.04 (có kết nối mạng LAN với LuckFox)
- Quyền sudo trên máy Ubuntu
- **Cho LVGL App:** Binary đã build (file `app_v1.2.3`) và metadata `check.json`
- **Cho Python BMS Engine:** Package đã build (file `bms_v1.0.0.tar.gz`) và metadata `check.json`

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

Tạo cấu trúc thư mục để Nginx serve file cho cả 2 ứng dụng:

```bash
# Tạo thư mục cho LVGL App (root của /ota/)
sudo mkdir -p /var/www/ota_root/ota

# Tạo thư mục cho Python BMS Engine (/ota/bms/)
sudo mkdir -p /var/www/ota_root/ota/bms

# Phân quyền
sudo chown -R $USER:$USER /var/www/ota_root
```

| **Lệnh** | **Giải thích** |
| --- | --- |
| `mkdir -p /var/www/ota_root/ota` | Tạo thư mục cho LVGL App OTA |
| `mkdir -p /var/www/ota_root/ota/bms` | Tạo thư mục cho Python BMS Engine OTA |
| `chown -R $USER:$USER ...` | Đổi chủ sở hữu về user hiện tại để không cần sudo khi copy file |

### 3.3. Build và Deploy Package OTA

#### A. Deploy LVGL App

**Trên máy development (build binary):**

```bash
cd /path/to/HLCFEOS/Device/Luckfoxpico86/code/lvgl_project

# Build binary version 1.2.3
make APP_VERSION=1.2.3

# Kết quả:
# - ota/app_v1.2.3 (binary)
# - ota/check.json (manifest)
```

**Deploy lên OTA server:**

```bash
# Copy binary và manifest lên server
scp ota/app_v1.2.3 user@192.168.1.171:/var/www/ota_root/ota/
scp ota/check.json user@192.168.1.171:/var/www/ota_root/ota/
```

Kiểm tra file đã có đúng chỗ:

```bash
ls -lh /var/www/ota_root/ota/
# Kết quả mong đợi:
# app_v1.2.3  check.json
```

#### B. Deploy Python BMS Engine

**Trên máy development (build package):**

```bash
cd /path/to/HLCFEOS/Device/Luckfoxpico86/code/python_engine

# Build package version 1.0.0
python3 build_ota.py 1.0.0

# Kết quả:
# - ota/bms_v1.0.0.tar.gz (package)
# - ota/check.json (manifest)
```

**Deploy lên OTA server:**

```bash
# Copy package và manifest lên server
scp ota/bms_v1.0.0.tar.gz user@192.168.1.171:/var/www/ota_root/ota/bms/
scp ota/check.json user@192.168.1.171:/var/www/ota_root/ota/bms/

# Hoặc dùng script deploy_ota.sh
./deploy_ota.sh 1.0.0
```

Kiểm tra file đã có đúng chỗ:

```bash
ls -lh /var/www/ota_root/ota/bms/
# Kết quả mong đợi:
# bms_v1.0.0.tar.gz  check.json
```

### 3.4. Cấu Hình check.json

#### A. LVGL App check.json

File `check.json` cho LVGL App được tạo tự động bởi Makefile. Nội dung mẫu:

```json
{
    "version": "1.2.3",
    "filename": "app_v1.2.3",
    "sha256": "9d23da93c8ff3b70dfc05c5524b34515...",
    "url": "http://192.168.1.171/ota/app_v1.2.3"
}
```

Lấy SHA256 của binary bằng lệnh:

```bash
sha256sum ota/app_v1.2.3
```

#### B. Python BMS Engine check.json

File `check.json` cho Python BMS Engine được tạo tự động bởi script `build_ota.py`. Nội dung mẫu:

```json
{
    "version": "1.0.0",
    "filename": "bms_v1.0.0.tar.gz",
    "sha256": "a1b2c3d4e5f6...",
    "url": "http://192.168.1.171/ota/bms/bms_v1.0.0.tar.gz",
    "release_notes": "Version 1.0.0",
    "min_version": "1.0.0",
    "force_update": false
}
```

**Lưu ý:** Mỗi lần build bản mới, script `build_ota.py` sẽ tự động cập nhật version, filename, sha256 và url trong check.json.

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
    
    # Optional: Enable CORS for cross-origin requests
    add_header Access-Control-Allow-Origin *;
    add_header Access-Control-Allow-Methods GET, OPTIONS;
    add_header Access-Control-Allow-Headers Origin, Accept, Content-Type;
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
# Kiểm tra LVGL App check.json
curl http://localhost/ota/check.json

# Kiểm tra LVGL App binary (chỉ lấy header, không tải cả file)
curl -I http://localhost/ota/app_v1.2.3

# Kiểm tra Python BMS Engine check.json
curl http://localhost/ota/bms/check.json

# Kiểm tra Python BMS Engine package (chỉ lấy header, không tải cả file)
curl -I http://localhost/ota/bms/bms_v1.0.0.tar.gz
```

**Kết quả mong đợi:**
- `curl check.json` → hiển thị nội dung JSON
- `curl -I app_v1.2.3` → HTTP/1.1 200 OK, Content-Type: application/octet-stream
- `curl -I bms_v1.0.0.tar.gz` → HTTP/1.1 200 OK, Content-Type: application/gzip

### 4.2. Kiểm Tra Từ LuckFox Pico

Trên LuckFox, thay 192.168.1.171 bằng IP thực của máy Ubuntu:

```bash
# Kiểm tra LVGL App manifest
wget http://192.168.1.171/ota/check.json -O -

# Kiểm tra LVGL App download
wget http://192.168.1.171/ota/app_v1.2.3 -O /tmp/test_app
ls -lh /tmp/test_app

# Kiểm tra Python BMS Engine manifest
wget http://192.168.1.171/ota/bms/check.json -O -

# Kiểm tra Python BMS Engine download
wget http://192.168.1.171/ota/bms/bms_v1.0.0.tar.gz -O /tmp/test_bms.tar.gz
ls -lh /tmp/test_bms.tar.gz
```

**Lưu ý:** Dùng lệnh `ip addr` trên Ubuntu để xem IP thực của máy.

## 5. Cấu Trúc Thư Mục Hoàn Chỉnh

```
/var/www/ota_root
└── ota/
    ├── check.json              ← LVGL App metadata: version, sha256, url
    ├── app_v1.2.3              ← LVGL App binary v1.2.3
    ├── app_v1.2.4              ← LVGL App binary v1.2.4 (older version)
    └── bms/
        ├── check.json          ← Python BMS Engine metadata: version, sha256, url
        ├── bms_v1.0.0.tar.gz   ← Python BMS Engine package v1.0.0
        ├── bms_v1.0.1.tar.gz   ← Python BMS Engine package v1.0.1
        └── bms_v1.1.0.tar.gz   ← Python BMS Engine package v1.1.0
```

URLs tương ứng từ LuckFox:

**LVGL App:**
```
http://192.168.1.171/ota/check.json
http://192.168.1.171/ota/app_v1.2.3
```

**Python BMS Engine:**
```
http://192.168.1.171/ota/bms/check.json
http://192.168.1.171/ota/bms/bms_v1.0.0.tar.gz
```

## 6. Quy Trình Deploy Bản Firmware Mới

### A. Deploy LVGL App Mới

Mỗi khi có bản LVGL App mới, thực hiện theo thứ tự sau:

| **STT** | **Hành động** | **Lệnh** |
| --- | --- | --- |
| 1 | Pull code mới nhất | `cd /path/to/HLCFEOS && git pull` |
| 2 | Build binary mới | `cd Device/Luckfoxpico86/code/lvgl_project && make APP_VERSION=1.2.4` |
| 3 | Deploy lên server | `scp ota/app_v1.2.4 user@192.168.1.171:/var/www/ota_root/ota/` |
| 4 | Deploy manifest | `scp ota/check.json user@192.168.1.171:/var/www/ota_root/ota/` |
| 5 | Xác nhận server OK | `curl http://192.168.1.171/ota/check.json` |
| 6 | Device tự động update | Chờ device check (mỗi giờ) hoặc restart service |

### B. Deploy Python BMS Engine Mới

Mỗi khi có bản Python BMS Engine mới, thực hiện theo thứ tự sau:

| **STT** | **Hành động** | **Lệnh** |
| --- | --- | --- |
| 1 | Pull code mới nhất | `cd /path/to/HLCFEOS && git pull` |
| 2 | Build package mới | `cd Device/Luckfoxpico86/code/python_engine && python3 build_ota.py 1.1.0` |
| 3 | Deploy lên server | `./deploy_ota.sh 1.1.0` |
| 4 | Xác nhận server OK | `curl http://192.168.1.171/ota/bms/check.json` |
| 5 | Device tự động update | Chờ device check (mỗi giờ) hoặc restart service |

### C. Deploy Cả 2 Cùng Lúc

Nếu bạn muốn deploy cả LVGL App và Python BMS Engine cùng lúc:

```bash
#!/bin/bash
# deploy_all_ota.sh

LVGL_VERSION=$1
BMS_VERSION=$2

if [ -z "$LVGL_VERSION" ] || [ -z "$BMS_VERSION" ]; then
    echo "Usage: $0 <lvgl_version> <bms_version>"
    echo "Example: $0 1.2.4 1.1.0"
    exit 1
fi

echo "Deploying LVGL App v$LVGL_VERSION..."
cd /path/to/HLCFEOS/Device/Luckfoxpico86/code/lvgl_project
make APP_VERSION=$LVGL_VERSION
scp ota/app_v$LVGL_VERSION user@192.168.1.171:/var/www/ota_root/ota/
scp ota/check.json user@192.168.1.171:/var/www/ota_root/ota/

echo "Deploying Python BMS Engine v$BMS_VERSION..."
cd /path/to/HLCFEOS/Device/Luckfoxpico86/code/python_engine
python3 build_ota.py $BMS_VERSION
scp ota/bms_v$BMS_VERSION.tar.gz user@192.168.1.171:/var/www/ota_root/ota/bms/
scp ota/check.json user@192.168.1.171:/var/www/ota_root/ota/bms/

echo "All deployments complete!"
echo "LVGL App: http://192.168.1.171/ota/check.json"
echo "Python BMS: http://192.168.1.171/ota/bms/check.json"
```

Sử dụng:

```bash
chmod +x deploy_all_ota.sh
./deploy_all_ota.sh 1.2.4 1.1.0
```

## 7. OTA Update Flow

```
Developer                    OTA Server                   Device
    │                            │                           │
    │  1. Build package          │                           │
    │  python3 build_ota.py 1.1.0│                           │
    │                            │                           │
    │  2. Deploy                 │                           │
    │  ./deploy_ota.sh 1.1.0     │                           │
    │ ──────────────────────────>│                           │
    │                            │                           │
    │                            │  3. Check (every hour)    │
    │                            │<──────────────────────────│
    │                            │                           │
    │                            │  4. Return manifest       │
    │                            │──────────────────────────>│
    │                            │                           │
    │                            │  5. Download package      │
    │                            │<──────────────────────────│
    │                            │                           │
    │                            │  6. Verify SHA256         │
    │                            │                           │
    │                            │  7. Backup current        │
    │                            │                           │
    │                            │  8. Extract & Install     │
    │                            │                           │
    │                            │  9. Restart service       │
    │                            │                           │
    │                            │  10. Health check         │
    │                            │                           │
```

## 8. Xử Lý Lỗi Thường Gặp

| **Lỗi** | **Nguyên nhân** | **Cách sửa** |
| --- | --- | --- |
| 404 Not Found | Config Nginx chưa đúng hoặc file chưa copy vào đúng thư mục | Kiểm tra root trong config, chạy `nginx -t` |
| 403 Forbidden | Quyền truy cập file bị sai | `chmod 644 /var/www/ota_root/ota/bms/*` |
| nginx -t báo lỗi | Config file có lỗi cú pháp | Xem dòng báo lỗi, kiểm tra lại dấu ; và {} |
| Kết nối từ LuckFox bị từ chối | Firewall Ubuntu chặn port 80 | `sudo ufw allow 80` |
| SHA256 mismatch | Package bị corrupt hoặc check.json sai | Rebuild package và deploy lại |
| Service không restart sau update | Service name sai hoặc permission | Kiểm tra `systemctl status bms-engine` |

## 9. So sánh với OTA cũ (C++ LVGL)

| Aspect | C++ LVGL (cũ) | Python BMS Engine (mới) |
|--------|---------------|-------------------------|
| Package format | Single binary `app_v1.2.3` | Tarball `bms_v1.0.0.tar.gz` |
| Update method | Atomic rename | Extract + symlink swap |
| Restart | System reboot | Service restart |
| Rollback | Boot flag | Backup directory |
| Health check | Boot success flag | Service status check |
| Build time | 5-10 phút (compile) | <1 phút (tar.gz) |
| Package size | ~2-5MB | ~1-3MB |

## 10. Tài liệu tham khảo

Xem thêm tài liệu chi tiết tại:
- [python_engine/PlanAndDoc/OTA_PLAN.md](../../code/python_engine/PlanAndDoc/OTA_PLAN.md) - Kế hoạch OTA chi tiết
- [python_engine/PlanAndDoc/OTA_IMPLEMENTATION.md](../../code/python_engine/PlanAndDoc/OTA_IMPLEMENTATION.md) - OTA implementation guide
- [python_engine/build_ota.py](../../code/python_engine/build_ota.py) - Build script
- [python_engine/deploy_ota.sh](../../code/python_engine/deploy_ota.sh) - Deploy script

*Tài liệu được cập nhật từ phiên làm việc thực tế — OTA Server Setup cho Python BMS Engine — 17/1/2026*
