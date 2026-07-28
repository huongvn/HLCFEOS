# Kiến Trúc Hệ Thống BMS Engine

## Tổng quan

Hệ thống BMS Engine sử dụng **NanoMQ làm MQTT bus nội bộ** tại `localhost:1883`. Tất cả giao tiếp giữa các components đều đi qua NanoMQ.

## Sơ đồ kiến trúc

```
┌──────────────────────────────────────────────────────────────────────┐
│                         NanoMQ (localhost:1883)                       │
│                         MQTT Bus Nội Bộ                               │
└──────────────────────────────────────────────────────────────────────┘
         │              │              │              │              │
         │              │              │              │              │
    ┌────┴────┐   ┌─────┴─────┐  ┌────┴────┐  ┌─────┴─────┐  ┌────┴────┐
    │  LVGL   │   │  Tasmota  │  │ Python  │  │  xsolar   │  │  Rule   │
    │  App    │   │  Gateway  │  │   BMS   │  │  Bridge   │  │ Engine  │
    │ (UI)    │   │ (Zigbee)  │  │ Engine  │  │ (Cloud)   │  │         │
    └─────────┘   └───────────┘  └─────────┘  └───────────┘  └─────────┘
         │              │              │              │              │
    bms/ac/+/set   tele/+/SENSOR    # (all)    smarteos/+/set    (internal)
```

## Components

### 1. NanoMQ (MQTT Broker)

**Vai trò:** Central message bus cho toàn bộ hệ thống

**Cấu hình:**
- Address: `localhost:1883`
- Protocol: MQTT v3.1.1 / v5
- Max connections: 64
- Keepalive: 60s

**Topics quản lý:**
- Tất cả topics được tự động tạo khi có publisher/subscriber
- Không cần cấu hình trước

### 2. LVGL App (UI Client)

**Vai trò:** Giao diện người dùng trên màn hình Luckfox

**Giao tiếp:**
- **Publish:** Gửi lệnh điều khiển
  - `bms/ac/{idx}/power/set` - Bật/tắt AC
  - `bms/ac/{idx}/temperature/set` - Đặt nhiệt độ
  - `bms/ac/{idx}/fan/set` - Đặt tốc độ quạt
  - `bms/sign/{idx}/power/set` - Bật/tắt Sign
  - `bms/scene/master` - Scene control

- **Subscribe:** Nhận feedback
  - `bms/#` - Tất cả BMS topics
  - `bms/ac/{idx}/power` - Trạng thái AC
  - `bms/ac/{idx}/temperature` - Nhiệt độ
  - `bms/ac/{idx}/room_temp` - Nhiệt độ phòng
  - `bms/ac/{idx}/fan` - Tốc độ quạt
  - `bms/sign/{idx}/power` - Trạng thái Sign

**QoS:** 1 (at least once)

### 3. Tasmota Gateway (Zigbee Bridge)

**Vai trò:** Bridge giữa Zigbee devices và MQTT

**Giao tiếp:**
- **Publish:** Sensor data từ Zigbee devices
  - `tele/tasmota_6DCAA8/SENSOR` - Zigbee sensor data
  - `tele/tasmota_6DCAA8/LWT` - Online/Offline status
  - `tele/tasmota_6DCAA8/STATE` - Gateway state

- **Subscribe:** Commands từ BMS Engine
  - `cmnd/tasmota_6DCAA8/ZbSend` - Zigbee commands

**Payload format:**
```json
{
  "ZbReceived": {
    "0xC5A9": {
      "Device": "0xC5A9",
      "EF00/0101": 1,
      "EF00/0202": 24,
      "Endpoint": 1,
      "LinkQuality": 120
    }
  }
}
```

**QoS:** 1

### 4. Python BMS Engine (Core Logic)

**Vai trò:** Central brain - xử lý logic, lưu trữ, điều phối

**Modules:**
- **MQTT Client:** Subscribe `#` để nhận tất cả messages
- **HMI Bridge:** Giao tiếp với LVGL
- **Xsolar Bridge:** Giao tiếp với xsolar cloud
- **Rule Engine:** Automation rules
- **State Manager:** SQLite storage
- **Device Manager:** Device registry

**Giao tiếp:**
- **Subscribe:** `#` (tất cả topics)
- **Publish:** 
  - `cmnd/+/ZbSend` - Zigbee commands
  - `bms/ac/+/+` - AC feedback
  - `bms/sign/+/power` - Sign feedback

**QoS:** 1

### 5. Xsolar Bridge (Cloud Integration)

**Vai trò:** Bridge giữa BMS Engine và xsolar cloud

**Giao tiếp:**
- **Publish lên xsolar:**
  - `smarteos/bluCafe/{device_id}` - Device states
  - Frequency: Every 10 minutes (periodic) + on change (event-driven)

- **Subscribe từ xsolar:**
  - `smarteos/bluCafe/+/set` - Remote commands

**External broker:** `mqtt.xsolar.energy:1883`

**QoS:** 
- Publish: 0 (fire and forget)
- Subscribe: 1 (at least once)

### 6. Rule Engine (Automation)

**Vai trò:** Execute automation rules dựa trên triggers/conditions

**Giao tiếp:**
- **Listen:** Tất cả MQTT messages
- **Publish:** Commands khi rule triggered
  - `cmnd/+/ZbSend` - Zigbee commands

**Internal only:** Không giao tiếp trực tiếp với external components

## Luồng dữ liệu chi tiết

### Luồng 1: LVGL điều khiển AC

```
1. User bấm nút "Bật AC" trên LVGL
   ↓
2. LVGL publish
   Topic: bms/ac/0/power/set
   Payload: "ON"
   QoS: 1
   ↓
3. NanoMQ route message
   ↓
4. BMS Engine nhận message (subscribe #)
   ↓
5. HMI Bridge.handle_message()
   - Parse topic: bms/ac/0/power/set
   - Extract: idx=0, attr=power
   ↓
6. Device Manager lookup
   - ac_index=0 → device 0xC5A9
   ↓
7. Build ZbSend command
   - power → EF00/0101
   - ON → 1
   ↓
8. MQTT Client publish
   Topic: cmnd/tasmota_6DCAA8/ZbSend
   Payload: {"Device":"0xC5A9","Write":{"EF00/0101":1}}
   QoS: 1
   ↓
9. NanoMQ route message
   ↓
10. Tasmota Gateway nhận command
    ↓
11. Tasmota gửi Zigbee command
    ↓
12. AC device bật/tắt
    ↓
13. AC device gửi confirmation
    ↓
14. Tasmota publish SENSOR
    Topic: tele/tasmota_6DCAA8/SENSOR
    Payload: {"ZbReceived":{"0xC5A9":{"EF00/0101":1}}}
    ↓
15. NanoMQ route message
    ↓
16. BMS Engine nhận message
    ↓
17. State Manager update SQLite
    ↓
18. HMI Bridge publish feedback
    Topic: bms/ac/0/power
    Payload: "ON"
    ↓
19. NanoMQ route message
    ↓
20. LVGL nhận feedback
    ↓
21. LVGL update UI
```

### Luồng 2: xsolar điều khiển AC từ cloud

```
1. xsolar cloud gửi command
   Topic: smarteos/bluCafe/0xC5A9/set
   Payload: {"power":"ON","temperature":24}
   ↓
2. mqtt.xsolar.energy:1883 (external broker)
   ↓
3. BMS Engine (mqtt_xsolar) nhận message
   ↓
4. Xsolar Bridge.handle_message()
   - Parse topic: smarteos/bluCafe/0xC5A9/set
   - Extract: device_id=0xC5A9
   ↓
5. Device Manager lookup
   - device_id=0xC5A9 → AC Zone A
   ↓
6. Build ZbSend command
   - power → EF00/0101 = 1
   - temperature → EF00/0202 = 24
   ↓
7. MQTT Client (mqtt_local) publish
   Topic: cmnd/tasmota_6DCAA8/ZbSend
   Payload: {"Device":"0xC5A9","Write":{"EF00/0101":1,"EF00/0202":24}}
   QoS: 1
   ↓
8. NanoMQ route message
   ↓
9. Tasmota Gateway → Zigbee device
   ↓
10. AC device bật + đặt nhiệt độ 24°C
```

### Luồng 3: BMS Engine push data lên xsolar

```
1. Scheduler trigger (every 10 minutes)
   ↓
2. Xsolar Bridge.push_all_states()
   ↓
3. Loop qua tất cả devices
   ↓
4. State Manager.get_latest_state(device_addr)
   ↓
5. Build xsolar payload
   {
     "ts": "2026-01-17T10:30:00+07:00",
     "site": "bluCafe",
     "id": "0xC5A9",
     "type": "ac_controller",
     "name": "AC Zone A",
     "location": "Zone A",
     "data": {
       "power": true,
       "temperature": 24,
       "room_temp": 28,
       "fan": 3
     }
   }
   ↓
6. MQTT Client (mqtt_xsolar) publish
   Topic: smarteos/bluCafe/0xC5A9
   Payload: (JSON above)
   QoS: 0
   ↓
7. mqtt.xsolar.energy:1883 (external broker)
   ↓
8. xsolar cloud nhận data
```

## Topic naming convention

### Internal topics (NanoMQ)

**BMS Control:**
- `bms/{device_type}/{idx}/{attr}/set` - Commands từ LVGL
- `bms/{device_type}/{idx}/{attr}` - Feedback về LVGL

**Ví dụ:**
- `bms/ac/0/power/set` - Bật/tắt AC index 0
- `bms/ac/0/power` - Trạng thái AC index 0
- `bms/sign/0/power/set` - Bật/tắt Sign index 0

**Tasmota:**
- `tele/{gateway}/SENSOR` - Sensor data
- `tele/{gateway}/LWT` - Online/Offline
- `tele/{gateway}/STATE` - Gateway state
- `cmnd/{gateway}/ZbSend` - Zigbee commands

**Scene:**
- `bms/scene/master` - Scene control

### External topics (xsolar)

**Publish:**
- `smarteos/bluCafe/{device_id}` - Device states

**Subscribe:**
- `smarteos/bluCafe/{device_id}/set` - Remote commands

## QoS levels

| Component | Publish QoS | Subscribe QoS | Reason |
|-----------|-------------|---------------|--------|
| LVGL | 1 | 1 | Đảm bảo lệnh được nhận |
| Tasmota | 1 | 1 | Đảm bảo sensor data được nhận |
| BMS Engine | 1 | 1 | Đảm bảo commands được nhận |
| Xsolar Bridge | 0 | 1 | Push data không cần guarantee |
| Rule Engine | 1 | - | Internal only |

## Retain flags

| Topic | Retain | Reason |
|-------|--------|--------|
| `bms/ac/+/power` | false | State thay đổi liên tục |
| `bms/ac/+/temperature` | false | State thay đổi liên tục |
| `bms/sign/+/power` | false | State thay đổi liên tục |
| `smarteos/bluCafe/+` | false | Push data không cần retain |
| `tele/+/LWT` | true | Last Will and Testament |

## Security

### NanoMQ (internal)

- **Authentication:** None (localhost only)
- **TLS:** Disabled (internal network)
- **Access control:** None (trusted components)

### Xsolar (external)

- **Authentication:** Username/password (config)
- **TLS:** Optional (config)
- **Access control:** Broker-side ACL

## Performance

### NanoMQ

- **RAM usage:** ~5MB
- **CPU usage:** <1% (idle), 5-10% (active)
- **Message throughput:** 10,000+ msg/s
- **Latency:** <1ms (localhost)

### BMS Engine

- **RAM usage:** ~30MB
- **CPU usage:** <5% (idle), 10-20% (active)
- **Message processing:** 1,000+ msg/s
- **SQLite writes:** 100+ writes/s

## Scalability

### Current limits

- **Max devices:** 12 (configurable in devices.yaml)
- **Max gateways:** 1 (Tasmota)
- **Max LVGL clients:** 1
- **Max xsolar connections:** 1

### Future enhancements

1. **Multiple gateways:** Support nhiều Tasmota gateways
2. **Multiple LVGL clients:** Support nhiều UI clients
3. **Multiple xsolar connections:** Support multiple cloud endpoints
4. **MQTT clustering:** NanoMQ cluster cho high availability

## Troubleshooting

### NanoMQ không khởi động

```bash
sudo systemctl status nanomq
sudo journalctl -u nanomq -f
```

### BMS Engine không kết nối được NanoMQ

```bash
# Kiểm tra NanoMQ có chạy không
sudo systemctl status nanomq

# Test kết nối
mosquitto_pub -h localhost -p 1883 -t "test" -m "hello"

# Xem logs
sudo journalctl -u bms-engine -f
```

### LVGL không nhận được feedback

```bash
# Subscribe để debug
mosquitto_sub -h localhost -p 1883 -t "bms/#" -v

# Kiểm tra BMS Engine có publish không
sudo journalctl -u bms-engine -f | grep "HMI Bridge"
```

### xsolar không nhận được data

```bash
# Kiểm tra kết nối xsolar broker
mosquitto_pub -h mqtt.xsolar.energy -p 1883 -t "test" -m "hello"

# Xem logs
sudo journalctl -u bms-engine -f | grep "Xsolar Bridge"
```

## Monitoring

### Metrics

- **NanoMQ:** Built-in metrics qua `$SYS/#` topics
- **BMS Engine:** Logging + SQLite stats
- **LVGL:** UI status indicators
- **Xsolar:** Cloud dashboard

### Alerts

- NanoMQ down → System alert
- BMS Engine crash → Auto-restart (systemd)
- xsolar connection lost → Retry + log
- Device offline → LWT detection

## Backup & Recovery

### NanoMQ

- Không cần backup (stateless)
- Auto-restart khi crash

### BMS Engine

- SQLite database: `/data/bms/bms.db`
- Backup: `sqlite3 /data/bms/bms.db ".backup /backup/bms.db"`
- Recovery: Restore từ backup

### Configuration

- `config/config.yaml`: Version controlled (Git)
- `devices.yaml`: Version controlled (Git)
- `rules.yaml`: Version controlled (Git)

## Conclusion

NanoMQ đóng vai trò là **central message bus** cho toàn bộ hệ thống, cung cấp:
- **Decoupled architecture:** Components không phụ thuộc trực tiếp
- **Scalability:** Dễ dàng thêm components mới
- **Reliability:** MQTT protocol với QoS, retain, LWT
- **Simplicity:** Single protocol cho tất cả giao tiếp

Kiến trúc này cho phép hệ thống phát triển linh hoạt trong khi vẫn giữ được sự đơn giản và dễ maintain.
