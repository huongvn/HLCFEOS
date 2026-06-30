#!/bin/bash
# =============================================================================
# Node-RED BMS - Deploy Script for Luckfox Core1106
# =============================================================================
# Usage:
#   ./deploy.sh          # Deploy combined flows.json
#   ./deploy.sh --backup # Backup current flows before deploy
#   ./deploy.sh --init   # First time: install nodes + init DB
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FLOWS_DIR="${SCRIPT_DIR}/flows"
NODE_RED_DIR="${NODE_RED_DIR:-/data/node-red}"
BMS_DB_DIR="/data/bms"
BMS_DB="${BMS_DB_DIR}/bms.db"
FLOWS_TARGET="${NODE_RED_DIR}/flows.json"
BACKUP_DIR="${NODE_RED_DIR}/backups"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*"; }

# ---------------------------------------------------------------------------
# Functions
# ---------------------------------------------------------------------------

check_prerequisites() {
    log "Checking prerequisites..."

    if ! command -v node-red &>/dev/null; then
        err "node-red not found. Install: npm install -g --unsafe-perm node-red"
        exit 1
    fi
    log "  node-red: $(node-red --version 2>/dev/null || echo 'found')"

    if ! command -v nanomq &>/dev/null; then
        err "nanomq not found"
        exit 1
    fi
    log "  nanomq: found"

    if [ ! -f "${FLOWS_DIR}/bms-flows-combined.json" ]; then
        err "Combined flows file not found: ${FLOWS_DIR}/bms-flows-combined.json"
        exit 1
    fi
    log "  flows file: OK"
}

install_nodes() {
    log "Installing Node-RED palette nodes..."
    cd "${NODE_RED_DIR}" 2>/dev/null || mkdir -p "${NODE_RED_DIR}" && cd "${NODE_RED_DIR}"

    npm list node-red-node-sqlite &>/dev/null || {
        log "  Installing node-red-node-sqlite..."
        npm install node-red-node-sqlite
    }

    npm list @flowfuse/node-red-dashboard &>/dev/null || {
        log "  Installing @flowfuse/node-red-dashboard..."
        npm install @flowfuse/node-red-dashboard
    }

    log "  Palette nodes: OK"
}

init_database() {
    log "Initializing database..."
    mkdir -p "${BMS_DB_DIR}"

    if [ -f "${BMS_DB}" ]; then
        log "  Database exists: ${BMS_DB}"
        log "  Tables:"
        sqlite3 "${BMS_DB}" ".tables" | while read -r line; do
            log "    ${line}"
        done
        return
    fi

    # Create database with schema
    sqlite3 "${BMS_DB}" <<'SQL'
CREATE TABLE IF NOT EXISTS device_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          TEXT    NOT NULL,
    device_id   TEXT    NOT NULL,
    device_type TEXT,
    location    TEXT,
    payload     TEXT,
    event       TEXT
);
CREATE INDEX IF NOT EXISTS idx_log_ts        ON device_log(ts);
CREATE INDEX IF NOT EXISTS idx_log_device_ts ON device_log(device_id, ts);
CREATE INDEX IF NOT EXISTS idx_log_event     ON device_log(event);

CREATE TABLE IF NOT EXISTS device_config (
    device_id     TEXT PRIMARY KEY,
    device_type   TEXT,
    location      TEXT,
    friendly_name TEXT,
    enabled       INTEGER DEFAULT 1,
    extra         TEXT
);

CREATE TABLE IF NOT EXISTS schedule (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT    NOT NULL,
    device_id     TEXT    NOT NULL,
    device_type   TEXT,
    action        TEXT    NOT NULL,
    action_params TEXT,
    sch_minute    TEXT    DEFAULT '*',
    sch_hour      TEXT    DEFAULT '*',
    sch_dow       TEXT    DEFAULT '*',
    sch_dom       TEXT    DEFAULT '*',
    sch_month     TEXT    DEFAULT '*',
    enabled       INTEGER DEFAULT 1,
    one_shot      INTEGER DEFAULT 0,
    last_run      TEXT,
    created_at    TEXT    DEFAULT (datetime('now')),
    updated_at    TEXT
);
CREATE INDEX IF NOT EXISTS idx_sch_enabled ON schedule(enabled);
CREATE INDEX IF NOT EXISTS idx_sch_device  ON schedule(device_id);

INSERT OR IGNORE INTO device_config VALUES
('0x384C', 'mcb', 'zone_A', 'MCB Tong zone A', 1,
 '{"gateway":"tasmota_6DCAA8","power_attr":"0110","attrs":{"0110":{"key":"power","type":"bool"},"0272":{"key":"meas_0272","type":"number"},"0273":{"key":"meas_0273","type":"number"},"0274":{"key":"meas_0274","type":"number"},"0276":{"key":"meas_0276","type":"number"},"0277":{"key":"meas_0277","type":"number"},"0283":{"key":"meas_0283","type":"number"},"0466":{"key":"meas_0466","type":"number"},"0467":{"key":"meas_0467","type":"number"},"0468":{"key":"meas_0468","type":"number"},"0469":{"key":"meas_0469","type":"number"},"046B":{"key":"meas_046B","type":"number"},"046E":{"key":"meas_046E","type":"number"},"0170":{"key":"meas_0170","type":"number"},"0201":{"key":"state_0201","type":"number"},"027D":{"key":"state_027D","type":"number"},"0006":{"key":"raw_hex","type":"hex"}}}'),
('0x8150', 'ac_controller', 'zone_B', 'Dieu hoa zone B', 1,
 '{"gateway":"tasmota_6DCAA8","power_attr":"0101","ac_power_attr":"0101","ac_temp_attr":"0202","ac_mode_attr":"0405","ac_mode_map":{"off":0,"cool":1,"heat":2,"fan_only":3,"dry":4},"attrs":{"0101":{"key":"power","type":"bool"},"0202":{"key":"temperature","type":"number"},"0405":{"key":"mode","type":"number"}}}');

INSERT OR IGNORE INTO schedule
    (name, device_id, device_type, action, action_params, sch_minute, sch_hour, sch_dow) VALUES
('Bat MCB mo cua',       '0x384C', 'mcb',  'POWER_ON',  NULL, '0', '7', '1-6'),
('Tat MCB dong cua',     '0x384C', 'mcb',  'POWER_OFF', NULL, '0', '22', '1-6'),
('Bat AC truoc mo cua',  '0x8150', 'ac_controller', 'AC_SET',
 '{"temp":24,"mode":"cool"}',
 '30', '6', '1-6'),
('Tat AC sau dong cua',  '0x8150', 'ac_controller', 'AC_OFF', NULL,
 '30', '22', '1-6');
SQL

    log "  Database created with seed data: ${BMS_DB}"
}

backup_flows() {
    mkdir -p "${BACKUP_DIR}"
    local ts
    ts=$(date +%Y%m%d_%H%M%S)
    local backup_file="${BACKUP_DIR}/flows_${ts}.json"

    if [ -f "${FLOWS_TARGET}" ]; then
        cp "${FLOWS_TARGET}" "${backup_file}"
        log "Backed up existing flows to: ${backup_file}"
    else
        log "No existing flows to backup"
    fi
}

deploy_flows() {
    log "Deploying flows..."
    mkdir -p "${NODE_RED_DIR}"

    if [ -f "${FLOWS_TARGET}" ] && [ "${1:-}" != "--overwrite" ]; then
        warn "Existing flows.json found. Use --overwrite to force overwrite."
        warn "Or use --backup to backup first."
    fi

    cp "${FLOWS_DIR}/bms-flows-combined.json" "${FLOWS_TARGET}"
    log "Flows deployed to: ${FLOWS_TARGET}"
}

restart_nodered() {
    log "Restarting Node-RED..."
    if command -v systemctl &>/dev/null && systemctl is-active --quiet node-red 2>/dev/null; then
        systemctl restart node-red
    elif pgrep -f "node-red" &>/dev/null; then
        pkill -f "node-red" || true
        sleep 2
        nohup node-red --userDir "${NODE_RED_DIR}" > /var/log/node-red.log 2>&1 &
    fi
    log "Node-RED restarted"
}

show_status() {
    echo ""
    echo "============================================="
    echo "  Node-RED BMS Deployment Status"
    echo "============================================="
    echo ""

    # NanoMQ
    if pgrep -f "nanomq" &>/dev/null; then
        echo -e "  NanoMQ:    ${GREEN}RUNNING${NC} :1883"
    else
        echo -e "  NanoMQ:    ${RED}NOT RUNNING${NC}"
    fi

    # Node-RED
    if pgrep -f "node-red" &>/dev/null; then
        echo -e "  Node-RED:  ${GREEN}RUNNING${NC} :1880"
    else
        echo -e "  Node-RED:  ${RED}NOT RUNNING${NC}"
    fi

    # Database
    if [ -f "${BMS_DB}" ]; then
        local tables
        tables=$(sqlite3 "${BMS_DB}" ".tables" 2>/dev/null || echo "")
        local log_count
        log_count=$(sqlite3 "${BMS_DB}" "SELECT COUNT(*) FROM device_log" 2>/dev/null || echo "0")
        local dev_count
        dev_count=$(sqlite3 "${BMS_DB}" "SELECT COUNT(*) FROM device_config" 2>/dev/null || echo "0")
        local sch_count
        sch_count=$(sqlite3 "${BMS_DB}" "SELECT COUNT(*) FROM schedule" 2>/dev/null || echo "0")
        echo -e "  Database:  ${GREEN}OK${NC} tables=${tables} logs=${log_count} devices=${dev_count} schedules=${sch_count}"
    else
        echo -e "  Database:  ${RED}NOT FOUND${NC}"
    fi

    echo ""
    echo "  Dashboard: http://<ip>:1880/dashboard"
    echo "  Flows UI:  http://<ip>:1880"
    echo ""
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

case "${1:-}" in
    --init)
        check_prerequisites
        install_nodes
        init_database
        deploy_flows --overwrite
        restart_nodered
        show_status
        ;;
    --backup)
        check_prerequisites
        backup_flows
        deploy_flows --overwrite
        restart_nodered
        show_status
        ;;
    --status)
        show_status
        ;;
    --test)
        log "Running MQTT smoke test..."
        # Simulate Tasmota SENSOR message with MCB data
        mosquitto_pub -h localhost -p 1883 \
            -t "tele/tasmota_6DCAA8/SENSOR" \
            -m '{"ZbReceived":{"0x384C":{"Device":"0x384C","EF00/0110":1,"EF00/0273":280,"EF00/0276":800,"Endpoint":1,"LinkQuality":123}}}'
        sleep 1
        # Simulate AC controller
        mosquitto_pub -h localhost -p 1883 \
            -t "tele/tasmota_6DCAA8/SENSOR" \
            -m '{"ZbReceived":{"0x8150":{"Device":"0x8150","EF00/0101":1,"EF00/0202":25,"EF00/0405":1,"Endpoint":1,"LinkQuality":107}}}'
        sleep 1
        if [ -f "${BMS_DB}" ]; then
            log "Last 5 log entries:"
            sqlite3 "${BMS_DB}" \
                "SELECT ts, device_id, event, substr(payload,1,60) FROM device_log ORDER BY id DESC LIMIT 5;"
        fi
        ;;
    *)
        check_prerequisites
        deploy_flows
        restart_nodered
        show_status
        ;;
esac
