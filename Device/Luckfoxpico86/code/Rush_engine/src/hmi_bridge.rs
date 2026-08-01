use log::{debug, info, warn};
use rumqttc::QoS;
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

use crate::device_manager::{Device, SharedDeviceManager};
use crate::mqtt_client::MqttClient;
use crate::queue_manager::QueueManager;
use crate::state_manager::StateManager;

pub struct HmiBridge {
    mqtt_client: Arc<MqttClient>,
    device_manager: SharedDeviceManager,
    queue_mgr: Arc<QueueManager>,
    last_states: Mutex<HashMap<String, HashMap<String, Value>>>,
}

impl HmiBridge {
    pub fn new(
        mqtt_client: Arc<MqttClient>,
        _state_manager: Arc<Mutex<StateManager>>,
        device_manager: SharedDeviceManager,
        queue_mgr: Arc<QueueManager>,
    ) -> Self {
        Self {
            mqtt_client,
            device_manager,
            queue_mgr,
            last_states: Mutex::new(HashMap::new()),
        }
    }

    pub async fn start(&self) {
        self.mqtt_client
            .subscribe("bms/ac/+/+/set", QoS::AtLeastOnce)
            .await;
        self.mqtt_client
            .subscribe("bms/sign/+/+/set", QoS::AtLeastOnce)
            .await;
        self.mqtt_client
            .subscribe("bms/scene/master", QoS::AtLeastOnce)
            .await;
        self.mqtt_client.subscribe("#", QoS::AtMostOnce).await;
        self.mqtt_client.subscribe("tele/#", QoS::AtMostOnce).await;
        info!("HMI Bridge started - subscribed to control topics");
    }

    pub async fn handle_message(&self, topic: &str, payload: &Value) {
        let parts: Vec<&str> = topic.split('/').collect();

        // Handle AC commands: bms/ac/{idx}/{attr}/set
        if parts.len() == 5
            && parts[0] == "bms"
            && parts[1] == "ac"
            && parts[4] == "set"
        {
            if let (Ok(idx), attr) = (parts[2].parse::<i64>(), parts[3]) {
                self.handle_ac_command(idx, attr, payload).await;
            }
        }
        // Handle Sign commands: bms/sign/{idx}/{attr_id}/set
        else if parts.len() == 5
            && parts[0] == "bms"
            && parts[1] == "sign"
            && parts[4] == "set"
        {
            if let (Ok(idx), attr) = (parts[2].parse::<i64>(), parts[3]) {
                self.handle_sign_command(idx, attr, payload).await;
            }
        }
        // Handle Scene commands: bms/scene/master
        else if parts.len() == 3
            && parts[0] == "bms"
            && parts[1] == "scene"
            && parts[2] == "master"
        {
            self.handle_scene_command(payload).await;
        }
    }

    async fn handle_ac_command(&self, idx: i64, attr: &str, payload: &Value) {
        let device = self.get_device_by_index("ac_controller", idx).await;
        let device = match device {
            Some(d) => d,
            None => {
                warn!("AC device not found for index {}", idx);
                return;
            }
        };

        let attr_config = match device.attributes.get(attr) {
            Some(c) => c.clone(),
            None => {
                warn!("Unknown AC attribute id: {}", attr);
                return;
            }
        };

        let is_bool = attr_config.attr_type == "bool";
        let value: i64 = if is_bool {
            match payload {
                Value::String(s) if s.to_uppercase() == "ON" => 1,
                _ => 0,
            }
        } else {
            match payload {
                Value::Number(n) => n.as_i64().unwrap_or(0),
                Value::String(s) => s.parse::<i64>().unwrap_or(0),
                _ => 0,
            }
        };

        let mut write_dict = HashMap::new();
        write_dict.insert(
            format!("EF00/{}", attr),
            serde_json::json!(value),
        );

        self.queue_mgr
            .send_zbsend(&device.gateway, &device.zigbee_addr, &write_dict, Some(1));

        info!(
            "AC command: {} {}={:?}",
            device.zigbee_addr, attr, payload
        );

        let feedback_payload = if is_bool {
            if value == 1 { "ON".to_string() } else { "OFF".to_string() }
        } else {
            value.to_string()
        };

        let feedback_topic = format!("bms/ac/{}/{}", idx, attr);
        self.mqtt_client
            .publish(&feedback_topic, &feedback_payload, QoS::AtLeastOnce, false)
            .await;
        info!("AC feedback: {} {}={}", idx, attr, feedback_payload);
    }

    async fn handle_sign_command(&self, idx: i64, attr: &str, payload: &Value) {
        let device = self.get_device_by_index("mcb", idx).await;
        let device = match device {
            Some(d) => d,
            None => {
                warn!("MCB device not found for index {}", idx);
                return;
            }
        };

        let _attr_config = match device.attributes.get(attr) {
            Some(c) => c.clone(),
            None => {
                warn!("Unknown Sign attribute id: {}", attr);
                return;
            }
        };

        let value: i64 = match payload {
            Value::String(s) if s.to_uppercase() == "ON" => 1,
            Value::Number(n) => n.as_i64().unwrap_or(0),
            _ => 0,
        };

        let mut write_dict = HashMap::new();
        write_dict.insert(
            format!("EF00/{}", attr),
            serde_json::json!(value),
        );

        self.queue_mgr
            .send_zbsend(&device.gateway, &device.zigbee_addr, &write_dict, Some(1));

        info!(
            "Sign command: {} {}={:?}",
            device.zigbee_addr, attr, payload
        );

        let feedback_payload = if value == 1 { "ON".to_string() } else { "OFF".to_string() };
        let feedback_topic = format!("bms/sign/{}/{}", idx, attr);
        self.mqtt_client
            .publish(&feedback_topic, &feedback_payload, QoS::AtLeastOnce, false)
            .await;
        info!("Sign feedback: {} {}={}", idx, attr, feedback_payload);
    }

    async fn handle_scene_command(&self, payload: &Value) {
        let action = match payload {
            Value::String(s) => s.to_uppercase(),
            _ => {
                warn!("Unknown scene command: {:?}", payload);
                return;
            }
        };

        match action.as_str() {
            "ON" => {
                info!("Scene: Open Store");
                self.execute_scene_open().await;
            }
            "OFF" => {
                info!("Scene: Close Store");
                self.execute_scene_close().await;
            }
            _ => warn!("Unknown scene command: {}", action),
        }
    }

    async fn execute_scene_open(&self) {
        let dm = self.device_manager.read().await;

        for device in dm.get_device_by_type("ac_controller") {
            if let Some(power_attr_id) = find_attr_id_by_label(&device.attributes, "Power") {
                let writes = {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", power_attr_id), serde_json::json!(1));
                    h
                };
                self.queue_mgr
                    .send_zbsend(&device.gateway, &device.zigbee_addr, &writes, None);
                info!("Scene Open: Turned on AC {}", device.zigbee_addr);
            }
        }

        for device in dm.get_device_by_type("mcb") {
            if let Some(control_attr_id) = find_attr_id_by_label(&device.attributes, "Control") {
                let writes = {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", control_attr_id), serde_json::json!(1));
                    h
                };
                self.queue_mgr
                    .send_zbsend(&device.gateway, &device.zigbee_addr, &writes, Some(1));
                info!("Scene Open: Turned on Sign {}", device.zigbee_addr);
            }
        }
    }

    async fn execute_scene_close(&self) {
        let dm = self.device_manager.read().await;

        for device in dm.get_device_by_type("ac_controller") {
            if let Some(power_attr_id) = find_attr_id_by_label(&device.attributes, "Power") {
                let writes = {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", power_attr_id), serde_json::json!(0));
                    h
                };
                self.queue_mgr
                    .send_zbsend(&device.gateway, &device.zigbee_addr, &writes, None);
                info!("Scene Close: Turned off AC {}", device.zigbee_addr);
            }
        }

        for device in dm.get_device_by_type("mcb") {
            if let Some(control_attr_id) = find_attr_id_by_label(&device.attributes, "Control") {
                let writes = {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", control_attr_id), serde_json::json!(0));
                    h
                };
                self.queue_mgr
                    .send_zbsend(&device.gateway, &device.zigbee_addr, &writes, Some(1));
                info!("Scene Close: Turned off Sign {}", device.zigbee_addr);
            }
        }
    }

    pub async fn publish_feedback(&self, device_addr: &str, state: &HashMap<String, Value>) {
        let dm = self.device_manager.read().await;
        let device = match dm.get_device(device_addr) {
            Some(d) => d,
            None => return,
        };

        let mut last_states = self.last_states.lock().await;
        let prev = last_states.entry(device_addr.to_string()).or_default();

        let mut changed = HashMap::new();
        for (k, v) in state {
            if prev.get(k) != Some(v) {
                changed.insert(k.clone(), v.clone());
            }
        }

        if changed.is_empty() {
            return;
        }

        *prev = state.clone();
        drop(last_states);

        let device_type = &device.nr_type;
        match device_type.as_str() {
            "ac_controller" => {
                if let Some(idx) = device.metadata.ac_index {
                    self.publish_ac_feedback(idx, &changed, device).await;
                }
            }
            "mcb" => {
                if let Some(idx) = device.metadata.sign_index {
                    self.publish_sign_feedback(idx, &changed, device).await;
                }
            }
            "power_meter" => {
                if let Some(idx) = device.metadata.power_index {
                    self.publish_power_feedback(idx, &changed, device).await;
                }
            }
            "light_sensor" => {
                if let Some(idx) = device.metadata.light_sensor_index {
                    self.publish_light_feedback(idx, &changed, device).await;
                }
            }
            _ => {}
        }
    }

    async fn publish_ac_feedback(
        &self,
        idx: i64,
        state: &HashMap<String, Value>,
        device: &Device,
    ) {
        for (attr_id, attr_config) in &device.attributes {
            let label = &attr_config.label;
            if !state.contains_key(label) {
                continue;
            }
            if !attr_config.display {
                continue;
            }

            let value = &state[label];
            let payload = if attr_config.attr_type == "bool" {
                let val_bool = match value {
                    Value::Number(n) => n.as_i64().unwrap_or(0) != 0,
                    Value::String(s) => s.to_uppercase() == "ON" || s == "1" || s == "TRUE",
                    Value::Bool(b) => *b,
                    _ => false,
                };
                if val_bool { "ON".to_string() } else { "OFF".to_string() }
            } else {
                match value {
                    Value::String(s) => s.clone(),
                    other => other.to_string(),
                }
            };

            let topic = format!("bms/ac/{}/{}", idx, attr_id);
            self.mqtt_client
                .publish(&topic, &payload, QoS::AtLeastOnce, false)
                .await;
            debug!("HMI AC[{}] -> {} = {}", idx, topic, payload);
        }
    }

    async fn publish_sign_feedback(
        &self,
        idx: i64,
        state: &HashMap<String, Value>,
        device: &Device,
    ) {
        for (attr_id, attr_config) in &device.attributes {
            let label = &attr_config.label;
            if !state.contains_key(label) {
                continue;
            }
            if !attr_config.display {
                continue;
            }

            let value = &state[label];
            let payload = if attr_config.attr_type == "bool" {
                let val_bool = match value {
                    Value::Number(n) => n.as_i64().unwrap_or(0) != 0,
                    Value::String(s) => s.to_uppercase() == "ON" || s == "1" || s == "TRUE",
                    Value::Bool(b) => *b,
                    _ => false,
                };
                if val_bool { "ON".to_string() } else { "OFF".to_string() }
            } else {
                match value {
                    Value::String(s) => s.clone(),
                    other => other.to_string(),
                }
            };

            let topic = format!("bms/sign/{}/{}", idx, attr_id);
            self.mqtt_client
                .publish(&topic, &payload, QoS::AtLeastOnce, false)
                .await;
            debug!("HMI Sign[{}] -> {} = {}", idx, topic, payload);
        }
    }

    async fn publish_power_feedback(
        &self,
        idx: i64,
        state: &HashMap<String, Value>,
        device: &Device,
    ) {
        for (attr_id, attr_config) in &device.attributes {
            let label = &attr_config.label;
            if !state.contains_key(label) {
                continue;
            }
            if !attr_config.display {
                continue;
            }

            let value = &state[label];
            let payload = match value {
                Value::String(s) => s.clone(),
                other => other.to_string(),
            };

            let topic = format!("bms/power/{}/{}", idx, attr_id);
            self.mqtt_client
                .publish(&topic, &payload, QoS::AtLeastOnce, false)
                .await;
            info!("HMI Power[{}] -> {} = {}", idx, topic, payload);
        }
    }

    async fn publish_light_feedback(
        &self,
        idx: i64,
        state: &HashMap<String, Value>,
        device: &Device,
    ) {
        for (attr_id, attr_config) in &device.attributes {
            let label = &attr_config.label;
            if !state.contains_key(label) {
                continue;
            }
            if !attr_config.display {
                continue;
            }

            let value = &state[label];
            let payload = match value {
                Value::String(s) => s.clone(),
                other => other.to_string(),
            };

            let topic = format!("bms/light/{}/{}", idx, attr_id);
            self.mqtt_client
                .publish(&topic, &payload, QoS::AtLeastOnce, false)
                .await;
            info!("HMI Light[{}] -> {} = {}", idx, topic, payload);
        }
    }

    async fn get_device_by_index(&self, device_type: &str, idx: i64) -> Option<Device> {
        let dm = self.device_manager.read().await;
        let devices: Vec<Device> = dm
            .get_device_by_type(device_type)
            .into_iter()
            .cloned()
            .collect();

        for device in devices {
            let found = match device_type {
                "ac_controller" => device.metadata.ac_index == Some(idx),
                "mcb" => device.metadata.sign_index == Some(idx),
                "power_meter" => device.metadata.power_index == Some(idx),
                "light_sensor" => device.metadata.light_sensor_index == Some(idx),
                _ => false,
            };
            if found {
                return Some(device);
            }
        }
        None
    }
}

pub fn find_attr_id_by_label(attributes: &HashMap<String, crate::device_manager::AttributeConfig>, label: &str) -> Option<String> {
    for (attr_id, config) in attributes {
        if config.label == label {
            return Some(attr_id.clone());
        }
    }
    None
}
