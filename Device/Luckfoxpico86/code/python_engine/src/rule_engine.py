"""
Rule Engine - Evaluate and execute automation rules
"""

import yaml
import logging
from datetime import datetime
from typing import Dict, List, Any, Optional
import fnmatch

logger = logging.getLogger(__name__)


class RuleEngine:
    """Evaluate and execute automation rules"""
    
    def __init__(self, rules_file: str, mqtt_client, state_manager, device_manager):
        """
        Initialize RuleEngine
        
        Args:
            rules_file: Path to rules.yaml file
            mqtt_client: MQTTClient instance
            state_manager: StateManager instance
            device_manager: DeviceManager instance
        """
        self.rules_file = rules_file
        self.mqtt_client = mqtt_client
        self.state_manager = state_manager
        self.device_manager = device_manager
        self.rules: List[Dict] = []
        self.load_rules()
    
    def load_rules(self):
        """Load rules from YAML file"""
        try:
            with open(self.rules_file, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f)
            
            self.rules = data.get('rules', [])
            logger.info(f"Loaded {len(self.rules)} rules from {self.rules_file}")
            
        except Exception as e:
            logger.error(f"Failed to load rules from {self.rules_file}: {e}")
            self.rules = []
    
    def evaluate_trigger(self, trigger: Dict, topic: str, payload: Any) -> bool:
        """
        Evaluate if trigger matches
        
        Args:
            trigger: Trigger definition from rule
            topic: MQTT topic
            payload: MQTT payload
            
        Returns:
            True if trigger matches, False otherwise
        """
        trigger_type = trigger.get('type')
        
        if trigger_type == 'mqtt':
            return self._match_topic(trigger['topic'], topic)
        
        return False
    
    def evaluate_condition(self, condition: Dict) -> bool:
        """
        Evaluate if condition is met
        
        Args:
            condition: Condition definition from rule
            
        Returns:
            True if condition is met, False otherwise
        """
        condition_type = condition.get('type')
        
        if condition_type == 'state':
            return self._check_state_condition(condition)
        elif condition_type == 'numeric_state':
            return self._check_numeric_state_condition(condition)
        elif condition_type == 'time':
            return self._check_time_condition(condition)
        
        return False
    
    def execute_action(self, action: Dict):
        """
        Execute action
        
        Args:
            action: Action definition from rule
        """
        action_type = action.get('type')
        
        if action_type == 'mqtt_publish':
            self.mqtt_client.publish(
                action['topic'],
                action['payload'],
                qos=action.get('qos', 1),
                retain=action.get('retain', False)
            )
            logger.info(f"Executed action: publish to {action['topic']}")
        
        elif action_type == 'log':
            logger.info(f"Rule action log: {action.get('message', '')}")
    
    def process_message(self, topic: str, payload: Any):
        """
        Process incoming MQTT message and evaluate rules
        
        Args:
            topic: MQTT topic
            payload: MQTT payload
        """
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
                try:
                    self.execute_action(action)
                except Exception as e:
                    logger.error(f"Failed to execute action in rule '{rule.get('alias', 'unknown')}': {e}")
            
            logger.info(f"Rule executed: {rule.get('alias', 'unknown')}")
    
    def _match_topic(self, pattern: str, topic: str) -> bool:
        """
        Match MQTT topic with wildcard pattern
        
        Args:
            pattern: MQTT topic pattern (supports + and # wildcards)
            topic: Actual MQTT topic
            
        Returns:
            True if topic matches pattern
        """
        # Convert MQTT wildcards to fnmatch wildcards
        # + matches single level, # matches multiple levels
        pattern = pattern.replace('+', '*')
        pattern = pattern.replace('#', '**')
        
        return fnmatch.fnmatch(topic, pattern)
    
    def _check_state_condition(self, condition: Dict) -> bool:
        """
        Check state condition
        
        Args:
            condition: State condition definition
            
        Returns:
            True if condition is met
        """
        topic = condition.get('topic')
        expected_value = condition.get('equals')
        
        # Extract device_id and attribute from topic
        # This is a simplified implementation
        # In practice, you'd need to map topics to device attributes
        
        # For now, return True (placeholder)
        logger.debug(f"State condition check: {topic} == {expected_value}")
        return True
    
    def _check_numeric_state_condition(self, condition: Dict) -> bool:
        """
        Check numeric state condition
        
        Args:
            condition: Numeric state condition definition
            
        Returns:
            True if condition is met
        """
        topic = condition.get('topic')
        above = condition.get('above')
        below = condition.get('below')
        
        # Extract device_id and attribute from topic
        # This is a simplified implementation
        
        # For now, return True (placeholder)
        logger.debug(f"Numeric state condition check: {topic} above={above} below={below}")
        return True
    
    def _check_time_condition(self, condition: Dict) -> bool:
        """
        Check time condition
        
        Args:
            condition: Time condition definition
            
        Returns:
            True if current time is within the specified range
        """
        now = datetime.now().time()
        
        if 'after' in condition:
            after_time = datetime.strptime(condition['after'], '%H:%M:%S').time()
            if now < after_time:
                logger.debug(f"Time condition failed: {now} < {after_time}")
                return False
        
        if 'before' in condition:
            before_time = datetime.strptime(condition['before'], '%H:%M:%S').time()
            if now > before_time:
                logger.debug(f"Time condition failed: {now} > {before_time}")
                return False
        
        logger.debug(f"Time condition passed: {now}")
        return True
    
    def reload_rules(self):
        """Reload rules from file"""
        logger.info("Reloading rules...")
        self.load_rules()
