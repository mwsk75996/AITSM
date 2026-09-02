#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>

#include <net/mqtt_helper.h>

#include <app_controller.h>
#include <mqtt_client.h>

#if __has_include("mqtt_credentials.h")
#include "mqtt_credentials.h"
#else
/* The configuration test can compile without access to local credentials. */
#define AITSM_MQTT_USERNAME "thingy91x"
#define AITSM_MQTT_PASSWORD ""
#endif

LOG_MODULE_REGISTER(mqtt_client, LOG_LEVEL_INF);

#define AITSM_MQTT_HOSTNAME "aitsm.vps.webdock.cloud"
#define AITSM_MQTT_CLIENT_ID "thingy91x"
#define AITSM_MQTT_TOPIC "aitsm/thingy91x/telemetry"
#define AITSM_MQTT_TEST_PAYLOAD \
	"{\"device_id\":\"thingy91x\",\"temperature\":0.0," \
	"\"battery\":100.0,\"test\":true}"

static char hostname[] = AITSM_MQTT_HOSTNAME;
static char client_id[] = AITSM_MQTT_CLIENT_ID;
static char username[] = AITSM_MQTT_USERNAME;
static char password[] = AITSM_MQTT_PASSWORD;
static char publish_topic[] = AITSM_MQTT_TOPIC;
static char test_payload[] = AITSM_MQTT_TEST_PAYLOAD;

static void mqtt_publish_test_work_handler(struct k_work *work);
static K_WORK_DEFINE(mqtt_publish_test_work, mqtt_publish_test_work_handler);

static struct mqtt_helper_conn_params conn_params = {
	.hostname = {
		.ptr = hostname,
		.size = sizeof(hostname) - 1,
	},
	.device_id = {
		.ptr = client_id,
		.size = sizeof(client_id) - 1,
	},
	.user_name = {
		.ptr = username,
		.size = sizeof(username) - 1,
	},
	.password = {
		.ptr = password,
		.size = sizeof(password) - 1,
	},
};

static void mqtt_on_connack(enum mqtt_conn_return_code return_code,
				    bool session_present)
{
	if (return_code == MQTT_CONNECTION_ACCEPTED) {
		LOG_INF("Forbundet til cloud MQTT over TLS%s",
			session_present ? " med eksisterende session" : "");
		(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_CONNECTED, 0);
		(void)k_work_submit(&mqtt_publish_test_work);
		return;
	}

	LOG_ERR("MQTT-forbindelse afvist, return code: %d", return_code);
	(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_ERROR, return_code);
}

static void mqtt_on_disconnect(int result)
{
	LOG_WRN("Cloud MQTT-forbindelse lukket, resultat: %d", result);
	(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_DISCONNECTED, result);
}

static void mqtt_on_error(enum mqtt_helper_error error)
{
	LOG_ERR("MQTT-helper fejl: %d", error);
	(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_ERROR, error);
}

static void mqtt_on_puback(uint16_t message_id, int result)
{
	if (result == 0) {
		LOG_INF("MQTT-testbesked bekræftet, message id: %u", message_id);
		(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
		return;
	}

	LOG_ERR("MQTT-testbesked blev ikke bekræftet, message id: %u, resultat: %d",
		message_id, result);
	(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, result);
}

static struct mqtt_helper_cfg mqtt_cfg = {
	.cb = {
		.on_connack = mqtt_on_connack,
		.on_disconnect = mqtt_on_disconnect,
		.on_puback = mqtt_on_puback,
		.on_error = mqtt_on_error,
	},
};

static void mqtt_publish_test_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = aitsm_mqtt_publish_payload(test_payload, sizeof(test_payload) - 1);
	if (err != 0) {
		LOG_ERR("Kunne ikke sende MQTT-testbesked: %d", err);
		return;
	}

	LOG_INF("MQTT-testbesked sendt til %s", publish_topic);
}

int aitsm_mqtt_publish_payload(const char *payload, size_t payload_length)
{
	if (payload == NULL || payload_length == 0) {
		return -EINVAL;
	}

	struct mqtt_publish_param param = {
		.message = {
			.payload = {
				.data = (uint8_t *)payload,
				.len = payload_length,
			},
			.topic = {
				.qos = MQTT_QOS_1_AT_LEAST_ONCE,
				.topic = {
					.utf8 = publish_topic,
					.size = sizeof(publish_topic) - 1,
				},
			},
		},
		.message_id = mqtt_helper_msg_id_get(),
	};

	int err = mqtt_helper_publish(&param);
	return err;
}

static void mqtt_connect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = mqtt_helper_connect(&conn_params);
	if (err != 0) {
		LOG_ERR("Kunne ikke starte MQTT-forbindelse: %d", err);
		(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_ERROR, err);
	}
}

static K_WORK_DEFINE(mqtt_connect_work, mqtt_connect_work_handler);

int aitsm_mqtt_init(void)
{
	if (strlen(AITSM_MQTT_PASSWORD) == 0) {
		LOG_ERR("MQTT password mangler; angiv AITSM_MQTT_PASSWORD ved build");
		return -EINVAL;
	}

	int err = mqtt_helper_init(&mqtt_cfg);
	if (err != 0) {
		LOG_ERR("MQTT-helper initialisering fejlede: %d", err);
		return err;
	}

	LOG_INF("MQTT TLS-klient initialiseret til %s:8883", hostname);
	return 0;
}

int aitsm_mqtt_connect(void)
{
	return k_work_submit(&mqtt_connect_work);
}
