# Slide 3: Fail-safe

# Triệt tiêu nỗi sợ "Rủi ro vận hành"

---

## Nỗi sợ

> *"Nếu hệ thống lỗi, quán có phải đóng cửa không? Khách có bị nóng không? Đèn có tắt hết không?"*

**Hệ thống được thiết kế để không bao giờ làm gián đoạn vận hành.**

---

## Kiến trúc Local-First (Ưu tiên điều khiển tại chỗ)

```mermaid
graph TB
    subgraph "Internet/Cloud"
        A[Cloud Backend]
    end
    
    subgraph "Tại quán - Hoạt động độc lập"
        C[Gateway Zigbee]
        C--> D[Smart Panel<br/>Node-RED + NanoMQ + LVGL HMI<br/>Màn hình cảm ứng]
        
        F[Cảm biến ánh sáng] --> C
        G[Công tắc Zigbee] --> C
        H[Contactor] --> C
        I[IR Controller] --> C
    end

    A -. Gửi lệnh .-> D
    A -. Nhận báo cáo .-> D

    style A fill:#9E9E9E,color:#fff
```

### Điểm mạnh của Local-First

- **Smart Panel** hoạt động độc lập hoàn toàn tại quán
- **Không phụ thuộc internet** để điều khiển thiết bị
- Các kịch bản tự động (automation) chạy trên **Node-RED local**
- NanoMQ broker chạy **ngay tại chỗ**, không cần cloud
- Dù mất internet 1 tuần, hệ thống vẫn chạy bình thường

---

## Manual Override (Chế độ dự phòng vật lý)

```mermaid
graph LR
    A[Nhân viên] --> B[Công tắc Zigbee]
    A --> C[Thermostat điều hòa]
    A --> D[Node-red Dashboard<br/>nội bộ]
    
    B --> E[Đèn/Thiết bị]
    C --> F[Điều hòa]
    D --> G[Gateway Zigbee]
    G --> E
    G --> F
    
    H[Automation<br/>Smart Touch Panel] --> G
    A --> H

    style B fill:#4CAF50,color:#fff
    style C fill:#4CAF50,color:#fff
    style D fill:#4CAF50,color:#fff
    style H fill:#2196F3,color:#fff
```

### Các lớp dự phòng ( ví dụ với hệ thống điều hòa)

| Lớp | Cách thức | Tình huống |
|-----|-----------|------------|
| **Lớp 1: Tự động** | Smart Panel chạy automation | Hoạt động bình thường |
| **Lớp 2: Màn hình** | Nhấn trên màn hình cảm ứng LVGL | Khi cần điều chỉnh nhanh |
| **Lớp 3: Thermostat** | Bật/tắt trực tiếp thermostat trên tường | Khi Smart Panel lỗi |

---

## Các tình huống rủi ro & Giải pháp

```mermaid
graph TD
    A[Tình huống] --> B{Mất Internet?}
    B -->|Có| C[Hệ thống local<br/>vẫn chạy 100%]
    B -->|Không| D{Smart Panel hỏng?}
    D -->|Có| E[Dùng Thermostat<br/>+ Remote IR]
    D -->|Không| F{Gateway Zigbee lỗi?}
    F -->|Có| G[Thermostat hoạt động<br/>độc lập]
    F -->|Không| H[Hoạt động<br/>bình thường]
    
    style C fill:#4CAF50,color:#fff
    style E fill:#4CAF50,color:#fff
    style G fill:#4CAF50,color:#fff
    style H fill:#4CAF50,color:#fff
```

| Tình huống | Ảnh hưởng | Giải pháp |
|-----------|-----------|-----------|
| **Mất internet** | Không có remote từ cloud | Local automation vẫn chạy |
| **Smart Panel lỗi** | Không có automation | Thermostat + Remote vẫn dùng được |
| **Gateway Zigbee lỗi** | Không tự động hóa | Thermostat bypass hoạt động |
| **Cảm biến hỏng** | Không tối ưu ánh sáng | Chuyển sang timer hoặc manual |
| **Mất điện** | Tất cả tắt | Bình thường như trước khi có hệ thống |

---

## Tổng kết

- ✅ **Không bao giờ** đóng cửa vì lỗi hệ thống
- ✅ **Không bao giờ** khách bị nóng/lạnh bất thường
- ✅ **Không bao giờ** mất điện đèn đột ngột
- ✅ Rủi ro vận hành = **gần như 0**

