#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>

#include <net/mqtt_helper.h>

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

static char hostname[] = AITSM_MQTT_HOSTNAME;
static char client_id[] = AITSM_MQTT_CLIENT_ID;
static char username[] = AITSM_MQTT_USERNAME;
static char password[] = AITSM_MQTT_PASSWORD;

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
		return;
	}

	LOG_ERR("MQTT-forbindelse afvist, return code: %d", return_code);
}

static void mqtt_on_disconnect(int result)
{
	LOG_WRN("Cloud MQTT-forbindelse lukket, resultat: %d", result);
}

static void mqtt_on_error(enum mqtt_helper_error error)
{
	LOG_ERR("MQTT-helper fejl: %d", error);
}

static struct mqtt_helper_cfg mqtt_cfg = {
	.cb = {
		.on_connack = mqtt_on_connack,
		.on_disconnect = mqtt_on_disconnect,
		.on_error = mqtt_on_error,
	},
};

static void mqtt_connect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = mqtt_helper_connect(&conn_params);
	if (err != 0) {
		LOG_ERR("Kunne ikke starte MQTT-forbindelse: %d", err);
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
