#!/bin/bash
# BMS Engine Deployment Script

set -e

echo "=== BMS Engine Deployment ==="

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Error: This script must be run as root (use sudo)"
    exit 1
fi

# Install dependencies
echo "Installing Python dependencies..."
pip3 install -r requirements.txt

# Create log directory
echo "Creating log directory..."
mkdir -p /var/log
touch /var/log/bms-engine.log
chmod 644 /var/log/bms-engine.log

# Create data directory for SQLite database
echo "Creating data directory..."
mkdir -p /data/bms
chown -R pico:pico /data/bms

# Install systemd service
echo "Installing systemd service..."
cp bms-engine.service /etc/systemd/system/
systemctl daemon-reload

# Enable and start service
echo "Enabling and starting service..."
systemctl enable bms-engine
systemctl restart bms-engine

# Check service status
echo ""
echo "=== Service Status ==="
systemctl status bms-engine --no-pager

echo ""
echo "=== Deployment Complete ==="
echo "Service is running. Check logs with: sudo journalctl -u bms-engine -f"
echo "Or: sudo tail -f /var/log/bms-engine.log"
