#ifndef AITSM_APP_CONTROLLER_H_
#define AITSM_APP_CONTROLLER_H_
#include <stdint.h>

/** Events exchanged between asynchronous network modules and the application. */
enum aitsm_app_event_type {
	AITSM_APP_EVENT_LTE_SEARCHING,
	AITSM_APP_EVENT_LTE_CONNECTED,
	AITSM_APP_EVENT_LTE_DISCONNECTED,
	AITSM_APP_EVENT_MQTT_CONNECTED,
	AITSM_APP_EVENT_MQTT_DISCONNECTED,
	AITSM_APP_EVENT_MQTT_ERROR,
	AITSM_APP_EVENT_MQTT_PUBLISH_RESULT,
};

/** Post an event to the application workqueue without blocking a callback. */
int aitsm_app_post_event(enum aitsm_app_event_type type, int value);

/** Post completion for the exact application publish, even across reconnect. */
int aitsm_app_post_publish_result(uint32_t token, int result);

/** Initialize controller state before starting LTE or MQTT callbacks. */
void aitsm_app_controller_init(void);

#endif /* AITSM_APP_CONTROLLER_H_ */
