# Node-RED BMS — Kiến trúc Flow Tổng quan
**Dự án:** Highlands Coffee — Smart Panel BMS  
**Nền tảng:** Luckfox Core1106 (RV1106G3, 256MB) · NanoMQ :1883 · Node-RED :1880  
**Phiên bản:** 1.0 · Ngày cập nhật: 2026-06-27

---

## Mục lục

1. [Tổng quan hệ thống](#1-tổng-quan-hệ-thống)
2. [Kiến trúc flow 5 layer](#2-kiến-trúc-flow-5-layer)
3. [L1 — MQTT Ingress + Topic Router](#3-l1--mqtt-ingress--topic-router)
4. [L2 — Device Abstraction](#4-l2--device-abstraction)
5. [L3 — Storage + State Cache](#5-l3--storage--state-cache)
6. [L4 — Schedule Engine](#6-l4--schedule-engine)
7. [L5 — Dashboard + Config UI](#7-l5--dashboard--config-ui)
8. [LVGL HMI Bridge](#8-lvgl-hmi-bridge)
9. [MQTT Topic Convention](#9-mqtt-topic-convention)
10. [SQLite Schema đầy đủ](#10-sqlite-schema-đầy-đủ)
11. [Cài đặt và triển khai](#11-cài-đặt-và-triển-khai)
12. [Checklist vận hành](#12-checklist-vận-hành)

---

## 1. Tổng quan hệ thống

### Sơ đồ kiến trúc vật lý

```
┌─────────────────────────────────────────────────────────────────┐
│  ZIGBEE MESH NETWORK                                            │
│                                                                 │
│  [Light Sensor]──┐                                              │
│  [Switch x N]────┤                                              │
│  [Contactor x N]─┼──Zigbee 3.0──► [Gateway Tasmota ZB-GW03]   │
│  [IR AC Ctrl]────┘                        │ MQTT pub/sub        │
└───────────────────────────────────────────┼─────────────────────┘
                                            ▼
┌─────────────────────────────────────────────────────────────────┐
│  SMART PANEL — Luckfox Core1106 (RV1106G3, 256MB RAM)          │
│                                                                 │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐   │
│  │  NanoMQ      │   │  Node-RED    │   │  LVGL HMI        │   │
│  │  :1883       │◄──►  :1880       │──►│  Màn hình 4 inch │   │
│  │  MQTT Broker │   │  5-layer     │   │  720×720         │   │
│  └──────────────┘   │  flow engine │   └──────────────────┘   │
│                     └──────┬───────┘                           │
│                            │ SQLite (local)                     │
│                     ┌──────▼───────┐                           │
│                     │  /data/bms/  │                           │
│                     │  bms.db      │                           │
│                     └──────────────┘                           │
└─────────────────────────────────────────────────────────────────┘
                            │ MQTT over TLS (tuỳ chọn)
                            ▼
              [Cloud Backend / Dashboard tập trung]
```

### Devices trong hệ thống

| Device | Type | MQTT prefix | Giao thức |
|--------|------|-------------|-----------|
| Cảm biến ánh sáng (solar pin) | `light_sensor` | `zigbee2mqtt/` | Zigbee 3.0 |
| Công tắc Zigbee (router node) | `switch` | `zigbee2mqtt/` | Zigbee 3.0 |
| Contactor Zigbee (router node) | `contactor` | `zigbee2mqtt/` | Zigbee 3.0 |
| IR Controller (điều hòa) | `ir_ac` | `cmnd/` `tele/` | Tasmota IR |
| Gateway Tasmota | `gateway` | `tele/` `stat/` | WiFi/MQTT |

---

## 2. Kiến trúc flow 5 layer

```
╔══════════════════════════════════════════════════════════════╗
║  NGUỒN MQTT — Zigbee devices qua NanoMQ broker :1883        ║
║  tele/+/SENSOR · stat/+/RESULT · zigbee2mqtt/+ · tele/+/LWT ║
╚══════════════════╤═══════════════════════════════════════════╝
                   │ subscribe wildcard
                   ▼
╔══════════════════════════════════════════════════════════════╗
║  L1 — MQTT INGRESS + TOPIC ROUTER                           ║
║  mqtt in → function (parse+detect) → switch (route) +       ║
║  LWT handler (online/offline state)                         ║
╚══════════════════╤═══════════════════════════════════════════╝
                   │ routed by device_type
                   ▼
╔══════════════════════════════════════════════════════════════╗
║  L2 — DEVICE ABSTRACTION                                    ║
║  4 function nodes (1 per type) → chuẩn hoá → device{} obj  ║
║  join node gom tất cả event vào 1 stream                    ║
╚══════════════════╤═══════════════════════════════════════════╝
                   │ device{} object stream
                   ▼
╔══════════════════════════════════════════════════════════════╗
║  L3 — STORAGE + STATE CACHE                                 ║
║  global context (live state) · device_log · device_config   ║
║  schedule table — tất cả SQLite local                       ║
╚══════════════════╤═══════════════════════════════════════════╝
                   │
          ┌────────┴────────┐
          ▼                 ▼
╔═════════════════╗  ╔══════════════════════════════════════════╗
║  L4 — SCHEDULE  ║  ║  L5 — DASHBOARD + CONFIG UI             ║
║  ENGINE         ║  ║  Device panel · Log viewer               ║
║  cron tick 60s  ║  ║  Schedule CRUD · AC config              ║
║  → mqtt out cmd ║  ║  Manual override                        ║
╚════════╤════════╝  ╚══════════════════════════════════════════╝
         │ MQTT command
         ▼
╔══════════════════════════════════════════════════════════════╗
║  LVGL HMI BRIDGE                                            ║
║  function (build payload) → mqtt out → hmi/status           ║
║  LVGL C app subscribe → hiển thị màn hình 4 inch           ║
╚══════════════════════════════════════════════════════════════╝
```

### Màu node theo category (Node-RED)

| Màu | Category | Nodes |
|-----|----------|-------|
| 🔵 Xanh dương | MQTT source | `mqtt in`, `mqtt out` |
| 🟢 Xanh lá | Logic / flow | `function`, `switch`, `split`, `join` |
| 🟡 Cam/vàng | Device abstraction | function normalize nodes |
| 🟣 Tím | Storage | `sqlite` nodes |
| ⚪ Xám | Neutral | `inject`, `debug`, `link` |

---

## 3. L1 — MQTT Ingress + Topic Router

### Mục đích

Nhận toàn bộ MQTT message từ NanoMQ bằng wildcard, phân tích topic để xác định `device_id` và `device_type`, route sang L2 handler tương ứng. Đồng thời xử lý LWT (Last Will Testament) để cập nhật trạng thái online/offline.

### Nodes

```
[mqtt in : #]
     │
     ▼
[function : Topic parser + type detect]  ──►  [switch : route by device_type]
                                                    │ output 0 → light_sensor
                                                    │ output 1 → switch
                                                    │ output 2 → contactor
                                                    │ output 3 → ir_ac
                                                    │ output 4 → default/gateway
                                          ──►  [function : LWT handler]
```

### Cấu hình node `mqtt in`

| Thuộc tính | Giá trị |
|------------|---------|
| Server | `localhost:1883` |
| Topic | `#` (wildcard toàn bộ) |
| QoS | 1 |
| Output | auto-detect JSON |

> **Lưu ý:** Đặt thêm 1 `mqtt in` riêng subscribe `tele/+/LWT` để bắt online/offline tách biệt với data flow.

### Code: Function node "Topic parser + type detect"

```javascript
// ── L1: Topic parser + type detect ──────────────────────────
const topic = msg.topic;
const parts = topic.split('/');

// Detect source và extract device_id
if (topic.startsWith('zigbee2mqtt/')) {
    msg.device_id  = parts[1];   // vd: "light_sensor_01"
    msg.source     = 'zigbee2mqtt';
    msg.device_type = _detectZigbeeType(msg.payload);

} else if (topic.startsWith('tele/')) {
    msg.device_id  = parts[1];   // vd: "highlands_B_ir_01"
    msg.source     = 'tasmota_tele';
    msg.device_type = parts[2] === 'LWT' ? 'lwt' : 'tasmota';

} else if (topic.startsWith('stat/')) {
    msg.device_id  = parts[1];
    msg.source     = 'tasmota_stat';
    msg.device_type = 'tasmota_stat';

} else {
    msg.device_type = 'unknown';
}

// ── Helper: detect Zigbee device type từ payload ────────────
function _detectZigbeeType(p) {
    if (p == null) return 'unknown';
    if (p.illuminance !== undefined || p.lux !== undefined)   return 'light_sensor';
    if (p.current    !== undefined)                           return 'contactor';
    if (p.current_heating_setpoint !== undefined)             return 'ir_ac';
    if (p.state      !== undefined && p.contact === undefined) return 'switch';
    if (p.action     !== undefined)                           return 'button';
    return 'unknown';
}

// Route index cho switch node
const routeMap = {
    light_sensor: 0,
    switch:       1,
    contactor:    2,
    ir_ac:        3,
};
msg._route = routeMap[msg.device_type] ?? 4;

return msg;
```

### Code: Switch node "route by device_type"

Cấu hình switch node với 5 outputs:

| Output | Điều kiện | Đến |
|--------|-----------|-----|
| 1 | `msg.device_type == "light_sensor"` | L2: normalize light sensor |
| 2 | `msg.device_type == "switch"` | L2: normalize switch |
| 3 | `msg.device_type == "contactor"` | L2: normalize contactor |
| 4 | `msg.device_type == "ir_ac"` | L2: normalize IR AC |
| 5 | otherwise | debug / drop |

### Code: Function node "LWT handler"

```javascript
// ── L1: LWT handler ─────────────────────────────────────────
// topic: tele/<device_id>/LWT
// payload: "Online" hoặc "Offline"
const parts   = msg.topic.split('/');
const devId   = parts[1];
const isOnline = (msg.payload === 'Online');

// Cập nhật global context
const devices = global.get('devices') || {};
if (!devices[devId]) devices[devId] = {};
devices[devId].online    = isOnline;
devices[devId].lwt_ts    = new Date().toISOString();
global.set('devices', devices);

// Ghi vào SQLite log
msg.topic   = 'INSERT INTO device_log (ts, device_id, device_type, location, payload, event) VALUES (?,?,?,?,?,?)';
msg.payload = [
    new Date().toISOString(),
    devId,
    devices[devId].type || 'unknown',
    devices[devId].location || '',
    JSON.stringify({ online: isOnline }),
    isOnline ? 'online' : 'offline'
];
return msg;
```

---

## 4. L2 — Device Abstraction

### Mục đích

Chuẩn hoá payload thô từ mỗi loại device thành **device object** thống nhất. Mọi layer phía sau chỉ làm việc với object này, không cần biết device là Zigbee hay Tasmota, payload format như thế nào.

### Cấu trúc device object chuẩn

```javascript
{
    id:        "highlands_A_light_01",   // device_id duy nhất
    type:      "light_sensor",           // light_sensor | switch | contactor | ir_ac
    location:  "zone_A",                 // đọc từ device_config table
    ts:        "2026-06-27T07:00:00Z",  // ISO timestamp
    state: {
        online:   true,
        // ... fields riêng của từng type (xem bên dưới)
    },
    raw: { /* payload gốc từ MQTT */ }
}
```

### Node flow

```
[switch output 0] ──► [function: normalize light_sensor] ──┐
[switch output 1] ──► [function: normalize switch]         ├──► [join: merge all] ──► L3
[switch output 2] ──► [function: normalize contactor]      │
[switch output 3] ──► [function: normalize ir_ac]          ┘
```

**Join node config:** Mode = `manual`, gộp tất cả messages thành stream đơn (không đợi tất cả — set `Send message` = `after each input`).

### Code: Normalize light sensor

```javascript
// ── L2: Normalize light_sensor ──────────────────────────────
const p = msg.payload;

// Đọc metadata từ device_config (cache vào global)
const cfg = (global.get('device_configs') || {})[msg.device_id] || {};

msg.device = {
    id:       msg.device_id,
    type:     'light_sensor',
    location: cfg.location      || 'unknown',
    ts:       new Date().toISOString(),
    state: {
        online:      true,
        lux:         p.illuminance ?? p.lux ?? null,
        battery:     p.battery     ?? null,
        battery_low: (p.battery ?? 100) < 20,
        link_quality: p.linkquality ?? null,
    },
    raw: p,
};
return msg;
```

### Code: Normalize switch

```javascript
// ── L2: Normalize switch ────────────────────────────────────
const p   = msg.payload;
const cfg = (global.get('device_configs') || {})[msg.device_id] || {};

msg.device = {
    id:       msg.device_id,
    type:     'switch',
    location: cfg.location || 'unknown',
    ts:       new Date().toISOString(),
    state: {
        online:       true,
        power:        p.state ?? null,          // "ON" | "OFF"
        link_quality: p.linkquality ?? null,
    },
    raw: p,
};
return msg;
```

### Code: Normalize contactor

```javascript
// ── L2: Normalize contactor ─────────────────────────────────
const p   = msg.payload;
const cfg = (global.get('device_configs') || {})[msg.device_id] || {};

msg.device = {
    id:       msg.device_id,
    type:     'contactor',
    location: cfg.location || 'unknown',
    ts:       new Date().toISOString(),
    state: {
        online:       true,
        power:        p.state   ?? null,
        current:      p.current ?? null,          // Ampere
        voltage:      p.voltage ?? null,          // Volt
        energy:       p.energy  ?? null,          // kWh
        link_quality: p.linkquality ?? null,
    },
    raw: p,
};
return msg;
```

### Code: Normalize IR / AC controller

```javascript
// ── L2: Normalize ir_ac ─────────────────────────────────────
const p   = msg.payload;
const cfg = (global.get('device_configs') || {})[msg.device_id] || {};

msg.device = {
    id:       msg.device_id,
    type:     'ir_ac',
    location: cfg.location || 'zone_AC',
    ts:       new Date().toISOString(),
    state: {
        online:      true,
        power:       p.state                     ?? 'OFF',
        temperature: p.current_heating_setpoint  ?? null,  // setpoint °C
        room_temp:   p.local_temperature         ?? null,  // nhiệt độ phòng
        mode:        p.system_mode               ?? null,  // cool|heat|fan_only|off
        fan:         p.fan_mode                  ?? null,  // auto|low|medium|high
        link_quality: p.linkquality              ?? null,
    },
    raw: p,
};
return msg;
```

### Code: Function node sau join — "Update global state"

Đặt sau join node để cập nhật live state vào global context:

```javascript
// ── L2→L3: Update global devices cache ──────────────────────
const d       = msg.device;
const devices = global.get('devices') || {};

// Chỉ ghi log khi state thực sự thay đổi
const prev    = devices[d.id];
const changed = !prev
    || JSON.stringify(prev.state) !== JSON.stringify(d.state);

devices[d.id] = d;
global.set('devices', devices);

msg._state_changed = changed;
msg._prev_state    = prev ? prev.state : null;
return msg;
```

---

## 5. L3 — Storage + State Cache

### Mục đích

Lưu trữ dữ liệu vào SQLite local (nhẹ, không cần server). Có 3 bảng chính:

| Bảng | Mục đích |
|------|----------|
| `device_log` | Log mọi event + state change của device |
| `device_config` | Metadata, cấu hình từng device |
| `schedule` | Lịch bật/tắt thiết bị |

### Cài đặt SQLite node

```bash
# Trong thư mục Node-RED
npm install node-red-node-sqlite
# Sau đó restart Node-RED
```

Trong Node-RED UI: kéo node **SQLite** vào flow, cấu hình:
- **Database:** `/data/bms/bms.db`
- **Mode:** `Run a query against the database`

### SQLite Schema

```sql
-- ── Khởi tạo database lần đầu ──────────────────────────────

-- Bảng 1: Log sự kiện
CREATE TABLE IF NOT EXISTS device_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          TEXT    NOT NULL,
    device_id   TEXT    NOT NULL,
    device_type TEXT,
    location    TEXT,
    payload     TEXT,              -- JSON string của state
    event       TEXT               -- state_change | online | offline | command
);
CREATE INDEX IF NOT EXISTS idx_log_ts     ON device_log(ts);
CREATE INDEX IF NOT EXISTS idx_log_device ON device_log(device_id, ts);

-- Bảng 2: Metadata thiết bị
CREATE TABLE IF NOT EXISTS device_config (
    device_id     TEXT PRIMARY KEY,
    device_type   TEXT,
    location      TEXT,
    friendly_name TEXT,
    enabled       INTEGER DEFAULT 1,
    extra         TEXT              -- JSON cho thông số riêng
);

-- Bảng 3: Schedule
CREATE TABLE IF NOT EXISTS schedule (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT    NOT NULL,
    device_id     TEXT    NOT NULL,
    device_type   TEXT,
    action        TEXT    NOT NULL,    -- POWER_ON|POWER_OFF|AC_SET|AC_OFF|SCENE
    action_params TEXT,               -- JSON: {"temp":24,"mode":"cool","fan":"Auto"}
    sch_minute    TEXT    DEFAULT '*',
    sch_hour      TEXT    DEFAULT '*',
    sch_dow       TEXT    DEFAULT '*', -- 0=CN, 1=T2 ... 6=T7
    sch_dom       TEXT    DEFAULT '*',
    sch_month     TEXT    DEFAULT '*',
    enabled       INTEGER DEFAULT 1,
    one_shot      INTEGER DEFAULT 0,   -- 1 = chạy 1 lần rồi tự disable
    last_run      TEXT,
    created_at    TEXT    DEFAULT (datetime('now')),
    updated_at    TEXT
);
CREATE INDEX IF NOT EXISTS idx_sch_enabled ON schedule(enabled);
```

### Khởi tạo database

Tạo 1 flow "DB Init" chạy 1 lần khi deploy:

```javascript
// inject (once) → function (tạo SQL) → sqlite

// Function node:
const sql = `
CREATE TABLE IF NOT EXISTS device_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL, device_id TEXT NOT NULL,
    device_type TEXT, location TEXT,
    payload TEXT, event TEXT
);
CREATE INDEX IF NOT EXISTS idx_log_ts ON device_log(ts);
CREATE INDEX IF NOT EXISTS idx_log_device ON device_log(device_id, ts);

CREATE TABLE IF NOT EXISTS device_config (
    device_id TEXT PRIMARY KEY, device_type TEXT,
    location TEXT, friendly_name TEXT,
    enabled INTEGER DEFAULT 1, extra TEXT
);

CREATE TABLE IF NOT EXISTS schedule (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL, device_id TEXT NOT NULL,
    device_type TEXT, action TEXT NOT NULL,
    action_params TEXT, sch_minute TEXT DEFAULT '*',
    sch_hour TEXT DEFAULT '*', sch_dow TEXT DEFAULT '*',
    sch_dom TEXT DEFAULT '*', sch_month TEXT DEFAULT '*',
    enabled INTEGER DEFAULT 1, one_shot INTEGER DEFAULT 0,
    last_run TEXT, created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT
);
`;

msg.topic   = sql;
msg.payload = [];
return msg;
```

### Code: Function node "Insert device log"

Kết nối sau "Update global state", chỉ ghi khi `_state_changed = true`:

```javascript
// ── L3: Insert device_log ───────────────────────────────────
if (!msg._state_changed) return null; // bỏ qua nếu không đổi

const d = msg.device;
msg.topic = `
    INSERT INTO device_log (ts, device_id, device_type, location, payload, event)
    VALUES (?, ?, ?, ?, ?, ?)
`;
msg.payload = [
    d.ts,
    d.id,
    d.type,
    d.location,
    JSON.stringify(d.state),
    msg.event_type ?? 'state_change'
];
return msg;  // → sqlite node
```

### Code: Load device_config vào global (startup)

```javascript
// inject (once) → sqlite SELECT → function:
// SELECT * FROM device_config WHERE enabled = 1

const configs = {};
(msg.payload || []).forEach(row => {
    configs[row.device_id] = {
        type:          row.device_type,
        location:      row.location,
        friendly_name: row.friendly_name,
        extra:         row.extra ? JSON.parse(row.extra) : {},
    };
});
global.set('device_configs', configs);
node.status({ fill:'green', shape:'dot', text: `${Object.keys(configs).length} devices loaded` });
return null;
```

---

## 6. L4 — Schedule Engine

### Mục đích

Engine lập lịch tự động bật/tắt và cài thông số thiết bị. Hỗ trợ:
- Cron expression (giờ, phút, ngày trong tuần)
- Action: `POWER_ON`, `POWER_OFF`, `AC_SET`, `AC_OFF`, `SCENE`
- One-shot: chạy 1 lần rồi tự disable
- Lux-based rule: tự động bật/tắt đèn theo ánh sáng

### Flow tổng quan

```
[inject: once on start] → [sqlite SELECT schedules] → [function: global.set schedules]
                                                              ▲
                                                              │ reload sau CRUD
[inject: repeat 60s] ──► [function: match cron]             │ (link call)
                               │ triggered[]                  │
                               ▼                              │
                          [split] → [switch: by action]  ←───┘
                                         │ POWER
                                         ├──► [function: build POWER cmd] → [mqtt out]
                                         │ AC_SET
                                         ├──► [function: build IRhvac cmd] → [mqtt out]
                                         │ SCENE
                                         └──► [function: build scene cmds] → [mqtt out]
                                                    │ (tất cả action)
                                                    └──► [sqlite: UPDATE last_run]
                                                    └──► [sqlite: INSERT device_log]
```

### Code: Startup — Load schedules

```javascript
// inject (once on start) → sqlite → function:
// SELECT * FROM schedule WHERE enabled = 1

global.set('schedules', msg.payload || []);
node.status({
    fill:  'green',
    shape: 'dot',
    text:  `${(msg.payload || []).length} schedules loaded`
});
return null;
```

### Code: Function "Match cron vs now()" — core tick engine

```javascript
// ── L4: Schedule tick engine (chạy mỗi 60s) ─────────────────
const now       = new Date();
const schedules = global.get('schedules') ?? [];

// ── Hàm khớp 1 field cron ───────────────────────────────────
function fieldMatch(expr, val) {
    if (!expr || expr === '*') return true;

    // Step: */15
    if (expr.startsWith('*/')) {
        return val % Number(expr.slice(2)) === 0;
    }
    // Range: 1-5
    if (expr.includes('-')) {
        const [lo, hi] = expr.split('-').map(Number);
        return val >= lo && val <= hi;
    }
    // List: 0,6
    if (expr.includes(',')) {
        return expr.split(',').map(Number).includes(val);
    }
    return Number(expr) === val;
}

// ── Kiểm tra toàn bộ cron expression ────────────────────────
function cronMatches(s, d) {
    return fieldMatch(s.sch_minute, d.getMinutes())
        && fieldMatch(s.sch_hour,   d.getHours())
        && fieldMatch(s.sch_dow,    d.getDay())      // 0 = CN
        && fieldMatch(s.sch_dom,    d.getDate())
        && fieldMatch(s.sch_month,  d.getMonth() + 1);
}

// ── Lọc schedule khớp giờ hiện tại ──────────────────────────
const triggered = schedules.filter(s => s.enabled && cronMatches(s, now));

if (triggered.length === 0) return null;

// Chuẩn bị payload cho split node
msg.payload = triggered.map(s => ({
    schedule_id:   s.id,
    schedule_name: s.name,
    device_id:     s.device_id,
    device_type:   s.device_type,
    action:        s.action,
    action_params: s.action_params ? JSON.parse(s.action_params) : {},
    one_shot:      s.one_shot,
    triggered_at:  now.toISOString(),
}));

return msg; // → split node
```

### Code: Action handlers

**Handler POWER ON/OFF (switch, contactor):**

```javascript
// ── L4: Build POWER command ──────────────────────────────────
const s = msg.payload;

msg.topic   = `cmnd/${s.device_id}/POWER`;
msg.payload = s.action === 'POWER_ON' ? 'ON' : 'OFF';
msg._schedule_id = s.schedule_id;
msg._one_shot    = s.one_shot;
msg._log = {
    device_id:  s.device_id,
    event:      'schedule_command',
    detail:     `${s.schedule_name} → ${msg.payload}`,
};
return msg;
```

**Handler AC_SET / AC_OFF:**

```javascript
// ── L4: Build IRhvac command ─────────────────────────────────
const s = msg.payload;
const p = s.action_params ?? {};

msg.topic = `cmnd/${s.device_id}/IRhvac`;

if (s.action === 'AC_OFF') {
    msg.payload = JSON.stringify({ Power: 'Off' });
} else {
    msg.payload = JSON.stringify({
        Vendor:   p.vendor  ?? 'MITSUBISHI',
        Power:    'On',
        Mode:     p.mode    ?? 'cool',     // cool | heat | dry | fan_only
        Celsius:  'On',
        Temp:     p.temp    ?? 25,
        FanSpeed: p.fan     ?? 'Auto',     // Auto | Low | Medium | High
        SwingV:   'Auto',
        SwingH:   'Off',
    });
}

msg._schedule_id = s.schedule_id;
msg._one_shot    = s.one_shot;
msg._log = {
    device_id: s.device_id,
    event:     'schedule_command',
    detail:    JSON.stringify(s.action_params),
};
return msg;
```

**Handler SCENE (multi-device):**

```javascript
// ── L4: Build scene commands ─────────────────────────────────
const s      = msg.payload;
const scenes = global.get('scenes') ?? {};
const scene  = scenes[s.action_params.scene_id];

if (!scene) {
    node.warn(`Scene not found: ${s.action_params.scene_id}`);
    return null;
}

// Trả ra mảng message cho từng device trong scene
return [scene.commands.map(cmd => ({
    topic:           `cmnd/${cmd.device_id}/POWER`,
    payload:         cmd.state,
    _schedule_id:    s.schedule_id,
    _one_shot:       s.one_shot,
    _log: {
        device_id: cmd.device_id,
        event:     'scene_command',
        detail:    s.action_params.scene_id,
    },
}))];
```

### Code: Sau mqtt out — cập nhật last_run và one_shot

```javascript
// ── L4: Update last_run + handle one_shot ────────────────────
const now   = new Date().toISOString();
const id    = msg._schedule_id;

if (msg._one_shot) {
    msg.topic   = 'UPDATE schedule SET enabled=0, last_run=?, updated_at=? WHERE id=?';
    msg.payload = [now, now, id];
} else {
    msg.topic   = 'UPDATE schedule SET last_run=?, updated_at=? WHERE id=?';
    msg.payload = [now, now, id];
}

// Reload lại schedules vào global
const updated = (global.get('schedules') ?? []).map(s =>
    s.id === id
        ? { ...s, last_run: now, enabled: msg._one_shot ? 0 : s.enabled }
        : s
);
global.set('schedules', updated);

return msg; // → sqlite node
```

### Code: Lux-based automation rule

Flow phụ: cảm biến ánh sáng trigger → kiểm tra rule → bật/tắt đèn:

```javascript
// ── L4: Lux rule — auto light control ───────────────────────
// Kết nối từ L2 output của light_sensor
const d        = msg.device;
const lux      = d.state.lux;
const configs  = global.get('device_configs') ?? {};

// Lấy ngưỡng từ config của sensor này
const cfg       = configs[d.id] ?? {};
const extra     = cfg.extra ?? {};
const threshold = extra.lux_threshold ?? 300;  // default 300 lux
const switchId  = extra.linked_switch;         // device_id của switch liên kết

if (!switchId || lux === null) return null;

const shouldBeOn = lux < threshold;
const current    = (global.get('devices') ?? {})[switchId]?.state?.power;

// Chỉ gửi lệnh nếu cần thay đổi
if ((shouldBeOn && current === 'ON') || (!shouldBeOn && current === 'OFF')) {
    return null;  // đã đúng trạng thái
}

msg.topic   = `cmnd/${switchId}/POWER`;
msg.payload = shouldBeOn ? 'ON' : 'OFF';
msg._log    = {
    device_id: switchId,
    event:     'auto_lux_control',
    detail:    `lux=${lux} threshold=${threshold}`,
};
return msg;  // → mqtt out
```

### Ví dụ data schedule cho Highlands Coffee

```sql
-- Lịch vận hành hàng ngày (T2-T7)
INSERT INTO schedule (name, device_id, device_type, action, sch_minute, sch_hour, sch_dow) VALUES
('Bật đèn quán mở cửa',     'highlands_A_switch_01',   'switch',   'POWER_ON',  '0', '7',  '1-6'),
('Tắt đèn quán đóng cửa',   'highlands_A_switch_01',   'switch',   'POWER_OFF', '0', '22', '1-6'),
('Bật AC trước giờ mở',     'highlands_B_ir_01',       'ir_ac',    'AC_SET',    '30','6',  '1-6'),
('Tắt AC sau giờ đóng',     'highlands_B_ir_01',       'ir_ac',    'AC_OFF',    '30','22', '1-6'),
('Tắt contactor chủ nhật',  'highlands_A_contactor_01','contactor','POWER_OFF', '0', '23', '0');

-- AC_SET cần thêm action_params
UPDATE schedule
SET action_params = '{"temp":24,"mode":"cool","fan":"Auto","vendor":"MITSUBISHI"}'
WHERE name = 'Bật AC trước giờ mở';
```

---

## 7. L5 — Dashboard + Config UI

### Cài đặt Dashboard 2.0

```bash
# Trong Node-RED, vào Manage Palette
# Tìm và cài: @flowfuse/node-red-dashboard
# Hoặc qua terminal:
npm install @flowfuse/node-red-dashboard
```

Truy cập dashboard: `http://<ip>:1880/dashboard`

### Cấu trúc tabs

| Tab | Mục đích | Nodes chính |
|-----|----------|-------------|
| **Devices** | Xem live state, manual override | `ui-table`, `ui-switch`, `ui-button` |
| **Log** | Xem lịch sử sự kiện | `ui-table`, `ui-dropdown` |
| **Schedules** | Thêm/sửa/xóa lịch | `ui-form`, `ui-table`, `ui-switch` |
| **AC Control** | Cài thông số điều hòa | `ui-slider`, `ui-dropdown`, `ui-button` |

### Flow: Device panel

```
[inject: 5s] → [function: build device cards] → [ui-table: device status]
                                              → [ui-text: online count]

[ui-switch: manual toggle] → [function: build cmd] → [mqtt out]
                                                   → [sqlite: log command]
```

```javascript
// Function: build device cards cho ui-table
const devices = global.get('devices') ?? {};
msg.payload = Object.values(devices).map(d => ({
    'ID':        d.id,
    'Tên':       (global.get('device_configs') ?? {})[d.id]?.friendly_name ?? d.id,
    'Vị trí':   d.location,
    'Loại':     d.type,
    'Trạng thái': d.state?.power ?? (d.state?.lux != null ? `${d.state.lux} lux` : '—'),
    'Online':    d.state?.online ? '✓' : '✗',
    'Cập nhật': d.ts ? d.ts.slice(11, 19) : '—',
}));
return msg;
```

### Flow: Log viewer

```
[ui-dropdown: chọn device] → [function: build SQL] → [sqlite SELECT] → [ui-table]
[ui-button: refresh]       ─┘
```

```javascript
// Function: build log query
const devId = msg.payload || '%';
msg.topic   = `
    SELECT ts, device_id, event, payload
    FROM device_log
    WHERE device_id LIKE ?
    ORDER BY ts DESC
    LIMIT 100
`;
msg.payload = [devId === 'all' ? '%' : devId];
return msg;
```

### Flow: Schedule CRUD

```
[ui-form: schedule form] → [function: validate + build SQL]
                               → [sqlite INSERT/UPDATE/DELETE]
                               → [link call: reload schedules]
                               → [ui-table: refresh list]
```

**Form fields cho ui-form:**

```javascript
// Cấu hình ui-form (Dashboard 2.0 format)
[
    { type: 'text',   label: 'Tên lịch',           key: 'name'    },
    { type: 'select', label: 'Thiết bị',            key: 'device_id',
      options: '<load from device_config>' },
    { type: 'select', label: 'Hành động',           key: 'action',
      options: ['POWER_ON','POWER_OFF','AC_SET','AC_OFF','SCENE'] },
    { type: 'number', label: 'Giờ (0-23)',           key: 'hour'   },
    { type: 'number', label: 'Phút (0-59)',           key: 'minute' },
    { type: 'select', label: 'Ngày trong tuần',     key: 'dow',
      options: [
          { value: '*',   label: 'Hàng ngày'  },
          { value: '1-5', label: 'Thứ 2-6'    },
          { value: '0,6', label: 'Cuối tuần'  },
          { value: '1',   label: 'Thứ 2'      },
          { value: '2',   label: 'Thứ 3'      },
          { value: '3',   label: 'Thứ 4'      },
          { value: '4',   label: 'Thứ 5'      },
          { value: '5',   label: 'Thứ 6'      },
          { value: '6',   label: 'Thứ 7'      },
          { value: '0',   label: 'Chủ nhật'   },
      ]
    },
    { type: 'switch', label: 'Chạy 1 lần rồi tắt', key: 'one_shot' },
    { type: 'switch', label: 'Kích hoạt ngay',      key: 'enabled'  },
]
```

**Function: validate + build SQL:**

```javascript
// ── L5: Schedule CRUD handler ────────────────────────────────
const f   = msg.payload;
const now = new Date().toISOString();

// Validate
if (!f.name || !f.device_id || !f.action) {
    node.warn('Thiếu thông tin schedule');
    msg.payload = { error: 'Vui lòng điền đầy đủ thông tin' };
    return [null, msg];  // output 2 = error
}

if (f.id) {
    // UPDATE
    msg.topic = `
        UPDATE schedule SET
            name=?, device_id=?, action=?, action_params=?,
            sch_minute=?, sch_hour=?, sch_dow=?,
            enabled=?, one_shot=?, updated_at=?
        WHERE id=?
    `;
    msg.payload = [
        f.name, f.device_id, f.action,
        f.action_params ? JSON.stringify(f.action_params) : null,
        String(f.minute ?? '*'),
        String(f.hour   ?? '*'),
        f.dow    ?? '*',
        f.enabled  ? 1 : 0,
        f.one_shot ? 1 : 0,
        now, f.id
    ];
} else {
    // INSERT
    msg.topic = `
        INSERT INTO schedule
            (name, device_id, action, action_params,
             sch_minute, sch_hour, sch_dow, enabled, one_shot, created_at)
        VALUES (?,?,?,?,?,?,?,?,?,?)
    `;
    msg.payload = [
        f.name, f.device_id, f.action,
        f.action_params ? JSON.stringify(f.action_params) : null,
        String(f.minute ?? '*'),
        String(f.hour   ?? '*'),
        f.dow    ?? '*',
        f.enabled  ? 1 : 0,
        f.one_shot ? 1 : 0,
        now
    ];
}
return [msg, null];  // output 1 = success → sqlite
```

### Flow: AC config panel

```
[ui-slider: nhiệt độ 16-30°C]    ─┐
[ui-dropdown: mode cool/heat/fan]  ├──► [function: build IRhvac] → [mqtt out]
[ui-dropdown: fan auto/low/hi]    ─┤
[ui-button: Gửi lệnh]             ┘
[ui-button: Tắt AC]               ──► [mqtt out: cmnd/device/IRhvac {"Power":"Off"}]
```

---

## 8. LVGL HMI Bridge

### Mục đích

Định kỳ tổng hợp trạng thái hệ thống và publish lên topic `hmi/status` để LVGL C app subscribe và hiển thị lên màn hình 4 inch (720×720) gắn trực tiếp trên Smart Panel.

### Flow

```
[inject: 10s] → [function: build HMI payload] → [mqtt out: hmi/status]
                                              → [mqtt out: hmi/alerts] (nếu có alert)
```

### Code: Build HMI payload

```javascript
// ── HMI Bridge: build compact status payload ─────────────────
const devices = global.get('devices') ?? {};

// Đếm online/offline
const allDevices = Object.values(devices);
const online     = allDevices.filter(d => d.state?.online).length;

// Lấy thông số nhanh
const lightSensor = allDevices.find(d => d.type === 'light_sensor');
const ac          = allDevices.find(d => d.type === 'ir_ac');

// Alerts
const alerts = [];
allDevices.forEach(d => {
    if (!d.state?.online) alerts.push(`${d.id}: OFFLINE`);
    if (d.state?.battery_low) alerts.push(`${d.id}: PIN YẾU`);
});

msg.topic   = 'hmi/status';
msg.payload = JSON.stringify({
    ts:          new Date().toISOString(),
    devices_on:  online,
    devices_tot: allDevices.length,
    lux:         lightSensor?.state?.lux   ?? null,
    ac_temp:     ac?.state?.temperature    ?? null,
    ac_power:    ac?.state?.power          ?? 'OFF',
    alerts:      alerts.slice(0, 3),       // tối đa 3 alert
});
return msg;
```

### LVGL C app — subscribe HMI status

LVGL app trên cùng RV1106 subscribe topic này qua NanoMQ local:

```c
// mqtt_handler.c (phía LVGL)
#define HMI_TOPIC "hmi/status"

void on_hmi_message(const char* payload) {
    cJSON* root = cJSON_Parse(payload);
    int lux      = cJSON_GetObjectItem(root, "lux")->valueint;
    int ac_temp  = cJSON_GetObjectItem(root, "ac_temp")->valueint;
    // ... cập nhật LVGL labels
    lv_label_set_text_fmt(label_lux,  "Ánh sáng: %d lux", lux);
    lv_label_set_text_fmt(label_temp, "Nhiệt độ AC: %d°C", ac_temp);
    cJSON_Delete(root);
}
```

---

## 9. MQTT Topic Convention

### Cấu trúc topic chuẩn cho hệ thống

```
Prefix/<zone>_<type>_<id>/<suffix>

Ví dụ:
  highlands_A_switch_01       → công tắc zone A, số 01
  highlands_B_ir_01           → IR AC controller zone B, số 01
  highlands_A_light_01        → cảm biến ánh sáng zone A, số 01
  highlands_A_contactor_01    → contactor zone A, số 01
```

### Bảng topic đầy đủ

| Direction | Topic | Payload | Mô tả |
|-----------|-------|---------|-------|
| Sub | `zigbee2mqtt/+` | JSON | Zigbee2MQTT telemetry |
| Sub | `tele/+/SENSOR` | JSON | Tasmota sensor data |
| Sub | `stat/+/RESULT` | JSON | Tasmota command result |
| Sub | `tele/+/LWT` | `Online`/`Offline` | Device heartbeat |
| Pub | `cmnd/<id>/POWER` | `ON` / `OFF` | Bật/tắt switch, contactor |
| Pub | `cmnd/<id>/IRhvac` | JSON | Lệnh điều hòa |
| Pub | `cmnd/<id>/Dimmer` | `1..100` | Độ sáng (nếu có dimmer) |
| Pub | `hmi/status` | JSON | Summary cho LVGL screen |
| Pub | `hmi/alerts` | JSON | Alert notifications |

### IRhvac payload format (Tasmota)

```json
{
    "Vendor":   "MITSUBISHI",
    "Power":    "On",
    "Mode":     "cool",
    "Celsius":  "On",
    "Temp":     24,
    "FanSpeed": "Auto",
    "SwingV":   "Auto",
    "SwingH":   "Off"
}
```

---

## 10. SQLite Schema đầy đủ

```sql
-- ══════════════════════════════════════════════════════════════
--  BMS DATABASE SCHEMA v1.0
--  File: /data/bms/bms.db
-- ══════════════════════════════════════════════════════════════

-- ── Bảng 1: Log sự kiện ──────────────────────────────────────
CREATE TABLE IF NOT EXISTS device_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          TEXT    NOT NULL,
    device_id   TEXT    NOT NULL,
    device_type TEXT,
    location    TEXT,
    payload     TEXT,              -- JSON string của state
    event       TEXT               -- state_change|online|offline|command|auto_lux
);
CREATE INDEX IF NOT EXISTS idx_log_ts        ON device_log(ts);
CREATE INDEX IF NOT EXISTS idx_log_device_ts ON device_log(device_id, ts);
CREATE INDEX IF NOT EXISTS idx_log_event     ON device_log(event);

-- ── Bảng 2: Metadata thiết bị ────────────────────────────────
CREATE TABLE IF NOT EXISTS device_config (
    device_id     TEXT PRIMARY KEY,
    device_type   TEXT,
    location      TEXT,
    friendly_name TEXT,
    enabled       INTEGER DEFAULT 1,
    extra         TEXT               -- JSON: lux_threshold, linked_switch, vendor, ...
);

-- ── Bảng 3: Schedule ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS schedule (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT    NOT NULL,
    device_id     TEXT    NOT NULL,
    device_type   TEXT,
    action        TEXT    NOT NULL,
    action_params TEXT,
    sch_minute    TEXT    DEFAULT '*',
    sch_hour      TEXT    DEFAULT '*',
    sch_dow       TEXT    DEFAULT '*',
    sch_dom       TEXT    DEFAULT '*',
    sch_month     TEXT    DEFAULT '*',
    enabled       INTEGER DEFAULT 1,
    one_shot      INTEGER DEFAULT 0,
    last_run      TEXT,
    created_at    TEXT    DEFAULT (datetime('now')),
    updated_at    TEXT
);
CREATE INDEX IF NOT EXISTS idx_sch_enabled   ON schedule(enabled);
CREATE INDEX IF NOT EXISTS idx_sch_device    ON schedule(device_id);

-- ── Seed data: device_config ──────────────────────────────────
INSERT OR IGNORE INTO device_config VALUES
('highlands_A_light_01',    'light_sensor', 'zone_A', 'Cảm biến cửa chính', 1,
 '{"lux_threshold":300,"linked_switch":"highlands_A_switch_01"}'),
('highlands_A_switch_01',   'switch',       'zone_A', 'Đèn cửa chính',      1, '{}'),
('highlands_A_contactor_01','contactor',    'zone_A', 'Tổng điện zone A',   1, '{}'),
('highlands_B_ir_01',       'ir_ac',        'zone_B', 'Điều hòa zone B',    1,
 '{"vendor":"MITSUBISHI","default_temp":24,"default_mode":"cool"}');

-- ── Seed data: schedule ───────────────────────────────────────
INSERT OR IGNORE INTO schedule
    (name, device_id, device_type, action, action_params, sch_minute, sch_hour, sch_dow) VALUES
('Bật đèn mở cửa',       'highlands_A_switch_01',    'switch',   'POWER_ON',  NULL,
 '0', '7', '1-6'),
('Tắt đèn đóng cửa',     'highlands_A_switch_01',    'switch',   'POWER_OFF', NULL,
 '0', '22', '1-6'),
('Bật AC trước mở cửa',  'highlands_B_ir_01',        'ir_ac',    'AC_SET',
 '{"temp":24,"mode":"cool","fan":"Auto","vendor":"MITSUBISHI"}',
 '30', '6', '1-6'),
('Tắt AC sau đóng cửa',  'highlands_B_ir_01',        'ir_ac',    'AC_OFF',    NULL,
 '30', '22', '1-6'),
('Tắt điện cuối tuần',   'highlands_A_contactor_01', 'contactor','POWER_OFF', NULL,
 '0', '23', '0');
```

---

## 11. Cài đặt và triển khai

### Yêu cầu

| Package | Phiên bản | Lý do |
|---------|-----------|-------|
| Node-RED | >= 3.1 | runtime |
| node-red-node-sqlite | >= 1.0 | SQLite storage |
| @flowfuse/node-red-dashboard | >= 1.0 | Dashboard 2.0 UI |
| NanoMQ | >= 0.18 | MQTT broker |

### Cài đặt trên Luckfox Core1106

```bash
# 1. Cài Node-RED (nếu chưa có)
npm install -g --unsafe-perm node-red

# 2. Cài các node cần thiết
cd ~/.node-red
npm install node-red-node-sqlite
npm install @flowfuse/node-red-dashboard

# 3. Tạo thư mục data
mkdir -p /data/bms

# 4. Khởi động Node-RED
node-red --userDir /data/node-red &

# 5. Kiểm tra NanoMQ đang chạy
nanomq status
# hoặc
mosquitto_pub -h localhost -p 1883 -t test -m hello
```

### Thứ tự import flow

1. Import flow **"DB Init"** → Deploy → chạy 1 lần để tạo schema
2. Import flow **"Startup"** (load configs + schedules vào global)
3. Import flow **"L1 MQTT Ingress"**
4. Import flow **"L2 Device Abstraction"**
5. Import flow **"L3 Storage"**
6. Import flow **"L4 Schedule Engine"**
7. Import flow **"L5 Dashboard"**
8. Import flow **"HMI Bridge"**

### Cấu trúc thư mục project

```
/data/
├── bms/
│   └── bms.db                  ← SQLite database
└── node-red/
    ├── flows.json              ← tất cả flows
    ├── settings.js
    └── package.json
```

### Cấu hình NanoMQ (`/etc/nanomq.conf`)

```hocon
listeners.tcp {
    bind = "0.0.0.0:1883"
}

log {
    to = file
    level = warn
    dir = "/var/log/nanomq"
}

# Giữ kết nối tối đa
mqtt.max_connections = 64
mqtt.keepalive       = 60
```

---

## 12. Checklist vận hành

### Startup checklist

- [ ] NanoMQ đang listen trên `:1883`
- [ ] Node-RED đang listen trên `:1880`
- [ ] `bms.db` tồn tại và có đủ 3 bảng
- [ ] Global `device_configs` đã load (kiểm tra debug node)
- [ ] Global `schedules` đã load (kiểm tra debug node)
- [ ] Gateway Tasmota hiện `Online` trên LWT

### Kiểm tra flow hoạt động

```bash
# Test publish message giả lập light sensor
mosquitto_pub -h localhost -p 1883 \
  -t "zigbee2mqtt/highlands_A_light_01" \
  -m '{"illuminance":250,"battery":85,"linkquality":72}'

# Xem log SQLite
sqlite3 /data/bms/bms.db \
  "SELECT ts, device_id, event, payload FROM device_log ORDER BY id DESC LIMIT 5;"

# Test schedule: trigger ngay 1 schedule thủ công
# → Vào Node-RED, inject msg vào function "match cron" với payload giả
```

### Monitoring

| Điều cần kiểm tra | Tần suất | Cách kiểm tra |
|-------------------|----------|---------------|
| NanoMQ còn sống | 5 phút | `tele/+/LWT` messages |
| SQLite size | Hàng ngày | `du -sh /data/bms/bms.db` |
| Schedule last_run | Hàng ngày | Dashboard → Schedules tab |
| Device online count | Real-time | Dashboard → Devices tab |

### Xoá log cũ (SQLite maintenance)

Cài 1 flow chạy hàng ngày lúc 2:00 AM:

```javascript
// inject (cron: 0 2 * * *) → sqlite
msg.topic   = 'DELETE FROM device_log WHERE ts < datetime("now", "-30 days")';
msg.payload = [];
return msg;
```

---

## Tài liệu tham khảo

- [Node-RED Documentation](https://nodered.org/docs/)
- [node-red-node-sqlite](https://flows.nodered.org/node/node-red-node-sqlite)
- [FlowFuse Dashboard 2.0](https://dashboard.flowfuse.com/)
- [NanoMQ Documentation](https://nanomq.io/docs/)
- [Tasmota IR Commands](https://tasmota.github.io/docs/IR-Remote/)
- [Zigbee2MQTT](https://www.zigbee2mqtt.io/)

---

*Tài liệu này được tạo cho dự án BMS Highlands Coffee trên nền tảng Luckfox Core1106.*  
*Mọi thay đổi cấu hình cần cập nhật đồng thời vào `device_config` table và flow Node-RED.*
