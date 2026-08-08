/**
 * @file ui.c
 * @brief Main UI initialization and navigation management
 */

#include "lvgl.h"
#include "src/ui.h"
#include "src/bms.h"
#include <stdio.h>

/*==================
 * NAVIGATION CALLBACKS
 *==================*/

static void navigate_to_settings(void)
{
    system_monitor_hide();
    settings_show();
    printf("[UI] Navigate to Settings\n");
}

static void navigate_back_from_settings(void)
{
    settings_hide();
    system_monitor_show();
    printf("[UI] Navigate back from Settings to System Monitor\n");
}

static void handle_settings_request(void)
{
    settings_auth_show(navigate_to_settings);
}

static void navigate_to_bms(void)
{
    printf("[UI] Navigate to BMS Dashboard\n");
    bms_init();
}

/*==================
 * PUBLIC API
 *==================*/

void ui_init(lv_obj_t *parent)
{
    printf("[UI] Initializing UI pages...\n");

    system_monitor_create(parent);
    settings_create(parent);

    system_monitor_set_settings_nav_cb(handle_settings_request);
    system_monitor_set_bms_nav_cb(navigate_to_bms);
    settings_set_back_nav_cb(navigate_back_from_settings);

    system_monitor_show();

    printf("[UI] UI initialization complete\n");
}

void ui_start_timers(void)
{
    system_monitor_start_timer();
}

void ui_stop_timers(void)
{
    system_monitor_stop_timer();
}

void ui_show_system_monitor(void)
{
    system_monitor_show();
}
