/**
 * @file main.c
 * @brief LVGL System Monitor - Main entry point
 *
 * Canopi Smart Room Controller for Luckfox Pico Panel 86
 *
 * Features:
 * - System Monitor: CPU, RAM, IP, Temperature
 * - Smart Room: HVAC control with temperature arc, mode selector, fan speed
 * - MQTT: Real-time sensor data via libmosquitto
 */

#include "lvgl.h"
#include "lvgl/src/drivers/evdev/lv_evdev.h"
#include "src/ui.h"
#include "src/ui_common.h"
#include "src/ota.h"
#include "src/bms.h"
#include "src/http_client.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/fb.h>
#include <string.h>
#include <time.h>

/*==================
 * FRAMEBUFFER GLOBALS
 *==================*/

static int fbfd = -1;
static struct fb_var_screeninfo vinfo;
static uint32_t *fbp = NULL;

/*==================
 * FRAMEBUFFER FUNCTIONS
 *==================*/

static void fbdev_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t x, y;
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    uint16_t *src = (uint16_t *)px_map;
    uint32_t *dest32 = fbp + area->y1 * vinfo.xres + area->x1;

    for(y = 0; y < h; y++) {
        for(x = 0; x < w; x++) {
            uint16_t c = src[y * w + x];
            uint8_t r = (c >> 11) & 0x1F;
            uint8_t g = (c >> 5) & 0x3F;
            uint8_t b = c & 0x1F;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            dest32[y * vinfo.xres + x] = (r << 16) | (g << 8) | b;
        }
    }
    ioctl(fbfd, FBIO_WAITFORVSYNC, 0);
    lv_display_flush_ready(disp);
}

static int fbdev_init_local(void)
{
    fbfd = open("/dev/fb0", O_RDWR);
    if(fbfd < 0) {
        perror("fbdev open");
        return -1;
    }

    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("fbdev ioctl");
        close(fbfd);
        return -1;
    }

    printf("[FB] Screen: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    long screensize = vinfo.xres * vinfo.yres * (vinfo.bits_per_pixel / 8);
    fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if(fbp == MAP_FAILED) {
        perror("mmap");
        close(fbfd);
        return -1;
    }

    return 0;
}

static void fbdev_deinit_local(void)
{
    if(fbp) {
        munmap(fbp, vinfo.xres * vinfo.yres * (vinfo.bits_per_pixel / 8));
    }
    if(fbfd >= 0) {
        close(fbfd);
    }
}

/*==================
 * MQTT PUBLISH
 *==================*/

// Publish is now handled directly via libmosquitto in smart_room_mqtt_publish()

/*==================
 * NON-BLOCKING INPUT
 *==================*/

static int kbhit(void)
{
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

/*==================
 * MAIN
 *==================*/

int main(void)
{
    printf("[APP] Canopi Smart Room Controller\n");
    printf("[APP] =============================\n");

    // Initialize Config
    config_load();
    config_apply_brightness();
    
    // Initialize OTA
    ota_init();

    // Initialize LVGL
    lv_init();

    // Initialize framebuffer
    if(fbdev_init_local() < 0) {
        return -1;
    }

    // Create display (dynamic resolution from vinfo)
    lv_display_t *disp = lv_display_create(vinfo.xres, vinfo.yres);

    // Phase 4: Buffer optimization - increase from 1/10 to 1/4 screen
    static lv_color_t *buf1 = NULL;
    if(!buf1) {
        buf1 = malloc(vinfo.xres * vinfo.yres * sizeof(lv_color_t) / 4);
    }
    lv_display_set_buffers(disp, buf1, NULL, vinfo.xres * vinfo.yres * sizeof(lv_color_t) / 4, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, fbdev_flush);

    // Initialize evdev input driver
    printf("[MAIN] Creating evdev input...\n");
    fflush(stdout);
    lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
    printf("[MAIN] Evdev created\n");
    fflush(stdout);

    // Initialize UI
    ui_init(lv_screen_active());

    // Init HTTP client (connects to BMS engine API + SSE live stream)
    http_client_init();

    // Start UI timers
    ui_start_timers();

    // OTA Exit Point 3: Signal health check success
    ota_signal_success();

    // Launch BMS Dashboard as default screen
    bms_init();

    printf("[APP] System running. Press Q to exit.\n");
    printf("[APP] BMS engine API: http://127.0.0.1:8080\n");

    // Non-blocking stdin
    if(system("stty -echo -icanon min 0 time 0 2>/dev/null") != 0) {
        printf("[APP] Warning: stty setup failed (no TTY?)\n");
    }

    // Main loop with accurate tick timing
    struct timespec ts_prev, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_prev);

    while(1) {
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        uint32_t elapsed_ms = (uint32_t)((ts_now.tv_sec - ts_prev.tv_sec) * 1000 +
                              (ts_now.tv_nsec - ts_prev.tv_nsec) / 1000000);
        if(elapsed_ms > 0) {
            lv_tick_inc(elapsed_ms);
            ts_prev = ts_now;
        }

        if(kbhit()) {
            char c = getchar();
            if(c == 'q' || c == 'Q') {
                printf("\n[APP] Exiting...\n");
                break;
            }
        }

        lv_timer_handler();
        http_client_poll();
        usleep(1000);  // ~1ms main loop
    }

    // Cleanup
    if(system("stty echo icanon 2>/dev/null") != 0) {
        printf("[APP] Warning: Failed to restore terminal mode\n");
    }
    
    ui_stop_timers();
    http_client_cleanup();
    fbdev_deinit_local();

    printf("[APP] Goodbye!\n");
    return 0;
}
