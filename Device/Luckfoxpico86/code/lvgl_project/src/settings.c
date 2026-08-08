#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "src/settings.h"
#include "src/config.h"
#include "src/ui_common.h"
#include "src/ui_helpers.h"
#include "src/ota.h"

static lv_obj_t *settings_page = NULL;
static lv_obj_t *tabview = NULL;
static lv_obj_t *kb = NULL;

// Text areas for settings
static lv_obj_t *ta_room_id;
static lv_obj_t *ta_pincode;
static lv_obj_t *ta_timeout;
static lv_obj_t *slider_brightness;
static lv_obj_t *dropdown_font;
static lv_obj_t *label_ota_ver;
static lv_obj_t *label_ota_status;
static lv_obj_t *bar_ota_progress;
static lv_obj_t *btn_ota_action;
static lv_obj_t *label_ota_btn;
static lv_obj_t *ta_ota_url;
static lv_obj_t *ta_wifi_ssid;
static lv_obj_t *ta_wifi_password;
static lv_obj_t *label_wifi_status;
static lv_timer_t *ota_timer = NULL;

static void (*back_cb)(void) = NULL;

static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    } else if(code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void brightness_slider_event_cb(lv_event_t *e) {
    app_config_t *cfg = config_get();
    cfg->screen_brightness = lv_slider_get_value(slider_brightness);
    config_apply_brightness();
}


static void reboot_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        printf("[SETTINGS] Rebooting system...\n");
        sync();
        system("/sbin/reboot");
        while(1) { sleep(1); }
    }
}

static void save_btn_event_cb(lv_event_t *e) {
    app_config_t *cfg = config_get();
    
    strncpy(cfg->room_id, lv_textarea_get_text(ta_room_id), sizeof(cfg->room_id)-1);
    strncpy(cfg->pincode, lv_textarea_get_text(ta_pincode), sizeof(cfg->pincode)-1);
    cfg->screen_brightness = lv_slider_get_value(slider_brightness);
    cfg->screen_timeout = atoi(lv_textarea_get_text(ta_timeout));
    cfg->font_choice = lv_dropdown_get_selected(dropdown_font);
    strncpy(cfg->ota_url, lv_textarea_get_text(ta_ota_url), sizeof(cfg->ota_url)-1);
    strncpy(cfg->wifi_ssid, lv_textarea_get_text(ta_wifi_ssid), sizeof(cfg->wifi_ssid)-1);
    strncpy(cfg->wifi_password, lv_textarea_get_text(ta_wifi_password), sizeof(cfg->wifi_password)-1);

    config_save();
    config_apply_brightness();
    
    if(back_cb) back_cb();
}

static void back_btn_event_cb(lv_event_t *e) {
    if(back_cb) back_cb();
}

static void wifi_connect_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    const char *ssid = lv_textarea_get_text(ta_wifi_ssid);
    const char *pass = lv_textarea_get_text(ta_wifi_password);

    if (strlen(ssid) == 0) {
        lv_label_set_text(label_wifi_status, "Error: SSID is empty");
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xFF5252), 0);
        return;
    }

    char status_buf[128];
    snprintf(status_buf, sizeof(status_buf), "Connecting to %s...", ssid);
    lv_label_set_text(label_wifi_status, status_buf);
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xAAAAAA), 0);
    lv_timer_handler(); // flush UI before blocking call

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "sudo nmcli dev wifi connect \"%s\" password \"%s\"",
             ssid, pass);

    printf("[SETTINGS] WiFi connect: %s\n", cmd);
    int ret = system(cmd);

    if (ret != 0) {
        snprintf(status_buf, sizeof(status_buf), "Connection failed");
        lv_label_set_text(label_wifi_status, status_buf);
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xFF5252), 0);
        return;
    }

    // Verify connection is real
    snprintf(cmd, sizeof(cmd),
             "nmcli -t -f ACTIVE,SSID dev wifi | grep '^yes:%s$'", ssid);
    ret = system(cmd);

    if (ret != 0) {
        lv_label_set_text(label_wifi_status, "Connected but verification failed");
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xFFA726), 0);
        return;
    }

    lv_label_set_text(label_wifi_status, "WiFi connected successfully!");
    lv_obj_set_style_text_color(label_wifi_status, COLOR_ROOM_GREEN, 0);

    app_config_t *cfg = config_get();
    strncpy(cfg->wifi_ssid, ssid, sizeof(cfg->wifi_ssid) - 1);
    strncpy(cfg->wifi_password, pass, sizeof(cfg->wifi_password) - 1);
    config_save();
}

static void ota_btn_event_cb(lv_event_t *e) {
    ota_status_t *status = ota_get_status();
    if (status->state == OTA_STATE_IDLE || status->state == OTA_STATE_FAILED) {
        ota_check_now();
    } else if (status->state == OTA_STATE_AVAILABLE) {
        ota_start_download();
    } else if (status->state == OTA_STATE_READY_TO_INSTALL) {
        ota_install_now();
    }
}

static void ota_timer_cb(lv_timer_t *t) {
    ota_status_t *status = ota_get_status();
    
    // Update status label
    lv_label_set_text(label_ota_status, ui_get_text(status->message));
    
    // Update progress bar
    if (status->state == OTA_STATE_DOWNLOADING) {
        lv_obj_remove_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(bar_ota_progress, status->progress, LV_ANIM_ON);
    } else {
        lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update button text and state
    switch (status->state) {
        case OTA_STATE_IDLE:
        case OTA_STATE_FAILED:
            lv_label_set_text(label_ota_btn, ui_get_text("Check for Update"));
            lv_obj_remove_state(btn_ota_action, LV_STATE_DISABLED);
            break;
        case OTA_STATE_CHECKING:
            lv_label_set_text(label_ota_btn, ui_get_text("Checking..."));
            lv_obj_add_state(btn_ota_action, LV_STATE_DISABLED);
            break;
        case OTA_STATE_AVAILABLE:
            lv_label_set_text(label_ota_btn, ui_get_text("Download & Install"));
            lv_obj_remove_state(btn_ota_action, LV_STATE_DISABLED);
            break;
        case OTA_STATE_DOWNLOADING:
            lv_label_set_text(label_ota_btn, ui_get_text("Downloading..."));
            lv_obj_add_state(btn_ota_action, LV_STATE_DISABLED);
            break;
        case OTA_STATE_READY_TO_INSTALL:
            lv_label_set_text(label_ota_btn, ui_get_text("Install Now"));
            lv_obj_remove_state(btn_ota_action, LV_STATE_DISABLED);
            break;
        case OTA_STATE_INSTALLING:
            lv_label_set_text(label_ota_btn, ui_get_text("Installing..."));
            lv_obj_add_state(btn_ota_action, LV_STATE_DISABLED);
            break;
        case OTA_STATE_SUCCESS:
            lv_label_set_text(label_ota_btn, ui_get_text("Rebooting..."));
            lv_obj_add_state(btn_ota_action, LV_STATE_DISABLED);
            break;
        default:
            break;
    }
}

/* Helper: create a styled label */
static lv_obj_t* create_section_label(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, ui_get_font(16), 0);
    lv_obj_set_style_text_color(lbl, COLOR_WHITE, 0);
    lv_obj_set_style_margin_top(lbl, 10, 0);
    return lbl;
}

/* Helper: create a styled text area */
static lv_obj_t* create_styled_ta(lv_obj_t *parent, const char *text, int width_pct) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_text(ta, text);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, LV_PCT(width_pct));
    lv_obj_set_style_bg_color(ta, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_text_color(ta, COLOR_WHITE, 0);
    lv_obj_set_style_border_color(ta, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_text_font(ta, ui_get_font(16), 0);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
    return ta;
}

/* Helper: style a tab content area */
static void style_tab(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 15, 0);
    lv_obj_set_style_pad_row(tab, 5, 0);
    lv_obj_set_style_bg_color(tab, COLOR_ROOM_BG, 0);
}

lv_obj_t* settings_create(lv_obj_t *parent) {
    settings_page = lv_obj_create(parent);
    lv_obj_set_size(settings_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(settings_page, COLOR_ROOM_BG, 0);
    lv_obj_set_style_border_width(settings_page, 0, 0);
    lv_obj_set_style_radius(settings_page, 0, 0);
    lv_obj_set_style_pad_all(settings_page, 0, 0);
    lv_obj_add_flag(settings_page, LV_OBJ_FLAG_HIDDEN);

    // Header
    lv_obj_t *header = lv_obj_create(settings_page);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 10, 0);

    lv_obj_t *title = lv_label_create(header);
    char t_buf[64]; snprintf(t_buf, sizeof(t_buf), "%s  %s", LV_SYMBOL_SETTINGS, ui_get_text("Settings")); lv_label_set_text(title, t_buf);
    lv_obj_set_style_text_font(title, ui_get_font(22), 0);
    lv_obj_set_style_text_color(title, COLOR_WHITE, 0);
    lv_obj_center(title);

    // TabView (below header, above footer)
    tabview = lv_tabview_create(settings_page);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_LEFT);
    lv_tabview_set_tab_bar_size(tabview, 130);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(tabview, COLOR_ROOM_BG, 0);
    lv_obj_set_style_text_color(tabview, COLOR_WHITE, 0);
    lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_height(tabview, lv_pct(100));
    lv_obj_update_layout(settings_page);
    lv_obj_set_height(tabview, lv_obj_get_height(settings_page) - 120); // header + footer

    // Tab bar styling
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_text_font(tab_bar, ui_get_font(16), 0);
    lv_obj_set_style_text_color(tab_bar, COLOR_ROOM_LIGHT, 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);

    lv_obj_t *t1 = lv_tabview_add_tab(tabview, ui_get_text("Network"));
    lv_obj_t *t2 = lv_tabview_add_tab(tabview, ui_get_text("Display"));
    lv_obj_t *t3 = lv_tabview_add_tab(tabview, ui_get_text("System"));
    lv_obj_t *t4 = lv_tabview_add_tab(tabview, ui_get_text("Update"));

    app_config_t *cfg = config_get();

    // --- Tab 1: Network ---
    style_tab(t1);

    create_section_label(t1, ui_get_text("Room ID / Client ID"));
    ta_room_id = create_styled_ta(t1, cfg->room_id, 90);

    create_section_label(t1, ui_get_text("WiFi SSID"));
    ta_wifi_ssid = create_styled_ta(t1, cfg->wifi_ssid, 90);

    create_section_label(t1, ui_get_text("WiFi Password"));
    ta_wifi_password = create_styled_ta(t1, cfg->wifi_password, 90);
    lv_textarea_set_password_mode(ta_wifi_password, true);

    lv_obj_t *btn_wifi_connect = lv_button_create(t1);
    lv_obj_set_size(btn_wifi_connect, 160, 40);
    lv_obj_set_style_bg_color(btn_wifi_connect, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_radius(btn_wifi_connect, 8, 0);
    lv_obj_set_style_margin_top(btn_wifi_connect, 5, 0);
    lv_obj_t *l_wifi = lv_label_create(btn_wifi_connect);
    lv_label_set_text(l_wifi, ui_get_text("Connect WiFi"));
    lv_obj_set_style_text_font(l_wifi, ui_get_font(16), 0);
    lv_obj_center(l_wifi);
    lv_obj_add_event_cb(btn_wifi_connect, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);

    label_wifi_status = lv_label_create(t1);
    lv_label_set_text(label_wifi_status, "");
    lv_obj_set_style_text_font(label_wifi_status, ui_get_font(14), 0);
    lv_obj_set_style_margin_top(label_wifi_status, 2, 0);

    // --- Tab 2: Display ---
    style_tab(t2);

    create_section_label(t2, ui_get_text("Language / Font"));
    dropdown_font = lv_dropdown_create(t2);
    lv_dropdown_set_options(dropdown_font, "English (Montserrat)\nTiếng Việt (Roboto)");
    lv_dropdown_set_selected(dropdown_font, cfg->font_choice);
    lv_obj_set_width(dropdown_font, LV_PCT(90));
    lv_obj_set_style_bg_color(dropdown_font, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_text_color(dropdown_font, COLOR_WHITE, 0);
    lv_obj_set_style_border_color(dropdown_font, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_border_width(dropdown_font, 1, 0);
    lv_obj_set_style_radius(dropdown_font, 8, 0);
    lv_obj_set_style_text_font(dropdown_font, ui_get_font(16), 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dropdown_font), ui_get_font(16), 0);
    lv_obj_set_style_bg_color(lv_dropdown_get_list(dropdown_font), COLOR_ROOM_DARK, 0);
    lv_obj_set_style_text_color(lv_dropdown_get_list(dropdown_font), COLOR_WHITE, 0);

    create_section_label(t2, ui_get_text("Brightness"));
    slider_brightness = lv_slider_create(t2);
    lv_slider_set_range(slider_brightness, 10, 255);
    lv_slider_set_value(slider_brightness, cfg->screen_brightness, LV_ANIM_OFF);
    lv_obj_set_width(slider_brightness, LV_PCT(90));
    lv_obj_set_style_bg_color(slider_brightness, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_bg_color(slider_brightness, COLOR_ROOM_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_brightness, COLOR_WHITE, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_brightness, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_section_label(t2, ui_get_text("Update PIN Code"));
    ta_pincode = create_styled_ta(t2, cfg->pincode, 40);
    lv_textarea_set_max_length(ta_pincode, 6);

    create_section_label(t2, ui_get_text("Screen Timeout (seconds, 0 = never)"));
    char timeout_buf[16];
    sprintf(timeout_buf, "%d", cfg->screen_timeout);
    ta_timeout = create_styled_ta(t2, timeout_buf, 40);

    // --- Tab 3: System ---
    style_tab(t3);

    lv_obj_t *l_ver = lv_label_create(t3);
    char sys_ver_buf[128];
    snprintf(sys_ver_buf, sizeof(sys_ver_buf), "Firmware: v%s\nHardware: Luckfox Pico Pro\nOS: Buildroot Linux", APP_VERSION);
    lv_label_set_text(l_ver, sys_ver_buf);
    lv_obj_set_style_text_color(l_ver, COLOR_ROOM_LIGHT, 0);
    lv_obj_set_style_text_font(l_ver, ui_get_font(16), 0);
    lv_obj_set_style_margin_top(l_ver, 10, 0);

    lv_obj_t *reboot_btn = lv_button_create(t3);
    lv_obj_set_size(reboot_btn, 200, 50);
    lv_obj_set_style_bg_color(reboot_btn, lv_color_hex(0x8B0000), 0);
    lv_obj_set_style_radius(reboot_btn, 8, 0);
    lv_obj_set_style_margin_top(reboot_btn, 20, 0);
    lv_obj_t *reboot_label = lv_label_create(reboot_btn);
    char btn_buf[64]; snprintf(btn_buf, sizeof(btn_buf), "%s  %s", LV_SYMBOL_POWER, ui_get_text("Reboot System")); lv_label_set_text(reboot_label, btn_buf);
    lv_obj_set_style_text_font(reboot_label, ui_get_font(16), 0);
    lv_obj_center(reboot_label);
    lv_obj_add_event_cb(reboot_btn, reboot_btn_event_cb, LV_EVENT_CLICKED, NULL);

    style_tab(t4);
    
    create_section_label(t4, ui_get_text("OTA Update Server URL"));
    ta_ota_url = create_styled_ta(t4, cfg->ota_url, 90);

    label_ota_ver = lv_label_create(t4);
    char ver_buf[64]; snprintf(ver_buf, sizeof(ver_buf), ui_get_text("Current Version: %s"), APP_VERSION);
    lv_label_set_text(label_ota_ver, ver_buf);
    lv_obj_set_style_text_font(label_ota_ver, ui_get_font(18), 0);
    lv_obj_set_style_text_color(label_ota_ver, COLOR_WHITE, 0);

    label_ota_status = lv_label_create(t4);
    lv_label_set_text(label_ota_status, "");
    lv_obj_set_style_text_font(label_ota_status, ui_get_font(14), 0);
    lv_obj_set_style_text_color(label_ota_status, COLOR_ROOM_LIGHT, 0);

    bar_ota_progress = lv_bar_create(t4);
    lv_obj_set_size(bar_ota_progress, LV_PCT(90), 20);
    lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(bar_ota_progress, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_bg_color(bar_ota_progress, COLOR_ROOM_GREEN, LV_PART_INDICATOR);

    btn_ota_action = lv_button_create(t4);
    lv_obj_set_size(btn_ota_action, 220, 50);
    lv_obj_set_style_bg_color(btn_ota_action, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_radius(btn_ota_action, 8, 0);
    lv_obj_set_style_margin_top(btn_ota_action, 20, 0);
    lv_obj_add_event_cb(btn_ota_action, ota_btn_event_cb, LV_EVENT_CLICKED, NULL);

    label_ota_btn = lv_label_create(btn_ota_action);
    lv_label_set_text(label_ota_btn, ui_get_text("Check for Update"));
    lv_obj_set_style_text_font(label_ota_btn, ui_get_font(16), 0);
    lv_obj_center(label_ota_btn);

    ota_timer = lv_timer_create(ota_timer_cb, 500, NULL);

    // Footer Buttons (Save / Back)
    lv_obj_t *footer = lv_obj_create(settings_page);
    lv_obj_set_size(footer, LV_PCT(100), 60);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(footer, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 10, 0);

    lv_obj_t *btn_save = lv_button_create(footer);
    lv_obj_set_size(btn_save, 160, 40);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_save, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_radius(btn_save, 8, 0);
    lv_obj_t *l_save = lv_label_create(btn_save);
    char save_buf[64]; snprintf(save_buf, sizeof(save_buf), "%s  %s", LV_SYMBOL_SAVE, ui_get_text("Apply & Save")); lv_label_set_text(l_save, save_buf);
    lv_obj_set_style_text_font(l_save, ui_get_font(16), 0);
    lv_obj_center(l_save);
    lv_obj_add_event_cb(btn_save, save_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_back = lv_button_create(footer);
    lv_obj_set_size(btn_back, 120, 40);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(btn_back, 8, 0);
    lv_obj_t *l_back = lv_label_create(btn_back);
    char back_buf[64]; snprintf(back_buf, sizeof(back_buf), "%s  %s", LV_SYMBOL_LEFT, ui_get_text("Back")); lv_label_set_text(l_back, back_buf);
    lv_obj_set_style_text_font(l_back, ui_get_font(16), 0);
    lv_obj_center(l_back);
    lv_obj_add_event_cb(btn_back, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    // Keyboard (styled dark)
    kb = lv_keyboard_create(settings_page);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(kb, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x333333), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COLOR_WHITE, LV_PART_ITEMS);

    return settings_page;
}

void settings_show(void) {
    if(settings_page) lv_obj_remove_flag(settings_page, LV_OBJ_FLAG_HIDDEN);
}

void settings_hide(void) {
    if(settings_page) lv_obj_add_flag(settings_page, LV_OBJ_FLAG_HIDDEN);
}

void settings_set_back_nav_cb(void (*cb)(void)) {
    back_cb = cb;
}
