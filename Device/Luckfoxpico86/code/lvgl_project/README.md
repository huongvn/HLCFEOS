# LVGL System Monitor - Canopi Smart Room Controller

[![LVGL](https://img.shields.io/badge/LVGL-v9.6-blue)](https://lvgl.io)
[![Platform](https://img.shields.io/badge/Platform-Luckfox%20Pico-green)](https://wiki.luckfox.com)
[![Architecture](https://img.shields.io/badge/Architecture-ARM%2032--bit-orange)](https://en.wikipedia.org/wiki/ARM_architecture)
[![License](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)

Smart room controller application for **Luckfox Pico Panel 86** using **LVGL v9.6** graphics library. check repo
repo
---

## 📋 Features

### 1. System Monitor Page
- ✅ **CPU Usage**: Real-time monitoring with moving average (10Hz update)
- ✅ **RAM Usage**: Memory usage percentage (with underflow protection)
- ✅ **IP Address**: Multi-interface display (active interfaces only)
- ✅ **Temperature**: Color-coded warnings (<50°C green, 50-70°C orange, ≥70°C red)
- ✅ **CANOPI Logo**: Custom branding display
- ✅ **Navigation**: Button to Smart Room page
- ✅ **Settings Access**: Gear icon to access Settings (PIN protected)
- ✅ **Auto-pause**: Timer skips updates when page is hidden

### 2. Smart Room Controller Page
- ✅ **Temperature Arc**: 16-32°C range with 270° arc control and **+/- buttons**
- ✅ **Mode Selector**: Cool / Dry / Heat (radio-style, refactored with helper)
- ✅ **Fan Speed Slider**: 0-100% with real-time percentage display
- ✅ **Eco Mode Toggle**: Energy saving mode
- ✅ **DND Button**: Do Not Disturb (boolean state tracking)
- ✅ **MMR Button**: Make My Room (mutually exclusive with DND)
- ✅ **Header Bar**: AC (Power) button on left, Time/Room Temp/Welcome centered, Canopi Logo on right
- ✅ **Room Temperature**: Displayed in header center
- ✅ **Power Button (AC)**: Square button with icon + text, style matching mode buttons
- ✅ **MQTT Integration**: All controls publish via libmosquitto
- ✅ **Input Validation**: MQTT sensor payloads validated before display

### 3. Settings Page (PIN Protected)
- ✅ **PIN Authentication**: 4-digit PIN entry before accessing settings (default: `1234`)
- ✅ **Network Tab**: MQTT Broker IP, Port, Room ID configuration
- ✅ **Display Tab**: Real-time brightness control slider, PIN change
- ✅ **System Tab**: Hardware/Software version info, Reboot button
- ✅ **Language Selection**: Toggle between English (Montserrat) and Vietnamese (Roboto with FontAwesome integration)
- ✅ **Persistent Storage**: All settings (including Smart Room states) saved to `app_config.txt`
- ✅ **Smart Room Persistence**: Save/Restore Temperature, Mode, Fan Speed, etc., across reboots with MQTT synchronization.
- ✅ **Config Manager**: Auto-load settings on startup, apply brightness immediately
- ✅ **OTA Update System**: Automated Over-The-Air updates with 3-point safety strategy (Pre-write validation, Atomic Swap, Health Watchdog).

---

## 🖥️ UI Layout

### System Monitor Page
```
┌─────────────────────────────────────┐
│ [Master Panel]  System Monitor     │
│ [Smart Room]      Temperature      │
│     [CANOPI LOGO]                  │
│                                     │
│   ┌───────────────────────────┐     │
│   │   CPU Usage               │     │
│   │   [████████░░] CPU: 45%   │     │
│   │   ─────────────────       │     │
│   │   RAM Usage               │     │
│   │   [██████░░░░] RAM: 38%   │     │
│   └───────────────────────────┘     │
│                                     │
│   ┌───────────────────────────┐     │
│   │   IP Address              │     │
│   │   192.168.1.100           │     │
│   └───────────────────────────┘     │
└─────────────────────────────────────┘
```

### Smart Room Controller Page
```
┌─────────────────────────────────────────────┐
│ [AC]    09:00  17/03/2025         Canopi  │
│         Room: 24°C                          │
│         Welcome!                            │
├─────────────────────────────────────────────┤
│  [-]  ╭───────╮  [+]    Mode               │
│       │  25°  │          ❄️ Cool (active)  │
│       │Temperature│      💧 Dry            │
│       │Occupied │        🌡️ Heat           │
│       ╰───────╯                             │
├─────────────────────────────────────────────┤
│ Fan Speed                                   │
│ [████████░░░░░░░░] 40%  ⚡🌿 Eco           │
├─────────────────────────────────────────────┤
│  🔕                  🧹                     │
│  Do Not Disturb     Make My Room           │
└─────────────────────────────────────────────┘
```

---

## 🏗️ Project Structure

```
lvgl_project/
├── main.c                    # Main entry point
├── Makefile                  # Build configuration
├── lv_conf.h                 # LVGL configuration
├── assets/                   # Image assets (logo, icons)
│   ├── logo.c               # CANOPI logo
│   ├── logo_med.c           # CANOPI logo (medium)
│   └── icons_*.c            # Mode and sensor icons
├── src/                     # Application modules
│   ├── ui.h                 # Main UI header
│   ├── ui.c                 # UI initialization & navigation
│   ├── ui_common.h          # Shared color palette
│   ├── system_monitor.h     # System Monitor header
│   ├── system_monitor.c     # System Monitor implementation
│   ├── smart_room.h         # Smart Room header
│   ├── smart_room.c         # Smart Room implementation
│   ├── config.h             # Configuration manager header
│   ├── config.c             # Persistent config (load/save/brightness)
│   ├── settings.h           # Settings page header
│   ├── settings.c           # Settings page (TabView UI)
│   ├── settings_auth.h      # PIN authentication header
│   └── settings_auth.c      # PIN entry popup
├── mosquitto-2.0.18/        # libmosquitto source (cross-compiled)
├── doc/                     # Documentation files
└── lvgl/                    # LVGL library (git submodule)
```

### Module Description

| Module | Files | Description |
|--------|-------|-------------|
| **main** | `main.c` | Entry point, framebuffer, LVGL init, accurate tick timing |
| **UI Core** | `src/ui.*` | UI initialization, navigation management |
| **Common** | `src/ui_common.h` | Color palette definitions |
| **System Monitor** | `src/system_monitor.*` | CPU, RAM, IP, Temperature display, Settings access |
| **Smart Room** | `src/smart_room.*` | HVAC control, MQTT, state persistence & sync |
| **Config Manager** | `src/config.*` | Persistent settings (load/save), brightness control |
- [x] **Settings Auth** | `src/settings_auth.*` | PIN entry popup for settings access 
- [x] **OTA Module** | `src/ota.*` | OTA state machine, `wget` integration, atomic swap logic
- [x] **Settings** | `src/settings.*` | Settings page with TabView (Network, Display, System, Update)

---

## 🔌 MQTT Integration

### libmosquitto (Direct MQTT client)

All sensor data is received and all control changes are published via **libmosquitto** (compiled statically from source for ARM).

**Broker:**
```
Mosquitto MQTT broker at 127.0.0.1:1883
```

**Topics (Publish):**

| Feature | Topic | Payload |
|---------|-------|---------|
| Temperature Set | `room/temperature/set` | `25` |
| Mode Set | `room/mode/set` | `Cool` / `Dry` / `Heat` |
| Fan Speed | `room/fan/speed` | `40` |
| Eco Mode | `room/eco/mode` | `ON` / `OFF` |
| Power | `room/power/set` | `true` / `false` |
| DND (Smart Room) | `room/dnd/status` | `ON` / `OFF` |
| MMR (Smart Room) | `room/mmr/status` | `ON` / `OFF` |

**Topics (Subscribe):**

| Feature | Topic | Payload |
|---------|-------|---------|
| Humidity | `sensors/humidity` | `55` (validated) |
| CO₂ | `sensors/co2` | `700` (validated) |
| Outdoor Temp | `sensors/outdoor_temp` | `24` (validated) |
| Room Temp | `sensors/room_temp` | `25` (validated) |
| Occupied | `sensors/occupied` | `true` / `false` |
| Room Name | `room/name` | `Room 1422 East` |
| Welcome | `room/welcome` | `Welcome Mr. VIP!` |
| Remote Temperature | `control/temperature` | `25` |
| Remote Mode | `control/mode` | `Cool`, `Dry`, `Heat` |
| Remote Fan Speed | `control/fan` | `0` - `100` |
| Remote Eco Mode | `control/eco` | `ON`, `true` |
| Remote Power | `control/power` | `ON`, `true` |
| Remote DND | `control/dnd` | `ON`, `true` |
| Remote MMR | `control/mmr` | `ON`, `true` |
| Screen Sleep Timeout| `control/sleep_timeout` | `60` (seconds, 0 to disable) |

**Test with mosquitto_pub:**
```bash
# Test temperature set
mosquitto_pub -h 127.0.0.1 -t room/temperature/set -m "25"

# Test mode set
mosquitto_pub -h 127.0.0.1 -t room/mode/set -m "Cool"
```

---

## 🛠️ Requirements

### Host PC (Ubuntu):
```bash
# Install cross-compiler
sudo apt-get update
sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

### Target (Luckfox Pico):
- Linux Kernel 5.10+
- Framebuffer `/dev/fb0` available
- 720x720 Panel 86 display
- Root/sudo permission for framebuffer access
- Mosquitto broker (for MQTT features)

---

## 📦 Build & Deploy

### 1. Clone Repository
```bash
git clone --recursive https://github.com/huongvn/lvgl_project.git
cd lvgl_project
```

### 2. Build on Host PC
```bash
# Clean previous builds
make clean

# Build with specific version (default is 1.1.0)
make APP_VERSION=1.2.3
```
This will generate:
- `app`: The main binary for local testing.
- `ota/app_v1.2.3`: Versioned binary for the OTA server.
- `ota/check.json`: Manifest for the OTA server including SHA-256.

### 3. OTA Server Setup
1.  Copy the content of the `ota/` folder to your web server (e.g., at `192.168.1.171/ota/`).
2.  The application will compare its internal version with `check.json` and prompt for update if they differ.

### 4. Deploy to Luckfox
```bash
scp app ubuntu@luckfox:/home/pico/
```

### 4. Run on Device
```bash
ssh ubuntu@luckfox
cd /tmp
chmod +x app
sudo ./app
```

### 5. Exit Application
Press **Q** or **Ctrl+C**

### 6. Navigation
- **System Monitor**: Default page
- **Smart Room**: Click "Smart Room" button
- **Settings**: Click gear icon on System Monitor → Enter PIN → Settings page
- **Back**: Return to previous page

---

## 🌲 Git Branches

| Branch | Description |
|--------|-------------|
| `main` | Main stable branch |
| `dev_test_touch` | Touch testing with counter |
| `dev_rcu_ui` | Master Panel + MQTT integration |
| `dev_lvgl_ui_demo` | Smart Room Controller (current) |

**Checkout branch:**
```bash
git checkout dev_lvgl_ui_demo
```

---

## ⚙️ Configuration

### Change Logo

1. **Prepare PNG image:**
   - Size: 100x47 pixels
   - Format: PNG with transparency

2. **Convert to C array:**
   - Visit: https://lvgl.io/tools/imageconverter
   - Upload logo PNG
   - Settings:
     ```
     Color format: ARGB8888
     Background color: #E0E0E0
     ```
   - Download `.c` file

3. **Replace file:**
   ```bash
   cp new_logo.c assets/logo.c
   ```

4. **Rebuild:**
   ```bash
   make clean && make
   ```

### Change Colors

Edit color definitions in `src/ui_common.h`:
```c
// Smart Room colors
#define COLOR_ROOM_BG       lv_color_hex(0xE8F5EE)
#define COLOR_ROOM_DARK     lv_color_hex(0x1A3C2E)
#define COLOR_ROOM_GREEN    lv_color_hex(0x2E7D32)
```

### Change Update Rate

Edit timer interval in module files:
```c
// system_monitor.c
g_sysmon.update_timer = lv_timer_create(update_timer_cb, 100, &g_sysmon);
// 100ms = 10Hz
```

---

## 🎨 Color Palette

| Name | Hex | Usage |
|------|-----|-------|
| Background Light | `#E0E0E0` | System Monitor background |
| Room Background | `#E8F5EE` | Smart Room background |
| Primary Dark | `#1A3C2E` | Header bar, titles |
| Primary Green | `#2E7D32` | Active elements |
| Light Green | `#C8E6C9` | Inactive buttons |
| Accent Green | `#66BB6A` | Logo, borders |
| Dark Navy | `#0D1B2A` | Fan slider background |
| White | `#FFFFFF` | Cards, text |
| Gray | `#757575` | Secondary text |

---

## 📊 Performance Metrics

| Metric | Value |
|--------|-------|
| **Resolution** | 720x720 pixels |
| **Color Depth** | 32-bit (RGB888) |
| **Buffer Size** | 1/10 screen (~52KB) |
| **Update Rate** | CPU/RAM: 10Hz, IP: 0.5Hz, Time: 1Hz |
| **Binary Size** | ~1.9MB |
| **RAM Usage** | ~200KB |
| **CPU Usage** | < 5% idle |
| **Main Loop** | ~1000 Hz (1ms, clock_gettime accurate tick) |

---

## 🔧 Troubleshooting

### White screen / No display

```bash
# Check framebuffer
ls -la /dev/fb0

# Fix permissions
sudo chmod 666 /dev/fb0
```

### Build error: `arm-linux-gnueabihf-gcc: not found`

```bash
# Install cross-compiler
sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

### Build error: `lvgl.h: No such file or directory`

```bash
# Initialize submodules
git submodule update --init --recursive
```

### Touch not working

- Luckfox Pico Panel 86 **does not have touch controller** in default kernel
- Need kernel compiled with `CONFIG_TOUCHSCREEN_GOODIX=y`
- Or use physical buttons (adc-keys) if available

### MQTT not working

```bash
# Check Mosquitto broker status
ps aux | grep mosquitto

# Start broker if not running
mosquitto -d -p 1883

# Test subscribe
mosquitto_pub -h 127.0.0.1 -t sensors/humidity -m 55
```

---

## 📸 Demo

### System Monitor
```
System Monitor
   [CANOPI Logo]

   CPU: ████████░░ 45%
   RAM: ██████░░░░ 38%

   IP: 192.168.1.100
   Temperature: 57°C 🟢
```

### Smart Room Controller
```
[AC]    09:00  17/03/2025      Canopi
        Room: 24°C
        Welcome!

 [-] Temperature: 25° [+]
 Mode: Cool (active)
 Fan Speed: 40%
 Eco Mode: OFF

 Do Not Disturb     Make My Room
```

---

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create feature branch: `git checkout -b feature/my-feature`
3. Commit changes: `git commit -am 'Add my feature'`
4. Push to branch: `git push origin feature/my-feature`
5. Create Pull Request

---

## 📝 License

This project uses:

- **LVGL**: [MIT License](https://github.com/lvgl/lvgl/blob/master/LICENCE.txt)
- **Application Code**: MIT License - Free for personal and commercial use

See [LICENSE](LICENSE) file for details.

---

## 👤 Author

**Developed for Luckfox Pico Panel 86 display**

- GitHub: [@huongvn](https://github.com/huongvn)
- Repository: [github.com/huongvn/lvgl_project](https://github.com/huongvn/lvgl_project)

---

## 📚 References

- [LVGL Documentation](https://docs.lvgl.io/)
- [Luckfox Wiki](https://wiki.luckfox.com/)
- [Linux Framebuffer Documentation](https://www.kernel.org/doc/html/latest/fb/index.html)
- [NanoMQ Documentation](https://nanomq.io/)
- [Eclipse Mosquitto](https://mosquitto.org/)

---

## 🗺️ Roadmap

- [x] Modular code structure (each screen in separate file)
- [x] Smart Room Controller with temperature arc
- [x] Mode selector (Cool/Dry/Heat)
- [x] Fan speed slider with Eco mode
- [x] MQTT integration for all controls (libmosquitto)
- [x] All text in English
- [x] Add MQTT subscribe for remote commands
- [x] Code review: accurate LVGL tick timing (clock_gettime)
- [x] Code review: input validation on MQTT payloads
- [x] Code review: boolean state for DND/MMR buttons
- [x] Code review: auto-pause timers when page hidden
- [x] Code review: refactored mode buttons with helper function
- [x] Add settings page with PIN authentication
- [x] Add persistent configuration (app_config.txt)
- [x] Add real-time brightness control
- [x] Redesign Smart Room header (AC button, centered info)
- [x] Add +/- buttons for temperature arc
- [x] Add Vietnamese language support and font mapping
- [x] Set default landing page to Smart Room
- [ ] Add network traffic widget
- [ ] Add dark/light theme support
- [ ] Add animations for transitions

---

**⭐ If you find this project useful, please star the repository!**

**Happy coding! 🎉**
