"""
Device Manager - Load and manage devices from devices.yaml
"""

import yaml
import logging
from pathlib import Path
from typing import Dict, Optional, Any

logger = logging.getLogger(__name__)


class DeviceManager:
    """Manage devices loaded from devices.yaml"""
    
    def __init__(self, devices_file: str):
        """
        Initialize DeviceManager
        
        Args:
            devices_file: Path to devices.yaml file
        """
        self.devices_file = Path(devices_file)
        self.devices: Dict[str, Dict] = {}
        self.site_name: str = ""
        self.max_devices: int = 0
        self.load_devices()
    
    def load_devices(self):
        """Load devices from devices.yaml"""
        try:
            with open(self.devices_file, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f)
            
            self.site_name = data.get('site', 'bluCafe')
            self.max_devices = data.get('max_devices', 12)
            
            for device in data.get('devices', []):
                if not device.get('enabled', True):
                    continue
                
                zigbee_addr = device['zigbee_addr']
                self.devices[zigbee_addr] = {
                    'device_id': device['device_id'],
                    'zigbee_addr': zigbee_addr,
                    'nr_type': device['nr_type'],
                    'name': device['name'],
                    'group': device['group'],
                    'gateway': device['gateway'],
                    'attributes': self._parse_attributes(device.get('attributes', [])),
                    'metadata': {
                        'ac_index': device.get('ac_index'),
                        'sign_index': device.get('sign_index'),
                        'power_index': device.get('power_index'),
                        'light_sensor_index': device.get('light_sensor_index'),
                    }
                }
            
            logger.info(f"Loaded {len(self.devices)} devices from {self.devices_file}")
            
        except Exception as e:
            logger.error(f"Failed to load devices from {self.devices_file}: {e}")
            raise
    
    def _parse_attributes(self, attrs: list) -> Dict[str, Dict]:
        """
        Parse attributes from devices.yaml format
        
        Args:
            attrs: List of attribute definitions from devices.yaml
            
        Returns:
            Dictionary mapping attribute ID to attribute config
        """
        result = {}
        for attr in attrs:
            attr_id = attr['id']
            result[attr_id] = {
                'label': attr.get('label', ''),
                'type': attr.get('type', 'number'),
                'scale': attr.get('scale'),
                'unit': attr.get('unit'),
                'display': attr.get('display', False),
                'xsolar': attr.get('xsolar', False),
                'xsolar_key': attr.get('xsolar_key'),
                'decode': attr.get('decode'),  # For composite attributes
            }
        return result
    
    def get_device(self, zigbee_addr: str) -> Optional[Dict]:
        """
        Get device by Zigbee address
        
        Args:
            zigbee_addr: Zigbee address (e.g., "0xC5A9")
            
        Returns:
            Device dictionary or None if not found
        """
        return self.devices.get(zigbee_addr)
    
    def get_all_devices(self) -> Dict[str, Dict]:
        """
        Get all enabled devices
        
        Returns:
            Dictionary of all devices
        """
        return self.devices
    
    def get_xsolar_attributes(self, zigbee_addr: str) -> Dict[str, Dict]:
        """
        Get only attributes marked for xsolar
        
        Args:
            zigbee_addr: Zigbee address
            
        Returns:
            Dictionary of xsolar-marked attributes
        """
        device = self.get_device(zigbee_addr)
        if not device:
            return {}
        
        return {
            attr_id: attr
            for attr_id, attr in device['attributes'].items()
            if attr.get('xsolar', False)
        }
    
    def get_device_by_type(self, nr_type: str) -> list:
        """
        Get all devices of a specific type
        
        Args:
            nr_type: Device type (e.g., "ac_controller", "mcb")
            
        Returns:
            List of devices matching the type
        """
        return [
            device for device in self.devices.values()
            if device['nr_type'] == nr_type
        ]
    
    def get_gateway_devices(self, gateway: str) -> list:
        """
        Get all devices connected to a specific gateway
        
        Args:
            gateway: Gateway name (e.g., "tasmota_6DCAA8")
            
        Returns:
            List of devices connected to the gateway
        """
        return [
            device for device in self.devices.values()
            if device['gateway'] == gateway
        ]
