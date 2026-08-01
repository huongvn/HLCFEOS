use log::{debug, info};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use tokio::sync::mpsc;

use crate::mqtt_client::MqttClient;
use rumqttc::QoS;

type OutPayload = (String, Value); // (gateway, payload)

pub struct QueueManager {
    out_tx: mpsc::UnboundedSender<OutPayload>,
    running: Arc<AtomicBool>,
}

impl QueueManager {
    pub fn new(mqtt_client: Arc<MqttClient>) -> Self {
        let (out_tx, mut out_rx) = mpsc::unbounded_channel::<OutPayload>();
        let running = Arc::new(AtomicBool::new(false));
        let running_worker = running.clone();

        tokio::spawn(async move {
            while running_worker.load(std::sync::atomic::Ordering::Relaxed) {
                match out_rx.recv().await {
                    Some((gateway, payload)) => {
                        let topic = format!("cmnd/{}/ZbSend", gateway);
                        let json_str = serde_json::to_string(&payload).unwrap_or_default();
                        mqtt_client
                            .publish(&topic, json_str, QoS::AtLeastOnce, false)
                            .await;
                        info!("OUT SENT: {} = {:?}", topic, payload);
                    }
                    None => break,
                }
            }
        });

        Self { out_tx, running }
    }

    pub fn start(&self) {
        self.running
            .store(true, std::sync::atomic::Ordering::Relaxed);
        info!("Queue Manager started (out worker)");
    }

    pub fn stop(&self) {
        self.running
            .store(false, std::sync::atomic::Ordering::Relaxed);
        info!("Queue Manager stopped");
    }

    pub fn send_zbsend(
        &self,
        gateway: &str,
        zigbee_addr: &str,
        write_dict: &HashMap<String, Value>,
        endpoint: Option<u8>,
    ) {
        let mut payload = serde_json::json!({
            "Device": zigbee_addr,
            "Write": write_dict,
        });

        if let Some(ep) = endpoint {
            payload["Endpoint"] = serde_json::json!(ep);
        }

        let _ = self.out_tx.send((gateway.to_string(), payload));
        debug!(
            "OUT QUEUED: {} -> {} {:?}",
            gateway, zigbee_addr, write_dict
        );
    }
}
