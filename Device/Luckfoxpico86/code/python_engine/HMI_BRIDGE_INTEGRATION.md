# HMI Bridge Integration - Hoàn thành

## Tổng kết

Đã implement và tích hợp thành công module `hmi_bridge.py` vào Python BMS Engine. Module này thay thế hoàn toàn flow `08-hmi-bridge.json` trong Node-RED.

## Files đã tạo/sửa

### 1. `src/hmi_bridge.py` (MỚI)
- Module giao tiếp giữa LVGL app và Zigbee devices
- Nhận lệnh từ LVGL qua MQTT topics
- Convert thành ZbSend commands
- Publish feedback về LVGL khi device state thay đổi

### 2. `src/main.py` (SỬA)
- Import `HMIBridge` module
- Khởi tạo `hmi_bridge` trong `__init__`
- Start `hmi_bridge` trong `start()`
- Route HMI commands đến `hmi_bridge` trong `_handle_mqtt_message`
- Publish feedback trong `_process_zigbee_message`

### 3. `src/HMI_BRIDGE.md` (MỚI)
- Tài liệu chi tiết về HMI Bridge
- Hướng dẫn sử dụng và debug
- API reference

## Chức năng

### 1. Nhận lệnh từ LVGL

**AC Control:**
- `bms/ac/{idx}/power/set` → Bật/tắt AC
- `bms/ac/{idx}/temperature/set` → Đặt nhiệt độ
- `bms/ac/{idx}/fan/set` → Đặt tốc độ quạt

**Sign/MCB Control:**
- `bms/sign/{idx}/power/set` → Bật/tắt Sign

**Scene Control:**
- `bms/scene/master` → Open/Close Store

### 2. Convert thành ZbSend

```python
# LVGL: bms/ac/0/power/set = "ON"
# → ZbSend: {"Device":"0xC5A9","Write":{"EF00/0101":1}}
```

### 3. Publish feedback

```python
# Device state thay đổi
# → LVGL: bms/ac/0/power = "ON"
```

## Luồng dữ liệu

### Điều khiển (LVGL → Device)

```
LVGL → MQTT → HMI Bridge → ZbSend → Gateway → Device
```

### Feedback (Device → LVGL)

```
Device → Gateway → MQTT → BMS Engine → HMI Bridge → MQTT → LVGL
```

## Testing

### Test từ command line

```bash
# Bật AC Zone A
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/power/set" -m "ON"

# Đặt nhiệt độ 24°C
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/temperature/set" -m "24"

# Đặt tốc độ quạt High
mosquitto_pub -h localhost -p 1883 -t "bms/ac/0/fan/set" -m "3"

# Bật Sign
mosquitto_pub -h localhost -p 1883 -t "bms/sign/0/power/set" -m "ON"

# Scene Open Store
mosquitto_pub -h localhost -p 1883 -t "bms/scene/master" -m "ON"
```

### Xem logs

```bash
sudo journalctl -u bms-engine -f | grep HMI
```

## So sánh với Node-RED

| Aspect | Node-RED | Python |
|--------|----------|--------|
| Logic | Visual flow | Code-based |
| Debug | Debug nodes | Logging |
| Update | Import flow | Git pull |
| Performance | Medium | High |
| Maintainability | Medium | High |

## Tích hợp hoàn chỉnh

Python BMS Engine giờ đã có đầy đủ các module:

1. ✅ **MQTT Client** - Giao tiếp với NanoMQ
2. ✅ **State Manager** - Lưu trữ state trong SQLite
3. ✅ **Device Manager** - Quản lý devices từ devices.yaml
4. ✅ **Rule Engine** - Automation rules
5. ✅ **Scheduler** - Periodic tasks (push xsolar)
6. ✅ **HMI Bridge** - Giao tiếp với LVGL app
7. ✅ **Xsolar Push** - Push data lên xsolar mỗi 10 phút

## Deployment

```bash
cd /home/pico/HLCFEOS/Device/Luckfoxpico86/code/python_engine
sudo ./deploy.sh
```

## Next steps

1. Test trên Luckfox với LVGL app thực tế
2. Verify feedback loop hoạt động đúng
3. Monitor logs để debug nếu có vấn đề
4. Commit code lên Git

## Notes

- HMI Bridge sử dụng `devices.yaml` để map index → device
- Index được định nghĩa trong `devices.yaml` (ac_index, sign_index)
- Feedback được gửi tự động khi device state thay đổi
- Scene control bật/tắt tất cả devices cùng loại
