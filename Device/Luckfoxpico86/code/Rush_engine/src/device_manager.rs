use log::info;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AttributeConfig {
    pub label: String,
    #[serde(rename = "type", default = "default_attr_type")]
    pub attr_type: String,
    pub scale: Option<f64>,
    pub formula: Option<String>,
    pub unit: Option<String>,
    #[serde(default)]
    pub display: bool,
    #[serde(default)]
    pub overview: bool,
    #[serde(default)]
    pub control: bool,
    #[serde(default)]
    pub xsolar: bool,
    pub xsolar_key: Option<String>,
    pub decode: Option<Vec<DecodeRule>>,
}

fn default_attr_type() -> String {
    "number".to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DecodeRule {
    pub id: String,
    pub label: Option<String>,
    pub slice: (usize, usize),
    pub scale: Option<f64>,
    #[serde(default)]
    pub xsolar: bool,
    pub xsolar_key: Option<String>,
}

#[derive(Debug, Clone)]
pub struct DeviceMetadata {
    pub ac_index: Option<i64>,
    pub sign_index: Option<i64>,
    pub switch_index: Option<i64>,
    pub power_index: Option<i64>,
    pub light_sensor_index: Option<i64>,
}

/// MQTT command template: payload khi engine gửi lệnh xuống device mqtt.
/// `prefix` + `value_key` + token(giá trị) + `suffix`.
/// Với attr bool: token = `on_value` khi ON, `off_value` khi OFF (nếu khai báo),
/// ngược lại là "1" / "0". Vd Tasmota: prefix `{"cmnd":"`, value_key `POWER`,
/// suffix `"}`, on_value `ON`, off_value `OFF` → `{"cmnd":"POWERON"}`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MqttCommandTemplate {
    #[serde(default)]
    pub prefix: String,
    #[serde(default)]
    pub value_key: String,
    #[serde(default)]
    pub suffix: String,
    #[serde(default)]
    pub on_value: Option<String>,
    #[serde(default)]
    pub off_value: Option<String>,
}

/// Cấu hình kết nối MQTT trực tiếp cho thiết bị (protocol == "mqtt").
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MqttCfg {
    pub state_topic: String,
    pub command_topic: String,
    #[serde(default)]
    pub lwt_topic: Option<String>,
    #[serde(default)]
    pub command_template: Option<MqttCommandTemplate>,
}

#[derive(Debug, Clone)]
pub struct Device {
    pub zigbee_addr: String,
    pub protocol: String,
    pub nr_type: String,
    pub name: String,
    pub group: String,
    pub gateway: String,
    pub mqtt_cfg: Option<MqttCfg>,
    pub attributes: HashMap<String, AttributeConfig>,
    pub metadata: DeviceMetadata,
}

impl Device {
    /// Key thống nhất để tra cứu trong DeviceManager: zigbee device dùng
    /// `zigbee_addr`, mqtt device dùng `mqtt:{state_topic}`.
    pub fn key(&self) -> String {
        if self.protocol == "mqtt" {
            self.mqtt_cfg
                .as_ref()
                .map(|c| format!("mqtt:{}", c.state_topic))
                .unwrap_or_default()
        } else {
            self.zigbee_addr.clone()
        }
    }

    pub fn is_mqtt(&self) -> bool {
        self.protocol == "mqtt"
    }
}

pub struct DeviceManager {
    devices_file: PathBuf,
    pub devices: HashMap<String, Device>,
    pub site_name: String,
    pub max_devices: i64,
    mtime: u64,
}

impl DeviceManager {
    pub fn new(devices_file: &str) -> Result<Self, anyhow::Error> {
        let mut dm = Self {
            devices_file: PathBuf::from(devices_file),
            devices: HashMap::new(),
            site_name: String::new(),
            max_devices: 12,
            mtime: 0,
        };
        dm.load_devices()?;
        Ok(dm)
    }

    pub fn load_devices(&mut self) -> Result<(), anyhow::Error> {
        let content = std::fs::read_to_string(&self.devices_file)?;
        let data: serde_yaml::Value = serde_yaml::from_str(&content)?;

        self.site_name = data
            .get("site")
            .and_then(|v| v.as_str())
            .unwrap_or("bluCafe")
            .to_string();
        self.max_devices = data
            .get("max_devices")
            .and_then(|v| v.as_i64())
            .unwrap_or(12);

        let mut devices = HashMap::new();
        if let Some(devices_list) = data.get("devices").and_then(|v| v.as_sequence()) {
            for dev_val in devices_list {
                let enabled = dev_val
                    .get("enabled")
                    .and_then(|v| v.as_bool())
                    .unwrap_or(true);
                if !enabled {
                    continue;
                }

                let protocol = dev_val
                    .get("protocol")
                    .and_then(|v| v.as_str())
                    .unwrap_or("zigbee")
                    .to_string();

                let zigbee_addr = dev_val
                    .get("zigbee_addr")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                if zigbee_addr.is_empty() && protocol != "mqtt" {
                    continue;
                }

                let mqtt_cfg = if protocol == "mqtt" {
                    dev_val.get("mqtt").map(|m| MqttCfg {
                        state_topic: m
                            .get("state_topic")
                            .and_then(|v| v.as_str())
                            .unwrap_or("")
                            .to_string(),
                        command_topic: m
                            .get("command_topic")
                            .and_then(|v| v.as_str())
                            .unwrap_or("")
                            .to_string(),
                        lwt_topic: m
                            .get("lwt_topic")
                            .and_then(|v| v.as_str())
                            .map(|s| s.to_string()),
                        command_template: m
                            .get("command_template")
                            .and_then(|v| v.as_mapping())
                            .map(|t| MqttCommandTemplate {
                                prefix: t
                                    .get("prefix")
                                    .and_then(|v| v.as_str())
                                    .unwrap_or("")
                                    .to_string(),
                                value_key: t
                                    .get("value_key")
                                    .and_then(|v| v.as_str())
                                    .unwrap_or("")
                                    .to_string(),
                                suffix: t
                                    .get("suffix")
                                    .and_then(|v| v.as_str())
                                    .unwrap_or("")
                                    .to_string(),
                                on_value: t
                                    .get("on_value")
                                    .and_then(|v| v.as_str())
                                    .map(|s| s.to_string()),
                                off_value: t
                                    .get("off_value")
                                    .and_then(|v| v.as_str())
                                    .map(|s| s.to_string()),
                            }),
                    })
                } else {
                    None
                };

                let mut attributes = HashMap::new();
                if let Some(attrs) = dev_val.get("attributes").and_then(|v| v.as_sequence()) {
                    for attr in attrs {
                        let attr_id = attr
                            .get("id")
                            .and_then(|v| v.as_str())
                            .unwrap_or("")
                            .to_string();
                        if attr_id.is_empty() {
                            continue;
                        }

                        let decode = attr.get("decode").and_then(|v| v.as_sequence()).map(|seq| {
                            seq.iter()
                                .map(|r| DecodeRule {
                                    id: r.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                                    label: r.get("label").and_then(|v| v.as_str()).map(|s| s.to_string()),
                                    slice: {
                                        let s = r.get("slice").and_then(|v| v.as_sequence());
                                        match s {
                                            Some(sl) => (
                                                sl.first().and_then(|v| v.as_u64()).unwrap_or(0) as usize,
                                                sl.get(1).and_then(|v| v.as_u64()).unwrap_or(0) as usize,
                                            ),
                                            None => (0, 0),
                                        }
                                    },
                                    scale: r.get("scale").and_then(|v| v.as_f64()),
                                    xsolar: r.get("xsolar").and_then(|v| v.as_bool()).unwrap_or(false),
                                    xsolar_key: r.get("xsolar_key").and_then(|v| v.as_str()).map(|s| s.to_string()),
                                })
                                .collect()
                        });

                        attributes.insert(
                            attr_id,
                            AttributeConfig {
                                label: attr.get("label").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                                attr_type: attr.get("type").and_then(|v| v.as_str()).unwrap_or("number").to_string(),
                                scale: attr.get("scale").and_then(|v| v.as_f64()),
                                formula: attr.get("formula").and_then(|v| v.as_str()).map(|s| s.to_string()),
                                unit: attr.get("unit").and_then(|v| v.as_str()).map(|s| s.to_string()),
                                display: attr.get("display").and_then(|v| v.as_bool()).unwrap_or(false),
                                overview: attr.get("overview").and_then(|v| v.as_bool()).unwrap_or(false),
                                control: attr.get("control").and_then(|v| v.as_bool()).unwrap_or(false),
                                xsolar: attr.get("xsolar").and_then(|v| v.as_bool()).unwrap_or(false),
                                xsolar_key: attr.get("xsolar_key").and_then(|v| v.as_str()).map(|s| s.to_string()),
                                decode,
                            },
                        );
                    }
                }

                let device = Device {
                    zigbee_addr: zigbee_addr.clone(),
                    protocol,
                    nr_type: dev_val.get("nr_type").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    name: dev_val.get("name").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    group: dev_val.get("group").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    gateway: dev_val.get("gateway").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    mqtt_cfg,
                    attributes,
                    metadata: DeviceMetadata {
                        ac_index: dev_val.get("ac_index").and_then(|v| v.as_i64()),
                        sign_index: dev_val.get("sign_index").and_then(|v| v.as_i64()),
                        switch_index: dev_val.get("switch_index").and_then(|v| v.as_i64()),
                        power_index: dev_val.get("power_index").and_then(|v| v.as_i64()),
                        light_sensor_index: dev_val.get("light_sensor_index").and_then(|v| v.as_i64()),
                    },
                };
                devices.insert(device.key(), device);
            }
        }

        self.devices = devices;
        self.mtime = std::fs::metadata(&self.devices_file)
            .ok()
            .and_then(|m| m.modified().ok())
            .map(|t| t.duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs())
            .unwrap_or(0);

        info!(
            "Loaded {} devices from {:?}",
            self.devices.len(),
            self.devices_file
        );
        Ok(())
    }

    pub fn check_reload(&mut self) {
        if !self.devices_file.exists() {
            return;
        }

        if let Ok(meta) = std::fs::metadata(&self.devices_file) {
            if let Ok(modified) = meta.modified() {
                let current_mtime = modified
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
                if current_mtime > self.mtime {
                    info!("Devices file changed, reloading...");
                    let old_count = self.devices.len();
                    if self.load_devices().is_ok() {
                        info!("Reloaded devices: {} -> {} devices", old_count, self.devices.len());
                    }
                }
            }
        }
    }

    pub fn get_device(&self, zigbee_addr: &str) -> Option<&Device> {
        self.devices.get(zigbee_addr)
    }

    /// Find an MQTT device by its state topic (exact match).
    pub fn find_by_state_topic(&self, topic: &str) -> Option<&Device> {
        self.devices.values().find(|d| {
            d.is_mqtt()
                && d.mqtt_cfg
                    .as_ref()
                    .map(|c| c.state_topic == topic)
                    .unwrap_or(false)
        })
    }

    /// Find an MQTT device by its LWT topic (exact match).
    pub fn find_by_lwt_topic(&self, topic: &str) -> Option<&Device> {
        self.devices.values().find(|d| {
            d.is_mqtt()
                && d.mqtt_cfg
                    .as_ref()
                    .and_then(|c| c.lwt_topic.as_deref())
                    .map(|t| t == topic)
                    .unwrap_or(false)
        })
    }

    pub fn get_all_devices(&self) -> &HashMap<String, Device> {
        &self.devices
    }

    pub fn get_device_by_type(&self, nr_type: &str) -> Vec<&Device> {
        self.devices
            .values()
            .filter(|d| d.nr_type == nr_type)
            .collect()
    }

    pub fn get_gateway_devices(&self, gateway: &str) -> Vec<&Device> {
        self.devices
            .values()
            .filter(|d| d.gateway == gateway)
            .collect()
    }

    /// Human-readable card type for the front-end (matches LVGL dev_type_t).
    pub fn card_type(&self, nr_type: &str) -> &'static str {
        match nr_type {
            "ac_controller" => "AC",
            "mcb" => "Sign",
            "power_meter" => "Power",
            "light_sensor" => "Light",
            "switch" => "Switch",
            _ => "Other",
        }
    }

    /// Stable device_id string for a given device (slug + index). Returns
    /// None when the device has no index metadata (not displayable/controllable).
    pub fn device_id_of(&self, device: &Device) -> Option<String> {
        let (prefix, index) = self.slug_and_index(device)?;
        Some(format!("{}_{}", prefix, index))
    }

    fn slug_and_index(&self, device: &Device) -> Option<(&'static str, i64)> {
        let slug = match device.nr_type.as_str() {
            "ac_controller" => "ac",
            "mcb" => "sign",
            "power_meter" => "power",
            "light_sensor" => "light",
            "switch" => "switch",
            _ => return None,
        };
        let index = match device.nr_type.as_str() {
            "ac_controller" => device.metadata.ac_index?,
            "mcb" => device.metadata.sign_index?,
            "power_meter" => device.metadata.power_index?,
            "light_sensor" => device.metadata.light_sensor_index?,
            "switch" => device.metadata.switch_index.or(device.metadata.sign_index)?,
            _ => device.metadata.sign_index?,
        };
        Some((slug, index))
    }

    /// Look up a device by its slug device_id (e.g. "ac_0", "sign_1").
    pub fn get_device_by_id(&self, device_id: &str) -> Option<&Device> {
        let (slug, idx) = device_id.split_once('_')?;
        let idx = idx.parse::<i64>().ok()?;
        for device in self.devices.values() {
            if self.slug_and_index(device) == Some((slug, idx)) {
                return Some(device);
            }
        }
        None
    }

    /// Look up a device by nr_type + index (used when translating MQTT topics
    /// like bms/ac/0/... back to a device).
    pub fn get_device_by_type_index(&self, nr_type: &str, idx: i64) -> Option<&Device> {
        for device in self.devices.values() {
            if device.nr_type != nr_type {
                continue;
            }
            let matched = match nr_type {
                "ac_controller" => device.metadata.ac_index == Some(idx),
                "mcb" => device.metadata.sign_index == Some(idx),
                "power_meter" => device.metadata.power_index == Some(idx),
                "light_sensor" => device.metadata.light_sensor_index == Some(idx),
                "switch" => {
                    device.metadata.switch_index == Some(idx)
                        || device.metadata.sign_index == Some(idx)
                }
                _ => false,
            };
            if matched {
                return Some(device);
            }
        }
        None
    }

    /// Resolve a display attribute's *label* into its YAML attr_id for this device.
    pub fn attr_id_by_label<'a>(&self, device: &'a Device, label: &str) -> Option<&'a String> {
        for (attr_id, cfg) in &device.attributes {
            if cfg.label == label {
                return Some(attr_id);
            }
        }
        None
    }

    /// Build the `/api/v1/devices/catalog` response (no realtime values).
    pub fn catalog_json(&self) -> Value {
        let mut devices = Vec::new();
        for device in self.devices.values() {
            let Some(device_id) = self.device_id_of(device) else {
                continue;
            };
            let mut display_attrs = Vec::new();
            for (attr_id, cfg) in &device.attributes {
                if !cfg.display {
                    continue;
                }
                display_attrs.push(json!({
                    "attr_id": attr_id,
                    "label": cfg.label,
                    "type": cfg.attr_type,
                    "unit": cfg.unit.clone().unwrap_or_default(),
                            "overview": cfg.overview,
                            "control": cfg.control,
                }));
            }
            devices.push(json!({
                "id": device_id,
                "name": device.name,
                "type": self.card_type(&device.nr_type),
                "group": device.group,
                "display_attrs": display_attrs,
            }));
        }
        json!({ "site": self.site_name, "devices": devices })
    }
}

pub type SharedDeviceManager = Arc<RwLock<DeviceManager>>;

#[cfg(test)]
mod tests {
    use super::*;

    fn test_dm() -> DeviceManager {
        DeviceManager::new("/tmp/devices_mqtt_test.yaml").expect("load test devices")
    }

#[test]
    fn mqtt_device_loaded_with_key() {
        let dm = test_dm();
        let dev = dm.devices.get("mqtt:dev/shade/state").expect("mqtt device key");
        assert_eq!(dev.protocol, "mqtt");
        assert_eq!(dev.nr_type, "switch");
        assert_eq!(dev.metadata.switch_index, Some(0));
        assert!(dev.is_mqtt());
        assert_eq!(dev.key(), "mqtt:dev/shade/state");
    }

    #[test]
    fn mqtt_find_by_topics() {
        let dm = test_dm();
        assert!(dm.find_by_state_topic("dev/shade/state").is_some());
        assert!(dm.find_by_state_topic("dev/unknown").is_none());
        assert!(dm.find_by_lwt_topic("dev/shade/lwt").is_some());
        assert!(dm.find_by_lwt_topic("tele/x/LWT").is_none());
    }

    #[test]
    fn switch_device_id_is_switch_0() {
        let dm = test_dm();
        let dev = dm.find_by_state_topic("dev/shade/state").unwrap();
        assert_eq!(dm.device_id_of(dev), Some("switch_0".to_string()));
    }
}
