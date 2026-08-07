# Plan: Redesign API Design (App LVGL ↔ Rush Engine)

## 1. Mục tiêu
Thay thế cơ chế giao tiếp dựa trên cấu trúc Topic MQTT (ví dụ: `bms/ac/0/0101/set`) bằng một bộ REST API hướng đối tượng (Object-Oriented API) tường minh hơn.

### Lý do thay đổi:
- **Giảm phụ thuộc**: App không cần biết cấu trúc topic MQTT của Gateway/Zigbee.
- **Tính trừu tượng**: Tách biệt giữa "Hành động" (Action) và "Giao thức thực thi" (MQTT/Zigbee).
- **Dễ bảo trì**: Khi thay đổi phần cứng hoặc cấu trúc topic MQTT, chỉ cần sửa tại Engine, không cần build lại App.
- **Tính an toàn**: Kiểm soát chặt chẽ kiểu dữ liệu qua JSON schema thay vì chuỗi payload tùy ý.

---

## 2. Thiết kế API mới (Proposed REST API)

### a. Quản lý Thiết bị (Device Management)
Sử dụng `device_id` (định danh duy nhất khai báo trong `devices.yaml`) thay vì dùng index số.

| Method | Endpoint | Mô tả | Request Body | Ví dụ Response |
| :--- | :--- | :--- | :--- | :--- |
| `GET` | `/api/v1/devices/catalog` | Lấy danh sách thiết bị kèm cấu hình hiển thị (không gồm giá trị realtime) | N/A | Xem mục 6.c |
| `GET` | `/api/v1/devices` | Lấy danh sách thiết bị + giá trị realtime | N/A | `[{"id":"ac_01","state":"ON","attrs":{...}}]` |

### b. Lấy trạng thái thiết bị (Query State)
Dùng để vẽ control khi mở App hoặc khi đồng bộ định kỳ.

- **Endpoint**: `GET /api/v1/devices/{device_id}/state`
- **Response `200 OK`:**
```json
{
  "device_id": "relay_01",
  "state": "OFF",
  "attrs": { "power": "OFF", "energy": 12.5 },
  "last_updated": 1785984000,
  "source": "SENSOR"
}
```
- **Giải thích field:**
  - `state`: Trạng thái thực tế hiện tại (`"ON"` | `"OFF"`).
  - `attrs`: Bản đồ `attr_name` → giá trị hiện tại (số/bool/string).
  - `last_updated`: Timestamp cập nhật gần nhất.
  - `source`: (Tùy chọn) Nguồn tác động gần nhất: `USER`, `SENSOR`, `TIMER`, `SCHEDULER`.

### c. Gửi lệnh điều khiển (Control Action API)

> **Nguyên tắc cốt lõi**: Frontend (App LVGL / Dashboard) **KHÔNG gửi "trạng thái mới"** mà gửi một **Yêu cầu Hành động (Action Command)** kèm `command_id` (mã giao dịch do Frontend tự sinh). Backend (Engine) xác nhận kết quả **bất đồng bộ** qua SSE.

- **Endpoint**: `POST /api/v1/devices/{device_id}/actions`
- **Request Body** (áp dụng cho mọi loại device):
```json
{
  "command_id": "cmd_fe_123456",
  "action": "TOGGLE",
  "params": {}
}
```

#### Bảng Action theo loại thiết bị (Action Vocabulary)

| Device type | Action | Params | Mô tả |
| :--- | :--- | :--- | :--- |
| **Switch** | `TURN_ON` / `TURN_OFF` / `TOGGLE` | — | Bật / Tắt / Đảo trạng thái relay |
| **Sign** | `TURN_ON` / `TURN_OFF` / `TOGGLE` | — | Bật / Tắt / Đảo trạng thái biển quảng cáo |
| **AC** | `TURN_ON` / `TURN_OFF` / `TOGGLE` | — | Bật / Tắt / Đảo trạng thái AC |
| **AC** | `SET_ATTRIBUTE` | `{"attr":"target_temp","value":24}` | Đặt nhiệt độ (stepper) |
| **AC** | `SET_ATTRIBUTE` | `{"attr":"fan","value":2}` | Đặt tốc độ quạt (Low/Med/High/Auto) |
| **Scene** | `TURN_ON` | — | Open Store |
| **Scene** | `TURN_OFF` | — | Close Store |

> Scene dùng riêng endpoint: `POST /api/v1/scenes/{scene_id}/actions` (cùng cấu trúc `command_id` + `action`).

### d. Phản hồi bất đồng bộ (Async Response)

Do phần cứng mất thời gian thực thi (hoặc phản hồi trễ), Engine trả về **`202 Accepted`** ngay lập tức để không giữ HTTP Request treo (khuyên dùng cho IoT):

- **Response `202 Accepted`:**
```json
{
  "command_id": "cmd_fe_123456",
  "status": "PENDING",
  "message": "Lệnh đã gửi tới thiết bị, đang chờ thực thi."
}
```

- **Response `200 OK`** (chế độ đồng bộ tùy chọn — Engine chờ phần cứng xong mới trả, dùng khi debug):
```json
{
  "command_id": "cmd_fe_123456",
  "status": "SUCCESS",
  "current_state": "ON"
}
```

- **Các mã lỗi HTTP:**
  | Status | Ý nghĩa |
  | :--- | :--- |
  | `404` | Không tìm thấy `device_id` / `scene_id` |
  | `409` | Thiết bị offline hoặc đang bận (busy) |
  | `422` | `action` không hợp lệ / thiếu `params` cho loại device |
  | `504` | Timeout chờ phần cứng (chỉ ở chế độ đồng bộ) |

### e. SSE: cập nhật kết quả điều khiển + trạng thái thời gian thực

- **Endpoint**: `GET /api/v1/events`
- **Event khi thiết bị đổi trạng thái** (kèm `ack_command_id` để Frontend xác nhận đúng lệnh vừa bấm):
```json
{
  "event": "DEVICE_STATE_CHANGED",
  "device_id": "relay_01",
  "state": "ON",
  "attrs": { "power": "ON" },
  "ack_command_id": "cmd_fe_123456",
  "source": "USER"
}
```
- **Event cập nhật attr thường** (không liên quan tới lệnh điều khiển — từ cảm biến/scheduler):
```json
{
  "event": "ATTR_UPDATED",
  "device_id": "ac_01",
  "attr": "target_temp",
  "value": 24,
  "online": true
}
```

### f. Quy tắc Frontend xử lý điều khiển (chống nút bị "gạt ngược")

> **Bối cảnh**: Nút toggle có độ trễ thực thi phần cứng → nếu UI thụ động chờ dữ liệu từ GET/SSE mà thiết bị chưa kịp đổi, nút sẽ bị "nháy/gạt ngược lại".

```
[User Click] ────► 1. Đổi UI sang trạng thái mới NGAY LẬP TỨC (optimistic)
                   2. Lock control (Disabled / Loading)
                   3. Sinh 'command_id' = "cmd_fe_<random>"
                   4. Bật đếm ngược Timeout 5s (phía App)
                   5. POST /api/v1/devices/{id}/actions
                                │
   ┌────────────────────────────┴────────────────────────────┐
   │                                                         │
[202 + SSE ack khớp "cmd_fe_<random>"]             [HTTP lỗi / Timeout quá 5s]
   │                                                         │
   ▼                                                         ▼
- Unlock control                                      - Unlock control
- Giữ nguyên trạng thái ON                             - ROLLBACK gạt nút về trạng thái cũ
                                                       - Hiện thông báo lỗi (toast)
```

1. **Optimistic UI**: Khi user bấm, gạt nút ngay lập tức (không chờ server).
2. **Lock**: Vô hiệu hóa control đang Pending → chống user spam / đa luồng ghi.
3. **`command_id`**: Sinh GUID/UUID duy nhất cho mỗi lần bấm; giữ để khớp với SSE ack.
4. **Timeout 5s**: Nếu hết thời gian chưa nhận ack → rollback + báo lỗi.
5. **Ignore Stale GET/SSE**: Trong lúc control đang Pending, nếu `GET /state` hoặc SSE trả về giá trị CŨ (thiết bị chưa kịp đổi), Frontend **bắt buộc BỎ QUA** — không ghi đè lên UI cho tới khi ack hoặc timeout. Chỉ chấp nhận bản tin có `ack_command_id` khớp.

---

## 3. Nguyên tắc Card Template

> **Card = Template cố định theo loại thiết bị.**
> Khi dựng UI, App chỉ quan tâm đến:
> - **Số lượng thiết bị** (`g_device_count`) — có bao nhiêu device thì vẽ bấy nhiêu card.
> - **Loại thiết bị** (`type`: AC / Sign / Power / Light / Switch) — chọn template card tương ứng.
>
> **Số đối tượng trên card KHÔNG phụ thuộc số attr của device.** Card có layout cố định (fixed slots); một số slot lấy dữ liệu từ attr, một số slot là đối tượng tĩnh (không khớp với attr nào).

### Phân loại Slot trên Card

| Ký hiệu | Loại Slot | Ý nghĩa |
| :--- | :--- | :--- |
| **`FIXED`** | Đối tượng tĩnh | Không gắn với attr thiết bị (icon, tên, dot online, badge ON/OFF, giá trị tính toán...) |
| **`ATTR_BOUND`** | Gắn với attr | Hiển thị / điều khiển đúng 1 attr của device (đọc/ghi qua API) |
| **`EMPTY`** | Slot trống | Chỉ hiển thị khi device có attr phù hợp, ngược lại ẩn đi |

### Bảng tổng hợp Card theo loại thiết bị

| Loại Device | Overview Card | Expanded Card (Screen riêng) |
| :--- | :--- | :--- |
| **AC** | `ov_ac_card` | `ac_expanded_card` |
| **Sign** | `ov_sign_card` | `sign_expanded_card` |
| **Power Meter** | `ov_power_meter_card` | `power_meter_expanded_card` |
| **Light Sensor** | `ov_light_sensor_card` | `light_sensor_expanded_card` |
| **Switch** | `ov_switch_card` | `switch_expanded_card` |

---

## 4. Chi tiết Slot & API theo từng Card

> Ký hiệu API:
> - **Catalog** = `GET /api/v1/devices/catalog` (cấu hình: tên, loại, ds attr, control flag).
> - **State** = `GET /api/v1/devices/{device_id}/state` (giá trị realtime hiện tại).
> - **Action** = `POST /api/v1/devices/{device_id}/actions` (gửi `command_id` + action, nhận ack qua SSE).
> - **SSE** = `GET /api/v1/events` (đẩy cập nhật thời gian thực + ack_command_id).

### 4.1 AC Overview Card (`ov_ac_card`)
| # | Slot | Loại | Hiển thị (Display API) | Điều khiển (Control API) |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Icon (`LV_SYMBOL_IMAGE`) | `FIXED` | — | — |
| 2 | Tên device | `FIXED` | Catalog (`name`) | — |
| 3 | Dot online (xanh/đỏ) | `FIXED` | SSE (`online`) | — |
| 4 | Badge ON/OFF | `FIXED` | SSE (`status`/`enabled`) | — |
| 5 | Nhiệt độ phòng `Room: --°C` | `FIXED` | Attr `room_temp` qua SSE | — |
| 6 | Nhãn ON/OFF | `ATTR_BOUND` | SSE | `Action` (`TOGGLE`) |
| 7 | Switch toggle | `ATTR_BOUND` | SSE | `Action` (`TOGGLE`) |
| 8 | Stepper nhiệt độ `- / value / +` (16–32°C) | `ATTR_BOUND` | Attr `target_temp` | `Action` (`SET_ATTRIBUTE`: `{"attr":"target_temp","value":24}`) |

**Nhận xét**: Layout cố định 8 slot, KHÔNG đổi dù device AC có bao nhiêu attr. `Room temp`, dot online, badge là `FIXED` — không khớp với attr device.

### 4.2 AC Expanded Card (`ac_expanded_card`)
Card lặp qua từng attr (`display_attr_count`), mỗi attr render theo `type` + `control`:
| # | Điều kiện attr | Slot render | Loại | Control API |
| :-: | :--- | :--- | :--- | :--- |
| 1 | `control=true`, `type=bool` | Hàng toggle: label + switch | `ATTR_BOUND` | `Action` (`TOGGLE`) |
| 2 | `control=true`, `type=number`, id=`0405` | Nút quạt: Low / Med / High / Auto | `ATTR_BOUND` | `Action` (`SET_ATTRIBUTE`: `{"attr":"fan","value":0-3}`) |
| 3 | `control=true`, `type=number` | Stepper `- / value / +` | `ATTR_BOUND` | `Action` (`SET_ATTRIBUTE` number attr) |
| 4 | `control=false`, `type=number` | Label read-only | `ATTR_BOUND` | — |
| 5 | Header: icon, tên, badge, dot online, room temp | `FIXED` | Catalog + SSE | — |

**Nhận xét**: Đây là "card lặp theo attr", header vẫn `FIXED`. Số hàng có thể khác nhau giữa các device AC.

### 4.3 Sign Overview Card (`ov_sign_card`)
| # | Slot | Loại | Hiển thị | Điều khiển |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Icon + Tên | `FIXED` | Catalog | — |
| 2 | Dot online | `FIXED` | SSE | — |
| 3 | Badge ON/OFF | `FIXED` | SSE | — |
| 4 | Nhãn ON/OFF | `ATTR_BOUND` | SSE | `Action` (`TOGGLE`) |
| 5 | Switch toggle | `ATTR_BOUND` | SSE | `Action` (`TOGGLE`) |
| 6 | Tối đa **2** attr number có `overview=true` | `EMPTY` | Attr qua SSE | — |

**Nhận xét**: Card có tối đa 2 slot attr nhưng device có thể có nhiều attr khác — "số đối tượng hiển thị ≠ số attr".

### 4.4 Sign Expanded Card (`sign_expanded_card`)
| # | Slot | Loại | Hiển thị | Điều khiển |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Header (icon, tên, badge, dot) | `FIXED` | Catalog + SSE | — |
| 2 | Switch toggle | `ATTR_BOUND` | SSE | `Action` (`TOGGLE`) |
| 3 | Toàn bộ attr `type=number` (read-only) | `ATTR_BOUND` | Attr qua SSE | — |

### 4.5 Power Meter Overview `ov_power_meter_card`
| # | Slot | Loại | Hiển thị |
| :-: | :--- | :--- | :--- |
| 1 | Icon + Tên | `FIXED` | Catalog |
| 2 | Dot online | `FIXED` | SSE |
| 3 | Tối đa **3** attr number có `overview=true` | `EMPTY` | Attr qua SSE |

**Nhận xét**: Không có toggle/ON-OFF (thiết bị đo, không điều khiển).

### 4.6 Power Meter Expanded Card (`power_meter_expanded_card`)
Layout **hoàn toàn cố định**, tìm attr theo **tên chuẩn**:
| # | Slot | Loại | Tên attr nguồn | Hiển thị |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Header (icon, tên, dot) | `FIXED` | — | — |
| 2 | Total Energy | `ATTR_BOUND` | `CurrentSummationDelivered` | Attr |
| 3 | Power (tổng) | `ATTR_BOUND` | `TotalActivePower` hoặc `InstantaneousDemand` | Attr |
| 4 | Phase A: V / A / W / PF | `ATTR_BOUND` | `RMSVoltage`, `RMSCurrent`, `ActivePower`, `PowerFactor` | Attr |
| 5 | Phase B: V / A / W / PF | `ATTR_BOUND` | `RMSVoltagePhB`, `RMSCurrentPhB`, `ActivePowerPhB`, `PowerFactorPhB` | Attr |
| 6 | Phase C: V / A / W / PF | `ATTR_BOUND` | `RMSVoltagePhC`, `RMSCurrentPhC`, `ActivePowerPhC`, `PowerFactorPhC` | Attr |

**Nhận xét**: Card LUÔN vẽ 3 cột phase (A/B/C) — `FIXED` theo layout. Device không có attr phase thì cột hiển thị `---` nhưng vẫn tồn tại. Điển hình cho "số đối tượng ≠ số attr".

### 4.7 Light Sensor Overview Card (`ov_light_sensor_card`)
| # | Slot | Loại | Hiển thị |
| :-: | :--- | :--- | :--- |
| 1 | Icon + Tên | `FIXED` | Catalog |
| 2 | Dot online | `FIXED` | SSE |
| 3 | Tối đa **3** attr number `overview=true` | `EMPTY` | Attr |

### 4.8 Light Sensor Expanded Card (`light_sensor_expanded_card`)
| # | Slot | Loại | Hiển thị |
| :-: | :--- | :--- | :--- |
| 1 | Header | `FIXED` | Catalog + SSE |
| 2 | Toàn bộ attr `type=number`: `Label: value unit` | `ATTR_BOUND` | Attr |

### 4.9 Switch Overview Card (`ov_switch_card`)
| # | Slot | Loại | Hiển thị | Điều khiển |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Icon + Tên | `FIXED` | Catalog | — |
| 2 | Dot online | `FIXED` | SSE | — |
| 3 | Badge ON/OFF | `FIXED` | SSE | — |
| 4 | Nhãn ON/OFF | `ATTR_BOUND` | SSE | `Action` (`TOGGLE`) |
| 5 | Switch toggle | `ATTR_BOUND` | SSE | `Action` (`TOGGLE`) |
| 6 | Tối đa **2** attr number có `overview=true` | `EMPTY` | Attr | — |

### 4.10 Switch Expanded Card (`switch_expanded_card`)
| # | Slot | Loại | Hiển thị | Điều khiển |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Header | `FIXED` | Catalog + SSE | — |
| 2 | Nút ON/OFF (checkable) | `ATTR_BOUND` | SSE | `Action` (`TURN_ON` / `TURN_OFF`) |
| 3 | Toàn bộ attr `type=number` (read-only) | `ATTR_BOUND` | Attr | — |

### 4.11 Quick Scenes (`ov_scenes`)
| # | Slot | Loại | Điều khiển |
| :-: | :--- | :--- | :--- |
| 1 | Button **Open Store** | `FIXED` | `POST /api/v1/scenes/master/actions` `{"action":"TURN_ON"}` |
| 2 | Button **Close Store** | `FIXED` | `POST /api/v1/scenes/master/actions` `{"action":"TURN_OFF"}` |

---

## 5. Đối tượng KHÔNG khớp với Attr từ API Rush

| # | Đối tượng | Card | Nguồn dữ liệu |
| :-: | :--- | :--- | :--- |
| 1 | Dot online (xanh/đỏ) | Tất cả card | Trạng thái kết nối device (không phải attr) |
| 2 | Badge ON/OFF | AC, Sign, Switch | Trạng thái bật/tắt tổng hợp (từ bool attr điều khiển) |
| 3 | `Room: --°C` | AC Overview | Trường riêng `room_temp` (có thể là attr `0203`, có thể không) |
| 4 | Nhiệt độ hiển thị lớn + stepper | AC Overview | Trường riêng `d->value` (map từ attr `0202`, 16–32°C) |
| 5 | 3 cột Phase A/B/C (V/A/W/PF) | Power Meter Expanded | Layout cố định, không có attr → hiện `---` |
| 6 | Summary bars ( `-- active`, `Avg: --°C`, `-- lux`) | Screen AC/Sign/Switch | Giá trị tổng hợp toàn hệ thống, không phải attr device |
| 7 | Status bar: `Outdoor: --°C`, `-- kWh` | Header chung | Dữ liệu môi trường, không phải attr device |
| 8 | Button Open/Close Store | Overview | Kịch bản hệ thống, không phải attr device |

---

## 6. Cấu trúc mới: App không còn đọc `devices.yaml`

### a. Thay đổi cốt lõi
**Hiện tại**: App gọi `bms_yaml_load("devices.yaml")` để tự parse file YAML ngay trên panel (`bms_yaml.c`) → tự dựng `g_devices[]` và quyết định thông số hiển thị.

**Mới**: App **bỏ hoàn toàn** việc đọc file `devices.yaml`. Thay vào đó:
1. App gọi **`GET /api/devices/catalog`** từ Engine.
2. Engine đọc `devices.yaml` (hoặc registry nội bộ), trả về JSON đầy đủ.
3. App dựa trên JSON:
   - Xác định **số lượng thiết bị** (`g_device_count`).
   - Xác định **loại thiết bị** (AC / Sign / Power / Light / Switch) → chọn template card.
   - Xác định **thông số hiển thị** (`display_attrs`: label, unit, overview, control).
   - Xác định **thông số điều khiển được** (bool → toggle, number → stepper, fan → buttons).

### b. Sơ đồ luồng mới
```
[LVGL App]                          [Rush Engine]                    [devices.yaml]
    │                                     │                               │
    │ 1. GET /api/v1/devices/catalog    │                               │
    │────────────────────────────────────>│                               │
    │                                     │ 2. đọc + parse devices.yaml    │
    │                                     │──────────────────────────────────>
    │                                     │ 3. trả về JSON catalog         │
    │<────────────────────────────────────│                               │
    │ 4. dựng g_devices[] theo JSON       │                               │
    │    (số device + thông số hiển thị)  │                               │
    │                                     │                               │
    │ 5. POST /api/v1/devices/<id>/actions│                               │
    │    {"command_id","action"}          │                               │
    │────────────────────────────────────>│ 6. tra mapping → gửi MQTT      │
    │ 7. SSE ack: DEVICE_STATE_CHANGED    │                               │
    │<────────────────────────────────────│                               │
```

### c. API Device Catalog (mới)
- **Endpoint**: `GET /api/v1/devices/catalog`
- **Mô tả**: Trả về danh sách thiết bị kèm cấu hình hiển thị (không gồm giá trị realtime).
- **Response**:
```json
{
  "devices": [
    {
      "id": "ac_01",
      "name": "AC Khu A",
      "type": "AC",
      "group": "Khu A",
      "display_attrs": [
        { "attr_id": "0101", "label": "Power", "type": "bool", "unit": "",
          "overview": true, "control": true },
        { "attr_id": "0202", "label": "Temperature", "type": "number", "unit": "°C",
          "overview": true, "control": true }
      ]
    },
    {
      "id": "sign_01",
      "name": "Bien QC",
      "type": "Sign",
      "group": "Ngoai",
      "display_attrs": [
        { "attr_id": "0110", "label": "Control", "type": "bool", "unit": "",
          "overview": true, "control": true },
        { "attr_id": "0201", "label": "Energy", "type": "number", "unit": "kWh",
          "overview": true, "control": false }
      ]
    }
  ]
}
```

### d. API State (kèm giá trị hiện tại)
- **Endpoint**: `GET /api/v1/devices`
- **Mô tả**: Trả về trạng thái realtime của mọi thiết bị (dùng cập nhật giá trị hiển thị ban đầu).
- **Response**:
```json
{
  "devices": [
    { "id": "ac_01", "status": "ON", "attrs": { "temp": 24, "fan": 2 } },
    { "id": "sign_01", "status": "ON", "attrs": { "lux": 450, "energy": 12.5 } }
  ]
}
```

### e. Thay đổi phía App (LVGL)
1. **Xoá luồng YAML tại App**: Không còn gọi `bms_yaml_load()` trong `bms_init()`. Giữ `bms_yaml.c` chỉ làm fallback khi Engine không reachable.
2. **Thêm hàm fetch catalog** trong `http_client.c`:
   - `http_fetch_catalog()`: gọi `GET /api/v1/devices/catalog`, parse JSON → `g_devices[]`.
   - `http_fetch_states()`: gọi `GET /api/v1/devices`, cập nhật giá trị realtime.
3. **Logic build UI giữ nguyên**: `ov_ac_card`, `ac_expanded_card`, ... vẫn dựa trên `g_devices[]`/`display_attrs` — chỉ đổi nguồn dữ liệu (JSON thay vì YAML).
4. **SSE mapping theo ID**: Parser SSE đổi từ `topic` sang `device_id` + `attr` + `ack_command_id`, cập nhật đúng card theo `g_devices[]`.
5. **Command Queue điều khiển**: mỗi control bấm tạo `command_id`, đưa vào `pending` list; khớp với ack SSE để hoàn tất/rollback. (Xem mục 2.f.)

### f. Thứ tự khởi động mới của App
```
main()
 ├── config_load()
 ├── ota_init()
 ├── lv_init() → display + evdev
 ├── ui_init()                       // dựng khung UI rỗng
 ├── http_client_init()              // (1) GET /api/v1/devices/catalog → g_devices[]
 │                                   // (2) GET /api/v1/devices → giá trị ban đầu
 │                                   // (3) mở SSE thread → cập nhật realtime
 ├── bms_init()                      // dựng card theo g_devices[] đã có
 └── main loop → http_client_poll()  // drain queue SSE → cập nhật UI
}
```

### g. Rủi ro & Phương án dự phòng
| Rủi ro | Phương án |
| :--- | :--- |
| Engine chưa sẵn sàng khi App khởi động | App retry `GET /api/v1/devices/catalog` vài lần, hiển thị "Đang kết nối..." |
| Engine offline vĩnh viễn | Giữ `bms_yaml_load()` làm fallback (đọc file cục bộ) kèm cảnh báo |
| Format JSON thay đổi giữa các version | Version field trong response catalog; App từ chối nếu không khớp |
| Thiết bị thực thi chậm > timeout 5s | Tăng timeout theo loại device trong catalog (`command_timeout_ms`); UI hiện "Đang xử lý..." |
| Ack mất trên SSE (kết nối đứt giữa chừng) | App tự reconcile bằng `GET /state` sau khi hết timeout thay vì rollback mù |

---

## 7. So sánh Trước và Sau
| Đặc điểm | Hiện tại (MQTT-based) | Thiết kế mới (REST-based) |
| :--- | :--- | :--- |
| **Kiến thức App** | Phải biết topic `bms/ac/0/0101/set` | Chỉ cần biết `device_id: "ac_01"` và `attr: "temp"` |
| **Nguồn dữ liệu device** | Đọc `devices.yaml` trực tiếp | Lấy qua `GET /api/v1/devices/catalog` từ Engine |
| **Thay đổi phần cứng** | Build lại App (nếu topic đổi) | Chỉ sửa mapping trong Engine |
| **Kiểm tra lỗi** | Khó (payload string tùy ý) | Dễ (JSON schema, HTTP status code) |
| **Số đối tượng card** | Tự động theo số attr | Cố định theo template loại device (`FIXED`/`ATTR_BOUND`/`EMPTY`) |
| **Khả năng mở rộng** | Thấp (phụ thuộc MQTT) | Cao (thêm API không phá vỡ cũ) |

---

## 7. Quy tắc khớp slot trên App
1. App dựng card theo **`type`** → chọn template.
2. Mỗi slot `ATTR_BOUND` lưu `device_id` + `attr_name`; cập nhật khi nhận SSE có `device_id` khớp.
3. Slot `EMPTY` hiển thị nếu device có attr phù hợp, ngược lại ẩn.
4. Slot `FIXED` cập nhật từ trường đặc biệt của SSE (`online`, `enabled`, `room_temp`, summary...).
5. **App KHÔNG cần biết attr_id (`0101`, `0202`...) hay index — Engine chịu trách nhiệm mapping sang MQTT.**
6. **Mọi thao tác điều khiển** (toggle, stepper, fan, scene) đều gửi qua `POST /api/v1/devices/{id}/actions` với `command_id` + `action` (xem mục 2.c — Action Vocabulary).
7. **Trạng thái UI ghi từ SSE theo ack**: bản tin `DEVICE_STATE_CHANGED` có `ack_command_id` khớp mới được coi là xác nhận; bản tin `ATTR_UPDATED`/GET stale bị bỏ qua trong lúc control đang Pending (xem 2.f).