/**
 * @file system_monitor.c
 * @brief System Monitor page implementation
 */

#include "lvgl.h"
#include "src/ui_common.h"
#include "src/system_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/ui_helpers.h"

/*==================
 * TYPES
 *==================*/

typedef struct {
    lv_obj_t *page;
    lv_obj_t *cpu_bar;
    lv_obj_t *cpu_label;
    lv_obj_t *ram_bar;
    lv_obj_t *ram_label;
    lv_obj_t *ip_label;
    lv_obj_t *temp_label;
    void (*settings_nav_cb)(void);
    void (*bms_nav_cb)(void);
    lv_timer_t *update_timer;
} system_monitor_t;

/*==================
 * STATICS
 *==================*/

static system_monitor_t g_sysmon = {0};

/*==================
 * HELPER FUNCTIONS
 *==================*/

static int get_cpu_usage(void)
{
    static unsigned long long lastTotalUser = 0, lastTotalUserLow = 0;
    static unsigned long long lastTotalSys = 0, lastTotalIdle = 0;
    static unsigned long long lastTotalIOWait = 0;
    static int initialized = 0;
    static int lastUsage = 0;

    unsigned long long totalUser, totalUserLow, totalSys, totalIdle, totalIOWait;
    FILE *fp = fopen("/proc/stat", "r");
    if(!fp) return lastUsage;

    int ret = fscanf(fp, "cpu %llu %llu %llu %llu %llu",
              &totalUser, &totalUserLow, &totalSys, &totalIdle, &totalIOWait);
    fclose(fp);

    if(ret != 5) return lastUsage;

    if(initialized) {
        unsigned long long totalUserDiff = totalUser - lastTotalUser;
        unsigned long long totalUserLowDiff = totalUserLow - lastTotalUserLow;
        unsigned long long totalSysDiff = totalSys - lastTotalSys;
        unsigned long long totalIdleDiff = totalIdle - lastTotalIdle;
        unsigned long long totalIOWaitDiff = totalIOWait - lastTotalIOWait;

        unsigned long long total = totalUserDiff + totalUserLowDiff + totalSysDiff + totalIOWaitDiff;
        unsigned long long totalAll = total + totalIdleDiff;

        int currentUsage = (totalAll > 0) ? (total * 100) / totalAll : 0;
        if(currentUsage > 100) currentUsage = 100;

        // Moving average: 70% new + 30% old
        lastUsage = (currentUsage * 7 + lastUsage * 3) / 10;

        lastTotalUser = totalUser;
        lastTotalUserLow = totalUserLow;
        lastTotalSys = totalSys;
        lastTotalIdle = totalIdle;
        lastTotalIOWait = totalIOWait;

        return lastUsage;
    }

    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;
    lastTotalIOWait = totalIOWait;
    initialized = 1;
    return 0;
}

static int get_ram_usage(void)
{
    unsigned long total = 0, free = 0, buffers = 0, cached = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if(!fp) {
        printf("[RAM] Failed to open /proc/meminfo\n");
        return 0;
    }

    char line[256];
    while(fgets(line, sizeof(line), fp)) {
        if(sscanf(line, "MemTotal: %lu", &total) == 1) continue;
        if(sscanf(line, "MemFree: %lu", &free) == 1) continue;
        if(sscanf(line, "Buffers: %lu", &buffers) == 1) continue;
        if(sscanf(line, "Cached: %lu", &cached) == 1) continue;
    }
    fclose(fp);

    if(total > 0) {
        unsigned long fb = free + buffers + cached;
        unsigned long used = (fb > total) ? 0 : (total - fb);
        int percent = (used * 100) / total;
        return (percent > 100) ? 100 : percent;
    }
    printf("[RAM] Total memory is 0\n");
    return 0;
}

static void get_ip_address(char *buf, size_t size)
{
    buf[0] = '\0';

    FILE *fp = popen("ip -o addr show 2>/dev/null | grep -E 'inet .* (eth|wlan|usb)' | "
                     "while read line; do "
                     "  iface=$(echo $line | awk '{print $2}'); "
                     "  ip=$(echo $line | awk '{print $4}' | cut -d/ -f1); "
                     "  if [ -f /sys/class/net/$iface/carrier ]; then "
                     "    carrier=$(cat /sys/class/net/$iface/carrier 2>/dev/null); "
                     "    if [ \"$carrier\" = \"1\" ]; then "
                     "      echo $ip; "
                     "    fi; "
                     "  fi; "
                     "done | tr '\\n' ' ' | sed 's/ $//'", "r");

    if(fp) {
        if(fgets(buf, size, fp)) {
            char *nl = strchr(buf, '\n');
            if(nl) *nl = '\0';
        }
        pclose(fp);
    }

    // Replace spaces with newlines if multiple IPs exist
    for (int i = 0; i < strlen(buf); i++) {
        if (buf[i] == ' ') {
            buf[i] = '\n';
        }
    }

    if(strlen(buf) == 0) {
        snprintf(buf, size, "No Connection");
    }
}

static int get_temperature(void)
{
    const char *zones[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/thermal/thermal_zone2/temp",
        NULL
    };

    for(int i = 0; zones[i] != NULL; i++) {
        FILE *fp = fopen(zones[i], "r");
        if(fp) {
            int temp;
            if(fscanf(fp, "%d", &temp) == 1) {
                fclose(fp);
                return temp / 1000;
            }
            fclose(fp);
        }
    }
    return 0;
}

/*==================
 * CALLBACKS
 *==================*/

static void update_timer_cb(lv_timer_t *timer)
{
    system_monitor_t *sysmon = (system_monitor_t *)lv_timer_get_user_data(timer);
    static int call_count = 0;

    // Skip updates when page is hidden to save resources
    if(sysmon->page && lv_obj_has_flag(sysmon->page, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    call_count++;

    // CPU
    int cpu = get_cpu_usage();
    lv_bar_set_value(sysmon->cpu_bar, cpu, LV_ANIM_OFF);
    char buf[32];
    snprintf(buf, sizeof(buf), "CPU: %d%%", cpu);
    lv_label_set_text(sysmon->cpu_label, buf);

    // RAM
    int ram = get_ram_usage();
    lv_bar_set_value(sysmon->ram_bar, ram, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "RAM: %d%%", ram);
    lv_label_set_text(sysmon->ram_label, buf);

    // Temperature
    int temp = get_temperature();
    snprintf(buf, sizeof(buf), ui_get_text("Temperature: %d°C"), temp);
    lv_label_set_text(sysmon->temp_label, buf);

    // Color based on temp
    if(temp < 50) {
        lv_obj_set_style_text_color(sysmon->temp_label, COLOR_TEMP_GREEN, 0);
    } else if(temp < 70) {
        lv_obj_set_style_text_color(sysmon->temp_label, COLOR_TEMP_ORANGE, 0);
    } else {
        lv_obj_set_style_text_color(sysmon->temp_label, COLOR_TEMP_RED, 0);
    }

    // Update IP every 2 seconds
    if(call_count % 20 == 0) {
        char ip_str[512];
        get_ip_address(ip_str, sizeof(ip_str));
        lv_label_set_text(sysmon->ip_label, ip_str);
        if(strcmp(ip_str, "No Connection") == 0) {
            lv_obj_set_style_text_color(sysmon->ip_label, COLOR_TEMP_RED, 0);
        } else {
            lv_obj_set_style_text_color(sysmon->ip_label, COLOR_RAM_BLUE, 0);
        }
    }
}

static void settings_btn_cb(lv_event_t *e)
{
    system_monitor_t *sysmon = (system_monitor_t *)lv_event_get_user_data(e);
    if(sysmon && sysmon->settings_nav_cb) {
        sysmon->settings_nav_cb();
    }
}

static void bms_btn_cb(lv_event_t *e)
{
    system_monitor_t *sysmon = (system_monitor_t *)lv_event_get_user_data(e);
    if(sysmon && sysmon->bms_nav_cb) {
        sysmon->bms_nav_cb();
    }
}

/*==================
 * PUBLIC API
 *==================*/

lv_obj_t* system_monitor_create(lv_obj_t *parent)
{
    // Create page
    g_sysmon.page = lv_obj_create(parent);
    lv_obj_set_size(g_sysmon.page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_sysmon.page, COLOR_ROOM_BG, 0); // Smart Room Background
    lv_obj_set_style_border_width(g_sysmon.page, 0, 0);
    lv_obj_set_style_pad_all(g_sysmon.page, 0, 0);

    // Background
    lv_obj_t *bg = lv_obj_create(g_sysmon.page);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bg, COLOR_ROOM_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);

    // Settings button (Gear)
    lv_obj_t *settings_btn = lv_btn_create(bg);
    lv_obj_set_size(settings_btn, 50, 50);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(settings_btn, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_border_color(settings_btn, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_border_width(settings_btn, 2, 0);
    lv_obj_set_style_radius(settings_btn, 8, 0);
    lv_obj_add_event_cb(settings_btn, settings_btn_cb, LV_EVENT_CLICKED, &g_sysmon);

    lv_obj_t *settings_label = lv_label_create(settings_btn);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(settings_label, COLOR_WHITE, 0);
    lv_obj_center(settings_label);

    // BMS Dashboard button
    lv_obj_t *bms_btn = lv_btn_create(bg);
    lv_obj_set_size(bms_btn, 100, 50);
    lv_obj_align_to(bms_btn, settings_btn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_bg_color(bms_btn, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_border_color(bms_btn, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_border_width(bms_btn, 2, 0);
    lv_obj_set_style_radius(bms_btn, 8, 0);
    lv_obj_add_event_cb(bms_btn, bms_btn_cb, LV_EVENT_CLICKED, &g_sysmon);

    lv_obj_t *bms_label = lv_label_create(bms_btn);
    lv_label_set_text(bms_label, "BMS");
    lv_obj_set_style_text_color(bms_label, COLOR_WHITE, 0);
    lv_obj_set_style_text_font(bms_label, ui_get_font(14), 0);
    lv_obj_center(bms_label);

    // Title - below buttons, centered
    lv_obj_t *title = lv_label_create(bg);
    lv_label_set_text(title, ui_get_text("System Monitor"));
    lv_obj_set_style_text_font(title, ui_get_font(24), 0);
    lv_obj_set_style_text_color(title, COLOR_WHITE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

    // Container for CPU and RAM
    lv_obj_t *container = lv_obj_create(bg);
    lv_obj_set_size(container, LV_PCT(80), LV_PCT(50));
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, COLOR_ROOM_DARK, 0); // Dark Green theme
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(container, 15, 0);
    lv_obj_set_style_pad_all(container, 20, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_EVENLY,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // CPU section
    lv_obj_t *cpu_title = lv_label_create(container);
    lv_label_set_text(cpu_title, ui_get_text("CPU Usage"));
    lv_obj_set_style_text_font(cpu_title, ui_get_font(18), 0);
    lv_obj_set_style_text_color(cpu_title, COLOR_WHITE, 0);

    g_sysmon.cpu_bar = lv_bar_create(container);
    lv_obj_set_size(g_sysmon.cpu_bar, LV_PCT(100), 30);
    lv_bar_set_range(g_sysmon.cpu_bar, 0, 100);
    lv_bar_set_value(g_sysmon.cpu_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_sysmon.cpu_bar, lv_color_hex(0x0D1B2A), 0); // Dark navy bg
    lv_obj_set_style_bg_color(g_sysmon.cpu_bar, COLOR_ROOM_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_sysmon.cpu_bar, 15, 0);

    g_sysmon.cpu_label = lv_label_create(container);
    lv_label_set_text(g_sysmon.cpu_label, "CPU: 0%");
    lv_obj_set_style_text_font(g_sysmon.cpu_label, ui_get_font(16), 0);
    lv_obj_set_style_text_color(g_sysmon.cpu_label, COLOR_ROOM_LIGHT, 0);

    // Separator
    lv_obj_t *line = lv_obj_create(container);
    lv_obj_set_size(line, LV_PCT(100), 2);
    lv_obj_set_style_bg_color(line, COLOR_ROOM_GREEN, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_50, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);

    // RAM section
    lv_obj_t *ram_title = lv_label_create(container);
    lv_label_set_text(ram_title, ui_get_text("RAM Usage"));
    lv_obj_set_style_text_font(ram_title, ui_get_font(18), 0);
    lv_obj_set_style_text_color(ram_title, COLOR_WHITE, 0);

    g_sysmon.ram_bar = lv_bar_create(container);
    lv_obj_set_size(g_sysmon.ram_bar, LV_PCT(100), 30);
    lv_bar_set_range(g_sysmon.ram_bar, 0, 100);
    lv_bar_set_value(g_sysmon.ram_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_sysmon.ram_bar, lv_color_hex(0x0D1B2A), 0);
    lv_obj_set_style_bg_color(g_sysmon.ram_bar, COLOR_ROOM_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_sysmon.ram_bar, 15, 0);

    g_sysmon.ram_label = lv_label_create(container);
    lv_label_set_text(g_sysmon.ram_label, "RAM: 0%");
    lv_obj_set_style_text_font(g_sysmon.ram_label, ui_get_font(16), 0);
    lv_obj_set_style_text_color(g_sysmon.ram_label, COLOR_ROOM_LIGHT, 0);

    // IP Address section
    lv_obj_t *ip_box = lv_obj_create(bg);
    lv_obj_set_size(ip_box, LV_PCT(80), 120);
    lv_obj_align(ip_box, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(ip_box, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_bg_opa(ip_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ip_box, 15, 0);
    lv_obj_set_style_pad_all(ip_box, 15, 0);
    lv_obj_set_style_border_width(ip_box, 0, 0);

    lv_obj_t *ip_title = lv_label_create(ip_box);
    lv_label_set_text(ip_title, ui_get_text("IP Address"));
    lv_obj_set_style_text_font(ip_title, ui_get_font(16), 0);
    lv_obj_set_style_text_color(ip_title, COLOR_WHITE, 0);
    lv_obj_align(ip_title, LV_ALIGN_TOP_MID, 0, 5);

    g_sysmon.ip_label = lv_label_create(ip_box);
    char ip_str[512];
    get_ip_address(ip_str, sizeof(ip_str));
    lv_label_set_text(g_sysmon.ip_label, ip_str);
    lv_obj_set_style_text_font(g_sysmon.ip_label, ui_get_font(16), 0);
    lv_obj_set_style_text_color(g_sysmon.ip_label, COLOR_ROOM_LIGHT, 0);
    lv_obj_set_style_text_align(g_sysmon.ip_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_sysmon.ip_label, LV_ALIGN_CENTER, 0, 15);

    // Temperature section
    lv_obj_t *temp_box = lv_obj_create(bg);
    lv_obj_set_size(temp_box, 180, 50);
    lv_obj_align(temp_box, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(temp_box, COLOR_ROOM_DARK, 0);
    lv_obj_set_style_bg_opa(temp_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(temp_box, 10, 0);
    lv_obj_set_style_pad_all(temp_box, 10, 0);
    lv_obj_set_style_border_width(temp_box, 0, 0);

    g_sysmon.temp_label = lv_label_create(temp_box);
    lv_label_set_text(g_sysmon.temp_label, "Temperature: --°C");
    lv_obj_set_style_text_font(g_sysmon.temp_label, ui_get_font(14), 0);
    lv_obj_set_style_text_color(g_sysmon.temp_label, COLOR_WHITE, 0);
    lv_obj_center(g_sysmon.temp_label);

    return g_sysmon.page;
}

void system_monitor_start_timer(void)
{
    if(!g_sysmon.update_timer) {
        g_sysmon.update_timer = lv_timer_create(update_timer_cb, 100, &g_sysmon);
    }
}

void system_monitor_stop_timer(void)
{
    if(g_sysmon.update_timer) {
        lv_timer_delete(g_sysmon.update_timer);
        g_sysmon.update_timer = NULL;
    }
}

void system_monitor_show(void)
{
    if(g_sysmon.page) {
        lv_obj_clear_flag(g_sysmon.page, LV_OBJ_FLAG_HIDDEN);
    }
}

void system_monitor_hide(void)
{
    if(g_sysmon.page) {
        lv_obj_add_flag(g_sysmon.page, LV_OBJ_FLAG_HIDDEN);
    }
}

void system_monitor_set_settings_nav_cb(void (*cb)(void))
{
    g_sysmon.settings_nav_cb = cb;
}

void system_monitor_set_bms_nav_cb(void (*cb)(void))
{
    g_sysmon.bms_nav_cb = cb;
}
