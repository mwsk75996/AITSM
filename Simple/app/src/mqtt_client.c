#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/sys/atomic.h>

#include <modem/modem_key_mgmt.h>
#include <net/mqtt_helper.h>

#include "mqtt_client.h"
/* Generated at build time from AITSM_MQTT_USERNAME and AITSM_MQTT_PASSWORD. */
#include "mqtt_credentials.h"

LOG_MODULE_REGISTER(mqtt_client, LOG_LEVEL_INF);

/* CA certificate used by the modem to verify the broker's TLS certificate. */
static const unsigned char ca_certificate[] = {
#include "ca-cert.pem"
};

static char hostname[] = CONFIG_AITSM_MQTT_HOSTNAME;
static char client_id[] = CONFIG_AITSM_SPARKPLUG_EDGE_NODE_ID;
static char username[] = AITSM_MQTT_USERNAME;
static char password[] = AITSM_MQTT_PASSWORD;

static aitsm_mqtt_handler_t mqtt_handler;
/* Message id of the publish that waits for its PUBACK; 0 when none is waiting. */
static atomic_t pending_message_id;

static void on_connack(enum mqtt_conn_return_code return_code, bool session_present)
{
	ARG_UNUSED(session_present);

	if (return_code != MQTT_CONNECTION_ACCEPTED) {
		/* mqtt_helper closes the socket and calls on_disconnect afterwards. */
		LOG_ERR("Brokeren afviste forbindelsen, kode: %d", return_code);
		return;
	}

	LOG_INF("Forbundet til %s over TLS", hostname);
	mqtt_handler(AITSM_MQTT_EVENT_CONNECTED);
}

static void on_disconnect(int result)
{
	LOG_WRN("MQTT-forbindelsen er lukket, resultat: %d", result);
	atomic_clear(&pending_message_id);
	mqtt_handler(AITSM_MQTT_EVENT_DISCONNECTED);
}

static void on_puback(uint16_t message_id, int result)
{
	if (result != 0 || !atomic_cas(&pending_message_id, message_id, 0)) {
		LOG_WRN("Uventet PUBACK, message id: %u, resultat: %d", message_id, result);
		return;
	}

	mqtt_handler(AITSM_MQTT_EVENT_PUBACK);
}

static int provision_ca_certificate(void)
{
	/* The modem stores the certificate under the sec tag that the TLS socket uses. */
	int err = modem_key_mgmt_write(CONFIG_MQTT_HELPER_SEC_TAG,
				       MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, ca_certificate,
				       sizeof(ca_certificate));

	if (err != 0) {
		LOG_ERR("CA-certifikatet kunne ikke gemmes i modemmet: %d", err);
	}
	return err;
}

int aitsm_mqtt_init(aitsm_mqtt_handler_t handler)
{
	struct mqtt_helper_cfg config = {
		.cb = {
			.on_connack = on_connack,
			.on_disconnect = on_disconnect,
			.on_puback = on_puback,
		},
	};
	int err;

	if (strlen(AITSM_MQTT_PASSWORD) == 0) {
		LOG_ERR("MQTT-password mangler; angiv AITSM_MQTT_PASSWORD ved build");
		return -EINVAL;
	}

	mqtt_handler = handler;

	err = provision_ca_certificate();
	if (err != 0) {
		return err;
	}

	err = mqtt_helper_init(&config);
	if (err != 0) {
		LOG_ERR("MQTT-klienten kunne ikke initialiseres: %d", err);
	}
	return err;
}

int aitsm_mqtt_connect(void)
{
	struct mqtt_helper_conn_params params = {
		.hostname = { .ptr = hostname, .size = sizeof(hostname) - 1 },
		.device_id = { .ptr = client_id, .size = sizeof(client_id) - 1 },
		.user_name = { .ptr = username, .size = sizeof(username) - 1 },
		.password = { .ptr = password, .size = sizeof(password) - 1 },
	};
	int err = mqtt_helper_connect(&params);

	if (err != 0) {
		LOG_ERR("Kunne ikke forbinde til %s: %d", hostname, err);
	}
	return err;
}

int aitsm_mqtt_disconnect(void)
{
	return mqtt_helper_disconnect();
}

int aitsm_mqtt_publish(const char *topic, const uint8_t *payload, size_t length)
{
	struct mqtt_publish_param param = {
		.message = {
			.topic = {
				.topic = { .utf8 = topic, .size = strlen(topic) },
				.qos = MQTT_QOS_1_AT_LEAST_ONCE,
			},
			.payload = { .data = (uint8_t *)payload, .len = length },
		},
		.message_id = mqtt_helper_msg_id_get(),
	};
	int err;

	/* Set before sending: the PUBACK can arrive before mqtt_helper_publish returns. */
	if (!atomic_cas(&pending_message_id, 0, param.message_id)) {
		return -EBUSY;
	}

	err = mqtt_helper_publish(&param);
	if (err != 0) {
		atomic_clear(&pending_message_id);
		LOG_WRN("MQTT-publish fejlede: %d", err);
	}
	return err;
}
