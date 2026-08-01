# Tài liệu Tích hợp MQTT — Smart Cafe Energy System

> **Mục đích:** Hướng dẫn bên tích hợp (xsolar) nhận dữ liệu + điều khiển thiết bị Smart Cafe qua MQTT.
> 
> **Ngày cập nhật:** 31/07/2026  
> **Phiên bản:** v4.0  
> **Hệ thống gửi dữ liệu:** Python BMS Engine trên Luckfox Core1106  
> **Hệ thống nhận dữ liệu:** `mqtt.xsolar.energy:1883`

---

## 1. Tổng quan

Mỗi thiết bị gửi dữ liệu qua **topic riêng** `smarteos/bluCafe/<device_id>`.

Cơ chế gửi:
- **Event-driven:** Khi 1 thiết bị thay đổi trạng thái → gửi snapshot của chính device đó (rate limit 5s)
- **Periodic:** Mỗi **10 phút** gửi toàn bộ device, mỗi device 1 message riêng

Timestamp theo múi giờ **Asia/Ho_Chi_Minh (GMT+7)**, format `YYYY-MM-DDTHH:mm:ss+07:00`.

---

## 2. Thông tin kết nối MQTT

| Thông số | Giá trị |
|----------|---------|
| Broker | `mqtt.xsolar.energy` |
| Port | `1883` |
| Protocol | MQTT v3.1.1 |
| QoS | `0` |
| Retain | `false` |
| Topic subscribe | `smarteos/bluCafe/#` |
| Auth | Có (user/pass — liên hệ Smart Cafe team) |

---

## 3. Topic

Mỗi device có 1 topic riêng:

```
smarteos/bluCafe/0xC5A9   → AC Zone B
smarteos/bluCafe/0x336A   → AC Zone A
smarteos/bluCafe/0x8CCC   → Sign Board
smarteos/bluCafe/0x51DF   → Power Meter ZA
smarteos/bluCafe/0x8DF5   → Power Meter ZB
smarteos/bluCafe/0x8BA4   → Light Sensor
```

Bên xsolar subscribe wildcard **`smarteos/bluCafe/#`** để nhận tất cả device. Mỗi message nhận được là snapshot của **1 thiết bị**.

---

## 4. Cấu trúc Payload (JSON)

### 4.1 AC Controller

```json
Topic: smarteos/bluCafe/0xC5A9

{
  "ts": "2026-07-31T18:33:24+07:00",
  "site": "bluCafe",
  "id": "0xC5A9",
  "type": "ac_controller",
  "name": "AC Zone B",
  "location": "Zone B",
  "data": {
    "Power": true,
    "Temperature": 24,
    "Room Temp": 28,
    "Fan Speed": 2
  }
}
```

### 4.2 MCB (Sign Board)

```json
Topic: smarteos/bluCafe/0x8CCC

{
  "ts": "2026-07-31T18:33:24+07:00",
  "site": "bluCafe",
  "id": "0x8CCC",
  "type": "mcb",
  "name": "Sign Board",
  "location": "Outdoor",
  "data": {
    "Control": true,
    "Energy": 303.5,
    "Power": 800,
    "Temperature": 30.1,
    "Voltage": 234.5,
    "Current": 2.5
  }
}
```

### 4.3 Power Meter (3-phase)

```json
Topic: smarteos/bluCafe/0x51DF

{
  "ts": "2026-07-31T18:33:24+07:00",
  "site": "bluCafe",
  "id": "0x51DF",
  "type": "power_meter",
  "name": "Power Meter ZA",
  "location": "Electrical Panel",
  "data": {
    "Voltage A": 224.35,
    "Current A": 2.35,
    "Active Power A": 425,
    "Reactive Power A": -118,
    "Apparent Power A": 529,
    "PF A": 0.8,
    "Voltage B": 221.07,
    "Current B": 2.47,
    "Active Power B": 462,
    "Reactive Power B": -114,
    "Apparent Power B": 547,
    "PF B": 0.84,
    "Voltage C": 221.87,
    "Current C": 2.27,
    "Active Power C": 403,
    "Reactive Power C": -99,
    "Apparent Power C": 504,
    "PF C": 0.79,
    "Total Power": 1291,
    "Total Reactive": -332,
    "Total Apparent": 1582,
    "Frequency": 50.01,
    "Total Energy": 209.4,
    "Energy Received": 0.27
  }
}
```

### 4.4 Light Sensor

```json
Topic: smarteos/bluCafe/0x8BA4

{
  "ts": "2026-07-31T18:33:24+07:00",
  "site": "bluCafe",
  "id": "0x8BA4",
  "type": "light_sensor",
  "name": "Light Sensor",
  "location": "Outdoor",
  "data": {
    "Illuminance": 668.0
  }
}
```

---

### 4.5 Cấu trúc chung

| Trường (root) | Kiểu | Mô tả |
|---------------|------|-------|
| `ts` | string | Timestamp ISO 8601 GMT+7 |
| `site` | string | Tên site: `"bluCafe"` |
| `id` | string | Zigbee address của thiết bị |
| `type` | string | Loại: `ac_controller`, `mcb`, `power_meter`, `light_sensor` |
| `name` | string | Tên hiển thị |
| `location` | string | Vị trí/group |
| `data` | object | Các thuộc tính (key = label trong devices.yaml) |

> **Lưu ý:** Key trong `data` là **label** tiếng Anh từ file cấu hình `devices.yaml`. Khi thêm device mới, chỉ cần thêm vào file cấu hình — không cần sửa code.

---

## 5. Tần suất gửi

| Cơ chế | Kích hoạt | Rate limit |
|--------|-----------|------------|
| **Event-driven** | Khi attribute thay đổi giá trị | 5s/device |
| **Periodic** | Mỗi 10 phút | Gửi snapshot toàn bộ |

---

## 6. Danh sách thiết bị hiện tại

| Device ID | Type | Name | Location |
|-----------|------|------|----------|
| `0x336A` | `ac_controller` | AC Zone A | Zone A |
| `0x8CCC` | `mcb` | Sign Board | Outdoor |
| `0xC5A9` | `ac_controller` | AC Zone B | Zone B |
| `0x51DF` | `power_meter` | Power Meter ZA | Electrical Panel |
| `0x8DF5` | `power_meter` | Power Meter ZB | Electrical Panel |
| `0x8BA4` | `light_sensor` | Light Sensor | Outdoor |

---

## 7. Remote Control (xsolar → Smart Cafe)

xsolar có thể điều khiển thiết bị từ xa qua MQTT.

### Topic format

```
smarteos/bluCafe/{device_id}/set
```

### Payload format

Key trong payload phải khớp với **label** trong `devices.yaml`. Tất cả fields là optional.

#### AC Controller (`0xC5A9`, `0x336A`)

| Field | Type | Values | Mô tả |
|-------|------|--------|-------|
| `Power` | string | `"ON"`, `"OFF"` | Bật/tắt |
| `Temperature` | number | 16-32 | Nhiệt độ cài đặt (°C) |
| `Fan Speed` | number | 0-3 | 0=Low, 1=Med, 2=High, 3=Auto |

#### MCB Sign Board (`0x8CCC`)

| Field | Type | Values | Mô tả |
|-------|------|--------|-------|
| `Control` | string | `"ON"`, `"OFF"` | Bật/tắt |

### Ví dụ

```json
// Bật AC Zone B
Topic: smarteos/bluCafe/0xC5A9/set
Payload: {"Power": "ON"}

// Đặt nhiệt độ 24°C
Topic: smarteos/bluCafe/0xC5A9/set
Payload: {"Temperature": 24}

// Tắt Sign Board
Topic: smarteos/bluCafe/0x8CCC/set
Payload: {"Control": "OFF"}
```

### Feedback
Sau khi gửi lệnh, state mới sẽ tự động publish về topic `smarteos/bluCafe/{device_id}`.

---

## 8. Liên hệ & Hỗ trợ

| Vấn đề | Liên hệ |
|--------|---------|
| Auth / user-pass | Smart Cafe team |
| Không nhận được dữ liệu | Kiểm tra `ts` trong payload có cập nhật không |
| Thêm thiết bị mới | Cập nhật `devices.yaml` → tự động push |

---

*Document v4.0 — Python BMS Engine*
