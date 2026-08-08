#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event callback for real-time updates from the engine (SSE + fetch).
// device_id is the engine's slug id (e.g. "ac_0"), attr is the YAML attr_id
// (e.g. "0101"), value is a NUL-terminated string snapshot, ack_command_id is
// the frontend command being acknowledged (or NULL for plain updates),
// online is the device connectivity (only meaningful for attr "online").
typedef void (*http_state_cb_t)(const char *device_id, const char *attr,
                                const char *value, const char *ack_command_id,
                                bool online);

#define HTTP_API_HOST "127.0.0.1"
#define HTTP_API_PORT 8080

// Connect + fetch catalog + initial state replay + start SSE (call once).
void http_client_init(void);

// Non-blocking poll (called from the main LVGL loop). Drains SSE events.
void http_client_poll(void);

// POST an action to a device: /api/v1/devices/{device_id}/actions
// action is one of TURN_ON / TURN_OFF / TOGGLE / SET_ATTRIBUTE.
// params_json is an optional JSON params object ("{...}") or NULL.
// command_id is the frontend-generated transaction id.
void http_client_action(const char *device_id, const char *action,
                        const char *params_json, const char *command_id);

// POST a scene action: /api/v1/scenes/{scene_id}/actions
// scene_id is the engine scene id (typically "master"); action TURN_ON / TURN_OFF.
void http_client_scene_action(const char *scene_id, const char *action,
                              const char *command_id);

bool http_client_is_connected(void);

void http_client_set_state_cb(http_state_cb_t cb);

// Fetch catalog + current states immediately (used to (re)build g_devices[]).
// Returns true if the catalog was parsed successfully.
bool http_fetch_catalog(void);
void http_fetch_states(void);

void http_client_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif