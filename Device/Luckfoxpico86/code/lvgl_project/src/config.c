#include "src/config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#define CONFIG_FILE "app_config.txt"

static app_config_t g_config = {
    .room_id = "Room_1422",
    .pincode = "1234",
    .screen_brightness = 200,
    .screen_timeout = 60,
    .font_choice = 0,
    .ota_url = "http://192.168.1.171/ota/check.json",
    .github_repo = "huongvn/HLCFEOS",
    .ota_github_token = "",
    .ota_check_interval = 0,
    .ota_enabled = false,
    .wifi_ssid = "",
    .wifi_password = "",
    .room_temp = 22,
    .room_mode = 0, // Cool
    .room_fan = 30,
    .room_eco = false,
    .room_power = true,
    .room_dnd = false,
    .room_mmr = false,
    .light_brightness = {100, 100, 100, 100},
    .light_state = {false, false, false, false},
    .curtain_state = {0, 0}  // 0=Closed
};

app_config_t* config_get(void) {
    return &g_config;
}

void config_load(void) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        printf("[CONFIG] No config file found, using defaults\n");
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%[^=]=%s", key, val) == 2) {
            if (strcmp(key, "room_id") == 0) strncpy(g_config.room_id, val, sizeof(g_config.room_id)-1);
            else if (strcmp(key, "pincode") == 0) strncpy(g_config.pincode, val, sizeof(g_config.pincode)-1);
            else if (strcmp(key, "brightness") == 0) g_config.screen_brightness = atoi(val);
            else if (strcmp(key, "timeout") == 0) g_config.screen_timeout = atoi(val);
            else if (strcmp(key, "font_choice") == 0) g_config.font_choice = atoi(val);
            else if (strcmp(key, "ota_url") == 0) strncpy(g_config.ota_url, val, sizeof(g_config.ota_url)-1);
            else if (strcmp(key, "github_repo") == 0) strncpy(g_config.github_repo, val, sizeof(g_config.github_repo)-1);
            else if (strcmp(key, "ota_github_token") == 0) strncpy(g_config.ota_github_token, val, sizeof(g_config.ota_github_token)-1);
            else if (strcmp(key, "ota_check_interval") == 0) g_config.ota_check_interval = atoi(val);
            else if (strcmp(key, "ota_enabled") == 0) g_config.ota_enabled = (atoi(val) != 0);
            else if (strcmp(key, "wifi_ssid") == 0) strncpy(g_config.wifi_ssid, val, sizeof(g_config.wifi_ssid)-1);
            else if (strcmp(key, "wifi_password") == 0) strncpy(g_config.wifi_password, val, sizeof(g_config.wifi_password)-1);
            else if (strcmp(key, "room_temp") == 0) g_config.room_temp = atoi(val);
            else if (strcmp(key, "room_mode") == 0) g_config.room_mode = atoi(val);
            else if (strcmp(key, "room_fan") == 0) g_config.room_fan = atoi(val);
            else if (strcmp(key, "room_eco") == 0) g_config.room_eco = (atoi(val) != 0);
            else if (strcmp(key, "room_power") == 0) g_config.room_power = (atoi(val) != 0);
            else if (strcmp(key, "room_dnd") == 0) g_config.room_dnd = (atoi(val) != 0);
            else if (strcmp(key, "room_mmr") == 0) g_config.room_mmr = (atoi(val) != 0);
            else if (strcmp(key, "light_b0") == 0) g_config.light_brightness[0] = atoi(val);
            else if (strcmp(key, "light_b1") == 0) g_config.light_brightness[1] = atoi(val);
            else if (strcmp(key, "light_b2") == 0) g_config.light_brightness[2] = atoi(val);
            else if (strcmp(key, "light_b3") == 0) g_config.light_brightness[3] = atoi(val);
            else if (strcmp(key, "light_s0") == 0) g_config.light_state[0] = (atoi(val) != 0);
            else if (strcmp(key, "light_s1") == 0) g_config.light_state[1] = (atoi(val) != 0);
            else if (strcmp(key, "light_s2") == 0) g_config.light_state[2] = (atoi(val) != 0);
            else if (strcmp(key, "light_s3") == 0) g_config.light_state[3] = (atoi(val) != 0);
            else if (strcmp(key, "curtain_0") == 0) g_config.curtain_state[0] = atoi(val);
            else if (strcmp(key, "curtain_1") == 0) g_config.curtain_state[1] = atoi(val);
        }
    }
    fclose(f);
    printf("[CONFIG] Loaded config from %s\n", CONFIG_FILE);
}

void config_save(void) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) {
        perror("[CONFIG] Failed to open config file for writing");
        return;
    }

    fprintf(f, "room_id=%s\n", g_config.room_id);
    fprintf(f, "pincode=%s\n", g_config.pincode);
    fprintf(f, "brightness=%d\n", g_config.screen_brightness);
    fprintf(f, "timeout=%d\n", g_config.screen_timeout);
    fprintf(f, "font_choice=%d\n", g_config.font_choice);
    fprintf(f, "ota_url=%s\n", g_config.ota_url);
    fprintf(f, "github_repo=%s\n", g_config.github_repo);
    fprintf(f, "ota_github_token=%s\n", g_config.ota_github_token);
    fprintf(f, "ota_check_interval=%d\n", g_config.ota_check_interval);
    fprintf(f, "ota_enabled=%d\n", g_config.ota_enabled ? 1 : 0);
    fprintf(f, "wifi_ssid=%s\n", g_config.wifi_ssid);
    fprintf(f, "wifi_password=%s\n", g_config.wifi_password);
    fprintf(f, "room_temp=%d\n", g_config.room_temp);
    fprintf(f, "room_mode=%d\n", g_config.room_mode);
    fprintf(f, "room_fan=%d\n", g_config.room_fan);
    fprintf(f, "room_eco=%d\n", g_config.room_eco ? 1 : 0);
    fprintf(f, "room_power=%d\n", g_config.room_power ? 1 : 0);
    fprintf(f, "room_dnd=%d\n", g_config.room_dnd ? 1 : 0);
    fprintf(f, "room_mmr=%d\n", g_config.room_mmr ? 1 : 0);
    fprintf(f, "light_b0=%d\n", g_config.light_brightness[0]);
    fprintf(f, "light_b1=%d\n", g_config.light_brightness[1]);
    fprintf(f, "light_b2=%d\n", g_config.light_brightness[2]);
    fprintf(f, "light_b3=%d\n", g_config.light_brightness[3]);
    fprintf(f, "light_s0=%d\n", g_config.light_state[0] ? 1 : 0);
    fprintf(f, "light_s1=%d\n", g_config.light_state[1] ? 1 : 0);
    fprintf(f, "light_s2=%d\n", g_config.light_state[2] ? 1 : 0);
    fprintf(f, "light_s3=%d\n", g_config.light_state[3] ? 1 : 0);
    fprintf(f, "curtain_0=%d\n", g_config.curtain_state[0]);
    fprintf(f, "curtain_1=%d\n", g_config.curtain_state[1]);

    fclose(f);
    printf("[CONFIG] Saved config to %s\n", CONFIG_FILE);
}

void config_set_room_id(const char* room_id) {
    if(room_id) strncpy(g_config.room_id, room_id, sizeof(g_config.room_id)-1);
}

void config_set_pincode(const char* pin) {
    if(pin) strncpy(g_config.pincode, pin, sizeof(g_config.pincode)-1);
}

void config_apply_brightness(void) {
    int fd = open("/sys/class/backlight/backlight/brightness", O_WRONLY);
    if(fd >= 0) {
        char buf[16];
        int len = sprintf(buf, "%d", g_config.screen_brightness);
        (void)write(fd, buf, len);
        close(fd);
        printf("[CONFIG] Applied brightness: %d\n", g_config.screen_brightness);
    } else {
        // Fallback for different hardware paths if needed
        fd = open("/sys/class/backlight/pwm-backlight.0/brightness", O_WRONLY);
        if(fd >= 0) {
            char buf[16];
            int len = sprintf(buf, "%d", g_config.screen_brightness);
            (void)write(fd, buf, len);
            close(fd);
            printf("[CONFIG] Applied brightness (alt path): %d\n", g_config.screen_brightness);
        } else {
            perror("[CONFIG] Failed to open brightness file");
        }
    }
}

