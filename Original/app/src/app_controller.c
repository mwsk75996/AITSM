#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_controller.h>
#include <led_status.h>
#include <measurement_service.h>
#include <mqtt_client.h>
#include <mqtt_reconnect.h>

LOG_MODULE_REGISTER(app_controller, CONFIG_AITSM_LOG_LEVEL);

#define AITSM_APP_EVENT_QUEUE_LENGTH 16

static struct aitsm_reconnect_state reconnect;
static void reconnect_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(reconnect_work, reconnect_work_handler);

static void apply_reconnect(enum aitsm_reconnect_event event)
{
	struct aitsm_reconnect_result result = aitsm_reconnect_handle(&reconnect, event);
	switch (result.action) {
	case AITSM_RECONNECT_CONNECT: {
		int err = aitsm_mqtt_connect();
		if (err < 0) {
			(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_ERROR, err);
		}
		break;
	}
	case AITSM_RECONNECT_SCHEDULE:
		LOG_INF("MQTT-reconnect om %u sekunder", result.delay_seconds);
		(void)k_work_reschedule(&reconnect_work, K_SECONDS(result.delay_seconds));
		break;
	case AITSM_RECONNECT_CANCEL:
		(void)k_work_cancel_delayable(&reconnect_work);
		break;
	case AITSM_RECONNECT_NONE:
		break;
	}
}

static void reconnect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	apply_reconnect(AITSM_RECONNECT_TIMER);
}

void aitsm_app_controller_init(void)
{
	aitsm_reconnect_init(&reconnect);
}

static void lte_lost(void)
{
	aitsm_mqtt_set_lte_available(false);
	aitsm_measurement_service_mqtt_disconnected();
	apply_reconnect(AITSM_RECONNECT_LTE_DOWN);
}

struct aitsm_app_event {
	enum aitsm_app_event_type type;
	int value;
	uint32_t publish_token;
};

K_MSGQ_DEFINE(aitsm_app_event_queue,
	      sizeof(struct aitsm_app_event),
	      AITSM_APP_EVENT_QUEUE_LENGTH,
	      4);

static void handle_event(const struct aitsm_app_event *event)
{
	switch (event->type) {
	case AITSM_APP_EVENT_LTE_SEARCHING:
		lte_lost();
		(void)led_status_set(LED_STATUS_SEARCHING);
		break;
	case AITSM_APP_EVENT_LTE_CONNECTED:
		aitsm_mqtt_set_lte_available(true);
		if (reconnect.phase != AITSM_RECONNECT_CONNECTED) {
			(void)led_status_set(LED_STATUS_LTE_CONNECTED);
		}
		apply_reconnect(AITSM_RECONNECT_LTE_UP);
		break;
	case AITSM_APP_EVENT_LTE_DISCONNECTED:
		LOG_INF("LTE-afbrudt event behandlet: %d", event->value);
		lte_lost();
		(void)led_status_set(LED_STATUS_DISCONNECTED);
		break;
	case AITSM_APP_EVENT_MQTT_CONNECTED:
		if (!reconnect.lte_available) {
			(void)aitsm_mqtt_disconnect();
			break;
		}
		apply_reconnect(AITSM_RECONNECT_MQTT_UP);
		LOG_INF("Cloud MQTT event modtaget: forbundet");
		(void)led_status_set(LED_STATUS_MQTT_CONNECTED);
		aitsm_measurement_service_mqtt_connected();
		break;
	case AITSM_APP_EVENT_MQTT_DISCONNECTED:
		LOG_WRN("Cloud MQTT event modtaget: afbrudt (%d)", event->value);
		if (!reconnect.lte_available) {
			(void)led_status_set(LED_STATUS_DISCONNECTED);
		} else if (reconnect.phase != AITSM_RECONNECT_WAITING) {
			/* A duplicate disconnect must not overwrite a preceding error. */
			(void)led_status_set(LED_STATUS_LTE_CONNECTED);
		}
		aitsm_measurement_service_mqtt_disconnected();
		apply_reconnect(AITSM_RECONNECT_FAILURE);
		break;
	case AITSM_APP_EVENT_MQTT_ERROR:
		LOG_ERR("Cloud MQTT-fejl modtaget af applikationen: %d", event->value);
		aitsm_measurement_service_mqtt_disconnected();
		if (reconnect.lte_available) {
			(void)led_status_set(LED_STATUS_ERROR);
			(void)aitsm_mqtt_disconnect();
		}
		apply_reconnect(AITSM_RECONNECT_FAILURE);
		break;
	case AITSM_APP_EVENT_MQTT_PUBLISH_RESULT:
		if (!aitsm_measurement_service_publish_result(event->publish_token, event->value)) {
			break;
		}
		if (event->value == 0) {
			LOG_INF("Cloud MQTT publish-event modtaget: succes");
			(void)led_status_set(LED_STATUS_PUBLISH_OK);
		} else {
			LOG_ERR("Cloud MQTT publish-event modtaget: fejl (%d)",
				event->value);
			(void)led_status_set(LED_STATUS_PUBLISH_ERROR);
		}
		break;
	default:
		break;
	}
}

static void app_event_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	while (true) {
		struct aitsm_app_event event;

		if (k_msgq_get(&aitsm_app_event_queue, &event, K_NO_WAIT) != 0) {
			break;
		}

		handle_event(&event);
	}
}

K_WORK_DEFINE(aitsma_app_event_work, app_event_work_handler);

static int post_event(enum aitsm_app_event_type type, int value, uint32_t token)
{
	const struct aitsm_app_event event = {
		.type = type,
		.value = value,
		.publish_token = token,
	};
	int err = k_msgq_put(&aitsm_app_event_queue, &event, K_NO_WAIT);
	if (err != 0) {
		LOG_ERR("Applikations-eventkø er fuld, event tabt: %d", type);
		return err;
	}

	return k_work_submit(&aitsma_app_event_work);
}

int aitsm_app_post_event(enum aitsm_app_event_type type, int value)
{
	return post_event(type, value, 0);
}

int aitsm_app_post_publish_result(uint32_t token, int result)
{
	return post_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, result, token);
}
