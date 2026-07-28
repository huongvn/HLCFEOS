# HMI Bridge Module - Giao tiếp LVGL ↔ NanoMQ

## Tổng quan

HMI Bridge là module trung gian giữa LVGL app (trên Luckfox) và Zigbee devices thông qua NanoMQ broker. Module này thay thế flow `08-hmi-bridge.json` trong Node-RED.

## Chức năng chính

### 1. Nhận lệnh từ LVGL

LVGL app gửi lệnh điều khiển qua các MQTT topics:

```
bms/ac/{idx}/power/set          → Bật/tắt AC
bms/ac/{idx}/temperature/set    → Đặt nhiệt độ AC
bms/ac/{idx}/fan/set            → Đặt tốc độ quạt AC
bms/sign/{idx}/power/set        → Bật/tắt Sign/MCB
bms/scene/master                → Scene (Open/Close Store)
```

**Ví dụ:**
```
Topic: bms/ac/0/power/set
Payload: "ON"
→ AC Zone A (index 0) bật
```

### 2. Convert thành ZbSend commands

HMI Bridge convert các lệnh trên thành ZbSend format cho Tasmota gateway:

```json
{
  "Device": "0xC5A9",
  "Write": {
    "EF00/0101": 1
  }
}
```

**Mapping:**
- `power` → `EF00/{power_attr_id}` (0101 cho AC)
- `temperature` → `EF00/{temp_attr_id}` (0202 cho AC)
- `fan` → `EF00/{fan_attr_id}` (0405 cho AC)
- `control` → `EF00/{control_attr_id}` (0110 cho MCB)

### 3. Publish feedback về LVGL

Khi device state thay đổi, HMI Bridge publish feedback về LVGL:

```
bms/ac/{idx}/power          → Trạng thái AC (ON/OFF)
bms/ac/{idx}/temperature    → Nhiệt độ cài đặt
bms/ac/{idx}/room_temp      → Nhiệt độ phòng
bms/ac/{idx}/fan            → Tốc độ quạt
bms/sign/{idx}/power        → Trạng thái Sign (ON/OFF)
```

**Ví dụ:**
```
Topic: bms/ac/0/temperature
Payload: "24"
→ LVGL cập nhật hiển thị nhiệt độ 24°C
```

## Luồng dữ liệu

### Luồng điều khiển (LVGL → Device)

```
1. LVGL app
   ↓ publish
2. MQTT: bms/ac/0/power/set = "ON"
   ↓
3. HMI Bridge nhận message
   ↓ parse topic
4. Tìm device theo index
   - ac_index = 0 → 0xC5A9 (AC Zone A)
   ↓
5. Build ZbSend command
   - power → EF00/0101
   - ON → 1
   ↓
6. Publish ZbSend
   - Topic: cmnd/tasmota_6DCAA8/ZbSend
   - Payload: {"Device":"0xC5A9","Write":{"EF00/0101":1}}
   ↓
7. Tasmota gateway → Zigbee device
   ↓
8. Device bật/tắt
```

### Luồng feedback (Device → LVGL)

```
1. Zigbee device thay đổi state
   ↓
2. Tasmota gateway publish SENSOR
   - Topic: tele/tasmota_6DCAA8/SENSOR
   - Payload: {"ZbReceived":{"0xC5A9":{"EF00/0101":1}}}
   ↓
3. BMS Engine nhận message
   ↓ parse ZbReceived
4. Normalize và lưu state
   - 0xC5A9: Power = ON
   ↓
5. HMI Bridge publish feedback
   - Topic: bms/ac/0/power
   - Payload: "ON"
   ↓
6. LVGL app nhận feedback
   ↓
7. Cập nhật UI
```

## Cấu hình

### devices.yaml

HMI Bridge sử dụng `devices.yaml` để map index → device:

```yaml
- device_id: 1
  zigbee_addr: "0xC5A9"
  nr_type: ac_controller
  name: "AC Zone A"
  ac_index: 0  # ← Index này được LVGL sử dụng
  attributes:
    - id: "0101"
      label: "Power"
      type: bool
```

### Topic mapping

| LVGL Topic | Attribute | ZbSend Attribute |
|------------|-----------|------------------|
| `bms/ac/0/power/set` | Power | EF00/0101 |
| `bms/ac/0/temperature/set` | Temperature | EF00/0202 |
| `bms/ac/0/fan/set` | Fan Speed | EF00/0405 |
| `bms/sign/0/power/set` | Control | EF00/0110 |

## Scene Control

### Open Store (bms/scene/master = "ON")

1. Bật tất cả AC devices
2. Bật tất cả Sign/MCB devices

### Close Store (bms/scene/master = "OFF")

1. Tắt tất cả AC devices
2. Tắt tất cả Sign/MCB devices

## Debug

### Xem logs

```bash
sudo journalctl -u bms-engine -f | grep HMI
```

### Test từ command line

```bash
# Bật AC Zone A
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/power/set" -m "ON"

# Đặt nhiệt độ 24°C
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/temperature/set" -m "24"

# Đặt tốc độ quạt High (3)
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/fan/set" -m "3"

# Bật Sign
mosquitto_pub -h localhost -p 1883 -t "bms/sign/0/power/set" -m "ON"

# Scene Open Store
mosquitto_pub -h localhost -p 1883 -t "bms/scene/master" -m "ON"
```

### Subscribe feedback

```bash
# Xem tất cả feedback
mosquitto_sub -h localhost -p 1883 -t "bms/#" -v

# Chỉ xem AC feedback
mosquitto_sub -h localhost -p 1883 -t "bms/ac/#" -v
```

## So sánh với Node-RED

| Aspect | Node-RED (08-hmi-bridge.json) | Python (hmi_bridge.py) |
|--------|-------------------------------|------------------------|
| Logic | Visual flow | Code-based |
| Debug | Debug nodes | Logging |
| Update | Import flow | Git pull |
| Performance | Medium | High |
| Maintainability | Medium | High |

## Troubleshooting

### Lệnh không được thực thi

1. Kiểm tra LVGL có publish đúng topic không
2. Kiểm tra BMS Engine có nhận message không
3. Kiểm tra logs: `sudo journalctl -u bms-engine -f`

### Feedback không được gửi về LVGL

1. Kiểm tra device có gửi SENSOR message không
2. Kiểm tra BMS Engine có parse được không
3. Kiểm tra logs: `sudo journalctl -u bms-engine -f`

### Device không được tìm thấy

1. Kiểm tra `devices.yaml` có device với index tương ứng không
2. Kiểm tra `ac_index` hoặc `sign_index` có đúng không
3. Restart BMS Engine để reload `devices.yaml`

## API Reference

### HMIBridge class

```python
class HMIBridge:
    def __init__(self, mqtt_client, state_manager, device_manager):
        """Khởi tạo HMI Bridge"""
    
    def start(self):
        """Start HMI Bridge - subscribe các topics"""
    
    def handle_message(self, topic: str, payload: Any):
        """Xử lý message từ LVGL"""
    
    def publish_feedback(self, device_addr: str, state: Dict):
        """Publish feedback về LVGL khi device state thay đổi"""
```

## Tích hợp với các module khác

- **MQTTClient**: Dùng để subscribe/publish messages
- **StateManager**: Dùng để lấy device state
- **DeviceManager**: Dùng để map index → device
- **Main**: Khởi tạo và start HMI Bridge

## Future enhancements

1. **Rate limiting**: Giới hạn số lệnh/giây để tránh spam
2. **Command queue**: Queue các lệnh khi gateway offline
3. **Retry mechanism**: Retry khi lệnh không được thực thi
4. **Command history**: Lưu lịch sử lệnh để debug
5. **Scene customization**: Cho phép custom scene qua config
