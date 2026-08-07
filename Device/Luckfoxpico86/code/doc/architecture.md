# Tài liệu Hệ thống Quản trị Năng lượng Smart Cafe (Luckfox Pico Panel 86)

> **Ghi chú**: Tài liệu này mô tả **kiến trúc và API hiện tại** (đã triển khai): Engine giao tiếp với App qua **REST API + SSE** (`device_id` + `attr_id`), App **không đọc** `devices.yaml` trực tiếp mà lấy từ catalog Engine. Xem [api_redesign.md](api_redesign.md) để biết lịch sử thiết kế. `architecture.md` là nguồn mô tả chính thức cho code hiện tại.

## 1. Kiến trúc Hệ thống (System Architecture)

Hệ thống hoạt động theo mô hình **Local-First** với 2 thành phần chính chạy trên Luckfox Pico Panel 86:

```
┌─────────────────────────────────────────────────────────────────┐
│                    Luckfox Pico Panel 86                        │
│                                                                 │
│  ┌─────────────────────┐         ┌──────────────────────────┐  │
│  │  Smart Panel App    │  HTTP   │   BMS Engine (Rust)      │  │
│  │  (C++ / LVGL)       │◄───────►│   bms-engine             │  │
│  │  - Catalog          │  REST   │   - Device Manager       │  │
│  │  - SSE realtime     │  +SSE   │   - Event Bus            │  │
│  │  - Actions (control)│         │   - Command Tracker      │  │
│  └─────────┬───────────┘         │   - Rule/Scheduler       │  │
│            │                     │   - xsolar bridge        │  │
│            │                     └────────────┬─────────────┘  │
│            │ MQTT (NanoMQ 127.0.0.1:1883)     │                 │
│  ┌─────────▼───────────┐                     │ SQLite (WAL)    │
│  │  Zigbee Gateway     │                     ▼                 │
│  │  (Tasmota + hub)    │              ┌─────────────┐          │
│  │  Tele: tele/+/SENSOR│              │ data/bms.db │          │
│  │  Cmd: cmnd/{gw}/ZbSend             └─────────────┘          │
│  └─────────────────────┘                                       │
└─────────────────────────────────────────────────────────────────┘
```

- **Smart Panel App** và **BMS Engine** chạy cùng một máy, App gọi Engine qua `http://127.0.0.1:8080`.
- **NanoMQ** là local broker: Gateway Tasmota publish telemetry (`tele/{gateway}/SENSOR`), Engine publish lệnh điều khiển (`cmnd/{gateway}/ZbSend`).
- Khi mất Internet, mọi chức năng cục bộ (điều khiển, hiển thị, kịch bản) vẫn hoạt động bình thường — chỉ riêng luồng push dữ liệu lên xsolar bị gián đoạn.

### a. BMS Engine (Rust — `bms-engine`)

- **Vai trò**: Backend điều phối — nhận telemetry từ Gateway, chuẩn hóa dữ liệu, lưu SQLite, thực thi rule/kịch bản, và phục vụ App qua REST + SSE.
- **Module** (`Rush_engine/src/`):
  | File | Vai trò |
  |------|---------|
  | `main.rs` | Entry point, wiring toàn bộ subsystem + vòng lặp chính + các task định kỳ |
  | `device_manager.rs` | Load/hot-reload `devices.yaml`, ánh xạ `nr_type` → `device_id` (slug_index), sinh catalog |
  | `state_manager.rs` | SQLite (WAL): history `device_log`, metrics `device_metric`, config |
  | `event_bus.rs` | Pub/Sub in-process: `BusEvent{topic,payload}` + snapshot gần nhất |
  | `hmi_bridge.rs` | Dịch dữ liệu ↔ HMI: sinh SSE feedback, dispatch action, kịch bản mở/đóng quán |
  | `http_api.rs` | Axum REST API + SSE stream |
  | `command_tracker.rs` | Theo dõi lệnh pending theo `command_id`, ack, timeout, reconcile |
  | `queue_manager.rs` | Hàng đợi gửi lệnh `ZbSend` ra Gateway (retry khi mất broker) |
  | `mqtt_client.rs` | Wrapper rumqttc cho 2 client: local + xsolar |
  | `rule_engine.rs` | Load `rules.yaml`, trigger theo MQTT / thời gian |
  | `xsolar_bridge.rs` | Push snapshot lên broker xsolar + nhận lệnh điều khiển từ xa |
  | `scheduler.rs` | Helper chạy task định kỳ |
  | `ota.rs` | State machine OTA cho engine (check → download → install → rollback) |
- **Config**: `config/config.yaml` (MQTT, xsolar, database, http, devices_file, rules_file, OTA).

### b. Smart Panel App (C++ / LVGL)

- **Vai trò**: Giao diện HMI tại quán: Overview (tổng quan), các tab theo loại thiết bị (AC, Sign, Power, Light, Switch), điều khiển thủ công, kịch bản Open/Close Store.
- **Giao tiếp với Engine**:
  - **Khởi tạo**: `http_client_init()` → `GET /api/v1/devices/catalog` (danh sách thiết bị + display attrs) → `GET /api/v1/devices` (trạng thái hiện tại) → mở SSE thread.
  - **Realtime**: Thread SSE đọc `GET /api/v1/events`, đẩy event vào **Event Queue** (mutex). Main loop gọi `http_client_poll()` để lấy event và cập nhật UI — **không gọi API LVGL từ thread khác**.
  - **Điều khiển**: `http_client_action()` → `POST /api/v1/devices/{device_id}/actions` với `command_id` (App tự sinh) → Engine gửi lệnh ra Gateway → khi device report về, Engine đính kèm `ack_command_id` vào SSE event → App unlock device.
- **Module chính** (`lvgl_project/src/`): `main.c` (khởi tạo + main loop), `http_client.c` (HTTP + SSE client), `bms.c` (logic BMS + render UI), `bms_yaml.c` (fallback đọc devices.yaml khi catalog rỗng), `config.c`, `ota.c`, `settings.c`, `ui.c` (navigation), `ui_helpers.c` (font/language).

---

## 2. Nguồn dữ liệu: `devices.yaml`

- **Single source of truth**: `devices.yaml` (trên board: `/home/pico/devices.yaml`).
- Engine `DeviceManager` load file này; mỗi **5s** kiểm tra `mtime` → **hot reload** khi file đổi (không cần restart engine).
- Mỗi device khai báo: `device_id`, `zigbee_addr`, `nr_type` (`ac_controller`, `mcb`, `power_meter`, `light_sensor`, `switch`), `name`, `group`, `gateway`, `*_index`, `enabled`, và danh sách `attributes[]`:
  - `id` (attr_id, VD `0110`), `label` (tiếng Anh, dùng cho xsolar), `type` (`bool|number|hex`), `scale`, `formula`, `unit`, `display`, `overview` (hiển thị ở card Overview), `control` (cho phép điều khiển), `xsolar` + `xsolar_key`, `decode` (composite hex → nhiều attr).
- **Ánh xạ `nr_type` → `device_id`** (`slug_{index}`): `ac_controller→ac`, `mcb→sign`, `power_meter→power`, `light_sensor→light`, `switch→switch`.
- **Lưu ý**: chỉ device có `*_index` mới xuất hiện trong catalog (`device_id_of()` trả `None` nếu thiếu index). Device `switch` hiện dùng chung field `sign_index` trong metadata engine.

---

## 3. Luồng dữ liệu trong Engine

### a. Khởi động (`main.rs`)
1. Đọc config từ `argv[1]` (mặc định `config/config.yaml`).
2. Khởi tạo: `DeviceManager` (devices.yaml) → `StateManager` (SQLite) → 2 `MqttClient` (local + xsolar) → `QueueManager` (gửi lệnh) → `EventBus` (broadcast 1024) → `HmiBridge` → `XsolarBridge` → `CommandTracker` → `RuleEngine`.
3. `seed_from_db()`: nạp trạng thái SQLite gần nhất vào cache để tránh "storm" thay đổi giả sau restart.
4. Subscribe local broker `#`.
5. Task định kỳ (tokio spawn):
   | Task | Chu kỳ |
   |------|--------|
   | Rule time-tick | 5s |
   | CommandTracker sweep (timeout) | 3s |
   | xsolar snapshot push | 600s (config) |
   | devices.yaml check_reload | 5s |
   | Poll power meter (`cmnd/.../ZbSend` Read cluster 1794) | 30s |
   | Phát hiện offline (`bms/{type}/{idx}/online`) | 60s |
   | OTA check | 3600s (config) |

### b. Ingest telemetry (`process_zigbee_message`)
Mỗi bản tin `tele/{gateway}/SENSOR` (ZbReceived) → normalize theo từng attr (decode hex, bool, number, `formula: zcl_illuminance`, scale) → **fan-out độc lập**:
1. **SQLite** write (batch, spawn task — không chặn UI).
2. **`command_tracker.reconcile()`**: đối chiếu giá trị report với lệnh pending → nếu khớp thì chuyển vào `ack_queue`.
3. **`xsolar_bridge.update_cache()`**: cache bộ nhớ (không gọi mạng).
4. **`hmi_bridge.publish_feedback()`**: diff so với `last_states`, emit các attr thay đổi (`display:true`) lên EventBus.
5. **`xsolar_bridge.push_device_state()`**: push realtime có throttle (≥5s/device).

### c. Luồng điều khiển (Command + Ack + Reconcile)
```
App  ──POST /api/v1/devices/sign_0/actions {command_id, action:TURN_ON}──►  Engine
                                                                           │
Engine: 1) idempotency: nếu command_id đang pending → 202 không tái gửi    │
        2) hmi_bridge.execute_action() → tìm bool control attr             │
        3) queue_mgr gửi cmnd/{gateway}/ZbSend {"Device":addr,"Write":{...}}│
        4) command_tracker.register(pending) → 202 PENDING                 │
                                                                           ▼
                                                          Gateway → thiết bị Zigbee
                                                                           │
Thiết bị report về tele/SENSOR ──► reconcile(addr,new_state)
          nếu value == expected_value ──► chuyển vào ack_queue            │
                                                                           ▼
Engine: SSE data: {"event":"ATTR_UPDATED","device_id":"sign_0",
                    "attr":"0110","value":"ON","ack_command_id":"cmd_..."}
                                                                           │
App:  http_client_poll() → bms_handle_api_event() → ack → unlock device    │
       + update UI. Hết 5s chưa ack → bms_rollback() hoàn trả trạng thái   │
```
- Mỗi device chỉ **1 lệnh pending** tại một thời điểm (`by_device` map); lệnh mới thay thế lệnh cũ.
- `TURN_ON`/`TURN_OFF`/`TOGGLE`/`SET_ATTRIBUTE` do `hmi_bridge.execute_action()` xử lý.

### d. Luồng lệnh cũ (legacy MQTT topics) — vẫn được Engine parse
- `bms/ac|sign/{idx}/{attr}/set` → `handle_ac_command` / `handle_sign_command`.
- `bms/scene/master` ON/OFF → kịch bản Open/Close Store (gửi `EF00/{Power|Control}=1/0` cho mọi `ac_controller` + `mcb`, **stagger 200ms** để tránh burst Zigbee).
- Gateway LWT (`*/LWT`) → cập nhật `bms/{type}/{idx}/online`.
- MQTT-trigger rules trong `rules.yaml`.

---

## 4. HTTP API Reference (đã triển khai)

Engine phục vụ tại `http://127.0.0.1:8080` (cấu hình trong `config.yaml`).

| Method | Endpoint | Mô tả |
|--------|----------|-------|
| `GET` | `/api/health` | `{"ok": true}` |
| `GET` | `/api/v1/devices` | Danh sách device + trạng thái realtime |
| `GET` | `/api/v1/devices/catalog` | Danh sách device + display attrs (không giá trị) |
| `GET` | `/api/v1/devices/{device_id}/state` | Trạng thái 1 device |
| `POST` | `/api/v1/devices/{device_id}/actions` | Điều khiển device |
| `POST` | `/api/v1/scenes/{scene_id}/actions` | Kịch bản (`master`) |
| `GET` | `/api/v1/events` | SSE stream realtime |

### a. Catalog — `GET /api/v1/devices/catalog`
```json
{
  "site": "bluCafe",
  "devices": [
    {
      "id": "sign_0",
      "name": "Sign Board",
      "type": "Sign",
      "group": "Outdoor",
      "display_attrs": [
        {"attr_id": "0110", "label": "Control", "type": "bool", "unit": "", "overview": false, "control": true},
        {"attr_id": "0201", "label": "Energy",  "type": "number", "unit": "kWh", "overview": false, "control": false}
      ]
    }
  ]
}
```
- `type`: `AC | Sign | Power | Light | Switch` (từ `card_type(nr_type)`).
- Chỉ attr có `display:true` mới xuất hiện.

### b. Trạng thái — `GET /api/v1/devices`
```json
{
  "devices": [
    {
      "id": "sign_0", "name": "Sign Board", "type": "Sign", "group": "Outdoor",
      "state": "ON",
      "attrs": { "Control": true, "Energy": 303.5 },
      "last_updated": 0, "source": "SENSOR"
    }
  ]
}
```
- `attrs` keyed theo **label** của attr.
- `state` = `"ON"` khi có attr bool+control đang true.

### c. Điều khiển — `POST /api/v1/devices/{device_id}/actions`
```json
// Request
{ "command_id": "cmd_fe_1730000000_1", "action": "TURN_ON", "params": {} }
// SET_ATTRIBUTE cần params
{ "command_id": "cmd_fe_...", "action": "SET_ATTRIBUTE",
  "params": { "attr": "0202", "value": 24 } }
```
- Response `202`: `{"command_id", "status":"PENDING", "message":"..."}`.
- Response `422`: `{"error":"...", "command_id":"..."}` (action không hỗ trợ, device không tồn tại, thiếu `command_id`).
- **Idempotent**: cùng `command_id` gửi lại → 202 PENDING, không tái gửi lệnh.

### d. SSE — `GET /api/v1/events`
- Đầu stream: replay `bus.snapshot()` (giá trị gần nhất, sắp xếp theo topic).
- Giữ kết nối: `data: ping` mỗi **15s**.
- Event cập nhật attr:
```json
data: {"event":"ATTR_UPDATED","device_id":"sign_0","attr":"0110","value":"ON"}
```
- Có ack:
```json
data: {"event":"ATTR_UPDATED","device_id":"sign_0","attr":"0110","value":"ON","ack_command_id":"cmd_fe_..."}
```
- Online:
```json
data: {"event":"ATTR_UPDATED","device_id":"sign_0","attr":"online","value":true,"online":true}
```
- `attr` là **attr_id** (YAML), `value` là **string** (`"ON"`/`"OFF"` hoặc số dạng chuỗi).

---

## 5. App LVGL: luồng xử lý

### a. Khởi tạo (`main.c`)
1. `config_load()` + `config_apply_brightness()`.
2. `ota_init()`.
3. `lv_init()` → framebuffer `/dev/fb0` → evdev `/dev/input/event0` → `ui_init()`.
4. `http_client_init()`: catalog → states → SSE thread.
5. `ui_start_timers()` → `ota_signal_success()`.
6. `bms_init()`: tạo màn hình Overview (nếu catalog rỗng → fallback `bms_yaml_load`).
7. **Main loop**: `lv_tick_inc` → `lv_timer_handler()` → `http_client_poll()` → `usleep(1000)`.

### b. Xử lý catalog (`http_fetch_catalog`)
- Parse bằng cách anchor vào chuỗi `"display_attrs"` (serde_json xuất key sắp xếp: `display_attrs` đứng **trước** `id`), giới hạn trong object của device.
- **Quan trọng**: `type`/`name`/`id` của device phải parse **từ vị trí `"id"` trở đi** (`idmark`) vì các attr cũng có field `"type"` (bool/number) đứng trước — nếu không sẽ lấy nhầm `type` của attr (bug cũ làm Sign bị render bằng template AC).
- Mỗi attr: `attr_id`, `label`, `unit`, `type` (bool/number), `overview`, `control`.
- Điền vào `bms_device_t` (`display_attr_*`, max 16 attrs).

### c. Xử lý SSE (`bms_handle_api_event`)
- Nhận `device_id`, `attr`, `value`, `ack_command_id` từ queue.
- Có `ack_command_id` → `bms_ack_command()` (unlock device).
- `attr == "online"` → cập nhật `d->online` + `bms_refresh_card_style`.
- **Đang có lệnh pending cho device → bỏ qua attr update "stale"** (tránh nhảy state).
- Attr bool:
  - Gán `display_attr_values[ai] = 1/0`.
  - Nếu `control` → `d->enabled = on` + `bms_refresh_card_style` + `bms_refresh_switch` + **`bms_rebuild_active_screen()`** (rebuild immediate — bỏ qua `throttled_rebuild` để nút ON/OFF không bị treo state cũ khi poll dày).
- Attr number: `value * 10` (fixed-point), map `0202`→value, `0203`→room_temp, `0405`→fan_speed; `throttled_rebuild_screens`.

### d. Điều khiển (`bms_set_onoff`)
- Nút **BẬT/TẮT** (`onoff_btns`) dùng chung cho card AC/Sign/Switch (cả Overview lẫn tab expanded) — mỗi nút gửi lệnh xác định `TURN_ON`/`TURN_OFF` thay vì toggle.
- Trước khi gửi: kiểm tra `bms_device_locked()` → tạo pending slot (CMD_TOGGLE, lưu `prev_bool_val`) → `bms_issue_action()` → gửi POST actions.
- `bms_process_pending()` chạy mỗi 250ms: hết deadline **5s** chưa ack → `bms_rollback()` hoàn trả giá trị cũ + rebuild.

### e. Màn hình
| Screen | Nội dung |
|--------|----------|
| Overview | Header, navbar 6 tab, sections theo type: AIR CONDITIONING / SIGNAGE / POWER METER / LIGHT SENSOR / SWITCHES / QUICK SCENES |
| AC | Summary + `ac_expanded_card`: bool → nút BẬT/TẮT; `0405` → fan Low/Med/High/Auto; number → stepper −/+; room temp hiển thị |
| Sign | Summary + `sign_expanded_card`: nút BẬT/TẮT (46px, container 48px) + các attr số |
| Power | Tổng năng lượng/công suất + 3 cột pha (V/A/W/PF) |
| Light | `light_sensor_expanded_card`: illuminance |
| Switch | Summary + `switch_expanded_card`: nút BẬT/TẮT |

- Overview card chỉ hiện attr số có `overview:true` (VD Light Sensor cần khai `overview:true` trong devices.yaml mới hiện name+value).

---

## 6. Cấu trúc Thư mục và File Quan trọng

```
Device/Luckfoxpico86/code/
├── lvgl_project/           # App (C++/LVGL) — build: make clean && make
│   ├── Makefile            # Cross-compile arm-linux-gnueabihf, APP_VERSION
│   ├── main.c              # Entry point, init display/evdev/http_client/UI
│   ├── devices.yaml        # Nguồn device (sync lên board /home/pico/devices.yaml)
│   ├── src/
│   │   ├── http_client.c   # REST + SSE client (catalog, states, actions, queue)
│   │   ├── bms.c           # Logic BMS + render UI (overview, tabs, pending/ack)
│   │   ├── bms_yaml.c      # Fallback load devices.yaml khi catalog rỗng
│   │   ├── config.c        # app_config.txt (brightness, pincode, ota_url, ...)
│   │   ├── ota.c           # OTA state machine
│   │   ├── ui.c / ui_helpers.c / settings.c / system_monitor.c
│   │   └── mqtt_broker.c   # MQTT (không còn dùng cho luồng BMS mới)
│   └── lv_conf.h           # Cấu hình LVGL
├── Rush_engine/            # Backend (Rust) — build: build.sh (cargo-zigbuild armv7)
│   ├── src/                # 13 module (mục 1.a)
│   ├── config/
│   │   ├── config.yaml     # MQTT, xsolar, database, http, devices_file, ota
│   │   └── rules.yaml      # Rule engine (mqtt/time triggers)
│   └── build.sh / deploy.sh
└── doc/
    ├── architecture.md     # (tài liệu này)
    ├── api_redesign.md     # Thiết kế REST API (đã triển khai)
    └── canopi_mqtt_engine_kien_truc_review.md
```

---

## 7. Quy trình Triển khai (Deployment)

### a. Build App
```bash
cd Device/Luckfoxpico86/code/lvgl_project
make clean && make                     # → app + ota/app_v<VERSION> + ota/check.json
make APP_VERSION=1.2.3                 # build phiên bản OTA cụ thể
```
- `make debug` → `app_debug` (symbols, no optimization).

### b. Build Engine
```bash
cd Device/Luckfoxpico86/code/Rush_engine
./build.sh      # cross-compile armv7-unknown-linux-musleabihf (cargo-zigbuild, static)
./deploy.sh     # push binary + config lên board
```

### c. Triển khai lên Board
1. Copy binary `app` vào `/home/pico/app` (nhớ `systemctl stop myapp` trước khi thay — file đang chạy).
2. Copy `devices.yaml` vào `/home/pico/devices.yaml` (engine hot-reload sau 5s, không cần restart).
3. Engine: `systemctl restart bms-engine` (nếu đổi binary/config).
4. App: `systemctl start myapp`.
   - App restart sẽ fetch lại catalog → render đủ thiết bị.
   - `myapp.service` có `ExecStartPre=/bin/sleep 30` (chờ engine + wifi sẵn sàng).

---

## 8. Ghi chú Kỹ thuật

- **Main loop App**: `usleep(1000)` (~1ms), gọi `lv_timer_handler()` + `http_client_poll()`.
- **Đa luồng**: không gọi API LVGL từ thread ngoài main thread. SSE thread chỉ đẩy vào queue (mutex), main loop mới update UI.
- **Kết nối**: NanoMQ `127.0.0.1:1883`, Engine API `127.0.0.1:8080`, xsolar `mqtt.xsolar.energy:1883` (topic `smarteos/bluCafe/...`).
- **SQLite**: WAL mode, `busy_timeout=5000`, `synchronous=NORMAL`; hot-path đọc từ in-memory cache, SQLite dành cho history/audit.
- **Bug lịch sử đã fix**:
  - Parse catalog đặt đúng anchor `"display_attrs"` + parse type từ `"id"` (trước đây Sign bị render bằng template AC).
  - Nút điều khiển bool → rebuild immediate (`bms_rebuild_active_screen`) thay vì `throttled_rebuild` (tránh treo state khi poll dày).
  - Các card AC/Switch đổi sang nút BẬT/TẮT (style Sign) — bỏ toggle `lv_switch` ở Overview lẫn tab expanded.
  - Light Sensor card cần attr `overview:true` trong devices.yaml để hiển thị giá trị ở Overview.
