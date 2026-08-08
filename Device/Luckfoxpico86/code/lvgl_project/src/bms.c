#include "src/bms.h"
#include "src/ui_common.h"
#include "src/ui_helpers.h"
#include "src/system_monitor.h"
#include "src/http_client.h"
#include "src/bms_yaml.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

/*==================
 * ACTION API + PENDING COMMAND QUEUE
 * (optimistic UI, lock, timeout 5s, rollback on ack / timeout)
 *==================*/
#define CMD_TIMEOUT_MS 5000
#define MAX_PENDING 8
#define CMD_ID_LEN 40

typedef enum {
    CMD_TOGGLE = 0,
    CMD_SET_ATTR
} bms_cmd_kind_t;

typedef struct {
    char       command_id[CMD_ID_LEN];
    char       device_id[32];
    bms_cmd_kind_t kind;
    char       attr_id[32];       // target display attr (for SET_ATTR)
    int16_t    prev_bool_val;     // previous value of the bool control attr (TOGGLE)
    int16_t    prev_num_val;      // previous number value (SET_ATTR)
    bool       has_prev;
    uint32_t   deadline_ms;
    bool       active;
} bms_pending_cmd_t;

static bms_pending_cmd_t g_pending[MAX_PENDING];

static uint32_t bms_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Generate a unique command_id: "cmd_fe_<monotonic-ms>_<counter>"
static void bms_gen_command_id(char *out, size_t sz)
{
    static unsigned int seq = 0;
    snprintf(out, sz, "cmd_fe_%lu_%u", (unsigned long)bms_now_ms(), seq++);
}

static bms_pending_cmd_t *bms_pending_find_by_device(const char *device_id)
{
    for (int i = 0; i < MAX_PENDING; i++) {
        if (g_pending[i].active && strcmp(g_pending[i].device_id, device_id) == 0)
            return &g_pending[i];
    }
    return NULL;
}

static bms_pending_cmd_t *bms_pending_find_by_id(const char *command_id)
{
    if (!command_id) return NULL;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (g_pending[i].active && strcmp(g_pending[i].command_id, command_id) == 0)
            return &g_pending[i];
    }
    return NULL;
}

static int bms_pending_alloc(void)
{
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!g_pending[i].active) return i;
    }
    return -1;
}

// True while a command is pending for the device (control is locked).
bool bms_device_locked(const char *device_id)
{
    return bms_pending_find_by_device(device_id) != NULL;
}

// Acknowledge a command by its command_id (SSE ack). Returns the device the
// command targeted (for UI unlock + refresh), or NULL.
const char *bms_ack_command(const char *command_id)
{
    bms_pending_cmd_t *p = bms_pending_find_by_id(command_id);
    if (!p) return NULL;
    p->active = false;
    return p->device_id;
}

// Public helper used by http_client's callback path.
void bms_handle_api_event(const char *device_id, const char *attr,
                          const char *value, const char *ack_command_id);
void bms_rebuild_active_screen(void);
static void bms_refresh_card_style(int device_idx);

static void bms_issue_action(bms_device_t *d, const char *action,
                             const char *params_json, bms_pending_cmd_t *slot)
{
    if (slot) {
        bms_gen_command_id(slot->command_id, sizeof(slot->command_id));
        strncpy(slot->device_id, d->api_device_id, sizeof(slot->device_id) - 1);
        slot->deadline_ms = bms_now_ms() + CMD_TIMEOUT_MS;
        slot->active = true;
        http_client_action(d->api_device_id, action, params_json, slot->command_id);
    } else {
        char cmd[CMD_ID_LEN];
        bms_gen_command_id(cmd, sizeof(cmd));
        http_client_action(d->api_device_id, action, params_json, cmd);
    }
}

// Publish helper kept for scene + legacy callers: routes to /api/v1 actions.
static void bms_publish(const char *topic, const char *payload) {
    // Scene topic -> scene action
    if (strcmp(topic, BMS_TOPIC_SCENE_MASTER) == 0) {
        const char *action = (strcmp(payload, "ON") == 0) ? "TURN_ON" : "TURN_OFF";
        char cmd[CMD_ID_LEN];
        bms_gen_command_id(cmd, sizeof(cmd));
        http_client_scene_action("master", action, cmd);
        return;
    }
    // Device topic bms/{type}/{idx}/{attr}/set -> map back to api device + action.
    // Find the device by its type index embedded in the topic.
    char type_slug[16] = {0}, attr_id[32] = {0};
    int idx = 0;
    if (sscanf(topic, "bms/%15[^/]/%d/%31[^/]/set", type_slug, &idx, attr_id) != 3) {
        printf("[BMS] cannot parse control topic %s\n", topic);
        return;
    }
    for (int i = 0; i < g_device_count; i++) {
        bms_device_t *d = &g_devices[i];
        bool match = false;
        if (strcmp(type_slug, "ac") == 0 && d->type == DEV_TYPE_AC && d->ac_index == idx) match = true;
        else if (strcmp(type_slug, "sign") == 0 && d->type == DEV_TYPE_SIGN && d->sign_index == idx) match = true;
        else if (strcmp(type_slug, "switch") == 0 && d->type == DEV_TYPE_SWITCH && d->switch_index == idx) match = true;
        if (!match) continue;

        int slot = bms_pending_alloc();
        bms_pending_cmd_t *p = (slot >= 0) ? &g_pending[slot] : NULL;

        if (strcmp(payload, "ON") == 0 || strcmp(payload, "OFF") == 0) {
            // Bool control -> TURN_ON / TURN_OFF (deterministic; UI already set)
            if (p) {
                p->kind = CMD_TOGGLE;
                strncpy(p->attr_id, attr_id, sizeof(p->attr_id) - 1);
                int ai = bms_device_get_display_attr(d, attr_id);
                p->has_prev = true;
                p->prev_bool_val = d->enabled ? 1 : 0;
                bms_issue_action(d, (strcmp(payload, "ON") == 0) ? "TURN_ON" : "TURN_OFF", NULL, p);
            } else {
                char cmd[CMD_ID_LEN];
                bms_gen_command_id(cmd, sizeof(cmd));
                http_client_action(d->api_device_id, (strcmp(payload, "ON") == 0) ? "TURN_ON" : "TURN_OFF", NULL, cmd);
            }
        } else {
            // Number attr -> SET_ATTRIBUTE
            char params[96];
            snprintf(params, sizeof(params), "{\"attr\":\"%s\",\"value\":%d}", attr_id, atoi(payload));
            if (p) {
                p->kind = CMD_SET_ATTR;
                strncpy(p->attr_id, attr_id, sizeof(p->attr_id) - 1);
                int ai = bms_device_get_display_attr(d, attr_id);
                p->has_prev = ai >= 0;
                p->prev_num_val = (ai >= 0) ? d->display_attr_values[ai] : 0;
                bms_issue_action(d, "SET_ATTRIBUTE", params, p);
            } else {
                char cmd[CMD_ID_LEN];
                bms_gen_command_id(cmd, sizeof(cmd));
                http_client_action(d->api_device_id, "SET_ATTRIBUTE", params, cmd);
            }
        }
        return;
    }
    printf("[BMS] control topic %s matched no device\n", topic);
}

#define BMS_BG_SCREEN    0x1A1A1A
#define BMS_BG_CARD      0x2A2A2A
#define BMS_BG_CARD_DARK 0x222222
#define BMS_BG_BAR       0x111111
#define BMS_TEXT_DIM     0x888888
#define BMS_TEXT_SUB     0xAAAAAA

/*==================
 * GLOBALS
 *==================*/

uint8_t       g_max_devices = MAX_DEVICES_DEFAULT;
bms_device_t *g_devices = NULL;
uint8_t       g_device_count = 0;

lv_obj_t *g_bms_overview = NULL;
lv_obj_t *g_bms_ac = NULL;
lv_obj_t *g_bms_sign = NULL;
lv_obj_t *g_bms_power = NULL;
lv_obj_t *g_bms_light = NULL;
lv_obj_t *g_bms_switch = NULL;

lv_obj_t *g_card_ptrs[12] = {NULL};  // indexed by device array index
lv_obj_t *g_sw_ptrs[12]   = {NULL};  // toggle switch pointers
bool       g_bms_mqtt_updating = false;

static screen_id_t g_current_screen = SCREEN_OVERVIEW;
static lv_timer_t  *g_clock_timer = NULL;
static lv_timer_t  *g_bms_pending_timer = NULL;

// Transient banner shown when a command cannot be confirmed (device offline).
static lv_timer_t *g_notify_timer  = NULL;
static lv_obj_t   *g_notify_banner = NULL;

static lv_obj_t *g_lbl_ac_on   = NULL;
static lv_obj_t *g_lbl_ac_avg  = NULL;
static lv_obj_t *g_lbl_ac_mode = NULL;
static lv_obj_t *g_lbl_sign_on   = NULL;
static lv_obj_t *g_lbl_sign_lux  = NULL;
static lv_obj_t *g_lbl_sw_on     = NULL;
static lv_obj_t *g_lbl_outdoor = NULL;
static lv_obj_t *g_lbl_energy  = NULL;

// Phase 3: Store label pointers for incremental updates
#define MAX_LABELS 128
static lv_obj_t *g_attr_labels[MAX_DEVICES_DEFAULT][MAX_DISPLAY_ATTRS] = {{NULL}};
static lv_obj_t *g_value_labels[MAX_DEVICES_DEFAULT][MAX_DISPLAY_ATTRS] = {{NULL}};

// Navbar per screen
static lv_obj_t *nav_tabs_ov[6], *nav_tabs_ac[6], *nav_tabs_sign[6], *nav_tabs_pwr[6], *nav_tabs_lt[6], *nav_tabs_sw[6];
static lv_obj_t *nav_dots_ov[6], *nav_dots_ac[6], *nav_dots_sign[6], *nav_dots_pwr[6], *nav_dots_lt[6], *nav_dots_sw[6];

/*==================
 * DEVICE MANAGEMENT
 *==================*/
void bms_device_add(bms_device_t dev) {
    if (g_device_count < g_max_devices) {
        g_devices[g_device_count] = dev;
        g_device_count++;
    }
}
void bms_device_update(int idx, bms_device_t dev) {
    if (idx >= 0 && idx < g_device_count) g_devices[idx] = dev;
}
void bms_device_delete(int idx) {
    if (idx >= 0 && idx < g_device_count) {
        for (int i = idx; i < g_device_count - 1; i++)
            g_devices[i] = g_devices[i + 1];
        g_device_count--;
    }
}
void bms_device_toggle(int idx) {
    if (idx >= 0 && idx < g_device_count)
        g_devices[idx].enabled = !g_devices[idx].enabled;
}

int bms_device_get_display_attr(const bms_device_t *d, const char *attr_id) {
    for (int i = 0; i < d->display_attr_count; i++) {
        if (strcmp(d->display_attr_ids[i], attr_id) == 0) return i;
    }
    return -1;
}

void bms_device_set_display_value(bms_device_t *d, const char *attr_id, int16_t val) {
    int idx = bms_device_get_display_attr(d, attr_id);
    if (idx >= 0) d->display_attr_values[idx] = val;
    d->online = true;  // received data = device is online
}

lv_color_t bms_get_thermo_color(int16_t temp) {
    if (temp < 24) return lv_color_hex(CLR_AC_ACCENT);
    if (temp < 28) return lv_color_hex(0xFFC107);
    return lv_color_hex(0xFF5252);
}
lv_color_t bms_get_lux_color(int16_t lux) {
    if (lux < 300) return lv_color_hex(CLR_BORDER_GREEN);
    if (lux < 700) return lv_color_hex(0xFFC107);
    return COLOR_WHITE;
}

/*==================
 * UI HELPERS
 *==================*/
static lv_obj_t* bm_lbl(lv_obj_t *parent, const char *txt, int size, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, ui_get_text(txt));
    lv_obj_set_style_text_font(l, ui_get_font(size), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

static lv_obj_t* bm_btn(lv_obj_t *parent, int w, int h, uint32_t bg, int radius) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    return b;
}

static void bm_btn_lbl(lv_obj_t *btn, const char *txt, int size, uint32_t color) {
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, ui_get_text(txt));
    lv_obj_set_style_text_font(l, ui_get_font(size), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_center(l);
}

static void set_flex2(lv_obj_t *o, lv_flex_flow_t flow, int gap) {
    lv_obj_set_flex_flow(o, flow);
    if (gap >= 0) {
        lv_obj_set_style_pad_row(o, gap, 0);
        lv_obj_set_style_pad_column(o, gap, 0);
    }
}

/*==================
 * CLOCK
 *==================*/
static lv_obj_t *g_clock_lbl = NULL;

static void clock_cb(lv_timer_t *t) {
    (void)t;
    if (!g_clock_lbl) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min);
    lv_label_set_text(g_clock_lbl, b);
}

static lv_obj_t *g_main_screen = NULL;

static void clock_click_cb(lv_event_t *e) {
    if (g_main_screen) {
        system_monitor_show();
        lv_scr_load(g_main_screen);
    }
}

/*==================
 * HEADER BAR - 60px
 *==================*/
static lv_obj_t* header_create(lv_obj_t *parent) {
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 60);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(BMS_BG_BAR), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 10, 0);
    set_flex2(hdr, LV_FLEX_FLOW_ROW, -1);

    lv_obj_t *left = lv_obj_create(hdr);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    set_flex2(left, LV_FLEX_FLOW_ROW, 8);

    lv_obj_t *brand = lv_label_create(left);
    lv_label_set_text(brand, "Blu Coffee");
    lv_obj_set_style_text_font(brand, ui_get_font(16), 0);
    lv_obj_set_style_text_color(brand, COLOR_WHITE, 0);

    lv_obj_t *sp = lv_obj_create(hdr);
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp, 0, 0);

    lv_obj_t *right = lv_obj_create(hdr);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    set_flex2(right, LV_FLEX_FLOW_ROW, 8);

    lv_obj_t *clk_wrapper = lv_obj_create(right);
    lv_obj_set_size(clk_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(clk_wrapper, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clk_wrapper, 0, 0);
    lv_obj_set_style_pad_all(clk_wrapper, 0, 0);
    lv_obj_add_flag(clk_wrapper, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clk_wrapper, clock_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clk = lv_label_create(clk_wrapper);
    lv_label_set_text(clk, "00:00");
    lv_obj_set_style_text_font(clk, ui_get_font(18), 0);
    lv_obj_set_style_text_color(clk, COLOR_WHITE, 0);
    g_clock_lbl = clk;

    if (!g_clock_timer)
        g_clock_timer = lv_timer_create(clock_cb, 60000, NULL);
    clock_cb(NULL);

    return hdr;
}

/*==================
 * STATUS BAR
 *==================*/
static lv_obj_t* status_create(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 32);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_set_style_bg_color(bar, lv_color_hex(BMS_BG_BAR), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 12, 0);
    set_flex2(bar, LV_FLEX_FLOW_ROW, -1);

    bm_lbl(bar, "EnergyX", 14, 0x00C853);

    lv_obj_t *sp = lv_obj_create(bar);
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp, 0, 0);

    g_lbl_outdoor = bm_lbl(bar, "Outdoor: --C - --%", 14, 0x888888);

    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_set_flex_grow(sp2, 1);
    lv_obj_set_style_bg_opa(sp2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp2, 0, 0);

    g_lbl_energy = bm_lbl(bar, "-- kWh", 14, 0xFFA726);

    return bar;
}

/*==================
 * NAVBAR
 *==================*/
static void nav_cb(lv_event_t *e) {
    bms_navigate_to((screen_id_t)(intptr_t)lv_event_get_user_data(e));
}

static void navbar_create(lv_obj_t *parent, screen_id_t active, lv_obj_t **tabs, lv_obj_t **dots) {
    lv_obj_t *nav = lv_obj_create(parent);
    lv_obj_set_size(nav, LV_PCT(100), 50);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nav, lv_color_hex(BMS_BG_BAR), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    set_flex2(nav, LV_FLEX_FLOW_ROW, 0);

    static const char *labs[] = {"Overview", "AC", "Sign", "Power", "Light", "Switch"};
    static uint32_t accents[] = {CLR_AC_ACCENT, CLR_AC_ACCENT, CLR_BORDER_GREEN, CLR_POWER_ACCENT, CLR_LIGHT_ACCENT, CLR_SWITCH_ACCENT};

    for (int i = 0; i < 6; i++) {
        lv_obj_t *tab = lv_obj_create(nav);
        lv_obj_set_flex_grow(tab, 1);
        lv_obj_set_height(tab, LV_PCT(100));
        lv_obj_set_style_bg_opa(tab, (i == active) ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(tab, lv_color_hex(accents[i]), 0);
        lv_obj_set_style_border_width(tab, 0, 0);
        lv_obj_set_style_radius(tab, 0, 0);
        lv_obj_set_style_pad_all(tab, 0, 0);
        set_flex2(tab, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_style_flex_main_place(tab, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_cross_place(tab, LV_FLEX_ALIGN_CENTER, 0);

        lv_obj_t *dot = lv_obj_create(tab);
        lv_obj_set_size(dot, 4, 4);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(accents[i]), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        if (i != active) lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        dots[i] = dot;

        lv_obj_t *lb = lv_label_create(tab);
        lv_obj_set_width(lb, LV_PCT(100));
        lv_label_set_text(lb, ui_get_text(labs[i]));
        lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lb, ui_get_font(8), 0);
        lv_obj_set_style_text_color(lb, (i == active) ? COLOR_WHITE : lv_color_hex(0x555555), 0);

        lv_obj_add_event_cb(tab, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        tabs[i] = tab;
    }
}

/*==================
 * SECTION LABEL
 *==================*/
static void sec_label(lv_obj_t *p, const char *t) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, ui_get_text(t));
    lv_obj_set_style_text_font(l, ui_get_font(14), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(BMS_TEXT_SUB), 0);
    lv_obj_set_style_margin_top(l, 14, 0);
}

/*==================
 * CARD HEADER ROW
 *==================*/
static void card_hdr(lv_obj_t *card, const char *icon, const char *name, bool on, uint32_t accent, int16_t room_temp, bool online, bool show_on_off) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), 42);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    set_flex2(row, LV_FLEX_FLOW_ROW, -1);

    lv_obj_t *sig = lv_obj_create(row);
    lv_obj_set_size(sig, 8, 8);
    lv_obj_set_style_radius(sig, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sig, lv_color_hex(online ? 0x00C853 : 0xFF5252), 0);
    lv_obj_set_style_border_width(sig, 0, 0);
    lv_obj_set_style_pad_all(sig, 0, 0);

    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, ui_get_font(20), 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(on ? accent : 0x555555), 0);

    lv_obj_t *nm = lv_label_create(row);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_font(nm, ui_get_font(18), 0);
    lv_obj_set_style_text_color(nm, on ? COLOR_WHITE : lv_color_hex(0x888888), 0);

    if (show_on_off) {
        lv_obj_t *badge = lv_obj_create(row);
        lv_obj_set_size(badge, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_bg_color(badge, lv_color_hex(on ? accent : 0x3A3A3A), 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_radius(badge, 4, 0);
        lv_obj_set_style_pad_hor(badge, 12, 0);
        lv_obj_set_style_pad_ver(badge, 4, 0);
        lv_obj_t *bl = lv_label_create(badge);
        lv_label_set_text(bl, ui_get_text(on ? "ON" : "OFF"));
        lv_obj_set_style_text_font(bl, ui_get_font(14), 0);
        lv_obj_set_style_text_color(bl, on ? COLOR_WHITE : lv_color_hex(0x888888), 0);
        lv_obj_center(bl);
    }

    if (room_temp > 0) {
        lv_obj_t *sp2 = lv_obj_create(row);
        lv_obj_set_flex_grow(sp2, 1);
        lv_obj_set_style_bg_opa(sp2, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sp2, 0, 0);

        char rtbuf[24];
        snprintf(rtbuf, sizeof(rtbuf), ui_get_text("Room: %d*C"), room_temp);
        lv_obj_t *rtl = lv_label_create(row);
        lv_label_set_text(rtl, rtbuf);
        lv_obj_set_style_text_font(rtl, ui_get_font(14), 0);
        lv_obj_set_style_text_color(rtl, lv_color_hex(CLR_AC_ACCENT), 0);
    }
}

/*==================
 * OVERVIEW CARDS
 *==================*/
static void ov_toggle_cb(lv_event_t *e) {
    if (g_bms_mqtt_updating) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (bms_device_locked(g_devices[idx].api_device_id)) return; // pending cmd
    bms_device_toggle(idx);

    bms_device_t *d = &g_devices[idx];
    // Find bool control attr_id from YAML
    const char *attr_id = NULL;
    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_control[a] && d->display_attr_types[a] == ATTR_TYPE_BOOL) {
            attr_id = d->display_attr_ids[a]; break;
        }
    }
    if (!attr_id) return;

    char topic[64];
    const char *type_prefix = (d->type == DEV_TYPE_AC) ? "ac" :
                              (d->type == DEV_TYPE_SIGN) ? "sign" : "switch";
    int dev_idx = (d->type == DEV_TYPE_AC) ? d->ac_index :
                  (d->type == DEV_TYPE_SIGN) ? d->sign_index : d->switch_index;
    snprintf(topic, sizeof(topic), "bms/%s/%d/%s/set", type_prefix, dev_idx, attr_id);
    bms_publish(topic, d->enabled ? "ON" : "OFF");
    bms_rebuild_active_screen();
}

static void ov_temp_cb(lv_event_t *e) {
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int idx = packed & 0xFFFF;
    int delta = (int)(packed >> 16);
    bms_device_t *d = &g_devices[idx];
    if (bms_device_locked(d->api_device_id)) return;
    d->value += delta;
    if (d->value < 16) d->value = 16;
    if (d->value > 32) d->value = 32;
    // Find temperature control attr_id from YAML (first number-type control attr)
    const char *attr_id = NULL;
    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_control[a] && d->display_attr_types[a] == ATTR_TYPE_NUMBER) {
            attr_id = d->display_attr_ids[a]; break;
        }
    }
    char topic[64], payload[8];
    snprintf(topic, sizeof(topic), "bms/ac/%d/%s/set", d->ac_index, attr_id ? attr_id : "0202");
    snprintf(payload, sizeof(payload), "%d", d->value);
    bms_publish(topic, payload);
    bms_rebuild_active_screen();
}

// ── Generic AC control callbacks (pub: bms/ac/<idx>/<attr_id>/set) ──
static void ac_ctrl_bool_cb(lv_event_t *e) {
    if (g_bms_mqtt_updating) return;
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int idx = packed & 0xFF;
    int attr_idx = (packed >> 8) & 0xFF;
    bms_device_t *d = &g_devices[idx];
    if (bms_device_locked(d->api_device_id)) return;
    lv_obj_t *sw = lv_event_get_target(e);
    bool chk = lv_obj_has_state(sw, LV_STATE_CHECKED);
    d->enabled = chk;
    d->display_attr_values[attr_idx] = chk ? 1 : 0;
    char topic[64];
    snprintf(topic, sizeof(topic), "bms/ac/%d/%s/set", d->ac_index, d->display_attr_ids[attr_idx]);
    bms_publish(topic, chk ? "ON" : "OFF");
    bms_rebuild_active_screen();
}

static void ac_ctrl_step_cb(lv_event_t *e) {
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int idx = packed & 0xFF;
    int attr_idx = (packed >> 8) & 0xFF;
    int delta = (int)(packed >> 24);
    bms_device_t *d = &g_devices[idx];
    if (bms_device_locked(d->api_device_id)) return;
    int16_t newval = d->display_attr_values[attr_idx] + delta * 10;
    if (newval < 160) newval = 160;
    if (newval > 320) newval = 320;
    d->display_attr_values[attr_idx] = newval;
    d->value = newval / 10;
    char topic[64], payload[8];
    snprintf(topic, sizeof(topic), "bms/ac/%d/%s/set", d->ac_index, d->display_attr_ids[attr_idx]);
    snprintf(payload, sizeof(payload), "%d", newval / 10);
    bms_publish(topic, payload);
    bms_rebuild_active_screen();
}

static void exp_fan_cb(lv_event_t *e) {
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int idx = packed & 0xFF;
    int fan = (packed >> 8) & 0xFF;
    bms_device_t *d = &g_devices[idx];
    if (bms_device_locked(d->api_device_id)) return;
    d->fan_speed = fan;
    char topic[64], payload[8];
    snprintf(topic, sizeof(topic), "bms/ac/%d/0405/set", d->ac_index);
    snprintf(payload, sizeof(payload), "%d", fan);
    bms_publish(topic, payload);
    bms_rebuild_active_screen();
}


static void bms_set_onoff(int idx, bool want_on);
static void sign_on_cb(lv_event_t *e);
static void sign_off_cb(lv_event_t *e);
static void onoff_btns(lv_obj_t *parent, int idx);

static void ov_ac_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(48));
    lv_obj_set_height(card, 300);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(d->enabled ? CLR_AC_ACCENT : 0x3A3A3A), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 10);

    card_hdr(card, LV_SYMBOL_IMAGE, d->name, d->enabled, CLR_AC_ACCENT, d->room_temp, d->online, true);

    char ov_rtb[24];
    if (d->room_temp > 0)
        snprintf(ov_rtb, sizeof(ov_rtb), "Room: %d*C", d->room_temp);
    else
        snprintf(ov_rtb, sizeof(ov_rtb), "Room: --*C");
    bm_lbl(card, ov_rtb, 14, d->room_temp > 0 ? CLR_AC_ACCENT : 0x888888);

    lv_obj_t *row_toggle = lv_obj_create(card);
    lv_obj_set_flex_grow(row_toggle, 1);
    lv_obj_set_width(row_toggle, LV_PCT(100));
    lv_obj_set_style_bg_opa(row_toggle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_toggle, 0, 0);
    lv_obj_set_style_pad_all(row_toggle, 0, 0);
    set_flex2(row_toggle, LV_FLEX_FLOW_ROW, 10);
    lv_obj_set_style_flex_main_place(row_toggle, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_flex_cross_place(row_toggle, LV_FLEX_ALIGN_CENTER, 0);

    bm_lbl(row_toggle, d->enabled ? "ON" : "OFF", 18, d->enabled ? CLR_AC_ACCENT : 0x888888);

    // Two explicit buttons: BẬT (ON) / TẮT (OFF) — same style as sign
    onoff_btns(row_toggle, idx);

    lv_obj_t *row_temp = lv_obj_create(card);
    lv_obj_set_flex_grow(row_temp, 1);
    lv_obj_set_width(row_temp, LV_PCT(100));
    lv_obj_set_style_bg_opa(row_temp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_temp, 0, 0);
    lv_obj_set_style_pad_all(row_temp, 0, 0);
    set_flex2(row_temp, LV_FLEX_FLOW_ROW, 16);
    lv_obj_set_style_flex_main_place(row_temp, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(row_temp, LV_FLEX_ALIGN_CENTER, 0);

    lv_obj_t *mn = bm_btn(row_temp, 64, 64, 0x333333, LV_RADIUS_CIRCLE);
    bm_btn_lbl(mn, "-", 32, d->enabled ? 0xFFFFFF : 0x555555);
    lv_obj_add_event_cb(mn, ov_temp_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(idx | (-1 << 16)));
    lv_obj_set_style_bg_color(mn, lv_color_hex(0x555555), LV_STATE_PRESSED);
    if (!d->enabled) lv_obj_add_state(mn, LV_STATE_DISABLED);

    char tb[8];
    snprintf(tb, sizeof(tb), "%d", d->value);
    bm_lbl(row_temp, tb, 48, d->enabled ? 0xFFFFFF : 0x888888);
    bm_lbl(row_temp, "*C", 24, d->enabled ? 0xAAAAAA : 0x888888);

    lv_obj_t *pl = bm_btn(row_temp, 64, 64, 0x333333, LV_RADIUS_CIRCLE);
    bm_btn_lbl(pl, "+", 32, d->enabled ? 0xFFFFFF : 0x555555);
    lv_obj_add_event_cb(pl, ov_temp_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(idx | (1 << 16)));
    lv_obj_set_style_bg_color(pl, lv_color_hex(0x555555), LV_STATE_PRESSED);
    if (!d->enabled) lv_obj_add_state(pl, LV_STATE_DISABLED);
}

// Deterministic ON/OFF action for a bool-controlled device. Replaces the toggle
// switch so each button sends an explicit TURN_ON / TURN_OFF (matches engine
// device action API, same path the Open/Close store scene uses).
static const char *bms_bool_control_attr(bms_device_t *d)
{
    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_control[a] && d->display_attr_types[a] == ATTR_TYPE_BOOL)
            return d->display_attr_ids[a];
    }
    return NULL;
}

static void bms_set_onoff(int idx, bool want_on)
{
    FILE *lg = fopen("/tmp/btn.log", "a");
    if (lg) {
        fprintf(lg, "set_onoff idx=%d want=%d count=%d\n", idx, want_on, g_device_count);
        if (idx >= 0 && idx < g_device_count) {
            bms_device_t *d = &g_devices[idx];
            fprintf(lg, "  api=%s locked=%d\n", d->api_device_id,
                    bms_device_locked(d->api_device_id));
        } else {
            fprintf(lg, "  BAD IDX\n");
        }
        fclose(lg);
    }
    if (idx < 0 || idx >= g_device_count) return;
    bms_device_t *d = &g_devices[idx];
    if (bms_device_locked(d->api_device_id)) return;

    d->enabled = want_on;

    int slot = bms_pending_alloc();
    bms_pending_cmd_t *p = (slot >= 0) ? &g_pending[slot] : NULL;
    if (p) {
        p->kind = CMD_TOGGLE;
        const char *attr_id = bms_bool_control_attr(d);
        strncpy(p->attr_id, attr_id ? attr_id : "0110", sizeof(p->attr_id) - 1);
        int ai = bms_device_get_display_attr(d, p->attr_id);
        p->has_prev = (ai >= 0);
        p->prev_bool_val = (ai >= 0) ? d->display_attr_values[ai] : (want_on ? 0 : 1);
        bms_issue_action(d, want_on ? "TURN_ON" : "TURN_OFF", NULL, p);
        fprintf(stderr, "[BMS] set_onoff -> issue %s via pending\n", want_on ? "TURN_ON" : "TURN_OFF");
    } else {
        char cmd[CMD_ID_LEN];
        bms_gen_command_id(cmd, sizeof(cmd));
        http_client_action(d->api_device_id, want_on ? "TURN_ON" : "TURN_OFF", NULL, cmd);
    }
    bms_refresh_card_style(idx);
    if (g_sw_ptrs[idx]) g_sw_ptrs[idx] = NULL; // switch removed; labels used instead
    bms_rebuild_active_screen();
}

static void sign_on_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    bms_set_onoff(i, true);
}
static void sign_off_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    bms_set_onoff(i, false);
}

// Shared ON/OFF button group (same style as the Sign Board card).
static void onoff_btns(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *btng = lv_obj_create(parent);
    lv_obj_set_size(btng, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_bg_opa(btng, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btng, 0, 0);
    lv_obj_set_style_pad_all(btng, 0, 0);
    set_flex2(btng, LV_FLEX_FLOW_ROW, 8);

    lv_obj_t *on = bm_btn(btng, 68, 40, d->enabled ? 0x1B3A2A : 0x2E2E2E, 8);
    if (d->enabled) {
        lv_obj_set_style_border_color(on, lv_color_hex(CLR_BORDER_GREEN), 0);
        lv_obj_set_style_border_width(on, 1, 0);
    }
    char lbl_on[32];
    snprintf(lbl_on, sizeof(lbl_on), "%s %s", LV_SYMBOL_POWER, ui_get_text("ON"));
    bm_btn_lbl(on, lbl_on, 14, d->enabled ? 0x00C853 : 0x888888);
    lv_obj_add_event_cb(on, sign_on_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t *off = bm_btn(btng, 68, 40, d->enabled ? 0x2E2E2E : 0x3A2010, 8);
    if (!d->enabled) {
        lv_obj_set_style_border_color(off, lv_color_hex(0xFFA726), 0);
        lv_obj_set_style_border_width(off, 1, 0);
    }
    char lbl_off[32];
    snprintf(lbl_off, sizeof(lbl_off), "%s %s", LV_SYMBOL_POWER, ui_get_text("OFF"));
    bm_btn_lbl(off, lbl_off, 14, d->enabled ? 0x888888 : 0xFFA726);
    lv_obj_add_event_cb(off, sign_off_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
}

static void ov_sign_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(48));
    lv_obj_set_height(card, 170);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(d->enabled ? CLR_BORDER_GREEN : 0x3A3A3A), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 8);

    card_hdr(card, LV_SYMBOL_IMAGE, d->name, d->enabled, CLR_BORDER_GREEN, 0, d->online, true);

    lv_obj_t *sr = lv_obj_create(card);
    lv_obj_set_flex_grow(sr, 1);
    lv_obj_set_width(sr, LV_PCT(100));
    lv_obj_set_style_bg_opa(sr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sr, 0, 0);
    lv_obj_set_style_pad_all(sr, 0, 0);
    set_flex2(sr, LV_FLEX_FLOW_ROW, 10);
    lv_obj_set_style_flex_main_place(sr, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_flex_cross_place(sr, LV_FLEX_ALIGN_CENTER, 0);

    lv_obj_t *st = bm_lbl(sr, d->enabled ? "ON" : "OFF", 18, d->enabled ? CLR_BORDER_GREEN : 0x888888);

    // Two explicit buttons: BẬT (ON) / TẮT (OFF)
    lv_obj_t *btng = lv_obj_create(sr);
    lv_obj_set_size(btng, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_bg_opa(btng, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btng, 0, 0);
    lv_obj_set_style_pad_all(btng, 0, 0);
    set_flex2(btng, LV_FLEX_FLOW_ROW, 8);

    lv_obj_t *on = bm_btn(btng, 68, 40, d->enabled ? 0x1B3A2A : 0x2E2E2E, 8);
    if (d->enabled) {
        lv_obj_set_style_border_color(on, lv_color_hex(CLR_BORDER_GREEN), 0);
        lv_obj_set_style_border_width(on, 1, 0);
    }
    char lbl_on[32];
    snprintf(lbl_on, sizeof(lbl_on), "%s %s", LV_SYMBOL_POWER, ui_get_text("ON"));
    bm_btn_lbl(on, lbl_on, 14, d->enabled ? 0x00C853 : 0x888888);
    lv_obj_add_event_cb(on, sign_on_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t *off = bm_btn(btng, 68, 40, d->enabled ? 0x2E2E2E : 0x3A2010, 8);
    if (!d->enabled) {
        lv_obj_set_style_border_color(off, lv_color_hex(0xFFA726), 0);
        lv_obj_set_style_border_width(off, 1, 0);
    }
    char lbl_off[32];
    snprintf(lbl_off, sizeof(lbl_off), "%s %s", LV_SYMBOL_POWER, ui_get_text("OFF"));
    bm_btn_lbl(off, lbl_off, 14, d->enabled ? 0x888888 : 0xFFA726);
    lv_obj_add_event_cb(off, sign_off_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    g_card_ptrs[idx] = card;

    // Display attr values (energy, power, etc.)
    int shown = 0;
    for (int a = 0; a < d->display_attr_count && shown < 2; a++) {
        if (d->display_attr_types[a] != ATTR_TYPE_NUMBER) continue;
        if (!d->display_attr_overview[a]) continue;
        shown++;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d.%d %s", d->display_attr_labels[a],
                 d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                 d->display_attr_units[a]);
        bm_lbl(card, buf, 12, 0xAAAAAA);
    }
}

static void ov_power_meter_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(48));
    lv_obj_set_height(card, 170);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(CLR_POWER_ACCENT), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 8);

    card_hdr(card, LV_SYMBOL_CHARGE, d->name, false, CLR_POWER_ACCENT, 0, d->online, false);

    // Display attr values (total_energy, instantaneous_demand, etc.)
    int shown = 0;
    for (int a = 0; a < d->display_attr_count && shown < 3; a++) {
        if (d->display_attr_types[a] != ATTR_TYPE_NUMBER) continue;
        if (!d->display_attr_overview[a]) continue;
        shown++;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d.%d %s", d->display_attr_labels[a],
                 d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                 d->display_attr_units[a]);
        bm_lbl(card, buf, 12, 0xAAAAAA);
    }
}

static void ov_light_sensor_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(48));
    lv_obj_set_height(card, 170);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(CLR_LIGHT_ACCENT), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 8);

    card_hdr(card, LV_SYMBOL_EYE_OPEN, d->name, false, CLR_LIGHT_ACCENT, 0, d->online, false);

    // Display attr values (illuminance)
    int shown = 0;
    for (int a = 0; a < d->display_attr_count && shown < 3; a++) {
        if (d->display_attr_types[a] != ATTR_TYPE_NUMBER) continue;
        if (!d->display_attr_overview[a]) continue;
        shown++;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d.%d %s", d->display_attr_labels[a],
                 d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                 d->display_attr_units[a]);
        bm_lbl(card, buf, 12, 0xAAAAAA);
    }
}

static void ov_switch_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(48));
    lv_obj_set_height(card, 180);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(d->enabled ? CLR_SWITCH_ACCENT : 0x3A3A3A), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 8);

    card_hdr(card, LV_SYMBOL_HOME, d->name, d->enabled, CLR_SWITCH_ACCENT, 0, d->online, true);

    lv_obj_t *sr = lv_obj_create(card);
    lv_obj_set_flex_grow(sr, 1);
    lv_obj_set_width(sr, LV_PCT(100));
    lv_obj_set_style_bg_opa(sr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sr, 0, 0);
    lv_obj_set_style_pad_all(sr, 0, 0);
    set_flex2(sr, LV_FLEX_FLOW_ROW, 10);
    lv_obj_set_style_flex_main_place(sr, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_flex_cross_place(sr, LV_FLEX_ALIGN_CENTER, 0);

    bm_lbl(sr, d->enabled ? "ON" : "OFF", 18, d->enabled ? CLR_SWITCH_ACCENT : 0x888888);

    // Two explicit buttons: BẬT (ON) / TẮT (OFF) — same style as sign
    onoff_btns(sr, idx);

    int shown = 0;
    for (int a = 0; a < d->display_attr_count && shown < 2; a++) {
        if (d->display_attr_types[a] != ATTR_TYPE_NUMBER) continue;
        if (!d->display_attr_overview[a]) continue;
        shown++;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d.%d %s", d->display_attr_labels[a],
                 d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                 d->display_attr_units[a]);
        bm_lbl(card, buf, 12, 0xAAAAAA);
    }
}

/*==================
 * SCENE BUTTONS
 *==================*/
static void scene_all_cb(lv_event_t *e) {
    bool on = (bool)(intptr_t)lv_event_get_user_data(e);
    bms_publish(BMS_TOPIC_SCENE_MASTER, on ? "ON" : "OFF");
}

static void ov_scenes(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 70);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    set_flex2(row, LV_FLEX_FLOW_ROW, 10);

    lv_obj_t *open = bm_btn(row, LV_PCT(48), 62, 0x1B3A2A, 12);
    lv_obj_set_style_border_color(open, lv_color_hex(0x00C853), 0);
    lv_obj_set_style_border_width(open, 1, 0);
    char scene_buf[64];
    snprintf(scene_buf, sizeof(scene_buf), "%s %s", LV_SYMBOL_IMAGE, ui_get_text("Open Store"));
    lv_obj_t *oic = lv_label_create(open);
    lv_label_set_text(oic, scene_buf);
    lv_obj_set_style_text_font(oic, ui_get_font(16), 0);
    lv_obj_set_style_text_color(oic, lv_color_hex(0x00C853), 0);
    lv_obj_center(oic);
    lv_obj_add_event_cb(open, scene_all_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);

    lv_obj_t *close = bm_btn(row, LV_PCT(48), 62, 0x3A2010, 12);
    lv_obj_set_style_border_color(close, lv_color_hex(0xFFA726), 0);
    lv_obj_set_style_border_width(close, 1, 0);
    snprintf(scene_buf, sizeof(scene_buf), "%s %s", LV_SYMBOL_IMAGE, ui_get_text("Close Store"));
    lv_obj_t *cic = lv_label_create(close);
    lv_label_set_text(cic, scene_buf);
    lv_obj_set_style_text_font(cic, ui_get_font(16), 0);
    lv_obj_set_style_text_color(cic, lv_color_hex(0xFFA726), 0);
    lv_obj_center(cic);
    lv_obj_add_event_cb(close, scene_all_cb, LV_EVENT_CLICKED, (void*)(intptr_t)0);
}

/*==================
 * OVERVIEW SCREEN
 *==================*/
static void overview_build(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(BMS_BG_SCREEN), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    header_create(scr);
    navbar_create(scr, SCREEN_OVERVIEW, nav_tabs_ov, nav_dots_ov);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_obj_get_height(scr) - 60 - 50);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 14, 0);
    lv_obj_set_style_pad_ver(cont, 0, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    set_flex2(cont, LV_FLEX_FLOW_COLUMN, -1);

    sec_label(cont, ui_get_text("AIR CONDITIONING"));
    lv_obj_t *gac = lv_obj_create(cont);
    lv_obj_set_size(gac, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gac, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gac, 0, 0);
    lv_obj_set_style_pad_all(gac, 0, 0);
    set_flex2(gac, LV_FLEX_FLOW_ROW, 10);
    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_AC) ov_ac_card(gac, i);

    sec_label(cont, "SIGNAGE");
    lv_obj_t *gsn = lv_obj_create(cont);
    lv_obj_set_size(gsn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gsn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gsn, 0, 0);
    lv_obj_set_style_pad_all(gsn, 0, 0);
    set_flex2(gsn, LV_FLEX_FLOW_ROW, 10);
    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_SIGN) ov_sign_card(gsn, i);

    sec_label(cont, "POWER METER");
    lv_obj_t *gpw = lv_obj_create(cont);
    lv_obj_set_size(gpw, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gpw, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gpw, 0, 0);
    lv_obj_set_style_pad_all(gpw, 0, 0);
    set_flex2(gpw, LV_FLEX_FLOW_ROW, 10);
    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_POWER_METER) ov_power_meter_card(gpw, i);

    sec_label(cont, "LIGHT SENSOR");
    lv_obj_t *glt = lv_obj_create(cont);
    lv_obj_set_size(glt, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(glt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(glt, 0, 0);
    lv_obj_set_style_pad_all(glt, 0, 0);
    set_flex2(glt, LV_FLEX_FLOW_ROW, 10);
    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_LIGHT_SENSOR) ov_light_sensor_card(glt, i);

    sec_label(cont, "SWITCHES");
    lv_obj_t *gsw = lv_obj_create(cont);
    lv_obj_set_size(gsw, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gsw, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gsw, 0, 0);
    lv_obj_set_style_pad_all(gsw, 0, 0);
    set_flex2(gsw, LV_FLEX_FLOW_ROW, 10);
    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_SWITCH) ov_switch_card(gsw, i);

    sec_label(cont, "QUICK SCENES");
    ov_scenes(cont);
}

/*==================
 * AC SCREEN
 *==================*/
static lv_obj_t* ac_expanded_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_scroll_dir(card, LV_DIR_NONE);
    lv_obj_set_style_bg_color(card, d->enabled ? lv_color_hex(0x0D2929) : lv_color_hex(BMS_BG_CARD_DARK), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(d->enabled ? CLR_AC_ACCENT : 0x3A3A3A), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 4);

    card_hdr(card, LV_SYMBOL_IMAGE, d->name, d->enabled, CLR_AC_ACCENT, d->room_temp, d->online, true);

    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_control[a]) {
            if (d->display_attr_types[a] == ATTR_TYPE_BOOL) {
                /* ── Bool → ON/OFF buttons (same style as sign) ── */
                lv_obj_t *row = lv_obj_create(card);
                lv_obj_set_size(row, LV_PCT(100), 42);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                set_flex2(row, LV_FLEX_FLOW_ROW, -1);
                lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
                lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, 0);
                bm_lbl(row, d->display_attr_labels[a], 14, d->enabled ? CLR_AC_ACCENT : 0x888888);
                onoff_btns(row, idx);
            } else {
                /* ── Number → Stepper (or Fan named buttons) ── */
                if (strcmp(d->display_attr_ids[a], "0405") == 0) {
                    lv_obj_t *fr = lv_obj_create(card);
                    lv_obj_set_size(fr, LV_PCT(100), 40);
                    lv_obj_set_style_bg_opa(fr, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(fr, 0, 0);
                    lv_obj_set_style_pad_all(fr, 0, 0);
                    set_flex2(fr, LV_FLEX_FLOW_ROW, 10);
                    lv_obj_t *fl1 = bm_lbl(fr, "Fan speed:", 14, 0xAAAAAA);
                    lv_obj_set_width(fl1, 80);
                    static const char *speeds[] = {"Low", "Med", "High", "Auto"};
                    for (int s = 0; s < 4; s++) {
                        lv_obj_t *sb = bm_btn(fr, 110, 36, (d->fan_speed == s) ? CLR_AC_ACCENT_DIM : 0x2A2A2A, -8);
                        if (d->fan_speed == s) {
                            lv_obj_set_style_border_color(sb, lv_color_hex(CLR_AC_ACCENT), 0);
                            lv_obj_set_style_border_width(sb, 1, 0);
                        }
                        bm_btn_lbl(sb, speeds[s], 14, (d->fan_speed == s) ? CLR_AC_ACCENT : 0x888888);
                        lv_obj_add_event_cb(sb, exp_fan_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(idx | (s << 8)));
                    }
                } else {
                    lv_obj_t *tr = lv_obj_create(card);
                    lv_obj_set_size(tr, LV_PCT(100), 40);
                    lv_obj_set_style_bg_opa(tr, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(tr, 0, 0);
                    lv_obj_set_style_pad_all(tr, 0, 0);
                    set_flex2(tr, LV_FLEX_FLOW_ROW, 10);
                    lv_obj_set_style_flex_cross_place(tr, LV_FLEX_ALIGN_CENTER, 0);
                    char slbl[32];
                    snprintf(slbl, sizeof(slbl), "%s:", d->display_attr_labels[a]);
                    lv_obj_t *tl1 = bm_lbl(tr, slbl, 14, 0xAAAAAA);
                    lv_obj_set_width(tl1, 80);
                    lv_obj_t *mn = bm_btn(tr, 110, 36, 0x333333, 8);
                    bm_btn_lbl(mn, "-", 18, 0xFFFFFF);
                    lv_obj_add_event_cb(mn, ac_ctrl_step_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)(idx | (a << 8) | ((-1) << 24)));
                    lv_obj_set_style_bg_color(mn, lv_color_hex(0x555555), LV_STATE_PRESSED);
                    if (!d->enabled) lv_obj_add_state(mn, LV_STATE_DISABLED);
                    lv_obj_t *pl = bm_btn(tr, 110, 36, 0x333333, -8);
                    bm_btn_lbl(pl, "+", 18, 0xFFFFFF);
                    lv_obj_add_event_cb(pl, ac_ctrl_step_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)(idx | (a << 8) | (1 << 24)));
                    lv_obj_set_style_bg_color(pl, lv_color_hex(0x555555), LV_STATE_PRESSED);
                    if (!d->enabled) lv_obj_add_state(pl, LV_STATE_DISABLED);
                    char tb[8];
                    snprintf(tb, sizeof(tb), "%d", d->display_attr_values[a] / 10);
                    bm_lbl(tr, tb, 36, d->enabled ? 0xFFFFFF : 0x888888);
                    if (d->display_attr_units[a][0])
                        bm_lbl(tr, d->display_attr_units[a], 18, d->enabled ? 0xAAAAAA : 0x888888);
                }
            }
        } else {
            /* ── Read-only label ── */
            if (d->display_attr_types[a] == ATTR_TYPE_NUMBER) {
                char buf[48];
                snprintf(buf, sizeof(buf), "%s: %d.%d %s",
                         d->display_attr_labels[a],
                         d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                         d->display_attr_units[a]);
                bm_lbl(card, buf, 16, CLR_AC_ACCENT);
            }
        }
    }
    g_card_ptrs[idx] = card;
    return card;
}

static void ac_summary(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E2E2E), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    set_flex2(bar, LV_FLEX_FLOW_ROW, -1);

    g_lbl_ac_on   = bm_lbl(bar, "-- active", 14, 0xAAAAAA);
    g_lbl_ac_avg  = bm_lbl(bar, "Avg: --*C", 14, 0xAAAAAA);
    g_lbl_ac_mode = bm_lbl(bar, "--", 14, CLR_BORDER_GREEN);
}

static void ac_screen_build(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(BMS_BG_SCREEN), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    header_create(scr);
    navbar_create(scr, SCREEN_AC, nav_tabs_ac, nav_dots_ac);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_obj_get_height(scr) - 60 - 50);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 14, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    set_flex2(cont, LV_FLEX_FLOW_COLUMN, 4);

    ac_summary(cont);

    int ac_indices[MAX_DEVICES_DEFAULT];
    int ac_count = 0;
    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_AC) ac_indices[ac_count++] = i;

    int num_rows = ac_count;
    for (int r = 0; r < num_rows; r++) {
        lv_obj_t *row = lv_obj_create(cont);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 240);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        set_flex2(row, LV_FLEX_FLOW_ROW, 0);

        int idx = ac_indices[r];
        lv_obj_t *card = ac_expanded_card(row, idx);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, 230);
    }
}

/*==================
 * SIGN SCREEN
 *==================*/
static void sign_toggle_cb(lv_event_t *e) {
    if (g_bms_mqtt_updating) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (bms_device_locked(g_devices[idx].api_device_id)) return;
    bms_device_toggle(idx);
    bms_device_t *d = &g_devices[idx];
    const char *attr_id = NULL;
    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_control[a] && d->display_attr_types[a] == ATTR_TYPE_BOOL) {
            attr_id = d->display_attr_ids[a]; break;
        }
    }
    if (!attr_id) return;
    char t[64];
    snprintf(t, sizeof(t), "bms/sign/%d/%s/set", d->sign_index, attr_id);
    bms_publish(t, d->enabled ? "ON" : "OFF");
    bms_rebuild_active_screen();
}

static lv_obj_t* sign_expanded_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_scroll_dir(card, LV_DIR_NONE);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(d->enabled ? CLR_BORDER_GREEN : 0x3A3A3A), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 4);

    card_hdr(card, LV_SYMBOL_IMAGE, d->name, d->enabled, CLR_BORDER_GREEN, 0, d->online, true);

    lv_obj_t *sr = lv_obj_create(card);
    lv_obj_set_size(sr, LV_PCT(100), 48);
    lv_obj_set_style_bg_opa(sr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sr, 0, 0);
    lv_obj_set_style_pad_all(sr, 0, 0);
    set_flex2(sr, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_flex_cross_place(sr, LV_FLEX_ALIGN_CENTER, 0);

    // Two explicit buttons: BẬT (ON) / TẮT (OFF)
    lv_obj_t *btng = lv_obj_create(sr);
    lv_obj_set_size(btng, LV_PCT(100), 48);
    lv_obj_set_style_bg_opa(btng, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btng, 0, 0);
    lv_obj_set_style_pad_all(btng, 0, 0);
    lv_obj_set_style_flex_main_place(btng, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    set_flex2(btng, LV_FLEX_FLOW_ROW, 10);

    lv_obj_t *on = bm_btn(btng, LV_PCT(46), 46, d->enabled ? 0x1B3A2A : 0x2E2E2E, 10);
    if (d->enabled) {
        lv_obj_set_style_border_color(on, lv_color_hex(CLR_BORDER_GREEN), 0);
        lv_obj_set_style_border_width(on, 1, 0);
    }
    char lbl_on[32];
    snprintf(lbl_on, sizeof(lbl_on), "%s %s", LV_SYMBOL_POWER, ui_get_text("ON"));
    bm_btn_lbl(on, lbl_on, 16, d->enabled ? 0x00C853 : 0x888888);
    lv_obj_add_event_cb(on, sign_on_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t *off = bm_btn(btng, LV_PCT(46), 46, d->enabled ? 0x2E2E2E : 0x3A2010, 10);
    if (!d->enabled) {
        lv_obj_set_style_border_color(off, lv_color_hex(0xFFA726), 0);
        lv_obj_set_style_border_width(off, 1, 0);
    }
    char lbl_off[32];
    snprintf(lbl_off, sizeof(lbl_off), "%s %s", LV_SYMBOL_POWER, ui_get_text("OFF"));
    bm_btn_lbl(off, lbl_off, 16, d->enabled ? 0x888888 : 0xFFA726);
    lv_obj_add_event_cb(off, sign_off_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_types[a] != ATTR_TYPE_NUMBER) continue;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d.%d %s", d->display_attr_labels[a],
                 d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                 d->display_attr_units[a]);
        bm_lbl(card, buf, 14, 0xAAAAAA);
    }
    g_card_ptrs[idx] = card;
    return card;
}

static void sign_summary(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E2E1E), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    set_flex2(bar, LV_FLEX_FLOW_ROW, -1);

    g_lbl_sign_on  = bm_lbl(bar, "-- active", 14, 0xAAAAAA);
    g_lbl_sign_lux = bm_lbl(bar, "-- lux", 14, 0xAAAAAA);
}

static void sign_screen_build(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(BMS_BG_SCREEN), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    header_create(scr);
    navbar_create(scr, SCREEN_SIGN, nav_tabs_sign, nav_dots_sign);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_obj_get_height(scr) - 60 - 50);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 14, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    set_flex2(cont, LV_FLEX_FLOW_COLUMN, 8);

    sign_summary(cont);

    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_SIGN) sign_expanded_card(cont, i);
}

static lv_obj_t* power_meter_expanded_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];

    // Look up attr indices by ID
    int idx_te = bms_device_get_display_attr(d, "CurrentSummationDelivered");
    int idx_pw = bms_device_get_display_attr(d, "TotalActivePower");
    if (idx_pw < 0) idx_pw = bms_device_get_display_attr(d, "InstantaneousDemand");
    int idx_va = bms_device_get_display_attr(d, "RMSVoltage");
    int idx_ca = bms_device_get_display_attr(d, "RMSCurrent");
    int idx_pa = bms_device_get_display_attr(d, "ActivePower");
    int idx_pfa = bms_device_get_display_attr(d, "PowerFactor");
    int idx_vb = bms_device_get_display_attr(d, "RMSVoltagePhB");
    int idx_cb = bms_device_get_display_attr(d, "RMSCurrentPhB");
    int idx_pb = bms_device_get_display_attr(d, "ActivePowerPhB");
    int idx_pfb = bms_device_get_display_attr(d, "PowerFactorPhB");
    int idx_vc = bms_device_get_display_attr(d, "RMSVoltagePhC");
    int idx_cc = bms_device_get_display_attr(d, "RMSCurrentPhC");
    int idx_pc = bms_device_get_display_attr(d, "ActivePowerPhC");
    int idx_pfc = bms_device_get_display_attr(d, "PowerFactorPhC");

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_scroll_dir(card, LV_DIR_NONE);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(CLR_POWER_ACCENT), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 4);

    card_hdr(card, LV_SYMBOL_CHARGE, d->name, false, CLR_POWER_ACCENT, 0, d->online, false);

    // ── Row 1: Total Energy + Power ──
    lv_obj_t *r1 = lv_obj_create(card);
    lv_obj_set_width(r1, LV_PCT(100));
    lv_obj_set_style_bg_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r1, 0, 0);
    lv_obj_set_style_pad_all(r1, 0, 0);
    set_flex2(r1, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_flex_cross_place(r1, LV_FLEX_ALIGN_CENTER, 0);

    if (idx_te >= 0) {
        lv_obj_t *c = lv_obj_create(r1);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_pad_hor(c, 4, 0);
        set_flex2(c, LV_FLEX_FLOW_COLUMN, 2);

        bm_lbl(c, d->display_attr_labels[idx_te], 11, 0x888888);
        char buf[24];
        if (d->display_attr_values[idx_te] == 0)
            snprintf(buf, sizeof(buf), "--- %s", d->display_attr_units[idx_te]);
        else
            snprintf(buf, sizeof(buf), "%d.%d %s",
                     d->display_attr_values[idx_te] / 10,
                     abs(d->display_attr_values[idx_te]) % 10,
                     d->display_attr_units[idx_te]);
        lv_obj_t *v = bm_lbl(c, buf, 18, 0xFFFFFF);
        g_value_labels[idx][idx_te] = v;
    }
    if (idx_pw >= 0) {
        lv_obj_t *c = lv_obj_create(r1);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_pad_hor(c, 4, 0);
        set_flex2(c, LV_FLEX_FLOW_COLUMN, 2);

        bm_lbl(c, d->display_attr_labels[idx_pw], 11, 0x888888);
        char buf[24];
        snprintf(buf, sizeof(buf), "%d.%d %s",
                 d->display_attr_values[idx_pw] / 10,
                 abs(d->display_attr_values[idx_pw]) % 10,
                 d->display_attr_units[idx_pw]);
        lv_obj_t *v = bm_lbl(c, buf, 18, 0xFFFFFF);
        g_value_labels[idx][idx_pw] = v;
    }

    // ── Row 2: 3 phase columns ──
    lv_obj_t *r2 = lv_obj_create(card);
    lv_obj_set_width(r2, LV_PCT(100));
    lv_obj_set_style_bg_opa(r2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r2, 0, 0);
    lv_obj_set_style_pad_all(r2, 0, 0);
    lv_obj_set_style_pad_top(r2, 6, 0);
    set_flex2(r2, LV_FLEX_FLOW_ROW, 0);

    // Helper macro to create a phase column
    #define MAKE_PHASE_COL(col_var, phase_label, idx_v, idx_c, idx_w, idx_pf) \
        do { \
            lv_obj_t *col_var = lv_obj_create(r2); \
            lv_obj_set_flex_grow(col_var, 1); \
            lv_obj_set_style_bg_opa(col_var, LV_OPA_TRANSP, 0); \
            lv_obj_set_style_border_color(col_var, lv_color_hex(0x333333), 0); \
            lv_obj_set_style_border_width(col_var, 1, 0); \
            lv_obj_set_style_radius(col_var, 0, 0); \
            lv_obj_set_style_pad_all(col_var, 4, 0); \
            set_flex2(col_var, LV_FLEX_FLOW_COLUMN, 1); \
            lv_obj_set_style_flex_cross_place(col_var, LV_FLEX_ALIGN_CENTER, 0); \
            \
            bm_lbl(col_var, phase_label, 12, CLR_POWER_ACCENT); \
            \
            char lb[24]; \
            if (idx_v >= 0) { \
                snprintf(lb, sizeof(lb), "V: %d.%d%s", \
                    d->display_attr_values[idx_v] / 10, \
                    abs(d->display_attr_values[idx_v]) % 10, \
                    d->display_attr_units[idx_v]); \
                lv_obj_t *vv = bm_lbl(col_var, lb, 11, 0xCCCCCC); \
                g_value_labels[idx][idx_v] = vv; \
            } \
            if (idx_c >= 0) { \
                snprintf(lb, sizeof(lb), "A: %d.%d%s", \
                    d->display_attr_values[idx_c] / 10, \
                    abs(d->display_attr_values[idx_c]) % 10, \
                    d->display_attr_units[idx_c]); \
                lv_obj_t *cv = bm_lbl(col_var, lb, 11, 0xCCCCCC); \
                g_value_labels[idx][idx_c] = cv; \
            } \
            if (idx_w >= 0) { \
                snprintf(lb, sizeof(lb), "W: %d.%d%s", \
                    d->display_attr_values[idx_w] / 10, \
                    abs(d->display_attr_values[idx_w]) % 10, \
                    d->display_attr_units[idx_w]); \
                lv_obj_t *wv = bm_lbl(col_var, lb, 11, 0xCCCCCC); \
                g_value_labels[idx][idx_w] = wv; \
            } \
            if (idx_pf >= 0) { \
                snprintf(lb, sizeof(lb), "PF: %d.%d", \
                    d->display_attr_values[idx_pf] / 10, \
                    abs(d->display_attr_values[idx_pf]) % 10); \
                lv_obj_t *pfv = bm_lbl(col_var, lb, 11, 0xCCCCCC); \
                g_value_labels[idx][idx_pf] = pfv; \
            } \
        } while(0)

    MAKE_PHASE_COL(col_a, "Phase A", idx_va, idx_ca, idx_pa, idx_pfa);
    MAKE_PHASE_COL(col_b, "Phase B", idx_vb, idx_cb, idx_pb, idx_pfb);
    MAKE_PHASE_COL(col_c, "Phase C", idx_vc, idx_cc, idx_pc, idx_pfc);

    #undef MAKE_PHASE_COL

    g_card_ptrs[idx] = card;
    return card;
}

static lv_obj_t* light_sensor_expanded_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_scroll_dir(card, LV_DIR_NONE);
    lv_obj_set_style_bg_color(card, lv_color_hex(BMS_BG_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(CLR_LIGHT_ACCENT), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 4);

    card_hdr(card, LV_SYMBOL_EYE_OPEN, d->name, false, CLR_LIGHT_ACCENT, 0, d->online, false);

    // Display all number attributes with label pointers for incremental updates
    int label_idx = 0;
    for (int a = 0; a < d->display_attr_count && label_idx < MAX_DISPLAY_ATTRS; a++) {
        if (d->display_attr_types[a] != ATTR_TYPE_NUMBER) continue;
        
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LV_PCT(100), 24);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        set_flex2(row, LV_FLEX_FLOW_ROW, 8);
        
        char label_buf[32];
        snprintf(label_buf, sizeof(label_buf), "%s:", d->display_attr_labels[a]);
        lv_obj_t *label = bm_lbl(row, label_buf, 14, 0xAAAAAA);
        lv_obj_set_width(label, 120);
        
        char value_buf[16];
        snprintf(value_buf, sizeof(value_buf), "%d.%d %s",
                 d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                 d->display_attr_units[a]);
        lv_obj_t *value_label = bm_lbl(row, value_buf, 14, 0xAAAAAA);
        
        // Store label pointers for incremental updates
        g_attr_labels[idx][label_idx] = label;
        g_value_labels[idx][label_idx] = value_label;
        label_idx++;
    }
    g_card_ptrs[idx] = card;
    return card;
}

/*==================
 * POWER METER SCREEN
 *==================*/
static void power_summary(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CLR_POWER_ACCENT_DIM), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    set_flex2(bar, LV_FLEX_FLOW_ROW, -1);
    bm_lbl(bar, "-- active", 14, 0xAAAAAA);
}

static void power_meter_screen_build(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(BMS_BG_SCREEN), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    header_create(scr);
    navbar_create(scr, SCREEN_POWER, nav_tabs_pwr, nav_dots_pwr);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_obj_get_height(scr) - 60 - 50);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 14, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    set_flex2(cont, LV_FLEX_FLOW_COLUMN, 8);

    power_summary(cont);

    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_POWER_METER) power_meter_expanded_card(cont, i);
}

/*==================
 * LIGHT SENSOR SCREEN
 *==================*/
static void light_summary(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CLR_LIGHT_ACCENT_DIM), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    set_flex2(bar, LV_FLEX_FLOW_ROW, -1);
    bm_lbl(bar, "-- active", 14, 0xAAAAAA);
}

static void light_sensor_screen_build(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(BMS_BG_SCREEN), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    header_create(scr);
    navbar_create(scr, SCREEN_LIGHT, nav_tabs_lt, nav_dots_lt);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_obj_get_height(scr) - 60 - 50);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 14, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    set_flex2(cont, LV_FLEX_FLOW_COLUMN, 8);

    light_summary(cont);

    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_LIGHT_SENSOR) light_sensor_expanded_card(cont, i);
}

/*==================
 * SWITCH SCREEN
 *==================*/
static void sw_toggle_cb(lv_event_t *e) {
    if (g_bms_mqtt_updating) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (bms_device_locked(g_devices[idx].api_device_id)) return;
    bms_device_toggle(idx);
    bms_device_t *d = &g_devices[idx];
    const char *attr_id = NULL;
    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_control[a] && d->display_attr_types[a] == ATTR_TYPE_BOOL) {
            attr_id = d->display_attr_ids[a]; break;
        }
    }
    if (!attr_id) return;
    char t[64];
    snprintf(t, sizeof(t), "bms/switch/%d/%s/set", d->switch_index, attr_id);
    bms_publish(t, d->enabled ? "ON" : "OFF");
    bms_rebuild_active_screen();
}

static lv_obj_t* switch_expanded_card(lv_obj_t *parent, int idx) {
    bms_device_t *d = &g_devices[idx];
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_scroll_dir(card, LV_DIR_NONE);
    lv_obj_set_style_bg_color(card, d->enabled ? lv_color_hex(CLR_SWITCH_ACCENT_DIM) : lv_color_hex(BMS_BG_CARD_DARK), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(d->enabled ? CLR_SWITCH_ACCENT : 0x3A3A3A), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_height(card, 200);
    set_flex2(card, LV_FLEX_FLOW_COLUMN, 4);

    card_hdr(card, LV_SYMBOL_HOME, d->name, d->enabled, CLR_SWITCH_ACCENT, 0, d->online, true);
    lv_obj_t *sr = lv_obj_create(card);
    lv_obj_set_width(sr, LV_PCT(100));
    lv_obj_set_flex_grow(sr, 1);
    lv_obj_set_style_bg_opa(sr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sr, 0, 0);
    lv_obj_set_style_pad_all(sr, 0, 0);
    set_flex2(sr, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_flex_cross_place(sr, LV_FLEX_ALIGN_CENTER, 0);

    bm_lbl(sr, d->enabled ? "ON" : "OFF", 18, d->enabled ? CLR_SWITCH_ACCENT : 0x888888);

    lv_obj_t *spflex = lv_obj_create(sr);
    lv_obj_set_flex_grow(spflex, 1);
    lv_obj_set_style_bg_opa(spflex, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spflex, 0, 0);

    // Two explicit buttons: BẬT (ON) / TẮT (OFF) — same style as sign
    onoff_btns(sr, idx);
    for (int a = 0; a < d->display_attr_count; a++) {
        if (d->display_attr_types[a] != ATTR_TYPE_NUMBER) continue;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d.%d %s", d->display_attr_labels[a],
                 d->display_attr_values[a] / 10, abs(d->display_attr_values[a]) % 10,
                 d->display_attr_units[a]);
        bm_lbl(card, buf, 14, 0xAAAAAA);
    }
    g_card_ptrs[idx] = card;
    return card;
}

static void switch_summary(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0D1A2A), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    set_flex2(bar, LV_FLEX_FLOW_ROW, -1);

    g_lbl_sw_on = bm_lbl(bar, "-- active", 14, 0xAAAAAA);
}

static void switch_screen_build(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(BMS_BG_SCREEN), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    header_create(scr);
    navbar_create(scr, SCREEN_SWITCH, nav_tabs_sw, nav_dots_sw);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_obj_get_height(scr) - 60 - 50);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 14, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    set_flex2(cont, LV_FLEX_FLOW_COLUMN, 8);

    switch_summary(cont);

    for (int i = 0; i < g_device_count; i++)
        if (g_devices[i].type == DEV_TYPE_SWITCH) switch_expanded_card(cont, i);
}

/*==================
 * REBUILD
 *==================*/

// Phase 1: Throttle rebuilds - prevent too frequent rebuilds
static uint32_t g_last_rebuild_time = 0;
#define MIN_REBUILD_INTERVAL_MS 1000  // Max 2 rebuilds/giây

static void throttled_rebuild_screens(void) {
    uint32_t now = lv_tick_get();
    if (now - g_last_rebuild_time < MIN_REBUILD_INTERVAL_MS) {
        return;  // Skip rebuild
    }
    g_last_rebuild_time = now;
    bms_rebuild_active_screen();
}

// Phase 2: Lazy rendering - only rebuild active screen
void bms_rebuild_active_screen(void) {
    switch (g_current_screen) {
        case SCREEN_OVERVIEW:
            if (g_bms_overview) lv_obj_delete(g_bms_overview);
            g_bms_overview = lv_obj_create(NULL);
            overview_build(g_bms_overview);
            break;
        case SCREEN_AC:
            if (g_bms_ac) lv_obj_delete(g_bms_ac);
            g_bms_ac = lv_obj_create(NULL);
            ac_screen_build(g_bms_ac);
            break;
        case SCREEN_SIGN:
            if (g_bms_sign) lv_obj_delete(g_bms_sign);
            g_bms_sign = lv_obj_create(NULL);
            sign_screen_build(g_bms_sign);
            break;
        case SCREEN_POWER:
            if (g_bms_power) lv_obj_delete(g_bms_power);
            g_bms_power = lv_obj_create(NULL);
            power_meter_screen_build(g_bms_power);
            break;
        case SCREEN_LIGHT:
            if (g_bms_light) lv_obj_delete(g_bms_light);
            g_bms_light = lv_obj_create(NULL);
            light_sensor_screen_build(g_bms_light);
            break;
        case SCREEN_SWITCH:
            if (g_bms_switch) lv_obj_delete(g_bms_switch);
            g_bms_switch = lv_obj_create(NULL);
            switch_screen_build(g_bms_switch);
            break;
    }
    bms_load_screen(g_current_screen);
}

// Phase 3: Incremental update - update card style without full rebuild
static void bms_refresh_card_style(int device_idx) {
    if (device_idx >= g_device_count) return;
    lv_obj_t *card = g_card_ptrs[device_idx];
    if (!card) return;
    bms_device_t *d = &g_devices[device_idx];

    uint32_t accent = (d->type == DEV_TYPE_AC) ? CLR_AC_ACCENT :
                      (d->type == DEV_TYPE_SIGN) ? CLR_BORDER_GREEN :
                      CLR_SWITCH_ACCENT;
    uint32_t bg = (d->type == DEV_TYPE_AC) ? 0x0D2929 :
                  (d->type == DEV_TYPE_SWITCH) ? CLR_SWITCH_ACCENT_DIM :
                  BMS_BG_CARD;

    lv_obj_set_style_bg_color(card, d->enabled ? lv_color_hex(bg) : lv_color_hex(BMS_BG_CARD_DARK), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(d->enabled ? accent : 0x3A3A3A), 0);
}

// Phase 3: Incremental update - update switch state directly (no rebuild)
static void bms_refresh_switch(int device_idx) {
    if (device_idx >= g_device_count) return;
    lv_obj_t *sw = g_sw_ptrs[device_idx];
    if (!sw) return;
    bms_device_t *d = &g_devices[device_idx];
    
    if (d->enabled)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

// Phase 3: Incremental update - update only labels
static void update_display_attr_label(int device_idx, int attr_idx) {
    if (device_idx >= g_device_count || attr_idx >= MAX_DISPLAY_ATTRS) return;
    if (g_value_labels[device_idx][attr_idx]) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d.%d %s", 
                 g_devices[device_idx].display_attr_values[attr_idx] / 10,
                 abs(g_devices[device_idx].display_attr_values[attr_idx]) % 10,
                 g_devices[device_idx].display_attr_units[attr_idx]);
        lv_label_set_text(g_value_labels[device_idx][attr_idx], buf);
    }
}

void bms_rebuild_all_screens(void) {
    if (g_bms_overview) { lv_obj_delete(g_bms_overview); }
    if (g_bms_ac)       { lv_obj_delete(g_bms_ac); }
    if (g_bms_sign)     { lv_obj_delete(g_bms_sign); }
    if (g_bms_power)    { lv_obj_delete(g_bms_power); }
    if (g_bms_light)    { lv_obj_delete(g_bms_light); }
    if (g_bms_switch)   { lv_obj_delete(g_bms_switch); }

    // Clear label pointers
    memset(g_attr_labels, 0, sizeof(g_attr_labels));
    memset(g_value_labels, 0, sizeof(g_value_labels));

    g_bms_overview = lv_obj_create(NULL);
    g_bms_ac       = lv_obj_create(NULL);
    g_bms_sign     = lv_obj_create(NULL);
    g_bms_power    = lv_obj_create(NULL);
    g_bms_light    = lv_obj_create(NULL);
    g_bms_switch   = lv_obj_create(NULL);

    overview_build(g_bms_overview);
    ac_screen_build(g_bms_ac);
    sign_screen_build(g_bms_sign);
    power_meter_screen_build(g_bms_power);
    light_sensor_screen_build(g_bms_light);
    switch_screen_build(g_bms_switch);

    bms_load_screen(g_current_screen);
}

/*==================
 * NAVIGATION
 *==================*/
screen_id_t bms_get_current_screen(void) { return g_current_screen; }

void bms_load_screen(screen_id_t s) {
    lv_obj_t *scr = NULL;
    if (s == SCREEN_OVERVIEW) scr = g_bms_overview;
    else if (s == SCREEN_AC) scr = g_bms_ac;
    else if (s == SCREEN_SIGN) scr = g_bms_sign;
    else if (s == SCREEN_POWER) scr = g_bms_power;
    else if (s == SCREEN_LIGHT) scr = g_bms_light;
    else scr = g_bms_switch;

    if (scr) lv_scr_load(scr);
    g_current_screen = s;
}

void bms_navigate_to(screen_id_t target) {
    if (target == g_current_screen) return;

    // Delete old target screen to force fresh rebuild with latest state
    if (target == SCREEN_OVERVIEW && g_bms_overview) { lv_obj_delete(g_bms_overview); g_bms_overview = NULL; }
    else if (target == SCREEN_AC && g_bms_ac) { lv_obj_delete(g_bms_ac); g_bms_ac = NULL; }
    else if (target == SCREEN_SIGN && g_bms_sign) { lv_obj_delete(g_bms_sign); g_bms_sign = NULL; }
    else if (target == SCREEN_POWER && g_bms_power) { lv_obj_delete(g_bms_power); g_bms_power = NULL; }
    else if (target == SCREEN_LIGHT && g_bms_light) { lv_obj_delete(g_bms_light); g_bms_light = NULL; }
    else if (target == SCREEN_SWITCH && g_bms_switch) { lv_obj_delete(g_bms_switch); g_bms_switch = NULL; }

    // Phase 2: Lazy rendering - build screen on demand
    lv_obj_t *next = NULL;
    if (target == SCREEN_OVERVIEW) {
        g_bms_overview = lv_obj_create(NULL);
        overview_build(g_bms_overview);
        next = g_bms_overview;
    } else if (target == SCREEN_AC) {
        g_bms_ac = lv_obj_create(NULL);
        ac_screen_build(g_bms_ac);
        next = g_bms_ac;
    } else if (target == SCREEN_SIGN) {
        g_bms_sign = lv_obj_create(NULL);
        sign_screen_build(g_bms_sign);
        next = g_bms_sign;
    } else if (target == SCREEN_POWER) {
        g_bms_power = lv_obj_create(NULL);
        power_meter_screen_build(g_bms_power);
        next = g_bms_power;
    } else if (target == SCREEN_LIGHT) {
        g_bms_light = lv_obj_create(NULL);
        light_sensor_screen_build(g_bms_light);
        next = g_bms_light;
    } else {
        g_bms_switch = lv_obj_create(NULL);
        switch_screen_build(g_bms_switch);
        next = g_bms_switch;
    }

    lv_scr_load_anim_t dir = (target > g_current_screen) ?
        LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT;

    g_current_screen = target;
    lv_scr_load_anim(next, dir, 250, 0, false);
}

/*==================
 * STATE HANDLER
 *==================*/
void bms_handle_state(const char *topic, const char *payload) {
    if (!topic || !payload) return;

    if (strcmp(topic, BMS_TOPIC_OUTDOOR_TEMP) == 0 && g_lbl_outdoor) {
        char buf[64];
        snprintf(buf, sizeof(buf),  "%s: %s*C", ui_get_text("Outdoor"), payload);
        lv_label_set_text(g_lbl_outdoor, buf);
        return;
    }
    if (strcmp(topic, BMS_TOPIC_ENERGY_TODAY) == 0 && g_lbl_energy) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s kWh", payload);
        lv_label_set_text(g_lbl_energy, buf);
        return;
    }

    if (strcmp(topic, BMS_TOPIC_AC_SUMMARY_ON) == 0 && g_lbl_ac_on) {
        lv_label_set_text(g_lbl_ac_on, payload);
        return;
    }
    if (strcmp(topic, BMS_TOPIC_AC_SUMMARY_AVG) == 0 && g_lbl_ac_avg) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %s*C", ui_get_text("Avg"), payload);
        lv_label_set_text(g_lbl_ac_avg, buf);
        return;
    }
    if (strcmp(topic, BMS_TOPIC_AC_SUMMARY_MODE) == 0 && g_lbl_ac_mode) {
        lv_label_set_text(g_lbl_ac_mode, payload);
        return;
    }
    if (strcmp(topic, BMS_TOPIC_SIGN_SUMMARY_ON) == 0 && g_lbl_sign_on) {
        lv_label_set_text(g_lbl_sign_on, payload);
        return;
    }
    if (strcmp(topic, BMS_TOPIC_SIGN_SUMMARY_LUX) == 0 && g_lbl_sign_lux) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s lux", payload);
        lv_label_set_text(g_lbl_sign_lux, buf);
        return;
    }
    if (strcmp(topic, BMS_TOPIC_SW_SUMMARY_ON) == 0 && g_lbl_sw_on) {
        lv_label_set_text(g_lbl_sw_on, payload);
        return;
    }

    for (int i = 0; i < g_device_count; i++) {
        bms_device_t *d = &g_devices[i];
        char expected[64];

        if (d->type == DEV_TYPE_AC) {
            snprintf(expected, sizeof(expected), BMS_TOPIC_AC_TEMP, d->ac_index);
            if (strcmp(topic, expected) == 0) { d->value = atoi(payload); bms_rebuild_all_screens(); return; }

            snprintf(expected, sizeof(expected), BMS_TOPIC_AC_ROOM_TEMP, d->ac_index);
            if (strcmp(topic, expected) == 0) {
                d->room_temp = atoi(payload);
                printf("[BMS] AC[%d] room_temp=%d from MQTT\n", d->ac_index, d->room_temp);
                bms_rebuild_active_screen(); return;
            }

            snprintf(expected, sizeof(expected), BMS_TOPIC_AC_POWER, d->ac_index);
            if (strcmp(topic, expected) == 0) {
                d->enabled = (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0);
                bms_rebuild_active_screen(); return;
            }

            snprintf(expected, sizeof(expected), BMS_TOPIC_AC_ONLINE, d->ac_index);
            if (strcmp(topic, expected) == 0) {
                d->online = (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0);
                bms_rebuild_active_screen(); return;
            }

            snprintf(expected, sizeof(expected), BMS_TOPIC_AC_MODE, d->ac_index);
            if (strcmp(topic, expected) == 0) { d->mode = atoi(payload); bms_rebuild_all_screens(); return; }

            snprintf(expected, sizeof(expected), BMS_TOPIC_AC_FAN, d->ac_index);
            if (strcmp(topic, expected) == 0) { d->fan_speed = atoi(payload); bms_rebuild_all_screens(); return; }
        }

        if (d->type == DEV_TYPE_SIGN) {
            snprintf(expected, sizeof(expected), BMS_TOPIC_SIGN_POWER, d->sign_index);
            if (strcmp(topic, expected) == 0) {
                d->enabled = (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0);
                bms_rebuild_active_screen(); return;
            }

            snprintf(expected, sizeof(expected), BMS_TOPIC_SIGN_ONLINE, d->sign_index);
            if (strcmp(topic, expected) == 0) {
                d->online = (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0);
                bms_rebuild_active_screen(); return;
            }

            snprintf(expected, sizeof(expected), BMS_TOPIC_SIGN_LUX, d->sign_index);
            if (strcmp(topic, expected) == 0) { d->lux_value = atoi(payload); bms_rebuild_all_screens(); return; }

            snprintf(expected, sizeof(expected), BMS_TOPIC_SIGN_MODE, d->sign_index);
            if (strcmp(topic, expected) == 0) { d->mode = atoi(payload); bms_rebuild_all_screens(); return; }

            snprintf(expected, sizeof(expected), BMS_TOPIC_SIGN_LUX_ON, d->sign_index);
            if (strcmp(topic, expected) == 0) { d->lux_on_thresh = atoi(payload); bms_rebuild_all_screens(); return; }

            snprintf(expected, sizeof(expected), BMS_TOPIC_SIGN_LUX_OFF, d->sign_index);
            if (strcmp(topic, expected) == 0) { d->lux_off_thresh = atoi(payload); bms_rebuild_all_screens(); return; }
        }

        if (d->type == DEV_TYPE_SWITCH) {
            snprintf(expected, sizeof(expected), BMS_TOPIC_SWITCH_POWER, d->switch_index);
            if (strcmp(topic, expected) == 0) {
                d->enabled = (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0);
                bms_rebuild_active_screen(); return;
            }

            snprintf(expected, sizeof(expected), BMS_TOPIC_SWITCH_ONLINE, d->switch_index);
            if (strcmp(topic, expected) == 0) {
                d->online = (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0);
                bms_rebuild_active_screen(); return;
            }
        }
    }

    // ── Generic display attr handler for AC devices ──
    // bms/ac/{idx}/{attr_id}  (attr_id from YAML, e.g., "0203" Room Temp)
    if (strncmp(topic, "bms/ac/", 7) == 0) {
        const char *after = topic + 7;
        int ac_idx = atoi(after);
        const char *slash = strchr(after, '/');
        if (slash) {
            const char *attr_id = slash + 1;
            for (int i = 0; i < g_device_count; i++) {
                bms_device_t *d = &g_devices[i];
                if (d->type == DEV_TYPE_AC && d->ac_index == ac_idx) {
                    // Handle bool-type attributes (Power on/off)
                    if (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0) {
                        if (strcmp(attr_id, "0101") == 0) {
                            d->enabled = true;
                            int attr_idx = bms_device_get_display_attr(d, attr_id);
                            if (attr_idx >= 0) {
                                bms_device_set_display_value(d, attr_id, 1);
                                update_display_attr_label(i, attr_idx);
                            }
                            bms_refresh_card_style(i);
                            bms_refresh_switch(i);
                            bms_rebuild_active_screen();
                            return;
                        }
                    } else if (strcmp(payload, "OFF") == 0 || strcmp(payload, "false") == 0) {
                        if (strcmp(attr_id, "0101") == 0) {
                            d->enabled = false;
                            int attr_idx = bms_device_get_display_attr(d, attr_id);
                            if (attr_idx >= 0) {
                                bms_device_set_display_value(d, attr_id, 0);
                                update_display_attr_label(i, attr_idx);
                            }
                            bms_refresh_card_style(i);
                            bms_refresh_switch(i);
                            bms_rebuild_active_screen();
                            return;
                        }
                    }
                    // Handle number-type attributes
                    int attr_idx = bms_device_get_display_attr(d, attr_id);
                    if (attr_idx >= 0) {
                        int16_t scaled = (int16_t)(atof(payload) * 10.0);
                        bms_device_set_display_value(d, attr_id, scaled);
                        // Map to dedicated struct fields for UI compatibility
                        if (strcmp(attr_id, "0202") == 0) d->value = atoi(payload);
                        else if (strcmp(attr_id, "0203") == 0) d->room_temp = atoi(payload);
                        else if (strcmp(attr_id, "0405") == 0) d->fan_speed = atoi(payload);
                        update_display_attr_label(i, attr_idx);
                        throttled_rebuild_screens();
                        return;
                    }
                    // Handle mode attribute
                    if (strcmp(attr_id, "0406") == 0) {
                        d->mode = atoi(payload);
                        throttled_rebuild_screens();
                        return;
                    }
                    return;
                }
            }
        }
    }

    // ── Generic online status handler ──
    // bms/{type}/{idx}/online
    if (strstr(topic, "/online") == topic + strlen(topic) - 7) {
        // Extract type and index from bms/ac/0/online format
        const char *start = topic + 4; // skip "bms/"
        const char *slash1 = strchr(start, '/');
        if (slash1) {
            char type_str[16] = {0};
            int type_len = slash1 - start;
            if (type_len > 15) type_len = 15;
            strncpy(type_str, start, type_len);
            int idx = atoi(slash1 + 1);
            bool online = (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0);
            
            for (int i = 0; i < g_device_count; i++) {
                bms_device_t *d = &g_devices[i];
                bool match = false;
                if (strcmp(type_str, "ac") == 0 && d->type == DEV_TYPE_AC && d->ac_index == idx) match = true;
                else if (strcmp(type_str, "sign") == 0 && d->type == DEV_TYPE_SIGN && d->sign_index == idx) match = true;
                else if (strcmp(type_str, "power") == 0 && d->type == DEV_TYPE_POWER_METER && d->power_index == idx) match = true;
                else if (strcmp(type_str, "light") == 0 && d->type == DEV_TYPE_LIGHT_SENSOR && d->light_sensor_index == idx) match = true;
                
                if (match) {
                    d->online = online;
                    bms_refresh_card_style(i);
                    return;
                }
            }
        }
    }

    // ── Generic display attr handler for sign-indexed devices ──
    // bms/sign/{idx}/{attr_id}
    if (strncmp(topic, "bms/sign/", 9) == 0) {
        const char *after = topic + 9;
        int sign_idx = atoi(after);
        const char *slash = strchr(after, '/');
        if (slash) {
            const char *attr_id = slash + 1;
            for (int i = 0; i < g_device_count; i++) {
                bms_device_t *d = &g_devices[i];
                if (d->type == DEV_TYPE_SIGN && d->sign_index == sign_idx) {
                    int attr_idx = bms_device_get_display_attr(d, attr_id);
                    if (attr_idx >= 0) {
                        if (strcmp(payload, "ON") == 0 || strcmp(payload, "true") == 0) {
                            bms_device_set_display_value(d, attr_id, 1);
                            if (d->display_attr_control[attr_idx] && d->display_attr_types[attr_idx] == ATTR_TYPE_BOOL) {
                                d->enabled = true;
                                bms_refresh_card_style(i);
                                bms_refresh_switch(i);
                            }
                        } else if (strcmp(payload, "OFF") == 0 || strcmp(payload, "false") == 0) {
                            bms_device_set_display_value(d, attr_id, 0);
                            if (d->display_attr_control[attr_idx] && d->display_attr_types[attr_idx] == ATTR_TYPE_BOOL) {
                                d->enabled = false;
                                bms_refresh_card_style(i);
                                bms_refresh_switch(i);
                            }
                        } else {
                            bms_device_set_display_value(d, attr_id, (int16_t)(atof(payload) * 10.0));
                        }
                        
                        update_display_attr_label(i, attr_idx);
                    }
                    return;
                }
            }
        }
    }

    // ── Generic display attr handler for power_meter devices ──
    // bms/power/{idx}/{attr_id}
    if (strncmp(topic, "bms/power/", 10) == 0) {
        const char *after = topic + 10;
        int pwr_idx = atoi(after);
        const char *slash = strchr(after, '/');
        if (slash) {
            const char *attr_id = slash + 1;
            for (int i = 0; i < g_device_count; i++) {
                bms_device_t *d = &g_devices[i];
                if (d->type == DEV_TYPE_POWER_METER && d->power_index == pwr_idx) {
                    int attr_idx = bms_device_get_display_attr(d, attr_id);
                    if (attr_idx >= 0) {
                        bms_device_set_display_value(d, attr_id, (int16_t)(atof(payload) * 10.0));
                        
                        // Phase 3: Incremental update - only update label if visible
                        update_display_attr_label(i, attr_idx);
                    }
                    return;
                }
            }
        }
    }

    // ── Generic display attr handler for light_sensor devices ──
    // bms/light/{idx}/{attr_id}
    if (strncmp(topic, "bms/light/", 10) == 0) {
        const char *after = topic + 10;
        int lt_idx = atoi(after);
        const char *slash = strchr(after, '/');
        if (slash) {
            const char *attr_id = slash + 1;
            for (int i = 0; i < g_device_count; i++) {
                bms_device_t *d = &g_devices[i];
                if (d->type == DEV_TYPE_LIGHT_SENSOR && d->light_sensor_index == lt_idx) {
                    int attr_idx = bms_device_get_display_attr(d, attr_id);
                    if (attr_idx >= 0) {
                        bms_device_set_display_value(d, attr_id, (int16_t)(atof(payload) * 10.0));
                        
                        // Phase 3: Incremental update - only update label if visible
                        update_display_attr_label(i, attr_idx);
                    }
                    return;
                }
            }
        }
    }
}

/*==================
 * PENDING COMMAND TIMEOUT / ROLLBACK
 *==================*/

// Find the device array index by its api_device_id, -1 if not found.
static int bms_device_index_by_api(const char *device_id)
{
    for (int i = 0; i < g_device_count; i++) {
        if (strcmp(g_devices[i].api_device_id, device_id) == 0) return i;
    }
    return -1;
}

// Rollback a pending command to its previous value (UI: un-lock + revert).
static void bms_rollback(bms_pending_cmd_t *p)
{
    int i = bms_device_index_by_api(p->device_id);
    p->active = false;
    if (i < 0) return;

    bms_device_t *d = &g_devices[i];
    if (p->kind == CMD_TOGGLE) {
        if (p->has_prev) {
            int ai = bms_device_get_display_attr(d, p->attr_id);
            if (ai >= 0) {
                d->display_attr_values[ai] = p->prev_bool_val;
                update_display_attr_label(i, ai);
            }
        }
        d->enabled = (p->prev_bool_val == 1);
    } else if (p->kind == CMD_SET_ATTR) {
        int ai = bms_device_get_display_attr(d, p->attr_id);
        if (p->has_prev && ai >= 0) {
            d->display_attr_values[ai] = p->prev_num_val;
            d->value = p->prev_num_val / 10;
            update_display_attr_label(i, ai);
        }
    }
    printf("[BMS] rollback %s (cmd %s)\n", p->device_id, p->command_id);
    bms_rebuild_active_screen();
}

// Show a transient notification banner over the top layer.
static void bms_notify_hide_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    if (g_notify_banner) { lv_obj_delete(g_notify_banner); g_notify_banner = NULL; }
    g_notify_timer = NULL;
}

static void bms_show_notify(const char *msg)
{
    if (g_notify_banner) { lv_obj_delete(g_notify_banner); g_notify_banner = NULL; }
    if (g_notify_timer)  { lv_timer_delete(g_notify_timer); g_notify_timer  = NULL; }

    lv_obj_t *b = lv_obj_create(lv_layer_top());
    lv_obj_set_size(b, LV_PCT(88), LV_SIZE_CONTENT);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x7A1717), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xFF8A80), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 6, 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_text(l, ui_get_text(msg));
    lv_obj_set_style_text_font(l, ui_get_font(14), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l);

    g_notify_banner = b;
    g_notify_timer = lv_timer_create(bms_notify_hide_cb, 3500, NULL);
    lv_timer_set_repeat_count(g_notify_timer, 1);
}

// Check all pending commands for timeout. Must roll back commands that could
// not be confirmed against a REACHABLE device. If the device is offline, the
// command may still reach it when it comes back, so we KEEP the requested
// state (no rollback) and show an explicit notification instead.
static void bms_process_pending(void)
{
    uint32_t now = bms_now_ms();
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!g_pending[i].active || now < g_pending[i].deadline_ms) continue;

        int di = bms_device_index_by_api(g_pending[i].device_id);
        bool offline = (di < 0) || !g_devices[di].online;
        if (offline) {
            // Device offline: the command may still reach it when it comes
            // back, so KEEP the user's chosen state (no rollback) and show a
            // notification. Applies to both toggle (ON/OFF) and set-attribute
            // (temp +/-, fan, mode) commands.
            g_pending[i].active = false;
            char name[48];
            if (g_pending[i].kind == CMD_TOGGLE) {
                snprintf(name, sizeof(name), "%s %s",
                         di >= 0 ? g_devices[di].name : g_pending[i].device_id,
                         g_devices[di].enabled ? "ON" : "OFF");
            } else {
                int ai = (di >= 0) ? bms_device_get_display_attr(&g_devices[di], g_pending[i].attr_id) : -1;
                if (di >= 0 && ai >= 0) {
                    snprintf(name, sizeof(name), "%s: %d",
                             g_devices[di].display_attr_labels[ai],
                             g_devices[di].display_attr_values[ai] / 10);
                } else {
                    snprintf(name, sizeof(name), "%s", g_pending[i].device_id);
                }
            }
            char msg[96];
            snprintf(msg, sizeof(msg), "%s - chua duoc xac nhan", name);
            printf("[BMS] %s: command not confirmed (offline), keeping state\n",
                   g_pending[i].device_id);
            bms_show_notify(msg);
        } else {
            bms_rollback(&g_pending[i]);
        }
    }
}

static void bms_pending_timer_cb(lv_timer_t *t)
{
    (void)t;
    bms_process_pending();
}

void bms_handle_api_event(const char *device_id, const char *attr,
                          const char *value, const char *ack_command_id)
{
    if (!device_id || !attr || !value) return;
    FILE *lg = fopen("/tmp/btn.log", "a");
    if (lg) { fprintf(lg, "on_event device=%s attr=%s value=%s ack=%s\n",
                      device_id, attr, value, ack_command_id ? ack_command_id : "none"); fclose(lg); }

    // Ack: resolve the matching pending command (unlock the device).
    if (ack_command_id) {
        const char *acked_dev = bms_ack_command(ack_command_id);
        if (acked_dev) {
            printf("[BMS] ack %s for %s\n", ack_command_id, acked_dev);
        }
    }

    int i = bms_device_index_by_api(device_id);
    if (i < 0) return;
    bms_device_t *d = &g_devices[i];

    // online attribute
    if (strcmp(attr, "online") == 0) {
        d->online = (strcmp(value, "true") == 0);
        bms_refresh_card_style(i);
        bms_rebuild_active_screen();
        return;
    }

    // While a command is pending for this device, ignore stale attribute
    // updates that are NOT the expected confirmation.
    if (bms_pending_find_by_device(device_id)) return;

    int ai = bms_device_get_display_attr(d, attr);
    if (ai >= 0) {
        if (d->display_attr_types[ai] == ATTR_TYPE_BOOL) {
            bool on = (strcmp(value, "true") == 0 || strcmp(value, "ON") == 0 || strcmp(value, "1") == 0);
            d->display_attr_values[ai] = on ? 1 : 0;
            if (d->display_attr_control[ai]) {
                d->enabled = on;
                bms_refresh_card_style(i);
                bms_refresh_switch(i);
                // Control toggles must always re-render (the ON/OFF label and
                // button styles are set at build time only). A throttled rebuild
                // may be dropped under rapid device polling, leaving the card
                // showing the stale state.
                bms_rebuild_active_screen();
                return;
            }
        } else {
            d->display_attr_values[ai] = (int16_t)(atof(value) * 10.0);
        }
        update_display_attr_label(i, ai);
        // map to dedicated struct fields for UI compatibility
        if (strcmp(attr, "0202") == 0) d->value = atoi(value);
        else if (strcmp(attr, "0203") == 0) d->room_temp = atoi(value);
        else if (strcmp(attr, "0405") == 0) d->fan_speed = atoi(value);
        throttled_rebuild_screens();
    }
}

/*==================
 * INIT
 *==================*/
static bool g_bms_initialized = false;

void bms_init(void) {
    if (g_bms_initialized) {
        if (g_bms_overview) lv_scr_load(g_bms_overview);
        return;
    }
    g_bms_initialized = true;

    g_main_screen = lv_screen_active();
    // Do NOT reset g_device_count here: http_client_init() already filled
    // g_devices[] from the engine catalog before bms_init() runs.

    if (!g_devices) {
        g_max_devices = MAX_DEVICES_DEFAULT;
        g_devices = calloc(g_max_devices, sizeof(bms_device_t));
        if (!g_devices) {
            printf("[BMS] FATAL: out of memory for device array\n");
            return;
        }
    }

    // New flow: devices come from the engine catalog (fetched by http_client_init
    // before bms_init). If the catalog is empty (engine unreachable at startup),
    // fall back to the local devices.yaml.
    if (g_device_count == 0) {
        FILE *lg = fopen("/tmp/btn.log", "a");
        if (lg) { fprintf(lg, "bms_init: catalog EMPTY, using YAML fallback\n"); fclose(lg); }
        struct stat st;
        if (stat(BMS_YAML_FILE, &st) != 0) {
            bms_yaml_write_default(BMS_YAML_FILE);
        }
        if (!bms_yaml_load(BMS_YAML_FILE)) {
            printf("[BMS] YAML load failed, falling back to hardcoded defaults\n");
            g_device_count = 0;

            bms_device_t a;
            memset(&a, 0, sizeof(a));
            a.device_id = 1; a.type = DEV_TYPE_AC; a.ac_index = 0;
            strcpy(a.name, "AC Khu A"); strcpy(a.group, "Khu A");
            strncpy(a.api_device_id, "ac_0", sizeof(a.api_device_id) - 1);
            bms_device_add(a);

            a.device_id = 2; a.ac_index = 1;
            strcpy(a.name, "AC Khu B"); strcpy(a.group, "Khu B");
            strncpy(a.api_device_id, "ac_1", sizeof(a.api_device_id) - 1);
            bms_device_add(a);

            bms_device_t s;
            memset(&s, 0, sizeof(s));
            s.device_id = 3; s.type = DEV_TYPE_SIGN; s.sign_index = 0;
            strcpy(s.name, "Bien QC"); strcpy(s.group, "Ngoai");
            strncpy(s.api_device_id, "sign_0", sizeof(s.api_device_id) - 1);
            bms_device_add(s);

            bms_device_t w;
            memset(&w, 0, sizeof(w));
            w.device_id = 4; w.type = DEV_TYPE_SWITCH; w.switch_index = 0;
            strcpy(w.name, "Den Khu A"); strcpy(w.group, "Khu A");
            strncpy(w.api_device_id, "switch_0", sizeof(w.api_device_id) - 1);
            bms_device_add(w);
        }
    }

    // Periodic timer to expire pending control commands (5s rollback).
    if (!g_bms_pending_timer) {
        g_bms_pending_timer = lv_timer_create(bms_pending_timer_cb, 250, NULL);
    }

    // Phase 2: Lazy rendering - only build overview screen initially
    // Other screens will be built on demand when navigating to them
    g_bms_overview = lv_obj_create(NULL);
    overview_build(g_bms_overview);

    g_current_screen = SCREEN_OVERVIEW;
    lv_scr_load(g_bms_overview);
}