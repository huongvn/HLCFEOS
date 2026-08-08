# HƯỚNG DẪN CÀI ĐẶT RUST BMS ENGINE (LUCKFOX PICO)

> Rust BMS Engine (thư mục `Rush_engine`) là bản viết lại bằng Rust của Python BMS Engine.
> Ưu điểm: binary **static musl** — chạy được trên mọi board Luckfox (không lỗi `GLIBC_2.39`), nhẹ (~3.4MB), không cần cài dependencies.

---

## 1. Tổng quan

| Mục | Giá trị |
|-----|---------|
| Nguồn code | `Device/Luckfoxpico86/code/Rush_engine/` |
| Binary chạy | `/home/pico/bms-engine/bms-engine` |
| Thư mục cài đặt | `/home/pico/bms-engine/` |
| Service | `bms-engine.service` |
| User chạy | `pico` |
| Target build | `armv7-unknown-linux-musleabihf` (static) |

---

## 2. Cài đặt app

### 2.1. Build cross-compile (trên máy tính dev)

Cần có: Rust, `zig`, `cargo-zigbuild`, target `armv7-unknown-linux-musleabihf`.

```bash
# 1. Cài zig (nếu chưa có)
curl -L -o /tmp/zig.tar.xz https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz
mkdir -p ~/zig && tar xJf /tmp/zig.tar.xz -C ~/zig --strip-components=1
export PATH="$HOME/zig:$PATH"

# 2. Cài cargo-zigbuild
cargo install cargo-zigbuild

# 3. Thêm target musl
rustup target add armv7-unknown-linux-musleabihf

# 4. Build release
cd Device/Luckfoxpico86/code/Rush_engine
./build.sh release
```

Kết quả mong đợi:

```
=== Verify (must be statically linked) ===
OK: statically linked
=== Build Complete ===
Binary: target/armv7-unknown-linux-musleabihf/release/bms-engine
```

Kiểm tra binary:

```bash
file target/armv7-unknown-linux-musleabihf/release/bms-engine
# ELF 32-bit LSB executable, ARM, EABI5, statically linked, stripped
```

> **Lưu ý:** phải là **statically linked**. Nếu thấy "dynamically linked" thì binary sẽ lỗi `GLIBC_2.39 not found` trên board.

### 2.2. Copy lên Luckfox

> Dừng service trước khi thay binary (nếu đang chạy), nếu không sẽ lỗi `Text file busy`:

```bash
ssh pico@<IP_LUCKFOX> 'echo <pass> | sudo -S systemctl stop bms-engine'
```

```bash
cd Device/Luckfoxpico86/code/Rush_engine
BIN=target/armv7-unknown-linux-musleabihf/release/bms-engine
scp "$BIN" pico@<IP_LUCKFOX>:/home/pico/bms-engine/bms-engine
```

SSH vào board để tạo cấu trúc thư mục và copy config:

```bash
ssh pico@<IP_LUCKFOX>
mkdir -p /home/pico/bms-engine/config /home/pico/bms-engine/data
chmod +x /home/pico/bms-engine/bms-engine
```

Copy config + rules + version (từ máy dev) — các file này đi thẳng vào thư mục `config/`:

```bash
cd Device/Luckfoxpico86/code/Rush_engine
scp config/config.yaml config/rules.yaml pico@<IP_LUCKFOX>:/home/pico/bms-engine/config/
scp VERSION pico@<IP_LUCKFOX>:/home/pico/bms-engine/
```

Sau khi copy xong, chạy lại service:

```bash
ssh pico@<IP_LUCKFOX>
sudo systemctl start bms-engine
```

### 2.3. Copy devices.yaml

`devices.yaml` nằm ngoài thư mục engine; engine dùng **symlink** trỏ về file đó:

```bash
# Nguồn trên board từ nơi sync (VD bản LVGL): copy thành file thật
cp /home/pico/lvgl_project/devices.yaml /home/pico/devices.yaml

# Symlink trong thư mục engine trỏ về file thật
ln -sf /home/pico/devices.yaml /home/pico/bms-engine/devices.yaml
```

> `/home/pico/devices.yaml` là **file thật** (Engine `DeviceManager` hot-reload theo `mtime` mỗi 5s); `/home/pico/bms-engine/devices.yaml` chỉ là **symlink** trỏ tới nó — khi sửa đổi hãy sửa file thật ở `/home/pico/devices.yaml`, không cần restart engine.

---

## 3. Cấu hình app trước khi chạy

### 3.1. File `config/config.yaml`

Chỉnh sửa theo cấu hình thực tế:

```bash
nano /home/pico/bms-engine/config/config.yaml
```

Các mục quan trọng:

| Khóa | Giá trị mặc định | Ghi chú |
|------|------------------|---------|
| `mqtt.broker` | `localhost` | NanoMQ cục bộ trên board |
| `mqtt.port` | `1883` | |
| `mqtt.user` / `mqtt.pass` | rỗng | Board NanoMQ chạy `allow_anonymous=true` — **để trống, KHÔNG gửi credentials** (gửi credentials làm broker từ chối kết nối) |
| `xsolar.broker` | `mqtt.xsolar.energy` | Broker remote |
| `xsolar.user` / `xsolar.pass` | rỗng | **Lấy từ `.env`** (xem 3.2), không ghi vào file này |
| `database.path` | `data/bms.db` | Đường dẫn tương đối so với WorkingDirectory |
| `devices_file` | `devices.yaml` | Symlink → `/home/pico/devices.yaml` |
| `rules_file` | `config/rules.yaml` | |
| `http.enabled` | `true` | Bật REST API cho App LVGL |
| `http.host` / `http.port` | `127.0.0.1` / `8080` | App LVGL gọi REST + SSE (thay vì MQTT cục bộ) |
| `ota.ota_url` | `http://<OTA-SERVER>/ota/bms/check.json` | Đổi IP cho đúng OTA server |
| `ota.enabled` | `true` | |

### 3.2. File `.env` (chứa credentials, KHÔNG commit git)

Credentials được đọc qua biến môi trường (ưu tiên hơn `config.yaml`), giữ bí mật không nằm trong git.

```bash
nano /home/pico/bms-engine/.env
chmod 600 /home/pico/bms-engine/.env
```

```ini
# Local NanoMQ (để trống nếu anonymous)
BMS_MQTT_USER=
BMS_MQTT_PASS=

# Remote xsolar
BMS_XSOLAR_USER=admin
BMS_XSOLAR_PASS=<mật khẩu xsolar>
```

> File template tham chiếu: `Rush_engine/.env.example`. Chỉ có `.env.example` được commit, `.env` nằm trong `.gitignore`.

---

## 4. Chạy app (chạy trực tiếp)

```bash
cd /home/pico/bms-engine
./bms-engine config/config.yaml
```

Log mong đợi (thấy 2 kết nối MQTT thành công):

```
[INFO bms_engine] Initializing BMS Engine...
[INFO bms_engine::device_manager] Loaded 7 devices from "devices.yaml"
[INFO bms_engine::mqtt_client] MQTT client initialized: localhost:1883, client_id=bms-engine-xxxx
[INFO bms_engine::mqtt_client] MQTT client initialized: mqtt.xsolar.energy:1883, client_id=bms-engine_xsolar_xxxx, auth=yes
[INFO bms_engine::mqtt_client] [bms-engine-xxxx] Connected to MQTT broker successfully
[INFO bms_engine] BMS Engine started successfully
[INFO bms_engine::mqtt_client] [bms-engine_xsolar_xxxx] Connected to MQTT broker successfully
```

> Nếu thấy lỗi `connection closed by peer` lặp lại trên connection xsolar → do client_id bị trùng với instance khác. Bản này đã tự thêm suffix ngẫu nhiên (`bms-engine_xsolar_xxxx`) để tránh.

---

## 5. Chạy app dạng service (tự khởi động cùng hệ thống)

### 5.1. Cài đặt service

File service nằm trong repo (`Rush_engine/bms-engine.service`), copy lên board:

```bash
# Từ máy dev
scp Device/Luckfoxpico86/code/Rush_engine/bms-engine.service pico@<IP_LUCKFOX>:/tmp/

# Trên board
sudo cp /tmp/bms-engine.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable bms-engine
sudo systemctl start bms-engine
```

để dừng service 
```bash
sudo systemctl stop bms-engine
```

### 5.2. Nội dung service (tham khảo)

`/etc/systemd/system/bms-engine.service`:

```ini
[Unit]
Description=BMS Engine - Rust Building Management System
After=network.target nanomq.service
Wants=nanomq.service

[Service]
Type=simple
User=pico
Group=pico
WorkingDirectory=/home/pico/bms-engine
ExecStart=/home/pico/bms-engine/bms-engine config/config.yaml
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal
SyslogIdentifier=bms-engine

Environment=RUST_LOG=info
EnvironmentFile=-/home/pico/bms-engine/.env

NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

> `EnvironmentFile=-/home/pico/bms-engine/.env`: dấu `-` nghĩa là file không bắt buộc tồn tại (nếu thiếu service vẫn chạy).
> `After=... nanomq.service`: đảm bảo NanoMQ đã chạy trước khi BMS Engine kết nối.

### 5.3. Kiểm tra & quản lý service

```bash
# Trạng thái
sudo systemctl status bms-engine

# Xem logs theo thời gian thực
sudo journalctl -u bms-engine -f

# Restart / stop / disable
sudo systemctl restart bms-engine
sudo systemctl stop bms-engine
sudo systemctl disable bms-engine
```

Kết quả mong đợi:

```
● bms-engine.service - BMS Engine - Rust Building Management System
     Loaded: loaded (/etc/systemd/system/bms-engine.service; enabled)
     Active: active (running) since ...
   Main PID: 1994 (bms-engine)
```

---

## 6. Xử lý sự cố thường gặp

| Lỗi | Nguyên nhân | Cách xử lý |
|-----|-------------|------------|
| `GLIBC_2.39 not found` | Binary build dynamic glibc mới | Build lại bằng musl static (`build.sh release`), verify "statically linked" |
| `NotAuthorized` (kết nối local) | Gửi credentials cho NanoMQ đang anonymous | Để `mqtt.user`/`mqtt.pass` rỗng |
| `NotAuthorized` (kết nối xsolar) | Sai user/pass | Sửa `.env` → `BMS_XSOLAR_USER`/`BMS_XSOLAR_PASS`, restart service |
| `connection closed by peer` lặp lại (xsolar) | Client_id trùng instance khác | Bản này đã tự thêm suffix ngẫu nhiên, restart service |
| Service `inactive (dead)` | Crash khi khởi động | `journalctl -u bms-engine -e` xem log; đảm bảo `devices.yaml` + `config/config.yaml` đúng đường dẫn |

---

## 7. Tài liệu liên quan

- [Cài đặt NanoMQ](7_Cai_dat_NanoMQ.md)
- [Cài đặt OTA server](10_ota_server_setup.md)
- Kiến trúc & API hiện tại: `Device/Luckfoxpico86/code/doc/architecture.md` + `code/doc/api_redesign.md`
- Nguồn code: `Device/Luckfoxpico86/code/Rush_engine/`
