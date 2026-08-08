#!/bin/bash
# Build OTA package for BMS Engine (Rust) - Binary only
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 1.1.0"
    exit 1
fi

echo "=== Building OTA Package v${VERSION} ==="

# Ensure binary is built
BINARY="target/armv7-unknown-linux-musleabihf/release/bms-engine"
if [ ! -f "$BINARY" ]; then
    echo "Building release binary..."
    ./build.sh release
fi

OTA_DIR="ota"
mkdir -p "$OTA_DIR"
BINARY_NAME="bms-engine"

echo "Copying binary..."
cp "$BINARY" "$OTA_DIR/$BINARY_NAME"

echo "Creating tarball..."
TARBALL="$OTA_DIR/bms_v${VERSION}.tar.gz"
tar -czf "$TARBALL" -C "$OTA_DIR" "$BINARY_NAME"

SHA256=$(sha256sum "$TARBALL" | cut -d' ' -f1)
SIZE=$(stat -c%s "$TARBALL" 2>/dev/null || stat -f%z "$TARBALL" 2>/dev/null)

echo "SHA256: $SHA256"
echo "Size: $SIZE bytes"

# Generate .sha256 sidecar file for GitHub Releases
echo "$SHA256  $TARBALL" > "$OTA_DIR/bms_v${VERSION}.tar.gz.sha256"

echo ""
echo "============================================================"
echo "OTA package built successfully!"
echo "============================================================"
echo "Package: $TARBALL"
echo "SHA256 sidecar: $OTA_DIR/bms_v${VERSION}.tar.gz.sha256"
echo "Version: $VERSION"
echo "SHA256: $SHA256"
echo "============================================================"