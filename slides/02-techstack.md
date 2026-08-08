# Slide 2: Tổng quan Kiến trúc Kỹ thuật

# Tech Stack & Kiến trúc Hệ thống Smart Cafe

## Từ cảm biến Zigbee đến Dashboard — Luồng dữ liệu và công nghệ

---

## Kiến trúc Tổng quan (High-level)

```mermaid
graph TB
    subgraph "Edge - Tại quán"
        subgraph "Smart Panel<br/>Luckfox Core1106<br/>RV1106G3, 256MB"
            B[NanoMQ<br/>MQTT Broker<br/>Port 1883]
            D[Node-RED<br/>Automation + Dashboard<br/>Port 1880]
            E[LVGL HMI<br/>Màn hình 4 inch<br/>720x720]
        end
        
        C[Gateway Tasmota<br/>Zigbee Coordinator] -->|MQTT| B
    end
    
    subgraph "Zigbee Mesh Network"
        C -->|Zigbee 3.0| G[Cảm biến ánh sáng<br/>Pin solar]
        C -->|Zigbee 3.0| H[Công tắc Zigbee<br/>Router Node]
        C -->|Zigbee 3.0| I[Contactor Zigbee<br/>Router Node]
        C -->|Zigbee 3.0| J[IR Controller<br/>Điều hòa<br/>Router]
        H -->|Mesh relay| G
    end
    
    subgraph "Cloud / Remote"
        B -.->|MQTT over TLS| K[Backend API<br/>Cloud Server]
        K --> L[Dashboard Tập trung]
        K --> M[OTA Update Server]
    end
    
    style B fill:#FF9800,color:#fff
    style C fill:#2196F3,color:#fff
    style D fill:#9C27B0,color:#fff
    style E fill:#4CAF50,color:#fff
```

---

## 1. Smart Panel — Trung tâm điều khiển Local

### Phần cứng

| Thông số | Giá trị |
|----------|---------|
| **Board** | Luckfox Core1106 Smart 86 Box |
| **SoC** | Rockchip RV1106G3 |
| **NPU** | 1 TOPS (hỗ trợ AI inference) |
| **RAM** | 256MB DDR3L |
| **Màn hình** | 4 inch, cảm ứng điện dung, 720x720 |
| **Storage** | eMMC / microSD |
| **Kết nối** | Ethernet, Wi-Fi, USB OTG |

### Phần mềm Stack

```mermaid
graph TB
    subgraph "Smart Panel Software Stack"
        A[Ubuntu Linux<br/>Optimized for RV1106] --> B[Drivers<br/>GPIO/I2C/SPI/UART]
        B --> C[System Services]
        
        C --> D[NanoMQ<br/>MQTT Broker<br/>Port 1883]
        C --> E[Node-RED<br/>Runtime<br/>Port 1880]
        C --> F[LVGL App<br/>C++ HMI<br/>Framebuffer]
     
        
        D <--> E
        D <--> F
    end
    
    style D fill:#4CAF50,color:#fff
    style E fill:#2196F3,color:#fff
    style F fill:#FF9800,color:#fff
```

| Thành phần | Vai trò | Lý do chọn |
|-----------|---------|-----------|
| **Ubuntu** | OS nền | Hệ sinh thái rộng, dễ dev, hỗ trợ RV1106 |
| **NanoMQ** | MQTT Broker | Nhẹ, high-performance, phù hợp edge device 256MB RAM |
| **Node-RED** | Automation + Dashboard | Low-code, dễ tùy biến flow, có sẵn MQTT nodes |
| **LVGL** | GUI trên màn hình 4" | C++ native, nhẹ, render nhanh trên embedded |

---

## 2. Gateway Hub — Zigbee Coordinator

### Phần cứng

| Thông số | Giá trị |
|----------|---------|
| **Thiết bị** | Hub Ewelink Zigbee Pro |
| **Flash firmware** | Tasmota (open-source) |
| **Chuẩn Zigbee** | 3.0 |
| **Vai trò** | Zigbee Coordinator + MQTT Bridge |

### Chức năng chính

```mermaid
graph LR
    A[Thiết bị Zigbee<br/>Cảm biến/Công tắc] -->|Zigbee 3.0<br/>AES-128| B[Gateway Tasmota]
    B -->|MQTT Publish| C[NanoMQ<br/>Smart Panel]
    C -->|MQTT Subscribe| D[Node-RED<br/>Smart Panel]
    D -->|MQTT Publish| C
    C -->|MQTT Subscribe| B
    B -->|Zigbee Command| A
    
    style B fill:#4CAF50,color:#fff
    style C fill:#2196F3,color:#fff
```

- **Coordinator:** Quản lý mạng Zigbee, cấp phát địa chỉ, lưu routing table
- **MQTT Bridge:** Chuyển đổi Zigbee message ↔ MQTT topic chuẩn
- **Topic format:**
  - Nhận dữ liệu: `zigbee2mqtt/[device_name]`
  - Gửi lệnh: `zigbee2mqtt/[device_name]/set`

---

## 3. Zigbee Mesh Network

```mermaid
graph TB
    subgraph ZigbeeNet["🔷 Zigbee Mesh Network"]
        
        %% Coordinator
        A[["🔴 Gateway\nCoordinator · Tasmota"]]

        %% Routers
        B["🔵 Công tắc đèn chính\nRouter"]
        C["🔵 IR Controller · Điều hòa\nRouter"]
        D["🔵 Contactor · Đèn biển QC\nRouter"]

        %% End Devices
        E["🟢 Cảm biến ánh sáng · End Device"]
        F["🟢 Cảm biến nhiệt độ · End Device"]

        %% Coordinator <-> Routers (2 chiều, nét liền)
        A <--> B
        A <--> C
        A <--> D

        %% Coordinator -- End Devices (1 chiều, nét đứt)
        A -.-> E
        A -.-> F

        %% Mesh relay giữa các Routers (nét đứt)
        B <-.-> C
        B <-.-> D
        C <-.-> D
    end

    style A fill:#c04428,color:#fff,stroke:#8b2e18
    style B fill:#1b5ba5,color:#fff,stroke:#123f75
    style C fill:#1b5ba5,color:#fff,stroke:#123f75
    style D fill:#1b5ba5,color:#fff,stroke:#123f75
    style E fill:#1e7a56,color:#fff,stroke:#145238
    style F fill:#1e7a56,color:#fff,stroke:#145238
```

### Phân biệt Router vs End Device

| Loại | Vai trò | Nguồn điện | Thiết bị trong dự án |
|------|---------|-----------|---------------------|
| **Coordinator** | Điều phối toàn mạng | AC adapter | Gateway Tasmota |
| **Router** | Chuyển tiếp (relay) message | AC hard-wired | Công tắc, Contactor, Controller điều hòa |
| **End Device** | Gửi/nhận, không relay | Pin (solar/battery) | Cảm biến ánh sáng, cảm biến nhiệt |

---

## 4. Luồng dữ liệu chi tiết

### 4.1. Thu thập dữ liệu cảm biến

```mermaid
sequenceDiagram
    participant Sensor as Cảm biến ánh sáng
    participant GW as Gateway Tasmota
    participant MQTT as NanoMQ
    participant NR as Node-RED
    participant LVGL as LVGL HMI
    participant DB as Local DB
    
    Sensor->>GW: Zigbee report (illuminance: 850 lux)
    GW->>MQTT: Publish: zigbee2mqtt/sensor_light
    MQTT->>LVGL: Forward message (update widget)
    MQTT->>NR: Forward message
    NR->>NR: Function: Check threshold
    NR->>DB: Store time-series data
```

### 4.2. Điều khiển thiết bị

```mermaid
sequenceDiagram
    participant LVGL as LVGL HMI
    participant NR as Node-RED Dashboard
    participant MQTT as NanoMQ
    participant GW as Gateway Tasmota
    participant Device as Công tắc Zigbee
    
    LVGL->>MQTT: Publish: zigbee2mqtt/switch_main/set
    NR->>MQTT: Publish: zigbee2mqtt/switch_main/set
    MQTT->>GW: Forward
    GW->>Device: Zigbee command: ON
    Device->>GW: ACK + status: ON
    GW->>MQTT: Publish: zigbee2mqtt/switch_main
    MQTT->>LVGL: Forward status
    MQTT->>NR: Forward status
```

### 4.3. Automation flow (ví dụ: Tự động tắt đèn)

```mermaid
graph TB
    A[Cảm biến ánh sáng<br/>> 500 lux] -->|MQTT| B[Node-RED<br/>Trigger]
    B --> C{Kiểm tra điều kiện}
    C -->|Giờ làm việc?| D[Schedule Node]
    C -->|Manual override?| E[Check flag]
    D -->|OK| F[Gửi lệnh TẮT đèn]
    E -->|Auto mode| F
    F -->|MQTT| G[Gateway]
    G -->|Zigbee| H[Công tắc đèn]
    
    style B fill:#4CAF50,color:#fff
    style F fill:#FF9800,color:#fff
```

---

## 5. Giao tiếp Cloud/Backend

```mermaid
graph TB
    subgraph "Local (Quán)"
        A[Node-RED] -->|MQTT Publish| B[NanoMQ]
        B -.->|MQTT over TLS<br/>Port 8883| C[Internet]
    end
    
    subgraph "Cloud"
        C --> D[Backend API<br/>MQTT + REST]
        D --> E[Time-series DB<br/>InfluxDB/Timescale]
        D --> F[Alert Manager]
        D --> G[OTA Server]
    end
    
    style B fill:#4CAF50,color:#fff
    style D fill:#2196F3,color:#fff
```

### Dữ liệu đồng bộ lên Cloud

| Loại dữ liệu | Tần suất | Topic ví dụ |
|-------------|----------|-------------|
| **Telemetry** | 5-15 phút | `cafe/quanA/sensor/light` |
| **Device status** | Real-time | `cafe/quanA/device/switch1/status` |
| **Energy kWh** | 15-30 phút | `cafe/quanA/energy/consumption` |
| **System health** | 30 giây | `cafe/quanA/health/panel` |
| **Alerts** | Real-time | `cafe/quanA/alerts` |

---

## 6. Bảng tổng hợp Tech Stack

| Layer | Công nghệ | Chức năng |
|-------|-----------|-----------|
| **Hardware** | Luckfox Core1106, Ewelink Hub | Edge computing + Zigbee coordinator |
| **OS** | Ubuntu (ARM64) | Nền tảng chạy services |
| **Connectivity** | Zigbee 3.0, MQTT, Wi-Fi, Ethernet | Giao tiếp thiết bị & network |
| **Broker** | NanoMQ | MQTT broker local nhẹ |
| **Automation** | Node-RED | Flow-based programming |
| **UI On-device** | LVGL (C++) | Màn hình cảm ứng 4 inch |
| **UI Web** | Node-RED Dashboard | Quản lý qua browser |
| **Security** | MQTT TLS, Zigbee AES-128 | Mã hóa truyền dữ liệu |
| **Update** | OTA (HTTP + script) | Cập nhật từ xa |

---


## Lợi thế của kiến trúc này

- ✅ **Local-First:** Hoạt động 100% khi mất internet
- ✅ **Nhẹ:** Toàn bộ stack chạy tốt trên 256MB RAM
- ✅ **Mở:** Tasmota + Node-RED = dễ tùy biến, không vendor lock-in
- ✅ **Mesh:** Zigbee tự mở rộng phạm vi, tự phục hồi
- ✅ **Low-code:** Node-RED cho phép thay đổi logic không cần biên dịch
- ✅ **OTA:** Cập nhật firmware và flow từ xa cho cả chuỗi
