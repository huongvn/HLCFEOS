# Bảng thông số Zigbee — Zemismart SPM02-D2TZ-U01-ZM
### Thiết bị: `0x4BAD` và `0x51DF` (Đồng hồ đo điện 3 pha, Zigbee 3.0 native)

## 1. Thông tin chung

| Mục | Giá trị |
|---|---|
| Model | SPM02-D2TZ-U01-ZM |
| Vendor | Zemismart |
| Loại thiết bị | Zigbee 3.0 3-phase Smart Energy Meter (clamp CT, không dùng Tuya cluster EF00) |
| Cluster chính | `0x0B04` — Electrical Measurement (ZCL chuẩn) |
| Endpoint | 1 |
| Định dạng log nguồn | Tasmota `RSL: SENSOR = {"ZbReceived":{...}}` |
| Dải đo | 110–240V (pha-trung tính), 208–415V (pha-pha), 50/60Hz, tối đa 63A/pha |
| Sai số công bố | ±1% |
| Chu kỳ báo cáo quan sát trong log | ~20–30 giây/lần (cấu hình 5s theo hãng, có thể do gateway/network) |

> **Lưu ý quan trọng cho code agent**: log KHÔNG dùng dạng `EF00/AABB` (Tuya raw) như các thiết bị TS0601 khác — mọi trường đã được Tasmota giải mã sẵn thành tên thuộc tính ZCL chuẩn. Do đó **không cần bảng dp Tuya** để parse thiết bị này, chỉ cần áp hệ số quy đổi bên dưới.

---

## 2. Bảng tham số — Nhóm "Pha A" (không hậu tố)

| Parameter (JSON key) | Attribute ID (cluster 0x0B04) | Kiểu dữ liệu | Raw mẫu (log) | Hệ số | Công thức | Đơn vị | Ý nghĩa |
|---|---|---|---|---|---|---|---|
| `ACFrequency` | 0x0300 | uint16 | 5016 | ÷100 | `raw/100` | Hz | Tần số lưới điện (đo tại pha A / chung hệ thống) |
| `RMSVoltage` | 0x0505 | uint16 | 22665 | ÷100 | `raw/100` | V | Điện áp hiệu dụng (RMS) — Pha A |
| `RMSCurrent` | 0x0508 | uint16 | 204 | ÷100 | `raw/100` | A | Dòng điện hiệu dụng (RMS) — Pha A |
| `ActivePower` | 0x050B | int16 | 335 | ÷1 | `raw` | W | Công suất tác dụng (thực) — Pha A |
| `ReactivePower` | 0x050E | int16 | -110 | ÷1 | `raw` | VAR | Công suất phản kháng — Pha A (dấu âm = tải có tính dung/quy ước chiều đo của thiết bị) |
| `ApparentPower` | 0x050F | int16 | 462 | ÷1 | `raw` | VA | Công suất biểu kiến — Pha A (S = √(P²+Q²), có sai số làm tròn nhỏ so với log) |
| `PowerFactor` | 0x0510 | int8 | 72 | ÷100 | `raw/100` | (không đơn vị, 0–1) | Hệ số công suất = P/S — Pha A |

---

## 3. Bảng tham số — Nhóm "Pha B" (hậu tố `PhB`)

| Parameter (JSON key) | Attribute ID | Raw mẫu | Hệ số | Đơn vị | Ý nghĩa |
|---|---|---|---|---|---|
| `RMSVoltagePhB` | 0x0905 | 22361 | ÷100 | V | Điện áp RMS — Pha B |
| `RMSCurrentPhB` | 0x0908 | 212 | ÷100 | A | Dòng điện RMS — Pha B |
| `ActivePowerPhB` | 0x090B | 367 | ÷1 | W | Công suất tác dụng — Pha B |
| `ReactivePowerPhB` | 0x090E | -108 | ÷1 | VAR | Công suất phản kháng — Pha B |
| `ApparentPowerPhB` | 0x090F | 474 | ÷1 | VA | Công suất biểu kiến — Pha B |
| `PowerFactorPhB` | 0x0910 | 77 | ÷100 | — | Hệ số công suất — Pha B |

---

## 4. Bảng tham số — Nhóm "Pha C" (hậu tố `PhC`)

| Parameter (JSON key) | Attribute ID | Raw mẫu | Hệ số | Đơn vị | Ý nghĩa |
|---|---|---|---|---|---|
| `RMSVoltagePhC` | 0x0A05 | 22067 | ÷100 | V | Điện áp RMS — Pha C |
| `RMSCurrentPhC` | 0x0A08 | 190 | ÷100 | A | Dòng điện RMS — Pha C |
| `ActivePowerPhC` | 0x0A0B | 302 | ÷1 | W | Công suất tác dụng — Pha C |
| `ReactivePowerPhC` | 0x0A0E | -92 | ÷1 | VAR | Công suất phản kháng — Pha C |
| `ApparentPowerPhC` | 0x0A0F | 421 | ÷1 | VA | Công suất biểu kiến — Pha C |
| `PowerFactorPhC` | 0x0A10 | 71 | ÷100 | — | Hệ số công suất — Pha C |

---

## 5. Bảng tham số — Nhóm "Tổng 3 pha" (synthetic, không phải attribute ZCL gốc)

| Parameter (JSON key) | Nguồn gốc | Raw mẫu | Hệ số | Đơn vị | Công thức xác minh | Ý nghĩa |
|---|---|---|---|---|---|---|
| `TotalActivePower` | Tasmota tự tính (computeSyntheticAttributes) | 1005 | ÷1 | W | `ActivePower + ActivePowerPhB + ActivePowerPhC` (335+367+302=1004≈1005 ✓) | Tổng công suất tác dụng 3 pha |
| `TotalReactivePower` | Tasmota tự tính | -311 | ÷1 | VAR | Tổng `ReactivePower` 3 pha (-110-108-92=-310≈-311 ✓) | Tổng công suất phản kháng 3 pha |
| `TotalApparentPower` | Tasmota tự tính | 1361 | ÷1 | VA | Tổng `ApparentPower` 3 pha (462+474+421=1357≈1361, lệch nhẹ do làm tròn) | Tổng công suất biểu kiến 3 pha |

> ⚠️ 3 trường `Total*` **không tồn tại trong đặc tả chuẩn ZCL cluster 0x0B04** — đây là attribute tổng hợp do chính Tasmota tính toán khi phát hiện thiết bị đo đa pha (theo cơ chế `computeSyntheticAttributes()` trong Zigbee driver của Tasmota), không phải giá trị đọc trực tiếp từ thiết bị.

---

## 6. Trường phụ / đặc biệt

| Parameter | Raw mẫu | Ý nghĩa | Ghi chú cho code agent |
|---|---|---|---|
| `Power` | 0 | Công suất tức thời (đơn lẻ, không kèm bộ đầy đủ RMS/PF) | Xuất hiện riêng lẻ trong log (VD: `00:23:17`), đến từ cluster **Metering (0x0702)** attribute `InstantaneousDemand` (0x0400), khác nguồn với `ActivePower` (cluster 0x0B04). Nên xử lý như key riêng, KHÔNG gộp chung logic với `ActivePower`. Cùng cluster với điện năng kWh ở mục 7. |
| `Device` | `"0x4BAD"` | Short address Zigbee của thiết bị | Định danh duy nhất trong mạng, có thể đổi nếu re-pair |
| `Endpoint` | 1 | Zigbee endpoint | Cố định = 1 cho model này |
| `LinkQuality` | 0–255 (log thấy 16–160+) | Chất lượng liên kết (LQI) | Không phải RSSI (dBm); giá trị càng cao càng tốt |

---

## 7. Điện năng tiêu thụ (kWh) — Cluster Metering (0x0702)

Ngoài cluster đo công suất tức thời `0x0B04` (Electrical Measurement), thiết bị còn hỗ trợ cluster **Metering (0x0702)** theo **Zigbee Cluster Library (ZCL) Specification** (CSA/Connectivity Standards Alliance) để báo **điện năng tích lũy (cumulative, không reset)**. Đây là nguồn dữ liệu cho "số điện đã dùng" của power meter.

| Parameter (JSON key) | Attribute ID (cluster 0x0702) | Kiểu dữ liệu (ZCL) | Hệ số | Đơn vị | Ý nghĩa |
|---|---|---|---|---|---|
| `CurrentSummationDelivered` | 0x0000 | uint48 | ×0.01 (theo `SummationFormatting`) | kWh | Tổng điện năng đã tiêu thụ (delivered) từ trước đến nay — giá trị tích lũy, KHÔNG reset |
| `CurrentSummationReceived` | 0x0001 | uint48 | ×0.01 | kWh | Tổng điện năng nhận vào (received) — dùng khi có bán điện ngược / pin |
| `InstantaneousDemand` | 0x0400 | int24 | ÷1 | W | Công suất tức thời — chính là field `Power` ở mục 6 |

> **Quy đổi kWh:** giá trị raw là **uint48**, đơn vị thực tế quyết định bởi attribute `UnitOfMeasure` (0x0300) và số chữ số thập phân `SummationFormatting` (0x0301). Với thiết bị này log cho thấy 2 chữ số thập phân → hệ số **×0.01**: raw `12345` = `123.45 kWh`. Đúng đặc tả ZCL (chương 10.4 Metering), tương đương các triển khai NXP/Espressif/SmartThings.

**Lưu ý cho code agent:**
- `CurrentSummationDelivered` thường gửi chung trong log với `Power`/`InstantaneousDemand` (cùng cluster 0x0702), nhưng cũng có thể xuất hiện trong bản tin `Electrical Measurement` — gộp theo rule mục 8 (split payload) như nhau.
- Đây là bộ đếm tích lũy → lưu dạng counter/absolute, **không trừ chênh lệch** khi hiển thị "số điện đã dùng"; tính tiêu thụ trong kỳ (VD tháng) thì lấy `value(t2) − value(t1)`.
- Trong `devices.yaml` của dự án, field này khai báo với `id: "CurrentSummationDelivered"`, `label: "Total Energy"`, `unit: "kWh"`, `scale: 0.01`, `xsolar_key: "total_energy"`.

---

## 8. Bảng tổng hợp nhanh — hệ số quy đổi (dùng để code parser 1 lần)

```
SCALE_MAP = {
  "ACFrequency":        0.01,   # -> Hz
  "RMSVoltage":          0.01,   # -> V     (áp dụng chung cho PhB/PhC)
  "RMSVoltagePhB":       0.01,
  "RMSVoltagePhC":       0.01,
  "RMSCurrent":          0.01,   # -> A     (áp dụng chung cho PhB/PhC)
  "RMSCurrentPhB":       0.01,
  "RMSCurrentPhC":       0.01,
  "PowerFactor":         0.01,   # -> hệ số 0..1 (áp dụng chung PhB/PhC)
  "PowerFactorPhB":      0.01,
  "PowerFactorPhC":      0.01,
  # Các trường công suất (W/VAR/VA) giữ nguyên raw, KHÔNG chia
  "ActivePower": 1, "ActivePowerPhB": 1, "ActivePowerPhC": 1, "TotalActivePower": 1,
  "ReactivePower": 1, "ReactivePowerPhB": 1, "ReactivePowerPhC": 1, "TotalReactivePower": 1,
  "ApparentPower": 1, "ApparentPowerPhB": 1, "ApparentPowerPhC": 1, "TotalApparentPower": 1,
  "Power": 1,
  # Điện năng tích lũy (kWh) — cluster Metering 0x0702
  "CurrentSummationDelivered": 0.01,
  "CurrentSummationReceived": 0.01,
}
```

---

## 9. ⚠️ Bản tin bị chia làm 2 (split payload) — quan trọng cho code agent

Trong log dài hơn, cả `0x4BAD` và `0x51DF` đôi lúc gửi bộ 22 thuộc tính **không nằm trọn trong 1 message**, mà bị tách thành 2 bản tin liên tiếp (cách nhau ~0.5–1s), do giới hạn kích thước payload của khung Zigbee ZCL Report Attributes.

**Ví dụ thực tế (0x51DF):**
```json
// Message 1 — 00:33:03.580
{"Device":"0x51DF","ACFrequency":5013,...,"RMSCurrentPhB":217,"ActivePowerPhB":385,"ReactivePowerPhB":-107,"Endpoint":1}

// Message 2 — 00:33:04.146 (0.57s sau)
{"Device":"0x51DF","ApparentPowerPhB":485,"PowerFactorPhB":79,"RMSVoltagePhC":21977,"RMSCurrentPhC":195,"ActivePowerPhC":319,"ReactivePowerPhC":-95,"ApparentPowerPhC":431,"PowerFactorPhC":74,"Endpoint":1}
```

Message 1 thiếu toàn bộ Pha C và nửa sau Pha B; Message 2 bổ sung phần còn thiếu. Hiện tượng này lặp lại nhiều lần trong log (~00:33, 00:37, 00:46, 00:53, 01:03, 01:09...) cho cả 2 thiết bị.

### Quy tắc xử lý bắt buộc cho code agent

- **Không xử lý từng message JSON độc lập** — nếu làm vậy, ~1 trong nhiều chu kỳ đo sẽ bị thiếu field Pha C (hoặc nửa Pha B), trông như dữ liệu `null`/lỗi dù thực ra là bình thường.
- **Gộp (merge) theo `Device` + cửa sổ thời gian ngắn** (khuyến nghị ≤ 2 giây) trước khi tính toán hay lưu xuống DB: giữ bản ghi mới nhất cho mỗi field, coi 2 message liên tiếp cùng Device trong cửa sổ đó là **1 chu kỳ đo duy nhất**.
- Field `Endpoint` và `LinkQuality` xuất hiện ở cả 2 message — không dùng làm tiêu chí phân biệt "đã đủ dữ liệu chưa"; nên dùng tập hợp field đã nhận được (VD: đã có `RMSVoltage` + `RMSVoltagePhB` + `RMSVoltagePhC` chưa) để xác định 1 chu kỳ đã hoàn chỉnh.
- Nếu quá cửa sổ thời gian mà message 2 không đến (mất gói), nên giữ giá trị Pha C/B cuối cùng đã biết (forward-fill) thay vì coi là 0, để tránh nhiễu số liệu.

---

## 10. Ví dụ decode đầy đủ 1 bản tin (0x51DF, 00:10:36)

**Raw JSON:**
```json
{"ACFrequency":5016,"TotalActivePower":1005,"TotalReactivePower":-311,"TotalApparentPower":1361,
 "RMSVoltage":22665,"RMSCurrent":204,"ActivePower":335,"ReactivePower":-110,"ApparentPower":462,"PowerFactor":72,
 "RMSVoltagePhB":22361,"RMSCurrentPhB":212,"ActivePowerPhB":367,"ReactivePowerPhB":-108,"ApparentPowerPhB":474,"PowerFactorPhB":77,
 "RMSVoltagePhC":22067,"RMSCurrentPhC":190,"ActivePowerPhC":302,"ReactivePowerPhC":-92,"ApparentPowerPhC":421,"PowerFactorPhC":71}
```

**Sau khi decode:**

| | Pha A | Pha B | Pha C | Tổng |
|---|---|---|---|---|
| Điện áp (V) | 226.65 | 223.61 | 220.67 | — |
| Dòng điện (A) | 2.04 | 2.12 | 1.90 | — |
| P (W) | 335 | 367 | 302 | 1005 |
| Q (VAR) | -110 | -108 | -92 | -311 |
| S (VA) | 462 | 474 | 421 | 1361 |
| PF | 0.72 | 0.77 | 0.71 | — |
| Tần số (Hz) | | | | 50.16 |
