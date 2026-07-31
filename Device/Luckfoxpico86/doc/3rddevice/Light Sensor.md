`0x8BA4` là một cảm biến ánh sáng (Ambient Light Sensor) dùng cluster ZCL chuẩn **Illuminance Measurement (0x0400)**, hoàn toàn khác nhóm với 0x4BAD/0x51DF (đồng hồ điện) hay các thiết bị Tuya EF00 khác trong log.

## Giải mã field `Illuminance`

- Đây chính là attribute `0x0000` (**MeasuredValue**) của cluster `0x0400`.
- Giá trị log **không phải lux trực tiếp**, mà là giá trị thô theo công thức chuẩn ZCL:

```
lux = 10^((MeasuredValue − 1) / 10000)
```

Áp một vài mẫu trong log của bạn:

| Thời điểm | Raw (MeasuredValue) | → Lux (tính toán) | Nhận xét |
|---|---|---|---|
| 02:40:45 | 28877 | ≈ 771 lux | Ánh sáng khá mạnh (trong nhà gần cửa sổ / đèn sáng) |
| 02:40:48 | 30608 | ≈ 1149 lux | Sáng hơn |
| 02:47:13 | 30593 | ≈ 1145 lux | |
| 02:47:16 | 24167 | ≈ 261 lux | Giảm mạnh — có thể bóng che/mây qua |
| 02:47:19 | 13618 | ≈ 23 lux | Tối hẳn — như tắt đèn hoặc bị che hoàn toàn |
| 02:47:22 | 30450 | ≈ 1109 lux | Sáng trở lại ngay |
| 02:47:27 | 25978 | ≈ 396 lux | |
| 02:47:31 | 27178 | ≈ 522 lux | |
| 02:47:34 | 30532 | ≈ 1130 lux | |
| 02:48:00 | 30270 | ≈ 1065 lux | |

## Nhận xét về hành vi

**1. Dao động rất nhanh và mạnh** (23 lux → 1149 lux → 23 lux chỉ trong vài giây, giữa `02:47:16` và `02:47:22`) — không giống biến đổi ánh sáng tự nhiên thông thường (mây che thường biến thiên chậm hơn nhiều giây/phút). Nhiều khả năng là:
- Có vật/người di chuyển qua che cảm biến liên tục (giống hành vi trước cửa/gần lối đi), hoặc
- Cảm biến đặt gần đèn có chu kỳ nhấp nháy (PWM dimming, đèn LED không ổn định), hoặc
- Bản thân cảm biến bị nhiễu/không ổn định phần cứng.

**2. Kiểu report "burst rồi im lặng"**: gửi dồn dập 5-7 lần trong ~20 giây (`02:47:13` → `02:47:34`), rồi im hoàn toàn ~7 phút trước đó (từ `02:40:48` đến `02:47:13`) và sau đó (đến `02:48:00`). Đây là hành vi **report-on-change** điển hình của cảm biến ZCL (chỉ gửi khi giá trị thay đổi vượt ngưỡng cấu hình `ReportableChange`, cộng thêm 1 lần bắt buộc theo `MaxInterval`) — chứ không phải report định kỳ cố định như đồng hồ điện SPM02.

## Tóm gọn cho code agent

```
Cluster: 0x0400 (Illuminance Measurement)
Attribute: 0x0000 (MeasuredValue) → field JSON "Illuminance"
Raw type: uint16
Công thức: lux = 10^((raw - 1) / 10000)
Trường hợp đặc biệt: raw = 0 hoặc 0xFFFF → giá trị không hợp lệ/cảm biến lỗi (theo chuẩn ZCL)
```

Bạn có biết vị trí lắp đặt thực tế của `0x8BA4` không (gần cửa, gần đèn, ngoài trời...)? Nếu biết context vật lý, mình có thể giúp đánh giá chính xác hơn nguyên nhân dao động mạnh này có bất thường hay không.