# HLCFEOS — Hệ thống Quản trị Năng lượng Smart Cafe

Hệ thống quản trị năng lượng thông minh cho chuỗi cửa hàng cafe, dùng thiết bị IoT để tiết kiệm điện, tập trung vào:

- Hệ thống điều hòa (AC)
- Hệ thống đèn biển quảng cáo
- Hệ thống đèn chiếu sáng

Hoạt động theo nguyên tắc **Local-First** (mất internet vẫn chạy được), hỗ trợ chuỗi 10–100 cửa hàng, có **Manual Override** để đảm bảo an toàn khi vận hành.

---

## 1. Kiến trúc tổng quan

```
┌──────────────────────────────────────────────┐
│   Smart Panel — Luckfox Pico 86 Box (RV1106) │
│                                              │
│   ┌──────────────┐  HTTP    ┌─────────────┐   │
│   │  LVGL App    │ ───────► │  BMS Engine │   │
│   │  (C++ HMI)   │  :8080   │  (Rust)     │   │
│   └──────────────┘          └──────┬──────┘   │
│                                    │ MQTT      │
│                         ┌──────────┴───────┐   │
│                         │ NanoMQ (broker)  │   │
│                         └──────────▲───────┘   │
└─────────────────────────────────────┼─────────┘
                                      │ MQTT
                         ┌────────────┴───────────┐
                         │ Gateway Tasmota (Zigbee)│
                         ├────────────────────────┤
                         │ Đèn chiếu sáng · Đèn   │
                         │ biển QC · Điều hòa (IR) │
                         └────────────────────────┘

   ┌──────────────┐        ┌────────────────────────┐
   │  GitHub      │        │ xsolar cloud (đối      │
   │  Releases    │◄──────►│ chiếu năng lượng mặt   │
   │  (nguồn OTA) │        │ trời qua MQTT)          │
   └──────────────┘        └────────────────────────┘
```

**Hai thành phần code chính:**

| Thành phần | Ngôn ngữ | Vai trò |
|-----------|----------|---------|
| **LVGL App** | C++ / LVGL v9 | Giao diện thao tác tại quán (HMI): bật/tắt AC, đèn, xem trạng thái |
| **BMS Engine** | Rust | Xử lý logic + rule tự động hóa, MQTT với gateway, HTTP API cho HMI, OTA |

---

## 2. Cấu trúc thư mục

```
HLCFEOS/
├── .github/workflows/
│   ├── release-app.yml     # Build & Release LVGL App (tag app-v*)
│   └── release-engine.yml  # Build & Release BMS Engine (tag bms-v*)
├── Device/Luckfoxpico86/code/
│   ├── lvgl_project/        # LVGL App (HMI) + OTA
│   │   ├── src/             # Mã nguồn C++ (ota.c, config.c, ...)
│   │   ├── Makefile         # make APP_VERSION=x.y.z
│   │   └── app_config.txt.example  # Cấu hình mẫu cho board
│   └── Rush_engine/         # BMS Engine (Rust)
│       ├── src/             # Mã nguồn (ota.rs, main.rs, ...)
│       ├── config/config.yaml
│       ├── bms-engine.service
│       ├── build.sh         # Build cross-compile armv7-musl
│       ├── deploy.sh        # Deploy lên board
│       └── build_ota.sh     # Tạo gói OTA cục bộ
├── AGENTS.md                # Quy ước làm việc & commit/push
├── HISTORY.md               # Lịch sử các commit trước khi rút gọn
└── README.md                # (file này)
```

---

## 3. Yêu cầu máy phát triển

- Linux (tested trên Ubuntu 22.04)
- **Rust** + `cargo-zigbuild` (build BMS Engine cho ARM):
  ```bash
  rustup target add armv7-unknown-linux-musleabihf
  cargo install cargo-zigbuild
  ```
- **Zig** (linker cho cross-compile musl):
  ```bash
  curl -L -o /tmp/zig.tar.xz https://ziglang.org/download/0.13.0/zig-linux-x86_64-0.13.0.tar.xz
  mkdir -p ~/zig && tar xJf /tmp/zig.tar.xz -C ~/zig --strip-components=1
  export PATH="$HOME/zig:$PATH"
  ```
- **GCC ARM cross** (chỉ khi build LVGL App):
  ```bash
  sudo apt-get install gcc-arm-linux-gnueabihf make file
  ```
- Truy cập SSH vào board: `ssh pico@<IP-board>`

---

## 4. Build & Chạy

### 4.1. BMS Engine (Rust)

```bash
cd Device/Luckfoxpico86/code/Rush_engine

# Build release binary (armv7-musl static)
./build.sh release
# Binary: target/armv7-unknown-linux-musleabihf/release/bms-engine

# Deploy lên board (chạy sudo, cần SSH)
sudo ./deploy.sh
```

Cấu hình chính trong `config/config.yaml`:

| Tham số | Mô tả | Mặc định |
|---------|-------|----------|
| `mqtt.broker` | MQTT broker cục bộ | `localhost:1883` |
| `xsolar.*` | Broker remote đối chiếu năng lượng | — |
| `http.port` | Cổng HTTP API cho HMI | `8080` |
| `devices_file` | File khai báo thiết bị | `devices.yaml` |
| `ota.*` | Cấu hình OTA (xem mục 6) | — |

Xem log engine trên board:

```bash
sudo journalctl -u bms-engine -f
```

### 4.2. LVGL App (HMI)

```bash
cd Device/Luckfoxpico86/code/lvgl_project

# Build (output: ota/app_v1.1.0 và app)
make
# Hoặc chỉ định version: make APP_VERSION=1.2.0
```

**Cấu hình trên board** — file `app_config.txt` (đặt cạnh binary `/home/pico/app`), copy từ `app_config.txt.example`:

```ini
room_id=Room_adfff        # ID phòng
pincode=1234              # PIN Settings (mặc định)
brightness=200
github_repo=huongvn/HLCFEOS   # Nguồn OTA (GitHub Releases)
ota_enabled=1                 # 1 = bật OTA
ota_check_interval=86400      # giây giữa 2 lần auto-check
ota_github_token=             # PAT tùy chọn (tránh rate limit)
```

---

## 5. Chạy tại cửa hàng

Hệ thống chạy như **service tự động** khi board boot:

- `bms-engine` chạy qua **systemd** (auto-restart khi lỗi).
- LVGL App tự khởi động cùng màn hình HMI.

Xem trạng thái:

```bash
# Trên board
sudo systemctl status bms-engine
sudo journalctl -u bms-engine -f      # log engine
systemctl status myapp                # app HMI (nếu chạy qua systemd)
```

Khi mất internet: hệ thống chuyển sang chế độ **Local-First** — MQTT cục bộ vẫn điều khiển được thiết bị; có thể **bấm trực tiếp** trên thiết bị làm phương án dự phòng.

---

## 6. OTA — Cập nhật phần mềm từ xa

Cả 2 app đều cập nhật qua **GitHub Releases** của repo này. Workflow CI tự build và publish Release khi push tag theo quy ước:

| Thành phần | Tag phải dùng | Asset được đăng |
|-----------|---------------|-----------------|
| LVGL App | `app-vX.Y.Z` | `app_vX.Y.Z` + `.sha256` |
| BMS Engine | `bms-vX.Y.Z` | `bms_vX.Y.Z.tar.gz` + `.sha256` |

> **Quan trọng:** board chỉ nhận bản tag **mới hơn** bản đang chạy. Muốn phát bản mới, tạo tag version cao hơn hiện tại.

### 6.1. Quy trình release

```bash
# 1. Commit changes (nếu có)
git add . && git commit -m "..."

# 2. Tạo tag version mới và đẩy lên
git tag bms-v1.1.1    # engine
git tag app-v1.2.1    # LVGL App
git push origin bms-v1.1.1

# 3. GitHub Actions tự build, tạo Release + asset
#    (theo dõi ở tab "Actions" của repo)
```

Sau khi Release được tạo, mỗi board **tự tải về + cài đặt**:

- **Engine:** kiểm tra mỗi `ota.check_interval` (giây) — mặc định 3600 (1 giờ).
- **App HMI**: auto-check theo `ota_check_interval`, hoặc bấm nút **"Check for Update"** trên màn hình.

### 6.2. Kích hoạt OTA từ xa qua cloud (xsolar MQTT)

Ngoài chu kỳ tự kiểm tra, hệ thống hỗ trợ **ép update từ xa** qua broker MQTT xsolar. Engine subscribe topic `{topic_prefix}/+/set` (mặc định `smarteos/bluCafe/+/set`) — `device_id = ota` là kênh điều khiển OTA:

| Phía | Topic | Payload | Ý nghĩa |
|------|-------|---------|---------|
| Cloud → Engine | `smarteos/bluCafe/ota/set` | `{"action":"check"}` | Hỏi version hiện tại |
| Cloud → Engine | `smarteos/bluCafe/ota/set` | `{"action":"update"}` | Ép update engine + app về bản mới nhất |
| Engine → Cloud | `smarteos/bluCafe/ota` | `{"state":"check_result","engine":"1.2.6","app":"1.2.6","ts":"..."}` | Trả lời check |
| Engine → Cloud | `smarteos/bluCafe/ota` | `{"state":"started","ts":"..."}` | Bắt đầu update |
| Engine → Cloud | `smarteos/bluCafe/ota` | `{"state":"updated","engine":"1.2.6","app":"1.2.6","ts":"..."}` | Update xong, kèm version thực tế |
| Engine → Cloud | `smarteos/bluCafe/ota` | `{"state":"error","message":"..."}` | Lỗi / action không hợp lệ |

> `topic_prefix` lấy từ `xsolar.topic_prefix` trong `config/config.yaml` (mặc định `smarteos/bluCafe`). Lệnh `update` luôn đưa cả engine + app về bản "latest" trên GitHub; nếu đã mới nhất thì báo `updated` với version hiện tại, không khởi động lại.

**Ví dụ phát lệnh (dùng `mosquitto_pub`):**

```bash
mosquitto_pub -h mqtt.xsolar.energy -u <user> -P <pass> \
  -t "smarteos/bluCafe/ota/set" -m '{"action":"update"}' -q 1

# Xem kết quả
mosquitto_sub -h mqtt.xsolar.energy -u <user> -P <pass> \
  -t "smarteos/bluCafe/ota" -q 0
```

**Cơ chế luồng update:**

1. Engine nhận `{"action":"update"}` → publish trạng thái `started` lên cloud.
2. Engine thông báo cho App LVGL qua **HTTP/SSE** (`/api/v1/events` — App không dùng MQTT trực tiếp).
3. App tự chạy chuỗi OTA GitHub (check → download → verify → install → reboot).
4. Engine tự update chính nó (check → download → verify → swap → restart service).
5. Instance engine mới sau khi boot publish `updated` kèm version engine + app (app báo version qua `POST /api/system/app_version` mỗi khi boot / reconnect SSE).

Khi App cập nhật thì board sẽ **reboot toàn bộ** (cả engine + app), `myapp` restart trong ~1 phút sau đó.

### 6.3. Xem kết quả OTA trên board

```bash
sudo journalctl -u bms-engine | grep -iE "ota|update|version"
```

Kết quả thành công sẽ hiển thị: `Update available: 1.1.0 -> 1.1.1`, `Installed new binary`, rồi engine khởi động lại với version mới.

### 6.4. Lưu ý khi release

- Không dùng lại tag cũ: nếu release đã tồn tại cùng số, tăng minor/patch (VD `1.1.0` → `1.1.1`).
- Cả hai workflow đã cấu hình sẵn `permissions: contents: write` để tải được asset lên Release.
- Không commit mật khẩu/token thật; dùng biến môi trường hoặc config ngoài git trên board.

---

## 7. Troubleshooting

| Triệu chứng | Nguyên nhân / cách xử lý |
|-------------|--------------------------|
| Không có bản cập nhật | Tag thấp hơn bản đang chạy. Kiểm tra `ota.enabled` và `ota.auto_update` |
| `GitHub API returned status: 403` | Hết rate limit GitHub API (60 req/h). Điền `ota_github_token` |
| OTA fail "Cross-device link" | Đã fix bằng cách copy trong cùng thư mục; dùng bản v1.1.0 trở lên |
| HMI không hiện trạng thái | Kiểm tra `bms-engine` đã chạy, HTTP `127.0.0.1:8080` còn mở |

---

## 8. Scripts hỗ trợ

| Script | Mục đích |
|--------|----------|
| `Rush_engine/build.sh` | Build engine arm/musl |
| `Rush_engine/build_ota.sh <ver>` | Tạo gói `bms_v<ver>.tar.gz` + sidecar cục bộ |
| `Rush_engine/deploy.sh` | Deploy engine lên board (cần root) |
| `lvgl_project/build.sh` | Build LVGL App |

---

## 9. Tài liệu thêm

- Kiến trúc chi tiết: `Device/Luckfoxpico86/code/doc/architecture.md`
- Thiết kế API: `Device/Luckfoxpico86/code/doc/api_redesign.md`
- Quy ước làm việc và commit: [`AGENTS.md`](AGENTS.md)
- Lịch sử các commit cũ: [`HISTORY.md`](HISTORY.md)

---

*Dự án được phát triển bởi `huongnv` — huongvn.tdh@gmail.com.*