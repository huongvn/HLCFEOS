# Luckfox Pico Ultra W on 86 Panel Board

Hardware Configuration Guide

**UART4_M0 + GT911 TouchScreen Fix**

March 2026

## Overview

This guide documents the complete process of configuring a Luckfox Pico Ultra W module mounted on the Luckfox Pico 86 Panel board. Because no dedicated 86 Panel firmware exists, the standard Ultra W Ubuntu firmware must be patched manually.

Two subsystems are covered:

- UART4_M0 — enabling `/dev/ttyS4` via config file (no GUI required)
- GT911 TouchScreen — fixing Device Tree (DTB) pin assignments, I2C address, and driver conflicts

| **Item** | **Value** |
| --- | --- |
| Board | Luckfox Pico 86 Panel |
| Module | Luckfox Pico Ultra W |
| OS | Ubuntu firmware (Ultra W) |
| Access | SSH only (no GUI/HDMI) |
| Config file | /etc/luckfox.cfg |
| Config tool | /usr/bin/luckfox-config |
| Touch IC | Goodix GT911 @ I2C3 (0x5d) |

## Important Notes

> ⚠ **Luôn dùng `sudo luckfox-config load`** — chạy không sudo sẽ báo "Permission denied" và không áp dụng được cấu hình.

> ⚠ **FIT image có RSA signature** — firmware gốc được ký RSA. Sau khi sửa DTB, **bắt buộc xóa node `signature`** trong FIT header, nếu không U-Boot sẽ từ chối DTB mới và load bản gốc.

> ⚠ **I2C address thực tế là `0x5d`** — DTB gốc ghi `reg = <0x14>` nhưng GT911 trên board 86 Panel thực tế ở địa chỉ `0x5d`. Địa chỉ thay đổi do trạng thái chân INT lúc reset.

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

#### 5. Wrong I2C address

The original DTB had `reg = <0x14>` but the GT911 on 86 Panel actually uses address `0x5d`. The address depends on INT pin state during reset — changing the reset pin altered the address.

✓ Solution: Change `reg = <0x5d>` in the touchscreen node.

#### 6. FIT image RSA signature blocks DTB changes

The boot partition contains a FIT image with RSA-2048 signature. After modifying the DTB, the SHA256 hash no longer matches the signature. U-Boot rejects the modified DTB and falls back to the original.

✓ Solution: Remove the `signature` node from the FIT header (`/images/fdt` configuration) so U-Boot skips verification.

### Step-by-Step Fix

#### Step 1: Write config and enable UART4 + Touch

```bash
sudo bash -c 'cat > /etc/luckfox.cfg << EOF
SPI0_M0_CS_ENABLE=1
SPI0_M0_MODE=1
TS_ENABLE=1
UART4_M0_STATUS=1
EOF'
sudo luckfox-config load
```

> ⚠ Luôn dùng `sudo` với `luckfox-config load`. Chạy không sudo sẽ báo lỗi "Permission denied" cho `/dev/iomux` và configfs.

#### Step 2: Verify GT911 is reachable via I2C

First unbind the driver so I2C is free, then read the product ID register:

```bash
echo '3-005d' | sudo tee /sys/bus/i2c/drivers/Goodix-TS/unbind
sudo i2ctransfer -y 3 w2@0x5d 0x81 0x40 r6
# Expected: 0x39 0x31 0x31 ... (ASCII '911')
```

#### Step 3: Extract and decompile the current DTB

```bash
sudo dd if=/dev/mmcblk0p4 bs=1 skip=2048 count=50000 of=/tmp/current.dtb 2>/dev/null
dtc -I dtb -O dts /tmp/current.dtb -o /tmp/current.dts 2>/dev/null
```

> ⚠ Dùng `count=50000` đủ lớn để đọc hết DTB. Kích thước thực tế đọc từ FDT header (`data-size`).

#### Step 4: Find correct phandles

Phandles are reassigned each time overlays are applied. Always look them up from the current DTB:

```bash
# Find gpio0 phandle
grep -A10 'gpio@ff380000' /tmp/current.dts | grep phandle

# Find gpio3 phandle
grep -A10 'gpio@ff550000' /tmp/current.dts | grep phandle
```

#### Step 5: Patch the touchscreen node

Replace the incorrect values and add the missing irq-gpios property. In this session the phandles were: gpio0 = 0x3b, gpio3 = 0x3f.

```bash
# Fix reset-gpios (use your gpio3 phandle)
sed -i 's/reset-gpios = <0x3b 0x04 0x01>/reset-gpios = <0x3f 0x18 0x01>/' /tmp/current.dts

# Fix interrupt pin (GPIO0_A0 = pin 0)
sed -i 's/interrupts = <0x03 0x02>/interrupts = <0x00 0x02>/' /tmp/current.dts

# Fix I2C address (0x14 -> 0x5d)
sed -i 's/reg = <0x14>/reg = <0x5d>/' /tmp/current.dts

# Add missing irq-gpios before pinctrl-names line
sed -i '/compatible = "goodix,gt911"/,/pinctrl-names/ {
    /pinctrl-names/ i\                        irq-gpios = <0x3b 0x00 0x00>;
}' /tmp/current.dts
```

#### Step 6: Remove audio PA conflict

```bash
sed -i '/pa-ctl-gpios = <0x3b 0x00 0x00>;/d' /tmp/current.dts

# Verify it is gone
grep 'pa-ctl-gpios' /tmp/current.dts  # must return nothing
```

#### Step 7: Remove FIT signature (critical!)

The FIT header contains an RSA signature that blocks modified DTB. Remove the signature node:

```bash
# Decompile FIT header
sudo dd if=/dev/mmcblk0p4 bs=1 count=2048 of=/tmp/fit_header.dtb 2>/dev/null
dtc -I dtb -O dts /tmp/fit_header.dtb -o /tmp/fit_header.dts 2>/dev/null

# Delete signature node from configuration
sed -i '/signature {/,/};/d' /tmp/fit_header.dts

# Recompile header
dtc -I dts -O dtb /tmp/fit_header.dts -o /tmp/fit_header_new.dtb 2>/dev/null

# Write back (no signature = no verification)
sudo dd if=/tmp/fit_header_new.dtb of=/dev/mmcblk0p4 bs=1 seek=0 count=2048 2>/dev/null
```

#### Step 8: Compile and write patched DTB

```bash
dtc -I dts -O dtb /tmp/current.dts -o /tmp/patched.dtb 2>/dev/null
sudo dd if=/tmp/patched.dtb of=/dev/mmcblk0p4 bs=1 seek=2048 2>/dev/null
```

#### Step 9: Update FDT header (size + SHA256)

The FIT image header stores the DTB size and SHA256 hash. Both must be updated:

```bash
SIZE=$(stat -c%s /tmp/patched.dtb)
SIZE_HEX=$(printf '%x' $SIZE)
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

#### Step 10: Reboot and verify

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
| I2C3 present | `ls /dev/i2c*` | /dev/i2c-3 appears |
| GT911 on I2C | `sudo i2cdetect -y 3` | 0x5d detected (UU = driver bound) |
| Driver loaded | `dmesg \| grep goodix` | ID 911, version: 1060 |
| Interrupts active | `cat /proc/interrupts \| grep gt911` | Counter increments on touch |
| Touch events | `sudo evtest /dev/input/event0` | EV_ABS X/Y events on touch |
| GPIO0_A0 as irq | `sudo cat /sys/kernel/debug/gpio` | gpio-0 labeled `irq` |
| GPIO3_D0 as reset | `sudo cat /sys/kernel/debug/gpio` | gpio-120 labeled `reset` |
| pa-ctl-gpios removed | `ls /proc/device-tree/acodec@ff480000/pa-ctl-gpios` | No such file |

## Common Pitfalls

| **Pitfall** | **Symptom** | **Fix** |
| --- | --- | --- |
| `echo >>` to luckfox.cfg | Config binary corruption | Always use `cat >` to rewrite |
| `luckfox-config load` without sudo | Permission denied, overlay not applied | Always use `sudo luckfox-config load` |
| I2C3 + TS_ENABLE conflict | Error during luckfox-config load | Remove I2C3_M0_STATUS when TS_ENABLE=1 |
| Hardcoded phandle | Wrong GPIO, probe fails -22 | Always read phandle from current DTB |
| pa-ctl-gpios conflict | Goodix probe fails error -16 | Remove pa-ctl-gpios from acodec node |
| DTB header not updated | Boot may use wrong size | Always run fdtoverlay header update |
| Dynamic overlay -22 | overlay phandle fixup failed | Use static DTB patch instead |
| RSA signature in FIT | Modified DTB rejected, boots original | Remove signature node from FIT header |
| Wrong I2C address (0x14) | Driver binds wrong address, no touch | Change reg to <0x5d> |
| Wrong reg value 0x14 in DTB | i2cdetect shows UU at 0x14, not 0x5d | Fix reg = <0x5d> in touchscreen node |

## Automated Fix Script

Script `fix_touch.sh` thực hiện toàn bộ quy trình tự động:

Chạy **trên board Luckfox Pico 86 Panel** (target device), không phải máy host.

```bash
# SSH vào board
ssh pico@172.32.0.70
# password: luckfox@1234

# Copy script vào board (nếu chưa có)
scp /home/huongnv/projects/HLCFEOS/Device/Luckfoxpico86/doc/Setup/fix_touch.sh pico@172.32.0.70:/tmp/

# SSH vào board và chạy
ssh pico@172.32.0.70
sudo bash /tmp/fix_touch.sh
```

Lý do: Script truy cập trực tiếp hardware của board:
- `/dev/mmcblk0p4` (boot partition)
- `/sys/kernel/debug/gpio`, `/proc/device-tree`
- `/dev/i2c-3`
- Ghi firmware vào eMMC

**Lưu ý:** Script sẽ reboot board sau khi ghi xong. Đảm bảo không có tiến trình quan trọng đang chạy.

**Script thực hiện:**
1. Ghi config `/etc/luckfox.cfg` (TS_ENABLE=1, UART4_M0_STATUS=1, SPI0_M0...)
2. Chạy `sudo luckfox-config load` để enable I2C3
3. Extract DTB từ `/dev/mmcblk0p4` (offset 2048)
4. Tự động tìm phandle GPIO0, GPIO3
4. Patch DTB:
   - `interrupts = <0x00 0x02>` (GPIO0_A0)
   - `reset-gpios = <GPIO3_PHANDLE 0x18 0x01>` (GPIO3_D0)
   - `reg = <0x5d>` (địa chỉ I2C đúng)
   - Thêm `irq-gpios = <GPIO0_PHANDLE 0x00 0x00>`
   - Xóa `pa-ctl-gpios` từ audio node
5. Xóa node `signature` khỏi FIT header (bỏ qua RSA verify)
6. Compile DTB patched, ghi vào `/dev/mmcblk0p4` offset 2048
7. Update FIT header: `data-size` + SHA256 hash
8. Ghi header unsigned + DTB patched vào disk
9. Hỏi xác nhận reboot

**Lưu ý:** Script dùng `sudo` và sẽ reboot board. Chỉ chạy trên board đích (Luckfox Pico 86 Panel).

*— End of Guide —*
