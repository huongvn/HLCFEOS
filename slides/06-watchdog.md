# Slide 6: Watchdog & Maintenance

# Khả năng giám sát "Sức khỏe" hệ thống

## Không để kỹ thuật phải "đoán mò"

---

## Nỗi sợ của Team Kỹ thuật

> *"Giải pháp vẽ ra thì hay, nhưng thực tế chạy chập chờn, suốt ngày phải đi sửa."*

**Cam kết: Hệ thống tự chẩn đoán, báo lỗi chính xác, giảm 80% thờigian xử lý sự cố.**

---

## Cơ chế giám sát 3 lớp

```mermaid
graph TB
    subgraph "Lớp 1: Thiết bị Zigbee"
        A[Cảm biến] -->|Heartbeat<br/>30 phút| B[Gateway Tasmota]
        C[Công tắc] -->|Heartbeat<br/>1 giờ| B
        D[Contactor] -->|Heartbeat<br/>1 giờ| B
    end
    
    subgraph "Lớp 2: Gateway & Smart Panel"
        B -->|LWT + Status<br/>MQTT| E[NanoMQ Broker]
        F[Smart Panel] -->|Heartbeat<br/>30 giây| E
    end
    
    subgraph "Lớp 3: Backend/Dashboard"
        E -->|Publish| G[Backend Server]
        G --> H[Dashboard<br/>Real-time]
        G --> I[Alert Engine]
        I --> J[Email/SMS<br/>Cảnh báo]
    end
    
    style E fill:#4CAF50,color:#fff
    style G fill:#FF9800,color:#fff
    style I fill:#f44336,color:#fff
```

---

## 1. LWT (Last Will and Testament)

### Nguyên lý

```mermaid
sequenceDiagram
    participant Device as Thiết bị Zigbee
    participant Gateway as Gateway Tasmota
    participant MQTT as NanoMQ Broker
    participant Backend as Backend
    
    Device->>Gateway: Kết nối + đăng ký LWT
    Gateway->>MQTT: Publish LWT topic
    Note over Device,Gateway: Hoạt động bình thường...
    
    Device-xGateway: Mất kết nối (>X phút)
    Gateway->>MQTT: Kích hoạt LWT message
    MQTT->>Backend: Thiết bị OFFLINE
    Backend->>Backend: Ghi log + Tạo cảnh báo
```

- Thiết bị Zigbee gửi tín hiệu "còn sống" định kỳ
- Nếu mất tín hiệu quá ngưỡng (configurable) → hệ thống tự động đánh dấu **"Offline"**
- **Không cần kỹ thuật viên kiểm tra từng thiết bị**

---

## 2. Heartbeat từ Smart Panel

```mermaid
graph LR
    A[Smart Panel<br/>Luckfox RV1106] -->|{"status":"online",<br/>"cpu":45%,<br/>"temp":55°C,<br/>"uptime":"15d"}| B[NanoMQ]
    B --> C[Backend]
    C --> D{Kiểm tra}
    D -->|Bình thường| E[Xanh]
    D -->|CPU cao| F[Vàng - Cảnh báo]
    D -->|Mất heartbeat| G[Đỏ - Offline]
    
    style E fill:#4CAF50,color:#fff
    style F fill:#FF9800,color:#fff
    style G fill:#f44336,color:#fff
```

- Smart Panel gửi heartbeat đầy đủ thông tin hệ thống
- Tần suất: **Mỗi 30-60 giây**
- Nếu mất heartbeat → cảnh báo ngay lập tức

---

## 3. Cảnh báo tự động theo tình huống

```mermaid
graph TD
    A[Tình huống] --> B{Cảm biến hết pin?}
    B -->|Có| C[Cảnh báo: Pin yếu<br/>Vị trí: Quán A, Khu vực B]
    B -->|Không| D{Gateway mất kết nối?}
    D -->|Có| E[Cảnh báo CẤP CAO<br/>Toàn bộ quán A offline]
    D -->|Không| F{Smart Panel lỗi?}
    F -->|Có| G[Cảnh báo: Panel lỗi<br/>Kích hoạt manual mode]
    F -->|Không| H[Bình thường]
    
    style C fill:#FF9800,color:#fff
    style E fill:#f44336,color:#fff
    style G fill:#f44336,color:#fff
    style H fill:#4CAF50,color:#fff
```

### Bảng cảnh báo chi tiết

| Tình huống | Mức độ | Cảnh báo | Hành động tự động |
|-----------|--------|----------|-------------------|
| Cảm biến pin yếu | Thấp | Thông báo vị trí | Lập lịch thay pin |
| Cảm biến hết pin | Trung bình | Cảnh báo + đề xuất | Chuyển sang timer |
| Công tắc mất nguồn | Trung bình | Cảnh báo vị trí | Kiểm tra điện |
| Gateway mất kết nối | Cao | Cảnh báo cấp độ cao | Kích hoạt backup |
| Smart Panel không phản hồi | Cao | Cảnh báo + manual | Bypass automation |
| Nhiệt độ panel cao | Trung bình | Cảnh báo | Tăng tốc quạt |

---

## Dashboard giám sát tập trung

```mermaid
graph TB
    subgraph "Dashboard Smart Cafe"
        A[Map View<br/>Vị trí các quán] --> B[Quán A: 🟢 Online]
        A --> C[Quán B: 🟡 Cảnh báo]
        A --> D[Quán C: 🔴 Offline]
        
        B --> E[Thiết bị: 10/10 Online]
        C --> F[Thiết bị: 9/10<br/>Cảm biến A pin yếu]
        D --> G[Thiết bị: 0/10<br/>Gateway mất kết nối]
    end
    
    style B fill:#4CAF50,color:#fff
    style C fill:#FF9800,color:#fff
    style D fill:#f44336,color:#fff
```

### Tính năng Dashboard

- **Real-time status:** Hiển thị trạng thái tất cả thiết bị
- **Màu sắc phân biệt:**
  - 🟢 Xanh: Online, hoạt động tốt
  - 🟡 Vàng: Cảnh báo (cần chú ý)
  - 🔴 Đỏ: Offline/Lỗi (cần xử lý ngay)
- **Lịch sử sự kiện:** Biết chính xác thời điểm thiết bị lỗi
- **Báo cáo định kỳ:** Tự động gửi báo cáo tuần/tháng

---
## Lợi ích cho Team Kỹ thuật

- ✅ **Giảm 80% thờigian xử lý sự cố** nhờ biết chính xác vị trí lỗi
- ✅ **Proactive maintenance** thay vì reactive (sửa khi hỏng)
- ✅ **Giảm tải cho team kỹ thuật**, không cần kiểm tra định kỳ thủ công
- ✅ **Remote diagnostics:** Chẩn đoán từ xa trước khi đến quán

