#ifndef SETTINGS_H
#define SETTINGS_H

#include "lvgl.h"

/**
 * Initialize settings page
 * @param parent pointer to parent screen
 * @return pointer to created page object
 */
lv_obj_t* settings_create(lv_obj_t *parent);

/**
 * Show settings page
 */
void settings_show(void);

/**
 * Hide settings page
 */
void settings_hide(void);

/**
 * Set callback for navigating back
 */
void settings_set_back_nav_cb(void (*cb)(void));

#endif // SETTINGS_H
