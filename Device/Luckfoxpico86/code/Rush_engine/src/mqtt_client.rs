use log::{debug, error, info, warn};
use rumqttc::{AsyncClient, Event, MqttOptions, Packet, QoS};
use serde_json::Value;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;

#[derive(Debug, Clone)]
pub struct MqttMessage {
    pub topic: String,
    pub payload: Value,
}

pub struct MqttClient {
    client: AsyncClient,
    message_tx: mpsc::UnboundedSender<MqttMessage>,
    connected: Arc<std::sync::atomic::AtomicBool>,
}

impl MqttClient {
    pub fn new(
        broker: &str,
        port: u16,
        client_id: &str,
        username: Option<&str>,
        password: Option<&str>,
    ) -> (Self, mpsc::UnboundedReceiver<MqttMessage>) {
        let (message_tx, message_rx) = mpsc::unbounded_channel();

        let mut options = MqttOptions::new(client_id, broker, port);
        options.set_keep_alive(Duration::from_secs(60));
        options.set_clean_session(true);

        if let (Some(u), Some(p)) = (username, password) {
            options.set_credentials(u, p);
            info!(
                "MQTT client initialized: {}:{}, client_id={}, auth=yes",
                broker, port, client_id
            );
        } else {
            info!(
                "MQTT client initialized: {}:{}, client_id={}",
                broker, port, client_id
            );
        }

        let (client, eventloop) = AsyncClient::new(options, 100);
        let connected = Arc::new(std::sync::atomic::AtomicBool::new(false));

        let conn = connected.clone();
        let tx = message_tx.clone();
        let cid = client_id.to_string();
        tokio::spawn(async move {
            run_event_loop(eventloop, cid, conn, tx).await;
        });

        (
            Self {
                client,
                message_tx,
                connected,
            },
            message_rx,
        )
    }

    pub async fn subscribe(&self, topic: &str, qos: QoS) {
        match self.client.subscribe(topic, qos).await {
            Ok(_) => info!("Subscribed to topic: {} (QoS {:?})", topic, qos),
            Err(e) => error!("Failed to subscribe to {}: {}", topic, e),
        }
    }

    pub async fn publish(
        &self,
        topic: &str,
        payload: impl Into<String>,
        qos: QoS,
        retain: bool,
    ) {
        let payload = payload.into();
        match self.client.publish(topic, qos, retain, payload.as_bytes()).await {
            Ok(_) => debug!("Published to {}: {}", topic, &payload[..payload.len().min(100)]),
            Err(e) => error!("Failed to publish to {}: {}", topic, e),
        }
    }

    pub async fn publish_json(&self, topic: &str, payload: &Value, qos: QoS, retain: bool) {
        let json_str = serde_json::to_string(payload).unwrap_or_default();
        self.publish(topic, json_str, qos, retain).await;
    }

    pub fn is_connected(&self) -> bool {
        self.connected.load(std::sync::atomic::Ordering::Relaxed)
    }

    pub fn message_sender(&self) -> mpsc::UnboundedSender<MqttMessage> {
        self.message_tx.clone()
    }
}

async fn run_event_loop(
    mut eventloop: rumqttc::EventLoop,
    client_id: String,
    connected: Arc<std::sync::atomic::AtomicBool>,
    message_tx: mpsc::UnboundedSender<MqttMessage>,
) {
    loop {
        match eventloop.poll().await {
            Ok(Event::Incoming(Packet::ConnAck(_))) => {
                connected.store(true, std::sync::atomic::Ordering::Relaxed);
                info!("[{}] Connected to MQTT broker successfully", client_id);
            }
            Ok(Event::Incoming(Packet::Publish(publish))) => {
                let topic = publish.topic.clone();
                let payload_str = String::from_utf8_lossy(&publish.payload).to_string();

                let payload = serde_json::from_str::<Value>(&payload_str)
                    .unwrap_or(Value::String(payload_str));

                debug!(
                    "Received message on {}: {}",
                    topic,
                    &format!("{:?}", payload)[..100.min(format!("{:?}", payload).len())]
                );

                let _ = message_tx.send(MqttMessage { topic, payload });
            }
            Ok(Event::Outgoing(_)) => {}
            Ok(Event::Incoming(Packet::Disconnect)) => {
                connected.store(false, std::sync::atomic::Ordering::Relaxed);
                warn!("[{}] Disconnected from MQTT broker", client_id);
            }
            Err(e) => {
                connected.store(false, std::sync::atomic::Ordering::Relaxed);
                error!("[{}] MQTT event loop error: {}", client_id, e);
                tokio::time::sleep(Duration::from_secs(5)).await;
            }
            _ => {}
        }
    }
}
