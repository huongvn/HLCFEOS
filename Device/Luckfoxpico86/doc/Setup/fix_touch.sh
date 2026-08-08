#!/bin/bash
# GT911 TouchScreen Fix for Luckfox Pico 86 Panel
# Run as: sudo bash fix_touch.sh

set -e

echo "=== GT911 TouchScreen Fix for Luckfox Pico 86 Panel ==="
echo "This script will:"
echo "  1. Fix DTB: interrupt pin, reset pin, irq-gpios, I2C address, pa-ctl-gpios"
echo "  2. Remove FIT signature (RSA) to allow modified DTB to boot"
echo "  3. Update FIT header (size + SHA256)"
echo "  4. Reboot to apply changes"
echo ""
read -p "Continue? (y/N): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted"
    exit 1
fi

# Step 0: Ensure config and load
echo "=== Step 0: Config and load ==="
cat > /etc/luckfox.cfg << EOF
SPI0_M0_CS_ENABLE=1
SPI0_M0_MODE=1
TS_ENABLE=1
UART4_M0_STATUS=1
EOF
sudo luckfox-config load
echo "Config loaded"

# Step 1: Extract DTB from boot partition
echo "=== Step 1: Extract DTB ==="
dd if=/dev/mmcblk0p4 bs=1 skip=2048 count=50000 of=/tmp/base.dtb 2>/dev/null

# Step 2: Decompile and patch
echo "=== Step 2: Decompile DTB ==="
dtc -I dtb -O dts /tmp/base.dtb -o /tmp/base.dts 2>/dev/null

# Get phandles
GPIO0_PHANDLE=$(grep -A10 'gpio@ff380000' /tmp/base.dts | grep phandle | sed 's/.*= <\(0x[0-9a-fA-F]*\)>.*/\1/')
GPIO3_PHANDLE=$(grep -A10 'gpio@ff550000' /tmp/base.dts | grep phandle | sed 's/.*= <\(0x[0-9a-fA-F]*\)>.*/\1/')
echo "GPIO0 phandle: $GPIO0_PHANDLE"
echo "GPIO3 phandle: $GPIO3_PHANDLE"

if [ -z "$GPIO0_PHANDLE" ] || [ -z "$GPIO3_PHANDLE" ]; then
    echo "ERROR: Could not find phandles"
    exit 1
fi

# Step 3: Apply fixes
echo "=== Step 3: Apply DTB patches ==="
sed -i "s/reset-gpios = <0x3b 0x04 0x01>/reset-gpios = <${GPIO3_PHANDLE} 0x18 0x01>/" /tmp/base.dts
sed -i 's/interrupts = <0x03 0x02>/interrupts = <0x00 0x02>/' /tmp/base.dts
sed -i 's/reg = <0x14>/reg = <0x5d>/' /tmp/base.dts
sed -i '/pa-ctl-gpios = <0x3b 0x00 0x00>;/d' /tmp/base.dts

# Add irq-gpios if missing
if ! grep -q 'irq-gpios' /tmp/base.dts; then
    sed -i '/compatible = "goodix,gt911"/,/pinctrl-names/ {
        /pinctrl-names/ i\                        irq-gpios = <0x3b 0x00 0x00>;
    }' /tmp/base.dts
fi

# Step 4: Compile patched DTB
echo "=== Step 4: Compile patched DTB ==="
dtc -I dts -O dtb /tmp/base.dts -o /tmp/patched.dtb 2>/dev/null

# Verify
dtc -I dtb -O dts /tmp/patched.dtb 2>/dev/null | grep -A6 'goodix,gt911'
echo "pa-ctl-gpios count: $(dtc -I dtb -O dts /tmp/patched.dtb 2>/dev/null | grep -c 'pa-ctl-gpios') (should be 0)"

# Step 5: Remove FIT signature
echo "=== Step 5: Remove FIT signature ==="
dd if=/dev/mmcblk0p4 bs=1 count=2048 of=/tmp/fit_hdr.dtb 2>/dev/null
dtc -I dtb -O dts /tmp/fit_hdr.dtb -o /tmp/fit_hdr.dts 2>/dev/null
sed -i '/signature {/,/};/d' /tmp/fit_hdr.dts
dtc -I dts -O dtb /tmp/fit_hdr.dts -o /tmp/fit_hdr_new.dtb 2>/dev/null

# Verify signature removed
SIG_COUNT=$(dtc -I dtb -O dts /tmp/fit_hdr_new.dtb 2>/dev/null | grep -c 'signature')
echo "Signature nodes: $SIG_COUNT (should be 0)"

# Step 6: Write patched DTB and unsigned header to disk
echo "=== Step 6: Write to disk ==="
dd if=/tmp/patched.dtb of=/dev/mmcblk0p4 bs=1 seek=2048 conv=notrunc 2>/dev/null
dd if=/tmp/fit_hdr_new.dtb of=/dev/mmcblk0p4 bs=1 seek=0 count=2048 conv=notrunc 2>/dev/null

# Step 7: Update FIT header (size + SHA256)
echo "=== Step 7: Update FIT header (size + SHA256) ==="
SIZE=$(stat -c%s /tmp/patched.dtb)
SIZE_HEX=$(printf '%x' $SIZE)
SHA=$(sha256sum /tmp/patched.dtb | awk '{print $1}')
HASH_FORMATTED=$(echo $SHA | sed 's/.\{8\}/0x& /g')
echo "SIZE=$SIZE (0x$SIZE_HEX) SHA=$SHA"

cat > /tmp/hdr.dts << HDR_EOF
/dts-v1/;
/plugin/;
&{/images/fdt}{
    data-size=<0x${SIZE_HEX}>;
    hash{ value=<${HASH_FORMATTED}>; };
};
HDR_EOF

dtc -I dts -O dtb /tmp/hdr.dts -o /tmp/hdr.dtbo 2>/dev/null

# Extract current header (without signature)
dd if=/dev/mmcblk0p4 bs=1 count=2048 of=/tmp/current_hdr.dtb 2>/dev/null
fdtoverlay -i /tmp/current_hdr.dtb -o /tmp/new_hdr.dtb /tmp/hdr.dtbo 2>/dev/null

# Write updated header
dd if=/tmp/new_hdr.dtb of=/dev/mmcblk0p4 bs=1 seek=0 count=2048 conv=notrunc 2>/dev/null

echo ""
echo "=== DONE ==="
echo "All changes written to /dev/mmcblk0p4"
echo "Reboot now to apply: sudo reboot"
echo ""
read -p "Reboot now? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo reboot
fi