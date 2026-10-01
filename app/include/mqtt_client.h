#ifndef AITSM_MQTT_CLIENT_H_
#define AITSM_MQTT_CLIENT_H_

#include <stddef.h>

/** Initialize the TLS-enabled MQTT helper and its callbacks. */
int aitsm_mqtt_init(void);

/** Schedule an MQTT connection attempt after LTE registration. */
int aitsm_mqtt_connect(void);

/** Copy and queue a payload on the dedicated MQTT worker.
 * Returns 0 when accepted, -EBUSY if a socket write is already pending.
 * Completion/failure is reported through AITSM_APP_EVENT_MQTT_PUBLISH_RESULT.
 * A successful socket write is only completed by the broker's PUBACK.
 */
int aitsm_mqtt_publish_payload(const char *payload, size_t payload_length);

#endif /* AITSM_MQTT_CLIENT_H_ */
