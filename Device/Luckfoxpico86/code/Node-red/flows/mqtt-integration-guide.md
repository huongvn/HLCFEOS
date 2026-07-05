# Tài liệu Tích hợp MQTT — Smart Cafe Energy System

> **Mục đích:** Hướng dẫn bên tích hợp (xsolar) nhận dữ liệu từ hệ thống Smart Cafe qua MQTT.
> 
> **Ngày cập nhật:** 04/07/2026  
> **Phiên bản:** v2.0  
> **Hệ thống gửi dữ liệu:** Node-RED BMS tại quán (Luckfox Core1106)  
> **Hệ thống nhận dữ liệu:** `mqtt.xsolar.energy:1883`

---

## 1. Tổng quan

Hệ thống gửi dữ liệu qua **1 topic MQTT duy nhất**. Mỗi message chứa **snapshot toàn bộ trạng thái** của tất cả thiết bị tại site.

Cơ chế gửi:
- **Event-driven:** Khi bất kỳ thiết bị nào thay đổi trạng thái -> gửi snapshot toàn bộ site ngay lập tức
- **Periodic:** Gửi snapshot toàn bộ site mỗi **10 phút** (dù không có thay đổi)

Timestamp theo múi giờ **Asia/Ho_Chi_Minh (GMT+7)**.

---

## 2. Thông tin kết nối MQTT

| Thông số | Giá trị |
|----------|---------|
| Broker | `mqtt.xsolar.energy` |
| Port | `1883` |
| Protocol | MQTT v4 |
| QoS | `0` |
| Retain | `false` |
| Topic subscribe | `smarteos/bluCafe` |
| Auth | *(sẽ cung cấp sau)* |

> **Luu ý:** Hệ thống chỉ **PUBLISH** (gửi dữ liệu đi). Không subscribe nhận lệnh từ bên ngoài.

---

## 3. Topic duy nhất

```
smarteos/bluCafe
```

Bên xsolar chỉ cần **subscribe 1 topic này**. Mỗi message nhận được là 1 snapshot đầy đủ.

---

## 4. Cấu trúc Payload (JSON)

```json
{
  "ts": "2026-07-04T10:30:00+07:00",
  "site": "bluCafe",
  "devices": [
    {
      "id": "0xC5A9",
      "type": "ac_controller",
      "name": "AC Khu C",
      "location": "zone_C",
      "data": {
        "power": true,
        "temperature": 24,
        "room_temp": 26.5,
        "online": true,
        "link_quality": 107
      }
    },
    {
      "id": "0x336A",
      "type": "ac_controller",
      "name": "AC Khu D",
      "location": "zone_D",
      "data": {
        "power": false,
        "temperature": 26,
        "room_temp": 28.0,
        "online": true,
        "link_quality": 95
      }
    },
    {
      "id": "0x384C",
      "type": "mcb",
      "name": "MCB Tong zone A",
      "location": "zone_A",
      "data": {
        "control": true,
        "temp": 29.7,
        "current_power": 800,
        "current_energy": 303,
        "current_ampe": 63,
        "current_voltage": 220,
        "high_voltage_cutoff": 280,
        "low_voltage_cutoff": 165,
        "max_power": 3500,
        "online": true,
        "link_quality": 123
      }
    }
  ]
}
```

---

### 4.1 Giải thích các trường

| Trường (root) | Kiểu | Mô tả |
|---------------|------|-------|
| `ts` | string | Timestamp ISO 8601, múi giờ GMT+7 |
| `site` | string | Tên site cố định: `"bluCafe"` |
| `devices` | array | Danh sách tất cả thiết bị |

| Trường (mỗi device) | Kiểu | Mô tả |
|---------------------|------|-------|
| `id` | string | ID Zigbee (VD: `0xC5A9`) |
| `type` | string | Loại thiết bị: `ac_controller`, `mcb` |
| `name` | string | Tên hiển thị |
| `location` | string | Vị trí: `zone_C`, `zone_D`, `zone_A` |
| `data` | object | **Chỉ chứa raw value**, key = tên thuộc tính |

---

### 4.2 Danh sách thuộc tính theo loại thiết bị

**AC Controller (`type: "ac_controller"`)**

| Key trong `data` | Ý nghĩa | Đơn vị | Kiểu |
|------------------|---------|--------|------|
| `power` | Trạng thái bật/tắt | — | bool |
| `temperature` | Nhiệt độ cài đặt | °C | number |
| `room_temp` | Nhiệt độ phòng | °C | number |
| `ambient_temp` | Nhiệt độ phòng (fallback) | °C | number |
| `online` | Trạng thái online | — | bool |
| `link_quality` | Chất lượng kết nối Zigbee | — | number |

**MCB (`type: "mcb"`)**

| Key trong `data` | Ý nghĩa | Đơn vị | Kiểu |
|------------------|---------|--------|------|
| `control` | Trạng thái relay | — | bool |
| `temp` | Nhiệt độ thiết bị | °C | number |
| `meas_046E` | Nhiệt độ thiết bị (fallback) | °C | number |
| `current_power` | Công suất hiện tại | W | number |
| `current_energy` | Năng lượng tiêu thụ | Wh | number |
| `current_ampe` | Dòng điện hiện tại | A | number |
| `current_voltage` | Điện áp hiện tại | V | number |
| `high_voltage_cutoff` | Ngưỡng điện áp cao | V | number |
| `low_voltage_cutoff` | Ngưỡng điện áp thấp | V | number |
| `max_power` | Công suất tối đa | W | number |
| `online` | Trạng thái online | — | bool |
| `link_quality` | Chất lượng kết nối | — | number |

---

## 5. Tần suất Gửi Dữ liệu

| Cơ chế | Kích hoạt | Nội dung |
|--------|-----------|----------|
| **Event-driven** | Khi 1 thiết bị thay đổi trạng thái | Snapshot **toàn bộ** 3 devices |
| **Periodic** | Mỗi 10 phút | Snapshot **toàn bộ** 3 devices |

> **Lưu ý:** Dù chỉ 1 device thay đổi, hệ thống vẫn gửi snapshot toàn bộ site.

---

## 6. Thiết bị hiện tại

| Device ID | Loại | Tên | Vị trí |
|-----------|------|-----|--------|
| `0xC5A9` | AC Controller | AC Khu C | zone_C |
| `0x336A` | AC Controller | AC Khu D | zone_D |
| `0x384C` | MCB | MCB Tong zone A | zone_A |

---

## 7. Khi thêm thiết bị mới

Hệ thống **data-driven**: thêm device mới không cần sửa code.

- `devices[]` sẽ tự động có thêm phần tử mới
- `type`, `id`, `name`, `location` lấy từ `device_config` trong DB
- Các key trong `data{}` lấy từ `device_config.extra.attrs`

Bên xsolar không cần thay đổi gì — chỉ cần parse `devices[]` và xử lý dynamic.

---

## 8. Liên hệ & Hỗ trợ

| Vấn đề | Liên hệ |
|--------|---------|
| Cấu hình broker / auth | Bên phía Smart Cafe |
| Không nhận được dữ liệu | Kiểm tra `ts` trong payload có cập nhật không |
| Thêm thiết bị mới | Bên phía Smart Cafe cập nhật `device_config` |

---

*Document generated for xsolar.energy integration team.*
