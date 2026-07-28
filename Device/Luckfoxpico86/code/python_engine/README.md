# BMS Engine - Python Building Management System

Python-based BMS engine thay thế Node-RED, sử dụng `devices.yaml` làm source of truth.

## Tính năng

- ✅ MQTT client cho device communication
- ✅ SQLite database cho state storage
- ✅ Rule engine với triggers, conditions, actions
- ✅ Scheduler cho periodic tasks
- ✅ Push data lên xsolar mỗi 10 phút
- ✅ Hỗ trợ composite attributes (MCB dp6 decode)
- ✅ Auto-reconnect MQTT

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
│   └── scheduler.py         # Time-based scheduling
├── devices.yaml             # Source of truth (symlink từ lvgl_project)
├── requirements.txt         # Python dependencies
├── deploy.sh                # Deployment script
└── bms-engine.service       # Systemd service
```

## Cài đặt

### 1. Clone hoặc copy project

```bash
cd /home/pico/HLCFEOS/Device/Luckfoxpico86/code/python_engine
```

### 2. Tạo symlink đến devices.yaml

```bash
ln -s ../lvgl_project/devices.yaml devices.yaml
```

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
  path: "/data/bms/bms.db"

devices_file: "/home/pico/HLCFEOS/Device/Luckfoxpico86/code/lvgl_project/devices.yaml"
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

## So sánh với Node-RED

| Aspect | Node-RED | Python BMS Engine |
|--------|----------|-------------------|
| Deploy | Import flow qua UI | `git pull && systemctl restart` |
| RAM | ~100MB | ~30MB |
| Debug | Debug tab | `journalctl -f` |
| Version control | JSON files | Python files |
| Maintenance | UI-based | Code-based |

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
sqlite3 /data/bms/bms.db ".tables"
sqlite3 /data/bms/bms.db "SELECT COUNT(*) FROM device_metric;"
```

## Development

### Chạy manual (không qua systemd)

```bash
cd /home/pico/HLCFEOS/Device/Luckfoxpico86/code/python_engine
python3 src/main.py
```

### Thêm rule mới

Chỉnh sửa `config/rules.yaml` và restart service:

```bash
sudo systemctl restart bms-engine
```

### Thêm device mới

Chỉnh sửa `devices.yaml` trong `lvgl_project` và restart service.

## License

MIT
