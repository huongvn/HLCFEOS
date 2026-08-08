#include "src/settings_auth.h"
#include "src/config.h"
#include "src/ui_common.h"
#include <string.h>
#include <stdio.h>
#include "src/ui_helpers.h"

static lv_obj_t *auth_modal = NULL;
static lv_obj_t *pin_ta = NULL;
static void (*on_auth_success)(void) = NULL;

static void pin_event_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(obj);
    if(id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    const char *txt = lv_buttonmatrix_get_button_text(obj, id);
    if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(pin_ta);
    } else if(strcmp(txt, LV_SYMBOL_OK) == 0) {
        const char *entered_pin = lv_textarea_get_text(pin_ta);
        if(strcmp(entered_pin, config_get()->pincode) == 0) {
            lv_obj_delete(auth_modal);
            auth_modal = NULL;
            if(on_auth_success) on_auth_success();
        } else {
            lv_textarea_set_text(pin_ta, "");
            lv_obj_set_style_border_color(pin_ta, lv_color_hex(0xFF4444), 0);
            lv_obj_set_style_border_width(pin_ta, 2, 0);
        }
    } else {
        lv_textarea_add_text(pin_ta, txt);
        lv_obj_set_style_border_width(pin_ta, 1, 0);
        lv_obj_set_style_border_color(pin_ta, COLOR_ROOM_GREEN, 0);
    }
}

static void close_cb(lv_event_t *e) {
    if(auth_modal) {
        lv_obj_delete(auth_modal);
        auth_modal = NULL;
    }
}

void settings_auth_show(void (*on_success_cb)(void)) {
    on_auth_success = on_success_cb;

    if(auth_modal) return;

    // Dark overlay
    auth_modal = lv_obj_create(lv_screen_active());
    lv_obj_set_size(auth_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(auth_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(auth_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(auth_modal, 0, 0);
    lv_obj_set_style_radius(auth_modal, 0, 0);
    lv_obj_clear_flag(auth_modal, LV_OBJ_FLAG_SCROLLABLE);

    // Dialog container - Smart Room themed
    lv_obj_t *cont = lv_obj_create(auth_modal);
    lv_obj_set_size(cont, 320, 460);
    lv_obj_center(cont);
    lv_obj_set_style_bg_color(cont, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_border_color(cont, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_border_width(cont, 2, 0);
    lv_obj_set_style_radius(cont, 15, 0);
    lv_obj_set_style_pad_all(cont, 15, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 10, 0);

    // Title
    lv_obj_t *title = lv_label_create(cont);
    char t_buf[64];
    snprintf(t_buf, sizeof(t_buf), "%s  %s", LV_SYMBOL_SETTINGS, ui_get_text("ENTER PIN"));
    lv_label_set_text(title, t_buf);
    lv_obj_set_style_text_font(title, ui_get_font(22), 0);
    lv_obj_set_style_text_color(title, COLOR_WHITE, 0);

    // PIN text area
    pin_ta = lv_textarea_create(cont);
    lv_textarea_set_password_mode(pin_ta, true);
    lv_textarea_set_one_line(pin_ta, true);
    lv_textarea_set_max_length(pin_ta, 6);
    lv_obj_set_width(pin_ta, 220);
    lv_obj_set_style_text_align(pin_ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(pin_ta, ui_get_font(28), 0);
    lv_obj_set_style_bg_color(pin_ta, lv_color_hex(0x0D1B2A), 0);
    lv_obj_set_style_text_color(pin_ta, COLOR_WHITE, 0);
    lv_obj_set_style_border_color(pin_ta, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_border_width(pin_ta, 1, 0);
    lv_obj_set_style_radius(pin_ta, 8, 0);

    // Numpad
    static const char * btnm_map[] = {"1", "2", "3", "\n",
                                     "4", "5", "6", "\n",
                                     "7", "8", "9", "\n",
                                     LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};

    lv_obj_t * btnm = lv_buttonmatrix_create(cont);
    lv_obj_set_size(btnm, 280, 260);
    lv_buttonmatrix_set_map(btnm, btnm_map);
    lv_obj_add_event_cb(btnm, pin_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnm, 0, 0);
    lv_obj_set_style_text_font(btnm, ui_get_font(22), 0);
    
    // Style numpad buttons - match Smart Room
    lv_obj_set_style_bg_color(btnm, COLOR_ROOM_GREEN, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_80, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, COLOR_WHITE, LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm, 10, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS);

    // Cancel button
    lv_obj_t *back_btn = lv_button_create(cont);
    lv_obj_set_size(back_btn, 140, 40);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    char b_buf[64];
    snprintf(b_buf, sizeof(b_buf), "%s  %s", LV_SYMBOL_CLOSE, ui_get_text("Cancel"));
    lv_label_set_text(back_label, b_buf);
    lv_obj_set_style_text_font(back_label, ui_get_font(16), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, close_cb, LV_EVENT_CLICKED, NULL);
}
