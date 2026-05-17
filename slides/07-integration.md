# Slide 7: Team Kỹ thuật — Integration

# Tích hợp sâu vào hệ thống chung

## Không đứng độc lập — Là mảnh ghép hoàn hảo

---

## Tầm nhìn tích hợp

```mermaid
graph TB
    subgraph "Hệ sinh thái Quản lý Năng lượng"
        A[Smart Cafe<br/>IoT] --> D[Backend API]
        B[Solar Monitoring<br/>Điện mặt trời] --> D
        C[ERP/CRM<br/>Hệ thống chung] --> D
        D --> E[Dashboard Tổng hợp]
        D --> F[AI/ML Analytics]
        D --> G[Báo cáo Tài chính]
    end
    
    style A fill:#4CAF50,color:#fff
    style B fill:#FF9800,color:#fff
    style D fill:#2196F3,color:#fff
    style E fill:#9C27B0,color:#fff
```

Giải pháp Smart Cafe không phải một đảo thông tin riêng lẻ. Nó là **mảnh ghép** trong bức tranh quản lý năng lượng tổng thể của công ty.

---

## Kiến trúc tích hợp tổng thể

```mermaid
graph TB
    subgraph "Tại quán"
        A[Smart Panel<br/>Luckfox RV1106] -->|MQTT| B[NanoMQ<br/>Local Broker]
        C[Gateway Tasmota] -->|MQTT| B
        B -->|Node-RED<br/>Automation| D[LVGL HMI<br/>Màn hình cảm ứng]
    end
    
    subgraph "Cloud/Backend"
        B -.->|MQTT over TLS/API| E[Backend API<br/>Cloud Server]
        E --> F[Dashboard<br/>Web/Mobile]
        E --> G[Database<br/>Time-series]
        E --> H[Alert Engine]
    end
    
    subgraph "Hệ thống liên kết"
        I[Solar Monitoring<br/>API] --> E
        J[ERP/CRM<br/>API] --> E
        E --> K[AI/ML<br/>Optimization]
    end
    
    style A fill:#4CAF50,color:#fff
    style E fill:#FF9800,color:#fff
    style F fill:#2196F3,color:#fff
```

---

## Luồng dữ liệu từ quán về backend

```mermaid
sequenceDiagram
    participant Device as Thiết bị Zigbee
    participant Gateway as Gateway Tasmota
    participant Panel as Smart Panel
    participant MQTT as NanoMQ
    participant Backend as Backend API
    participant DB as Time-series DB
    participant Dashboard as Dashboard
    
    Device->>Gateway: Dữ liệu cảm biến
    Gateway->>Panel: MQTT Publish
    Panel->>MQTT: Aggregation + Local logic
    Note over Panel,MQTT: Xử lý local trước<br/>giảm tải cloud
    
    MQTT->>Backend: MQTT over TLS
    Backend->>DB: Lưu trữ
    Backend->>Dashboard: Real-time update
    Backend->>Dashboard: Báo cáo định kỳ
```

---

## 3 điểm tích hợp chính

### 1. MQTT → Backend trung tâm

| Dữ liệu | Tần suất | Mục đích |
|---------|----------|----------|
| Trạng thái thiết bị | Real-time | Giám sát |
| Dữ liệu cảm biến | 5-15 phút | Phân tích, tối ưu |
| Điện năng tiêu thụ | 15-30 phút | Tính toán chi phí |
| Sự kiện (bật/tắt) | Real-time | Audit log |

- Backend subscribe và lưu trữ, phân tích
- **Real-time dashboard** cho toàn chuỗi

### 2. Đối chiếu với điện mặt trời (Solar)

```mermaid
graph LR
    A[Smart Cafe<br/>Tiêu thụ điện] --> C[Backend]
    B[Solar Monitoring<br/>Sản xuất điện] --> C
    C --> D{Net Metering}
    D --> E[Tiêu thụ: 1000 kWh]
    D --> F[Sản xuất: 400 kWh]
    D --> G[Thực trả: 600 kWh]
    
    style E fill:#f44336,color:#fff
    style F fill:#4CAF50,color:#fff
    style G fill:#FF9800,color:#fff
```

- Hệ thống Smart Cafe biết **mỗi quán tiêu thụ bao nhiêu điện**
- Hệ thống Solar biết **mỗi quán sản xuất bao nhiêu điện**
- **Tính toán net-metering:** Tiêu thụ - Sản xuất = Chi phí thực tế
- Tối ưu hóa: Khi nào nên dùng điện solar, khi nào nên tiết kiệm

### 3. API mở rộng

```mermaid
graph TB
    A[Backend API] --> B[ERP<br/>Quản lý tài sản]
    A --> C[CRM<br/>Chăm sóc khách hàng]
    A --> D[Báo cáo tài chính<br/>Tự động]
    A --> E[Mobile App<br/>Cho quản lý]
    A --> F[AI/ML<br/>Dự đoán, tối ưu]
    
    style A fill:#4CAF50,color:#fff
```

- Cung cấp RESTful API cho các hệ thống khác
- Đồng bộ dữ liệu tự động, không cần nhập liệu thủ công
- Mở rộng: AI/ML phân tích pattern tiêu thụ, dự đoán hỏng hóc

---

## Giá trị trước & sau tích hợp

```mermaid
graph TB
    subgraph "Trước tích hợp"
        A1[Không biết quán nào tiết kiệm]
        B1[Không biết Solar có đủ không]
        C1[Nhập liệu thủ công hàng tháng]
        D1[Không có cơ sở để tối ưu]
    end
    
    subgraph "Sau tích hợp"
        A2[Báo cáo so sánh từng quán real-time]
        B2[Tính toán tự động nhu cầu vs cung cấp]
        C2[Dữ liệu tự động đồng bộ 24/7]
        D2[AI/ML phân tích để tối ưu năng lượng]
    end
    
    style A1 fill:#f44336,color:#fff
    style B1 fill:#f44336,color:#fff
    style C1 fill:#f44336,color:#fff
    style D1 fill:#f44336,color:#fff
    style A2 fill:#4CAF50,color:#fff
    style B2 fill:#4CAF50,color:#fff
    style C2 fill:#4CAF50,color:#fff
    style D2 fill:#4CAF50,color:#fff
```

---

## Thông điệp thuyết phục

> *"Giải pháp này không đứng độc lập. Nó là một mảnh ghép hoàn hảo trong bức tranh lớn của công ty — kết nối từ màn hình cảm ứng tại quán, qua MQTT/API về hệ thống quản lý chung, và đối chiếu với sản lượng điện mặt trời."*

---

## Lợi ích tích hợp

- ✅ **Dữ liệu tập trung:** Một nơi xem toàn bộ hệ thống
- ✅ **Tự động hóa:** Không nhập liệu thủ công
- ✅ **Tối ưu toàn diện:** Kết hợp tiêu thụ + sản xuất điện
- ✅ **Mở rộng:** Dễ dàng tích hợp AI/ML, ERP, CRM trong tương lai

