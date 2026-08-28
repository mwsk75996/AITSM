#ifndef AITSM_MQTT_CLIENT_H_
#define AITSM_MQTT_CLIENT_H_

/** Initialize the TLS-enabled MQTT helper and its callbacks. */
int aitsm_mqtt_init(void);

/** Schedule an MQTT connection attempt after LTE registration. */
int aitsm_mqtt_connect(void);

#endif /* AITSM_MQTT_CLIENT_H_ */
