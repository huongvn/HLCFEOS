"""
HMI Bridge - Giao tiếp giữa LVGL app và NanoMQ

Module này đảm nhận vai trò tương tự như flow 08-hmi-bridge.json trong Node-RED:
1. Nhận lệnh từ LVGL (bms/ac/+/+/set, bms/sign/+/power/set)
2. Convert thành ZbSend commands
3. Publish đến Tasmota gateway
4. Listen device state changes
5. Publish feedback về LVGL
"""

import logging
import json
from typing import Dict, Optional, Any

logger = logging.getLogger(__name__)


class HMIBridge:
    """Bridge giữa LVGL app và Zigbee devices thông qua MQTT"""
    
    def __init__(self, mqtt_client, state_manager, device_manager):
        """
        Khởi tạo HMI Bridge
        
        Args:
            mqtt_client: MQTT client instance
            state_manager: State manager instance
            device_manager: Device manager instance
        """
        self.mqtt_client = mqtt_client
        self.state_manager = state_manager
        self.device_manager = device_manager
        
        # Cache để track state changes
        self._last_states: Dict[str, Dict] = {}
        
        logger.info("HMI Bridge initialized")
    
    def start(self):
        """
        Start HMI Bridge - subscribe các topics cần thiết
        """
        # Subscribe AC control commands
        # Format: bms/ac/{idx}/{attr}/set
        # Ví dụ: bms/ac/0/power/set, bms/ac/0/temperature/set
        self.mqtt_client.subscribe("bms/ac/+/+/set", qos=1)
        
        # Subscribe Sign/MCB control commands
        # Format: bms/sign/{idx}/power/set
        # Ví dụ: bms/sign/0/power/set
        self.mqtt_client.subscribe("bms/sign/+/power/set", qos=1)
        
        # Subscribe Scene control
        # Format: bms/scene/master
        # Payload: "ON" (Open Store) hoặc "OFF" (Close Store)
        self.mqtt_client.subscribe("bms/scene/master", qos=1)
        
        logger.info("HMI Bridge started - subscribed to control topics")
    
    def handle_message(self, topic: str, payload: Any):
        """
        Xử lý message từ LVGL
        
        Args:
            topic: MQTT topic
            payload: Message payload
        """
        try:
            # Parse topic
            parts = topic.split('/')
            
            # Handle AC commands: bms/ac/{idx}/{attr}/set
            if len(parts) == 5 and parts[0] == 'bms' and parts[1] == 'ac' and parts[4] == 'set':
                idx = int(parts[2])
                attr = parts[3]
                self._handle_ac_command(idx, attr, payload)
            
            # Handle Sign commands: bms/sign/{idx}/power/set
            elif len(parts) == 5 and parts[0] == 'bms' and parts[1] == 'sign' and parts[3] == 'power' and parts[4] == 'set':
                idx = int(parts[2])
                self._handle_sign_command(idx, payload)
            
            # Handle Scene commands: bms/scene/master
            elif len(parts) == 3 and parts[0] == 'bms' and parts[1] == 'scene' and parts[2] == 'master':
                self._handle_scene_command(payload)
            
        except Exception as e:
            logger.error(f"Error handling HMI message: {e}", exc_info=True)
    
    def _handle_ac_command(self, idx: int, attr: str, payload: Any):
        """
        Xử lý lệnh điều khiển AC
        
        Args:
            idx: AC index (0, 1, 2, ...)
            attr: Attribute name (power, temperature, fan)
            payload: Command payload
        """
        # Tìm device theo index
        device = self._get_device_by_index('ac_controller', idx)
        if not device:
            logger.warning(f"AC device not found for index {idx}")
            return
        
        zigbee_addr = device['zigbee_addr']
        gateway = device['gateway']
        attributes = device['attributes']
        
        # Build ZbSend command
        writes = {}
        
        if attr == 'power':
            # Tìm attribute ID cho power
            power_attr_id = self._find_attr_id_by_label(attributes, 'Power')
            if power_attr_id:
                value = 1 if payload.upper() == 'ON' else 0
                writes[f"EF00/{power_attr_id}"] = value
            else:
                logger.warning(f"Power attribute not found for device {zigbee_addr}")
                return
        
        elif attr == 'temperature':
            # Tìm attribute ID cho temperature
            temp_attr_id = self._find_attr_id_by_label(attributes, 'Temperature')
            if temp_attr_id:
                value = int(payload)
                writes[f"EF00/{temp_attr_id}"] = value
            else:
                logger.warning(f"Temperature attribute not found for device {zigbee_addr}")
                return
        
        elif attr == 'fan':
            # Tìm attribute ID cho fan speed
            fan_attr_id = self._find_attr_id_by_label(attributes, 'Fan Speed')
            if fan_attr_id:
                value = int(payload)
                writes[f"EF00/{fan_attr_id}"] = value
            else:
                logger.warning(f"Fan Speed attribute not found for device {zigbee_addr}")
                return
        
        else:
            logger.warning(f"Unknown AC attribute: {attr}")
            return
        
        # Publish ZbSend command
        if writes:
            zb_send_payload = {
                "Device": zigbee_addr,
                "Write": writes
            }
            
            topic = f"cmnd/{gateway}/ZbSend"
            self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
            
            logger.info(f"AC command sent: {zigbee_addr} {attr}={payload}")
    
    def _handle_sign_command(self, idx: int, payload: Any):
        """
        Xử lý lệnh điều khiển Sign/MCB
        
        Args:
            idx: Sign index (0, 1, 2, ...)
            payload: Command payload ("ON" hoặc "OFF")
        """
        # Tìm device theo index
        device = self._get_device_by_index('mcb', idx)
        if not device:
            logger.warning(f"MCB device not found for index {idx}")
            return
        
        zigbee_addr = device['zigbee_addr']
        gateway = device['gateway']
        attributes = device['attributes']
        
        # Tìm attribute ID cho control
        control_attr_id = self._find_attr_id_by_label(attributes, 'Control')
        if not control_attr_id:
            logger.warning(f"Control attribute not found for device {zigbee_addr}")
            return
        
        # Build ZbSend command
        value = 1 if payload.upper() == 'ON' else 0
        writes = {
            f"EF00/{control_attr_id}": value
        }
        
        zb_send_payload = {
            "Device": zigbee_addr,
            "Write": writes,
            "Endpoint": 1  # MCB cần Endpoint
        }
        
        topic = f"cmnd/{gateway}/ZbSend"
        self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
        
        logger.info(f"Sign command sent: {zigbee_addr} power={payload}")
    
    def _handle_scene_command(self, payload: Any):
        """
        Xử lý lệnh Scene (Open Store / Close Store)
        
        Args:
            payload: "ON" (Open Store) hoặc "OFF" (Close Store)
        """
        action = payload.upper()
        
        if action == 'ON':
            # Open Store: Bật tất cả AC và Sign
            logger.info("Scene: Open Store")
            self._execute_scene_open()
        elif action == 'OFF':
            # Close Store: Tắt tất cả AC và Sign
            logger.info("Scene: Close Store")
            self._execute_scene_close()
        else:
            logger.warning(f"Unknown scene command: {payload}")
    
    def _execute_scene_open(self):
        """
        Thực hiện scene Open Store
        - Bật tất cả AC
        - Bật tất cả Sign
        """
        # Bật tất cả AC
        ac_devices = self.device_manager.get_device_by_type('ac_controller')
        for device in ac_devices:
            zigbee_addr = device['zigbee_addr']
            gateway = device['gateway']
            attributes = device['attributes']
            
            power_attr_id = self._find_attr_id_by_label(attributes, 'Power')
            if power_attr_id:
                writes = {f"EF00/{power_attr_id}": 1}
                zb_send_payload = {"Device": zigbee_addr, "Write": writes}
                topic = f"cmnd/{gateway}/ZbSend"
                self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
                logger.info(f"Scene Open: Turned on AC {zigbee_addr}")
        
        # Bật tất cả Sign/MCB
        mcb_devices = self.device_manager.get_device_by_type('mcb')
        for device in mcb_devices:
            zigbee_addr = device['zigbee_addr']
            gateway = device['gateway']
            attributes = device['attributes']
            
            control_attr_id = self._find_attr_id_by_label(attributes, 'Control')
            if control_attr_id:
                writes = {f"EF00/{control_attr_id}": 1}
                zb_send_payload = {"Device": zigbee_addr, "Write": writes, "Endpoint": 1}
                topic = f"cmnd/{gateway}/ZbSend"
                self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
                logger.info(f"Scene Open: Turned on Sign {zigbee_addr}")
    
    def _execute_scene_close(self):
        """
        Thực hiện scene Close Store
        - Tắt tất cả AC
        - Tắt tất cả Sign
        """
        # Tắt tất cả AC
        ac_devices = self.device_manager.get_device_by_type('ac_controller')
        for device in ac_devices:
            zigbee_addr = device['zigbee_addr']
            gateway = device['gateway']
            attributes = device['attributes']
            
            power_attr_id = self._find_attr_id_by_label(attributes, 'Power')
            if power_attr_id:
                writes = {f"EF00/{power_attr_id}": 0}
                zb_send_payload = {"Device": zigbee_addr, "Write": writes}
                topic = f"cmnd/{gateway}/ZbSend"
                self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
                logger.info(f"Scene Close: Turned off AC {zigbee_addr}")
        
        # Tắt tất cả Sign/MCB
        mcb_devices = self.device_manager.get_device_by_type('mcb')
        for device in mcb_devices:
            zigbee_addr = device['zigbee_addr']
            gateway = device['gateway']
            attributes = device['attributes']
            
            control_attr_id = self._find_attr_id_by_label(attributes, 'Control')
            if control_attr_id:
                writes = {f"EF00/{control_attr_id}": 0}
                zb_send_payload = {"Device": zigbee_addr, "Write": writes, "Endpoint": 1}
                topic = f"cmnd/{gateway}/ZbSend"
                self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
                logger.info(f"Scene Close: Turned off Sign {zigbee_addr}")
    
    def publish_feedback(self, device_addr: str, state: Dict):
        """
        Publish feedback về LVGL khi device state thay đổi
        
        Args:
            device_addr: Zigbee address của device
            state: State dictionary mới
        """
        device = self.device_manager.get_device(device_addr)
        if not device:
            return
        
        device_type = device['nr_type']
        
        # Lấy index từ metadata
        if device_type == 'ac_controller':
            idx = device['metadata'].get('ac_index')
            if idx is None:
                return
            self._publish_ac_feedback(idx, state)
        
        elif device_type == 'mcb':
            idx = device['metadata'].get('sign_index')
            if idx is None:
                return
            self._publish_sign_feedback(idx, state)
    
    def _publish_ac_feedback(self, idx: int, state: Dict):
        """
        Publish AC feedback về LVGL
        
        Args:
            idx: AC index
            state: State dictionary
        """
        # Publish từng attribute
        for attr_name, value in state.items():
            # Map attribute name to LVGL topic
            if attr_name == 'Power':
                topic = f"bms/ac/{idx}/power"
                payload = "ON" if value else "OFF"
                self.mqtt_client.publish(topic, payload, qos=1)
            
            elif attr_name == 'Temperature':
                topic = f"bms/ac/{idx}/temperature"
                payload = str(value)
                self.mqtt_client.publish(topic, payload, qos=1)
            
            elif attr_name == 'Room Temp':
                topic = f"bms/ac/{idx}/room_temp"
                payload = str(value)
                self.mqtt_client.publish(topic, payload, qos=1)
            
            elif attr_name == 'Fan Speed':
                topic = f"bms/ac/{idx}/fan"
                payload = str(value)
                self.mqtt_client.publish(topic, payload, qos=1)
    
    def _publish_sign_feedback(self, idx: int, state: Dict):
        """
        Publish Sign/MCB feedback về LVGL
        
        Args:
            idx: Sign index
            state: State dictionary
        """
        # Publish control state
        if 'Control' in state:
            topic = f"bms/sign/{idx}/power"
            payload = "ON" if state['Control'] else "OFF"
            self.mqtt_client.publish(topic, payload, qos=1)
        
        # Publish other attributes if needed
        for attr_name, value in state.items():
            if attr_name != 'Control':
                # Có thể publish thêm các attributes khác
                pass
    
    def _get_device_by_index(self, device_type: str, idx: int) -> Optional[Dict]:
        """
        Tìm device theo type và index
        
        Args:
            device_type: Device type (ac_controller, mcb, etc.)
            idx: Device index
            
        Returns:
            Device dictionary hoặc None
        """
        devices = self.device_manager.get_device_by_type(device_type)
        
        for device in devices:
            metadata = device.get('metadata', {})
            
            if device_type == 'ac_controller':
                if metadata.get('ac_index') == idx:
                    return device
            
            elif device_type == 'mcb':
                if metadata.get('sign_index') == idx:
                    return device
        
        return None
    
    def _find_attr_id_by_label(self, attributes: Dict, label: str) -> Optional[str]:
        """
        Tìm attribute ID theo label
        
        Args:
            attributes: Attributes dictionary từ device
            label: Attribute label (Power, Temperature, etc.)
            
        Returns:
            Attribute ID hoặc None
        """
        for attr_id, attr_config in attributes.items():
            if attr_config.get('label') == label:
                return attr_id
        
        return None
