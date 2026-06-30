# EF00 Attribute Mapping & Device Config Reference

## `device_config.extra` format (DATA-DRIVEN)

Moi device chi can 1 row trong `device_config`. Toan bo flow L1-L5 doc config tu `extra`.

```json
{
  "gateway":          "tasmota-6DCAA8-2728-eth",
  "power_attr":       "0110",
  "ac_power_attr":    "0101",
  "ac_temp_attr":     "0202",
  "ac_mode_attr":     "0405",
  "ac_mode_map":      {"off":0,"cool":1,"heat":2,"fan_only":3,"dry":4},
  "attrs": {
    "<EF00_attr_id>": {
      "key":   "property_name",
      "type":  "bool|number|hex|string",
      "scale": 0.01,
      "desc":  "description"
    }
  },
  "custom_actions": {
    "DIM_50":  {"0101":1, "0200":50},
    "DIM_100": {"0101":1, "0200":100}
  }
}
```

### Cac field chinh

| Field | Su dung | Bat buoc |
|-------|---------|----------|
| `gateway` | Hostname Tasmota gateway | **YES** |
| `power_attr` | EF00 attr ID cho ON/OFF | **YES** |
| `attrs` | Mapping EF00/XXXX -> state property | **YES** |
| `ac_power_attr` | AC: attr power (neu khac power_attr) | AC only |
| `ac_temp_attr` | AC: attr nhiet do | AC only |
| `ac_mode_attr` | AC: attr che do | AC only |
| `ac_mode_map` | AC: mapping mode name -> value | AC only |
| `custom_actions` | Custom action -> Write object (mo rong) | Optional |

### Custom actions (mo rong khong can sua code)

Them action moi bang cach them vao `custom_actions`:

```json
{
  "custom_actions": {
    "DIM_50":    {"0110": 1, "0200": 50},
    "DIM_100":   {"0110": 1, "0200": 100},
    "NIGHT_MODE": {"0110": 1, "0200": 20, "0202": 26},
    "ALL_OFF":   {"0110": 0, "0200": 0, "0202": 0}
  }
}
```

Sau do tao schedule voi:
- `action` = `DIM_50` (ten action trong custom_actions)
- `action_params` = `{}` (hoac `{"var":"value"}` de substitute `{{var}}`)

Template variable: `{{var_name}}` trong custom_actions value se duoc thay bang `action_params.var_name`.

Vi du:
```json
"custom_actions": {
  "SET_TEMP": {"0101": 1, "0202": "{{deg}}"}
}
```
Schedule: `action=SET_TEMP, action_params={"deg":24}` → Write `{"0101":1, "0202":"24"}`

---

## Vi du device_config cho tung loai

### 1. MCB (0x384C) — chi can power_attr + attrs

```sql
INSERT OR REPLACE INTO device_config VALUES (
  '0x384C', 'mcb', 'zone_A', 'MCB Tong zone A', 1,
  '{"gateway":"tasmota-6DCAA8-2728-eth","power_attr":"0110","attrs":{
    "0110":{"key":"power","type":"bool","desc":"Relay"},
    "0272":{"key":"meas_0272","type":"number"},
    "0273":{"key":"meas_0273","type":"number"},
    "0276":{"key":"meas_0276","type":"number"},
    "0277":{"key":"meas_0277","type":"number"},
    "046E":{"key":"meas_046E","type":"number"},
    "0006":{"key":"raw_hex","type":"hex"}
  }}'
);
```

→ L4 tu dong biet dung `power_attr=0110` cho POWER_ON/POWER_OFF.

### 2. AC Controller (0x8150) — them ac_*_attr

```sql
INSERT OR REPLACE INTO device_config VALUES (
  '0x8150', 'ac_controller', 'zone_B', 'Dieu hoa zone B', 1,
  '{"gateway":"tasmota-6DCAA8-2728-eth",
    "power_attr":"0101",
    "ac_power_attr":"0101",
    "ac_temp_attr":"0202",
    "ac_mode_attr":"0405",
    "ac_mode_map":{"off":0,"cool":1,"heat":2,"fan_only":3,"dry":4},
    "attrs":{
      "0101":{"key":"power","type":"bool","desc":"AC Power"},
      "0202":{"key":"temperature","type":"number","desc":"Setpoint C"},
      "0405":{"key":"mode","type":"number","desc":"Mode"}
    }}'
);
```

→ L4 tu dong biet dung `ac_power_attr`, `ac_temp_attr`, `ac_mode_attr` cho AC_SET/AC_OFF.

### 3. Cong tac Zigbee (them moi)

```sql
INSERT OR REPLACE INTO device_config VALUES (
  '0x5678', 'switch', 'zone_A', 'Cong tac cua chinh', 1,
  '{"gateway":"tasmota-6DCAA8-2728-eth",
    "power_attr":"0110",
    "attrs":{
      "0110":{"key":"power","type":"bool","desc":"Switch state"}
    }}'
);
```

→ Xong! Khong can sua code. L2 tu dong parse, L4 tu dong biet gui POWER_ON/OFF.

### 4. Thiet bi moi hoan toan (custom)

```sql
INSERT OR REPLACE INTO device_config VALUES (
  '0xABCD', 'dimmer', 'zone_C', 'Dimmer LED', 1,
  '{"gateway":"tasmota-6DCAA8-2728-eth",
    "power_attr":"0101",
    "attrs":{
      "0101":{"key":"power","type":"bool"},
      "0200":{"key":"brightness","type":"number"}
    },
    "custom_actions":{
      "DIM_25":  {"0101":1,"0200":25},
      "DIM_50":  {"0101":1,"0200":50},
      "DIM_75":  {"0101":1,"0200":75},
      "DIM_100": {"0101":1,"0200":100}
    }}'
);
```

Tao schedule:
- `action=DIM_50` → tu dong gui `Write{"0101":1,"0200":50}`
- `action=POWER_OFF` → tu dong gui `Write{"0101":0}`

---

## Them device qua Dashboard

Vao tab **Config** → dien form:

| Field | Vi du |
|-------|-------|
| Device ID | `0x5678` |
| Device Type | `switch` |
| Gateway hostname | `tasmota-6DCAA8-2728-eth` |
| Power attr ID | `0110` |
| Attrs mapping (JSON) | `{"0110":{"key":"power","type":"bool"}}` |
| Custom actions (JSON) | (bo trong hoac `{"DIM_50":{"0110":1,"0200":50}}`) |

Sau khi submit, an **Reload Config** → L1, L2, L4 tu dong nhan device moi.

---

## Command format (Tasmota ZbSend)

```bash
# Bat/Tat
mosquitto_pub -h localhost -p 1883 \
  -t "cmnd/tasmota-6DCAA8-2728-eth/ZbSend" \
  -m '{"device":"0x384C","send":{"Cluster":"0xEF00","Write":{"0110":1}}}'

# AC
mosquitto_pub -h localhost -p 1883 \
  -t "cmnd/tasmota-6DCAA8-2728-eth/ZbSend" \
  -m '{"device":"0x8150","send":{"Cluster":"0xEF00","Write":{"0101":1,"0202":24,"0405":1}}}'

# Doc attr
mosquitto_pub -h localhost -p 1883 \
  -t "cmnd/tasmota-6DCAA8-2728-eth/ZbSend" \
  -m '{"device":"0x8150","send":{"Cluster":"0xEF00","Read":["0101","0202","0405"]}}'
```

---

## Flow du lieu tong quan (data-driven)

```
tele/<gw>/SENSOR                      L5 Config UI
    │                                   │ INSERT device_config
    ▼                                   ▼
[L1 Generic Parser]              [Reload configs → global]
    │ device_config{} lookup             │
    │ device_id = short_addr            │
    ▼                                   ▼
[L2 Generic Normalize] ◄──── device_config.extra.attrs
    │                                   │
    ▼                                   ▼
[L3 Storage]                      [L4 Schedule Engine]
    │                                   │ doc extra.power_attr, ac_*_attr,
    ▼                                   │ custom_actions → build ZbSend
  SQLite                                ▼
                                  MQTT out ZbSend
```
