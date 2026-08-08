#ifndef BMS_H
#define BMS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==================
 * SCREEN IDs
 *==================*/
typedef enum {
    SCREEN_OVERVIEW = 0,
    SCREEN_AC,
    SCREEN_SIGN,
    SCREEN_POWER,
    SCREEN_LIGHT,
    SCREEN_SWITCH
} screen_id_t;

/*==================
 * DEVICE TYPES
 *==================*/
typedef enum {
    DEV_TYPE_AC = 0,
    DEV_TYPE_SIGN,
    DEV_TYPE_POWER_METER,
    DEV_TYPE_LIGHT_SENSOR,
    DEV_TYPE_SWITCH
} dev_type_t;

/*==================
 * MQTT TOPIC DEFINES
 *==================*/
#define BMS_TOPIC_OUTDOOR_TEMP  "bms/status/outdoor_temp"
#define BMS_TOPIC_HUMIDITY      "bms/status/humidity"
#define BMS_TOPIC_ENERGY_TODAY  "bms/status/energy_today"

#define BMS_TOPIC_AC_TEMP       "bms/ac/%d/temperature"
#define BMS_TOPIC_AC_ROOM_TEMP  "bms/ac/%d/room_temp"
#define BMS_TOPIC_AC_POWER      "bms/ac/%d/power"
#define BMS_TOPIC_AC_MODE       "bms/ac/%d/mode"
#define BMS_TOPIC_AC_FAN        "bms/ac/%d/fan"

#define BMS_TOPIC_AC_TEMP_SET   "bms/ac/%d/temperature/set"
#define BMS_TOPIC_AC_POWER_SET  "bms/ac/%d/power/set"
#define BMS_TOPIC_AC_MODE_SET   "bms/ac/%d/mode/set"
#define BMS_TOPIC_AC_FAN_SET    "bms/ac/%d/fan/set"

#define BMS_TOPIC_AC_ONLINE     "bms/ac/%d/online"

#define BMS_TOPIC_SIGN_POWER    "bms/sign/%d/power"
#define BMS_TOPIC_SIGN_LUX       "bms/sign/%d/lux"
#define BMS_TOPIC_SIGN_MODE      "bms/sign/%d/mode"
#define BMS_TOPIC_SIGN_LUX_ON    "bms/sign/%d/lux_on"
#define BMS_TOPIC_SIGN_LUX_OFF   "bms/sign/%d/lux_off"

#define BMS_TOPIC_SIGN_POWER_SET "bms/sign/%d/power/set"
#define BMS_TOPIC_SIGN_MODE_SET  "bms/sign/%d/mode/set"
#define BMS_TOPIC_SIGN_LUX_ON_SET  "bms/sign/%d/lux_on/set"
#define BMS_TOPIC_SIGN_LUX_OFF_SET "bms/sign/%d/lux_off/set"

#define BMS_TOPIC_SIGN_ONLINE   "bms/sign/%d/online"

#define BMS_TOPIC_SWITCH_POWER      "bms/switch/%d/power"
#define BMS_TOPIC_SWITCH_POWER_SET  "bms/switch/%d/power/set"
#define BMS_TOPIC_SWITCH_ONLINE     "bms/switch/%d/online"

#define BMS_TOPIC_POWER_ONLINE      "bms/power/%d/online"
#define BMS_TOPIC_LIGHT_ONLINE      "bms/light/%d/online"

#define BMS_TOPIC_SCENE_MASTER "bms/scene/master"

#define BMS_TOPIC_AC_SUMMARY_ON    "bms/status/ac_on_count"
#define BMS_TOPIC_AC_SUMMARY_AVG   "bms/status/ac_avg_temp"
#define BMS_TOPIC_AC_SUMMARY_MODE  "bms/status/ac_mode"
#define BMS_TOPIC_SIGN_SUMMARY_ON   "bms/status/sign_on_count"
#define BMS_TOPIC_SIGN_SUMMARY_LUX  "bms/status/sign_total_lux"
#define BMS_TOPIC_SW_SUMMARY_ON     "bms/status/sw_on_count"

/*==================
 * DEVICE STRUCT
 *==================*/
#define MAX_DEVICES_DEFAULT 12
#define DEV_NAME_LEN 24
#define MAX_DISPLAY_ATTRS 16

typedef enum {
    ATTR_TYPE_BOOL = 0,
    ATTR_TYPE_NUMBER
} bms_attr_type_t;

typedef struct {
    int      device_id;
    char     api_device_id[32];
    char     name[DEV_NAME_LEN];
    char     group[16];
    dev_type_t type;
    bool     enabled;
    int16_t  value;
    int16_t  room_temp;
    int16_t  mode;
    int16_t  fan_speed;
    int16_t  lux_value;
    int16_t  lux_on_thresh;
    int16_t  lux_off_thresh;
    int      ac_index;
    int      sign_index;
    int      power_index;
    int      light_sensor_index;
    int      switch_index;
    bool     online;
    // Dynamic display attributes from YAML
    uint8_t         display_attr_count;
    char            display_attr_ids[MAX_DISPLAY_ATTRS][32];
    char            display_attr_labels[MAX_DISPLAY_ATTRS][32];
    char            display_attr_units[MAX_DISPLAY_ATTRS][16];
    bms_attr_type_t display_attr_types[MAX_DISPLAY_ATTRS];
    bool            display_attr_overview[MAX_DISPLAY_ATTRS];
    bool            display_attr_control[MAX_DISPLAY_ATTRS];   // controllable via UI (toggle, +/-)
    int16_t         display_attr_values[MAX_DISPLAY_ATTRS];
} bms_device_t;

// Lookup display attr index by attr_id, -1 if not found
int bms_device_get_display_attr(const bms_device_t *d, const char *attr_id);

// Set value for a display attr by attr_id
void bms_device_set_display_value(bms_device_t *d, const char *attr_id, int16_t val);

extern uint8_t       g_max_devices;
extern bms_device_t *g_devices;
extern uint8_t       g_device_count;

/*==================
 * BMS-specific color accents
 *==================*/
#define CLR_AC_ACCENT         0x00CFCF
#define CLR_AC_ACCENT_DIM     0x007A7A
#define CLR_BORDER_GREEN      0x00C853
#define CLR_POWER_ACCENT      0xFF9800
#define CLR_POWER_ACCENT_DIM  0x2A1E00
#define CLR_LIGHT_ACCENT      0xFFEB3B
#define CLR_LIGHT_ACCENT_DIM  0x2A2A00
#define CLR_SWITCH_ACCENT     0x2196F3
#define CLR_SWITCH_ACCENT_DIM 0x0D1A2A
#define CLR_BG_OVERLAY         0x000000

/*==================
 * GLOBAL SCREENS
 *==================*/
extern lv_obj_t *g_bms_overview;
extern lv_obj_t *g_bms_ac;
extern lv_obj_t *g_bms_sign;
extern lv_obj_t *g_bms_power;
extern lv_obj_t *g_bms_light;
extern lv_obj_t *g_bms_switch;

// Incremental update: stored card pointers (indexed by device array index)
extern lv_obj_t *g_card_ptrs[12];
extern lv_obj_t *g_sw_ptrs[12];    // toggle switch pointers
extern bool g_bms_mqtt_updating;   // true when switch state is changed by MQTT (not user)

/*==================
 * FUNCTIONS
 *==================*/

void bms_init(void);
void bms_load_screen(screen_id_t screen);
void bms_navigate_to(screen_id_t target);
screen_id_t bms_get_current_screen(void);

void bms_device_add(bms_device_t dev);
void bms_device_update(int idx, bms_device_t dev);
void bms_device_delete(int idx);
void bms_device_toggle(int idx);

void bms_handle_state(const char *topic, const char *payload);
void bms_handle_api_event(const char *device_id, const char *attr,
                          const char *value, const char *ack_command_id);
void bms_rebuild_all_screens(void);

// Pending-command / optimistic UI helpers
bool bms_device_locked(const char *device_id);
const char *bms_ack_command(const char *command_id);

lv_color_t bms_get_thermo_color(int16_t temp);
lv_color_t bms_get_lux_color(int16_t lux);

#ifdef __cplusplus
}
#endif

#endif