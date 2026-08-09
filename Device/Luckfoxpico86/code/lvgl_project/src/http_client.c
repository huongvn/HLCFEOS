#include "src/http_client.h"
#include "src/bms.h"
#include "src/ota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define HTTP_CONNECT_TIMEOUT_S 3
#define HTTP_RECV_CHUNK 2048
#define HTTP_LINE_MAX 2048
#define HTTP_SSE_RECONNECT_MS 5000

static pthread_t      g_sse_thread;
static volatile bool  g_sse_running = false;
static volatile bool  g_connected    = false;
static http_state_cb_t g_state_cb   = NULL;

#define SSE_QUEUE_MAX 64

typedef struct {
    char device_id[64];
    char attr[32];
    char value[64];
    char ack_command_id[40];
    bool online;
    bool has_ack;
} sse_event_t;

static sse_event_t   g_sse_queue[SSE_QUEUE_MAX];
static int           g_sse_q_head = 0;
static int           g_sse_q_tail = 0;
static int           g_sse_q_cnt  = 0;
static pthread_mutex_t g_sse_q_mutex = PTHREAD_MUTEX_INITIALIZER;

static void sse_queue_push(const sse_event_t *ev)
{
    pthread_mutex_lock(&g_sse_q_mutex);
    if (g_sse_q_cnt < SSE_QUEUE_MAX) {
        g_sse_queue[g_sse_q_tail] = *ev;
        g_sse_q_tail = (g_sse_q_tail + 1) % SSE_QUEUE_MAX;
        g_sse_q_cnt++;
    }
    pthread_mutex_unlock(&g_sse_q_mutex);
}

static bool sse_queue_pop(sse_event_t *ev)
{
    bool got = false;
    pthread_mutex_lock(&g_sse_q_mutex);
    if (g_sse_q_cnt > 0) {
        *ev = g_sse_queue[g_sse_q_head];
        g_sse_q_head = (g_sse_q_head + 1) % SSE_QUEUE_MAX;
        g_sse_q_cnt--;
        got = true;
    }
    pthread_mutex_unlock(&g_sse_q_mutex);
    return got;
}

static void bms_dispatch(const sse_event_t *ev)
{
    if (g_state_cb) {
        g_state_cb(ev->device_id, ev->attr,
                   ev->value, ev->has_ack ? ev->ack_command_id : NULL,
                   ev->attr && strcmp(ev->attr, "online") == 0
                       ? (strcmp(ev->value, "true") == 0) : true);
    } else {
        bms_handle_api_event(ev->device_id, ev->attr, ev->value,
                             ev->has_ack ? ev->ack_command_id : NULL);
    }
}

/* ---------------- JSON helpers ---------------- */

static int json_unescape(const char *s, size_t len, char *out, size_t outsz)
{
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < outsz; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < len) {
            char n = s[i + 1];
            switch (n) {
                case 'n': out[o++] = '\n'; i++; break;
                case 't': out[o++] = '\t'; i++; break;
                case 'r': out[o++] = '\r'; i++; break;
                case '"': out[o++] = '"'; i++; break;
                case '\\': out[o++] = '\\'; i++; break;
                default:  out[o++] = c; break;
            }
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return (int)o;
}

static int json_get(const char *json, const char *key, char *out, size_t outsz)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        const char *start = ++p;
        size_t len = 0;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
            len = (size_t)(p - start);
        }
        json_unescape(start, len, out, outsz);
        return 0;
    }
    const char *start = p;
    while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\n') p++;
    size_t len = (size_t)(p - start);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

/* ---------------- Socket helpers ---------------- */

static int http_socket_connect_with_timeout(int timeout_s)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    if (timeout_s > 0) {
        struct timeval tv = {timeout_s, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_API_PORT);
    if (inet_pton(AF_INET, HTTP_API_HOST, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int http_socket_connect(void)
{
    return http_socket_connect_with_timeout(HTTP_CONNECT_TIMEOUT_S);
}

static int http_socket_connect_sse(void)
{
    int fd = http_socket_connect_with_timeout(0);
    if (fd >= 0) {
        struct timeval tv = {1, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    return fd;
}

static int http_send_all(int fd, const char *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, data + off, len - off, 0);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

typedef struct {
    int   fd;
    char  buf[HTTP_RECV_CHUNK];
    size_t head;
    size_t tail;
} line_reader_t;

static void lr_init(line_reader_t *lr, int fd)
{
    lr->fd = fd;
    lr->head = 0;
    lr->tail = 0;
}

static int lr_read_line(line_reader_t *lr, char *out, size_t outsz)
{
    size_t line_len = 0;
    for (;;) {
        if (lr->head < lr->tail) {
            char c = lr->buf[lr->head++];
            if (c == '\n') {
                if (line_len > 0 && out[line_len - 1] == '\r') line_len--;
                out[line_len] = '\0';
                return (int)line_len;
            }
            if (line_len + 1 < outsz) out[line_len++] = c;
            continue;
        }
        ssize_t n = recv(lr->fd, lr->buf, sizeof(lr->buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
            if (line_len > 0) {
                out[line_len] = '\0';
                return (int)line_len;
            }
            return -1;
        }
        if (n == 0) {
            if (line_len > 0) {
                out[line_len] = '\0';
                return (int)line_len;
            }
            return -1;
        }
        lr->head = 0;
        lr->tail = (size_t)n;
    }
}

static char *http_request(const char *method, const char *path,
                          const char *body, int *status_out)
{
    int fd = http_socket_connect();
    if (fd < 0) return NULL;

    char req[4096];
    int len = 0;
    if (body) {
        len = snprintf(req, sizeof(req),
                       "%s %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
                       "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
                       method, path, HTTP_API_HOST, HTTP_API_PORT, strlen(body), body);
    } else {
        len = snprintf(req, sizeof(req),
                       "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                       method, path, HTTP_API_HOST, HTTP_API_PORT);
    }
    if (len <= 0 || len >= (int)sizeof(req)) {
        close(fd);
        return NULL;
    }
    if (http_send_all(fd, req, (size_t)len) < 0) {
        close(fd);
        return NULL;
    }

    line_reader_t lr;
    lr_init(&lr, fd);
    char line[HTTP_LINE_MAX];
    int status = 0;
    long content_length = -1;
    int first_line = 1;

    for (;;) {
        int n = lr_read_line(&lr, line, sizeof(line));
        if (n < 0) { close(fd); return NULL; }
        if (n == 0 && first_line) { close(fd); return NULL; }
        if (first_line) {
            if (sscanf(line, "HTTP/1.%*d %d", &status) != 1) status = 0;
            first_line = 0;
        }
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = atol(line + 15);
        }
        if (line[0] == '\0') break;
    }
    if (status_out) *status_out = status;

    size_t cap = (content_length > 0) ? (size_t)content_length + 1 : HTTP_RECV_CHUNK;
    char *out = malloc(cap ? cap : 1);
    if (!out) { close(fd); return NULL; }
    size_t used = 0;

    if (lr.head < lr.tail) {
        size_t avail = lr.tail - lr.head;
        if (used + avail + 1 > cap) {
            cap = used + avail + 1;
            char *tmp = realloc(out, cap);
            if (!tmp) { free(out); close(fd); return NULL; }
            out = tmp;
        }
        memcpy(out + used, lr.buf + lr.head, avail);
        used += avail;
        lr.head = lr.tail;
    }

    for (;;) {
        if (content_length >= 0 && (long)used >= content_length) break;
        if (used + HTTP_RECV_CHUNK + 1 > cap) {
            cap = used + HTTP_RECV_CHUNK + 1;
            char *tmp = realloc(out, cap);
            if (!tmp) { free(out); close(fd); return NULL; }
            out = tmp;
        }
        ssize_t n = recv(fd, out + used, HTTP_RECV_CHUNK, 0);
        if (n <= 0) break;
        used += (size_t)n;
    }
    out[used] = '\0';
    close(fd);
    return out;
}

/* ---------------- Catalog + states fetch ---------------- */

static dev_type_t type_from_str(const char *type)
{
    if (strcmp(type, "AC") == 0) return DEV_TYPE_AC;
    if (strcmp(type, "Sign") == 0) return DEV_TYPE_SIGN;
    if (strcmp(type, "Power") == 0) return DEV_TYPE_POWER_METER;
    if (strcmp(type, "Light") == 0) return DEV_TYPE_LIGHT_SENSOR;
    if (strcmp(type, "Switch") == 0) return DEV_TYPE_SWITCH;
    return DEV_TYPE_AC;
}

static int index_from_device_id(const char *id)
{
    const char *us = strrchr(id, '_');
    if (!us) return 0;
    return atoi(us + 1);
}

// Parse /api/v1/devices/catalog into g_devices[]. Must be called on the main
// thread (uses bms globals). Returns true on success.
bool http_fetch_catalog(void)
{
    FILE *lg = fopen("/tmp/btn.log", "a");
    if (lg) { fprintf(lg, "http_fetch_catalog CALLED\n"); fclose(lg); }
    int status = 0;
    char *body = http_request("GET", "/api/v1/devices/catalog", NULL, &status);
    FILE *lg2 = fopen("/tmp/btn.log", "a");
    if (lg2) { fprintf(lg2, "  catalog status=%d body=%s\n", status, body ? body : "(null)"); fclose(lg2); }
    if (!body) {
        printf("[HTTP] catalog failed (no response)\n");
        return false;
    }
    if (status != 200) {
        printf("[HTTP] catalog status %d\n", status);
        free(body);
        return false;
    }

    if (!g_devices) {
        g_max_devices = MAX_DEVICES_DEFAULT;
        g_devices = calloc(g_max_devices, sizeof(bms_device_t));
        if (!g_devices) { free(body); return false; }
    }
    g_device_count = 0;

    // Iterate each device object in the "devices" array. The engine emits
    // display_attrs BEFORE the device "id", so anchor on "display_attrs" and
    // bound to this device object (array-closing ']' ... device-closing '}')
    // so we never run into the next device.
    const char *p = body;
    while ((p = strstr(p, "\"display_attrs\"")) != NULL) {
        bms_device_t *d = &g_devices[g_device_count];

        const char *arr_close = strchr(p, ']');
        const char *end_obj = arr_close ? strchr(arr_close, '}') : NULL;
        if (!arr_close || !end_obj) {
            p += strlen("\"display_attrs\""); continue;
        }

        memset(d, 0, sizeof(bms_device_t));

        // Parse the display attrs inside the JSON array.
        int ac = 0;
        const char *q = p;
        while (ac < MAX_DISPLAY_ATTRS && (q = strstr(q, "\"attr_id\"")) && q < arr_close) {
            char aid[32], label[32], unit[16], atype[16];
            if (json_get(q, "attr_id", aid, sizeof(aid)) != 0) break;
            json_get(q, "label", label, sizeof(label));
            json_get(q, "unit", unit, sizeof(unit));
            json_get(q, "type", atype, sizeof(atype));
            char ov[8], ctl[8];
            bool overview = json_get(q, "overview", ov, sizeof(ov)) == 0 && strcmp(ov, "true") == 0;
            bool control  = json_get(q, "control", ctl, sizeof(ctl)) == 0 && strcmp(ctl, "true") == 0;

            strncpy(d->display_attr_ids[ac], aid, 31);
            strncpy(d->display_attr_labels[ac], label, 31);
            strncpy(d->display_attr_units[ac], unit, 15);
            d->display_attr_types[ac] = (strcmp(atype, "bool") == 0) ? ATTR_TYPE_BOOL : ATTR_TYPE_NUMBER;
            d->display_attr_overview[ac] = overview;
            d->display_attr_control[ac] = control;
            d->display_attr_count++;
            ac++;
            q += strlen("\"attr_id\"");
        }

        // Identity fields live in the same device object, after the attrs array.
        // NOTE: attribute objects also carry a "type" (bool/number) that appears
        // BEFORE the device id, so search id first and parse type/name/group
        // from the id position onward to avoid picking the attr's type.
        const char *idmark = strstr(p, "\"id\"");
        char id[64], t[16], name[64], group[32];
        if (!idmark ||
            json_get(idmark, "id", id, sizeof(id)) != 0 ||
            json_get(idmark, "type", t, sizeof(t)) != 0) {
            p += strlen("\"display_attrs\""); continue;
        }
        json_get(idmark, "name", name, sizeof(name));
        json_get(p, "group", group, sizeof(group));

        d->type = type_from_str(t);
        strncpy(d->api_device_id, id, sizeof(d->api_device_id) - 1);
        strncpy(d->name, name, sizeof(d->name) - 1);
        strncpy(d->group, group, sizeof(d->group) - 1);
        d->ac_index = index_from_device_id(id);
        d->sign_index = d->ac_index;
        d->power_index = d->ac_index;
        d->switch_index = d->ac_index;
        d->light_sensor_index = d->ac_index;
        d->online = false;
        d->enabled = false;

        g_device_count++;
        if (g_device_count >= g_max_devices) break;
        p = end_obj;
    }

    printf("[HTTP] catalog: %d devices\n", g_device_count);
    free(body);
    return g_device_count > 0;
}

// Apply realtime attrs from /api/v1/devices to g_devices[]. Attr values are
// keyed by engine label; map to our display_attr by label.
void http_fetch_states(void)
{
    int status = 0;
    char *body = http_request("GET", "/api/v1/devices", NULL, &status);
    if (!body || status != 200) {
        if (body) free(body);
        return;
    }
    const char *p = body;
    while ((p = strstr(p, "\"id\"")) != NULL) {
        char id[64];
        if (json_get(p, "id", id, sizeof(id)) != 0) { p += strlen("\"id\""); continue; }
        for (int i = 0; i < g_device_count; i++) {
            if (strcmp(g_devices[i].api_device_id, id) != 0) continue;
            // state
            char st[8];
            if (json_get(p, "state", st, sizeof(st)) == 0 &&
                strcmp(st, "ON") == 0) {
                g_devices[i].enabled = true;
                // mark the bool control display attr to 1
                for (int a = 0; a < g_devices[i].display_attr_count; a++) {
                    if (g_devices[i].display_attr_control[a] &&
                        g_devices[i].display_attr_types[a] == ATTR_TYPE_BOOL)
                        g_devices[i].display_attr_values[a] = 1;
                }
            }
            // attrs object keyed by label: find each
            const char *attrs = strstr(p, "\"attrs\"");
            if (attrs) {
                for (int a = 0; a < g_devices[i].display_attr_count; a++) {
                    char key[64];
                    snprintf(key, sizeof(key), "\"%s\"", g_devices[i].display_attr_labels[a]);
                    const char *kv = strstr(attrs, key);
                    if (!kv) continue;
                    char val[32];
                    if (json_get(kv, g_devices[i].display_attr_labels[a], val, sizeof(val)) != 0) continue;
                    if (g_devices[i].display_attr_types[a] == ATTR_TYPE_BOOL) {
                        g_devices[i].display_attr_values[a] = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
                    } else {
                        g_devices[i].display_attr_values[a] = (int16_t)(atof(val) * 10.0);
                    }
                }
            }
            break;
        }
        p += strlen("\"id\"");
    }
    free(body);
}

/* ---------------- SSE thread ---------------- */

static void *http_sse_thread(void *arg)
{
    (void)arg;
    while (g_sse_running) {
        int fd = http_socket_connect_sse();
        if (fd < 0) {
            g_connected = false;
            usleep(HTTP_SSE_RECONNECT_MS * 1000);
            continue;
        }

        const char *req =
            "GET /api/v1/events HTTP/1.1\r\n"
            "Host: 127.0.0.1:8080\r\n"
            "Accept: text/event-stream\r\n"
            "Connection: keep-alive\r\n\r\n";
        if (http_send_all(fd, req, strlen(req)) < 0) {
            close(fd);
            g_connected = false;
            usleep(HTTP_SSE_RECONNECT_MS * 1000);
            continue;
        }

        line_reader_t lr;
        lr_init(&lr, fd);
        char line[HTTP_LINE_MAX];
        int headers_done = 0;

        g_connected = true;
        printf("[HTTP] SSE connected to %s:%d/api/v1/events\n", HTTP_API_HOST, HTTP_API_PORT);

        /* Report app version on every (re)connect — the engine may have been
         * restarted (OTA) and needs the version for cloud "check"/"updated". */
        http_client_report_app_version(APP_VERSION);

        int ok = 1;
        while (g_sse_running && ok) {
            int n = lr_read_line(&lr, line, sizeof(line));
            if (n == -2) continue;
            if (n < 0) { ok = 0; break; }

            if (!headers_done) {
                if (n == 0) headers_done = 1;
                continue;
            }
            if (n == 0) continue;
            if (strncmp(line, "data:", 5) != 0) continue;

            const char *json = line + 5;
            while (*json == ' ') json++;

            /* OTA control events carry no device_id; pass them straight to
             * the OTA module. Shape: {"event":"OTA_UPDATE","action":"update"} */
            char ev_name[32];
            if (json_get(json, "event", ev_name, sizeof(ev_name)) == 0 &&
                strcmp(ev_name, "OTA_UPDATE") == 0) {
                char action[16] = "update";
                json_get(json, "action", action, sizeof(action));
                printf("[HTTP] SSE OTA_UPDATE action=%s\n", action);
                if (strcmp(action, "update") == 0) {
                    ota_auto_update_now();
                }
                continue;
            }

            sse_event_t ev;
            memset(&ev, 0, sizeof(ev));
            if (json_get(json, "device_id", ev.device_id, sizeof(ev.device_id)) != 0) continue;
            if (json_get(json, "attr", ev.attr, sizeof(ev.attr)) != 0) continue;
            json_get(json, "value", ev.value, sizeof(ev.value));
            if (json_get(json, "ack_command_id", ev.ack_command_id, sizeof(ev.ack_command_id)) == 0) {
                ev.has_ack = true;
            }
            sse_queue_push(&ev);
        }

        if (fd >= 0) close(fd);
        g_connected = false;
        if (g_sse_running) {
            printf("[HTTP] SSE disconnected, reconnecting in %dms...\n",
                   HTTP_SSE_RECONNECT_MS);
            usleep(HTTP_SSE_RECONNECT_MS * 1000);
        }
    }
    return NULL;
}

/* ---------------- Public API ---------------- */

void http_client_init(void)
{
    // Report the running app version to the engine so cloud OTA "check" and
    // "updated" replies include it. Best effort (no-op if engine is down).
    http_client_report_app_version(APP_VERSION);

    // Fetch catalog -> build/refresh g_devices[]. If engine temporarily down,
    // fallback (hardcoded defaults) is left to bms_init().
    bool catalog_ok = http_fetch_catalog();

    // Replay current realtime values (best effort). Skip if no catalog.
    if (catalog_ok) http_fetch_states();

    // Start SSE live stream thread (works regardless of catalog fetch).
    if (!g_sse_running) {
        g_sse_running = true;
        if (pthread_create(&g_sse_thread, NULL, http_sse_thread, NULL) != 0) {
            printf("[HTTP] FATAL: failed to create SSE thread\n");
            g_sse_running = false;
            return;
        }
        printf("[HTTP] SSE thread started\n");
    }
}

void http_client_poll(void)
{
    sse_event_t ev;
    while (sse_queue_pop(&ev)) {
        bms_dispatch(&ev);
    }
}

void http_client_action(const char *device_id, const char *action,
                        const char *params_json, const char *command_id)
{
    if (!device_id || !action || !command_id) return;

    char body[512];
    if (params_json && params_json[0]) {
        snprintf(body, sizeof(body), "{\"command_id\":\"%s\",\"action\":\"%s\",\"params\":%s}",
                 command_id, action, params_json);
    } else {
        snprintf(body, sizeof(body), "{\"command_id\":\"%s\",\"action\":\"%s\"}",
                 command_id, action);
    }

    char path[160];
    snprintf(path, sizeof(path), "/api/v1/devices/%s/actions", device_id);

    FILE *lg = fopen("/tmp/btn.log", "a");
    if (lg) { fprintf(lg, "  http_client_action -> %s action=%s\n", path, action); fclose(lg); }

    int status = 0;
    char *resp = http_request("POST", path, body, &status);
    FILE *lg2 = fopen("/tmp/btn.log", "a");
    if (lg2) { fprintf(lg2, "  http_client_action resp status=%d\n", status); fclose(lg2); }
    if (resp) {
        if (status == 202) {
            printf("[HTTP] action %s on %s queued (cmd %s)\n", action, device_id, command_id);
        } else {
            printf("[HTTP] action %s on %s FAILED (%d): %s\n", action, device_id, status, resp);
        }
        free(resp);
    } else {
        printf("[HTTP] action %s on %s: no response (engine down?)\n", action, device_id);
    }
}

void http_client_scene_action(const char *scene_id, const char *action,
                              const char *command_id)
{
    if (!scene_id || !action || !command_id) return;

    char body[320];
    snprintf(body, sizeof(body), "{\"command_id\":\"%s\",\"action\":\"%s\"}", command_id, action);

    char path[160];
    snprintf(path, sizeof(path), "/api/v1/scenes/%s/actions", scene_id);

    int status = 0;
    char *resp = http_request("POST", path, body, &status);
    if (resp) {
        if (status == 202) {
            printf("[HTTP] scene %s %s queued (cmd %s)\n", scene_id, action, command_id);
        } else {
            printf("[HTTP] scene %s %s FAILED (%d): %s\n", scene_id, action, status, resp);
        }
        free(resp);
    } else {
        printf("[HTTP] scene %s %s: no response\n", scene_id, action);
    }
}

bool http_client_is_connected(void)
{
    return g_connected;
}

void http_client_set_state_cb(http_state_cb_t cb)
{
    g_state_cb = cb;
}

void http_client_report_app_version(const char *version)
{
    if (!version || version[0] == '\0') return;

    char body[96];
    snprintf(body, sizeof(body), "{\"app_version\":\"%s\"}", version);

    int status = 0;
    char *resp = http_request("POST", "/api/system/app_version", body, &status);
    if (resp) {
        if (status == 200) {
            printf("[HTTP] reported app version %s\n", version);
        } else {
            printf("[HTTP] report app version failed (%d): %s\n", status, resp);
        }
        free(resp);
    }
}

void http_client_cleanup(void)
{
    g_sse_running = false;
    if (g_sse_thread) {
        pthread_join(g_sse_thread, NULL);
        g_sse_thread = 0;
    }
    printf("[HTTP] Cleanup done\n");
}