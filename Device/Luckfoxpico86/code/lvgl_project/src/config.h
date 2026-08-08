#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @struct app_config_t
 * @brief Global configuration structure
 */
typedef struct {
    char room_id[32];
    char pincode[8];
    int screen_brightness; // 0-255
    int screen_timeout;    // seconds, 0 = never
    int font_choice;       // 0 = English (Montserrat), 1 = Vietnamese (Roboto)
    char ota_url[128];     // OTA Update URL
    char github_repo[96];  // GitHub "owner/repo" for Releases-based OTA
    char ota_github_token[80]; // Optional GitHub token (PAT) to avoid rate limits
    int ota_check_interval;    // Seconds between OTA checks (0 = disabled auto-check)
    bool ota_enabled;          // Master switch for OTA
    char wifi_ssid[64];    // WiFi SSID
    char wifi_password[64];// WiFi Password
    
    // Smart Room Persistence
    int room_temp;         // 16-32
    int room_mode;         // 0=Cool, 1=Dry, 2=Heat
    int room_fan;          // 0-100
    bool room_eco;
    bool room_power;
    bool room_dnd;
    bool room_mmr;

    // Smart Light Persistence
    int light_brightness[4];  // 0-100 for each light
    bool light_state[4];      // ON/OFF for each light
    int curtain_state[2];     // 0=Closed, 1=Opening, 2=Open, 3=Closing
} app_config_t;

/**
 * Get the global configuration instance
 */
app_config_t* config_get(void);

/**
 * Load configuration from file
 * If file not found, use default values
 */
void config_load(void);

/**
 * Save current configuration to file
 */
void config_save(void);

/**
 * Update a specific string config
 */
void config_set_room_id(const char* room_id);
void config_set_pincode(const char* pin);

/**
 * Apply current brightness to hardware (/sys/class/backlight)
 */
void config_apply_brightness(void);

#endif // CONFIG_H
