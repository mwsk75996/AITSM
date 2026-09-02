#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_controller.h>
#include <led_status.h>
#include <measurement_service.h>
#include <mqtt_client.h>

LOG_MODULE_REGISTER(app_controller, LOG_LEVEL_INF);

#define AITSM_APP_EVENT_QUEUE_LENGTH 16

struct aitsm_app_event {
	enum aitsm_app_event_type type;
	int value;
};

K_MSGQ_DEFINE(aitsm_app_event_queue,
	      sizeof(struct aitsm_app_event),
	      AITSM_APP_EVENT_QUEUE_LENGTH,
	      4);

static void handle_event(const struct aitsm_app_event *event)
{
	switch (event->type) {
	case AITSM_APP_EVENT_LTE_SEARCHING:
		(void)led_status_set(LED_STATUS_CONNECTING);
		break;
	case AITSM_APP_EVENT_LTE_CONNECTED:
		(void)led_status_set(LED_STATUS_CONNECTED);
		(void)aitsm_mqtt_connect();
		break;
	case AITSM_APP_EVENT_LTE_DISCONNECTED:
		(void)led_status_set(LED_STATUS_DISCONNECTED);
		break;
	case AITSM_APP_EVENT_MQTT_CONNECTED:
		LOG_INF("Cloud MQTT event modtaget: forbundet");
		aitsm_measurement_service_mqtt_connected();
		break;
	case AITSM_APP_EVENT_MQTT_DISCONNECTED:
		LOG_WRN("Cloud MQTT event modtaget: afbrudt (%d)", event->value);
		aitsm_measurement_service_mqtt_disconnected();
		break;
	case AITSM_APP_EVENT_MQTT_ERROR:
		LOG_ERR("Cloud MQTT-fejl modtaget af applikationen: %d", event->value);
		break;
	case AITSM_APP_EVENT_MQTT_PUBLISH_RESULT:
		aitsm_measurement_service_publish_result(event->value);
		if (event->value == 0) {
			LOG_INF("Cloud MQTT publish-event modtaget: succes");
		} else {
			LOG_ERR("Cloud MQTT publish-event modtaget: fejl (%d)",
				event->value);
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

int aitsm_app_post_event(enum aitsm_app_event_type type, int value)
{
	const struct aitsm_app_event event = {
		.type = type,
		.value = value,
	};
	int err = k_msgq_put(&aitsm_app_event_queue, &event, K_NO_WAIT);
	if (err != 0) {
		LOG_ERR("Applikations-eventkø er fuld, event tabt: %d", type);
		return err;
	}

	return k_work_submit(&aitsma_app_event_work);
}
