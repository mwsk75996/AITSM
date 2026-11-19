#include "mqtt_link.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <mqtt_client.h>
#include <string.h>

#include "credentials.h"
#include "sparkplug.h"

#define EVENT_QUEUE_LENGTH 16
#define WILL_MAX_SIZE 32
#define KEEPALIVE_SECONDS 60

static esp_mqtt_client_handle_t client;
static QueueHandle_t events;
static uint8_t will[WILL_MAX_SIZE];

static void queue_event(enum mqtt_link_event_type type, int msg_id)
{
	struct mqtt_link_event event = {type, msg_id};

	/* If loop() is far behind, the oldest events are the least useful to lose. */
	if (xQueueSend(events, &event, 0) != pdTRUE) {
		struct mqtt_link_event discarded;

		xQueueReceive(events, &discarded, 0);
		xQueueSend(events, &event, 0);
	}
}

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

	(void)arg;
	(void)base;
	switch ((esp_mqtt_event_id_t)id) {
	case MQTT_EVENT_CONNECTED:
		queue_event(MQTT_LINK_CONNECTED, 0);
		break;
	case MQTT_EVENT_DISCONNECTED:
		queue_event(MQTT_LINK_DISCONNECTED, 0);
		break;
	case MQTT_EVENT_PUBLISHED:
		queue_event(MQTT_LINK_PUBLISHED, event->msg_id);
		break;
	case MQTT_EVENT_DATA:
		/* Only the Thingy's NDATA is subscribed; the payload itself is not needed. */
		if (event->current_data_offset == 0) {
			queue_event(MQTT_LINK_NORDIC_BATCH, 0);
		}
		break;
	case MQTT_EVENT_ERROR:
		if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
			Serial.printf("MQTT: forbindelse afvist (kode %d)\n",
				      event->error_handle->connect_return_code);
		} else if (event->error_handle->esp_tls_last_esp_err != 0) {
			Serial.printf("MQTT: TLS-fejl 0x%x\n",
				      event->error_handle->esp_tls_last_esp_err);
		}
		break;
	default:
		break;
	}
}

static void copy_will(const uint8_t *payload, size_t length)
{
	memcpy(will, payload, length < sizeof(will) ? length : sizeof(will));
}

bool mqtt_link_start(const char *will_topic, const uint8_t *will_payload, size_t will_length)
{
	static char uri[96];
	esp_mqtt_client_config_t config = {};

	if (will_length > sizeof(will)) {
		return false;
	}
	copy_will(will_payload, will_length);
	snprintf(uri, sizeof(uri), "mqtts://%s:%d", MQTT_HOST, MQTT_PORT);

	config.broker.address.uri = uri;
	/* Verify the broker's Let's Encrypt certificate against the built-in CA bundle. */
	config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
	config.credentials.username = MQTT_USER;
	config.credentials.client_id = SPARKPLUG_EDGE_NODE_ID;
	config.credentials.authentication.password = MQTT_PASSWORD;
	config.session.keepalive = KEEPALIVE_SECONDS;
	config.session.last_will.topic = will_topic;
	config.session.last_will.msg = (const char *)will;
	config.session.last_will.msg_len = (int)will_length;
	config.session.last_will.qos = 1;
	config.buffer.size = 1024;
	config.buffer.out_size = SPARKPLUG_MAX_PAYLOAD + 128;

	events = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(struct mqtt_link_event));
	client = esp_mqtt_client_init(&config);
	if (events == NULL || client == NULL) {
		return false;
	}
	esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, on_mqtt_event, NULL);
	return esp_mqtt_client_start(client) == ESP_OK;
}

void mqtt_link_set_will(const uint8_t *will_payload, size_t will_length)
{
	esp_mqtt_client_config_t config = {};

	if (will_length > sizeof(will)) {
		return;
	}
	/* The client keeps a pointer to will, so the new bytes are used on reconnect. */
	copy_will(will_payload, will_length);
	config.session.last_will.msg = (const char *)will;
	config.session.last_will.msg_len = (int)will_length;
	esp_mqtt_set_config(client, &config);
}

int mqtt_link_publish(const char *topic, const uint8_t *payload, size_t length)
{
	return esp_mqtt_client_publish(client, topic, (const char *)payload, (int)length, 1, 0);
}

bool mqtt_link_subscribe(const char *topic)
{
	return esp_mqtt_client_subscribe(client, topic, 1) >= 0;
}

bool mqtt_link_next_event(struct mqtt_link_event *event)
{
	return events != NULL && xQueueReceive(events, event, 0) == pdTRUE;
}
