use log::{debug, error, info};
use rusqlite::{params, Connection};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Mutex;

pub struct StateManager {
    conn: Mutex<Connection>,
    db_path: String,
}

impl StateManager {
    pub fn new(db_path: &str) -> Result<Self, anyhow::Error> {
        let conn = Connection::open(db_path)?;

        let sm = Self {
            conn: Mutex::new(conn),
            db_path: db_path.to_string(),
        };
        sm.init_db()?;
        info!("State manager initialized with database: {}", db_path);
        Ok(sm)
    }

    fn init_db(&self) -> Result<(), anyhow::Error> {
        let conn = self.conn.lock().unwrap();

        // WAL mode: tolerates concurrent writers and write bursts from the
        // Zigbee mesh (many devices reporting at once) without lock contention.
        conn.pragma_update(None, "journal_mode", "WAL")?;
        // NORMAL synchronous + WAL is the right durability/latency trade-off for
        // a history/audit store that is never read back on the hot path.
        conn.pragma_update(None, "synchronous", "NORMAL")?;
        let _ = conn.execute("PRAGMA busy_timeout=5000", []);

        conn.execute_batch(
            "
            CREATE TABLE IF NOT EXISTS device_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts TEXT NOT NULL,
                device_id TEXT NOT NULL,
                device_type TEXT,
                location TEXT,
                payload TEXT,
                event TEXT
            );

            CREATE TABLE IF NOT EXISTS device_metric (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts TEXT NOT NULL,
                device_id TEXT NOT NULL,
                device_type TEXT,
                attr_name TEXT NOT NULL,
                attr_value REAL,
                attr_str TEXT,
                attr_type TEXT,
                raw_attr_id TEXT
            );

            CREATE TABLE IF NOT EXISTS device_config (
                device_id TEXT PRIMARY KEY,
                device_type TEXT,
                location TEXT,
                friendly_name TEXT,
                enabled INTEGER DEFAULT 1,
                extra TEXT
            );

            CREATE INDEX IF NOT EXISTS idx_log_ts ON device_log(ts);
            CREATE INDEX IF NOT EXISTS idx_log_device ON device_log(device_id, ts);
            CREATE INDEX IF NOT EXISTS idx_metric_ts ON device_metric(ts);
            CREATE INDEX IF NOT EXISTS idx_metric_device_attr ON device_metric(device_id, attr_name, ts);
            ",
        )?;

        info!("Database schema initialized");
        Ok(())
    }

    pub fn log_event(
        &self,
        device_id: &str,
        event: &str,
        payload: &Value,
        device_type: Option<&str>,
        location: Option<&str>,
    ) {
        let conn = self.conn.lock().unwrap();
        let ts = chrono::Local::now().to_rfc3339();
        let payload_str = serde_json::to_string(payload).unwrap_or_default();

        if let Err(e) = conn.execute(
            "INSERT INTO device_log (ts, device_id, device_type, location, payload, event) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            params![&ts, device_id, device_type.unwrap_or(""), location.unwrap_or(""), &payload_str, event],
        ) {
            error!("Failed to log event for {}: {}", device_id, e);
        } else {
            debug!("Logged event for {}: {}", device_id, event);
        }
    }

    pub fn batch_update_metrics(
        &self,
        metrics: &[(String, String, Value, String, String, String)],
    ) {
        if metrics.is_empty() {
            return;
        }

        let conn = self.conn.lock().unwrap();
        let ts = chrono::Local::now().to_rfc3339();

        if let Err(e) = conn.execute("BEGIN TRANSACTION", []) {
            error!("Failed to begin transaction: {}", e);
            return;
        }

        for (device_id, attr_name, value, attr_type, device_type, raw_attr_id) in metrics {
            let result = if attr_type == "number" {
                let num_val = match value {
                    Value::Number(n) => n.as_f64().unwrap_or(0.0),
                    Value::String(s) => s.parse::<f64>().unwrap_or(0.0),
                    Value::Bool(b) => {
                        if *b {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    _ => 0.0,
                };
                conn.execute(
                    "INSERT INTO device_metric (ts, device_id, device_type, attr_name, attr_value, attr_type, raw_attr_id) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                    params![&ts, device_id, device_type, attr_name, num_val, attr_type, raw_attr_id],
                )
            } else {
                let str_val = match value {
                    Value::String(s) => s.clone(),
                    other => other.to_string(),
                };
                conn.execute(
                    "INSERT INTO device_metric (ts, device_id, device_type, attr_name, attr_str, attr_type, raw_attr_id) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                    params![&ts, device_id, device_type, attr_name, str_val, attr_type, raw_attr_id],
                )
            };

            if let Err(e) = result {
                error!(
                    "Failed to insert metric for {}.{}: {}",
                    device_id, attr_name, e
                );
            }
        }

        if let Err(e) = conn.execute("COMMIT", []) {
            error!("Failed to commit batch: {}", e);
            let _ = conn.execute("ROLLBACK", []);
        } else {
            debug!("Batch inserted {} metrics", metrics.len());
        }
    }

    pub fn get_latest_state(&self, device_id: &str) -> HashMap<String, Value> {
        let conn = self.conn.lock().unwrap();
        let mut state = HashMap::new();

        let query = "SELECT attr_name, attr_value, attr_str, attr_type, ts FROM ( \
                     SELECT *, ROW_NUMBER() OVER (PARTITION BY attr_name ORDER BY ts DESC) rn \
                     FROM device_metric WHERE device_id = ?1 \
                     ) WHERE rn = 1";

        match conn.prepare(query) {
            Ok(mut stmt) => {
                if let Ok(rows) = stmt.query_map(params![device_id], |row| {
                    let attr_name: String = row.get(0)?;
                    let attr_value: Option<f64> = row.get(1)?;
                    let attr_str: Option<String> = row.get(2)?;
                    let attr_type: String = row.get(3)?;
                    Ok((attr_name, attr_value, attr_str, attr_type))
                }) {
                    for row in rows.flatten() {
                        if row.3 == "number" {
                            if let Some(v) = row.1 {
                                state.insert(
                                    row.0,
                                    serde_json::Number::from_f64(v)
                                        .map(Value::Number)
                                        .unwrap_or(Value::Null),
                                );
                            }
                        } else if let Some(v) = row.2 {
                            state.insert(row.0, Value::String(v));
                        }
                    }
                }
            }
            Err(e) => {
                error!("Failed to get latest state for {}: {}", device_id, e);
            }
        }

        state
    }

    pub fn update_device_config(
        &self,
        device_id: &str,
        device_type: &str,
        location: &str,
        friendly_name: &str,
        extra: &Value,
    ) {
        let conn = self.conn.lock().unwrap();
        let extra_str = serde_json::to_string(extra).unwrap_or_default();

        if let Err(e) = conn.execute(
            "INSERT OR REPLACE INTO device_config (device_id, device_type, location, friendly_name, enabled, extra) VALUES (?1, ?2, ?3, ?4, 1, ?5)",
            params![device_id, device_type, location, friendly_name, extra_str],
        ) {
            error!("Failed to update device config for {}: {}", device_id, e);
        } else {
            info!("Updated device config for {}", device_id);
        }
    }
}
