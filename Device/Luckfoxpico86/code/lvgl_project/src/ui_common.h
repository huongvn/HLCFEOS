/**
 * @file ui_common.h
 * @brief Common utilities and shared definitions for UI modules
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==================
 * COLOR PALETTE
 *==================*/

// System Monitor colors
#define COLOR_BG_LIGHT      lv_color_hex(0x1A1A1A)
#define COLOR_WHITE         lv_color_hex(0xFFFFFF)
#define COLOR_CPU_GREEN     lv_color_hex(0x4CAF50)
#define COLOR_RAM_BLUE      lv_color_hex(0x2196F3)
#define COLOR_TEMP_GREEN    lv_color_hex(0x00AA00)
#define COLOR_TEMP_ORANGE   lv_color_hex(0xFFAA00)
#define COLOR_TEMP_RED      lv_color_hex(0xFF0000)

// Room colors (dark theme)
#define COLOR_ROOM_BG       lv_color_hex(0x1A1A1A)
#define COLOR_ROOM_DARK     lv_color_hex(0x2A2A2A)
#define COLOR_ROOM_GREEN    lv_color_hex(0x00CFCF)
#define COLOR_ROOM_LIGHT    lv_color_hex(0xAAAAAA)
#define COLOR_ROOM_ACCENT   lv_color_hex(0x00CFCF)
#define COLOR_ROOM_NAVY     lv_color_hex(0x111111)
#define COLOR_ROOM_DARKNAVY lv_color_hex(0x222222)

// Master Panel colors
#define COLOR_PANEL_GREEN   lv_color_hex(0x4CAF50)
#define COLOR_PANEL_ORANGE  lv_color_hex(0xFF5722)
#define COLOR_PANEL_GRAY    lv_color_hex(0x757575)

// Smart Light colors (matching Smart Room style)
#define COLOR_LIGHT_BG       lv_color_hex(0x083731)  /* Changed from 0x0D0D1A to match Smart Room */
#define COLOR_LIGHT_BTN_OFF  lv_color_hex(0x1E1E2E)
#define COLOR_LIGHT_BTN_ON   lv_color_hex(0xFF6B00)  /* Changed from 0xFFC107 (yellow) to orange */
#define COLOR_CURTAIN_OPEN   lv_color_hex(0x2196F3)
#define COLOR_CURTAIN_MOVING lv_color_hex(0xFF9800)
#define COLOR_ALL_OFF_BG     lv_color_hex(0x3D0000)
#define COLOR_ALL_OFF_ICON   lv_color_hex(0xFF5252)
#define COLOR_LIGHT_BORDER   lv_color_hex(0x2A2A3E)
#define COLOR_TEXT_DIM       lv_color_hex(0x888888)
#define COLOR_TEXT_FOOTER    lv_color_hex(0xAAAAAA)

/*==================
 * TYPEDEFS
 *==================*/

/** Navigation callback type */
typedef void (*nav_callback_t)(void);


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* UI_COMMON_H */
