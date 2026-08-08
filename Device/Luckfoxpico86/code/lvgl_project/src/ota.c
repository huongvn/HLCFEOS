#include "src/ota.h"
#include "src/config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define GH_API_TMP   "/tmp/gh_release.json"
#define APP_NEW_PATH  "/home/pico/app_new"
#define APP_REAL_PATH "/home/pico/app"
#define APP_BACKUP    "/home/pico/app.bak.ota"
#define SHA_TMP       "/tmp/ota_new.sha256"
#define DONE_FLAG     "/tmp/ota_download.done"

static ota_status_t g_ota_status = {
    .state = OTA_STATE_IDLE,
    .progress = 0,
    .message = "System Up to Date",
    .new_version = ""
};

static char g_download_url[512] = "";
static char g_sha_url[512] = "";

static void ota_update_progress(int progress);

/* ---------- Minimal JSON helpers (GitHub API releases/latest) ---------- */

/* Extract the value of a flat "key":value or "key": "value" string. */
static int json_get_str(const char *json, const char *key, char *out, size_t n) {
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
    out[i] = '\0';
    return 0;
}

/* Find the first release asset whose "name" starts with `prefix`, then return
 * its browser_download_url. Also copies the asset name into name_out.
 * Tolerates both `"name":"..."` and `"name": "..."` JSON styles. */
static int json_asset_url(const char *json, const char *prefix,
                          char *name_out, size_t name_n,
                          char *url_out, size_t url_n) {
    size_t plen = strlen(prefix);
    const char *p = json;
    while ((p = strstr(p, "\"name\"")) != NULL) {
        p += 6;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != '"') continue;
        p++;
        const char *q = strchr(p, '"');
        if (!q) break;
        if ((size_t)(q - p) >= plen && strncmp(p, prefix, plen) == 0) {
            /* asset name found: scan forward for its browser_download_url */
            const char *u = strstr(q, "\"browser_download_url\"");
            if (!u) return -1;
            while (*u && *u != ':') u++;
            if (*u != ':') return -1;
            u++;
            while (*u == ' ' || *u == '\t' || *u == '\n' || *u == '\r') u++;
            if (*u != '"') return -1;
            u++;
            const char *ue = strchr(u, '"');
            if (!ue) return -1;
            size_t nlen = (size_t)(q - p);
            size_t urllen = (size_t)(ue - u);
            if (nlen < name_n) { strncpy(name_out, p, nlen); name_out[nlen] = '\0'; }
            if (urllen < url_n) { strncpy(url_out, u, urllen); url_out[urllen] = '\0'; }
            return 0;
        }
    }
    return -1;
}

/* ---------- Version comparison (x.y.z) ---------- */
static int ver_cmp(const char *a, const char *b) {
    int ai[3] = {0}, bi[3] = {0}, an = 0, bn = 0;
    sscanf(a, "%d.%d.%d", &ai[0], &ai[1], &ai[2]);
    sscanf(b, "%d.%d.%d", &bi[0], &bi[1], &bi[2]);
    for (an = 0; an < 3 && ai[an]; an++);
    for (bn = 0; bn < 3 && bi[bn]; bn++);
    int n = an > bn ? an : bn;
    for (int i = 0; i < n; i++) {
        if (ai[i] != bi[i]) return ai[i] < bi[i] ? -1 : 1;
    }
    return 0;
}

/* ---------- HTTP via curl (board has curl with OpenSSL) ---------- */

static int http_get_to_file(const char *url, const char *token, const char *dest) {
    char cmd[1024];
    if (token && token[0]) {
        snprintf(cmd, sizeof(cmd),
                 "curl -sSL --connect-timeout 8 --max-time 15 -H \"Authorization: token %s\" -o %s %s",
                 token, dest, url);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "curl -sSL --connect-timeout 8 --max-time 15 -o %s %s", dest, url);
    }
    int rc = system(cmd);
    return (rc == 0) ? 0 : -1;
}

/* Extract version number from a GitHub tag: strip leading
 * "v", "app-v", "bms-v", "release-v" ... i.e. copy from the first digit. */
static void tag_to_version(const char *tag, char *ver, size_t n) {
    const char *p = tag;
    while (*p && !(*p >= '0' && *p <= '9')) p++;
    strncpy(ver, p, n - 1);
    ver[n - 1] = '\0';
}

void ota_init(void) {
    g_ota_status.state = OTA_STATE_IDLE;
    strncpy(g_ota_status.message, "System Up to Date", sizeof(g_ota_status.message));
    printf("[OTA] GitHub Releases OTA initialized\n");
}

ota_status_t* ota_get_status(void) {
    return &g_ota_status;
}

void ota_check_now(void) {
    if (g_ota_status.state != OTA_STATE_IDLE && g_ota_status.state != OTA_STATE_FAILED) return;

    app_config_t *cfg = config_get();
    if (!cfg->ota_enabled || cfg->github_repo[0] == '\0') {
        g_ota_status.state = OTA_STATE_IDLE;
        strncpy(g_ota_status.message, "OTA disabled", sizeof(g_ota_status.message));
        return;
    }

    g_ota_status.state = OTA_STATE_CHECKING;
    strncpy(g_ota_status.message, "Checking GitHub...", sizeof(g_ota_status.message));

    char api_url[256];
    snprintf(api_url, sizeof(api_url),
             "https://api.github.com/repos/%s/releases/latest", cfg->github_repo);
    printf("[OTA] Checking: %s\n", api_url);

    if (http_get_to_file(api_url, cfg->ota_github_token, GH_API_TMP) != 0) {
        g_ota_status.state = OTA_STATE_FAILED;
        strncpy(g_ota_status.message, "GitHub check failed", sizeof(g_ota_status.message));
        printf("[OTA] Failed to fetch GitHub release\n");
        return;
    }

    char buf[262144];
    FILE *f = fopen(GH_API_TMP, "r");
    if (!f) {
        g_ota_status.state = OTA_STATE_FAILED;
        strncpy(g_ota_status.message, "GitHub check failed", sizeof(g_ota_status.message));
        return;
    }
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (len == 0) {
        g_ota_status.state = OTA_STATE_FAILED;
        strncpy(g_ota_status.message, "Empty release response", sizeof(g_ota_status.message));
        return;
    }
    buf[len] = '\0';

    char tag[64] = "", asset_name[128] = "", url[512] = "";
    if (json_get_str(buf, "tag_name", tag, sizeof(tag)) != 0) {
        g_ota_status.state = OTA_STATE_FAILED;
        strncpy(g_ota_status.message, "Release parse failed", sizeof(g_ota_status.message));
        return;
    }

    char new_ver[48];
    tag_to_version(tag, new_ver, sizeof(new_ver));

    if (json_asset_url(buf, "app_v", asset_name, sizeof(asset_name),
                       url, sizeof(url)) != 0) {
        g_ota_status.state = OTA_STATE_IDLE;
        strncpy(g_ota_status.message, "No app asset in release", sizeof(g_ota_status.message));
        return;
    }

    printf("[OTA] Latest: %s (asset: %s)\n", new_ver, asset_name);
    printf("[OTA] Current build: %s\n", APP_VERSION);

    if (ver_cmp(new_ver, APP_VERSION) <= 0) {
        g_ota_status.state = OTA_STATE_IDLE;
        strncpy(g_ota_status.message, "System Up to Date", sizeof(g_ota_status.message));
        return;
    }

    g_ota_status.state = OTA_STATE_AVAILABLE;
    strncpy(g_ota_status.new_version, new_ver, sizeof(g_ota_status.new_version));
    snprintf(g_ota_status.message, sizeof(g_ota_status.message), "New version %s available", new_ver);
    strncpy(g_download_url, url, sizeof(g_download_url));

    /* sidecar sha url: same asset name + ".sha256" */
    char surl[160];
    snprintf(surl, sizeof(surl), "%s.sha256", url);
    strncpy(g_sha_url, surl, sizeof(g_sha_url));
    printf("[OTA] Asset URL: %s\n[OTA] SHA URL: %s\n", url, surl);
}

static void ota_monitor_timer_cb(lv_timer_t *t) {
    if (g_ota_status.state != OTA_STATE_DOWNLOADING) return;

    if (access(DONE_FLAG, F_OK) == 0) {
        unlink(DONE_FLAG);
        ota_update_progress(100);
        lv_timer_pause(t);
    } else {
        if (g_ota_status.progress < 95) {
            ota_update_progress(g_ota_status.progress + 2);
        }
    }
}

static lv_timer_t *monitor_timer = NULL;

void ota_start_download(void) {
    if (g_ota_status.state != OTA_STATE_AVAILABLE) return;

    g_ota_status.state = OTA_STATE_DOWNLOADING;
    g_ota_status.progress = 0;
    strncpy(g_ota_status.message, "Downloading... 0%", sizeof(g_ota_status.message));

    /* Clear stale state before starting */
    unlink(DONE_FLAG);
    unlink(SHA_TMP);
    unlink(APP_NEW_PATH);

    /* Download to staging path; install step swaps it atomically. */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "(curl -sSL -f -o %s %s && curl -sSL -f -o %s %s && touch %s) &",
             APP_NEW_PATH, g_download_url,
             SHA_TMP, g_sha_url, DONE_FLAG);

    printf("[OTA] Downloading...\n");
    system(cmd);

    if (!monitor_timer) {
        monitor_timer = lv_timer_create(ota_monitor_timer_cb, 1000, NULL);
    } else {
        lv_timer_resume(monitor_timer);
    }
}

static int verify_downloaded_file(void) {
    FILE *sf = fopen(SHA_TMP, "r");
    if (!sf) {
        fprintf(stderr, "[OTA] VERIFY: cannot open %s\n", SHA_TMP);
        return -1;
    }
    char line[256];
    if (!fgets(line, sizeof(line), sf)) { fclose(sf); return -1; }
    fclose(sf);
    char expected[65];
    if (sscanf(line, "%64s", expected) != 1) return -1;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sha256sum %s 2>/dev/null", APP_NEW_PATH);
    FILE *cf = popen(cmd, "r");
    if (!cf) return -1;
    char actual[80];
    if (!fgets(actual, sizeof(actual), cf)) { pclose(cf); return -1; }
    pclose(cf);
    char calc[64];
    if (sscanf(actual, "%63s", calc) != 1) return -1;

    if (strcmp(expected, calc) == 0) {
        fprintf(stderr, "[OTA] SHA256 OK (%s)\n", calc);
        return 0;
    }
    fprintf(stderr,
            "[OTA] SHA256 MISMATCH\n"
            "  expected: %s\n"
            "  actual:   %s\n",
            expected, calc);

    /* One retry: re-fetch the sidecar sha file (transient failure) */
    char retry[1024];
    snprintf(retry, sizeof(retry), "curl -sSL -f -o %s %s", SHA_TMP, g_sha_url);
    if (system(retry) == 0) {
        sf = fopen(SHA_TMP, "r");
        if (sf) {
            if (fgets(line, sizeof(line), sf)) {
                char expected2[65];
                if (sscanf(line, "%64s", expected2) == 1 && strcmp(expected2, calc) == 0) {
                    fclose(sf);
                    fprintf(stderr, "[OTA] SHA256 OK after retry (%s)\n", calc);
                    return 0;
                }
            }
            fclose(sf);
        }
    }
    return -1;
}

static void ota_update_progress(int progress) {
    g_ota_status.progress = progress;
    snprintf(g_ota_status.message, sizeof(g_ota_status.message), "Downloading... %d%%", progress);

    if (progress >= 100) {
        g_ota_status.state = OTA_STATE_VERIFYING;
        strncpy(g_ota_status.message, "Verifying integrity...", sizeof(g_ota_status.message));

        if (verify_downloaded_file() == 0) {
            g_ota_status.state = OTA_STATE_READY_TO_INSTALL;
            strncpy(g_ota_status.message, "Ready to install", sizeof(g_ota_status.message));
        } else {
            unlink(SHA_TMP);
            g_ota_status.state = OTA_STATE_FAILED;
            strncpy(g_ota_status.message, "SHA256 mismatch", sizeof(g_ota_status.message));
        }
    }
}

void ota_install_now(void) {
    if (g_ota_status.state != OTA_STATE_READY_TO_INSTALL) return;

    g_ota_status.state = OTA_STATE_INSTALLING;
    strncpy(g_ota_status.message, "Installing...", sizeof(g_ota_status.message));

    printf("[OTA] Atomic install: backup then swap\n");

    /* Keep current (old) binary as backup, swap new one in atomically. */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cp %s %s", APP_REAL_PATH, APP_BACKUP);
    system(cmd);

    if (rename(APP_NEW_PATH, APP_REAL_PATH) == 0) {
        if (chmod(APP_REAL_PATH, 0755) != 0) {
            printf("[OTA] Warning: chmod failed\n");
        }
        g_ota_status.state = OTA_STATE_SUCCESS;
        strncpy(g_ota_status.message, "Update Successful! Rebooting...", sizeof(g_ota_status.message));
        sync();
        printf("[OTA] Rebooting now...\n");
        system("/sbin/reboot");
        while(1) { sleep(1); }
    } else {
        int err = errno;
        printf("[OTA] Rename failed: %s (%d)\n", strerror(err), err);
        g_ota_status.state = OTA_STATE_FAILED;
        snprintf(g_ota_status.message, sizeof(g_ota_status.message), "Install failed: %s", strerror(err));
    }
}

void ota_signal_success(void) {
    printf("[OTA] Health check success at boot. New binary is running.\n");
    /* Old backup no longer needed. */
    unlink(APP_BACKUP);
}