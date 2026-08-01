#!/bin/bash
# BMS Engine (Rust) Deployment Script
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== BMS Engine (Rust) Deployment ==="

if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)"
    exit 1
fi

# Build if binary doesn't exist
BINARY="target/release/bms-engine"
if [ ! -f "$BINARY" ]; then
    echo "Binary not found, building..."
    if command -v cargo &>/dev/null; then
        cargo build --release
    else
        echo "Error: Rust/cargo not found"
        exit 1
    fi
fi

INSTALL_DIR="/home/pico/bms-engine"
BINARY_NAME="bms-engine"

echo "Creating install directory: $INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR/config"
mkdir -p "$INSTALL_DIR/data"

echo "Copying binary..."
cp "$BINARY" "$INSTALL_DIR/$BINARY_NAME"
chmod +x "$INSTALL_DIR/$BINARY_NAME"

echo "Copying configuration..."
cp config/config.yaml "$INSTALL_DIR/config/"
cp config/rules.yaml "$INSTALL_DIR/config/"
cp VERSION "$INSTALL_DIR/"

echo "Setting permissions..."
chown -R pico:pico "$INSTALL_DIR"

echo "Installing systemd service..."
cp bms-engine.service /etc/systemd/system/
systemctl daemon-reload

echo "Enabling and starting service..."
systemctl enable bms-engine
systemctl restart bms-engine

echo ""
echo "=== Service Status ==="
systemctl status bms-engine --no-pager

echo ""
echo "=== Deployment Complete ==="
echo "Check logs with: sudo journalctl -u bms-engine -f"
