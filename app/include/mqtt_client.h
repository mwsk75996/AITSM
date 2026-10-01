#ifndef AITSM_MQTT_CLIENT_H_
#define AITSM_MQTT_CLIENT_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/** Initialize the TLS-enabled MQTT helper and its callbacks. */
int aitsm_mqtt_init(void);

/** Schedule an MQTT connection attempt after LTE registration. */
int aitsm_mqtt_connect(void);

/** Gate queued connection attempts on LTE registration (nonblocking). */
void aitsm_mqtt_set_lte_available(bool available);

/** Invalidate the current session and queue a disconnect (nonblocking). */
int aitsm_mqtt_disconnect(void);

/** Copy and queue a payload on the dedicated MQTT worker.
 * Returns 0 when accepted, -EBUSY while writing or awaiting PUBACK.
 * token receives the identifier used to correlate its completion event.
 * Completion/failure is reported through AITSM_APP_EVENT_MQTT_PUBLISH_RESULT.
 * A successful socket write is only completed by the broker's PUBACK.
 */
int aitsm_mqtt_publish_payload(const char *payload, size_t payload_length, uint32_t *token);

#endif /* AITSM_MQTT_CLIENT_H_ */
