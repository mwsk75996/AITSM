#ifndef AITSM_MQTT_CLIENT_H_
#define AITSM_MQTT_CLIENT_H_

#include <stddef.h>

/** Initialize the TLS-enabled MQTT helper and its callbacks. */
int aitsm_mqtt_init(void);

/** Schedule an MQTT connection attempt after LTE registration. */
int aitsm_mqtt_connect(void);

/** Publish a payload through the shared MQTT client on the telemetry topic. */
int aitsm_mqtt_publish_payload(const char *payload, size_t payload_length);

#endif /* AITSM_MQTT_CLIENT_H_ */
