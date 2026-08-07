use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{broadcast, RwLock};

/// An event emitted to consumers (HTTP/SSE) when a bms/# topic updates.
#[derive(Clone, Debug)]
pub struct BusEvent {
    pub topic: String,
    pub payload: Value,
}

/// In-process pub/sub: keeps the latest value per topic (for /api/state replay)
/// and broadcasts every update to live subscribers (for /api/events SSE).
#[derive(Clone)]
pub struct EventBus {
    tx: broadcast::Sender<BusEvent>,
    latest: Arc<RwLock<HashMap<String, Value>>>,
}

impl EventBus {
    pub fn new(capacity: usize) -> Self {
        let (tx, _) = broadcast::channel(capacity);
        Self {
            tx,
            latest: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    pub async fn emit(&self, topic: &str, payload: Value) {
        self.latest
            .write()
            .await
            .insert(topic.to_string(), payload.clone());
        let _ = self.tx.send(BusEvent {
            topic: topic.to_string(),
            payload,
        });
    }

    /// Snapshot of latest value per topic, sorted for stable ordering.
    pub async fn snapshot(&self) -> Vec<BusEvent> {
        let map = self.latest.read().await;
        let mut events: Vec<BusEvent> = map
            .iter()
            .map(|(t, p)| BusEvent {
                topic: t.clone(),
                payload: p.clone(),
            })
            .collect();
        events.sort_by(|a, b| a.topic.cmp(&b.topic));
        events
    }

    pub fn subscribe(&self) -> broadcast::Receiver<BusEvent> {
        self.tx.subscribe()
    }
}
