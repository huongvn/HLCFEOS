/**
 * @file system_monitor.h
 * @brief System Monitor page - displays CPU, RAM, IP, and temperature
 */

#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==================
 * INITIALIZATION
 *==================*/

/**
 * Initialize System Monitor page
 * @param parent pointer to parent screen
 * @return pointer to created page object
 */
lv_obj_t* system_monitor_create(lv_obj_t *parent);

/**
 * Start System Monitor update timer
 */
void system_monitor_start_timer(void);

/**
 * Stop System Monitor update timer
 */
void system_monitor_stop_timer(void);

/*==================
 * NAVIGATION
 *==================*/

/**
 * Show System Monitor page, hide other pages
 */
void system_monitor_show(void);

/**
 * Hide System Monitor page
 */
void system_monitor_hide(void);

/*==================
 * CALLBACKS
 *==================*/

/**
 * Set callback for navigating to Settings
 * @param cb callback function
 */
void system_monitor_set_settings_nav_cb(void (*cb)(void));

/**
 * Set callback for navigating to BMS Dashboard
 * @param cb callback function
 */
void system_monitor_set_bms_nav_cb(void (*cb)(void));

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* SYSTEM_MONITOR_H */
