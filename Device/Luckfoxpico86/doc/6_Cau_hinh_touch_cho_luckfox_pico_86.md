# Luckfox Pico Ultra W on 86 Panel Board

Hardware Configuration Guide

**UART4_M0 + GT911 TouchScreen Fix**

March 2026

## Overview

This guide documents the complete process of configuring a Luckfox Pico Ultra W module mounted on the Luckfox Pico 86 Panel board. Because no dedicated 86 Panel firmware exists, the standard Ultra W Ubuntu firmware must be patched manually.

Two subsystems are covered:

- UART4_M0 — enabling `/dev/ttyS4` via config file (no GUI required)
- GT911 TouchScreen — fixing Device Tree (DTB) pin assignments and driver conflicts

| **Item** | **Value** |
| --- | --- |
| Board | Luckfox Pico 86 Panel |
| Module | Luckfox Pico Ultra W |
| OS | Ubuntu firmware (Ultra W) |
| Access | SSH only (no GUI/HDMI) |
| Config file | /etc/luckfox.cfg |
| Config tool | /usr/bin/luckfox-config |
| Touch IC | Goodix GT911 @ I2C3 (0x5d) |

## Part 1: Enabling UART4_M0

### Background

The luckfox-config tool opens a graphical TUI (ncurses) when invoked without arguments. To enable UART4 over SSH without a terminal emulator that supports full TUI, write the config file directly and call `luckfox-config load` (non-GUI mode).

> ⚠ Never use `echo >>` to append to `/etc/luckfox.cfg` — this causes binary/null byte corruption.
>
> ⚠ Always rewrite the entire file using `cat >` in a single operation.

### Known Conflicts

Do NOT include `I2C3_M0_STATUS=1` when `TS_ENABLE=1`. These conflict and cause an error during load. The touchscreen overlay already handles I2C3 internally.

### Step-by-Step

1. Write the config file (rewrite entire file):

```bash
sudo bash -c 'cat > /etc/luckfox.cfg << EOF
SPI0_M0_CS_ENABLE=1
SPI0_M0_MODE=1
TS_ENABLE=1
UART4_M0_STATUS=1
EOF'
```

2. Apply the config (non-GUI, dynamic overlay — no reboot needed):

```bash
sudo luckfox-config load
```

3. Verify UART4 appeared:

```bash
ls /dev/ttyS*
# Expected: /dev/ttyS4
```

✓ Auto-boot: `/etc/rc.local` already calls `luckfox-config load` — no extra setup needed.

### UART4 Pin Mapping

| **Signal** | **GPIO** | **Function** |
| --- | --- | --- |
| RX | GPIO1_B0 | Receive |
| TX | GPIO1_B1 | Transmit |

## Part 2: GT911 TouchScreen Fix

### Problem Description

After enabling TouchScreen via luckfox-config and rebooting, `/dev/input/event0` appears as Goodix Capacitive TouchScreen but does not respond to touch. The interrupt counter (gt911 in `/proc/interrupts`) stays at 0.

Root cause: The DTB shipped with Ultra W firmware has incorrect GPIO assignments for the 86 Panel's touch controller, and the audio codec occupies the same GPIO as the touch interrupt line.

### Hardware Pin Map (86 Panel Schematic)

| **Signal** | **GPIO** | **DTB Property** |
| --- | --- | --- |
| TP_INT (interrupt) | GPIO0_A0 | irq-gpios / interrupts |
| TP_RES (reset) | GPIO3_D0 (pin 24) | reset-gpios |
| TP_SDA | GPIO3_D1 (I2C3_M2) | — |
| TP_SCL | GPIO3_D2 (I2C3_M2) | — |

### Root Causes Found

#### 1. Wrong interrupt pin

The original DTB had `interrupts = <0x03 0x02>` (GPIO0_A3). The correct pin is GPIO0_A0, so it must be `interrupts = <0x00 0x02>`.

#### 2. Wrong reset GPIO

The original DTB had `reset-gpios = <0x3b 0x04 0x01>` which maps to GPIO0_A4. The correct pin is GPIO3_D0 (pin 24 of gpio3 bank). The correct value is `reset-gpios = <PHANDLE_GPIO3 0x18 0x01>`.

> ⚠ The phandle for gpio3 changes after `luckfox_fdt_overlay` runs. Always read it fresh from the DTB in `/dev/mmcblk0p4` — do not hardcode it.

#### 3. Missing irq-gpios property

The irq-gpios property was entirely absent from the touchscreen node. It must be added: `irq-gpios = <PHANDLE_GPIO0 0x00 0x00>`.

#### 4. Audio codec conflict on GPIO0_A0

The audio codec node (acodec @ 0xff480000) had `pa-ctl-gpios = <PHANDLE_GPIO0 0x00 0x00>` — claiming GPIO0_A0 for PA (power amplifier) control. This caused the Goodix driver to fail with error -16 (EBUSY) because the GPIO was already held by the audio driver.

✓ Solution: Remove `pa-ctl-gpios` from the audio node. Audio recording/playback still works; only the external PA control pin is disabled.

### Step-by-Step Fix

#### Step 1: Verify GT911 is reachable via I2C

First unbind the driver so I2C is free, then read the product ID register:

```bash
echo '3-005d' | sudo tee /sys/bus/i2c/drivers/Goodix-TS/unbind
sudo i2ctransfer -y 3 w2@0x5d 0x81 0x40 r6
# Expected: 0x39 0x31 0x31 ... (ASCII '911')
```

#### Step 2: Extract and decompile the current DTB

```bash
sudo dd if=/dev/mmcblk0p4 bs=1 skip=2048 count=41815 of=/tmp/current.dtb 2>/dev/null
dtc -I dtb -O dts /tmp/current.dtb -o /tmp/current.dts 2>/dev/null
```

> ⚠ The count value (41815) must match the actual DTB size. Read it from the FDT header's data-size field if unsure.

#### Step 3: Find correct phandles

Phandles are reassigned each time overlays are applied. Always look them up from the current DTB:

```bash
# Find gpio0 phandle
grep -A10 'gpio@ff380000' /tmp/current.dts | grep phandle

# Find gpio3 phandle
grep -A10 'gpio@ff550000' /tmp/current.dts | grep phandle
```

#### Step 4: Patch the touchscreen node

Replace the incorrect values and add the missing irq-gpios property. In this session the phandles were: gpio0 = 0x3b, gpio3 = 0x3f.

```bash
# Fix reset-gpios (use your gpio3 phandle)
sed -i 's/reset-gpios = <OLD_VALUE>/reset-gpios = <0x3f 0x18 0x01>/' /tmp/current.dts

# Fix interrupt pin (GPIO0_A0 = pin 0)
sed -i 's/interrupts = <0x03 0x02>/interrupts = <0x00 0x02>/' /tmp/current.dts

# Add missing irq-gpios before pinctrl-names line
sed -i '/compatible = "goodix,gt911"/,/pinctrl-names/ {
    /pinctrl-names/ i\                        irq-gpios = <0x3b 0x00 0x00>;
}' /tmp/current.dts
```

#### Step 5: Remove audio PA conflict

```bash
sed -i '/pa-ctl-gpios = <0x3b 0x00 0x00>;/d' /tmp/current.dts

# Verify it is gone
grep 'pa-ctl-gpios' /tmp/current.dts  # must return nothing
```

#### Step 6: Verify final touchscreen node

The node should look exactly like this (with your actual phandles):

```dts
touchscreen {
    status = "okay";
    compatible = "goodix,gt911";
    reg = <0x5d>;
    interrupt-parent = <0x3b>;   /* gpio0 */
    interrupts = <0x00 0x02>;    /* GPIO0_A0, edge */
    reset-gpios = <0x3f 0x18 0x01>; /* gpio3, pin 24, active-low */
    irq-gpios = <0x3b 0x00 0x00>;   /* gpio0, pin 0 */
    pinctrl-names = "default";
};
```

#### Step 7: Compile and write patched DTB

```bash
dtc -I dts -O dtb /tmp/current.dts -o /tmp/patched.dtb 2>/dev/null
SIZE=$(stat -c%s /tmp/patched.dtb)
SIZE_HEX=$(printf '%x' $SIZE)
echo "Size: $SIZE (0x$SIZE_HEX)"
sudo dd if=/tmp/patched.dtb of=/dev/mmcblk0p4 bs=1 seek=2048 2>/dev/null
```

#### Step 8: Update FDT header

The FIT image header stores the DTB size and SHA256 hash. Both must be updated:

```bash
SHA=$(sha256sum /tmp/patched.dtb | awk '{print $1}')
HASH_FORMATTED=$(echo $SHA | sed 's/.\{8\}/0x& /g')

cat > /tmp/fdt_hdr.dts << EOF
/dts-v1/;
/plugin/;
&{/images/fdt}{
    data-size = <0x0000${SIZE_HEX}>;
    hash{ value = <${HASH_FORMATTED}>; };
};
EOF

dtc -I dts -O dtb /tmp/fdt_hdr.dts -o /tmp/fdt_hdr.dtbo 2>/dev/null
sudo dd if=/dev/mmcblk0p4 bs=1 skip=0 count=2048 of=/tmp/fdt_header.dtb 2>/dev/null
fdtoverlay -i /tmp/fdt_header.dtb -o /tmp/fdt_header_new.dtb /tmp/fdt_hdr.dtbo
sudo dd if=/tmp/fdt_header_new.dtb of=/dev/mmcblk0p4 bs=1 seek=0 count=2048 2>/dev/null
echo 'Done'
```

#### Step 9: Reboot and verify

```bash
sudo reboot

# After reboot:
dmesg | grep -i 'gt9\|goodix'
# Expected: Goodix-TS 3-005d: ID 911, version: 1060

cat /proc/interrupts | grep gt911
# Expected: counter increasing when touching

sudo evtest /dev/input/event0
# Touch the screen — events should appear
```

## Final Working State

### /etc/luckfox.cfg

```
SPI0_M0_CS_ENABLE=1
SPI0_M0_MODE=1
TS_ENABLE=1
UART4_M0_STATUS=1
```

### Confirmed Results

| **Check** | **Command** | **Expected Result** |
| --- | --- | --- |
| UART4 present | `ls /dev/ttyS*` | /dev/ttyS4 appears |
| GT911 on I2C | `sudo i2cdetect -y 3` | 0x5d detected |
| Driver loaded | `dmesg \| grep goodix` | ID 911, version: 1060 |
| Interrupts active | `cat /proc/interrupts \| grep gt911` | Counter increments on touch |
| Touch events | `sudo evtest /dev/input/event0` | EV_ABS X/Y events on touch |
| GPIO0_A0 free | `sudo cat /sys/kernel/debug/gpio` | gpio-0 not listed (no pa-ctl) |

## Common Pitfalls

| **Pitfall** | **Symptom** | **Fix** |
| --- | --- | --- |
| echo >> to luckfox.cfg | Config binary corruption | Always use `cat >` to rewrite |
| I2C3 + TS_ENABLE conflict | Error during luckfox-config load | Remove I2C3_M0_STATUS when TS_ENABLE=1 |
| Hardcoded phandle | Wrong GPIO, probe fails -22 | Always read phandle from current DTB |
| pa-ctl-gpios conflict | Goodix probe fails error -16 | Remove pa-ctl-gpios from acodec node |
| DTB header not updated | Boot may use wrong size | Always run fdtoverlay header update |
| Dynamic overlay -22 | overlay phandle fixup failed | Use static DTB patch instead |

*— End of Guide —*
