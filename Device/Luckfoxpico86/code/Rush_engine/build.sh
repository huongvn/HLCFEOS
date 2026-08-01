#!/bin/bash
# BMS Engine (Rust) Build Script
# Cross-compile for Luckfox ARM 32-bit
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

# Add ARM target if not present
ARM_TARGET="armv7-unknown-linux-gnueabihf"
if ! rustup target list --installed | grep -q "$ARM_TARGET"; then
    echo "Adding ARM target: $ARM_TARGET"
    rustup target add "$ARM_TARGET"
fi

# Install cross-compiler if needed
if ! command -v arm-linux-gnueabihf-gcc &>/dev/null; then
    echo "WARNING: arm-linux-gnueabihf-gcc not found."
    echo "Install with: sudo apt install gcc-arm-linux-gnueabihf"
    echo "Or use cargo-cross: cargo install cross"
    echo ""
    echo "Falling back to native build..."
    TARGET=""
else
    TARGET="$ARM_TARGET"
fi

echo ""
echo "Building for target: ${TARGET:-native}"

if [ "$RELEASE" = "release" ]; then
    echo "Mode: release (optimized)"
    if [ -n "$TARGET" ]; then
        cargo build --release --target "$TARGET"
        BINARY="target/$TARGET/release/bms-engine"
    else
        cargo build --release
        BINARY="target/release/bms-engine"
    fi
else
    echo "Mode: debug"
    if [ -n "$TARGET" ]; then
        cargo build --target "$TARGET"
        BINARY="target/$TARGET/debug/bms-engine"
    else
        cargo build
        BINARY="target/debug/bms-engine"
    fi
fi

echo ""
echo "=== Build Complete ==="
echo "Binary: $BINARY"
echo "Size: $(stat -c%s "$BINARY" 2>/dev/null || stat -f%z "$BINARY" 2>/dev/null) bytes"
echo ""
echo "To deploy: ./deploy.sh"
