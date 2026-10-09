#include <zephyr/sys/util.h>
#include <mqtt_reconnect.h>

BUILD_ASSERT(CONFIG_AITSM_MQTT_RECONNECT_INITIAL_DELAY_SECONDS <=
	     CONFIG_AITSM_MQTT_RECONNECT_MAX_DELAY_SECONDS,
	     "Initial reconnect delay must not exceed maximum delay");

void aitsm_reconnect_init(struct aitsm_reconnect_state *state)
{
	*state = (struct aitsm_reconnect_state) {
		.phase = AITSM_RECONNECT_OFFLINE,
		.next_delay_seconds = CONFIG_AITSM_MQTT_RECONNECT_INITIAL_DELAY_SECONDS,
	};
}

struct aitsm_reconnect_result aitsm_reconnect_handle(
	struct aitsm_reconnect_state *state, enum aitsm_reconnect_event event)
{
	struct aitsm_reconnect_result result = { .action = AITSM_RECONNECT_NONE };
	switch (event) {
	case AITSM_RECONNECT_LTE_UP:
		if (!state->lte_available) {
			state->lte_available = true;
			state->phase = AITSM_RECONNECT_CONNECTING;
			result.action = AITSM_RECONNECT_CONNECT;
		}
		break;
	case AITSM_RECONNECT_LTE_DOWN:
		if (state->lte_available) {
			result.action = AITSM_RECONNECT_CANCEL;
		}
		aitsm_reconnect_init(state);
		break;
	case AITSM_RECONNECT_MQTT_UP:
		if (state->lte_available) {
			state->phase = AITSM_RECONNECT_CONNECTED;
			state->next_delay_seconds = CONFIG_AITSM_MQTT_RECONNECT_INITIAL_DELAY_SECONDS;
			result.action = AITSM_RECONNECT_CANCEL;
		}
		break;
	case AITSM_RECONNECT_FAILURE:
		/* ERROR and DISCONNECTED can describe the same failed attempt.
		 * Keep the original deadline and advance the backoff only once.
		 */
		if (state->lte_available && state->phase != AITSM_RECONNECT_WAITING) {
			state->phase = AITSM_RECONNECT_WAITING;
			result.action = AITSM_RECONNECT_SCHEDULE;
			result.delay_seconds = state->next_delay_seconds;
			state->next_delay_seconds = MIN(state->next_delay_seconds * 2U,
				CONFIG_AITSM_MQTT_RECONNECT_MAX_DELAY_SECONDS);
		}
		break;
	case AITSM_RECONNECT_TIMER:
		if (state->lte_available && state->phase == AITSM_RECONNECT_WAITING) {
			state->phase = AITSM_RECONNECT_CONNECTING;
			result.action = AITSM_RECONNECT_CONNECT;
		}
		break;
	}
	return result;
}
