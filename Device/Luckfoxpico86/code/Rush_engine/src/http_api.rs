use crate::command_tracker::{CommandSource, CommandTracker, PendingCommand};
use crate::device_manager::SharedDeviceManager;
use crate::event_bus::EventBus;
use crate::hmi_bridge::HmiBridge;
use crate::state_manager::StateManager;
use axum::extract::{Path, State};
use axum::http::StatusCode;
use axum::response::sse::{Event as SseEvent, KeepAlive, Sse};
use axum::response::IntoResponse;
use axum::routing::{get, post};
use axum::{Json, Router};
use log::{info, warn};
use serde::Deserialize;
use serde_json::{json, Value};
use std::convert::Infallible;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::Mutex;
use tokio_stream::wrappers::BroadcastStream;
use tokio_stream::{Stream, StreamExt};

pub struct HttpApiState {
    pub bus: EventBus,
    pub hmi_bridge: Arc<HmiBridge>,
    pub device_manager: SharedDeviceManager,
    pub state_manager: Arc<Mutex<StateManager>>,
    /// Tracks in-flight control commands: registers pending commands, confirms
    /// them against real reports, and feeds the SSE `ack_command_id`.
    pub commands: Arc<CommandTracker>,
    /// Live per-device online state, keyed by zigbee addr. Used to send an
    /// online snapshot to every new SSE client so the HMI dot is correct on
    /// boot/reconnect even without a fresh online edge event.
    pub device_online: Arc<Mutex<HashMap<String, bool>>>,
}

impl HttpApiState {
    /// Register a control command as pending for a device so it can be
    /// reconciled against the real report. Returns true if newly registered.
    async fn register_pending(
        &self,
        command_id: &str,
        device_addr: &str,
        device_id: &str,
        attr_label: &str,
        expected_value: i64,
    ) -> bool {
        self.commands
            .register(PendingCommand {
                command_id: command_id.to_string(),
                device_addr: device_addr.to_string(),
                device_id: device_id.to_string(),
                attr_label: attr_label.to_string(),
                expected_value,
                sent_at: Instant::now(),
                source: CommandSource::App,
            })
            .await
    }

    /// If a command is confirmed pending for this device, take its
    /// ack_command_id so the next SSE event carries it.
    async fn take_ack(&self, device_id: &str) -> Option<String> {
        self.commands.take_ack(device_id).await
    }

    /// Blocking-ish variant used by the snapshot stream (non-async closure).
    fn take_ack_blocking(&self, device_id: &str) -> Option<String> {
        self.commands.take_ack_blocking(device_id)
    }
}

#[derive(Debug, Deserialize)]
struct ActionRequest {
    command_id: String,
    action: String,
    #[serde(default)]
    params: Value,
}

async fn health() -> Json<Value> {
    Json(json!({"ok": true}))
}

/* ================= /api/v1/devices ================= */

async fn list_devices(State(state): State<Arc<HttpApiState>>) -> Json<Value> {
    let dm = state.device_manager.read().await;
    let mut devices = Vec::new();

    for device in dm.get_all_devices().values() {
        let Some(device_id) = dm.device_id_of(device) else {
            continue;
        };
        let state_map = state.state_manager.lock().await.get_latest_state(&device.key());

        let mut attrs = serde_json::Map::new();
        for (_, cfg) in &device.attributes {
            if !cfg.display {
                continue;
            }
            if let Some(v) = state_map.get(&cfg.label) {
                attrs.insert(cfg.label.clone(), v.clone());
            }
        }

        devices.push(json!({
            "id": device_id,
            "name": device.name,
            "type": dm.card_type(&device.nr_type),
            "group": device.group,
            "state": device_state(&device, &state_map),
            "attrs": attrs,
            "last_updated": 0,
            "source": "SENSOR",
        }));
    }

    Json(json!({"devices": devices}))
}

async fn get_device_state(
    State(state): State<Arc<HttpApiState>>,
    Path(device_id): Path<String>,
) -> impl IntoResponse {
    let dm = state.device_manager.read().await;
    let device = match dm.get_device_by_id(&device_id) {
        Some(d) => d,
        None => return (StatusCode::NOT_FOUND, Json(json!({"error": "device not found"}))).into_response(),
    };

    let state_map = state.state_manager.lock().await.get_latest_state(&device.key());

    let mut attrs = serde_json::Map::new();
    for (_, cfg) in &device.attributes {
        if !cfg.display {
            continue;
        }
        if let Some(v) = state_map.get(&cfg.label) {
            attrs.insert(cfg.label.clone(), v.clone());
        }
    }

    (
        StatusCode::OK,
        Json(json!({
            "device_id": device_id,
            "state": device_state(device, &state_map),
            "attrs": attrs,
            "last_updated": 0,
            "source": "SENSOR",
        })),
    )
        .into_response()
}

fn device_state(
    device: &crate::device_manager::Device,
    state_map: &std::collections::HashMap<String, Value>,
) -> Value {
    // A device is "ON" when any bool control attribute is true. The bool is
    // stored/read back from SQLite as the string "true"/"false", but tolerate
    // Bool / Number representations too (mirrors hmi_bridge::current_bool).
    for cfg in device.attributes.values() {
        if cfg.attr_type == "bool" && cfg.control {
            let on = match state_map.get(&cfg.label) {
                Some(Value::Bool(b)) => *b,
                Some(Value::Number(n)) => n.as_i64().unwrap_or(0) != 0,
                Some(Value::String(s)) => {
                    s.eq_ignore_ascii_case("ON")
                        || s == "1"
                        || s.eq_ignore_ascii_case("TRUE")
                }
                _ => false,
            };
            return json!(if on { "ON" } else { "OFF" });
        }
    }
    json!("OFF")
}

/* ================= /api/v1/devices/{id}/actions ================= */

async fn device_action(
    State(state): State<Arc<HttpApiState>>,
    Path(device_id): Path<String>,
    Json(req): Json<ActionRequest>,
) -> impl IntoResponse {
    if req.command_id.is_empty() {
        return (StatusCode::UNPROCESSABLE_ENTITY, Json(json!({"error": "command_id is required"}))).into_response();
    }

    // Idempotency (review §5 D3): if this command_id is already pending, a
    // client retry (e.g. after HTTP timeout) must NOT re-fire the ZbSend.
    if state.commands.is_pending(&req.command_id).await {
        return (
            StatusCode::ACCEPTED,
            Json(json!({
                "command_id": req.command_id,
                "status": "PENDING",
                "message": "Lệnh đã được xử lý trước đó (idempotent).",
            })),
        )
            .into_response();
    }

    let device = {
        let dm = state.device_manager.read().await;
        match dm.get_device_by_id(&device_id) {
            Some(d) => d.clone(),
            None => return (StatusCode::NOT_FOUND, Json(json!({"error": "device not found"}))).into_response(),
        }
    };

    let result = state
        .hmi_bridge
        .execute_action(&device, &req.action, &req.params)
        .await;

    match result {
        Ok(rep) => {
            info!("Action {} on {} (cmd {})", req.action, device_id, req.command_id);
            let device_key = device.key();
            state
                .register_pending(
                    &req.command_id,
                    &device_key,
                    &device_id,
                    &rep.attr_label,
                    rep.expected_value,
                )
                .await;
            (
                StatusCode::ACCEPTED,
                Json(json!({
                    "command_id": req.command_id,
                    "status": "PENDING",
                    "message": "Lệnh đã gửi tới thiết bị, đang chờ xác nhận từ report thật."
                })),
            )
                .into_response()
        }
        Err(msg) => (
            StatusCode::UNPROCESSABLE_ENTITY,
            Json(json!({"error": msg, "command_id": req.command_id})),
        )
            .into_response(),
    }
}

/* ================= /api/v1/scenes/{scene}/actions ================= */

async fn scene_action(
    State(state): State<Arc<HttpApiState>>,
    Path(scene): Path<String>,
    Json(req): Json<ActionRequest>,
) -> impl IntoResponse {
    if scene != "master" {
        return (StatusCode::NOT_FOUND, Json(json!({"error": "scene not found"}))).into_response();
    }
    if req.command_id.is_empty() {
        return (StatusCode::UNPROCESSABLE_ENTITY, Json(json!({"error": "command_id is required"}))).into_response();
    }

    match req.action.as_str() {
        "TURN_ON" => {
            state.hmi_bridge.execute_scene_open().await;
            (StatusCode::ACCEPTED, Json(json!({"command_id": req.command_id, "status": "PENDING"}))).into_response()
        }
        "TURN_OFF" => {
            state.hmi_bridge.execute_scene_close().await;
            (StatusCode::ACCEPTED, Json(json!({"command_id": req.command_id, "status": "PENDING"}))).into_response()
        }
        _ => (
            StatusCode::UNPROCESSABLE_ENTITY,
            Json(json!({"error": "invalid action for scene", "command_id": req.command_id})),
        )
            .into_response(),
    }
}

/* ================= /api/v1/events (SSE) ================= */

/// Translate a BusEvent topic/payload into the new SSE JSON format, or None
/// when the event is not device-related.
async fn translate_event(
    state: &Arc<HttpApiState>,
    topic: &str,
    payload: &Value,
) -> Option<Value> {
    // bms/{type}/{idx}/{attr_id}  or  bms/{type}/{idx}/online
    let parts: Vec<&str> = topic.split('/').collect();
    if parts.len() != 4 || parts[0] != "bms" {
        return None;
    }
    let (type_slug, idx_str, attr_id) = (parts[1], parts[2], parts[3]);
    let idx: i64 = idx_str.parse().ok()?;

    let nr_type = match type_slug {
        "ac" => "ac_controller",
        "sign" => "mcb",
        "power" => "power_meter",
        "light" => "light_sensor",
        "switch" => "switch",
        _ => return None,
    };

    let dm = state.device_manager.read().await;
    let device = dm.get_device_by_type_index(nr_type, idx)?;
    let device_id = dm.device_id_of(device).map(|s| s.to_string());
    drop(dm);

    let device_id = device_id?;

    // Online status
    if attr_id == "online" {
        let online = payload.as_str().map(|s| s.eq_ignore_ascii_case("ON")).unwrap_or(false);
        return Some(json!({
            "event": "ATTR_UPDATED",
            "device_id": device_id,
            "attr": "online",
            "value": if online { true } else { false },
            "online": online,
        }));
    }

    // Regular attr update
    let mut ev = json!({
        "event": "ATTR_UPDATED",
        "device_id": device_id,
        "attr": attr_id,
        "value": payload.clone(),
    });
    if let Some(cmd) = state.take_ack(&device_id).await {
        ev["ack_command_id"] = Value::String(cmd);
    }
    Some(ev)
}

async fn events_stream(
    State(state): State<Arc<HttpApiState>>,
) -> Sse<impl Stream<Item = Result<SseEvent, Infallible>>> {
    let bus = state.bus.clone();

    // Send the current online state for every device first, so the HMI dot is
    // correct on boot/reconnect even for devices that never emitted a fresh
    // online=edge event (e.g. a device that was already online continuously).
    let mut events: Vec<SseEvent> = Vec::new();
    {
        let dm = state.device_manager.read().await;
        let online = state.device_online.lock().await;
        for device in dm.get_all_devices().values() {
            let Some(device_id) = dm.device_id_of(device) else { continue; };
            let on = online.get(&device.key()).copied().unwrap_or(false);
            events.push(SseEvent::default().data(
                json!({
                    "event": "ATTR_UPDATED",
                    "device_id": device_id,
                    "attr": "online",
                    "value": on,
                    "online": on,
                })
                .to_string(),
            ));
        }
    }

    let snapshot = bus.snapshot().await;
    let rx = bus.subscribe();

    // Combine the fresh online state with the retained event history, both
    // translated to SSE JSON, then emit them before the live stream.
    let mut snapshot_events: Vec<Result<SseEvent, Infallible>> = events.into_iter().map(Ok).collect();
    {
        let state_ref = state.clone();
        for e in snapshot {
            if let Some(v) = translate_event_blocking(&state_ref, &e.topic, &e.payload) {
                snapshot_events.push(Ok(SseEvent::default().data(v.to_string())));
            }
        }
    }
    let snapshot_stream = tokio_stream::iter(snapshot_events);

    let state_ref = state.clone();
    let live_stream = BroadcastStream::new(rx)
        .then(move |res| {
            let state = state_ref.clone();
            async move {
                match res {
                    Ok(event) => {
                        translate_event(&state, &event.topic, &event.payload)
                            .await
                            .map(|val| {
                                Ok::<SseEvent, Infallible>(
                                    SseEvent::default().data(val.to_string()),
                                )
                            })
                    }
                    Err(e) => {
                        warn!("SSE stream lagged, dropping events: {}", e);
                        None
                    }
                }
            }
        })
        .filter_map(|x| x);

    Sse::new(snapshot_stream.chain(live_stream))
        .keep_alive(KeepAlive::new().interval(Duration::from_secs(15)).text("ping"))
}

/// Blocking fallback of translate_event for the initial snapshot (no device
/// lookup is async here because we only need the device_manager read lock,
/// which we take synchronously — the async version is used for live events).
fn translate_event_blocking(
    state: &Arc<HttpApiState>,
    topic: &str,
    payload: &Value,
) -> Option<Value> {
    let parts: Vec<&str> = topic.split('/').collect();
    if parts.len() != 4 || parts[0] != "bms" {
        return None;
    }
    let (type_slug, idx_str, attr_id) = (parts[1], parts[2], parts[3]);
    let idx: i64 = idx_str.parse().ok()?;

    let nr_type = match type_slug {
        "ac" => "ac_controller",
        "sign" => "mcb",
        "power" => "power_meter",
        "light" => "light_sensor",
        "switch" => "switch",
        _ => return None,
    };

    let dm = state.device_manager.try_read();
    let dm = match dm {
        Ok(d) => d,
        Err(_) => return None,
    };
    let device = dm.get_device_by_type_index(nr_type, idx)?;
    let device_id = dm.device_id_of(device)?;
    drop(dm);

    if attr_id == "online" {
        let online = payload.as_str().map(|s| s.eq_ignore_ascii_case("ON")).unwrap_or(false);
        return Some(json!({
            "event": "ATTR_UPDATED",
            "device_id": device_id,
            "attr": "online",
            "value": online,
            "online": online,
        }));
    }

    let value = payload.clone();
    let mut ev = json!({
        "event": "ATTR_UPDATED",
        "device_id": device_id,
        "attr": attr_id,
        "value": value,
    });
    if let Some(cmd) = state.take_ack_blocking(&device_id) {
        ev["ack_command_id"] = Value::String(cmd);
    }
    Some(ev)
}

/* ================= router ================= */

/// Bind and serve the HTTP API. Returns after the server stops.
pub async fn run(
    bus: EventBus,
    hmi_bridge: Arc<HmiBridge>,
    device_manager: SharedDeviceManager,
    state_manager: Arc<Mutex<StateManager>>,
    commands: Arc<CommandTracker>,
    device_online: Arc<Mutex<HashMap<String, bool>>>,
    host: &str,
    port: u16,
) -> anyhow::Result<()> {
    let state = Arc::new(HttpApiState {
        bus,
        hmi_bridge,
        device_manager,
        state_manager,
        commands,
        device_online,
    });

    let app = Router::new()
        .route("/api/health", get(health))
        .route("/api/v1/devices", get(list_devices))
        .route("/api/v1/devices/catalog", get(catalog))
        .route("/api/v1/devices/{device_id}/state", get(get_device_state))
        .route("/api/v1/devices/{device_id}/actions", post(device_action))
        .route("/api/v1/scenes/{scene_id}/actions", post(scene_action))
        .route("/api/v1/events", get(events_stream))
        .with_state(state);

    let listener = tokio::net::TcpListener::bind((host, port)).await?;
    info!("HTTP API listening on http://{}:{}", host, port);

    axum::serve(listener, app).await?;
    Ok(())
}

async fn catalog(State(state): State<Arc<HttpApiState>>) -> Json<Value> {
    let dm = state.device_manager.read().await;
    Json(dm.catalog_json())
}
