# Kế hoạch: Cloud Command → Update Toàn Hệ Thống (Engine + App)

## 1. Bối cảnh & Mục tiêu

Hiện tại OTA engine chạy tự động theo chu kỳ (`check_interval: 3600`), OTA app chỉ có nút Check thủ công trong Settings. Không có cách nào **chủ động ép update từ xa** qua cloud.

**Mục tiêu:** Cho phép cloud (MQTT broker xsolar) gửi lệnh update tới engine; engine tự update chính nó, đồng thời ra lệnh cho app tự update; sau khi hoàn tất gửi ngược lên cloud bản tin trạng thái kèm version hiện tại; hỗ trợ lệnh check version chủ động.

## 2. Ràng buộc & Quyết định đã chốt

| # | Ràng buộc | Hệ quả |
|---|-----------|--------|
| 1 | **App LVGL KHÔNG dùng MQTT** | Mệnh lệnh update tới app phải đi qua **HTTP/SSE** từ engine (`/api/v1/events`) như cơ chế hiện tại |
| 2 | App tự cài qua HTTP engine | Engine chỉ ra hiệu lệnh; app tự chạy chuỗi GitHub-OTA vốn có của nó (không phải engine đẩy binary) |
| 3 | Kênh trigger: **chỉ cloud xsolar** | Không thêm kênh local MQTT cho chức năng này |
| 4 | Chế độ vận hành: **tự động full chuỗi** | Cloud gửi `{"action":"update"}` → engine check + download + install + restart engine và app, không cần thao tác tay |
| 5 | Giữ nút Check thủ công trong Settings | Chạy song song, không xoá luồng cũ |
| 6 | Update luôn cần khởi động lại app + engine | Sau khi update xong phải gửi ngược bản tin lên cloud kèm version |
| 7 | Có topic để cloud chủ động check version | Dùng chính topic command với `{"action":"check"}` |

## 3. Giao thức MQTT Cloud (xsolar)

Topic prefix lấy động từ `topic_prefix` trong `config/config.yaml` (hiện tại `smarteos/bluCafe`).

| Phía | Topic | Payload | Ý nghĩa |
|------|-------|---------|---------|
| Cloud → Engine | `{prefix}/ota/set` | `{"action":"update"}` | Ép update engine + app |
| Cloud → Engine | `{prefix}/ota/set` | `{"action":"check"}` | Hỏi version hiện tại |
| Engine → Cloud | `{prefix}/ota` | `{"state":"updated","engine":"1.2.5","app":"1.2.5","ts":"..."}` | Báo đã update xong |
| Engine → Cloud | `{prefix}/ota` | `{"state":"check_result","engine":"1.2.4","app":"1.2.4","ts":"..."}` | Trả lời check version |
| Engine → Cloud | `{prefix}/ota` | `{"state":"error","message":"...","ts":"..."}` | Báo lỗi |

Lưu ý:
- Subscription hiện tại của engine đã là `{prefix}/+/set` (xsolar_bridge.rs:51) → topic `ota/set` tự khớp, không cần đăng ký thêm.
- `handle_xsolar_message` hiện yêu cầu `parts.len()==4` → `smarteos/bluCafe/ota/set` có 4 phần, hợp lệ. `device_id = parts[2] = "ota"`.
- Điểm mấu chốt: `handle_remote_command` hiện **yêu cầu device tồn tại trong devices.yaml** → cần rẽ nhánh riêng cho `device_id == "ota"` **trước** khi lookup device manager.

## 4. Luồng xử lý chi tiết

### 4.1. Lệnh `{"action":"update"}`

```
Cloud ──{prefix}/ota/set {"action":"update"}──► Engine (xsolar_bridge)
                                                  │
        ┌─────────────────────────────────────────┤
        │ 1. Decode device_id == "ota"            │
        │ 2. Validate action                      │
        │ 3. Trigger OTA handler (callback)       │
        └─────────────────────────────────────────┘
                                                  ▼
                                    ┌───────────────────────────┐
                                    │  OtaCoordinator (main.rs) │
                                    └───────────────────────────┘
        ┌──────────────────────────────────┬─────────────────────────────┐
        ▼                                  ▼                             ▼
 Engine self-update (ota.rs)      Publish SSE tới app               Reply cloud
  check → download → verify     "system/ota" {"action":"update"}   state=started
  → backup → swap → ghi VERSION   (qua event_bus → /api/v1/events)
  → restart bms-engine
                                                  ▼
                                         App LVGL (SSE listener)
                                          ota_check_now()
                                          ota_start_download()
                                          ota_install_now()
                                          → reboot app
                                                  ▼
                          App boot → HTTP POST /api/system/app_version
                                    (engine lưu version app)
                                                  ▼
                    Engine boot → publish cloud {prefix}/ota
                     {"state":"updated","engine":X,"app":Y}
```

### 4.2. Lệnh `{"action":"check"}`

```
Cloud ──{prefix}/ota/set {"action":"check"}──► Engine
  → engine đọc current version engine (từ VERSION/cấu hình)
  → đọc version app cuối cùng app gửi lên (bộ nhớ, mặc định rỗng)
  → publish cloud {prefix}/ota {"state":"check_result","engine":X,"app":Y}
```

## 5. Thay đổi code — `Rush_engine`

### 5.1. `src/xsolar_bridge.rs`

- Thêm field `ota_command_tx: Option<mpsc::UnboundedSender<OtaCommand>>` (hoặc `Arc<tokio::sync::Notify>` + shared state) truyền vào từ main.
- Trong `handle_xsolar_message` / `handle_remote_command`: **đặt nhánh `device_id == "ota"` lên đầu**, trước lookup devices.yaml:

```rust
if device_id == "ota" {
    self.handle_ota_command(payload).await;
    return;
}
```

- `handle_ota_command(payload)`:
  - Parse `payload["action"]` → `"update"` hoặc `"check"`.
  - `"update"`: gửi lệnh `OtaCommand::Update` qua channel tới coordinator (main.rs), không block.
  - `"check"`: engine publish cloud `{prefix}/ota` `{"state":"check_result","engine":<ver>,"app":<ver>}`.
  - action không hợp lệ → publish `{"state":"error","message":"unknown action"}`.

### 5.2. `src/main.rs`

- Định nghĩa enum `OtaCommand { Update }` và `mpsc::UnboundedChannel<OtaCommand>`.
- Tạo channel ở phần setup, đưa sender vào `xsolar_bridge` constructor; spawn task **`ota_command_worker`** nhận từ receiver:

```rust
let (ota_tx, mut ota_rx) = mpsc::unbounded_channel::<OtaCommand>();
tokio::spawn(async move {
    while let Some(cmd) = ota_rx.recv().await {
        match cmd {
            OtaCommand::Update => run_cloud_ota(&ota_config, &event_bus, &mqtt_xsolar, &topic_prefix).await,
        }
    }
});
```

- `run_cloud_ota()`:
  1. Publish cloud `{prefix}/ota` `{"state":"started"}`.
  2. **Publish SSE tới app** qua `event_bus.emit("system/ota", json!({"action":"update"}))` — app đang subscribe SSE sẽ nhận.
  3. Gọi `check_ota_updates(&ota_config, true)` — hàm hiện có ở main.rs, ép `auto_update=true` (update ngay cả khi config `auto_update:false`).
     - **Chú ý:** hàm này tạo OtaUpdater **mới trong từng tick** → xem lại cách gọi để dùng chung luồng; tách phần thân thành hàm tái sử dụng được.
- **Đọc version app để trả lời check / báo updated:** lưu `app_version: Arc<Mutex<String>>` trong state dùng chung, được cập nhật bởi HTTP endpoint (mục 5.4), đọc bởi worker.
- Không cần đụng đến timer OTA sẵn có (vẫn chạy chu kỳ như cũ).

### 5.3. `src/ota.rs`

- Bổ sung getter công khai để lấy version hiện tại cho check:
  - `pub fn current_version(&self) -> &str` — **đã có** (dòng 145).
- (Không bắt buộc) thêm `pub async fn check_and_get_status(...)` gọn cho worker.

### 5.4. `src/http_api.rs`

- **Mở rộng `translate_event`** (hiện filter chỉ nhận topic `bms/*` 4 phần, http_api.rs:293/421): cho phép topic `system/ota` truyền thẳng qua SSE với payload gốc.

```rust
if parts[0] == "system" && parts[1] == "ota" {
    return Ok(Some((topic.to_string(), payload.clone())));
}
```

- **Thêm route `POST /api/system/app_version`** nhận body `{"app_version":"1.2.4"}`:
  - Cập nhật `app_version: Arc<Mutex<String>>`.
  - Trả `200 {"ok":true}`.
- (Không bắt buộc) route `GET /api/ota/status` trả `{engine, app}`.

### 5.5. `config/config.yaml` (board + mẫu trong repo)

- Không cần thay đổi field — `topic_prefix` đã có, subscription `{prefix}/+/set` đã có.
- Verify `github_repo` trỏ đúng `huongvn/HLCFEOS`.

## 6. Thay đổi code — App LVGL

### 6.1. `src/http_client.c`

- SSE listener hiện parse event `bms/...` → thêm nhánh xử lý topic `system/ota`:
  - Nhận `{"action":"update"}` → gọi `ota_auto_update_now()` (mục 6.2).
- Thêm hàm `http_client_report_app_version(const char *ver)`:
  - `POST /api/system/app_version` body `{"app_version":"..."}`, reuse `http_request` hiện có.
- Gọi hàm báo version sau khi app boot (sau khi OTA init, tại khởi tạo chính).

### 6.2. `src/ota.c`

- Thêm public wrapper chain:

```c
void ota_auto_update_now(void) {
    ota_check_now();
    // sau khi state = AVAILABLE → tự gọi ota_start_download();
    // sau khi download OK → ota_install_now();
}
```

- Đảm bảo các hàm `ota_check_now`, `ota_start_download`, `ota_install_now` đã public (hoặc thêm wrapper). Cơ chế chạy: dùng callback/state-machine hoặc chuỗi tuần tự + chờ — chọn phương án đơn giản nhất với cấu trúc hiện có của ota.c (kiểm tra khi thực hiện).
- Giữ nguyên nút Check thủ công.

## 7. Phiên bản & Release

- Bump **engine và app cùng version** (vd `1.2.5`).
- Push tag `bms-v1.2.5` → workflow `release-engine.yml` build engine.
- Push tag `app-v1.2.5` → workflow `release-app.yml` build app, upload app assets vào **cùng release `bms-v1.2.5`** (cơ chế hiện có — cả engine và app đọc `releases/latest`).
- Kết quả: release `bms-v1.2.5` = Latest, đủ 4 assets (`app_v1.2.5`, `.sha256`, `bms_v1.2.5.tar.gz`, `.sha256`).

## 8. Các rủi ro & lưu ý

| Rủi ro | Xử lý |
|--------|-------|
| SSE connection mất khi engine restart | App tự reconnect SSE (đã có); lệnh update được ban trước khi engine restart, app xử lý độc lập |
| `handle_remote_command` hiện từ chối device không trong devices.yaml | Rẽ nhánh `ota` **trước** lookup — phải làm đúng thứ tự |
| Engine update tự restart → quá trình publish cloud "updated" bị gián đoạn | Bản tin "updated" được publish bởi **instance mới** sau khi boot (worker đọc lại state); cần ghi trước `state=started` và để instance mới báo `state=updated` |
| App chưa kịp báo version trước khi engine publish | Khi báo `state=updated`, nếu chưa có `app` version → gửi `app:""` và (tùy chọn) báo thêm bản tin cập nhật sau khi app báo lên |
| Lệnh update trùng khi hệ thống đã mới | `check_update()` trả `(false, version)` → publish `{"state":"up_to_date"}` hoặc bỏ qua, không restart |
| `sudo -n systemctl restart` cần sudo không mật khẩu cho pico | Verify khi deploy; engine service đã chạy với User=pico — kiểm tra sudoers |
| App chạy với User=root, engine User=pico | Khi cần engine trigger app restart, đảm bảo quyền (service app hiện tự restart qua systemd) |

## 9. Các bước triển khai (thứ tự)

1. Implement `Rush_engine`: `xsolar_bridge.rs` (nhánh ota + OtaCommand), `main.rs` (channel + worker + `run_cloud_ota`), `http_api.rs` (SSE system/ota + POST app_version), `ota.rs` (getter nếu cần).
2. Implement App LVGL: `http_client.c` (SSE handler + report version), `ota.c` (wrapper auto update).
3. Build engine: `cargo build --release` trong `Rush_engine`.
4. Build app: `bash Device/Luckfoxpico86/code/lvgl_project/build.sh`.
5. Bump version engine + app → push tags → verify release chung đủ 4 assets.
6. Deploy lên board `172.32.0.70` (SFTP /tmp → sudo cp → chown pico:pico → chmod 755 → restart services).
7. Test:
   - Cloud gửi `{"action":"check"}` → nhận `check_result` đúng version.
   - Cloud gửi `{"action":"update"}` → theo dõi `journalctl -u bms-engine` + `journalctl -u myapp`; xác nhận app tự update, cả 2 restart, nhận `state=updated` kèm version.
   - Giữ nguyên nút Check thủ công hoạt động.
