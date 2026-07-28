"""
MQTT Client - Handle MQTT communication with broker
"""

import paho.mqtt.client as mqtt
import json
import logging
from typing import Callable, Optional, Any

logger = logging.getLogger(__name__)


class MQTTClient:
    """MQTT client for subscribing and publishing messages"""
    
    def __init__(self, broker: str, port: int, client_id: str):
        """
        Initialize MQTT client
        
        Args:
            broker: MQTT broker hostname or IP
            port: MQTT broker port
            client_id: MQTT client ID
        """
        self.broker = broker
        self.port = port
        self.client_id = client_id
        self.client = mqtt.Client(client_id=client_id)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        self.message_handler: Optional[Callable] = None
        self.connected = False
        
        logger.info(f"MQTT client initialized: {broker}:{port}, client_id={client_id}")
    
    def connect(self):
        """Connect to MQTT broker"""
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            logger.info(f"Connecting to MQTT broker {self.broker}:{self.port}")
        except Exception as e:
            logger.error(f"Failed to connect to MQTT broker: {e}")
            raise
    
    def disconnect(self):
        """Disconnect from MQTT broker"""
        self.client.loop_stop()
        self.client.disconnect()
        logger.info("Disconnected from MQTT broker")
    
    def subscribe(self, topic: str, qos: int = 1):
        """
        Subscribe to topic
        
        Args:
            topic: MQTT topic pattern
            qos: Quality of Service level (0, 1, or 2)
        """
        self.client.subscribe(topic, qos)
        logger.info(f"Subscribed to topic: {topic} (QoS {qos})")
    
    def publish(self, topic: str, payload: Any, qos: int = 1, retain: bool = False):
        """
        Publish message
        
        Args:
            topic: MQTT topic
            payload: Message payload (will be JSON encoded if not string)
            qos: Quality of Service level (0, 1, or 2)
            retain: Whether to retain the message
        """
        if not isinstance(payload, str):
            payload = json.dumps(payload)
        
        self.client.publish(topic, payload, qos=qos, retain=retain)
        logger.debug(f"Published to {topic}: {payload[:100]}")
    
    def set_message_handler(self, handler: Callable):
        """
        Set message handler callback
        
        Args:
            handler: Callback function(topic, payload)
        """
        self.message_handler = handler
    
    def _on_connect(self, client, userdata, flags, rc):
        """Callback when connected to broker"""
        if rc == 0:
            self.connected = True
            logger.info(f"Connected to MQTT broker successfully")
            # Subscribe to all topics
            self.client.subscribe("#", qos=1)
        else:
            logger.error(f"Failed to connect to MQTT broker, return code {rc}")
    
    def _on_disconnect(self, client, userdata, rc):
        """Callback when disconnected from broker"""
        self.connected = False
        if rc != 0:
            logger.warning(f"Unexpected disconnection from MQTT broker (rc={rc})")
        else:
            logger.info("Disconnected from MQTT broker")
    
    def _on_message(self, client, userdata, msg):
        """Callback when message received"""
        try:
            # Try to parse as JSON
            payload = json.loads(msg.payload.decode('utf-8'))
        except (json.JSONDecodeError, UnicodeDecodeError):
            # If not JSON, use raw string
            payload = msg.payload.decode('utf-8', errors='ignore')
        
        logger.debug(f"Received message on {msg.topic}: {str(payload)[:100]}")
        
        if self.message_handler:
            try:
                self.message_handler(msg.topic, payload)
            except Exception as e:
                logger.error(f"Error in message handler: {e}")
    
    def is_connected(self) -> bool:
        """Check if client is connected"""
        return self.connected
