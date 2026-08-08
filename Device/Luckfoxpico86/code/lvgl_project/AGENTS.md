# AGENTS.md

## Build

- **Cross-compiler**: `arm-linux-gnueabihf-gcc` (install via `apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf`)
- **Build**: `make clean && make`
- **Versioned build**: `make APP_VERSION=1.2.3` (generates `ota/app_v1.2.3` + `ota/check.json`)
- **Debug build**: `make debug` → `app_debug` (with symbols, no optimization)
- **Build mosquitto lib** (one-time): `make mosquitto` — cross-compiles `libmosquitto_static.a` from `mosquitto-2.0.18/`
- **Target**: Luckfox Pico Panel 86, ARM 32-bit, Linux 5.10+

## Architecture

Single-binary embedded UI app. No test framework, no CI.

### Page lifecycle pattern

Every page module (`system_monitor`, `smart_room`, `smart_light`, `settings`) follows this contract:
- `xxx_create(lv_obj_t *parent)` — create all LVGL objects (called once at init)
- `xxx_show()` / `xxx_hide()` — toggle visibility
- `xxx_start_timer()` / `xxx_stop_timer()` — control update timer
- `xxx_set_xxx_nav_cb(callback)` — wire navigation callbacks

Navigation is callback-based and **all wired in `src/ui.c`**. Pages never reference each other directly.

### Initialization order (see `main.c`)

1. `config_load()` — reads `app_config.txt`, applies brightness
2. `ota_init()` — starts OTA state machine
3. `lv_init()` → framebuffer setup → display + evdev input → `ui_init()`
4. `ui_set_mqtt_publish_cb(smart_room_mqtt_publish)` — connects MQTT publish path
5. `ui_start_timers()` — Smart Room's timer also **connects to MQTT broker**
6. `ota_signal_success()` — health watchdog for OTA

### MQTT

- **All MQTT is in `smart_room.c`** — owns the `struct mosquitto *mosq` client and handles connect/subscribe/publish
- `smart_room_mqtt_publish()` is the sole publish entry point (set as callback)
- Incoming messages for light/curtain are dispatched via `smart_light_handle_mqtt()`
- Broker runs locally at `127.0.0.1:1883` (configurable via Settings page)
- Config fields `mqtt_broker`, `mqtt_port`, `room_id` in `app_config.txt`

### Config

- `src/config.c` — singleton `app_config_t`, loaded from `app_config.txt` (key=value format)
- `config_load()` is called **before UI init** in `main.c`
- Defaults are hardcoded in `config.c:11-30`
- Changes via Settings page call `config_save()`

## Include paths

- `-I.` is the key: local headers use `#include "src/xxx.h"` (not `../` or bare filename)
- LVGL headers: `#include "lvgl.h"` (no path, via `-Ilvgl`)
- Mosquitto: `#include <mosquitto.h>` (via `-Imosquitto-2.0.18/include`)
- Image declarations: `LV_IMAGE_DECLARE(logo_name)` in source files (images are in `assets/`)

## LVGL

- LVGL is a **git submodule** at `lvgl/`. Clone with `--recursive` or run `git submodule update --init --recursive`
- Config at `lv_conf.h` (root level), used via `-DLV_CONF_INCLUDE_SIMPLE`
- Color depth: RGB565, framebuffer flush converts to RGB888
- Framebuffer: `/dev/fb0`, input: `/dev/input/event0` (evdev)
- Font system: dual-language via `ui_get_font(size)` / `ui_get_text(eng_str)` in `src/ui_helpers.c`

## OTA

- `APP_VERSION` passed as `-DAPP_VERSION=\"$(APP_VERSION)\"` to the compiler
- `make` builds both `app` (plain) and `ota/app_v<VERSION>` (versioned)
- `ota/check.json` contains version, download URL, SHA-256
- OTA state machine in `src/ota.c` uses `wget` for download and `rename()` for atomic swap
- After OTA reboot, `main.c` calls `ota_signal_success()` as health check

## Adding a new font

1. Add `LV_FONT_DECLARE(lv_font_xxx_NN)` to `LV_FONT_CUSTOM_DECLARE` block in `lv_conf.h`
2. Add the `.c` file to `ASSET_SRC` in `Makefile`
3. Add the font to the lookup table in `src/ui_helpers.c`

## Known quirks

- `system_monitor.c` declares `smart_light_nav_cb` and `system_monitor_set_smart_light_nav_cb()` but **no declaration in `system_monitor.h`** — ui.c calls it via implicit declaration. If you add prototypes to the header, include this one.
- `.gitmodules` references `mqtt-c` submodule that is **not used** — actual MQTT uses `mosquitto-2.0.18/` (not a submodule, included as source tarball)
- App requires **root** to access `/dev/fb0` and `/sys/class/backlight`
- `make check` (cppcheck) and `make format` (clang-format) are defined in Makefile but neither tool is installed or enforced
- File count limit: when adding new `.c` files, add them to both `APP_SRC` and (if an asset) `ASSET_SRC` in `Makefile`
