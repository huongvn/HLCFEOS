use log::{debug, info, warn};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

use crate::device_manager::{Device, SharedDeviceManager};
use crate::event_bus::EventBus;
use crate::queue_manager::QueueManager;
use crate::state_manager::StateManager;

pub struct HmiBridge {
    bus: EventBus,
    device_manager: SharedDeviceManager,
    queue_mgr: Arc<QueueManager>,
    last_states: Mutex<HashMap<String, HashMap<String, Value>>>,
}

/// Result of a device control action, used by the HTTP layer to register a
/// pending command that will be reconciled against the real device report
/// (review §5.3). Carries the expected attribute label + value.
pub struct ActionReport {
    pub attr_label: String,
    pub expected_value: i64,
}

impl HmiBridge {
    pub fn new(
        bus: EventBus,
        _state_manager: Arc<Mutex<StateManager>>,
        device_manager: SharedDeviceManager,
        queue_mgr: Arc<QueueManager>,
    ) -> Self {
        Self {
            bus,
            device_manager,
            queue_mgr,
            last_states: Mutex::new(HashMap::new()),
        }
    }

    pub async fn start(&self) {
        // Subscriptions live in main.rs (mqtt_local.subscribe("#") covers all
        // topics). Do NOT re-subscribe here: overlapping subscriptions make
        // NanoMQ deliver one copy per match, causing duplicate command handling.
        info!("HMI Bridge started");
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

/// Execute a high-level control action on a device (new /api/v1 API).
    /// Returns Ok(ActionReport) on dispatch, Err(message) on invalid action/params.
    pub async fn execute_action(
        &self,
        device: &Device,
        action: &str,
        params: &Value,
    ) -> Result<ActionReport, String> {
        // Locate the bool control attribute (Power / Control) for the device.
        let bool_ctrl = find_bool_control(&device.attributes);

        match action {
            "TURN_ON" | "TURN_OFF" => {
                let attr_id = bool_ctrl
                    .clone()
                    .ok_or("device has no bool control attribute")?;
                let value = if action == "TURN_ON" { 1 } else { 0 };
                self.do_write(device, &attr_id, &device.attributes[&attr_id], value, format!(
                    "TURN_ON/OFF -> {}",
                    attr_id
                ))
                .await;
                Ok(ActionReport {
                    attr_label: device.attributes[&attr_id].label.clone(),
                    expected_value: value,
                })
            }
            "TOGGLE" => {
                let attr_id = bool_ctrl
                    .clone()
                    .ok_or("không tìm thấy thuộc tính điều khiển (Power/Control)")?;
                let cfg = device.attributes.get(&attr_id).ok_or("attr not found")?;
                let current = self.current_bool(device, &cfg.label).await;
                let value = if current.unwrap_or(false) { 0 } else { 1 };
                self.do_write(device, &attr_id, cfg, value, format!("TOGGLE -> {}", attr_id))
                    .await;
                Ok(ActionReport {
                    attr_label: cfg.label.clone(),
                    expected_value: value,
                })
            }
            "SET_ATTRIBUTE" => {
                let attr = params
                    .get("attr")
                    .and_then(|v| v.as_str())
                    .ok_or_else(|| "SET_ATTRIBUTE requires 'attr'".to_string())?;
                let value = params
                    .get("value")
                    .ok_or_else(|| "SET_ATTRIBUTE requires 'value'".to_string())?;

                // attr given as label; resolve to YAML attr_id
                let attr_id = self
                    .find_attr_id_by_label_in_device(device, attr)
                    .ok_or_else(|| format!("không tìm thấy thuộc tính '{}'", attr))?;
                let cfg = device.attributes.get(&attr_id).ok_or("no config")?;
                if !cfg.control {
                    return Err(format!("thuộc tính '{}' không điều khiển được", attr));
                }

                let int_val = match value {
                    Value::Number(n) => n.as_i64().unwrap_or(0),
                    Value::String(s) => s.parse::<i64>().unwrap_or(0),
                    _ => {
                        return Err("value phải là số".to_string());
                    }
                };
                self.do_write(device, &attr_id, cfg, int_val, format!("SET_ATTRIBUTE {}={}", attr_id, int_val))
                    .await;
                Ok(ActionReport {
                    attr_label: cfg.label.clone(),
                    expected_value: int_val,
                })
            }
            _ => Err(format!("hành động '{}' không được hỗ trợ", action)),
        }
    }

    async fn do_write(
        &self,
        device: &Device,
        attr_id: &str,
        cfg: &crate::device_manager::AttributeConfig,
        value: i64,
        comment: String,
    ) {
        let mut write_dict = HashMap::new();
        write_dict.insert(format!("EF00/{}", attr_id), serde_json::json!(value));
        self.queue_mgr
            .send_zbsend(&device.gateway, &device.zigbee_addr, &write_dict);
        info!(
            "Action {}: {} {}={}",
            comment, device.zigbee_addr, attr_id, value
        );

        // Optimistic feedback so the front-end sees the change immediately.
        let feedback_payload = if cfg.attr_type == "bool" {
            if value == 1 { "ON".to_string() } else { "OFF".to_string() }
        } else {
            value.to_string()
        };
        if let Some((type_prefix, idx)) = self.device_topic(device) {
            let feedback_topic = format!("bms/{}/{}/{}", type_prefix, idx, attr_id);
            self.bus
                .emit(&feedback_topic, serde_json::json!(feedback_payload))
                .await;
        }
    }

    /// Current boolean value (by label) from the latest cached state.
    async fn current_bool(&self, device: &Device, label: &str) -> Option<bool> {
        let states = self.last_states.lock().await;
        let dev = states.get(&device.zigbee_addr)?;
        let v = dev.get(label)?;
        match v {
            Value::Bool(b) => Some(*b),
            Value::Number(n) => Some(n.as_i64().unwrap_or(0) != 0),
            Value::String(s) => Some(s.eq_ignore_ascii_case("ON") || s == "1" || s.eq_ignore_ascii_case("true")),
            _ => None,
        }
    }

    /// Compose the bms type+index topic segment for a device (for feedback).
    fn device_topic(&self, device: &Device) -> Option<(&'static str, i64)> {
        match device.nr_type.as_str() {
            "ac_controller" => device.metadata.ac_index.map(|i| ("ac", i)),
            "mcb" => device.metadata.sign_index.map(|i| ("sign", i)),
            "power_meter" => device.metadata.power_index.map(|i| ("power", i)),
            "light_sensor" => device.metadata.light_sensor_index.map(|i| ("light", i)),
            _ => None,
        }
    }

    fn find_attr_id_by_label_in_device(&self, device: &Device, label: &str) -> Option<String> {
        for (attr_id, cfg) in &device.attributes {
            // Accept both the human-readable label ("Temperature") and the raw
            // attr_id ("0202"). The HMI sends the ID in SET_ATTRIBUTE params.
            if cfg.label == label || attr_id == label {
                return Some(attr_id.clone());
            }
        }
        None
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
            .send_zbsend(&device.gateway, &device.zigbee_addr, &write_dict);

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
        self.bus
            .emit(&feedback_topic, serde_json::json!(feedback_payload))
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
            .send_zbsend(&device.gateway, &device.zigbee_addr, &write_dict);

        info!(
            "Sign command: {} {}={:?}",
            device.zigbee_addr, attr, payload
        );

        let feedback_payload = if value == 1 { "ON".to_string() } else { "OFF".to_string() };
        let feedback_topic = format!("bms/sign/{}/{}", idx, attr);
        self.bus
            .emit(&feedback_topic, serde_json::json!(feedback_payload))
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

    /// Stagger between consecutive ZbSend commands to avoid bursting the
    /// Zigbee mesh (review §5.4 D4). Configurable in ms.
    const SCENE_STAGGER_MS: u64 = 200;

    pub async fn execute_scene_open(&self) {
        let jobs = {
            let dm = self.device_manager.read().await;
            let mut jobs = Vec::new();
            for device in dm.get_device_by_type("ac_controller") {
                if let Some(power_attr_id) = find_attr_id_by_label(&device.attributes, "Power") {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", power_attr_id), serde_json::json!(1));
                    jobs.push((device.gateway.clone(), device.zigbee_addr.clone(), h));
                    info!("Scene Open: Turned on AC {}", device.zigbee_addr);
                }
            }
            for device in dm.get_device_by_type("mcb") {
                if let Some(control_attr_id) = find_attr_id_by_label(&device.attributes, "Control") {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", control_attr_id), serde_json::json!(1));
                    jobs.push((device.gateway.clone(), device.zigbee_addr.clone(), h));
                    info!("Scene Open: Turned on Sign {}", device.zigbee_addr);
                }
            }
            jobs
        };

        for (gateway, addr, writes) in jobs {
            self.queue_mgr.send_zbsend(&gateway, &addr, &writes);
            tokio::time::sleep(std::time::Duration::from_millis(Self::SCENE_STAGGER_MS)).await;
        }
    }

    pub async fn execute_scene_close(&self) {
        let jobs = {
            let dm = self.device_manager.read().await;
            let mut jobs = Vec::new();
            for device in dm.get_device_by_type("ac_controller") {
                if let Some(power_attr_id) = find_attr_id_by_label(&device.attributes, "Power") {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", power_attr_id), serde_json::json!(0));
                    jobs.push((device.gateway.clone(), device.zigbee_addr.clone(), h));
                    info!("Scene Close: Turned off AC {}", device.zigbee_addr);
                }
            }
            for device in dm.get_device_by_type("mcb") {
                if let Some(control_attr_id) = find_attr_id_by_label(&device.attributes, "Control") {
                    let mut h = HashMap::new();
                    h.insert(format!("EF00/{}", control_attr_id), serde_json::json!(0));
                    jobs.push((device.gateway.clone(), device.zigbee_addr.clone(), h));
                    info!("Scene Close: Turned off Sign {}", device.zigbee_addr);
                }
            }
            jobs
        };

        for (gateway, addr, writes) in jobs {
            self.queue_mgr.send_zbsend(&gateway, &addr, &writes);
            tokio::time::sleep(std::time::Duration::from_millis(Self::SCENE_STAGGER_MS)).await;
        }
    }

    /// Seed the last_states cache from SQLite once at engine startup so the
    /// first report of each attribute after restart is NOT treated as a
    /// "change" (prevents the false event storm described in review §2 gap #3).
    pub async fn seed_from_db(&self, state_manager: &Arc<Mutex<StateManager>>) {
        let addrs: Vec<String> = {
            let dm = self.device_manager.read().await;
            dm.get_all_devices().keys().cloned().collect()
        };
        let sm = state_manager.lock().await;
        let mut last_states = self.last_states.lock().await;
        for addr in addrs {
            let state = sm.get_latest_state(&addr);
            if !state.is_empty() {
                last_states.insert(addr, state);
            }
        }
        info!("HMI Bridge: seeded last_states for {} devices", last_states.len());
    }

    /// Diff a freshly normalised report against the cached last state and emit
    /// only the attributes that actually changed. `updates` carries the values
    /// from THIS message (already computed by normalize) — nothing reads the DB.
    pub async fn publish_feedback(&self, device_addr: &str, updates: &HashMap<String, Value>) {
        let dm = self.device_manager.read().await;
        let device = match dm.get_device(device_addr) {
            Some(d) => d,
            None => return,
        };

        let mut last_states = self.last_states.lock().await;
        let prev = last_states.entry(device_addr.to_string()).or_default();

        let mut changed = HashMap::new();
        for (k, v) in updates {
            if prev.get(k) != Some(v) {
                changed.insert(k.clone(), v.clone());
            }
        }

        if changed.is_empty() {
            return;
        }

        for (k, v) in updates {
            prev.insert(k.clone(), v.clone());
        }
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
            self.bus
                .emit(&topic, serde_json::json!(payload))
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
            self.bus
                .emit(&topic, serde_json::json!(payload))
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
            self.bus
                .emit(&topic, serde_json::json!(payload))
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
            self.bus
                .emit(&topic, serde_json::json!(payload))
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

/// Find the bool control attribute id (Power / Control) for a device.
fn find_bool_control(attributes: &HashMap<String, crate::device_manager::AttributeConfig>) -> Option<String> {
    // Prefer control-flagged bool attrs.
    for (attr_id, config) in attributes {
        if config.attr_type == "bool" && config.control {
            return Some(attr_id.clone());
        }
    }
    // Fall back to a bool attribute named Power/Control.
    for (attr_id, config) in attributes {
        if config.attr_type == "bool"
            && (config.label.eq_ignore_ascii_case("power") || config.label.eq_ignore_ascii_case("control"))
        {
            return Some(attr_id.clone());
        }
    }
    None
}
