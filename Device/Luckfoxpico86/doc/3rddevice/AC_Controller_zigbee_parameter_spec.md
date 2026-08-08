# Bảng thông số Zigbee — Điều hòa (AC) qua bộ điều khiển AC Zigbee (dây P1/P2)
### Thiết bị: `0x336A` (Zone A) và `0xC5A9` (Zone B) — Bộ điều khiển AC Zigbee đấu dây **P1/P2** vào dàn lạnh (indoor unit) của điều hòa, giao tiếp Zigbee qua cluster Tuya `0xEF00`

## 1. Thông tin chung

| Mục | Giá trị |
|---|---|
| Loại thiết bị | **Tuya EF00 AC Controller** — bộ điều khiển điều hòa qua Zigbee, **đấu dây trực tiếp vào cổng P1/P2 của dàn lạnh (indoor unit)** (giao tiếp serial P1/P2, KHÔNG phải IR blaster) |
| Cluster chính | `0xEF00` — Tuya proprietary DP (KHÔNG phải ZCL chuẩn như đồng hồ điện SPM02) |
| Endpoint | 1 |
| Zigbee short address | `0x336A` (AC Zone A / Khu A), `0xC5A9` (AC Zone B / Khu B) |
| Gateway (Coordinator) | `tasmota_6DD514` |
| Nr_type (devices.yaml) | `ac_controller` → hiển thị loại **AC** trên UI |
| Khai báo trong dự án | `Device/Luckfoxpico86/code/lvgl_project/devices.yaml` |
| Offline timeout | 10 phút (cấu hình `offline_timeout` riêng cho type AC) |
| Cảm biến nhiệt độ | Nhiệt độ phòng (Room Temp) lấy từ **cảm biến của dàn lạnh AC** truyền về qua đường P1/P2 — là giá trị thật của AC, không phải cảm biến cắm ngoài |

> **Lưu ý quan trọng cho code agent**: THIẾT BỊ NÀY KHÁC SPM02. Log Tasmota **giữ nguyên** key Tuya raw dạng `EF00/AABB` trong `ZbReceived`, KHÔNG được Tasmota giải mã sẵn thành tên ZCL chuẩn. Muốn read/write 1 thuộc tính thì dùng đúng key `EF00/AABB` (AABB = `type + dpid`), và `attr_id` trong devices.yaml là phần hậu tố đó (VD attr `0101` ⇔ `EF00/0101`).

---

## 2. Bảng tham số — các DP (data point) điều khiển & báo về

| Key trên log / ZbSend | attr_id (devices.yaml) | label | Recipe (type AA – dpid BB) | Kiểu dữ liệu | Raw mẫu / dải | Hệ số | Đơn vị | Ý nghĩa |
|---|---|---|---|---|---|---|---|---|
| `EF00/0101` | `0101` | Power | type `01` (bool), dpid `01` | bool | `0` / `1` | ÷1 | — | Bật/tắt điều hòa (1 = ON, 0 = OFF) |
| `EF00/0202` | `0202` | Temperature | type `02` (value), dpid `02` | uint32 | `16`–`32` | 1 | °C | Nhiệt độ cài đặt (setpoint) mà bộ điều khiển gửi tới dàn lạnh AC |
| `EF00/0203` | `0203` | Room Temp | type `02` (value), dpid `03` | uint32 | giá trị nhiệt độ phòng | 1 | °C | Nhiệt độ phòng đọc từ cảm biến dàn lạnh (feedback, không điều khiển) |
| `EF00/0405` | `0405` | Fan Speed | type `04` (enum), dpid `05` | enum/uint | `0`–`3` | 1 | — | Tốc độ quạt |

> **Giải nghĩa key `EF00/AABB`**: theo quy ước Tuya trong Tasmota, `AA` = loại dữ liệu (TP type code), `BB` = dpid số hex. Admin của dự án đã **gộp sẵn cả 2 vào attr_id** dạng 4 ký tự (`0101`, `0202`, `0203`, `0405`) nên trong log/`ZbSend` ta thấy đúng chuỗi `EF00/0101` … mà không cần tách nữa.

---

## 3. Chi tiết từng DP và trạng thái Fan

### Power (`EF00/0101`)
| Raw | Ý nghĩa |
|---|---|
| `1` | Bật AC (gửi lệnh ON tới dàn lạnh qua P1/P2) |
| `0` | Tắt AC (gửi lệnh OFF tới dàn lạnh qua P1/P2) |

### Temperature setpoint (`EF00/0202`)
- Phạm vi UI cho phép **16–32°C** (App LVGL dùng stepper +/−), giá trị nguyên.
- Trong dự án **không khai scale** cho attr này → hệ thống coi raw = value (1:1).
- Khi write giá trị nào, bộ điều khiển gửi lệnh đặt nhiệt độ tới dàn lạnh qua giao thức P1/P2 (không cần kèm theo Power/mode).

### Room Temp (`EF00/0203`)
- Là dữ liệu **chỉ-đọc từ thiết bị** (engine chia `control: false`), dùng hiển thị `Room: xx°C` trên card AC.
- Giá trị do **cảm biến của dàn lạnh AC** đo và truyền về qua đường P1/P2 → phản ánh nhiệt độ phòng thực tế tại dàn lạnh.
- ⚠️ Chưa từng ghi nhận log thực tế rõ ràng: nếu deploy với bo mạch khác thấy raw lệch bậc 10 (vd `253` thay vì `26`), khai thêm `scale: 0.1` trong devices.yaml.

### Fan Speed (`EF00/0405`)

| Raw | Fan |
|---|---|
| `0` | Low (Thấp) |
| `1` | Med (Trung bình) |
| `2` | High (Cao) |
| `3` | Auto (Tự động) |

---

## 4. Lệnh điều khiển qua Tasmota (`ZbSend`)

**Topic:** `cmnd/{gateway}/ZbSend` — với gateway `tasmota_6DD514` thì là `cmnd/tasmota_6DD514/ZbSend`

**Format chuẩn (ÁP BẮT BUỘC là `Endpoint`):**
```json
{"Device": "0x336A", "Endpoint": 1, "Write": {"EF00/0101": 1}}
```

### Ví dụ thực tế

```json
// Bật AC Zone B (0xC5A9)
cmnd/tasmota_6DD514/ZbSend {"Device":"0xC5A9","Endpoint":1,"Write":{"EF00/0101":1}}

// Tắt AC Zone B
cmnd/tasmota_6DD514/ZbSend {"Device":"0xC5A9","Endpoint":1,"Write":{"EF00/0101":0}}

// Đặt nhiệt độ 24°C — AC Zone A (0x336A)
cmnd/tasmota_6DD514/ZbSend {"Device":"0x336A","Endpoint":1,"Write":{"EF00/0202":24}}

// Đặt fan High (2) — AC Zone A
cmnd/tasmota_6DD514/ZbSend {"Device":"0x336A","Endpoint":1,"Write":{"EF00/0405":2}}
```

### Quy tắc bắt buộc (đã xác nhận từ bug lịch sử)

- **Phải là `"Endpoint": 1`.** Thiếu nó, Tasmota trả `{"ZbSend":"Missing endpoint"}` và **KHÔNG phát sóng** ra thiết bị (đèn LED gateway không nháy) — bug từng xảy ra ở MCB.
- **`Device` viết hoa chữ D** (không phải `device`). Key trong `Write` phải có tiền tố `EF00/`.
- Mỗi lệnh `ZbSend` **chỉ ghi đúng 1 thuộc tính** — Tasmota không hỗ trợ ghi multi-attr trong 1 `Write` cho dòng thiết bị Tuya EF00 này. Muốn đổi cả power + nhiệt phải gửi 2 lệnh riêng.
- Bộ điều khiển đấu **dây P1/P2 vào dàn lạnh (indoor unit)** → về **phần cứng** có thể đọc trạng thái thật của AC (power, setpoint, room temp, fan) và gửi lệnh điều khiển qua cùng đường dây (KHÔNG phải IR một chiều). Tuy nhiên trong **code hiện tại engine KHÔNG polling trạng thái AC** và **chưa có log/test nào xác nhận** bộ điều khiển này có tự thông báo (push report) khi trạng thái đổi từ bên ngoài hệ thống (VD: bấm remote tay). Engine chỉ phản ánh lên UI **những bản tin report mà thiết bị chủ động gửi tới**: thiết bị có push thì UI tự cập nhật, thiết bị im lặng thì hệ thống không biết. Khuyến nghị ghi log thử nghiệm trước khi khẳng định tính năng này.

### Legacy `AC_SET` (ĐÃ BỎ — chỉ ghi chú)

Giai đoạn đầu HMI Bridge từng dùng lệnh Tasmota `AC_SET` cho `temp`/`mode` (topic `bms/ac/N/{temperature|mode}/set`). Về sau bỏ hẳn `mode`, mọi nhu cầu chuyển thành ghi thẳng `EF00/{attr_id}` như mục 4 — không bảo trì theo đường cũ.

---

## 5. Phản hồi / đọc telemetry

- Tasmota báo dữ liệu về topic `tele/{gateway}/SENSOR`, gói trong `ZbReceived`:
```json
{"ZbReceived":{"0x336A":{"Device":"0x336A","EF00/0101":1,"EF00/0202":24,"EF00/0203":26,"EF00/0405":2,"Endpoint":1,"LinkQuality":..."}}}
```
- Engine (Rush_engine `main.rs`) normalize bằng cách đọc `EF00/{attr_id}` tương ứng:
  - attr bool (`0101`): raw `1`/`0` → `"ON"`/`"OFF"` (đúng giá trị hiển thị).
  - attr số (`0202`, `0203`, `0405`): nhân `scale` (mặc định 1, tương đương không chia).
- Xem log trên thiết bị thật: topic `tele/tasmota_6DD514/SENSOR` phải có các key `EF00/…` khi thiết bị hoạt động. ⚠️ Chưa có log thực tế trong repo nên **chưa xác nhận** tần suất report, số key mỗi lần, hay có bị chia đôi payload không — nếu gặp hiện tượng thừa/thiếu attr (như SPM02 bị split 2 message) thì merge theo `Device` + cửa sổ ≤2s.

---

## 6. Tổng hợp nhanh cho code agent (parse 1 lần)

```
QUY_DOI_AC = {
  # key log -> (attr_id, label, kind, unit)
  "EF00/0101": ("0101", "Power",        "bool",   "ON/OFF"),
  "EF00/0202": ("0202", "Temperature",  "num",    "C"),     # 16-32, setpoint
  "EF00/0203": ("0203", "Room Temp",    "num",    "C"),     # read-only, từ cảm biến dàn lạnh (P1/P2)
  "EF00/0405": ("0405", "Fan Speed",    "num",    "0=Low 1=Med 2=High 3=Auto"),
}
```

- **Write**: `cmnd/{gw}/ZbSend {"Device":"0x..","Endpoint":1,"Write":{"EF00/{attr_id}": value}}`
- **Read/report**: key còn nguyên `EF00/AABB` trong `ZbReceived`.
- **xsolar remote set**: publish lên topic `smarteos/{site}/{addr}/set` với key theo **label** trong devices.yaml (`Power`, `Temperature`, `Fan Speed`) — engine resolve label → `EF00/{id}` rồi `ZbSend`.

---

## 7. Lưu ý vận hành trong dự án này

| Tình huống | Cách xử lý |
|---|---|
| Kịch bản Open/Close Store | Gửi `EF00/0101=1/0` cho mọi `ac_controller` qua `bms/scene/master`, **stagger 200ms** để tránh burst Zigbee |
| Hiển thị room temp | dùng `0203` — card Overview luôn hiển thị `Room: xx°C` dù attr đó không `control` |
| Thiết bị offline | HMI **giữ nguyên lựa chọn người dùng** + banner *"chua duoc xac nhan"*; không rollback. AC có dải offline: 10 phút |
| Re-pair / đổi thiết bị | Short address (`0x336A`, `0xC5A9`) có thể đổi — phải cập nhật lại `zigbee_addr` trong devices.yaml (engine hot-reload sau 5s) |

---

## Tham khảo chéo
- `SPM02-D2TZ_zigbee_parameter_spec.md` — đồng hồ điện 3 pha (ZCL chuẩn, trái ngược với thiết bị Tuya EF00 này).
- `Light Sensor.md` — cảm biến ánh sáng (ZCL 0x0400).
- `MCB TONGOU TOQCB2-80 DECODE.md` — thiết bị Tuya EF00 khác, cách đọc dp `EF00/AABB` (AA = type, BB = dpid).