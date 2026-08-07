#![allow(dead_code)]

mod command_tracker;
mod device_manager;
mod event_bus;
mod hmi_bridge;
mod http_api;
mod mqtt_client;
mod ota;
mod queue_manager;
mod rule_engine;
mod scheduler;
mod state_manager;
mod xsolar_bridge;

use command_tracker::CommandTracker;
use device_manager::{DeviceManager, SharedDeviceManager};
use event_bus::EventBus;
use hmi_bridge::HmiBridge;
use log::{error, info, warn};
use mqtt_client::{MqttClient, MqttMessage};
use ota::OtaUpdater;
use queue_manager::QueueManager;
use rule_engine::RuleEngine;
use serde::Deserialize;
use serde_json::Value;
use state_manager::StateManager;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{Mutex, RwLock};
use xsolar_bridge::XsolarBridge;

use rumqttc::QoS;

#[derive(Debug, Deserialize)]
struct MqttConfig {
    broker: String,
    port: u16,
    client_id: String,
    #[serde(default)]
    user: String,
    #[serde(default)]
    pass: String,
}

#[derive(Debug, Deserialize)]
struct XsolarConfig {
    broker: String,
    port: u16,
    #[serde(default)]
    user: String,
    #[serde(default)]
    pass: String,
    #[serde(default = "default_topic_prefix")]
    topic_prefix: String,
    #[serde(default = "default_push_interval")]
    push_interval: u64,
    #[serde(default = "default_site")]
    site: String,
}

fn default_topic_prefix() -> String {
    "smarteos/bluCafe".to_string()
}

/// Read credentials from env var (priority), else fall back to config value.
/// Empty string is returned as None (no auth sent to the broker).
fn cred_from_env(env_name: &str, config_val: &str) -> Option<String> {
    std::env::var(env_name)
        .ok()
        .filter(|v| !v.is_empty())
        .or_else(|| {
            if config_val.is_empty() {
                None
            } else {
                Some(config_val.to_string())
            }
        })
}
fn default_push_interval() -> u64 {
    600
}
fn default_site() -> String {
    "bluCafe".to_string()
}

#[derive(Debug, Deserialize)]
struct DatabaseConfig {
    path: String,
}

#[derive(Debug, Deserialize, Default)]
struct OtaAppConfig {
    #[serde(default)]
    enabled: bool,
    #[serde(default = "default_ota_url")]
    ota_url: String,
    #[serde(default = "default_install_dir")]
    install_dir: String,
    #[serde(default = "default_temp_dir")]
    temp_dir: String,
    #[serde(default = "default_backup_dir")]
    backup_dir: String,
    #[serde(default = "default_check_interval")]
    check_interval: u64,
    #[serde(default)]
    auto_update: bool,
}

fn default_ota_url() -> String {
    "http://192.168.1.171/ota/bms/check.json".to_string()
}
fn default_install_dir() -> String {
    "/home/pico/bms-engine".to_string()
}
fn default_temp_dir() -> String {
    "/tmp/bms_ota".to_string()
}
fn default_backup_dir() -> String {
    "/home/pico/bms_backup".to_string()
}
fn default_check_interval() -> u64 {
    3600
}

#[derive(Debug, Deserialize)]
struct AppConfig {
    mqtt: MqttConfig,
    xsolar: XsolarConfig,
    database: DatabaseConfig,
    #[serde(default = "default_offline_timeout")]
    offline_timeout: u64,
    #[serde(default = "default_devices_file")]
    devices_file: String,
    #[serde(default = "default_rules_file")]
    rules_file: String,
    #[serde(default)]
    ota: OtaAppConfig,
    #[serde(default)]
    http: HttpConfig,
}

fn default_offline_timeout() -> u64 {
    120
}
fn default_devices_file() -> String {
    "devices.yaml".to_string()
}
fn default_rules_file() -> String {
    "config/rules.yaml".to_string()
}

#[derive(Debug, Deserialize, Default)]
struct HttpConfig {
    #[serde(default)]
    enabled: bool,
    #[serde(default = "default_http_host")]
    host: String,
    #[serde(default = "default_http_port")]
    port: u16,
}

fn default_http_host() -> String {
    "127.0.0.1".to_string()
}
fn default_http_port() -> u16 {
    8080
}

struct OfflineTimeoutMap {
    default_timeout: u64,
}

impl OfflineTimeoutMap {
    fn get(&self, nr_type: &str) -> u64 {
        match nr_type {
            "ac_controller" => self.default_timeout.max(600),
            "mcb" => self.default_timeout.max(300),
            "light_sensor" => self.default_timeout.max(1800),
            "switch" => self.default_timeout.max(600),
            _ => self.default_timeout,
        }
    }
}

fn normalize_attribute_value(
    _attr_id: &str,
    raw_value: &Value,
    attr_config: &device_manager::AttributeConfig,
) -> Option<Value> {
    if attr_config.decode.is_some() {
        if let Value::String(hex_str) = raw_value {
            return Some(decode_composite(hex_str, attr_config.decode.as_ref().unwrap()));
        }
        return None;
    }

    let attr_type = &attr_config.attr_type;
    match attr_type.as_str() {
        "bool" => match raw_value {
            Value::Number(n) => Some(Value::Bool(n.as_f64().unwrap_or(0.0) != 0.0)),
            Value::String(s) => Some(Value::Bool(
                s.to_uppercase() == "ON" || s == "1" || s == "TRUE",
            )),
            Value::Bool(b) => Some(Value::Bool(*b)),
            _ => Some(Value::Bool(false)),
        },
        "number" => {
            let raw_str = raw_value.to_string();
            let value = if raw_str.starts_with("0x") || raw_str.starts_with("0X") {
                u64::from_str_radix(&raw_str[2..], 16).unwrap_or(0) as f64
            } else {
                match raw_value {
                    Value::Number(n) => n.as_f64().unwrap_or(0.0),
                    Value::String(s) => s.parse::<f64>().unwrap_or(0.0),
                    _ => 0.0,
                }
            };

            let value = match attr_config.formula.as_deref() {
                Some("zcl_illuminance") => {
                    if value > 0.0 && (value as u64) != 0xFFFF {
                        let lux = 10f64.powf((value - 1.0) / 10000.0);
                        (lux * 10.0).round() / 10.0
                    } else {
                        0.0
                    }
                }
                _ => value,
            };

            let value = match attr_config.scale {
                Some(scale) => value * scale,
                None => value,
            };

            Some(serde_json::json!(value))
        }
        "hex" => match raw_value {
            Value::String(s) => Some(Value::String(s.clone())),
            other => Some(Value::String(other.to_string())),
        },
        _ => Some(raw_value.clone()),
    }
}

fn decode_composite(hex_value: &str, decode_rules: &[device_manager::DecodeRule]) -> Value {
    let mut result = serde_json::Map::new();

    for rule in decode_rules {
        let (start, end) = rule.slice;
        let hex_slice = if start < hex_value.len() && end <= hex_value.len() {
            &hex_value[start..end]
        } else {
            continue;
        };

        let value = u64::from_str_radix(hex_slice, 16).unwrap_or(0) as f64;
        let value = match rule.scale {
            Some(scale) => value * scale,
            None => value,
        };

        result.insert(rule.id.clone(), serde_json::json!(value));
    }

    Value::Object(result)
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .format_timestamp_millis()
        .init();

    let config_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "config/config.yaml".to_string());

    let config_content = std::fs::read_to_string(&config_path)?;
    let config: AppConfig = serde_yaml::from_str(&config_content)?;

    info!("Initializing BMS Engine...");

    let device_manager: SharedDeviceManager =
        Arc::new(RwLock::new(DeviceManager::new(&config.devices_file)?));

    let state_manager = Arc::new(Mutex::new(StateManager::new(&config.database.path)?));

    let random_suffix: String = uuid::Uuid::new_v4()
        .to_string()
        .chars()
        .take(4)
        .collect();
    let local_client_id = format!("{}-{}", config.mqtt.client_id, random_suffix);

    // Credentials from env vars take priority (keeps secrets out of the
    // tracked config.yaml). Fall back to config values (usually empty).
    let local_user = cred_from_env("BMS_MQTT_USER", &config.mqtt.user);
    let local_pass = cred_from_env("BMS_MQTT_PASS", &config.mqtt.pass);

    let (mqtt_local, mut local_rx) = MqttClient::new(
        &config.mqtt.broker,
        config.mqtt.port,
        &local_client_id,
        local_user.as_deref(),
        local_pass.as_deref(),
    );
    let mqtt_local = Arc::new(mqtt_local);

    let xsolar_user = cred_from_env("BMS_XSOLAR_USER", &config.xsolar.user);
    let xsolar_pass = cred_from_env("BMS_XSOLAR_PASS", &config.xsolar.pass);
    // Unique client_id to avoid session-takeover conflicts with other instances
    // sharing the same base id on the xsolar broker.
    let xsolar_client_id = format!("{}_xsolar_{}", config.mqtt.client_id, random_suffix);

    let (mqtt_xsolar, mut xsolar_rx) = MqttClient::new(
        &config.xsolar.broker,
        config.xsolar.port,
        &xsolar_client_id,
        xsolar_user.as_deref(),
        xsolar_pass.as_deref(),
    );
    let mqtt_xsolar = Arc::new(mqtt_xsolar);

    let queue_mgr = Arc::new(QueueManager::new(mqtt_local.clone()));

    // In-process event bus: all bms/# state changes go here. The LVGL panel
    // consumes them over HTTP (SSE + /api/state) instead of local MQTT.
    let event_bus = EventBus::new(1024);

    let hmi_bridge = Arc::new(HmiBridge::new(
        event_bus.clone(),
        state_manager.clone(),
        device_manager.clone(),
        queue_mgr.clone(),
    ));

    let xsolar_bridge = Arc::new(XsolarBridge::new(
        mqtt_local.clone(),
        mqtt_xsolar.clone(),
        state_manager.clone(),
        device_manager.clone(),
        config.xsolar.topic_prefix.clone(),
        config.xsolar.site.clone(),
    ));

    // Tracks in-flight control commands so an optimistic UI update is only
    // acknowledged against a matching real report and cleaned up on timeout.
    let command_tracker = Arc::new(CommandTracker::new());

    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();

    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        info!("Received Ctrl+C, shutting down...");
        r.store(false, std::sync::atomic::Ordering::Relaxed);
    });

    let rule_engine = Arc::new(Mutex::new(RuleEngine::new(&config.rules_file)));

    // Channel carrying fired rule actions to a worker that publishes them.
    let (rule_action_tx, rule_action_rx) = tokio::sync::mpsc::unbounded_channel();
    rule_engine.lock().await.set_action_sender(rule_action_tx);
    let action_mqtt = mqtt_local.clone();
    let action_dm = device_manager.clone();
    let action_qm = queue_mgr.clone();
    tokio::spawn(async move {
        let mut rx = rule_action_rx;
        while let Some(action) = rx.recv().await {
            // Declarative device write: resolve (device, attr/attr label, value)
            // and send via the same queue path the HMI uses.
            if action.action_type == "device" {
                let written = {
                    let dm = action_dm.read().await;
                    resolve_device_action(&dm, &action, &action_qm)
                };
                if !written {
                    warn!(
                        "Rule action 'device' could not resolve: device={:?} attr={:?}",
                        action.device, action.attr
                    );
                }
                continue;
            }
            if let (Some(topic), Some(payload)) = (&action.topic, &action.payload) {
                let payload_str = match payload {
                    serde_json::Value::String(s) => s.clone(),
                    other => serde_json::to_string(other).unwrap_or_default(),
                };
                action_mqtt
                    .publish(topic, payload_str, QoS::AtLeastOnce, action.retain.unwrap_or(false))
                    .await;
            }
        }
    });

    // Periodic time-trigger evaluation for scheduler rules
    {
        let rule_engine = rule_engine.clone();
        let running = running.clone();
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(Duration::from_secs(5));
            ticker.tick().await;
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                ticker.tick().await;
                let mut rule_engine = rule_engine.lock().await;
                rule_engine.check_reload();
                rule_engine.process_time_tick();
            }
        });
    }

    // Start subsystems
    hmi_bridge.start().await;
    xsolar_bridge.start().await;
    queue_mgr.start();

    // Seed in-memory caches from SQLite once (review §4 / gap #3): prevents a
    // false event storm after restart and keeps devices that haven't reported
    // yet visible in the periodic xsolar snapshot.
    hmi_bridge.seed_from_db(&state_manager).await;
    xsolar_bridge.seed_from_db().await;

    // Periodic sweep of unconfirmed commands (review §5.3): entries older than
    // the timeout never produced a matching report -> clean up + log warning.
    {
        let command_tracker = command_tracker.clone();
        let running = running.clone();
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(Duration::from_secs(3));
            ticker.tick().await;
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                ticker.tick().await;
                let expired = command_tracker.sweep(Duration::from_secs(15)).await;
                for pc in expired {
                    warn!(
                        "[command] {} to {} timed out without confirmation (attr {}, expected {})",
                        pc.command_id, pc.device_addr, pc.attr_label, pc.expected_value
                    );
                }
            }
        });
    }

    // Subscribe to # on local broker
    mqtt_local.subscribe("#", QoS::AtMostOnce).await;

    // Device last seen tracking
    let device_last_seen: Arc<Mutex<HashMap<String, f64>>> =
        Arc::new(Mutex::new(HashMap::new()));
    // Device online-state tracking: drives offline->online (+->online=ON) emission
    let device_online: Arc<Mutex<HashMap<String, bool>>> =
        Arc::new(Mutex::new(HashMap::new()));
    let offline_timeout = config.offline_timeout;
    let offline_map = Arc::new(OfflineTimeoutMap {
        default_timeout: offline_timeout,
    });

    // Spawn periodic tasks
    {
        let xsolar_bridge = xsolar_bridge.clone();
        let running = running.clone();
        let interval = config.xsolar.push_interval;
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(Duration::from_secs(interval));
            ticker.tick().await;
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                ticker.tick().await;
                xsolar_bridge.push_all_states().await;
            }
        });
    }

    {
        let device_manager = device_manager.clone();
        let running = running.clone();
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(Duration::from_secs(5));
            ticker.tick().await;
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                ticker.tick().await;
                device_manager.write().await.check_reload();
            }
        });
    }

    {
        let mqtt_local = mqtt_local.clone();
        let device_manager = device_manager.clone();
        let running = running.clone();
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(Duration::from_secs(30));
            ticker.tick().await;
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                ticker.tick().await;
                let dm = device_manager.read().await;
                let power_devices = dm.get_device_by_type("power_meter");
                for device in power_devices {
                    // Read CurrentSummationDelivered (attr 0x0000)
                    let cmd = serde_json::json!({
                        "Device": device.zigbee_addr,
                        "Cluster": 1794,
                        "Endpoint": 1,
                        "Read": 0
                    });
                    mqtt_local
                        .publish_json(
                            &format!("cmnd/{}/ZbSend", device.gateway),
                            &cmd,
                            QoS::AtLeastOnce,
                            false,
                        )
                        .await;
                    // Read CurrentSummationReceived (attr 0x0001)
                    let cmd2 = serde_json::json!({
                        "Device": device.zigbee_addr,
                        "Cluster": 1794,
                        "Endpoint": 1,
                        "Read": 1
                    });
                    mqtt_local
                        .publish_json(
                            &format!("cmnd/{}/ZbSend", device.gateway),
                            &cmd2,
                            QoS::AtLeastOnce,
                            false,
                        )
                        .await;
                }
            }
        });
    }

    {
        let event_bus = event_bus.clone();
        let device_manager = device_manager.clone();
        let device_last_seen = device_last_seen.clone();
        let device_online = device_online.clone();
        let offline_map = offline_map.clone();
        let running = running.clone();
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(Duration::from_secs(60));
            ticker.tick().await;
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                ticker.tick().await;
                let now = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs_f64();
                let last_seen = device_last_seen.lock().await;
                let dm = device_manager.read().await;

                for (device_addr, device) in dm.get_all_devices() {
                    let last = last_seen.get(device_addr).copied().unwrap_or(0.0);
                    if last == 0.0 {
                        continue;
                    }
                    let timeout = offline_map.get(&device.nr_type) as f64;
                    if now - last > timeout {
                        let mut online = device_online.lock().await;
                        if online.get(device_addr).copied().unwrap_or(true) {
                            online.insert(device_addr.clone(), false);
                            drop(online);
                            emit_device_online(&device, "OFF", &event_bus).await;
                        }
                        warn!(
                            "Device {} ({}) OFFLINE - last seen {:.0}s ago",
                            device_addr, device.name, now - last
                        );
                    }
                }
            }
        });
    }

    // OTA update check
    if config.ota.enabled {
        let ota_config = config.ota;
        let running = running.clone();
        tokio::spawn(async move {
            let mut ticker =
                tokio::time::interval(Duration::from_secs(ota_config.check_interval));
            ticker.tick().await;
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                ticker.tick().await;
                check_ota_updates(
                    &ota_config,
                    ota_config.auto_update,
                );
            }
        });
    }

    // Handle xsolar messages
    {
        let xsolar_bridge = xsolar_bridge.clone();
        let running = running.clone();
        tokio::spawn(async move {
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                if let Some(msg) = xsolar_rx.recv().await {
                    xsolar_bridge
                        .handle_xsolar_message(&msg.topic, &msg.payload)
                        .await;
                }
            }
        });
    }

    info!("BMS Engine started successfully");

    // HTTP API for the LVGL Smart Panel (bms/# events over HTTP/SSE)
    if config.http.enabled {
        let event_bus = event_bus.clone();
        let hmi_bridge = hmi_bridge.clone();
        let device_manager = device_manager.clone();
        let state_manager = state_manager.clone();
        let command_tracker = command_tracker.clone();
        let device_online = device_online.clone();
        let http_host = config.http.host.clone();
        let http_port = config.http.port;
        tokio::spawn(async move {
            if let Err(e) = http_api::run(
                event_bus,
                hmi_bridge,
                device_manager,
                state_manager,
                command_tracker,
                device_online,
                &http_host,
                http_port,
            )
            .await
            {
                error!("HTTP API server error: {}", e);
            }
        });
    }

    // Main message handling loop
    {
        let hmi_bridge = hmi_bridge.clone();
        let xsolar_bridge = xsolar_bridge.clone();
        let state_manager = state_manager.clone();
        let device_manager = device_manager.clone();
        let rule_engine = rule_engine.clone();
        let event_bus = event_bus.clone();
        let device_last_seen = device_last_seen.clone();
        let device_online = device_online.clone();
        let command_tracker = command_tracker.clone();

        tokio::spawn(async move {
            while running.load(std::sync::atomic::Ordering::Relaxed) {
                if let Some(msg) = local_rx.recv().await {
                    process_mqtt_message(
                        &msg,
                        &hmi_bridge,
                        &xsolar_bridge,
                        &state_manager,
                        &device_manager,
                        &rule_engine,
                        &event_bus,
                        &device_last_seen,
                        &device_online,
                        &command_tracker,
                    )
                    .await;
                }
            }
        })
        .await?;
    }

    info!("BMS Engine stopped");
    Ok(())
}

async fn process_mqtt_message(
    msg: &MqttMessage,
    hmi_bridge: &Arc<HmiBridge>,
    xsolar_bridge: &Arc<XsolarBridge>,
    state_manager: &Arc<Mutex<StateManager>>,
    device_manager: &SharedDeviceManager,
    rule_engine: &Arc<Mutex<RuleEngine>>,
    event_bus: &EventBus,
    device_last_seen: &Arc<Mutex<HashMap<String, f64>>>,
    device_online: &Arc<Mutex<HashMap<String, bool>>>,
    command_tracker: &Arc<CommandTracker>,
) {
    let topic = &msg.topic;
    let payload = &msg.payload;

    // Route HMI commands -> HmiBridge
    if topic.starts_with("bms/") && (topic.contains("/set") || topic == "bms/scene/master") {
        hmi_bridge.handle_message(topic, payload).await;
        return;
    }

    // Handle Tasmota LWT (gateway online/offline)
    if topic.contains("/LWT") {
        if let Some(is_online) = payload.as_str() {
            handle_gateway_lwt(
                topic,
                is_online.to_uppercase() == "ONLINE",
                event_bus,
                device_manager,
                device_online,
            )
            .await;
        }
    }

    // Process Tasmota telemetry (tele/+/SENSOR -> ZbReceived)
    if topic.starts_with("tele/") && topic.contains("/SENSOR") {
        process_zigbee_message(
            topic,
            payload,
            device_manager,
            state_manager,
            hmi_bridge,
            xsolar_bridge,
            event_bus,
            device_last_seen,
            device_online,
            rule_engine,
            command_tracker,
        )
        .await;
    }

    // Evaluate rules for dict payloads
    if payload.is_object() {
        rule_engine.lock().await.process_message(topic, payload);
    }
}

async fn process_zigbee_message(
    topic: &str,
    payload: &Value,
    device_manager: &SharedDeviceManager,
    state_manager: &Arc<Mutex<StateManager>>,
    hmi_bridge: &Arc<HmiBridge>,
    xsolar_bridge: &Arc<XsolarBridge>,
    event_bus: &EventBus,
    device_last_seen: &Arc<Mutex<HashMap<String, f64>>>,
    device_online: &Arc<Mutex<HashMap<String, bool>>>,
    rule_engine: &Arc<Mutex<RuleEngine>>,
    command_tracker: &Arc<CommandTracker>,
) {
    let zb_received = payload.get("ZbReceived");

    if let Some(obj) = zb_received.and_then(|v| v.as_object()) {
        // Collect device info while holding read lock, then release
        let tasks: Vec<(String, serde_json::Map<String, Value>, device_manager::Device)> = {
            let dm = device_manager.read().await;
            obj.iter()
                .filter_map(|(addr, data)| {
                    dm.get_device(addr)
                        .cloned()
                        .map(|d| (addr.clone(), data.as_object().cloned().unwrap_or_default(), d))
                })
                .collect()
        };

        for (device_addr, device_data_raw, device) in tasks {
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs_f64();
            device_last_seen.lock().await.insert(device_addr.clone(), now);

            // Emit online=ON on the offline->online transition so the HMI dot
            // goes green the moment a device reports telemetry again.
            {
                let mut online = device_online.lock().await;
                if !online.get(&device_addr).copied().unwrap_or(false) {
                    online.insert(device_addr.clone(), true);
                    drop(online);
                    emit_device_online(&device, "ON", event_bus).await;
                }
            }

            let device_data = Value::Object(device_data_raw);

            // Normalise every attribute once. The resulting "new_state" is the
            // single source of truth for all downstream branches (review §3).
            let mut batch_metrics: Vec<(String, String, Value, String, String, String)> = Vec::new();
            let mut new_state: HashMap<String, Value> = HashMap::new();

            for (attr_id, attr_config) in &device.attributes {
                let raw_value = device_data
                    .get(attr_id)
                    .or_else(|| device_data.get(&format!("EF00/{}", attr_id)));

                if let Some(raw) = raw_value {
                    if let Some(value) = normalize_attribute_value(attr_id, raw, attr_config) {
                        if let Value::Object(decoded) = &value {
                            for (decoded_id, decoded_value) in decoded {
                                if let Some(decoded_config) =
                                    get_decoded_attr_config(attr_config, decoded_id)
                                {
                                    let label = decoded_config
                                        .label
                                        .clone()
                                        .unwrap_or_else(|| decoded_id.clone());
                                    batch_metrics.push((
                                        device_addr.clone(),
                                        label.clone(),
                                        decoded_value.clone(),
                                        "number".to_string(),
                                        device.nr_type.clone(),
                                        attr_id.clone(),
                                    ));
                                    new_state.insert(label, decoded_value.clone());
                                }
                            }
                        } else {
                            let label = attr_config.label.clone();
                            batch_metrics.push((
                                device_addr.clone(),
                                label.clone(),
                                value.clone(),
                                attr_config.attr_type.clone(),
                                device.nr_type.clone(),
                                attr_id.clone(),
                            ));
                            new_state.insert(label, value);
                        }
                    }
                }
            }

            // ---- Fan-out (review §3): independent branches, same value. ----

            // 1) SQLite write-only (history/audit). Spawned so a slow disk does
            //    not delay the UI feedback branch.
            {
                let sm = state_manager.clone();
                let db_addr = device_addr.clone();
                let db_metrics = batch_metrics;
                let db_payload = device_data.clone();
                let db_nr = device.nr_type.clone();
                let db_group = device.group.clone();
                tokio::spawn(async move {
                    let sm = sm.lock().await;
                    sm.batch_update_metrics(&db_metrics);
                    sm.log_event(
                        &db_addr,
                        "state_update",
                        &db_payload,
                        Some(&db_nr),
                        Some(&db_group),
                    );
                });
            }

            // 2) Reconcile command ack against this real report before the HMI
            //    event is emitted, so the SSE ack reflects the actual state.
            command_tracker.reconcile(&device_addr, &new_state).await;

            // 3) Update the xsolar in-memory cache (no network) then emit.
            xsolar_bridge.update_cache(&device_addr, &new_state);
            hmi_bridge.publish_feedback(&device_addr, &new_state).await;
            xsolar_bridge.push_device_state(&device_addr).await;
        }
    }

    // Evaluate rules
    if payload.is_object() {
        rule_engine.lock().await.process_message(topic, payload);
    }
}

async fn handle_gateway_lwt(
    topic: &str,
    is_online: bool,
    event_bus: &EventBus,
    device_manager: &SharedDeviceManager,
    device_online: &Arc<Mutex<HashMap<String, bool>>>,
) {
    let parts: Vec<&str> = topic.split('/').collect();
    if parts.len() < 2 {
        return;
    }
    let gateway = parts[1];

    let dm = device_manager.read().await;
    let mut online = device_online.lock().await;
    for (device_addr, device) in dm.get_all_devices() {
        // A gateway going OFFLINE takes all its devices offline immediately.
        // On ONLINE, do NOT force devices online: a device is shown online only
        // once it reports telemetry again (see process_zigbee_message).
        if device.gateway == gateway && !is_online {
            online.insert(device_addr.clone(), false);
            emit_device_online(&device, "OFF", event_bus).await;
        }
    }

    info!(
        "Gateway {} {}",
        gateway,
        if is_online { "ONLINE" } else { "OFFLINE" }
    );
}

async fn emit_device_online(device: &device_manager::Device, status: &str, event_bus: &EventBus) {
    match device.nr_type.as_str() {
        "ac_controller" => {
            if let Some(idx) = device.metadata.ac_index {
                event_bus
                    .emit(&format!("bms/ac/{}/online", idx), serde_json::json!(status))
                    .await;
            }
        }
        "mcb" => {
            if let Some(idx) = device.metadata.sign_index {
                event_bus
                    .emit(&format!("bms/sign/{}/online", idx), serde_json::json!(status))
                    .await;
            }
        }
        "power_meter" => {
            if let Some(idx) = device.metadata.power_index {
                event_bus
                    .emit(&format!("bms/power/{}/online", idx), serde_json::json!(status))
                    .await;
            }
        }
        "light_sensor" => {
            if let Some(idx) = device.metadata.light_sensor_index {
                event_bus
                    .emit(&format!("bms/light/{}/online", idx), serde_json::json!(status))
                    .await;
            }
        }
        _ => {}
    }
}

fn get_decoded_attr_config(
    attr_config: &device_manager::AttributeConfig,
    decoded_id: &str,
) -> Option<device_manager::DecodeRule> {
    attr_config
        .decode
        .as_ref()
        .and_then(|rules| rules.iter().find(|r| r.id == decoded_id).cloned())
}

fn check_ota_updates(ota_config: &OtaAppConfig, auto_update: bool) {
    let version_path = PathBuf::from("VERSION");
    let current_version = std::fs::read_to_string(&version_path)
        .unwrap_or_else(|_| "0.0.0".to_string())
        .trim()
        .to_string();

    let mut updater = OtaUpdater::new(
        current_version,
        ota_config.ota_url.clone(),
        PathBuf::from(&ota_config.install_dir),
        PathBuf::from(&ota_config.temp_dir),
        PathBuf::from(&ota_config.backup_dir),
        auto_update,
    );

    info!("Checking for OTA updates...");

    match updater.check_update() {
        Ok((true, new_version)) => {
            info!("OTA update available: {}", new_version);
            if auto_update {
                info!("Starting OTA update process...");
                match updater.download_update() {
                    Ok(true) => {
                        match updater.install_update() {
                            Ok(true) => info!("OTA update completed successfully"),
                            Ok(false) => error!("OTA installation failed"),
                            Err(e) => error!("OTA installation error: {}", e),
                        }
                    }
                    Ok(false) => error!("OTA download failed"),
                    Err(e) => error!("OTA download error: {}", e),
                }
            } else {
                info!("Auto-update disabled, skipping update");
            }
        }
        Ok((false, _)) => info!("No OTA update available"),
        Err(e) => error!("OTA update check failed: {}", e),
    }
}

/// Resolve a declarative `device` rule action (device + attr + value) into a
/// Zigbee write via the queue manager. Returns true if dispatched.
fn resolve_device_action(
    dm: &DeviceManager,
    action: &rule_engine::RuleAction,
    queue_mgr: &QueueManager,
) -> bool {
    let Some(dev_value) = &action.device else { return false };
    let Some(attr) = &action.attr else { return false };
    let Some(val) = &action.value else { return false };

    // Find device by zigbee address or by name.
    let device = dm
        .devices
        .get(dev_value)
        .or_else(|| {
            dm.devices
                .values()
                .find(|d| d.name.eq_ignore_ascii_case(dev_value) || d.nr_type.eq_ignore_ascii_case(dev_value))
        });

    let Some(device) = device else { return false };

    // attr is given as YAML id (e.g. "0110") or label (e.g. "Control").
    let attr_id = if device.attributes.contains_key(attr.as_str()) {
        attr.clone()
    } else {
        match hmi_bridge::find_attr_id_by_label(&device.attributes, attr) {
            Some(id) => id,
            None => return false,
        }
    };

    let value = match val {
        Value::Bool(b) => if *b { 1 } else { 0 },
        Value::Number(n) => n.as_i64().unwrap_or(0) as i64,
        Value::String(s) => match s.as_str() {
            "ON" | "on" | "true" | "1" => 1,
            "OFF" | "off" | "false" | "0" => 0,
            other => other.parse::<i64>().unwrap_or(0),
        },
        _ => return false,
    };

    let mut write_dict = HashMap::new();
    write_dict.insert(format!("EF00/{}", attr_id), serde_json::json!(value));
    queue_mgr.send_zbsend(&device.gateway, &device.zigbee_addr, &write_dict);
    info!(
        "Rule device action: {} {}={} -> OUT QUEUED",
        device.zigbee_addr, attr_id, value
    );
    true
}
