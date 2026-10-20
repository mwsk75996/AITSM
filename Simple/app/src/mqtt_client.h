#ifndef AITSM_MQTT_CLIENT_H_
#define AITSM_MQTT_CLIENT_H_

#include <stddef.h>
#include <stdint.h>

/** Reported from the MQTT callbacks to the application. */
enum aitsm_mqtt_event {
	/** The broker accepted the connection. */
	AITSM_MQTT_EVENT_CONNECTED,
	/** The connection was closed or could not be established. */
	AITSM_MQTT_EVENT_DISCONNECTED,
	/** The broker acknowledged (PUBACK) the last published message. */
	AITSM_MQTT_EVENT_PUBACK,
};

typedef void (*aitsm_mqtt_handler_t)(enum aitsm_mqtt_event event);

/**
 * Store the CA certificate in the modem and initialize the MQTT client.
 * Must be called after the modem is initialized and before LTE is connected,
 * because the modem only accepts certificates while it is offline.
 */
int aitsm_mqtt_init(aitsm_mqtt_handler_t handler);

/**
 * Connect to the broker over TLS. Blocks during DNS lookup and TLS handshake.
 * Returns 0 when the request is sent; the result is then reported to the
 * handler as CONNECTED or DISCONNECTED.
 */
int aitsm_mqtt_connect(void);

/**
 * Close the connection. The result is reported to the handler as DISCONNECTED
 * when 0 is returned; otherwise there was no connection to close.
 */
int aitsm_mqtt_disconnect(void);

/**
 * Publish a message with QoS 1. Only one message can wait for its PUBACK at a
 * time; the acknowledgement is reported to the handler as PUBACK.
 */
int aitsm_mqtt_publish(const char *topic, const uint8_t *payload, size_t length);

#endif /* AITSM_MQTT_CLIENT_H_ */
