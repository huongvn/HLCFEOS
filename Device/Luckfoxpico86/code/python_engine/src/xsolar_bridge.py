"""
Xsolar Bridge - Giao tiếp với xsolar cloud qua MQTT

Chức năng:
1. Periodic push device states lên xsolar mỗi 10 phút
2. Event-driven push khi device state thay đổi
3. Nhận lệnh điều khiển từ xsolar (smarteos/bluCafe/+/set)
4. Convert lệnh xsolar thành ZbSend và gửi về local broker
"""

import logging
import json
from datetime import datetime
from typing import Dict, Any, Optional

logger = logging.getLogger(__name__)


class XsolarBridge:
    """Bridge giữa BMS Engine và xsolar cloud"""
    
    def __init__(self, mqtt_local, mqtt_xsolar, state_manager, device_manager):
        """
        Khởi tạo Xsolar Bridge
        
        Args:
            mqtt_local: MQTT client cho local broker (NanoMQ)
            mqtt_xsolar: MQTT client cho xsolar broker
            state_manager: State manager instance
            device_manager: Device manager instance
        """
        self.mqtt_local = mqtt_local
        self.mqtt_xsolar = mqtt_xsolar
        self.state_manager = state_manager
        self.device_manager = device_manager
        
        # Track last push time per device để tránh spam
        self._last_push: Dict[str, float] = {}
        self._min_push_interval = 5  # Tối thiểu 5 giây giữa 2 lần push cùng device
        
        logger.info("Xsolar Bridge initialized")
    
    def start(self):
        """
        Start Xsolar Bridge - subscribe nhận lệnh từ xsolar
        """
        # Subscribe remote commands từ xsolar
        # Format: smarteos/bluCafe/{device_id}/set
        # Payload: {"power": "ON", "temperature": 24, "fan": 2}
        self.mqtt_xsolar.subscribe("smarteos/bluCafe/+/set", qos=1)
        
        # Set message handler cho xsolar client
        self.mqtt_xsolar.set_message_handler(self._handle_xsolar_message)
        
        logger.info("Xsolar Bridge started - subscribed to smarteos/bluCafe/+/set")
    
    def _handle_xsolar_message(self, topic: str, payload: Any):
        """
        Xử lý message từ xsolar broker
        
        Args:
            topic: MQTT topic
            payload: Message payload
        """
        try:
            parts = topic.split('/')
            
            # Handle remote commands: smarteos/bluCafe/{device_id}/set
            if len(parts) == 4 and parts[0] == 'smarteos' and parts[1] == 'bluCafe' and parts[3] == 'set':
                device_id = parts[2]
                self._handle_remote_command(device_id, payload)
            
        except Exception as e:
            logger.error(f"Error handling xsolar message: {e}", exc_info=True)
    
    def _handle_remote_command(self, device_id: str, payload: Any):
        """
        Xử lý lệnh điều khiển từ xsolar
        
        Args:
            device_id: Zigbee address (VD: "0xC5A9")
            payload: Command payload (dict hoặc JSON string)
        """
        # Parse payload
        if isinstance(payload, str):
            try:
                cmd = json.loads(payload)
            except json.JSONDecodeError:
                logger.warning(f"Invalid JSON from xsolar: {payload}")
                return
        else:
            cmd = payload
        
        # Lookup device
        device = self.device_manager.get_device(device_id)
        if not device:
            logger.warning(f"Remote command: device not found: {device_id}")
            return
        
        if device['nr_type'] != 'ac_controller':
            logger.warning(f"Remote command: only AC supported, got {device['nr_type']}")
            return
        
        gateway = device['gateway']
        attributes = device['attributes']
        
        # Build ZbSend writes
        writes = {}
        
        # Power
        if 'power' in cmd:
            power_attr_id = self._find_attr_id_by_xsolar_key(attributes, 'power')
            if power_attr_id:
                value = 1 if cmd['power'] in ('ON', True, 1) else 0
                writes[f"EF00/{power_attr_id}"] = value
        
        # Temperature
        if 'temperature' in cmd:
            temp_attr_id = self._find_attr_id_by_xsolar_key(attributes, 'temperature')
            if temp_attr_id:
                writes[f"EF00/{temp_attr_id}"] = int(cmd['temperature'])
        
        # Fan speed
        if 'fan' in cmd:
            fan_attr_id = self._find_attr_id_by_xsolar_key(attributes, 'fan')
            if fan_attr_id:
                writes[f"EF00/{fan_attr_id}"] = int(cmd['fan'])
        
        if not writes:
            logger.warning(f"Remote command: no valid commands for {device_id}")
            return
        
        # Publish ZbSend về local broker
        zb_send_payload = {
            "Device": device_id,
            "Write": writes
        }
        
        topic = f"cmnd/{gateway}/ZbSend"
        self.mqtt_local.publish(topic, json.dumps(zb_send_payload), qos=1)
        
        # Log command
        self.state_manager.log_event(
            device_id,
            'remote_command',
            {'source': 'xsolar', 'command': cmd, 'writes': writes},
            device['nr_type'],
            device['group']
        )
        
        logger.info(f"Remote command from xsolar: {device_id} -> {cmd}")
    
    def push_device_state(self, device_addr: str):
        """
        Push device state lên xsolar (event-driven)
        
        Args:
            device_addr: Zigbee address
        """
        device = self.device_manager.get_device(device_addr)
        if not device:
            return
        
        # Rate limiting - tránh spam
        now = datetime.now().timestamp()
        last_push = self._last_push.get(device_addr, 0)
        if now - last_push < self._min_push_interval:
            return
        
        state = self.state_manager.get_latest_state(device_addr)
        if not state:
            return
        
        # Build payload với chỉ xsolar-marked attributes
        xsolar_data = self._build_xsolar_data(device, state)
        if not xsolar_data:
            return
        
        payload = {
            'ts': datetime.now().isoformat() + '+07:00',
            'site': 'bluCafe',
            'id': device_addr,
            'type': device['nr_type'],
            'name': device['name'],
            'location': device['group'],
            'data': xsolar_data
        }
        
        topic = f"smarteos/bluCafe/{device_addr}"
        
        try:
            self.mqtt_xsolar.publish(topic, json.dumps(payload), qos=0)
            self._last_push[device_addr] = now
            logger.info(f"Event push: {device_addr} -> xsolar ({len(xsolar_data)} attrs)")
        except Exception as e:
            logger.error(f"Failed to push {device_addr} to xsolar: {e}")
    
    def push_all_states(self):
        """
        Push tất cả device states lên xsolar (periodic - gọi bởi scheduler)
        """
        logger.info("Periodic push: pushing all device states to xsolar...")
        
        for device_addr, device in self.device_manager.get_all_devices().items():
            state = self.state_manager.get_latest_state(device_addr)
            if not state:
                logger.debug(f"No state for {device_addr}, skipping")
                continue
            
            xsolar_data = self._build_xsolar_data(device, state)
            if not xsolar_data:
                logger.debug(f"No xsolar data for {device_addr}, skipping")
                continue
            
            payload = {
                'ts': datetime.now().isoformat() + '+07:00',
                'site': 'bluCafe',
                'id': device_addr,
                'type': device['nr_type'],
                'name': device['name'],
                'location': device['group'],
                'data': xsolar_data
            }
            
            topic = f"smarteos/bluCafe/{device_addr}"
            
            try:
                self.mqtt_xsolar.publish(topic, json.dumps(payload), qos=0)
                logger.info(f"Periodic push: {device_addr} -> xsolar ({len(xsolar_data)} attrs)")
            except Exception as e:
                logger.error(f"Failed to push {device_addr} to xsolar: {e}")
        
        self._last_push = {}  # Reset rate limit sau periodic push
    
    def _build_xsolar_data(self, device: Dict, state: Dict) -> Dict:
        """
        Build xsolar data dict từ device state
        
        Chỉ lấy attributes có xsolar: true, dùng xsolar_key làm key name.
        
        Args:
            device: Device dictionary từ device_manager
            state: State dictionary từ state_manager
            
        Returns:
            Dictionary với xsolar_key -> value
        """
        xsolar_data = {}
        
        for attr_id, attr_config in device['attributes'].items():
            if not attr_config.get('xsolar', False):
                continue
            
            # Handle composite attributes (MCB dp6)
            if attr_config.get('decode'):
                for rule in attr_config['decode']:
                    if rule.get('xsolar', False) and rule['id'] in state:
                        xsolar_data[rule['xsolar_key']] = state[rule['id']]
            elif attr_config['label'] in state:
                xsolar_key = attr_config.get('xsolar_key')
                if xsolar_key:
                    xsolar_data[xsolar_key] = state[attr_config['label']]
        
        return xsolar_data
    
    def _find_attr_id_by_xsolar_key(self, attributes: Dict, xsolar_key: str) -> Optional[str]:
        """
        Tìm attribute ID theo xsolar_key
        
        Args:
            attributes: Attributes dictionary từ device
            xsolar_key: xsolar key name (power, temperature, fan)
            
        Returns:
            Attribute ID hoặc None
        """
        for attr_id, attr_config in attributes.items():
            if attr_config.get('xsolar_key') == xsolar_key:
                return attr_id
        
        return None
