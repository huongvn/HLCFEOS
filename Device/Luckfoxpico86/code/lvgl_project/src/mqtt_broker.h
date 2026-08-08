#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_msg_cb_t)(const char *topic, const char *payload);
typedef void (*mqtt_connect_cb_t)(void);

void mqtt_broker_connect(void);
void mqtt_broker_cleanup(void);
bool mqtt_broker_is_connected(void);
void mqtt_broker_poll(void);
void mqtt_broker_publish(const char *topic, const char *payload);
void mqtt_broker_subscribe(const char *topic, int qos);

void mqtt_broker_set_msg_cb(mqtt_msg_cb_t cb);
void mqtt_broker_set_connect_cb(mqtt_connect_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif
