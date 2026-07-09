# Hướng dẫn Deploy Remote Bridge — Không rủi ro, không xóa flow cũ

> **Phương châm:** Import tối thiểu, không xóa gì cả, chỉ thêm tab mới + 1 wire.

---

## Tóm tắt

| Thao tác | Rủi ro | Giải thích |
|----------|--------|------------|
| Import `safe-import-patch.json` | **Không có** | Chỉ thêm nodes mới, ghi đè duy nhất `fn-l2-update` (thêm 1 wire) |
| Xóa/Xóa flows cũ | **Không cần** | Không đụng đến 154 nodes hiện có |
| Restart Node-RED | **Không bắt buộc** | Chỉ cần Deploy trong editor |

---

## File cần dùng

| File | Nội dung | Số nodes |
|------|----------|----------|
| `safe-import-patch.json` | Tab Remote Bridge (11 nodes) + L2 wire patch (2 nodes) | 13 |

---

## Checklist từng bước

### Bước 1: Backup (QUAN TRỌNG)

Trong Node-RED editor:
1. Menu ☰ → **Export** → **all flows**
2. Copy to clipboard → lưu vào file `flows-backup-YYYY-MM-DD.json`
3. Giữ file này để khôi phục nếu cần

### Bước 2: Import patch

1. Menu ☰ → **Import** → **Clipboard**
2. Mở file `safe-import-patch.json` → copy nội dung → paste vào clipboard
3. Chọn **"New Flow"** → click **Import**

**Kết quả:**
- Tab mới **"Remote Bridge"** xuất hiện (bên phải editor)
- Tab **"L2 Device Abstraction"** có node `fn-l2-update` được cập nhật (thêm wire màu cam)

### Bước 3: Kiểm tra kết nối

Vào tab **L2 Device Abstraction**:
- Tìm node `fn-l2-update` (tên: "Update global state")
- Kiểm tra output wires: phải có **4 dây** ra:
  - `lk-l2-to-l3` (L3 storage)
  - `lk-l2-to-l5` (L5 Dashboard)
  - `lk-l2-to-hmi` (HMI Bridge)
  - `lk-l2-to-remote` (**mới thêm** → Remote Bridge)

Vào tab **Remote Bridge**:
- Kiểm tra node `lk-remote-event-in` có link từ `lk-l2-to-remote`
- Kiểm tra `cfg-remote-broker` có cấu hình broker `mqtt.xsolar.energy:1883`

### Bước 4: Cấu hình broker (chưa có credentials)

**Nếu chưa có user/password từ xsolar:**
- Double-click `cfg-remote-broker`
- Để trống User/Password → **Update**
- Tab **Remote Bridge** → click chuột phải → **Disable flow**
→ Để test các tab cũ vẫn chạy bình thường.

**Nếu đã có credentials:**
- Double-click `cfg-remote-broker` → nhập User/Password → **Update**

### Bước 5: Deploy

Click nút **Deploy** (góc trên phải).

**Chọn "Modified Flows"** (không cần Full).

### Bước 6: Verify (không rủi ro)

| Kiểm tra | Kết quả mong đợi |
|----------|------------------|
| Tab Startup, L1, L3, L4, L5, HMI vẫn chạy | Không có lỗi debug |
| Dashboard vẫn hiển thị | Truy cập `http://<ip>:1880/dashboard` |
| MQTT local vẫn nhận data | Tasmota gửi SENSOR bình thường |
| SQLite vẫn ghi log | `device_log` và `device_metric` tiếp tục tăng |

### Bước 7: Enable Remote Bridge (khi sẵn sàng)

Khi đã có credentials xsolar + test xong:
1. Tab **Remote Bridge** → click chuột phải → **Enable flow**
2. Deploy
3. Kiểm tra debug node trong tab Remote Bridge xem có publish thành công không

---

## Khôi phục nếu có vấn đề

Nếu sau deploy có lỗi:
1. Menu ☰ → **Import** → paste file backup `flows-backup-YYYY-MM-DD.json`
2. Chọn **"Replace current flow"**
3. Deploy → hệ thống về trạng thái cũ

---

## Nodes trong patch

### Nodes mới (12 nodes)

| ID | Type | Tab | Chức năng |
|----|------|-----|-----------|
| `tab-remote` | tab | — | Tab Remote Bridge |
| `cfg-remote-broker` | mqtt-broker | — | Broker xsolar.energy:1883 |
| `lk-remote-event-in` | link in | Remote Bridge | Nhận từ L2 |
| `queue-remote-event` | delay | Remote Bridge | Rate limit 50/s |
| `fn-remote-event-format` | function | Remote Bridge | Format JSON event |
| `inj-remote-10min` | inject | Remote Bridge | Trigger 10 phút |
| `fn-remote-query` | function | Remote Bridge | Build SQL periodic |
| `sql-remote-latest` | sqlite | Remote Bridge | Query DB |
| `fn-remote-build-periodic` | function | Remote Bridge | Build JSON periodic |
| `queue-remote-publish` | delay | Remote Bridge | Rate limit 20/s |
| `mqtt-remote-out` | mqtt out | Remote Bridge | Publish xsolar |
| `lk-l2-to-remote` | link out | L2 | Wire từ L2 → Remote |

### Node ghi đè (1 node)

| ID | Type | Tab | Thay đổi |
|----|------|-----|----------|
| `fn-l2-update` | function | L2 | Wires: 3 → 4 (thêm `lk-l2-to-remote`) |

> **Lưu ý:** Node `fn-l2-update` chỉ thay đổi mảng `wires`, không đổi code function.

---

*File generated for zero-risk deployment on production Luckfox.*
