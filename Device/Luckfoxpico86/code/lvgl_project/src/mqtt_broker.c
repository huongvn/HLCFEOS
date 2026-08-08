#include "src/mqtt_broker.h"
#include "src/config.h"
#include "src/bms.h"
#include <stdio.h>
#include <string.h>
#include <mosquitto.h>

static struct mosquitto *g_mosq = NULL;
static bool g_mqtt_ready = false;

static mqtt_msg_cb_t     g_msg_cb     = NULL;
static mqtt_connect_cb_t g_connect_cb = NULL;

static void on_connect(struct mosquitto *m, void *userdata, int rc) {
    (void)userdata;
    if (rc != 0) {
        printf("[MQTT] Connection failed: %s (rc=%d)\n", mosquitto_connack_string(rc), rc);
        return;
    }
    printf("[MQTT] Connected to broker!\n");

    // Subscribe to BMS wildcard
    mosquitto_subscribe(m, NULL, "bms/#", 1);

    // Let registered modules subscribe their topics
    if (g_connect_cb) g_connect_cb();

    fflush(stdout);
}

static void on_disconnect(struct mosquitto *m, void *userdata, int rc) {
    (void)m; (void)userdata;
    if (rc != 0)
        printf("[MQTT] Unexpected disconnection (rc=%d), will auto-reconnect\n", rc);
    else
        printf("[MQTT] Disconnected cleanly\n");
    fflush(stdout);
}

static void on_message(struct mosquitto *m, void *userdata, const struct mosquitto_message *msg) {
    (void)m; (void)userdata;
    if (!msg || !msg->topic || !msg->payload || msg->payloadlen == 0) return;

    char payload_str[64];
    size_t len = (size_t)msg->payloadlen;
    if (len >= sizeof(payload_str)) len = sizeof(payload_str) - 1;
    memcpy(payload_str, msg->payload, len);
    payload_str[len] = '\0';

    g_bms_mqtt_updating = true;
    bms_handle_mqtt(msg->topic, payload_str);
    g_bms_mqtt_updating = false;
    if (g_msg_cb) g_msg_cb(msg->topic, payload_str);
    fflush(stdout);
}

void mqtt_broker_connect(void) {
    if (g_mosq) {
        int rc = mosquitto_reconnect(g_mosq);
        if (rc != MOSQ_ERR_SUCCESS) {
            printf("[MQTT] Reconnect failed: %s\n", mosquitto_strerror(rc));
            return;
        }
        printf("[MQTT] Reconnecting...\n");
        g_mqtt_ready = true;
        return;
    }

    mosquitto_lib_init();

    g_mosq = mosquitto_new("pico-lvgl", true, NULL);
    if (!g_mosq) {
        fprintf(stderr, "[MQTT] Failed to create mosquitto client\n");
        return;
    }

    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_disconnect_callback_set(g_mosq, on_disconnect);
    mosquitto_message_callback_set(g_mosq, on_message);
    mosquitto_reconnect_delay_set(g_mosq, 5, 30, false);

    app_config_t *cfg = config_get();
    int rc = mosquitto_connect(g_mosq, cfg->mqtt_broker, cfg->mqtt_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("[MQTT] Connect failed to %s: %s\n", cfg->mqtt_broker, mosquitto_strerror(rc));
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
        mosquitto_lib_cleanup();
        return;
    }
    printf("[MQTT] Connection initiated\n");
    g_mqtt_ready = true;
}

void mqtt_broker_cleanup(void) {
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }
    g_mqtt_ready = false;
    mosquitto_lib_cleanup();
    printf("[MQTT] Cleanup done\n");
}

bool mqtt_broker_is_connected(void) {
    return g_mqtt_ready && g_mosq != NULL;
}

void mqtt_broker_poll(void) {
    if (!g_mqtt_ready || !g_mosq) return;

    int rc = mosquitto_loop(g_mosq, 0, 1);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("[MQTT] Loop error: %s\n", mosquitto_strerror(rc));
        g_mqtt_ready = false;
        rc = mosquitto_reconnect(g_mosq);
        if (rc == MOSQ_ERR_SUCCESS) {
            g_mqtt_ready = true;
            printf("[MQTT] Reconnected successfully\n");
        } else {
            printf("[MQTT] Reconnect failed: %s\n", mosquitto_strerror(rc));
        }
    }
}

void mqtt_broker_publish(const char *topic, const char *payload) {
    if (!g_mqtt_ready || !g_mosq) {
        printf("[MQTT] Cannot publish: not connected\n");
        return;
    }
    int rc = mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 1, false);
    if (rc != MOSQ_ERR_SUCCESS)
        printf("[MQTT] Publish failed: %s\n", mosquitto_strerror(rc));
    else
        printf("[MQTT] Published to %s: %s\n", topic, payload);
}

void mqtt_broker_subscribe(const char *topic, int qos) {
    if (!g_mqtt_ready || !g_mosq) return;
    mosquitto_subscribe(g_mosq, NULL, topic, qos);
}

void mqtt_broker_set_msg_cb(mqtt_msg_cb_t cb) {
    g_msg_cb = cb;
}

void mqtt_broker_set_connect_cb(mqtt_connect_cb_t cb) {
    g_connect_cb = cb;
}
