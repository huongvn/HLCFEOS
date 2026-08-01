#!/bin/bash
# Deploy OTA package to server
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

OTA_SERVER="pico@192.168.1.171"
OTA_DIR="/var/www/ota_root/bms"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    echo -e "${RED}[ERROR]${NC} Version not specified"
    echo "Usage: $0 <version>"
    exit 1
fi

if ! echo "$VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo -e "${RED}[ERROR]${NC} Invalid version: $VERSION"
    exit 1
fi

PACKAGE_NAME="bms_v${VERSION}"
TARBALL="ota/${PACKAGE_NAME}.tar.gz"
MANIFEST="ota/check.json"

if [ ! -f "$TARBALL" ]; then
    echo -e "${RED}[ERROR]${NC} Package not found: $TARBALL"
    echo "Build first: ./build_ota.sh $VERSION"
    exit 1
fi

echo -e "${GREEN}[INFO]${NC} Deploying OTA package v${VERSION}"
echo "  Server: $OTA_SERVER:$OTA_DIR"
read -p "Continue? [y/N] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 0
fi

ssh "$OTA_SERVER" "mkdir -p $OTA_DIR"
scp "$TARBALL" "$OTA_SERVER:$OTA_DIR/"
scp "$MANIFEST" "$OTA_SERVER:$OTA_DIR/"

ssh "$OTA_SERVER" "ls -lh $OTA_DIR/${PACKAGE_NAME}.tar.gz $OTA_DIR/check.json"

echo ""
echo -e "${GREEN}[INFO]${NC} OTA package deployed successfully!"
echo "  Devices will check for updates automatically (every hour)"
