#ifndef SETTINGS_AUTH_H
#define SETTINGS_AUTH_H

#include "lvgl.h"

/**
 * Show the PIN entry popup
 * @param on_success_cb Callback function when PIN is correct
 */
void settings_auth_show(void (*on_success_cb)(void));

#endif // SETTINGS_AUTH_H
