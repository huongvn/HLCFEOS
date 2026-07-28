#!/bin/bash
# Deploy OTA package to server
#
# Usage:
#   ./deploy_ota.sh <version>
#
# Example:
#   ./deploy_ota.sh 1.1.0

set -e

# Configuration
OTA_SERVER="pico@192.168.1.171"
OTA_DIR="/var/www/ota_root/bms"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check arguments
if [ -z "$1" ]; then
    log_error "Version not specified"
    echo "Usage: $0 <version>"
    echo "Example: $0 1.1.0"
    exit 1
fi

VERSION=$1

# Validate version format
if ! echo "$VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    log_error "Invalid version format: $VERSION"
    echo "Version should be in format: X.Y.Z (e.g., 1.1.0)"
    exit 1
fi

# Check if OTA package exists
PACKAGE_NAME="bms_v${VERSION}"
TARBALL="ota/${PACKAGE_NAME}.tar.gz"
MANIFEST="ota/check.json"

if [ ! -f "$TARBALL" ]; then
    log_error "OTA package not found: $TARBALL"
    log_info "Building package first..."
    python3 build_ota.py "$VERSION"
fi

if [ ! -f "$MANIFEST" ]; then
    log_error "Manifest not found: $MANIFEST"
    exit 1
fi

# Display package info
log_info "Deploying OTA package:"
echo "  Version: $VERSION"
echo "  Package: $TARBALL"
echo "  Manifest: $MANIFEST"
echo "  Server: $OTA_SERVER:$OTA_DIR"
echo ""

# Ask for confirmation
read -p "Continue? [y/N] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    log_warn "Deployment cancelled"
    exit 0
fi

# Create remote directory
log_info "Creating remote directory..."
ssh "$OTA_SERVER" "mkdir -p $OTA_DIR" || {
    log_error "Failed to create remote directory"
    exit 1
}

# Copy tarball
log_info "Copying tarball to server..."
scp "$TARBALL" "$OTA_SERVER:$OTA_DIR/" || {
    log_error "Failed to copy tarball"
    exit 1
}

# Copy manifest
log_info "Copying manifest to server..."
scp "$MANIFEST" "$OTA_SERVER:$OTA_DIR/" || {
    log_error "Failed to copy manifest"
    exit 1
}

# Verify deployment
log_info "Verifying deployment..."
ssh "$OTA_SERVER" "ls -lh $OTA_DIR/${PACKAGE_NAME}.tar.gz $OTA_DIR/check.json" || {
    log_error "Failed to verify deployment"
    exit 1
}

# Display success message
echo ""
echo "============================================================"
log_info "OTA package deployed successfully!"
echo "============================================================"
echo "  Version: $VERSION"
echo "  Server: $OTA_SERVER:$OTA_DIR"
echo "  Files:"
echo "    - $OTA_DIR/${PACKAGE_NAME}.tar.gz"
echo "    - $OTA_DIR/check.json"
echo "============================================================"
echo ""
echo "Next steps:"
echo "  1. Devices will automatically check for updates (every hour)"
echo "  2. Or trigger manual check on device:"
echo "     sudo systemctl restart bms-engine"
echo "  3. Monitor logs on device:"
echo "     sudo journalctl -u bms-engine -f | grep OTA"
echo ""
