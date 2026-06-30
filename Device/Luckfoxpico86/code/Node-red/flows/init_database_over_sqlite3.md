# Init Database over SQLite3

## Overview

Tao database SQLite3 cho BMS (Building Management System) tren Luckfox Core1106.  
File database: `/data/bms/bms.db`

## Prerequisites

```bash
# Cai dat sqlite3 neu chua co
sudo apt-get update && sudo apt-get install -y sqlite3

# Tao thu muc data
mkdir -p /data/bms
```

## 1. Xoa Table Cu (Neu Ton Tai)

```bash
sqlite3 /data/bms/bms.db "
DROP TABLE IF EXISTS device_log;
DROP TABLE IF EXISTS device_config;
DROP TABLE IF EXISTS schedule;
DROP TABLE IF EXISTS device_metric;
"
```

## 2. Tao Schema Moi

```bash
sqlite3 /data/bms/bms.db <<'EOF'
-- Bang log su kien va trang thai thiet bi
CREATE TABLE IF NOT EXISTS device_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          TEXT    NOT NULL,
    device_id   TEXT    NOT NULL,
    device_type TEXT,
    location    TEXT,
    payload     TEXT,
    event       TEXT
);
CREATE INDEX IF NOT EXISTS idx_log_ts        ON device_log(ts);
CREATE INDEX IF NOT EXISTS idx_log_device_ts ON device_log(device_id, ts);
CREATE INDEX IF NOT EXISTS idx_log_event     ON device_log(event);

-- Bang cau hinh thiet bi
CREATE TABLE IF NOT EXISTS device_config (
    device_id     TEXT PRIMARY KEY,
    device_type   TEXT,
    location      TEXT,
    friendly_name TEXT,
    enabled       INTEGER DEFAULT 1,
    extra         TEXT
);

-- Bang lich tu dong
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
CREATE INDEX IF NOT EXISTS idx_sch_enabled ON schedule(enabled);
CREATE INDEX IF NOT EXISTS idx_sch_device  ON schedule(device_id);

-- Bang time-series metric
CREATE TABLE IF NOT EXISTS device_metric (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          TEXT    NOT NULL,
    device_id   TEXT    NOT NULL,
    device_type TEXT,
    attr_name   TEXT    NOT NULL,
    attr_value  REAL,
    attr_str    TEXT,
    attr_type   TEXT,
    raw_attr_id TEXT
);
CREATE INDEX IF NOT EXISTS idx_metric_ts ON device_metric(ts);
CREATE INDEX IF NOT EXISTS idx_metric_device_attr ON device_metric(device_id, attr_name, ts);
EOF
```

## 3. Them Device Moi Vao Table

```bash
sqlite3 /data/bms/bms.db <<'EOF'
INSERT OR IGNORE INTO device_config VALUES
-- MCB 1 pha
('0x384C', 'mcb', 'zone_A', 'MCB Tong zone A', 1,
 '{"gateway":"tasmota_6DCAA8","power_attr":"0110",
   "attrs":{
     "0110":{"key":"control","type":"bool"},
     "0170":{"key":"meas_0170","type":"number"},
     "0201":{"key":"state_0201","type":"number"},
     "0272":{"key":"rated_current","type":"number"},
     "0273":{"key":"high_voltage_cutoff","type":"number"},
     "0274":{"key":"low_voltage_cutoff","type":"number"},
     "0276":{"key":"meas_0276","type":"number"},
     "0277":{"key":"max_power","type":"number"},
     "027D":{"key":"state_027D","type":"number"},
     "0283":{"key":"temp","type":"number","scale":0.1},
     "0466":{"key":"meas_0466","type":"number"},
     "0467":{"key":"meas_0467","type":"number"},
     "0468":{"key":"meas_0468","type":"number"},
     "0469":{"key":"meas_0469","type":"number"},
     "046B":{"key":"meas_046B","type":"number"},
     "046E":{"key":"meas_046E","type":"number"},
     "0006":{"key":"raw_hex","type":"hex"}
   }
 }'),

-- AC Controller Zone C
('0xC5A9', 'ac_controller', 'zone_C', 'Dieu hoa zone C', 1,
 '{"gateway":"tasmota_6DCAA8","power_attr":"0101",
   "ac_power_attr":"0101","ac_temp_attr":"0202","ac_mode_attr":"0405",
   "ac_mode_map":{"off":0,"cool":1,"heat":2,"fan_only":3,"dry":4},
   "attrs":{
     "0101":{"key":"power","type":"bool"},
     "0202":{"key":"temperature","type":"number"},
     "0203":{"key":"ambient_temp","type":"number"},
     "0405":{"key":"fan_speed","type":"number"}
   }
 }'),

-- AC Controller Zone D
('0x336A', 'ac_controller', 'zone_D', 'Dieu hoa zone D', 1,
 '{"gateway":"tasmota_6DCAA8","power_attr":"0101",
   "ac_power_attr":"0101","ac_temp_attr":"0202","ac_mode_attr":"0405",
   "ac_mode_map":{"off":0,"cool":1,"heat":2,"fan_only":3,"dry":4},
   "attrs":{
     "0101":{"key":"power","type":"bool"},
     "0202":{"key":"temperature","type":"number"},
     "0203":{"key":"ambient_temp","type":"number"},
     "0405":{"key":"fan_speed","type":"number"}
   }
 }');
EOF
```

## 4. Verify

```bash
# Xem danh sach bang
sqlite3 /data/bms/bms.db ".tables"

# Xem schema
sqlite3 /data/bms/bms.db ".schema"

# Xem config da seed chua
sqlite3 /data/bms/bms.db "SELECT device_id, device_type, friendly_name FROM device_config"

# Xem metric table
sqlite3 /data/bms/bms.db ".schema device_metric"
```

## 5. Backup / Restore

```bash
# Backup
sqlite3 /data/bms/bms.db ".backup /data/bms/bms.db.backup.$(date +%Y%m%d_%H%M%S)"

# Restore
mv /data/bms/bms.db.backup.20260101_120000 /data/bms/bms.db
```

## Notes

- Chay tuan tu tu Section 1 -> 2 -> 3
- Section 1 xoa table cu (mat het data cu)
- Section 2 tao lai schema
- Section 3 them device moi
- Neu DB da co data quan trong, backup truoc khi xoa
- `device_metric` tu dong duoc tao nhung chua co data cho den khi Node-RED chay va nhan MQTT
