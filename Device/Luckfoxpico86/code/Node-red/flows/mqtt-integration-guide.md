# Tài liệu Tích hợp MQTT — Smart Cafe Energy System

> **Mục đích:** Hướng dẫn bên tích hợp (xsolar) nắm cấu trúc topic MQTT để hiển thị dữ liệu lên web app.
> 
> **Ngày cập nhật:** 04/07/2026  
> **Phiên bản:** v1.0  
> **Hệ thống gửi dữ liệu:** Node-RED BMS tại quán (Luckfox Core1106)  
> **Hệ thống nhận dữ liệu:** `mqtt.xsolar.energy:1883`

---

## 1. Tổng quan

Hệ thống gửi dữ liệu theo 2 cơ chế:
- **Event-driven:** Khi trạng thái thiết bị thay đổi (bật/tắt, nhiệt độ thay đổi...)
- **Periodic:** Gửi toàn bộ snapshot mỗi **10 phút**

Tất cả message dùng **JSON payload**, timestamp theo múi giờ **Asia/Ho_Chi_Minh (GMT+7)**.

---

## 2. Thông tin kết nối MQTT

| Thông số | Giá trị |
|----------|---------|
| Broker | `mqtt.xsolar.energy` |
| Port | `1883` |
| Protocol | MQTT v4 |
| QoS | `0` |
| Retain | `false` |
| Auth | *(sẽ cung cấp sau)* |

> **Lưu ý:** Hệ thống chỉ **PUBLISH** (gửi dữ liệu đi). Không subscribe nhận lệnh từ bên ngoài.

---

## 3. Cấu trúc Topic

```
smarteos/bluCafe/<device_type>/<device_id>/<attribute>
```

| Phần | Ý nghĩa | Ví dụ |
|------|---------|-------|
| `smarteos/bluCafe` | Prefix cố định | — |
| `<device_type>` | Loại thiết bị: `ac_controller` hoặc `mcb` | `ac_controller` |
| `<device_id>` | ID Zigbee (hex) | `0xC5A9`, `0x336A`, `0x384C` |
| `<attribute>` | Thuộc tính cụ thể | `temperature`, `current_power` |

---

## 4. Danh sách Thiết bị Hiện tại

### 4.1 AC Controller — Khu vực C & D

Cấu trúc giống nhau, chỉ khác `device_id`.

| Topic pattern | Mô tả | Đơn vị | Ví dụ payload |
|---------------|-------|--------|---------------|
| `smarteos/bluCafe/ac_controller/<id>/power` | Trạng thái bật/tắt | — | `{"ts":"2026-07-04T10:30:00+07:00","value":true,"name":"Power Status","device_name":"AC Khu C","location":"zone_C"}` |
| `smarteos/bluCafe/ac_controller/<id>/temperature` | Nhiệt độ cài đặt | °C | `{"value":24,"name":"Set Temperature","unit":"°C",...}` |
| `smarteos/bluCafe/ac_controller/<id>/room_temp` | Nhiệt độ phòng (nếu có) | °C | `{"value":26.5,...}` |
| `smarteos/bluCafe/ac_controller/<id>/ambient_temp` | Nhiệt độ phòng (fallback) | °C | `{"value":26.5,...}` |
| `smarteos/bluCafe/ac_controller/<id>/online` | Trạng thái online | — | `{"value":true,...}` |
| `smarteos/bluCafe/ac_controller/<id>/link_quality` | Chất lượng kết nối Zigbee | — | `{"value":107,...}` |

**Device ID hiện tại:**
- Khu vực C: `0xC5A9`
- Khu vực D: `0x336A`

### 4.2 MCB (Contactor) — Khu vực A (Zone A)

| Topic | Mô tả | Đơn vị | Ví dụ payload |
|-------|-------|--------|---------------|
| `smarteos/bluCafe/mcb/0x384C/control` | Trạng thái relay bật/tắt | — | `{"value":true,"name":"Relay Status",...}` |
| `smarteos/bluCafe/mcb/0x384C/temp` | Nhiệt độ thiết bị | °C | `{"value":29.7,"name":"Device Temperature","unit":"°C",...}` |
| `smarteos/bluCafe/mcb/0x384C/meas_046E` | Nhiệt độ thiết bị (fallback) | °C | `{"value":13,...}` |
| `smarteos/bluCafe/mcb/0x384C/current_power` | Công suất hiện tại | W | `{"value":800,"name":"Current Power","unit":"W",...}` |
| `smarteos/bluCafe/mcb/0x384C/current_energy` | Năng lượng tiêu thụ | Wh | `{"value":303,"name":"Current Energy","unit":"Wh",...}` |
| `smarteos/bluCafe/mcb/0x384C/current_ampe` | Dòng điện hiện tại | A | `{"value":63,"name":"Current Amperage","unit":"A",...}` |
| `smarteos/bluCafe/mcb/0x384C/current_voltage` | Điện áp hiện tại | V | `{"value":220,"name":"Current Voltage","unit":"V",...}` |
| `smarteos/bluCafe/mcb/0x384C/rated_current` | Dòng điện định mức | A | `{"value":63,"name":"Rated Current","unit":"A",...}` |
| `smarteos/bluCafe/mcb/0x384C/high_voltage_cutoff` | Ngưỡng điện áp cao | V | `{"value":280,"name":"High Voltage Cutoff","unit":"V",...}` |
| `smarteos/bluCafe/mcb/0x384C/low_voltage_cutoff` | Ngưỡng điện áp thấp | V | `{"value":165,"name":"Low Voltage Cutoff","unit":"V",...}` |
| `smarteos/bluCafe/mcb/0x384C/max_power` | Công suất tối đa | W | `{"value":3500,"name":"Max Power","unit":"W",...}` |
| `smarteos/bluCafe/mcb/0x384C/online` | Trạng thái online | — | `{"value":true,...}` |
| `smarteos/bluCafe/mcb/0x384C/link_quality` | Chất lượng kết nối | — | `{"value":123,...}` |

---

## 5. Cấu trúc Payload (JSON)

```json
{
  "ts": "2026-07-04T10:30:00+07:00",
  "value": 24,
  "name": "Set Temperature",
  "unit": "°C",
  "device_name": "AC Khu C",
  "location": "zone_C"
}
```

| Trường | Kiểu | Bắt buộc | Mô tả |
|--------|------|----------|-------|
| `ts` | string | ✅ | Timestamp ISO 8601, múi giờ GMT+7 (VD: `2026-07-04T10:30:00+07:00`) |
| `value` | any | ✅ | Giá trị thuộc tính (bool, number, string) |
| `name` | string | ✅ | Tên hiển thị của thuộc tính (tiếng Anh) |
| `unit` | string | ❌ | Đơn vị đo (°C, W, Wh, A, V) — chỉ có khi áp dụng |
| `device_name` | string | ✅ | Tên thân thiện của thiết bị |
| `location` | string | ✅ | Vị trí lắp đặt (zone_C, zone_D, zone_A) |

---

## 6. Tần suất Gửi Dữ liệu

| Cơ chế | Kích hoạt | Tần suất tối đa |
|--------|-----------|-----------------|
| **Event-driven** | Khi trạng thái thiết bị thay đổi | 50 msg/s (rate limit) |
| **Periodic** | Mỗi 10 phút (cố định) | Snapshot toàn bộ thiết bị |

> **Lưu ý:** Cả 2 cơ chế đều đi qua queue rate-limit 20 msg/s trước khi publish. Không có message bị drop.

---

## 7. Mẫu Topic cho Thiết bị Tương lai

Khi thêm thiết bị mới (không cần sửa code), hệ thống tự động sinh topic theo mẫu:

```
smarteos/bluCafe/<device_type>/<device_id>/<attribute>
```

### Ví dụ thiết bị mới

| Thiết bị mới | Topic mẫu |
|--------------|-----------|
| Cảm biến ánh sáng | `smarteos/bluCafe/sensor/<id>/lux` |
| Công tắc Zigbee | `smarteos/bluCafe/switch/<id>/power` |
| Đèn LED | `smarteos/bluCafe/light/<id>/brightness` |

**Quy tắc tự động:**
- `<device_type>`: lấy từ `device_config.device_type`
- `<device_id>`: ID Zigbee (VD: `0x384C`)
- `<attribute>`: key trong `device_config.extra.attrs` (VD: `temperature`, `power`, `lux`)

Bên nhận chỉ cần **subscribe wildcard** để tự động nhận thiết bị mới:

```
smarteos/bluCafe/+/+/+
```

---

## 8. Tóm tắt Số lượng Topic

| Loại thiết bị | Số topic / device | Số device | Tổng topic |
|---------------|-------------------|-----------|------------|
| AC Controller | 6 | 2 (0xC5A9, 0x336A) | 12 |
| MCB | 14 | 1 (0x384C) | 14 |
| **Tổng cộng hiện tại** | — | **3** | **26 topic** |

> Số lượng có thể tăng khi thêm thiết bị mới (data-driven, không cần sửa code).

---

## 9. Liên hệ & Hỗ trợ

| Vấn đề | Liên hệ |
|--------|---------|
| Cấu hình broker / auth | Bên phía Smart Cafe |
| Thiếu dữ liệu / offline | Kiểm tra `*/online` topic |
| Thêm thiết bị mới | Bên phía Smart Cafe cập nhật `device_config` |

---

*Document generated for xsolar.energy integration team.*
