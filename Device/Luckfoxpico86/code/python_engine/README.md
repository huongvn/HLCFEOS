# BMS Engine - Python Building Management System

Python-based BMS engine thay thế Node-RED, sử dụng `devices.yaml` làm source of truth.

## Tính năng

- ✅ MQTT client cho device communication
- ✅ SQLite database cho state storage
- ✅ Rule engine với triggers, conditions, actions
- ✅ Scheduler cho periodic tasks
- ✅ HMI Bridge - giao tiếp với LVGL app qua NanoMQ
- ✅ Xsolar Bridge - giao tiếp với xsolar cloud
  - Periodic push device states mỗi 10 phút
  - Event-driven push khi state thay đổi
  - Remote control từ xsolar (điều khiển AC từ cloud)
- ✅ Hỗ trợ composite attributes (MCB dp6 decode)
- ✅ Auto-reconnect MQTT
- ✅ Scene control (Open/Close Store)

## Cấu trúc project

```
python_engine/
├── config/
│   ├── config.yaml          # Cấu hình chính
│   └── rules.yaml           # Automation rules
├── src/
│   ├── main.py              # Entry point
│   ├── mqtt_client.py       # MQTT client
│   ├── state_manager.py     # SQLite state management
│   ├── device_manager.py    # Load devices from devices.yaml
│   ├── rule_engine.py       # Rule evaluation
│   ├── scheduler.py         # Time-based scheduling
│   ├── hmi_bridge.py        # Giao tiếp với LVGL app
│   └── xsolar_bridge.py     # Giao tiếp với xsolar cloud
├── devices.yaml             # Source of truth (symlink → /home/pico/devices.yaml)
├── requirements.txt         # Python dependencies
├── deploy.sh                # Deployment script
└── bms-engine.service       # Systemd service
```

## Kiến trúc

Hệ thống sử dụng **NanoMQ làm MQTT bus nội bộ** tại `localhost:1883`:

```
┌─────────────────────────────────────────────────────────────┐
│                    NanoMQ (localhost:1883)                   │
│                    MQTT Bus Nội Bộ                           │
└─────────────────────────────────────────────────────────────┘
         │              │              │              │
    ┌────┴────┐   ┌─────┴─────┐  ┌────┴────┐  ┌─────┴─────┐
    │  LVGL   │   │  Tasmota  │  │ Python  │  │  xsolar   │
    │  App    │   │  Gateway  │  │   BMS   │  │  Bridge   │
    │ (UI)    │   │ (Zigbee)  │  │ Engine  │  │ (Cloud)   │
    └─────────┘   └───────────┘  └─────────┘  └───────────┘
```

Tất cả giao tiếp đều đi qua NanoMQ:
- LVGL ↔ BMS Engine: `bms/ac/+/set`, `bms/ac/+`
- Tasmota ↔ BMS Engine: `tele/+/SENSOR`, `cmnd/+/ZbSend`
- xsolar ↔ BMS Engine: `smarteos/bluCafe/+` (external broker)

Xem chi tiết: [PlanAndDoc/ARCHITECTURE.md](PlanAndDoc/ARCHITECTURE.md)

## Cài đặt

### 1. Clone hoặc copy project

```bash
cd /home/pico/python_engine
```

### 2. Tạo symlink đến devices.yaml

```bash
cd /home/pico/python_engine
ln -s ../devices.yaml devices.yaml
```

File `devices.yaml` nằm tại `/home/pico/devices.yaml`, dùng chung cho cả LVGL app và Python engine.

### 3. Chỉnh sửa config

```bash
nano config/config.yaml
```

Đảm bảo đường dẫn đến `devices.yaml` đúng.

### 4. Deploy

```bash
sudo ./deploy.sh
```

Script sẽ:
- Cài đặt Python dependencies
- Tạo log file
- Cài đặt systemd service
- Khởi động service

## Kiểm tra

### Xem logs

```bash
# Systemd journal
sudo journalctl -u bms-engine -f

# Log file
sudo tail -f /var/log/bms-engine.log
```

### Kiểm tra service status

```bash
sudo systemctl status bms-engine
```

### Test MQTT connection

```bash
# Subscribe to see device messages
mosquitto_sub -h localhost -p 1883 -t "tele/tasmota_6DCAA8/SENSOR" -v
```

## Cấu hình

### config.yaml

```yaml
mqtt:
  broker: "localhost"
  port: 1883
  client_id: "bms-engine"

xsolar:
  broker: "mqtt.xsolar.energy"
  port: 1883
  topic_prefix: "smarteos/bluCafe"
  push_interval: 600  # 10 minutes

database:
  path: "data/bms.db"

devices_file: "/home/pico/python_engine/devices.yaml"
rules_file: "config/rules.yaml"
```

### rules.yaml

Ví dụ rule:

```yaml
rules:
  - alias: "bat den khi toi va co nguoi"
    enabled: true
    triggers:
      - type: mqtt
        topic: "zigbee/motion/state"
    conditions:
      - type: numeric_state
        topic: "zigbee/light_sensor/lux"
        below: 50
      - type: time
        after: "18:00:00"
        before: "23:00:00"
    actions:
      - type: mqtt_publish
        topic: "zigbee/switch/cmd"
        payload: "ON"
```

## MQTT Topics

### LVGL ↔ BMS Engine (qua NanoMQ)

| Topic | Direction | Mô tả |
|-------|-----------|-------|
| `bms/ac/{idx}/{attr_id}/set` | LVGL → BMS | Điều khiển AC (attr_id từ YAML: "0101"=Power, "0202"=Temp, "0405"=Fan) |
| `bms/ac/{idx}/{attr_id}` | BMS → LVGL | Feedback AC |
| `bms/sign/{idx}/{attr_id}/set` | LVGL → BMS | Điều khiển Sign |
| `bms/sign/{idx}/{attr_id}` | BMS → LVGL | Feedback Sign |
| `bms/scene/master` | LVGL → BMS | Scene control |

### Tasmota ↔ BMS Engine (qua NanoMQ)

| Topic | Direction | Mô tả |
|-------|-----------|-------|
| `tele/tasmota_6DCAA8/SENSOR` | Tasmota → BMS | Zigbee sensor data |
| `tele/tasmota_6DCAA8/LWT` | Tasmota → BMS | Online/Offline status |
| `cmnd/tasmota_6DCAA8/ZbSend` | BMS → Tasmota | Zigbee commands |

### xsolar ↔ BMS Engine

| Topic | Direction | Broker | Mô tả |
|-------|-----------|--------|-------|
| `smarteos/bluCafe/{device_id}` | BMS → xsolar | mqtt.xsolar.energy | Device states |
| `smarteos/bluCafe/{device_id}/set` | xsolar → BMS | mqtt.xsolar.energy | Remote commands |

## HMI Bridge

HMI Bridge giao tiếp với LVGL app qua NanoMQ:

### Nhận lệnh từ LVGL

```bash
# Bật AC Zone A (attr_id "0101" = Power)
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/0101/set" -m "ON"

# Đặt nhiệt độ 24°C (attr_id "0202" = Temperature)
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/0202/set" -m "24"

# Đặt tốc độ quạt High=3 (attr_id "0405" = Fan Speed)
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/0405/set" -m "3"

# Bật Sign (attr_id "0110" = Control)
mosquitto_pub -h localhost -p 1883 -t "bms/sign/0/0110/set" -m "ON"

# Scene Open Store
mosquitto_pub -h localhost -p 1883 -t "bms/scene/master" -m "ON"
```

### Xem feedback

```bash
# Subscribe tất cả feedback
mosquitto_sub -h localhost -p 1883 -t "bms/#" -v

# Chỉ xem AC feedback
mosquitto_sub -h localhost -p 1883 -t "bms/ac/#" -v
```

Xem chi tiết: [src/HMI_BRIDGE.md](src/HMI_BRIDGE.md)

## Xsolar Bridge

Xsolar Bridge giao tiếp với xsolar cloud:

### Periodic push (mỗi 10 phút)

Tự động push tất cả device states lên xsolar mỗi 10 phút.

### Event-driven push

Push ngay khi device state thay đổi (rate limit: 5 giây/device).

### Remote control từ xsolar

xsolar có thể điều khiển AC từ xa:

```bash
# Từ xsolar broker
mosquitto_pub -h mqtt.xsolar.energy -p 1883 \
  -t "smarteos/bluCafe/0xC5A9/set" \
  -m '{"power":"ON","temperature":24,"fan":2}'
```

Payload format:
```json
{
  "power": "ON",
  "temperature": 24,
  "fan": 2
}
```

Tất cả fields là optional, chỉ gửi field cần điều khiển.

## So sánh với Node-RED

| Aspect | Node-RED | Python BMS Engine |
|--------|----------|-------------------|
| Deploy | Import flow qua UI | `git pull && systemctl restart` |
| RAM | ~100MB | ~30MB |
| Debug | Debug tab | `journalctl -f` |
| Version control | JSON files | Python files |
| Maintenance | UI-based | Code-based |
| HMI Bridge | Flow 08-hmi-bridge.json | hmi_bridge.py |
| Xsolar Bridge | Flow 10-remote-bridge.json | xsolar_bridge.py |
| Rule Engine | Flow-based | Code-based với YAML config |
| Performance | Medium | High |

## Troubleshooting

### Service không khởi động

```bash
# Kiểm tra logs
sudo journalctl -u bms-engine -n 50

# Kiểm tra config
python3 -c "import yaml; yaml.safe_load(open('config/config.yaml'))"
```

### MQTT không kết nối

```bash
# Kiểm tra NanoMQ
sudo systemctl status nanomq

# Test connection
mosquitto_pub -h localhost -p 1883 -t "test" -m "hello"
```

### Database lỗi

```bash
# Kiểm tra database
sqlite3 data/bms.db ".tables"
sqlite3 data/bms.db "SELECT COUNT(*) FROM device_metric;"
```

### HMI Bridge không hoạt động

```bash
# Kiểm tra LVGL có publish không
mosquitto_sub -h localhost -p 1883 -t "bms/#" -v

# Kiểm tra BMS Engine có nhận không
sudo journalctl -u bms-engine -f | grep "HMI Bridge"

# Kiểm tra BMS Engine có gửi ZbSend không
mosquitto_sub -h localhost -p 1883 -t "cmnd/+/ZbSend" -v
```

### Xsolar Bridge không hoạt động

```bash
# Kiểm tra kết nối xsolar broker
mosquitto_pub -h mqtt.xsolar.energy -p 1883 -t "test" -m "hello"

# Kiểm tra logs
sudo journalctl -u bms-engine -f | grep "Xsolar Bridge"

# Test remote command
mosquitto_sub -h mqtt.xsolar.energy -p 1883 -t "smarteos/bluCafe/#" -v
```

### Device không được tìm thấy

```bash
# Kiểm tra devices.yaml có device không
cat /home/pico/devices.yaml

# Restart để reload devices.yaml
sudo systemctl restart bms-engine
```

## Development

### Chạy manual (không qua systemd)

```bash
cd /home/pico/python_engine
python3 src/main.py
```

### Thêm rule mới

Chỉnh sửa `config/rules.yaml` và restart service:

```bash
sudo systemctl restart bms-engine
```

### Thêm device mới

Chỉnh sửa `/home/pico/devices.yaml` và restart service.

### Testing

#### Test HMI Bridge

```bash
# Bật AC Zone A
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/0101/set" -m "ON"

# Xem feedback
mosquitto_sub -h localhost -p 1883 -t "bms/ac/0/0101" -v
```

#### Test Xsolar Bridge

```bash
# Xem data push lên xsolar
mosquitto_sub -h mqtt.xsolar.energy -p 1883 -t "smarteos/bluCafe/#" -v

# Test remote command từ xsolar
mosquitto_pub -h mqtt.xsolar.energy -p 1883 \
  -t "smarteos/bluCafe/0xC5A9/set" \
  -m '{"power":"ON","temperature":24}'
```

#### Xem tất cả logs

```bash
# Systemd journal
sudo journalctl -u bms-engine -f

# Filter theo module
sudo journalctl -u bms-engine -f | grep "HMI Bridge"
sudo journalctl -u bms-engine -f | grep "Xsolar Bridge"
sudo journalctl -u bms-engine -f | grep "Rule Engine"
```

## Documentation

Tất cả tài liệu được đặt trong thư mục `PlanAndDoc/`:

- [PlanAndDoc/ARCHITECTURE.md](PlanAndDoc/ARCHITECTURE.md) - Kiến trúc hệ thống chi tiết
- [PlanAndDoc/OTA_PLAN.md](PlanAndDoc/OTA_PLAN.md) - Kế hoạch OTA update
- [PlanAndDoc/OTA_IMPLEMENTATION.md](PlanAndDoc/OTA_IMPLEMENTATION.md) - OTA implementation guide
- [PlanAndDoc/PYTHON_BMS_ENGINE_PLAN.md](PlanAndDoc/PYTHON_BMS_ENGINE_PLAN.md) - Kế hoạch phát triển ban đầu
- [PlanAndDoc/HMI_BRIDGE_INTEGRATION.md](PlanAndDoc/HMI_BRIDGE_INTEGRATION.md) - HMI Bridge integration guide
- [src/HMI_BRIDGE.md](src/HMI_BRIDGE.md) - HMI Bridge documentation

## License

MIT
