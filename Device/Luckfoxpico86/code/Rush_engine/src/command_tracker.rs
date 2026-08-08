use log::{debug, info};
use serde_json::Value;
use std::collections::HashMap;
use std::time::{Duration, Instant};
use tokio::sync::Mutex;

/// Where a control command originated from. Determines how a timeout is
/// handled downstream (log warning / nack) — see review doc §5.3 (D6).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum CommandSource {
    App,
    Xsolar,
}

/// A control command sent downstream that hasn't been confirmed by a real
/// device report yet.
#[derive(Clone, Debug)]
pub struct PendingCommand {
    pub command_id: String,
    /// zigbee address of the target device.
    pub device_addr: String,
    /// slug device_id (e.g. "sign_0") used to echo the ack back to the app.
    pub device_id: String,
    /// attribute *label* whose real report we expect to change.
    pub attr_label: String,
    /// value we expect the device to reach (1 / 0 for bool controls).
    pub expected_value: i64,
    pub sent_at: Instant,
    pub source: CommandSource,
}

/// Tracks commands that are waiting for a real report to confirm them.
///
/// * Reconcile (§5.3): a matching uplink report moves the command to the ack
///   queue so the *next* SSE event carries `ack_command_id` — fixing D1 (ack
///   was previously attached optimistically, not against the real report).
/// * Idempotency (D3): a duplicate `command_id` is rejected.
/// * Sweep (D2/D6): a periodic sweep removes entries that never confirmed and
///   reports them as unconfirmed/stale so the UI can downgrade.
pub struct CommandTracker {
    /// command_id -> pending command.
    pending: Mutex<HashMap<String, PendingCommand>>,
    /// device_addr(zigbee) -> command_id (one in-flight command per device).
    by_device: Mutex<HashMap<String, String>>,
    /// device_id(slug) -> command_id confirmed and waiting to be attached to
    /// the next translated SSE event for that device.
    ack_queue: Mutex<HashMap<String, String>>,
}

impl CommandTracker {
    pub fn new() -> Self {
        Self {
            pending: Mutex::new(HashMap::new()),
            by_device: Mutex::new(HashMap::new()),
            ack_queue: Mutex::new(HashMap::new()),
        }
    }

    /// Register a command. Returns `true` when newly registered.
    ///
    /// Returns `false` (and keeps the original) when `command_id` is already
    /// pending — the caller must reply ACCEPTED without re-sending (D3), so a
    /// client retry on HTTP timeout cannot double-fire the ZbSend.
    pub async fn register(&self, pc: PendingCommand) -> bool {
        let mut pending = self.pending.lock().await;
        if pending.contains_key(&pc.command_id) {
            debug!("Duplicate command_id {} ignored (idempotent)", pc.command_id);
            return false;
        }
        // A device gets one in-flight command: abandon any older pending one.
        if let Some(prev) = self.by_device.lock().await.get(&pc.device_addr).cloned() {
            pending.remove(&prev);
        }
        pending.insert(pc.command_id.clone(), pc.clone());
        self.by_device
            .lock()
            .await
            .insert(pc.device_addr.clone(), pc.command_id.clone());
        info!(
            "[command] pending {} -> {} (attr {}= {})",
            pc.command_id, pc.device_addr, pc.attr_label, pc.expected_value
        );
        true
    }

    pub async fn is_pending(&self, command_id: &str) -> bool {
        self.pending.lock().await.contains_key(command_id)
    }

    /// Called from the ingest fan-out with the freshly normalised attributes
    /// of a device. If the report matches the expected value of the pending
    /// command, confirm it: move the command_id to the ack_queue so the next
    /// SSE event for that device carries `ack_command_id`.
    pub async fn reconcile(&self, device_addr: &str, updates: &HashMap<String, Value>) {
        let cmd_id = match self.by_device.lock().await.get(device_addr).cloned() {
            Some(id) => id,
            None => {
                debug!("No pending command for {}, skipping reconcile", device_addr);
                return;
            }
        };

        let pc = {
            let mut pending = self.pending.lock().await;
            let Some(pc) = pending.get(&cmd_id).cloned() else {
                self.by_device.lock().await.remove(device_addr);
                return;
            };
            if !value_matches_expected(&pc.attr_label, pc.expected_value, updates) {
                return; // not confirmed yet; stays pending for the sweep
            }
            pending.remove(&cmd_id);
            pc
        };

        self.by_device.lock().await.remove(device_addr);
        self.ack_queue.lock().await.insert(pc.device_id.clone(), pc.command_id.clone());
        info!(
            "[command] {} confirmed by report on {} ({}={})",
            pc.command_id, device_addr, pc.attr_label, pc.expected_value
        );
    }

    /// Take the ack_command_id for a device that just had an SSE event.
    pub async fn take_ack(&self, device_id: &str) -> Option<String> {
        self.ack_queue.lock().await.remove(device_id)
    }

    /// Blocking fallback used by the SSE snapshot stream (non-async context).
    pub fn take_ack_blocking(&self, device_id: &str) -> Option<String> {
        let mut q = self.ack_queue.try_lock().ok()?;
        q.remove(device_id)
    }

    /// Remove and return pending commands that have not confirmed within
    /// `timeout`. Caller logs/reports them as unconfirmed (D2 cleanup, D6 warn).
    pub async fn sweep(&self, timeout: Duration) -> Vec<PendingCommand> {
        let mut pending = self.pending.lock().await;
        let mut by_device = self.by_device.lock().await;
        let now = Instant::now();
        let expired: Vec<String> = pending
            .iter()
            .filter(|(_, pc)| now.duration_since(pc.sent_at) > timeout)
            .map(|(id, _)| id.clone())
            .collect();

        let mut out = Vec::new();
        for id in expired {
            if let Some(pc) = pending.remove(&id) {
                by_device.remove(&pc.device_addr);
                out.push(pc);
            }
        }
        out
    }
}

fn value_matches_expected(label: &str, expected: i64, updates: &HashMap<String, Value>) -> bool {
    let Some(v) = updates.get(label) else {
        return false;
    };
    match v {
        Value::Number(n) => {
            n.as_f64().map(|f| f as i64).unwrap_or(0) == expected
                || n.as_i64().unwrap_or(0) == expected
        }
        Value::String(s) => {
            let want_on = expected != 0;
            (s.eq_ignore_ascii_case("ON") || s == "1" || s.eq_ignore_ascii_case("TRUE")) == want_on
        }
        Value::Bool(b) => *b == (expected != 0),
        _ => false,
    }
}