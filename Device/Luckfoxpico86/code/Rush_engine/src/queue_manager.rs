use log::{debug, info, warn};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;

use crate::mqtt_client::MqttClient;
use rumqttc::QoS;

type OutPayload = (String, String); // (topic, payload_str)

/// Maximum publish attempts when the local broker is temporarily down
/// (review §5 D5). Commands must not be silently dropped by a transient
/// disconnect of the NanoMQ broker.
const MAX_RETRIES: u32 = 3;

pub struct QueueManager {
    out_tx: mpsc::UnboundedSender<OutPayload>,
    running: Arc<AtomicBool>,
}

impl QueueManager {
    pub fn new(mqtt_client: Arc<MqttClient>) -> Self {
        let (out_tx, mut out_rx) = mpsc::unbounded_channel::<OutPayload>();
        // Start running immediately. The worker is spawned in new() but checks the
        // flag on entry; if it starts false the task exits before start() runs,
        // silently dropping every outgoing command (no OUT SENT ever logged).
        let running = Arc::new(AtomicBool::new(true));
        let running_worker = running.clone();

        tokio::spawn(async move {
            while running_worker.load(std::sync::atomic::Ordering::Relaxed) {
                match out_rx.recv().await {
                    Some((topic, payload)) => {
                        publish_with_retry(&mqtt_client, &topic, &payload).await;
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
    ) {
        // Payload for Tasmota ZbSend. Endpoint MUST be explicit:
        //   ZbSend {"Device":"0x..","Endpoint":1,"Write":{"EF00/xxxx":N}}
        // Without it, Tasmota resolves the endpoint from its cached device
        // table. For an ONLINE device that cache exists, but for an OFFLINE
        // device Tasmota returns {"ZbSend":"Missing endpoint"} and transmits
        // NOTHING (the gateway's zigbee LED never blinks). Providing Endpoint:1
        // lets the coordinator always submit the frame to the radio, even when
        // the target device is offline (it is retried/queued until reachable).
        // Tuya EF00 controllers (AC, sign/MCB, switch) all use endpoint 1 — and
        // this is also what the power-meter read path already uses.
        let payload = serde_json::json!({
            "Device": zigbee_addr,
            "Endpoint": 1,
            "Write": write_dict,
        });

        self.send_publish(
            &format!("cmnd/{}/ZbSend", gateway),
            serde_json::to_string(&payload).unwrap_or_default(),
        );
        debug!(
            "OUT QUEUED: {} -> {} {:?}",
            gateway, zigbee_addr, write_dict
        );
    }

    /// Queue an arbitrary MQTT publish (used for MQTT-direct devices). Same
    /// worker + retry policy as ZbSend.
    pub fn send_publish(&self, topic: &str, payload: String) {
        let _ = self.out_tx.send((topic.to_string(), payload));
    }
}

/// Publish with retry + exponential backoff while the broker is disconnected.
/// rumqttc buffers a publish as Ok once connected, so the meaningful failure
/// mode to guard against (review §5 D5) is a transient broker outage.
async fn publish_with_retry(client: &Arc<MqttClient>, topic: &str, payload: &str) {
    for attempt in 0..=MAX_RETRIES {
        if !client.is_connected() {
            if attempt == MAX_RETRIES {
                warn!(
                    "Dropping command to {} after {} attempts (broker down)",
                    topic, attempt + 1
                );
                return;
            }
            let backoff_ms = 200u64.saturating_mul(2u64.pow(attempt));
            tokio::time::sleep(Duration::from_millis(backoff_ms)).await;
            continue;
        }

        client
            .publish(topic, payload.to_string(), QoS::AtLeastOnce, false)
            .await;
        info!("OUT SENT: {} = {}", topic, payload);
        return;
    }
}
