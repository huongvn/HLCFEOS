use log::info;
use serde::{Deserialize, Serialize};
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
    pub power_index: Option<i64>,
    pub light_sensor_index: Option<i64>,
}

#[derive(Debug, Clone)]
pub struct Device {
    pub zigbee_addr: String,
    pub nr_type: String,
    pub name: String,
    pub group: String,
    pub gateway: String,
    pub attributes: HashMap<String, AttributeConfig>,
    pub metadata: DeviceMetadata,
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

                let zigbee_addr = dev_val
                    .get("zigbee_addr")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                if zigbee_addr.is_empty() {
                    continue;
                }

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
                                xsolar: attr.get("xsolar").and_then(|v| v.as_bool()).unwrap_or(false),
                                xsolar_key: attr.get("xsolar_key").and_then(|v| v.as_str()).map(|s| s.to_string()),
                                decode,
                            },
                        );
                    }
                }

                devices.insert(
                    zigbee_addr.clone(),
                    Device {
                        zigbee_addr: zigbee_addr.clone(),
                        nr_type: dev_val.get("nr_type").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                        name: dev_val.get("name").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                        group: dev_val.get("group").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                        gateway: dev_val.get("gateway").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                        attributes,
                        metadata: DeviceMetadata {
                            ac_index: dev_val.get("ac_index").and_then(|v| v.as_i64()),
                            sign_index: dev_val.get("sign_index").and_then(|v| v.as_i64()),
                            power_index: dev_val.get("power_index").and_then(|v| v.as_i64()),
                            light_sensor_index: dev_val.get("light_sensor_index").and_then(|v| v.as_i64()),
                        },
                    },
                );
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
}

pub type SharedDeviceManager = Arc<RwLock<DeviceManager>>;
