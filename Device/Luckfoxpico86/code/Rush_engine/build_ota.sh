#!/bin/bash
# Build OTA package for BMS Engine (Rust)
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

BINARY="target/release/bms-engine"
if [ ! -f "$BINARY" ]; then
    echo "Building release binary..."
    cargo build --release
fi

PACKAGE_NAME="bms_v${VERSION}"
OTA_DIR="ota"
mkdir -p "$OTA_DIR"
PACKAGE_DIR="$OTA_DIR/$PACKAGE_NAME"
rm -rf "$PACKAGE_DIR"

echo "Creating package structure..."
mkdir -p "$PACKAGE_DIR/config"
mkdir -p "$PACKAGE_DIR/data"

cp "$BINARY" "$PACKAGE_DIR/bms-engine"
cp config/config.yaml "$PACKAGE_DIR/config/"
cp config/rules.yaml "$PACKAGE_DIR/config/"
echo "$VERSION" > "$PACKAGE_DIR/VERSION"

echo "Creating tarball..."
TARBALL="$OTA_DIR/${PACKAGE_NAME}.tar.gz"
tar -czf "$TARBALL" -C "$OTA_DIR" "$PACKAGE_NAME"

SHA256=$(sha256sum "$TARBALL" | cut -d' ' -f1)
SIZE=$(stat -c%s "$TARBALL" 2>/dev/null || stat -f%z "$TARBALL" 2>/dev/null)

echo "SHA256: $SHA256"
echo "Size: $SIZE bytes"

cat > "$OTA_DIR/check.json" <<EOF
{
  "version": "$VERSION",
  "filename": "${PACKAGE_NAME}.tar.gz",
  "sha256": "$SHA256",
  "url": "http://192.168.1.171/ota/bms/${PACKAGE_NAME}.tar.gz",
  "release_notes": "Version $VERSION",
  "min_version": "1.0.0",
  "force_update": false
}
EOF

rm -rf "$PACKAGE_DIR"

echo ""
echo "============================================================"
echo "OTA package built successfully!"
echo "============================================================"
echo "Package: $TARBALL"
echo "Manifest: $OTA_DIR/check.json"
echo "Version: $VERSION"
echo "SHA256: $SHA256"
echo "============================================================"
