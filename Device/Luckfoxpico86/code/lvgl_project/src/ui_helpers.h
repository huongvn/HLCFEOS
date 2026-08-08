#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "lvgl/lvgl.h"

/**
 * Get the appropriate LVGL font based on the configured font choice.
 * size: The pixel size of the font (14, 16, 18, 20, 22, 24, 28, 32, 48)
 * Returns a pointer to the font struct or default if size not exist.
 */
const lv_font_t* ui_get_font(int size);

const char* ui_get_text(const char* eng);

#endif // UI_HELPERS_H
