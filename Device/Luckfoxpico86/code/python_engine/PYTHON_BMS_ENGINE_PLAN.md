# Python BMS Engine - Kế hoạch thay thế Node-RED

## Tổng quan

Thay thế Node-RED bằng Python script để giải quyết vấn đề:
- Deploy phức tạp (cần import flow qua UI)
- RAM cao (~100MB)
- Khó debug và version control

**Mục tiêu:**
- Single Python file hoặc module nhỏ
- Deploy: `git pull && systemctl restart`
- RAM: ~30MB
- Giữ nguyên logic hiện tại
- Sử dụng `devices.yaml` làm source of truth

## Kiến trúc

```
bms-engine/
├── config/
│   ├── config.yaml          # MQTT, SQLite, xsolar config
│   └── rules.yaml           # Rule definitions
├── devices.yaml             # Source of truth (symlink từ lvgl_project)
├── src/
│   ├── __init__.py
│   ├── main.py              # Entry point
│   ├── mqtt_client.py       # MQTT subscribe/publish
│   ├── rule_engine.py       # Rule evaluation engine
│   ├── state_manager.py     # State storage (SQLite)
│   ├── scheduler.py         # Time-based scheduling
│   └── device_manager.py    # Load từ devices.yaml
├── requirements.txt
├── deploy.sh                # Deploy script
└── bms-engine.service       # Systemd service file
```

## Components

### 1. Device Manager (`device_manager.py`)

**Chức năng:** Load và quản lý device từ `devices.yaml`

```python
import yaml
from pathlib import Path

class DeviceManager:
    def __init__(self, devices_file: str):
        self.devices_file = Path(devices_file)
        self.devices = {}
        self.load_devices()
    
    def load_devices(self):
        """Load devices from devices.yaml"""
        with open(self.devices_file, 'r') as f:
            data = yaml.safe_load(f)
        
        for device in data['devices']:
            if not device.get('enabled', True):
                continue
            
            device_id = device['zigbee_addr']
            self.devices[device_id] = {
                'device_id': device['device_id'],
                'zigbee_addr': device_id,
                'nr_type': device['nr_type'],
                'name': device['name'],
                'group': device['group'],
                'gateway': device['gateway'],
                'attributes': self._parse_attributes(device['attributes']),
                'metadata': {
                    'ac_index': device.get('ac_index'),
                    'sign_index': device.get('sign_index'),
                    'power_index': device.get('power_index'),
                    'light_sensor_index': device.get('light_sensor_index'),
                }
            }
    
    def _parse_attributes(self, attrs: list) -> dict:
        """Parse attributes from devices.yaml format"""
        result = {}
        for attr in attrs:
            attr_id = attr['id']
            result[attr_id] = {
                'label': attr['label'],
                'type': attr['type'],
                'scale': attr.get('scale'),
                'unit': attr.get('unit'),
                'display': attr.get('display', False),
                'xsolar': attr.get('xsolar', False),
                'xsolar_key': attr.get('xsolar_key'),
                'decode': attr.get('decode'),  # For composite attributes
            }
        return result
    
    def get_device(self, zigbee_addr: str) -> dict:
        """Get device by Zigbee address"""
        return self.devices.get(zigbee_addr)
    
    def get_all_devices(self) -> dict:
        """Get all enabled devices"""
        return self.devices
    
    def get_xsolar_attributes(self, zigbee_addr: str) -> dict:
        """Get only attributes marked for xsolar"""
        device = self.get_device(zigbee_addr)
        if not device:
            return {}
        
        return {
            attr_id: attr
            for attr_id, attr in device['attributes'].items()
            if attr.get('xsolar', False)
        }
```

### 2. MQTT Client (`mqtt_client.py`)

**Chức năng:**
- Subscribe to all topics (`#`)
- Parse ZbReceived messages (Tasmota format)
- Publish commands (ZbSend, BMS topics)
- Connection management (auto-reconnect)

```python
import paho.mqtt.client as mqtt
import json
import logging

class MQTTClient:
    def __init__(self, broker: str, port: int, client_id: str):
        self.broker = broker
        self.port = port
        self.client_id = client_id
        self.client = mqtt.Client(client_id=client_id)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.message_handler = None
    
    def connect(self):
        """Connect to MQTT broker"""
        self.client.connect(self.broker, self.port, 60)
        self.client.loop_start()
    
    def disconnect(self):
        """Disconnect from MQTT broker"""
        self.client.loop_stop()
        self.client.disconnect()
    
    def subscribe(self, topic: str, qos: int = 1):
        """Subscribe to topic"""
        self.client.subscribe(topic, qos)
    
    def publish(self, topic: str, payload: str, qos: int = 1, retain: bool = False):
        """Publish message"""
        self.client.publish(topic, payload, qos=qos, retain=retain)
    
    def _on_connect(self, client, userdata, flags, rc):
        """Callback khi kết nối thành công"""
        logging.info(f"Connected to MQTT broker with result code {rc}")
        # Subscribe to all topics
        self.client.subscribe("#", qos=1)
    
    def _on_message(self, client, userdata, msg):
        """Callback khi nhận message"""
        try:
            # Try to parse as JSON
            payload = json.loads(msg.payload.decode('utf-8'))
        except:
            # If not JSON, use raw string
            payload = msg.payload.decode('utf-8')
        
        if self.message_handler:
            self.message_handler(msg.topic, payload)
    
    def set_message_handler(self, handler):
        """Set message handler callback"""
        self.message_handler = handler
```

### 3. State Manager (`state_manager.py`)

**Chức năng:**
- SQLite database cho device states
- Store device_log, device_metric
- Query latest state cho conditions
- Thread-safe operations

```python
import sqlite3
import threading
from datetime import datetime

class StateManager:
    def __init__(self, db_path: str):
        self.db_path = db_path
        self.lock = threading.Lock()
        self._init_db()
    
    def _init_db(self):
        """Initialize database schema"""
        with self.lock:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            # Device log table
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS device_log (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    ts TEXT NOT NULL,
                    device_id TEXT NOT NULL,
                    device_type TEXT,
                    location TEXT,
                    payload TEXT,
                    event TEXT
                )
            ''')
            
            # Device metric table
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS device_metric (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    ts TEXT NOT NULL,
                    device_id TEXT NOT NULL,
                    device_type TEXT,
                    attr_name TEXT NOT NULL,
                    attr_value REAL,
                    attr_str TEXT,
                    attr_type TEXT,
                    raw_attr_id TEXT
                )
            ''')
            
            conn.commit()
            conn.close()
    
    def log_event(self, device_id: str, event: str, payload: dict):
        """Log device event"""
        with self.lock:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            cursor.execute(
                'INSERT INTO device_log (ts, device_id, payload, event) VALUES (?, ?, ?, ?)',
                (datetime.now().isoformat(), device_id, json.dumps(payload), event)
            )
            conn.commit()
            conn.close()
    
    def update_metric(self, device_id: str, attr_name: str, value, attr_type: str):
        """Update device metric"""
        with self.lock:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            if attr_type == 'number':
                cursor.execute(
                    'INSERT INTO device_metric (ts, device_id, attr_name, attr_value, attr_type) VALUES (?, ?, ?, ?, ?)',
                    (datetime.now().isoformat(), device_id, attr_name, value, attr_type)
                )
            else:
                cursor.execute(
                    'INSERT INTO device_metric (ts, device_id, attr_name, attr_str, attr_type) VALUES (?, ?, ?, ?, ?)',
                    (datetime.now().isoformat(), device_id, attr_name, str(value), attr_type)
                )
            
            conn.commit()
            conn.close()
    
    def get_latest_state(self, device_id: str) -> dict:
        """Get latest state for all attributes of a device"""
        with self.lock:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            
            # Get latest value for each attribute
            cursor.execute('''
                SELECT attr_name, attr_value, attr_str, attr_type
                FROM device_metric m1
                WHERE device_id = ? AND ts = (
                    SELECT MAX(ts) FROM device_metric m2
                    WHERE m2.device_id = m1.device_id AND m2.attr_name = m1.attr_name
                )
            ''', (device_id,))
            
            state = {}
            for row in cursor.fetchall():
                attr_name, attr_value, attr_str, attr_type = row
                if attr_type == 'number':
                    state[attr_name] = attr_value
                else:
                    state[attr_name] = attr_str
            
            conn.close()
            return state
```

### 4. Rule Engine (`rule_engine.py`)

**Chức năng:**
- Load rules từ YAML
- Evaluate triggers (MQTT topic match)
- Evaluate conditions (state, numeric_state, time)
- Execute actions (mqtt_publish)

```python
import yaml
from datetime import datetime
from typing import List, Dict, Any

class RuleEngine:
    def __init__(self, rules_file: str, mqtt_client, state_manager):
        self.rules_file = rules_file
        self.mqtt_client = mqtt_client
        self.state_manager = state_manager
        self.rules = []
        self.load_rules()
    
    def load_rules(self):
        """Load rules from YAML file"""
        with open(self.rules_file, 'r') as f:
            data = yaml.safe_load(f)
        self.rules = data.get('rules', [])
    
    def evaluate_trigger(self, trigger: dict, topic: str, payload: Any) -> bool:
        """Evaluate if trigger matches"""
        trigger_type = trigger.get('type')
        
        if trigger_type == 'mqtt':
            return self._match_topic(trigger['topic'], topic)
        
        return False
    
    def evaluate_condition(self, condition: dict) -> bool:
        """Evaluate if condition is met"""
        condition_type = condition.get('type')
        
        if condition_type == 'state':
            return self._check_state_condition(condition)
        elif condition_type == 'numeric_state':
            return self._check_numeric_state_condition(condition)
        elif condition_type == 'time':
            return self._check_time_condition(condition)
        
        return False
    
    def execute_action(self, action: dict):
        """Execute action"""
        action_type = action.get('type')
        
        if action_type == 'mqtt_publish':
            self.mqtt_client.publish(
                action['topic'],
                action['payload'],
                qos=action.get('qos', 1),
                retain=action.get('retain', False)
            )
    
    def process_message(self, topic: str, payload: Any):
        """Process incoming MQTT message and evaluate rules"""
        for rule in self.rules:
            if not rule.get('enabled', True):
                continue
            
            # Check triggers
            triggered = False
            for trigger in rule.get('triggers', []):
                if self.evaluate_trigger(trigger, topic, payload):
                    triggered = True
                    break
            
            if not triggered:
                continue
            
            # Check conditions
            conditions_met = True
            for condition in rule.get('conditions', []):
                if not self.evaluate_condition(condition):
                    conditions_met = False
                    break
            
            if not conditions_met:
                continue
            
            # Execute actions
            for action in rule.get('actions', []):
                self.execute_action(action)
            
            logging.info(f"Rule executed: {rule['alias']}")
    
    def _match_topic(self, pattern: str, topic: str) -> bool:
        """Match MQTT topic with wildcard pattern"""
        import fnmatch
        return fnmatch.fnmatch(topic, pattern)
    
    def _check_state_condition(self, condition: dict) -> bool:
        """Check state condition"""
        # Get current state from state manager
        # Compare with expected value
        pass
    
    def _check_numeric_state_condition(self, condition: dict) -> bool:
        """Check numeric state condition"""
        # Get current state from state manager
        # Compare with above/below thresholds
        pass
    
    def _check_time_condition(self, condition: dict) -> bool:
        """Check time condition"""
        now = datetime.now().time()
        
        if 'after' in condition:
            after_time = datetime.strptime(condition['after'], '%H:%M:%S').time()
            if now < after_time:
                return False
        
        if 'before' in condition:
            before_time = datetime.strptime(condition['before'], '%H:%M:%S').time()
            if now > before_time:
                return False
        
        return True
```

### 5. Scheduler (`scheduler.py`)

**Chức năng:**
- Time-based rule triggers
- Periodic tasks (e.g., push to xsolar every 10 minutes)

```python
import schedule
import time
import threading

class Scheduler:
    def __init__(self):
        self.tasks = []
        self.running = False
    
    def add_periodic_task(self, interval: int, task_func):
        """Add periodic task (interval in seconds)"""
        schedule.every(interval).seconds.do(task_func)
        self.tasks.append(task_func)
    
    def start(self):
        """Start scheduler in background thread"""
        self.running = True
        thread = threading.Thread(target=self._run, daemon=True)
        thread.start()
    
    def stop(self):
        """Stop scheduler"""
        self.running = False
    
    def _run(self):
        """Run scheduler loop"""
        while self.running:
            schedule.run_pending()
            time.sleep(1)
```

### 6. Main (`main.py`)

**Chức năng:** Entry point, orchestrate all components

```python
import logging
import yaml
import signal
import sys
from pathlib import Path

from mqtt_client import MQTTClient
from state_manager import StateManager
from device_manager import DeviceManager
from rule_engine import RuleEngine
from scheduler import Scheduler

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)

class BMSEngine:
    def __init__(self, config_file: str):
        # Load config
        with open(config_file, 'r') as f:
            self.config = yaml.safe_load(f)
        
        # Initialize components
        self.device_manager = DeviceManager(self.config['devices_file'])
        self.state_manager = StateManager(self.config['database']['path'])
        self.mqtt_client = MQTTClient(
            self.config['mqtt']['broker'],
            self.config['mqtt']['port'],
            self.config['mqtt']['client_id']
        )
        self.rule_engine = RuleEngine(
            self.config['rules_file'],
            self.mqtt_client,
            self.state_manager
        )
        self.scheduler = Scheduler()
        
        # Setup MQTT message handler
        self.mqtt_client.set_message_handler(self._handle_mqtt_message)
        
        # Setup periodic tasks
        self.scheduler.add_periodic_task(
            self.config['xsolar']['push_interval'],
            self._push_to_xsolar
        )
    
    def start(self):
        """Start BMS engine"""
        logging.info("Starting BMS Engine...")
        
        # Connect to MQTT
        self.mqtt_client.connect()
        
        # Start scheduler
        self.scheduler.start()
        
        # Setup signal handlers
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
        
        # Keep main thread alive
        signal.pause()
    
    def stop(self):
        """Stop BMS engine"""
        logging.info("Stopping BMS Engine...")
        self.scheduler.stop()
        self.mqtt_client.disconnect()
    
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        self.stop()
        sys.exit(0)
    
    def _handle_mqtt_message(self, topic: str, payload: Any):
        """Handle incoming MQTT message"""
        # Parse Tasmota ZbReceived format
        if topic.startswith('tele/') and '/SENSOR' in topic:
            self._process_zigbee_message(topic, payload)
        
        # Evaluate rules
        self.rule_engine.process_message(topic, payload)
    
    def _process_zigbee_message(self, topic: str, payload: dict):
        """Process Zigbee message from Tasmota"""
        zb_received = payload.get('ZbReceived', {})
        
        for device_addr, device_data in zb_received.items():
            device = self.device_manager.get_device(device_addr)
            if not device:
                continue
            
            # Normalize and store attributes
            for attr_id, attr_config in device['attributes'].items():
                if attr_id in device_data:
                    value = self._normalize_attribute_value(
                        attr_id, device_data[attr_id], attr_config
                    )
                    self.state_manager.update_metric(
                        device_addr, attr_id, value, attr_config['type']
                    )
            
            # Log event
            self.state_manager.log_event(
                device_addr, 'state_update', device_data
            )
    
    def _normalize_attribute_value(self, attr_id: str, raw_value, attr_config: dict):
        """Normalize attribute value based on devices.yaml config"""
        # Handle composite attributes (like MCB dp6)
        if attr_config.get('decode'):
            return self._decode_composite(raw_value, attr_config['decode'])
        
        # Apply type conversion
        attr_type = attr_config['type']
        if attr_type == 'bool':
            value = bool(raw_value)
        elif attr_type == 'number':
            value = float(raw_value)
            # Apply scale if present
            if attr_config.get('scale'):
                value = value * attr_config['scale']
        elif attr_type == 'hex':
            value = str(raw_value)
        else:
            value = raw_value
        
        return value
    
    def _decode_composite(self, hex_value: str, decode_rules: list) -> dict:
        """Decode composite hex value (e.g., MCB dp6)"""
        result = {}
        for rule in decode_rules:
            slice_start, slice_end = rule['slice']
            hex_slice = hex_value[slice_start:slice_end]
            value = int(hex_slice, 16)
            
            # Apply scale
            if rule.get('scale'):
                value = value * rule['scale']
            
            result[rule['id']] = value
        
        return result
    
    def _push_to_xsolar(self):
        """Push device states to xsolar"""
        xsolar_config = self.config['xsolar']
        
        for device_addr, device in self.device_manager.get_all_devices().items():
            state = self.state_manager.get_latest_state(device_addr)
            if not state:
                continue
            
            # Build payload with only xsolar-marked attributes
            xsolar_data = {}
            for attr_id, attr_config in device['attributes'].items():
                if not attr_config.get('xsolar', False):
                    continue
                
                xsolar_key = attr_config.get('xsolar_key')
                if attr_id in state:
                    xsolar_data[xsolar_key] = state[attr_id]
            
            payload = {
                'ts': datetime.now().isoformat() + '+07:00',
                'site': 'bluCafe',
                'id': device_addr,
                'type': device['nr_type'],
                'name': device['name'],
                'location': device['group'],
                'data': xsolar_data
            }
            
            topic = f"{xsolar_config['topic_prefix']}/{device_addr}"
            self.mqtt_client.publish(topic, json.dumps(payload), qos=0)

def main():
    config_file = Path(__file__).parent / 'config' / 'config.yaml'
    engine = BMSEngine(config_file)
    engine.start()

if __name__ == '__main__':
    main()
```

## Configuration Files

### config.yaml

```yaml
mqtt:
  broker: "localhost"
  port: 1883
  client_id: "bms-engine"

xsolar:
  broker: "mqtt.xsolar.energy"
  port: 1883
  topic_prefix: "smarteos/bluCafe"
  push_interval: 600  # seconds

database:
  path: "/data/bms/bms.db"

devices_file: "../devices.yaml"
rules_file: "config/rules.yaml"
```

### rules.yaml

```yaml
rules:
  - alias: "bat den khi toi va co nguoi"
    enabled: true
    triggers:
      - type: mqtt
        topic: "zigbee/motion/state"
      - type: mqtt
        topic: "zigbee/light_sensor/lux"
    conditions:
      - type: numeric_state
        topic: "zigbee/light_sensor/lux"
        below: 50
      - type: state
        topic: "zigbee/motion/state"
        equals: "on"
      - type: time
        after: "18:00:00"
        before: "23:00:00"
    actions:
      - type: mqtt_publish
        topic: "zigbee/switch/cmd"
        payload: "ON"
        qos: 1
```

## Deployment

### requirements.txt

```
paho-mqtt==1.6.1
PyYAML==6.0.1
schedule==1.2.0
```

### deploy.sh

```bash
#!/bin/bash
set -e

echo "Deploying BMS Engine..."

# Pull latest code
cd /home/pico/bms-engine
git pull

# Install dependencies
pip3 install -r requirements.txt

# Restart service
sudo systemctl restart bms-engine

echo "Deployment complete!"
```

### bms-engine.service

```ini
[Unit]
Description=BMS Engine
After=network.target

[Service]
Type=simple
User=pico
WorkingDirectory=/home/pico/bms-engine
ExecStart=/usr/bin/python3 /home/pico/bms-engine/src/main.py
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

## Migration Steps

1. **Setup project structure**
   ```bash
   mkdir -p /home/pico/bms-engine/{config,src}
   cd /home/pico/bms-engine
   git init
   ```

2. **Copy devices.yaml**
   ```bash
   ln -s /home/pico/HLCFEOS/Device/Luckfoxpico86/code/lvgl_project/devices.yaml devices.yaml
   ```

3. **Create all Python files** (theo plan trên)

4. **Install dependencies**
   ```bash
   pip3 install -r requirements.txt
   ```

5. **Setup systemd service**
   ```bash
   sudo cp bms-engine.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable bms-engine
   sudo systemctl start bms-engine
   ```

6. **Test với real MQTT data**
   ```bash
   # Monitor logs
   sudo journalctl -u bms-engine -f
   
   # Test MQTT publish
   mosquitto_pub -h localhost -p 1883 -t "tele/tasmota_6DCAA8/SENSOR" \
     -m '{"ZbReceived":{"0xC5A9":{"EF00/0101":1,"EF00/0202":24}}}'
   ```

7. **Stop Node-RED**
   ```bash
   sudo systemctl stop node-red
   sudo systemctl disable node-red
   ```

## Timeline

- **Phase 1**: Core infrastructure (MQTT, State, Config) - 2-3 hours
- **Phase 2**: Rule Engine - 2-3 hours
- **Phase 3**: Scheduler & Device Manager - 1-2 hours
- **Phase 4**: Integration & Testing - 1-2 hours
- **Phase 5**: Deployment & Documentation - 1 hour

**Total**: 7-11 hours

## Benefits

| Aspect | Node-RED | Python BMS Engine |
|--------|----------|-------------------|
| Deploy | Import flow qua UI | `git pull && systemctl restart` |
| RAM | ~100MB | ~30MB |
| Debug | Debug tab | `journalctl -f` |
| Version control | JSON files | Python files |
| Maintenance | UI-based | Code-based |
| Performance | Medium | High |

## Notes

- Giữ nguyên logic hiện tại từ Node-RED flows
- Sử dụng `devices.yaml` làm source of truth
- Hỗ trợ composite attribute decode (MCB dp6)
- Rule engine linh hoạt với triggers, conditions, actions
- Dễ mở rộng thêm device types và rules mới
