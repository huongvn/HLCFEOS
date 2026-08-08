#!/bin/bash
# BMS Engine (Rust) Build Script
# Cross-compile for Luckfox ARM 32-bit (musl static)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RELEASE="${1:-debug}"  # "release" or "debug"

echo "=== BMS Engine (Rust) Build ==="

# Check Rust toolchain
if ! command -v cargo &>/dev/null; then
    echo "Error: Rust toolchain not found. Install from https://rustup.rs"
    exit 1
fi

# Add ARM musl target if not present
ARM_TARGET="armv7-unknown-linux-musleabihf"
if ! rustup target list --installed | grep -q "$ARM_TARGET"; then
    echo "Adding ARM target: $ARM_TARGET"
    rustup target add "$ARM_TARGET"
fi

# Check zig (musl cross-linker)
if ! command -v zig &>/dev/null; then
    echo "Error: zig not found."
    echo "Install: download from https://ziglang.org/download and add to PATH"
    echo "  e.g.  curl -L -o /tmp/zig.tar.xz https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz"
    echo "        mkdir -p ~/zig && tar xJf /tmp/zig.tar.xz -C ~/zig --strip-components=1"
    echo "        export PATH=\"\$HOME/zig:\$PATH\""
    exit 1
fi

# Check cargo-zigbuild
if ! command -v cargo-zigbuild &>/dev/null; then
    echo "Error: cargo-zigbuild not found."
    echo "Install with: cargo install cargo-zigbuild"
    exit 1
fi

echo ""
echo "Building for target: $ARM_TARGET (musl static)"

if [ "$RELEASE" = "release" ]; then
    echo "Mode: release (optimized)"
    cargo zigbuild --release --target "$ARM_TARGET"
    BINARY="target/$ARM_TARGET/release/bms-engine"
else
    echo "Mode: debug"
    cargo zigbuild --target "$ARM_TARGET"
    BINARY="target/$ARM_TARGET/debug/bms-engine"
fi

# Verify statically linked (must NOT require glibc of the dev machine)
echo ""
echo "=== Verify (must be statically linked) ==="
if file "$BINARY" | grep -q "statically linked"; then
    echo "OK: statically linked"
else
    echo "WARNING: binary is dynamically linked - may fail on board due to glibc version"
fi

echo ""
echo "=== Build Complete ==="
echo "Binary: $BINARY"
echo "Size: $(stat -c%s "$BINARY" 2>/dev/null || stat -f%z "$BINARY" 2>/dev/null) bytes"
echo ""
echo "To deploy: ./deploy.sh"
