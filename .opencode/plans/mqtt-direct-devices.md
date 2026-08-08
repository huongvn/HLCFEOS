# Plan: Hỗ trợ thiết bị MQTT trực tiếp (nối thẳng vào Luckfox, không qua gateway Zigbee)

> Trạng thái: **draft — chờ review** · Ngày: 2026-08-08

## 1. Bối cảnh & mục tiêu

Hệ thống hiện chỉ xử lý thiết bị Zigbee đi qua gateway Tasmota (`tele/<gw>/SENSOR` → `ZbReceived`, điều khiển qua `cmnd/<gw>/ZbSend`). Yêu cầu mới: hỗ trợ thiết bị **MQTT trực tiếp** — publish/subscribe thẳng vào NanoMQ trên Luckfox, **không qua gateway Zigbee**, không dùng `zigbee_addr`.

Yêu cầu đã chốt với user:
- Làm **tổng quát** cho mọi loại thiết bị MQTT.
- Hiển thị & điều khiển được trên **HMI** (card có sẵn, ko thêm card mới).
- Xác định online/offline qua **LWT hoặc last_seen timeout**.
- Payload lệnh dùng **template tùy thiết bị (prefix/suffix)**.

## 2. Tham chiếu hiện tại

- `Rush_engine/src/device_manager.rs`
  - `load_devices()` (L84-204): `HashMap<zigbee_addr, Device>`; **skip device rỗng `zigbee_addr`** (line 109-116) — phải sửa để cho phép mqtt device.
  - `DeviceMetadata`: chỉ có `ac_index, sign_index, power_index, light_sensor_index` — **thiếu `switch_index`**.
  - `slug_and_index` (L269): có branch `"switch"` nhưng index rơi vào `sign_index` (default `_ =>`) — **sai/thiếu**, cần dùng `switch_index`.
  - `catalog_json` (L333): dùng `device_id_of` → tự render đúng nếu thêm switch_index.
- `main.rs`
  - `process_mqtt_message` (L690-747): route policy — `bms/…/set`, `/LWT` (gateway, L886 `handle_gateway_lwt`), `tele/…/SENSOR` (L886-884 `process_zigbee_message`).
  - `process_zigbee_message` (L749-884): normalize attribute → fan-out SQLite, `command_tracker.reconcile`, xsolar cache, `hmi_bridge.publish_feedback`.
  - Sweep offline (L553-586): loop `dm.get_all_devices()`, check `device_last_seen` theo key = `zigbee_addr`.
  - `resolve_device_action` (L1009-1059): rule action device → `queue_mgr.send_zbsend` — cần branch mqtt.
  - `emit_device_online` (L918-950): chỉ ac/mcb/power/light — **thiếu switch**.
- `hmi_bridge.rs`
  - `do_write` (L159-188): build write_dict → `send_zbsend`; feedback optimistic qua `device_topic`.
  - `device_topic` (L204-212): chỉ ac/mcb/power/light — **thiếu switch**.
  - `publish_feedback` (L433+): chỉ ac/sign/power/light — **thiếu switch**.
- LVGL (không cần sửa):
  - `DEV_TYPE_SWITCH`, `switch_index`, topic `BMS_TOPIC_SWITCH_POWER=“bms/switch/%d/power”`, `SWITCH_POWER_SET`, `SWITCH_ONLINE` (`bms.h:66-68`) — sẵn sàng nhận.

## 3. Thiết kế khai báo (`devices.yaml`)

```yaml
- device_id: 6
  protocol: mqtt              # NEW - mặc định “zigbee” khi thiếu
  nr_type: switch
  name: "Cửa cuốn WiFi"
  group: "Back"
  switch_index: 0             # field mới → id HMI = "switch_0"
  mqtt:
    state_topic: "dev/shade/state"      # nơi device publish state
    command_topic: "dev/shade/cmnd"     # nơi engine publish lệnh
    lwt_topic: "dev/shade/lwt"          # tùy chọn; bỏ → dùng last_seen timeout
    command_template:                   # template payload khi SEND
      prefix: '{"cmnd":"'
      value_key: "POWER"                # key giá trị trong payload
      suffix: '"}'
  enabled: true
  attributes:
    - id: "power"             # key trong JSON device gửi lên
      label: "Power"
      type: bool
      display: true
      control: true           # control → bật/tắt → publish lên command_topic
```

- Device `protocol: mqtt` **không cần** `zigbee_addr`.
- Key trong `DeviceManager.devices`: mqtt device dùng key = `mqtt:{state_topic}` (tạo unique); zigbee giữ nguyên = `zigbee_addr`.

## 4. Thay đổi theo file

### 4.1 `device_manager.rs`
- Struct: `Device` thêm `protocol: String` (default `"zigbee"`) + `mqtt_cfg: Option<MqttCfg>`.
  ```rust
  pub struct MqttCfg { pub state_topic: String, pub command_topic: String, pub lwt_topic: Option<String>, pub command_template: Option<MqttCommandTemplate> }
  pub struct MqttCommandTemplate { pub prefix: String, pub value_key: String, pub suffix: String }
  ```
- `load_devices`: bỏ skip rỗng `zigbee_addr` khi `protocol == "mqtt"`; parse block `mqtt:`. Device `key` helper `device_key()` = `zigbee_addr` hoặc `mqtt:{state_topic}`.
- `DeviceMetadata` thêm `switch_index: Option<i64>`; parse `switch_index` trong YAML.
- `slug_and_index`: `"switch"` → dùng `switch_index || sign_index` (ưu tiên switch_index).
- `get_device_by_type_index`: `"switch"` → match `switch_index` (`|| sign_index`).
- Thêm `find_by_state_topic(topic)` , `find_by_lwt_topic(topic)` (duyệt `devices`).

### 4.2 `main.rs`
- `process_mqtt_message` (L690): **trước** nhánh `tele/`, thêm:
  - topic == `state_topic` của device mqtt → `process_mqtt_device_message` (clone body `process_zigbee_message`, nhưng payload là JSON object trực tiếp, key map theo `attr_id`; update `device_last_seen` theo device key; online/offline; SQLite; reconcile; xsolar; hmi feedback).
  - topic == `lwt_topic` → parse `"online"/"offline"/"true"/"false"` → `emit_device_online`.
- `handle_gateway_lwt` (L886): skip device `protocol != "zigbee"` (mqtt device không có gateway).
- `emit_device_online` (L918): thêm nhánh `"switch"` → phát `bms/switch/{idx}/online`.
- `resolve_device_action` (L1009): branch `protocol == "mqtt"` → gọi `send_command` (publish command_topic) thay `send_zbsend`.
- Sweep offline: ghi `last_seen` bằng `device.key` (đồng nhất cho cả zigbee lẫn mqtt).

### 4.3 `hmi_bridge.rs`
- Tách phổ biến `send_command(device, attr_id, value) -> bool`:
  - mqtt → dựng payload `prefix + value_key + JSON(value) + suffix`, publish thẳng qua `mqtt_client.publish(command_topic, QoS::AtLeastOnce)`.
  - zigbee → `send_zbsend` như cũ.
- `do_write` → gọi `send_command`.
- `device_topic` (L204): thêm `"switch" → ("switch", switch_index)`, feedback optimistic hoạt động.
- `publish_feedback`: nhánh switch phát `bms/switch/{idx}/power`.

### 4.4 Không cần sửa
- LVGL: tự render `switch_0` (card Switch sẵn có).
- `rules.yaml`: action `mqtt_publish` đã có; action device chạy qua `resolve_device_action` (đã branch).

## 5. Hành vi online / offline
- Có `lwt_topic` → dùng LWT thiết bị (payload ON/OFF, true/false, online/offline).
- Không có → dựa `last_seen` timeout (sweep hiện có, áp dụng cho device key).

## 6. Test (sau khi implement)
- Build: luôn `cargo zigbuild --release --target armv7-unknown-linux-musleabihf` (không target = x86-64 sai).
- Deploy board; thêm 1 mqtt device mẫu trong config.
- Mô phỏng bằng `mosquitto_pub`:
  - Gửi `dev/shade/state` `{"power":1}` → check `catalog`/state, dot xanh, log.
  - Gửi lệnh toggle qua `/api/v1/devices/switch_0/actions` → kiểm tra message trên `dev/shade/cmnd`.
  - Gửi LWT offline → dot đỏ; gửi lại state → dot xanh.
  - Trường hợp không LWT: dừng publish → chờ sweep → dot đỏ.
- Bump `bms-v` → workflow release → OTA board.

## 7. Rủi ro / quyết định mở
- Mỗi mqtt device trỏ 1 `state_topic` cố định; chưa hỗ trợ wildcard (`+`) cho nhiều con/1 chủ đề. (Giữ simple; mở rộng sau nếu cần.)
- Quy ước key value trong payload state = `attr_id` thẳng (đơn giản); nếu thiết bị dùng key khác → thêm `mqtt_key` riêng tiếp theo.
- Feedback optimistic phụ thuộc `device_topic` (đã sửa cho switch).
</content>