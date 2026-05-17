# Slide 1: Tiêu đề & Tổng quan dự án

# Hệ thống Quản trị Năng lượng Smart Cafe

## Giải pháp IoT tiết kiệm năng lượng toàn diện cho chuỗi F&B

---

## Tóm tắt dự án trong 30 giây

- **Tên dự án:** Hệ thống Quản trị Năng lượng Smart Cafe
- **Đối tượng:** Chuỗi cafe & F&B
- **Mục tiêu:** Tiết kiệm **30%** điện năng tiêu thụ
- **Thờigian hoàn vốn:** **~10 tháng**
- **Phạm vi ứng dụng:**
  - Hệ thống điều hòa
  - Hệ thống đèn biển quảng cáo
  - Hệ thống đèn chiếu sáng

---

## Kiến trúc tổng quan

```mermaid
graph TB
    subgraph "Tại quán - Local"
        A[Smart Panel<br/>Luckfox RV1106G3] --> B[NanoMQ<br/>MQTT Broker]
        B --> C[Node-RED<br/>Automation Engine]
        C --> D[LVGL HMI<br/>Màn hình cảm ứng]
        E[Gateway Tasmota<br/>Zigbee Hub] --> B
        E --> F[Cảm biến ánh sáng]
        E --> G[Công tắc Zigbee]
        E --> H[Contactor Zigbee]
        E --> I[IR Controller]
    end
    
    subgraph "Cloud/Backend"
        J[Backend API] --> K[Dashboard Tập trung]
        J --> L[Solar Monitoring]
        J --> M[OTA Update Server]
    end
    
    B -. MQTT/API .-> J
    
    style A fill:#4CAF50,color:#fff
    style E fill:#2196F3,color:#fff
    style J fill:#FF9800,color:#fff
```

---

## 3 Nhóm đối tượng chính

| Nhóm | Quan tâm chính | Slide tương ứng |
|------|---------------|-----------------|
| **Ban Giám đốc** (Sếp & Tài chính) | Tiền, Rủi ro, Khả năng nhân rộng | #2 ROI, #3 Fail-safe, #4 Scalability |
| **Team Kỹ thuật** | Tính khả thi, Độ bền, Tích hợp | #5 Zigbee, #6 Watchdog, #7 Integration |
| **Bước tiếp theo** | Giai đoạn 0: Khảo sát & Lab Test | #8 Phase 0 |

---

## Thông điệp cốt lõi

> *"Hệ thống sẽ tự nuôi chính nó sau chưa đầy 1 năm. Những năm sau đó là lợi nhuận thuần túy cho chuỗi."*

---

## Thiết bị & Công nghệ nổi bật

| Thành phần | Thiết bị/Công nghệ |
|-----------|-------------------|
| **Smart Panel** | Luckfox Core1106 Smart 86 Box (RV1106G3, 1TOPS, 256MB DDR3L) |
| **Gateway** | Hub Ewelink Zigbee Pro (Firmware Tasmota) |
| **Cảm biến** | Cảm biến ánh sáng (pin solar) |
| **Điều khiển** | Công tắc, Contactor, IR Controller (Zigbee) |
| **Phần mềm** | Ubuntu, LVGL, Node-RED, NanoMQ, OTA |

