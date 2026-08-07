use dashmap::DashMap;
use log::{debug, info, warn};
use rumqttc::QoS;
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

use crate::device_manager::SharedDeviceManager;
use crate::mqtt_client::MqttClient;
use crate::state_manager::StateManager;

pub struct XsolarBridge {
    mqtt_local: Arc<MqttClient>,
    mqtt_xsolar: Arc<MqttClient>,
    state_manager: Arc<Mutex<StateManager>>,
    device_manager: SharedDeviceManager,
    last_push: Mutex<HashMap<String, f64>>,
    min_push_interval: f64,
    /// In-memory cache of the last known state per device (zigbee addr ->
    /// label -> value). Written by the ingest fan-out, read by both the
    /// real-time push and the periodic 10-min snapshot push. Never reads DB.
    cache: DashMap<String, HashMap<String, Value>>,
}

impl XsolarBridge {
    pub fn new(
        mqtt_local: Arc<MqttClient>,
        mqtt_xsolar: Arc<MqttClient>,
        state_manager: Arc<Mutex<StateManager>>,
        device_manager: SharedDeviceManager,
    ) -> Self {
        Self {
            mqtt_local,
            mqtt_xsolar,
            state_manager,
            device_manager,
            last_push: Mutex::new(HashMap::new()),
            min_push_interval: 5.0,
            cache: DashMap::new(),
        }
    }

    pub async fn start(&self) {
        self.mqtt_xsolar
            .subscribe("smarteos/bluCafe/+/set", QoS::AtLeastOnce)
            .await;
        info!("Xsolar Bridge started - subscribed to smarteos/bluCafe/+/set");
    }

    pub async fn handle_xsolar_message(&self, topic: &str, payload: &Value) {
        let parts: Vec<&str> = topic.split('/').collect();

        if parts.len() == 4
            && parts[0] == "smarteos"
            && parts[1] == "bluCafe"
            && parts[3] == "set"
        {
            let device_id = parts[2];
            self.handle_remote_command(device_id, payload).await;
        }
    }

    async fn handle_remote_command(&self, device_id: &str, payload: &Value) {
        let cmd: Value = match payload {
            Value::String(s) => {
                match serde_json::from_str::<Value>(s) {
                    Ok(v) => v,
                    Err(_) => {
                        warn!("Invalid JSON from xsolar: {}", s);
                        return;
                    }
                }
            }
            other => other.clone(),
        };

        let dm = self.device_manager.read().await;
        let device = match dm.get_device(device_id) {
            Some(d) => d,
            None => {
                warn!("Remote command: device not found: {}", device_id);
                return;
            }
        };

        let mut writes = HashMap::new();

        for (attr_id, attr_config) in &device.attributes {
            let label = &attr_config.label;
            if let Some(raw_val) = cmd.get(label) {
                let value = if attr_config.attr_type == "bool" {
                    match raw_val {
                        Value::String(s) if s.to_uppercase() == "ON" || s == "TRUE" || s == "1" => serde_json::json!(1),
                        _ => serde_json::json!(0),
                    }
                } else {
                    match raw_val {
                        Value::Number(n) => serde_json::json!(n.as_f64().unwrap_or(0.0) as i64),
                        Value::String(s) => serde_json::json!(s.parse::<f64>().unwrap_or(0.0) as i64),
                        _ => serde_json::json!(0),
                    }
                };
                writes.insert(format!("EF00/{}", attr_id), value);
            }

            if let Some(decode_rules) = &attr_config.decode {
                for rule in decode_rules {
                    let rlabel = rule.label.as_deref().unwrap_or("");
                    if let Some(raw_val) = cmd.get(rlabel) {
                        let value = match raw_val {
                            Value::Number(n) => serde_json::json!(n.as_f64().unwrap_or(0.0) as i64),
                            Value::String(s) => serde_json::json!(s.parse::<f64>().unwrap_or(0.0) as i64),
                            _ => serde_json::json!(0),
                        };
                        writes.insert(format!("EF00/{}", attr_id), value);
                    }
                }
            }
        }

        if writes.is_empty() {
            warn!("Remote command: no matching attributes for {}", device_id);
            return;
        }

        let zb_send = serde_json::json!({
            "Device": device_id,
            "Write": writes,
            "Endpoint": 1,
        });

        let topic = format!("cmnd/{}/ZbSend", device.gateway);
        self.mqtt_local
            .publish_json(&topic, &zb_send, QoS::AtLeastOnce, false)
            .await;

        let sm = self.state_manager.lock().await;
        sm.log_event(
            device_id,
            "remote_command",
            &serde_json::json!({"source": "xsolar", "command": cmd, "writes": writes}),
            Some(&device.nr_type),
            Some(&device.group),
        );

        info!(
            "Remote command from xsolar: {} {} -> {:?}",
            device_id, device.name, cmd
        );
    }

    /// Write-only cache update from the ingest fan-out (no network, no backpressure).
    pub fn update_cache(&self, device_addr: &str, updates: &HashMap<String, Value>) {
        let mut entry = self.cache.entry(device_addr.to_string()).or_default();
        for (k, v) in updates {
            entry.insert(k.clone(), v.clone());
        }
    }

    /// Seed the cache from SQLite once at engine startup so devices that have
    /// not yet reported after a restart still appear in the periodic push
    /// (review §4). Uses the fixed window-function query via get_latest_state().
    pub async fn seed_from_db(&self) {
        let addrs: Vec<String> = {
            let dm = self.device_manager.read().await;
            dm.get_all_devices().keys().cloned().collect()
        };
        let sm = self.state_manager.lock().await;
        for addr in addrs {
            let state = sm.get_latest_state(&addr);
            if !state.is_empty() {
                self.update_cache(&addr, &state);
            }
        }
        info!("Xsolar Bridge: seeded cache for {} devices", self.cache.len());
    }

    /// Real-time on-change push, throttled per device. Reads the in-memory cache.
    pub async fn push_device_state(&self, device_addr: &str) {
        let dm = self.device_manager.read().await;
        let device = match dm.get_device(device_addr) {
            Some(d) => d,
            None => return,
        };

        let now = chrono::Utc::now().timestamp() as f64;
        {
            let last_push = self.last_push.lock().await;
            let last = last_push.get(device_addr).copied().unwrap_or(0.0);
            if now - last < self.min_push_interval {
                return;
            }
        }

        let state = self.cache.get(device_addr).map(|r| r.clone()).unwrap_or_default();
        if state.is_empty() {
            return;
        }

        let xsolar_data = build_xsolar_data(device, &state);
        if xsolar_data.is_empty() {
            return;
        }

        let payload = serde_json::json!({
            "ts": format!("{}+07:00", chrono::Local::now().format("%Y-%m-%dT%H:%M:%S")),
            "site": "bluCafe",
            "id": device_addr,
            "type": device.nr_type,
            "name": device.name,
            "location": device.group,
            "data": xsolar_data,
        });

        let topic = format!("smarteos/bluCafe/{}", device_addr);
        self.mqtt_xsolar
            .publish_json(&topic, &payload, QoS::AtMostOnce, false)
            .await;

        {
            let mut last_push = self.last_push.lock().await;
            last_push.insert(device_addr.to_string(), now);
        }

        info!(
            "Event push: {} -> xsolar ({} attrs)",
            device_addr,
            xsolar_data.len()
        );
    }

    /// Periodic full-snapshot push (default every 10 min). Reads the in-memory
    /// cache, one publish per device — no DB reads on the hot path.
    pub async fn push_all_states(&self) {
        info!("Periodic push: pushing all device states to xsolar...");
        let devices: Vec<crate::device_manager::Device> = {
            let dm = self.device_manager.read().await;
            dm.get_all_devices().values().cloned().collect()
        };

        for device in &devices {
            let device_addr = &device.zigbee_addr;
            let state = self.cache.get(device_addr).map(|r| r.clone()).unwrap_or_default();
            if state.is_empty() {
                debug!("No state for {}, skipping", device_addr);
                continue;
            }

            let xsolar_data = build_xsolar_data(device, &state);
            if xsolar_data.is_empty() {
                debug!("No xsolar data for {}, skipping", device_addr);
                continue;
            }

            let payload = serde_json::json!({
                "ts": format!("{}+07:00", chrono::Local::now().format("%Y-%m-%dT%H:%M:%S")),
                "site": "bluCafe",
                "id": device_addr,
                "type": device.nr_type,
                "name": device.name,
                "location": device.group,
                "data": xsolar_data,
            });

            let topic = format!("smarteos/bluCafe/{}", device_addr);
            let _ = self
                .mqtt_xsolar
                .publish_json(&topic, &payload, QoS::AtMostOnce, false)
                .await;

            info!(
                "Periodic push: {} -> xsolar ({} attrs)",
                device_addr,
                xsolar_data.len()
            );
        }

        self.last_push.lock().await.clear();
    }
}

fn build_xsolar_data(
    device: &crate::device_manager::Device,
    state: &HashMap<String, Value>,
) -> serde_json::Map<String, Value> {
    let mut data = serde_json::Map::new();

    for (_attr_id, attr_config) in &device.attributes {
        if !attr_config.xsolar {
            continue;
        }

        if let Some(decode_rules) = &attr_config.decode {
            for rule in decode_rules {
                if rule.xsolar {
                    if let Some(value) = state.get(&rule.id) {
                        data.insert(
                            rule.label.clone().unwrap_or_else(|| rule.id.clone()),
                            value.clone(),
                        );
                    }
                }
            }
        } else if let Some(value) = state.get(&attr_config.label) {
            data.insert(attr_config.label.clone(), value.clone());
        }
    }

    data
}
