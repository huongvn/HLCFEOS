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
    
    def __init__(self, mqtt_client, state_manager, device_manager, queue_manager=None):
        """
        Khởi tạo HMI Bridge
        
        Args:
            mqtt_client: MQTT client instance
            state_manager: State manager instance
            device_manager: Device manager instance
            queue_manager: QueueManager instance (optional, for queued ZbSend)
        """
        self.mqtt_client = mqtt_client
        self.state_manager = state_manager
        self.device_manager = device_manager
        self.queue_mgr = queue_manager
        
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
        # Format: bms/sign/{idx}/{attr_id}/set
        # Ví dụ: bms/sign/0/0110/set
        self.mqtt_client.subscribe("bms/sign/+/+/set", qos=1)
        
        # Subscribe Scene control
        # Format: bms/scene/master
        # Payload: "ON" (Open Store) hoặc "OFF" (Close Store)
        self.mqtt_client.subscribe("bms/scene/master", qos=1)
        
        # Subscribe Tasmota telemetry (use # to catch all, tele/# has issues with paho v1)
        self.mqtt_client.subscribe("#", qos=0)
        
        # Subscribe Tasmota telemetry (explicit - # alone doesnt work with paho v1)
        self.mqtt_client.subscribe("tele/#", qos=0)
        
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
            
            # Handle Sign commands: bms/sign/{idx}/{attr_id}/set
            elif len(parts) == 5 and parts[0] == 'bms' and parts[1] == 'sign' and parts[4] == 'set':
                idx = int(parts[2])
                attr = parts[3]
                self._handle_sign_command(idx, attr, payload)
            
            # Handle Scene commands: bms/scene/master
            elif len(parts) == 3 and parts[0] == 'bms' and parts[1] == 'scene' and parts[2] == 'master':
                self._handle_scene_command(payload)
            
        except Exception as e:
            logger.error(f"Error handling HMI message: {e}", exc_info=True)
    
    def _handle_ac_command(self, idx: int, attr: str, payload: Any):
        """
        Xử lý lệnh điều khiển AC
        
        Format: bms/ac/{idx}/{attr_id}/set  (attr_id = YAML id field)
        
        Args:
            idx: AC index
            attr: Attribute id (e.g., "0101", "0202", "0405")
            payload: Command payload
        """
        device = self._get_device_by_index('ac_controller', idx)
        if not device:
            logger.warning(f"AC device not found for index {idx}")
            return
        
        zigbee_addr = device['zigbee_addr']
        gateway = device['gateway']
        attributes = device['attributes']
        
        attr_config = attributes.get(attr)
        if not attr_config:
            logger.warning(f"Unknown AC attribute id: {attr}")
            return
        
        attr_type = attr_config.get('type', 'number')
        
        if attr_type == 'bool':
            value = 1 if str(payload).upper() == 'ON' else 0
        else:
            value = int(payload)
        
        # Queue ZbSend via QueueManager (không mất lệnh)
        if self.queue_mgr:
            self.queue_mgr.send_zbsend(gateway, zigbee_addr, {f"EF00/{attr}": value}, endpoint=1)
        else:
            # Fallback: publish directly
            zb_send_payload = {
                "Device": zigbee_addr,
                "Write": {f"EF00/{attr}": value},
                "Endpoint": 1
            }
            topic = f"cmnd/{gateway}/ZbSend"
            self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
        logger.info(f"AC command: {zigbee_addr} {attr}={payload}")
        
        # Immediately publish feedback to LVGL (optimistic update)
        label = attr_config.get('label', attr)
        if attr_type == 'bool':
            feedback_payload = "ON" if value == 1 else "OFF"
        else:
            feedback_payload = str(value)
        feedback_topic = f"bms/ac/{idx}/{attr}"
        self.mqtt_client.publish(feedback_topic, feedback_payload, qos=1)
        logger.info(f"AC feedback: {idx} {attr}={feedback_payload}")


    
    def _handle_sign_command(self, idx: int, attr: str, payload: Any):
        """
        Xử lý lệnh điều khiển Sign/MCB
        
        Format: bms/sign/{idx}/{attr_id}/set  (attr_id = YAML id field)
        
        Args:
            idx: Sign index
            attr: Attribute id (e.g., "0110")
            payload: Command payload ("ON" hoặc "OFF")
        """
        device = self._get_device_by_index('mcb', idx)
        if not device:
            logger.warning(f"MCB device not found for index {idx}")
            return
        
        zigbee_addr = device['zigbee_addr']
        gateway = device['gateway']
        attributes = device['attributes']
        
        attr_config = attributes.get(attr)
        if not attr_config:
            logger.warning(f"Unknown Sign attribute id: {attr}")
            return
        
        attr_type = attr_config.get('type', 'number')
        
        if attr_type == 'bool':
            value = 1 if str(payload).upper() == 'ON' else 0
        else:
            value = int(payload)
        
        # Queue ZbSend
        if self.queue_mgr:
            self.queue_mgr.send_zbsend(gateway, zigbee_addr, {f"EF00/{attr}": value}, endpoint=1)
        else:
            topic = f"cmnd/{gateway}/ZbSend"
            zb_send_payload = {"Device": zigbee_addr, "Write": {f"EF00/{attr}": value}, "Endpoint": 1}
            self.mqtt_client.publish(topic, json.dumps(zb_send_payload), qos=1)
        logger.info(f"Sign command: {zigbee_addr} {attr}={payload}")
        
        # Immediately publish feedback to LVGL (optimistic update)
        if attr_type == 'bool':
            feedback_payload = "ON" if value == 1 else "OFF"
        else:
            feedback_payload = str(value)
        feedback_topic = f"bms/sign/{idx}/{attr}"
        self.mqtt_client.publish(feedback_topic, feedback_payload, qos=1)
        logger.info(f"Sign feedback: {idx} {attr}={feedback_payload}")
    
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
        
        elif device_type == 'power_meter':
            idx = device['metadata'].get('power_index')
            if idx is None:
                return
            self._publish_power_feedback(idx, state)
        
        elif device_type == 'light_sensor':
            idx = device['metadata'].get('light_sensor_index')
            if idx is None:
                return
            self._publish_light_feedback(idx, state)
    
    def _publish_ac_feedback(self, idx: int, state: Dict):
        """
        Publish AC feedback về LVGL
        
        Topic format: bms/ac/{idx}/{attr_id}
        attr_id from YAML (e.g., "0101"=Power, "0202"=Temperature, "0203"=Room Temp, "0405"=Fan Speed)
        
        Args:
            idx: AC index
            state: State dictionary (keys are labels, e.g., "Room Temp")
        """
        device = self._get_device_by_index('ac_controller', idx)
        if not device:
            return
        
        attributes = device.get('attributes', {})
        for attr_id, attr_config in attributes.items():
            label = attr_config.get('label', '')
            if label not in state:
                continue
            
            value = state[label]
            topic = f"bms/ac/{idx}/{attr_id}"
            
            if attr_config.get('type') == 'bool':
                val_bool = value in (1, '1', True, 'True', 'true', 'ON', 'on')
                payload = "ON" if val_bool else "OFF"
            else:
                payload = str(value)
            
            self.mqtt_client.publish(topic, payload, qos=1)
            logger.debug(f"HMI AC[{idx}] → {topic} = {payload}")
    
    def _publish_sign_feedback(self, idx: int, state: Dict):
        """
        Publish Sign/MCB feedback về LVGL
        
        Topic format: bms/sign/{idx}/{attr_id}
        attr_id from YAML (e.g., "0110"=Control, "0201"=Energy)
        
        Args:
            idx: Sign index
            state: State dictionary
        """
        device = self._get_device_by_index('mcb', idx)
        if not device:
            return
        
        attributes = device.get('attributes', {})
        for attr_id, attr_config in attributes.items():
            label = attr_config.get('label', '')
            if label not in state:
                continue
            
            value = state[label]
            topic = f"bms/sign/{idx}/{attr_id}"
            
            if attr_config.get('type') == 'bool':
                val_bool = value in (1, '1', True, 'True', 'true', 'ON', 'on')
                payload = "ON" if val_bool else "OFF"
            else:
                payload = str(value)
            
            self.mqtt_client.publish(topic, payload, qos=1)
            logger.debug(f"HMI Sign[{idx}] → {topic} = {payload}")
    
    def _publish_power_feedback(self, idx: int, state: Dict):
        """
        Publish Power Meter feedback về LVGL
        
        Topic format: bms/power/{idx}/{attr_id}
        
        Args:
            idx: Power index
            state: State dictionary
        """
        device = self._get_device_by_index('power_meter', idx)
        if not device:
            return
        
        attributes = device.get('attributes', {})
        for attr_id, attr_config in attributes.items():
            label = attr_config.get('label', '')
            if label not in state:
                continue
            
            value = state[label]
            topic = f"bms/power/{idx}/{attr_id}"
            payload = str(value)
            self.mqtt_client.publish(topic, payload, qos=1)
            logger.info(f"HMI Power[{idx}] → {topic} = {payload}")
    
    def _publish_light_feedback(self, idx: int, state: Dict):
        """
        Publish Light Sensor feedback về LVGL
        
        Topic format: bms/light/{idx}/{attr_id}
        
        Args:
            idx: Light sensor index
            state: State dictionary
        """
        device = self._get_device_by_index('light_sensor', idx)
        if not device:
            return
        
        attributes = device.get('attributes', {})
        for attr_id, attr_config in attributes.items():
            label = attr_config.get('label', '')
            if label not in state:
                continue
            
            value = state[label]
            topic = f"bms/light/{idx}/{attr_id}"
            payload = str(value)
            self.mqtt_client.publish(topic, payload, qos=1)
            logger.info(f"HMI Light[{idx}] → {topic} = {payload}")
    
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
            
            elif device_type == 'power_meter':
                if metadata.get('power_index') == idx:
                    return device
            
            elif device_type == 'light_sensor':
                if metadata.get('light_sensor_index') == idx:
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
