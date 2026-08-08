# LVGL System Monitor - Makefile
# Canopi Smart Room Controller for Luckfox Pico Panel 86

CC = arm-linux-gnueabihf-gcc

# Application Version
APP_VERSION ?= 1.1.0
OTA_DIR = ota
OTA_SERVER_IP = 192.168.1.171

CFLAGS = -O3 -g0 -static -I. -Ilvgl -DLV_CONF_INCLUDE_SIMPLE -DAPP_VERSION=\"$(APP_VERSION)\"
LDFLAGS = -lm -lpthread

# LVGL source files
LVGL_SRC = $(shell find lvgl/src -name "*.c")

# Application source files
APP_SRC = main.c \
          src/ui.c \
          src/ui_helpers.c \
          src/system_monitor.c \
          src/config.c \
          src/settings_auth.c \
          src/settings.c \
          src/ota.c \
          src/bms.c \
          src/bms_yaml.c \
          src/http_client.c

# Asset files
ROBOTO_FONTS = $(wildcard assets/lv_font_roboto_*.c)
ASSET_SRC = assets/logo.c assets/logo_med.c assets/icons_co2_20.c assets/icons_humidity_20.c assets/icons_temperature_20.c assets/icons_cold.c assets/icons_dry.c assets/icons_heat.c assets/icons_eco.c assets/icon_clean.c assets/icons_dnd.c assets/icons_power.c assets/icons_light.c assets/icons_curtain.c $(ROBOTO_FONTS)

all:
	@mkdir -p $(OTA_DIR)
	$(CC) $(CFLAGS) $(APP_SRC) $(LVGL_SRC) $(ASSET_SRC) $(LDFLAGS) -o $(OTA_DIR)/app_v$(APP_VERSION)
	@cp $(OTA_DIR)/app_v$(APP_VERSION) app
	@echo "Generating $(OTA_DIR)/check.json and .sha256 sidecar..."
	@echo "{" > $(OTA_DIR)/check.json
	@echo "  \"version\": \"$(APP_VERSION)\"," >> $(OTA_DIR)/check.json
	@echo "  \"url\": \"http://$(OTA_SERVER_IP)/ota/app_v$(APP_VERSION)\"," >> $(OTA_DIR)/check.json
	@echo "  \"sha256\": \"$$(sha256sum $(OTA_DIR)/app_v$(APP_VERSION) | cut -d' ' -f1)\"" >> $(OTA_DIR)/check.json
	@echo "}" >> $(OTA_DIR)/check.json
	@sha256sum $(OTA_DIR)/app_v$(APP_VERSION) | cut -d' ' -f1 > $(OTA_DIR)/app_v$(APP_VERSION).sha256
	@echo "Build complete: $(OTA_DIR)/app_v$(APP_VERSION), app, check.json, app_v$(APP_VERSION).sha256"

clean:
	rm -f app
	rm -rf $(OTA_DIR)

# Debug build with symbols
debug:
	$(CC) -O0 -g -static -I. -Ilvgl -DLV_CONF_INCLUDE_SIMPLE $(APP_SRC) $(LVGL_SRC) $(ASSET_SRC) $(LDFLAGS) -o app_debug

# Run static analysis (requires cppcheck)
check:
	cppcheck --enable=all --inconclusive --std=c99 main.c src/

# Format code (requires clang-format)
format:
	clang-format -i main.c src/*.c src/*.h

.PHONY: all clean debug check format
