"""
BMS Engine Main - Entry point for the BMS Engine
"""

import logging
import yaml
import signal
import sys
import json
from pathlib import Path
from datetime import datetime
from typing import Any, Dict, Optional

from mqtt_client import MQTTClient
from state_manager import StateManager
from device_manager import DeviceManager
from rule_engine import RuleEngine
from scheduler import Scheduler
from hmi_bridge import HMIBridge
from xsolar_bridge import XsolarBridge
from ota import OTAUpdater
from queue_manager import QueueManager

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler('/home/pico/python_engine/bms-engine.log', mode='a')
    ]
)

logger = logging.getLogger(__name__)


class BMSEngine:
    """Main BMS Engine class"""
    
    def __init__(self, config_file: str):
        """
        Initialize BMS Engine
        
        Args:
            config_file: Path to config.yaml file
        """
        # Load config
        with open(config_file, 'r', encoding='utf-8') as f:
            self.config = yaml.safe_load(f)
        
        logger.info("Initializing BMS Engine...")
        
        # Initialize components
        self.device_manager = DeviceManager(self.config['devices_file'])
        self.state_manager = StateManager(self.config['database']['path'])
        
        # Local MQTT client (for device communication)
        import random, string
        _rs = ''.join(random.choices(string.ascii_lowercase, k=4))
        self.mqtt_local = MQTTClient(
            self.config['mqtt']['broker'],
            self.config['mqtt']['port'],
            f"{self.config['mqtt']['client_id']}-{_rs}"
        )
        
        # Remote MQTT client (for xsolar communication)
        self.mqtt_xsolar = MQTTClient(
            self.config['xsolar']['broker'],
            self.config['xsolar']['port'],
            f"{self.config['mqtt']['client_id']}_xsolar",
            self.config['xsolar'].get('user'),
            self.config['xsolar'].get('pass')
        )
        
        # Rule engine
        self.rule_engine = RuleEngine(
            self.config['rules_file'],
            self.mqtt_local,
            self.state_manager,
            self.device_manager
        )
        
        # Scheduler
        self.scheduler = Scheduler()
        
        # Queue Manager - hàng đợi gửi/nhận cho từng gateway
        self.queue_mgr = QueueManager(self.mqtt_local)
        
        # HMI Bridge - giao tiếp với LVGL app
        self.hmi_bridge = HMIBridge(
            self.mqtt_local,
            self.state_manager,
            self.device_manager,
            self.queue_mgr
        )
        
        # Xsolar Bridge - giao tiếp với xsolar cloud
        self.xsolar_bridge = XsolarBridge(
            self.mqtt_local,
            self.mqtt_xsolar,
            self.state_manager,
            self.device_manager
        )
        
        # OTA Updater - cập nhật phần mềm từ xa
        self.ota_updater = OTAUpdater(self.config.get('ota', {}))
        
        # Setup MQTT message handler
        self.mqtt_local.set_message_handler(self._handle_mqtt_message)
        
        # Setup periodic tasks
        self.scheduler.add_periodic_task(
            self.config['xsolar']['push_interval'],
            self.xsolar_bridge.push_all_states,
            name="Push to xsolar"
        )
        
        # Periodically read Metering cluster (0x0702) energy values
        self.scheduler.add_periodic_task(
            30,  # every 30 seconds
            self._read_energy_values,
            name="Read energy"
        )
        
        # Watch for devices.yaml changes (every 5 seconds)
        self.scheduler.add_periodic_task(
            5,
            self.device_manager.check_reload,
            name="Watch devices.yaml"
        )
        
        # Setup OTA update check (every hour)
        ota_config = self.config.get('ota', {})
        if ota_config.get('enabled', False):
            check_interval = ota_config.get('check_interval', 3600)  # Default: 1 hour
            self.scheduler.add_periodic_task(
                check_interval,
                self._check_ota_updates,
                name="Check OTA updates"
            )
            logger.info(f"OTA update check enabled (every {check_interval}s)")
        
        logger.info("BMS Engine initialized successfully")
    
    def start(self):
        """Start BMS engine"""
        logger.info("Starting BMS Engine...")
        
        # Connect to local MQTT broker
        self.mqtt_local.connect()
        
        # Connect to xsolar MQTT broker
        self.mqtt_xsolar.connect()
        
        # Start Queue Manager (in + out workers)
        self.queue_mgr.set_process_callback(self._process_sensor_from_queue)
        self.queue_mgr.start()
        
        # Start HMI Bridge
        self.hmi_bridge.start()
        
        # Start Xsolar Bridge
        self.xsolar_bridge.start()
        
        # Start scheduler
        self.scheduler.start()
        
        # Setup signal handlers
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
        
        logger.info("BMS Engine started successfully")
        
        # Keep main thread alive
        try:
            signal.pause()
        except AttributeError:
            # signal.pause() is not available on Windows
            while True:
                time.sleep(1)
    
    def stop(self):
        """Stop BMS engine"""
        logger.info("Stopping BMS Engine...")
        self.scheduler.stop()
        self.mqtt_local.disconnect()
        self.mqtt_xsolar.disconnect()
        logger.info("BMS Engine stopped")
    
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        logger.info(f"Received signal {signum}, shutting down...")
        self.stop()
        sys.exit(0)
    
    def _check_ota_updates(self):
        """Check for OTA updates and perform if available"""
        try:
            logger.info("Checking for OTA updates...")
            
            # Check for updates
            update_available, new_version = self.ota_updater.check_update()
            
            if not update_available:
                logger.info("No OTA update available")
                return
            
            logger.info(f"OTA update available: {new_version}")
            
            # Check if auto-update is enabled
            ota_config = self.config.get('ota', {})
            if not ota_config.get('auto_update', False):
                logger.info("Auto-update disabled, skipping update")
                return
            
            # Perform update
            logger.info("Starting OTA update process...")
            success = self.ota_updater.perform_update()
            
            if success:
                logger.info("OTA update completed successfully")
            else:
                logger.error("OTA update failed")
                
        except Exception as e:
            logger.error(f"OTA update check failed: {e}")
    
    def _handle_mqtt_message(self, topic: str, payload: Any):
        """
        Handle incoming MQTT message
        
        Args:
            topic: MQTT topic
            payload: MQTT payload
        """
        # Route HMI commands to HMI Bridge
        if topic.startswith('bms/') and ('/set' in topic or topic == 'bms/scene/master'):
            self.hmi_bridge.handle_message(topic, payload)
            return
        
        # Handle Tasmota LWT (online/offline status)
        if '/LWT' in topic and isinstance(payload, str):
            is_online = payload.upper() == 'ONLINE'
            self._handle_gateway_lwt(topic, is_online)
        
        # Parse Tasmota ZbReceived format - queue for async processing
        if topic.startswith('tele/') and '/SENSOR' in topic:
            self.queue_mgr.enqueue_sensor(topic, payload)
        
        # Evaluate rules (only for dict payloads to avoid blocking paho thread)
        if isinstance(payload, dict):
            self.rule_engine.process_message(topic, payload)
    
    def _read_energy_values(self):
        """Periodically read Metering cluster (0x0702) energy attributes from power meters"""
        import json
        power_devices = self.device_manager.get_device_by_type('power_meter')
        for device in power_devices:
            zigbee_addr = device['zigbee_addr']
            gateway = device['gateway']
            # Read CurrentSummationDelivered (attr 0x0000)
            read_cmd = {"Device": zigbee_addr, "Cluster": 1794, "Endpoint": 1, "Read": 0}
            self.mqtt_local.publish(f"cmnd/{gateway}/ZbSend", json.dumps(read_cmd), qos=1)
            # Read CurrentSummationReceived (attr 0x0001)
            read_cmd2 = {"Device": zigbee_addr, "Cluster": 1794, "Endpoint": 1, "Read": 1}
            self.mqtt_local.publish(f"cmnd/{gateway}/ZbSend", json.dumps(read_cmd2), qos=1)

    def _handle_gateway_lwt(self, topic: str, is_online: bool):
        """Handle Tasmota gateway LWT - publish online status for all devices"""
        # Extract gateway name from topic: tele/tasmota_6DCAA8/LWT
        parts = topic.split('/')
        if len(parts) < 2:
            return
        gateway = parts[1]  # tasmota_6DCAA8
        status = "ON" if is_online else "OFF"
        
        for device_addr, device in self.device_manager.get_all_devices().items():
            if device.get('gateway') == gateway:
                dtype = device['nr_type']
                if dtype == 'ac_controller':
                    idx = device['metadata'].get('ac_index')
                    if idx is not None:
                        self.mqtt_local.publish(f"bms/ac/{idx}/online", status, qos=1)
                elif dtype == 'mcb':
                    idx = device['metadata'].get('sign_index')
                    if idx is not None:
                        self.mqtt_local.publish(f"bms/sign/{idx}/online", status, qos=1)
                elif dtype == 'power_meter':
                    idx = device['metadata'].get('power_index')
                    if idx is not None:
                        self.mqtt_local.publish(f"bms/power/{idx}/online", status, qos=1)
                elif dtype == 'light_sensor':
                    idx = device['metadata'].get('light_sensor_index')
                    if idx is not None:
                        self.mqtt_local.publish(f"bms/light/{idx}/online", status, qos=1)
        
        logger.info(f"Gateway {gateway} {'ONLINE' if is_online else 'OFFLINE'}")
    
    def _process_sensor_from_queue(self, topic: str, payload: Any):
        """Wrapper called by queue worker thread"""
        self._process_zigbee_message(topic, payload)
        # Evaluate rules (only for dict payloads)
        if isinstance(payload, dict):
            self.rule_engine.process_message(topic, payload)

    def _process_zigbee_message(self, topic: str, payload: Dict):
        """
        Process Zigbee message from Tasmota
        
        Args:
            topic: MQTT topic
            payload: MQTT payload (should contain ZbReceived)
        """
        payload_dict = json.loads(payload) if isinstance(payload, str) else payload
        zb_received = payload_dict.get('ZbReceived', {})
        
        for device_addr, device_data in zb_received.items():
            device = self.device_manager.get_device(device_addr)
            if not device:
                logger.debug(f"Unknown device: {device_addr}")
                continue
            
            # Normalize and store attributes
            for attr_id, attr_config in device['attributes'].items():
                raw_value = None
                # Try direct match first (ZCL native: RMSVoltage, Illuminance)
                if attr_id in device_data:
                    raw_value = device_data[attr_id]
                # Try EF00/ prefix (Tuya devices: EF00/0101, EF00/0202)
                elif f"EF00/{attr_id}" in device_data:
                    raw_value = device_data[f"EF00/{attr_id}"]
                
                if raw_value is not None:
                    try:
                        value = self._normalize_attribute_value(
                            attr_id, raw_value, attr_config
                        )
                        
                        # Handle composite attributes (like MCB dp6)
                        if isinstance(value, dict):
                            # Store each decoded attribute separately
                            for decoded_id, decoded_value in value.items():
                                decoded_config = self._get_decoded_attr_config(
                                    attr_config, decoded_id
                                )
                                if decoded_config:
                                    self.state_manager.update_metric(
                                        device_addr, 
                                        decoded_config['label'], 
                                        decoded_value, 
                                        decoded_config['type'],
                                        device['nr_type'],
                                        attr_id
                                    )
                        else:
                            self.state_manager.update_metric(
                                device_addr, 
                                attr_config['label'], 
                                value, 
                                attr_config['type'],
                                device['nr_type'],
                                attr_id
                            )
                    except Exception as e:
                        logger.error(f"Failed to normalize attribute {attr_id} for {device_addr}: {e}")
            
            # Log event
            self.state_manager.log_event(
                device_addr, 
                'state_update', 
                device_data,
                device['nr_type'],
                device['group']
            )
            
            # Publish feedback to LVGL
            try:
                # Get current state after update
                current_state = self.state_manager.get_latest_state(device_addr)
                self.hmi_bridge.publish_feedback(device_addr, current_state)
            except Exception as e:
                logger.error(f"Failed to publish HMI feedback for {device_addr}: {e}")
            
            # Event-driven push to xsolar (rate-limited)
            try:
                self.xsolar_bridge.push_device_state(device_addr)
            except Exception as e:
                logger.error(f"Failed to push {device_addr} to xsolar: {e}")
    
    def _normalize_attribute_value(self, attr_id: str, raw_value: Any, attr_config: Dict) -> Any:
        """
        Normalize attribute value based on devices.yaml config
        
        Args:
            attr_id: Attribute ID
            raw_value: Raw value from MQTT
            attr_config: Attribute configuration from devices.yaml
            
        Returns:
            Normalized value
        """
        # Handle composite attributes (like MCB dp6)
        if attr_config.get('decode'):
            return self._decode_composite(raw_value, attr_config['decode'])
        
        # Apply type conversion
        attr_type = attr_config['type']
        if attr_type == 'bool':
            value = bool(raw_value)
        elif attr_type == 'number':
            # Handle hex strings from Tasmota (e.g., "0x000000005163")
            raw_str = str(raw_value)
            if raw_str.startswith('0x') or raw_str.startswith('0X'):
                value = float(int(raw_str, 16))
            else:
                value = float(raw_value)
            # Apply formula if present (before scale)
            formula = attr_config.get('formula')
            if formula == 'zcl_illuminance':
                # lux = 10^((raw - 1) / 10000), round to 1 decimal
                if value > 0 and value != 0xFFFF:
                    value = round(pow(10, (value - 1) / 10000), 1)
                else:
                    value = 0.0
            # Apply scale if present
            if attr_config.get('scale'):
                value = value * attr_config['scale']
        elif attr_type == 'hex':
            value = str(raw_value)
        else:
            value = raw_value
        
        return value
    
    def _decode_composite(self, hex_value: str, decode_rules: list) -> Dict:
        """
        Decode composite hex value (e.g., MCB dp6)
        
        Args:
            hex_value: Hex string value
            decode_rules: List of decode rules from devices.yaml
            
        Returns:
            Dictionary of decoded values
        """
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
    
    def _get_decoded_attr_config(self, attr_config: Dict, decoded_id: str) -> Optional[Dict]:
        """
        Get configuration for decoded attribute
        
        Args:
            attr_config: Parent attribute configuration
            decoded_id: Decoded attribute ID
            
        Returns:
            Decoded attribute configuration or None
        """
        decode_rules = attr_config.get('decode', [])
        for rule in decode_rules:
            if rule['id'] == decoded_id:
                return rule
        return None
    
    def _push_to_xsolar(self):
        """Push device states to xsolar"""
        logger.info("Pushing device states to xsolar...")
        
        xsolar_config = self.config['xsolar']
        
        for device_addr, device in self.device_manager.get_all_devices().items():
            state = self.state_manager.get_latest_state(device_addr)
            if not state:
                logger.debug(f"No state data for {device_addr}, skipping")
                continue
            
            # Build payload with only xsolar-marked attributes
            xsolar_data = {}
            for attr_id, attr_config in device['attributes'].items():
                if not attr_config.get('xsolar', False):
                    continue
                
                # Handle composite attributes
                if attr_config.get('decode'):
                    for rule in attr_config['decode']:
                        if rule.get('xsolar', False) and rule['id'] in state:
                            xsolar_data[rule['xsolar_key']] = state[rule['id']]
                elif attr_config['label'] in state:
                    xsolar_key = attr_config.get('xsolar_key')
                    if xsolar_key:
                        xsolar_data[xsolar_key] = state[attr_config['label']]
            
            if not xsolar_data:
                logger.debug(f"No xsolar data for {device_addr}, skipping")
                continue
            
            payload = {
                'ts': datetime.now().isoformat() + '+07:00',
                'site': xsolar_config.get('site', 'bluCafe'),
                'id': device_addr,
                'type': device['nr_type'],
                'name': device['name'],
                'location': device['group'],
                'data': xsolar_data
            }
            
            topic = f"{xsolar_config['topic_prefix']}/{device_addr}"
            
            try:
                self.mqtt_xsolar.publish(topic, json.dumps(payload), qos=0)
                logger.info(f"Pushed state for {device_addr} to xsolar: {len(xsolar_data)} attributes")
            except Exception as e:
                logger.error(f"Failed to push state for {device_addr} to xsolar: {e}")


def main():
    """Main entry point"""
    config_file = Path(__file__).parent.parent / 'config' / 'config.yaml'
    
    if not config_file.exists():
        logger.error(f"Config file not found: {config_file}")
        sys.exit(1)
    
    engine = BMSEngine(str(config_file))
    
    try:
        engine.start()
    except KeyboardInterrupt:
        logger.info("Received KeyboardInterrupt, shutting down...")
        engine.stop()
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        engine.stop()
        sys.exit(1)


if __name__ == '__main__':
    main()
