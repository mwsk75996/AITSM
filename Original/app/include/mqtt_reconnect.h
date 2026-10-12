#ifndef AITSM_MQTT_RECONNECT_H_
#define AITSM_MQTT_RECONNECT_H_
#include <stdbool.h>
#include <stdint.h>

enum aitsm_reconnect_event {
	AITSM_RECONNECT_LTE_UP,
	AITSM_RECONNECT_LTE_DOWN,
	AITSM_RECONNECT_MQTT_UP,
	AITSM_RECONNECT_FAILURE,
	AITSM_RECONNECT_TIMER,
};
enum aitsm_reconnect_phase {
	AITSM_RECONNECT_OFFLINE,
	AITSM_RECONNECT_CONNECTING,
	AITSM_RECONNECT_WAITING,
	AITSM_RECONNECT_CONNECTED,
};
enum aitsm_reconnect_action {
	AITSM_RECONNECT_NONE,
	AITSM_RECONNECT_CONNECT,
	AITSM_RECONNECT_SCHEDULE,
	AITSM_RECONNECT_CANCEL,
};
struct aitsm_reconnect_result {
	enum aitsm_reconnect_action action;
	uint32_t delay_seconds;
};
/* Owned exclusively by the app controller on the system workqueue. */
struct aitsm_reconnect_state {
	bool lte_available;
	enum aitsm_reconnect_phase phase;
	uint32_t next_delay_seconds;
};
void aitsm_reconnect_init(struct aitsm_reconnect_state *state);
struct aitsm_reconnect_result aitsm_reconnect_handle(
	struct aitsm_reconnect_state *state, enum aitsm_reconnect_event event);
#endif
