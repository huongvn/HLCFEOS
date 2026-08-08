Chuẩn rồi — mình tìm được đúng bảng ánh xạ dp (data point) cho dòng TOQCB2-80 từ file định nghĩa thiết bị Tongou (zigbee-herdsman-converters), khớp gần như hoàn hảo với các attribute EF00/xxxx trong log của bạn.

## Bảng dp gốc (theo converter Tongou TOQCB2-80)

```
1   = Total forward energy
6   = Phase A (raw: điện áp + dòng điện + công suất pha A)
16  = Switch (đóng/cắt CB)
102 = Over-voltage setting
103 = Under-voltage setting
104 = Over-current setting
105 = Over-power setting
107 = Temperature setting
109 = Online state
110 = Event (last_event)
112 = Auto-reclosing switch
113 = Restore default switch
114 = Over-current threshold
115 = Over-voltage threshold
116 = Under-voltage threshold
118 = Temperature threshold
119 = Over-power threshold
125 = Forwarded electricity
131 = Real-time Temp (device_temperature)
```

Nhớ lại quy tắc Tasmota cho cluster Tuya: dp Tuya có dạng `EF00/AABB` — trong đó **AA = type**, **BB = dpid (hex)**. Đối chiếu decimal → hex rồi khớp với log:

## Kết quả khớp với log của bạn

| Log attr | dpid (dec) | Ý nghĩa | Giá trị mẫu | Diễn giải |
|---|---|---|---|---|
| EF00/**02**01 | dp1 | Total forward energy (÷100) | 1 | 0.01 kWh |
| EF00/**00**06 | dp6 | **Phase A (raw: V+I+P)** | `0924000000000000` | **👉 chứa điện áp tức thời** |
| EF00/**01**10 | dp16 | Switch | 1 | CB đang ON |
| EF00/**04**66 | dp102 | Over-voltage setting | 2 | Trip |
| EF00/**04**67 | dp103 | Under-voltage setting | 0 | Closed |
| EF00/**04**68 | dp104 | Over-current setting | 2 | Trip |
| EF00/**04**69 | dp105 | Over-power setting | 0 | Closed |
| EF00/**04**6B | dp107 | Temperature setting | 2 | Trip |
| EF00/**04**6E | dp110 | Event (last_event) | 13 | **manual_on** (đóng CB bằng tay) |
| EF00/**01**70 | dp112 | Auto-reclosing | 0 | OFF |
| EF00/**02**72 | dp114 | Over-current threshold | 63 | 63A |
| EF00/**02**73 | dp115 | Over-voltage threshold | 280 | 280V |
| EF00/**02**74 | dp116 | Under-voltage threshold | 165 | 165V |
| EF00/**02**76 | dp118 | Temperature threshold (÷10) | 800 | 80.0°C |
| EF00/**02**77 | dp119 | Over-power threshold | 2000 | 2000W |
| EF00/**02**83 | dp131 | Real-time Temp (÷10) | 307→337 | 30.7°C → 33.7°C |
| EF00/**02**7D | dp125 | Forwarded electricity | 1 | 1 (raw) |

## 🎯 Datapoint chứa điện áp tức thời: **dp6 (EF00/0006)**

Đây là datapoint **composite** (kiểu raw, không phải số đơn), gộp chung Voltage + Current + Power của pha A trong một chuỗi byte, theo cấu trúc phổ biến của Tuya "phaseVariant2":

```
byte[0:2] = Voltage  (đơn vị 0.1V, big-endian)
byte[2:5] = Current  (đơn vị mA)
byte[5:8] = Power    (đơn vị W)
```

Áp dụng vào dữ liệu thực tế trong log của bạn:

| Raw hex | 2 byte đầu (V) | 3 byte giữa (I) | 3 byte cuối (P) | → Điện áp |
|---|---|---|---|---|
| `0924000000000000` | `09 24` = 2340 | `00 00 00` | `00 00 00` | **234.0 V** |
| `092C000000000000` | `09 2C` = 2348 | 0 | 0 | **234.8 V** |
| `0928000000000000` | `09 28` = 2344 | 0 | 0 | **234.4 V** |
| `092E000000000000` | `09 2E` = 2350 | 0 | 0 | **235.0 V** |

→ Các giá trị 234–235V hoàn toàn khớp với điện áp lưới thực tế (220-240V), và dòng/công suất = 0 vì lúc đó CB không có tải đấu vào (đúng với thực tế bạn đang test thiết bị chưa cấp tải).

## Điểm còn nghi vấn: `EF00?11`
Dòng này (`00E940`→`011F40`, tăng dần ~4608 mỗi ~10 phút) **không nằm trong danh sách dp đã biết** (dp cao nhất được định nghĩa là 131=0x83). Dấu `?` thay vì `/` cũng không phải cú pháp chuẩn Tasmota — nhiều khả năng đây là phản hồi nội bộ của cluster Tuya (vd: đồng bộ thời gian/heartbeat MCU) chứ không phải dp đo lường, nên chưa nên diễn giải là điện áp/công suất.

Bạn có muốn mình viết luôn 1 file plugin `.zb` để Tasmota tự hiện tên các dp này (Voltage_a, Power_a, Current_a, Switch, DeviceTemp...) thay vì hiển thị `EF00/xxxx` không?


Trên console tasmota 
Tìm được rồi — thì ra Tasmota có cơ chế ghi thuộc tính Tuya **built-in** đơn giản hơn nhiều, không cần tự soạn payload raw qua `Send`/`Cmd` (cách đó không phải cú pháp Tasmota hỗ trợ, nên liên tục báo lỗi).

Theo tài liệu Zigbee chính thức của Tasmota: với attribute Tuya dạng `EF00/AABB` (AA = type, BB = dpid), bạn có thể **ghi trực tiếp** bằng lệnh `Write`, miễn là type đã được xác định — và log của bạn đã cho thấy chính xác `EF00/0110` (type = 01 tức bool, dpid = 0x10 = 16 = Switch).

Vậy lệnh đúng và đơn giản nhất là:

**Bật CB:**
```
ZbSend {"Device":"0xD46F","Write":{"EF00/0110":1}}
```

**Tắt CB:**
```
ZbSend {"Device":"0xD46F","Write":{"EF00/0110":0}}
```

Đây chính là cách Tasmota official document mô tả: với attribute từ cluster Tuya 0xEF00, attribute có dạng AABB trong đó AA là type và BB là dpid; để ghi được attribute thì type phải được chỉ định cụ thể.

Bạn thử lệnh này và cho tôi xem log `ZbReceived`/`RESULT` tiếp theo nhé — nếu đúng, bạn sẽ nghe CB đóng/cắt thật và `EF00/0110` sẽ đổi giá trị tương ứng.