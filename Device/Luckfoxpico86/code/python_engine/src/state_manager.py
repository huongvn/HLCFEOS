"""
State Manager - SQLite database for device states and metrics
"""

import sqlite3
import threading
import json
import logging
from datetime import datetime
from typing import Dict, Any, Optional

logger = logging.getLogger(__name__)


class StateManager:
    """Manage device states and metrics in SQLite database"""
    
    def __init__(self, db_path: str):
        """
        Initialize StateManager
        
        Args:
            db_path: Path to SQLite database file
        """
        self.db_path = db_path
        self.lock = threading.Lock()
        self._init_db()
        logger.info(f"State manager initialized with database: {db_path}")
    
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
            
            # Device metric table (time-series)
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
            
            # Device config table
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS device_config (
                    device_id TEXT PRIMARY KEY,
                    device_type TEXT,
                    location TEXT,
                    friendly_name TEXT,
                    enabled INTEGER DEFAULT 1,
                    extra TEXT
                )
            ''')
            
            # Create indexes for better query performance
            cursor.execute('CREATE INDEX IF NOT EXISTS idx_log_ts ON device_log(ts)')
            cursor.execute('CREATE INDEX IF NOT EXISTS idx_log_device ON device_log(device_id, ts)')
            cursor.execute('CREATE INDEX IF NOT EXISTS idx_metric_ts ON device_metric(ts)')
            cursor.execute('CREATE INDEX IF NOT EXISTS idx_metric_device_attr ON device_metric(device_id, attr_name, ts)')
            
            conn.commit()
            conn.close()
            logger.info("Database schema initialized")
    
    def log_event(self, device_id: str, event: str, payload: Dict, 
                  device_type: str = None, location: str = None):
        """
        Log device event
        
        Args:
            device_id: Device Zigbee address
            event: Event type (e.g., "state_update", "command")
            payload: Event payload dictionary
            device_type: Device type (optional)
            location: Device location (optional)
        """
        with self.lock:
            try:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                cursor.execute(
                    '''INSERT INTO device_log 
                       (ts, device_id, device_type, location, payload, event) 
                       VALUES (?, ?, ?, ?, ?, ?)''',
                    (datetime.now().isoformat(), device_id, device_type, 
                     location, json.dumps(payload), event)
                )
                conn.commit()
                conn.close()
                logger.debug(f"Logged event for {device_id}: {event}")
            except Exception as e:
                logger.error(f"Failed to log event for {device_id}: {e}")
    
    def update_metric(self, device_id: str, attr_name: str, value: Any, 
                      attr_type: str, device_type: str = None, raw_attr_id: str = None):
        """
        Update device metric
        
        Args:
            device_id: Device Zigbee address
            attr_name: Attribute name
            value: Attribute value
            attr_type: Attribute type ("number", "bool", "string")
            device_type: Device type (optional)
            raw_attr_id: Raw attribute ID (optional)
        """
        with self.lock:
            try:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                
                if attr_type == 'number':
                    cursor.execute(
                        '''INSERT INTO device_metric 
                           (ts, device_id, device_type, attr_name, attr_value, attr_type, raw_attr_id) 
                           VALUES (?, ?, ?, ?, ?, ?, ?)''',
                        (datetime.now().isoformat(), device_id, device_type, 
                         attr_name, float(value), attr_type, raw_attr_id)
                    )
                else:
                    cursor.execute(
                        '''INSERT INTO device_metric 
                           (ts, device_id, device_type, attr_name, attr_str, attr_type, raw_attr_id) 
                           VALUES (?, ?, ?, ?, ?, ?, ?)''',
                        (datetime.now().isoformat(), device_id, device_type, 
                         attr_name, str(value), attr_type, raw_attr_id)
                    )
                
                conn.commit()
                conn.close()
                logger.debug(f"Updated metric for {device_id}.{attr_name} = {value}")
            except Exception as e:
                logger.error(f"Failed to update metric for {device_id}.{attr_name}: {e}")
    
    def get_latest_state(self, device_id: str) -> Dict[str, Any]:
        """
        Get latest state for all attributes of a device
        
        Args:
            device_id: Device Zigbee address
            
        Returns:
            Dictionary mapping attribute name to latest value
        """
        with self.lock:
            try:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                
                # Get latest value for each attribute (bare GROUP BY + MAX)
                cursor.execute('''
                    SELECT attr_name, attr_value, attr_str, attr_type, MAX(ts)
                    FROM device_metric
                    WHERE device_id = ?
                    GROUP BY attr_name
                ''', (device_id,))
                
                state = {}
                for row in cursor.fetchall():
                    attr_name, attr_value, attr_str, attr_type, _ = row
                    if attr_type == 'number':
                        state[attr_name] = attr_value
                    else:
                        state[attr_name] = attr_str
                
                conn.close()
                return state
            except Exception as e:
                logger.error(f"Failed to get latest state for {device_id}: {e}")
                return {}
    
    def get_latest_states(self) -> Dict[str, Dict[str, Any]]:
        """
        Get latest state for all devices
        
        Returns:
            Dictionary mapping device_id to state dictionary
        """
        with self.lock:
            try:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                
                # Get all unique device IDs
                cursor.execute('SELECT DISTINCT device_id FROM device_metric')
                device_ids = [row[0] for row in cursor.fetchall()]
                
                states = {}
                for device_id in device_ids:
                    states[device_id] = self.get_latest_state(device_id)
                
                conn.close()
                return states
            except Exception as e:
                logger.error(f"Failed to get latest states: {e}")
                return {}
    
    def update_device_config(self, device_id: str, device_type: str, 
                             location: str, friendly_name: str, extra: Dict):
        """
        Update device configuration
        
        Args:
            device_id: Device Zigbee address
            device_type: Device type
            location: Device location
            friendly_name: Human-readable device name
            extra: Additional device metadata (JSON)
        """
        with self.lock:
            try:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                cursor.execute(
                    '''INSERT OR REPLACE INTO device_config 
                       (device_id, device_type, location, friendly_name, enabled, extra) 
                       VALUES (?, ?, ?, ?, 1, ?)''',
                    (device_id, device_type, location, friendly_name, json.dumps(extra))
                )
                conn.commit()
                conn.close()
                logger.info(f"Updated device config for {device_id}")
            except Exception as e:
                logger.error(f"Failed to update device config for {device_id}: {e}")
    
    def get_device_config(self, device_id: str) -> Optional[Dict]:
        """
        Get device configuration
        
        Args:
            device_id: Device Zigbee address
            
        Returns:
            Device configuration dictionary or None
        """
        with self.lock:
            try:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                cursor.execute(
                    'SELECT device_type, location, friendly_name, extra FROM device_config WHERE device_id = ?',
                    (device_id,)
                )
                row = cursor.fetchone()
                conn.close()
                
                if row:
                    device_type, location, friendly_name, extra = row
                    return {
                        'device_type': device_type,
                        'location': location,
                        'friendly_name': friendly_name,
                        'extra': json.loads(extra) if extra else {}
                    }
                return None
            except Exception as e:
                logger.error(f"Failed to get device config for {device_id}: {e}")
                return None
