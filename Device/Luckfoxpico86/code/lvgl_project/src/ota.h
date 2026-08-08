#ifndef OTA_H
#define OTA_H

#include <lvgl/lvgl.h>
#include <stdbool.h>

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_AVAILABLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_READY_TO_INSTALL,
    OTA_STATE_INSTALLING,
    OTA_STATE_FAILED,
    OTA_STATE_SUCCESS
} ota_state_t;

typedef struct {
    ota_state_t state;
    int progress;           // 0-100
    char message[128];      // Status message
    char new_version[32];   // New version available
} ota_status_t;

/**
 * Initialize OTA module
 */
void ota_init(void);

/**
 * Check for updates
 */
void ota_check_now(void);

/**
 * Start the download process
 */
void ota_start_download(void);

/**
 * Perform installation (Atomic swap)
 */
void ota_install_now(void);

/**
 * Get current OTA status
 */
ota_status_t* ota_get_status(void);

/**
 * Health check signal (Call this after successful boot)
 */
void ota_signal_success(void);

#endif // OTA_H
