# Slide 4: Scalability

# Khả năng đóng gói và nhân rộng

## Thiết kế cho 1 quán = Thiết kế cho cả chuỗi 10-100 quán

---

## Vấn đề của giải pháp thủ công

```mermaid
graph TD
    A[Quán 1<br/>Lắp đặt riêng] --> E[Không đồng bộ]
    B[Quán 2<br/>Lắp đặt riêng] --> E
    C[Quán 3<br/>Lắp đặt riêng] --> E
    D[Quán N<br/>Lắp đặt riêng] --> E
    E --> F[Khó quản lý]
    E --> G[Chi phí không kiểm soát]
    E --> H[Bảo trì rời rạc]
    E --> I[Không có dữ liệu tập trung]
    
    style E fill:#f44336,color:#fff
    style F fill:#f44336,color:#fff
    style G fill:#f44336,color:#fff
    style H fill:#f44336,color:#fff
    style I fill:#f44336,color:#fff
```

### Hệ quả:

- Mỗi quán tự lắp đặt riêng lẻ → khó quản lý, chi phí không kiểm soát được
- Không có quy trình chuẩn → mỗi nơi một kiểu, khó bảo trì
- Không đồng bộ dữ liệu → không biết quán nào tiết kiệm, quán nào lãng phí

---

## Giải pháp: Kiến trúc chuẩn hóa

```mermaid
graph TB
    A[Quy trình chuẩn<br/>Giai đoạn 0] --> B[Kit phần cứng<br/>Đồng nhất]
    B --> C[Firmware thống nhất<br/>Tasmota + OTA]
    C --> D[Dashboard tập trung<br/>Toàn chuỗi]
    D --> E[N quán<br/>Cùng 1 cách vận hành]
    
    style A fill:#4CAF50,color:#fff
    style B fill:#4CAF50,color:#fff
    style C fill:#4CAF50,color:#fff
    style D fill:#4CAF50,color:#fff
    style E fill:#FF9800,color:#fff
```

### 4 trụ cột chuẩn hóa

| Yếu tố | Cách thực hiện | Lợi ích |
|--------|---------------|---------|
| **Quy trình khảo sát chuẩn** | Checklist 1 site mẫu áp dụng cho mọi quán | Khảo sát nhanh, không bỏ sót |
| **Bộ thiết bị đồng bộ** | Cùng 1 loại Smart Panel, Gateway, cảm biến | Mua sắm số lượng lớn = giá tốt hơn |
| **Firmware thống nhất** | Tasmota + OTA update từ xa cho cả chuỗi | Cập nhật 1 lần, áp dụng mọi nơi |
| **Dashboard tập trung** | Theo dõi tất cả quán trên 1 hệ thống | So sánh, xếp hạng, tối ưu |

---