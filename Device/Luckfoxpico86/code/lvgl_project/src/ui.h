/**
 * @file ui.h
 * @brief Main UI header - includes all UI modules
 */

#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"
#include "system_monitor.h"
#include "settings.h"
#include "settings_auth.h"
#include "config.h"

void ui_init(lv_obj_t *parent);
void ui_start_timers(void);
void ui_stop_timers(void);
void ui_show_system_monitor(void);

#ifdef __cplusplus
}
#endif

#endif
