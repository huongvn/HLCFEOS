# Review kiến trúc: luồng MQTT → UI (CanopiCardGateway engine)

**Ngày:** 2026-08-07
**Phạm vi:** luồng xử lý Zigbee report từ Tasmota gateway → engine (Rust) → SQLite / HMI bridge / xsolar bridge → SSE → app LVGL

---

## 1. Tóm tắt vấn đề

Log thực tế cho thấy engine nhận đúng giá trị mới (`EF00/0110 = Number(1)`, tức bật) nhưng lại emit ra `OFF` cho UI. Nguyên nhân gốc nằm ở hàm `get_latest_state()` (`state_manager.rs:159`), dùng pattern SQL không an toàn:

```sql
SELECT attr_name, attr_value, attr_str, attr_type, MAX(ts)
FROM device_metric WHERE device_id=? GROUP BY attr_name
```

`GROUP BY attr_name` kèm `MAX(ts)` chỉ đảm bảo đúng cho **cột `ts`**. Các cột non-aggregate (`attr_value`, `attr_str`) SQLite trả về từ **một dòng bất kỳ trong group** — không đảm bảo là dòng có `ts` lớn nhất. Đây là root cause khiến `/api/v1/devices` và feedback UI thỉnh thoảng đúng, thỉnh thoảng sai.

Vấn đề này là triệu chứng của một pattern kiến trúc rộng hơn: **engine ghi giá trị mới vào SQLite rồi lập tức đọc lại để lấy state hiện tại**, dù giá trị mới đã có sẵn ngay tại thời điểm nhận message — round-trip này vừa thừa vừa là nguồn bug.

---

## 2. Kiến trúc hiện tại

```
Thiết bị vật lý (MCB/Sign 0xD46F)
   → Zigbee report EF00/0110
   → Tasmota gateway (tasmota_6DD514)
   → MQTT publish: tele/tasmota_6DD514/SENSOR

Engine (main.rs / state_manager.rs):
   process_mqtt_message()
     → process_zigbee_message()
     → normalize_attribute_value()          [value đã sẵn sàng ở đây]
     → batch_update_metrics()               [ghi SQLite]
     → get_latest_state()                   ⚠️ ĐỌC LẠI DB — BUG Ở ĐÂY
     → hmi_bridge.publish_feedback()        [so sánh state cũ/mới, emit]
     → xsolar_bridge.push_device_state()

HMI bridge (hmi_bridge.rs):
   publish_feedback() → so last_states cache → bus.emit("bms/sign/0/0110", "ON"/"OFF")

SSE / App (http_api.rs + http_client.c):
   EventBus → events_stream() → translate_event() → SSE /api/v1/events
   App: http_sse_thread() → bms_handle_api_event() → cập nhật UI
```

### Gap đã xác định

| # | Gap | Mức độ | Ghi chú |
|---|-----|--------|---------|
| 1 | `get_latest_state()` dùng `GROUP BY` + non-aggregate column | **Cao** — bug đang xảy ra | Root cause đã xác nhận qua log |
| 2 | Read-after-write không cần thiết trên hot path | Cao | Nguồn gốc thật sự của gap #1, tạo latency + race risk |
| 3 | `last_states` cache trong `hmi_bridge` chỉ ở memory, không seed lúc restart | Trung bình | Sau restart, lần nhận đầu tiên của mỗi attr bị coi là "thay đổi" → event storm giả |
| 4 | SSE không hỗ trợ resume (Last-Event-ID) | Trung bình | App mất kết nối tạm thời sẽ miss event trong lúc disconnect |
| 5 | Trách nhiệm dồn vào `main.rs` (transport + business logic + persistence) | Thấp | Khó unit test riêng phần normalize/eval |
| 6 | Chưa xác nhận SQLite bật WAL mode | Thấp–Trung bình | Cần cho write burst khi nhiều Zigbee device report đồng thời |

---

## 3. Kiến trúc đề xuất: fan-out sau normalize

Thay vì write → read → publish tuần tự, giá trị sau `normalize_attribute_value()` được fan-out song song tới 3 nhánh độc lập, không nhánh nào đọc lại DB:

```
normalize_attribute_value() → value đã có sẵn
   ├─→ batch_update_metrics(value)          // ghi SQLite, write-only, chỉ lưu lịch sử
   ├─→ hmi_bridge.publish_feedback(value)   // diff với last_states cache riêng, emit nếu đổi
   └─→ xsolar_bridge.update_cache(value)    // ghi in-memory cache (xem mục 4)
```

### Nguyên tắc thiết kế

- **Không nhánh nào phụ thuộc kết quả nhánh khác** — cả 3 đều dùng chung `value` đã tính xong, thứ tự thực thi giữa các nhánh không ảnh hưởng tính đúng đắn.
- **Cô lập lỗi** — lỗi ghi SQLite (disk full, lock timeout) không được làm mất event feedback tới UI, và ngược lại. Bọc try/catch riêng, log riêng cho từng nhánh.
- **Có thể `tokio::spawn` cho nhánh ghi SQLite** để không block việc emit feedback nếu SQLite đang bận I/O — feedback tới UI cần latency thấp nhất.
- **SQLite chỉ còn 1 vai trò: lưu lịch sử/audit**, không ai đọc lại trên hot path nữa.

### Vị trí duy nhất còn đọc lại DB

`GET /api/v1/devices` (REST, cold path — phục vụ client mới kết nối cần full state) vẫn cần query DB. Đây là nơi duy nhất cần áp fix SQL (mục 6), vì logic query giống hệt cái đang bug.

`hmi_bridge.last_states` cũng cần seed một lần từ SQLite lúc engine khởi động, dùng cùng query đã fix, để tránh event storm giả (gap #3).

---

## 4. Thiết kế cache riêng cho xsolar_bridge

xsolar cần push **full snapshot định kỳ mỗi 10 phút**, không chỉ real-time on-change — nên cần cache riêng giữ trạng thái cuối của toàn bộ device, độc lập với `last_states` của `hmi_bridge` (cache đó chỉ phục vụ diff, không phục vụ periodic push).

```
Fan-out (mỗi khi có message mới)
   → xsolar_bridge.update_cache(device_id, attr, value)   // ghi cache, không gọi network
   → xsolar_bridge.push_device_state(value)                // vẫn giữ push real-time on-change như cũ

tokio::interval(10 phút)
   → đọc snapshot toàn bộ cache
   → xsolar_bridge.push_full_snapshot()                    // 1 network call, full state, có retry
```

### Điểm cần lưu ý

| Vấn đề | Giải pháp |
|---|---|
| Kiểu dữ liệu cache | `DashMap<String, HashMap<String, Value>>` — ghi từ ingest path, đọc từ timer task đồng thời, không cần lock thủ công |
| Seed lúc khởi động | Nạp từ SQLite (query đã fix) một lần khi engine start, tránh thiếu device chưa có message mới sau restart |
| Update cache | Write-only, cực nhanh, không tạo backpressure lên hot path |
| Timer task độc lập | Lỗi/timeout khi push lên xsolar không ảnh hưởng real-time feedback hay ghi SQLite |
| Idempotency | Mỗi lần push là full snapshot ghi đè — không cần dedup/orphan logic giữa các chu kỳ |
| Retry/backoff | Cần, vì đây là network call ra ngoài, lỗi tạm thời không nên làm mất chu kỳ push tiếp theo |

---

## 5. Luồng điều khiển xuống (app LVGL & xsolar)

### 5.1. Hiện trạng

Mọi luồng gửi lệnh xuống thiết bị đều đi qua `queue_manager.send_zbsend()` → MQTT `cmnd/{gateway}/ZbSend` → Tasmota → Zigbee → thiết bị. Ba luồng cụ thể:

```
App LVGL (nút bật/tắt):
  sign_on_cb/off_cb → bms_set_onoff() → POST /api/v1/devices/{id}/actions
    → hmi_bridge.execute_action() → find_bool_control() → do_write()
        ├─ queue_mgr.send_zbsend()                     // command xuống
        └─ optimistic feedback: bus.emit(...)           // UI cập nhật ngay, không chờ ack
    → HTTP 202 ACCEPTED {command_id, status:"PENDING"}
    → register_pending(device_id, command_id)

App LVGL (scene open/close):
  scene_all_cb → POST /api/v1/scenes/master/actions
    → hmi_bridge.execute_scene_open()/close() → loop qua toàn bộ AC + Sign
        → send_zbsend() cho từng thiết bị, không stagger

xsolar (backend):
  MQTT command → xsolar_bridge.handle_xsolar_message() → parse → send_zbsend()
    → không có xác nhận ngược (ack/nack) về phía xsolar
```

Luồng gửi **hoạt động đúng** cho happy path — đây là lý do nút bấm trên panel phản hồi được ngay cả khi bug đọc-state (mục 1) đang tồn tại, vì luồng này không phụ thuộc DB. Tuy nhiên có các khoảng trống về độ tin cậy khi command thất bại thầm lặng (thiết bị offline, mất Zigbee ack, mesh nghẽn).

### 5.2. Gap ở luồng xuống

| # | Gap | Rủi ro | Đề xuất |
|---|-----|--------|---------|
| D1 | Optimistic feedback không reconcile với report thật | UI có thể hiển thị sai vĩnh viễn nếu command thất bại thầm lặng và thiết bị không tự report lại | Thêm timeout đối chiếu `command_id` với report thật (mục 5.3) |
| D2 | `register_pending` không có cơ chế dọn dẹp | Entry pending tồn tại mãi nếu report thật không bao giờ khớp `command_id` → memory leak nhẹ | Thêm TTL/cleanup định kỳ cho pending map |
| D3 | Chưa rõ có idempotency cho `command_id` trùng | App retry khi timeout HTTP có thể gửi lại cùng action → double `ZbSend` | Check `command_id` đã tồn tại trước khi gửi lại, trả 202 luôn nếu trùng thay vì gửi lần 2 |
| D4 | Scene open/close bắn `ZbSend` liên tục không stagger | Burst nhiều lệnh cùng lúc dễ gây nghẽn/rớt gói trên Zigbee mesh (mesh vốn đã nhạy với report tần suất cao) | Thêm delay nhỏ giữa các lệnh trong loop, hoặc rate-limit qua `queue_mgr` |
| D5 | `send_zbsend` không có retry/backoff khi MQTT publish thất bại | Lệnh rơi mất hoàn toàn nếu publish fail đúng lúc mất kết nối broker/Tasmota | Thêm retry với backoff ở `queue_manager` |
| D6 | xsolar không nhận ack/nack cho command đã gửi | xsolar không biết command có thành công không; nếu fail thầm lặng, cache xsolar (mục 4) mang giá trị sai tới chu kỳ push 10 phút kế tiếp | Trả ack/nack qua `command_id` giống app, hoặc tối thiểu log warning khi timeout không thấy report xác nhận |

### 5.3. Đề xuất: vòng reconcile command ↔ report thật

Vấn đề cốt lõi (D1, D6): luồng xuống (command) và luồng lên (report thật) hiện tách rời hoàn toàn, không có điểm nào đối chiếu `command_id` với việc thiết bị có thực sự đổi trạng thái hay không.

```
do_write() gửi command
   → lưu {command_id, device_id, attr, expected_value, sent_at} vào pending map
   → optimistic feedback emit ngay (giữ nguyên UX hiện tại)

Khi report thật (uplink) tới với cùng device_id/attr:
   → nếu value khớp expected_value → xác nhận thành công, xóa pending, gắn ack_command_id
   → nếu value KHÔNG khớp → ưu tiên giá trị report thật (đã đúng theo thiết kế fan-out ở mục 3), đồng thời có thể log mismatch để theo dõi tần suất

Timer quét pending map (vd mỗi 3-5s):
   → entry pending quá X giây (vd 10-15s) chưa có report xác nhận
   → coi là command có thể đã fail:
       - đánh dấu thiết bị "unconfirmed"/"stale" trên UI (khác với optimistic ON/OFF thường)
       - dọn entry khỏi pending map (giải quyết luôn D2)
       - (tùy chọn) gửi nack cho xsolar nếu command đến từ xsolar (giải quyết D6)
```

Cơ chế này tận dụng lại đúng pending map đã có (`register_pending`), chỉ cần thêm: lưu `expected_value` khi tạo pending, so khớp khi có report thật tới (đã đi qua fan-out ở mục 3 nên không cần đọc DB), và một timer quét định kỳ để dọn + downgrade UI khi timeout. Không cần thêm hạ tầng mới.

---

## 6. Fix SQL cụ thể (áp dụng cho `get_latest_state` và `GET /api/v1/devices`)

```sql
SELECT attr_name, attr_value, attr_str, attr_type, ts FROM (
  SELECT *, ROW_NUMBER() OVER (PARTITION BY attr_name ORDER BY ts DESC) rn
  FROM device_metric WHERE device_id = ?
) WHERE rn = 1
```

Window function rõ ràng và dễ maintain hơn correlated subquery khi cần thêm cột sau này.

---

## 7. Lộ trình triển khai đề xuất

1. **Fix ngay** — sửa SQL trong `get_latest_state()` bằng window function (mục 6). Không breaking, giảm rủi ro tức thì trong lúc chờ refactor lớn hơn.
2. **Refactor `process_zigbee_message`** theo pattern fan-out (mục 3) — loại bỏ hoàn toàn read-after-write trên hot path.
3. **Thêm in-memory cache cho `xsolar_bridge`** (mục 4) — tách push real-time và push định kỳ 10 phút.
4. **Seed `hmi_bridge.last_states` và `xsolar_bridge` cache** từ SQLite lúc khởi động, dùng query đã fix.
5. **Thêm vòng reconcile command ↔ report thật** (mục 5.3) — lưu `expected_value` vào pending map, đối chiếu khi report thật tới, timer dọn + downgrade UI khi timeout. Giải quyết đồng thời D1, D2, D6.
6. **Thêm idempotency cho `command_id` trùng** (D3) và **retry/backoff cho `send_zbsend`** (D5) ở `queue_manager`.
7. **Stagger lệnh trong scene open/close** (D4) — thêm delay nhỏ giữa các `ZbSend` để tránh burst nghẽn mesh.
8. **Xác nhận SQLite bật `journal_mode=WAL`** — đảm bảo chịu được write burst từ Zigbee mesh tần suất cao.
9. **(Tùy chọn, ưu tiên thấp)** thêm resume cho SSE qua Last-Event-ID nếu tần suất app mất kết nối tạm thời đáng kể trong thực tế.

---

## 8. Ghi chú triển khai

- Áp dụng bước 1 trước, độc lập với các bước còn lại — có thể deploy ngay để chặn bug hiện tại.
- Bước 2–3 nên đi cùng nhau vì cùng thay đổi signature/flow của `process_zigbee_message`.
- Bước 5 (reconcile) nên làm sau bước 2, vì fan-out là điều kiện để report thật tới `hmi_bridge`/pending map mà không phải đọc lại DB.
- Chưa cần unit test riêng cho `normalize_attribute_value` tách khỏi DB call cho tới khi refactor bước 2 xong — lúc đó tách logic dễ hơn nhiều.
