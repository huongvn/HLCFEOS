# AGENTS.md

## Bản chất project
- **Không phải project code.** Mục tiêu là tạo bộ slide trình bày dự án.
- Mỗi trang slide = 1 file Markdown riêng biệt.
- Nội dung slide bằng tiếng Việt.

## Thông tin dự án
- **Tên dự án:** Hệ thống Quản trị Năng lượng Smart Cafe
- **Mục tiêu:** Dùng thiết bị IoT tiết kiệm năng lượng, tập trung vào:
  - Hệ thống điều hòa
  - Hệ thống đèn biển quảng cáo
  - Hệ thống đèn chiếu sáng

## Thiết bị phần cứng
| Vai trò | Thiết bị |
|---------|----------|
| Smart Panel | Luckfox Core1106 Smart 86 Box (RV1106G3, 1TOPS, 256MB DDR3L) |
| Gateway Hub | Hub Ewelink Zigbee Pro (flash Firmware Tasmota) |
| Thiết bị Zigbee | Cảm biến ánh sáng (pin solar), Công tắc zigbee, Contactor zigbee, Bộ điều khiển hồng ngoại zigbee |

## Tech Stack
- **Smart Panel:** Ubuntu, giao diện C++ LVGL, Node-RED, NanoMQ (MQTT broker), OTA updates
- **Gateway Hub:** Firmware Tasmota

## Cấu trúc slide (3 nhóm đối tượng)
1. **Ban Giám Đốc (Sếp & Tài chính):** Fail-safe (Local-First + Manual Override), Scalability (chuỗi 10-100 cửa hàng)
2. **Team Kỹ thuật:** Tech Stack & Kiến trúc tổng quan, Zigbee vs Wi-Fi (Mesh network, không chiếm IP), Watchdog & Maintenance (LWT, Heartbeat, cảnh báo tự động), Integration (MQTT/API về backend, đối chiếu điện mặt trời)
3. **Bước tiếp theo (Giai đoạn 0):** Khảo sát 1 site mẫu + Lab test trong 2 tuần, chi phí thấp, giảm rủi ro trước khi triển khai hàng loạt


## Cấu trúc thư mục
```
slides/
├── 01-title.md          # Slide tiêu đề + Tóm tắt dự án
├── 02-techstack.md      # Team Kỹ thuật: Tech Stack & Kiến trúc Hệ thống
├── 03-failsafe.md       # Ban Giám đốc: Fail-safe (Local-First + Manual Override)
├── 04-scalability.md    # Ban Giám đốc: Scalability (chuỗi 10-100 cửa hàng)
├── 05-zigbee.md         # Team Kỹ thuật: Zigbee vs Wi-Fi (Mesh, bảo mật)
├── 06-watchdog.md       # Team Kỹ thuật: Watchdog & Maintenance (LWT, Heartbeat)
├── 07-integration.md    # Team Kỹ thuật: Integration (MQTT/API + Solar)
├── 08-phase0.md         # Giai đoạn 0: Khảo sát 1 site + Lab test (2 tuần)
├── 09-smartpanel-ui.md  # Giao diện Smart Panel: LVGL HMI tại quán
└── 10-nodered-dashboard.md # Dashboard Node-RED: Quản lý qua web
```

## Cấu trúc Git (multi-repo)
Repo gốc `HLCFEOS` chứa 2 submodule:

| Submodule path | Repo riêng |
|----------------|------------|
| `Device/Luckfoxpico86/code/lvgl_project/` | `huongvn/HLCFEOS-lvgl_project` |
| `Device/Luckfoxpico86/code/Node-red/` | `huongvn/HLCFEOS-Node-red` |

### Quy tắc Commit & Push (BẮT BUỘC)
Submodule hoạt động độc lập với repo gốc. **Luôn push submodule trước, repo gốc sau.**

1. **Trước khi commit/push repo gốc**, kiểm tra tất cả submodule:
   ```
   git submodule foreach --recursive 'git status -s'
   ```
   Nếu có thay đổi trong submodule nào, commit + push submodule đó trước.

2. **Thứ tự push bắt buộc:**
   ```bash
   # Push từng submodule có thay đổi
   git -C <submodule-path> add -A && git -C <submodule-path> commit -m "..." && git -C <submodule-path> push

   # Về repo gốc, cập nhật con trỏ submodule
   git add <submodule-path> && git commit -m "chore: update submodule" && git push
   ```

3. **Clone lần đầu** phải dùng:
   ```
   git clone --recurse-submodules git@github.com:huongvn/HLCFEOS.git
   ```

4. **Sau mỗi `git pull`** ở repo gốc, chạy:
   ```
   git submodule update --init --recursive
   ```

## Quy ước làm việc
- Mỗi slide là 1 file `.md` độc lập, đặt trong thư mục `slides/`.
- Viết bằng tiếng Việt.
- Ưu tiên dùng bullet, số liệu và từ khóa thuyết phục thay vì đoạn văn dài.
