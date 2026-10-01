#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/sys/atomic.h>

#include <net/mqtt_helper.h>

#include <app_controller.h>
#include <mqtt_client.h>
#include <data_transmission.h>

#if __has_include("mqtt_credentials.h")
#include "mqtt_credentials.h"
#else
/* The configuration test can compile without access to local credentials. */
#define AITSM_MQTT_USERNAME "thingy91x"
#define AITSM_MQTT_PASSWORD ""
#endif

LOG_MODULE_REGISTER(mqtt_client, CONFIG_AITSM_LOG_LEVEL);

#define AITSM_MQTT_HOSTNAME "aitsm.vps.webdock.cloud"
#define AITSM_MQTT_CLIENT_ID "thingy91x"
#define AITSM_MQTT_TOPIC "aitsm/thingy91x/telemetry"
static char hostname[] = AITSM_MQTT_HOSTNAME;
static char client_id[] = AITSM_MQTT_CLIENT_ID;
static char username[] = AITSM_MQTT_USERNAME;
static char password[] = AITSM_MQTT_PASSWORD;
static char publish_topic[] = AITSM_MQTT_TOPIC;
static struct k_work_q mqtt_workqueue;
static K_THREAD_STACK_DEFINE(mqtt_workqueue_stack,
			    CONFIG_AITSM_MQTT_WORKQUEUE_STACK_SIZE);
static bool initialized;
/* Owned by the MQTT worker from submission until mqtt_helper_publish returns.
 * Copying the payload prevents measurement_service from reusing its buffer
 * while a socket write is still reading it. The atomic also bounds the queue
 * to one pending publish and provides the cross-thread memory barrier.
 */
static atomic_t publish_pending;
static char publish_payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
static size_t publish_length;
static atomic_t lte_available;
static atomic_t helper_connected;
static atomic_t connect_pending;
static atomic_t generation;
static atomic_t connect_generation;
static atomic_t active_publish_id;
static atomic_t active_publish_generation;
static atomic_val_t publish_generation;
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
	atomic_clear(&connect_pending);
	if (return_code == MQTT_CONNECTION_ACCEPTED) {
		atomic_set(&helper_connected, 1);
		if (!atomic_get(&lte_available) ||
		    atomic_get(&connect_generation) != atomic_get(&generation)) {
			/* LTE was lost while DNS/TCP/TLS was in progress. */
			(void)aitsm_mqtt_disconnect();
			return;
		}
		LOG_INF("Forbundet til cloud MQTT over TLS%s",
			session_present ? " med eksisterende session" : "");
		(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_CONNECTED, 0);
		return;
	}

	LOG_ERR("MQTT-forbindelse afvist, return code: %d", return_code);
	if (atomic_get(&lte_available) &&
	    atomic_get(&connect_generation) == atomic_get(&generation)) {
		(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_ERROR, return_code);
	}
}

static void mqtt_on_disconnect(int result)
{
	atomic_clear(&helper_connected);
	atomic_clear(&connect_pending);
	atomic_clear(&active_publish_id);
	atomic_inc(&generation);
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
	if (!atomic_get(&lte_available) ||
	    atomic_get(&active_publish_generation) != atomic_get(&generation) ||
	    !atomic_cas(&active_publish_id, message_id, 0)) {
		LOG_DBG("Ignorerer PUBACK fra tidligere publish/session: %u", message_id);
		return;
	}
	if (result == 0) {
		LOG_INF("MQTT-målepayload bekræftet, message id: %u", message_id);
		(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
		return;
	}

	LOG_ERR("MQTT-målepayload blev ikke bekræftet, message id: %u, resultat: %d",
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

static void mqtt_publish_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	const atomic_val_t job_generation = publish_generation;
	if (!atomic_get(&lte_available) || !atomic_get(&helper_connected) ||
	    job_generation != atomic_get(&generation)) {
		/* LTE/disconnect already stopped the measurement service. Never
		 * let a stale completion change a newer publish's state.
		 */
		atomic_clear(&publish_pending);
		return;
	}

	struct mqtt_publish_param param = {
		.message = {
			.payload = {
				.data = (uint8_t *)publish_payload,
				.len = publish_length,
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

	atomic_set(&active_publish_generation, job_generation);
	atomic_set(&active_publish_id, param.message_id);
	int err = mqtt_helper_publish(&param);
	atomic_clear(&publish_pending);
	if (err != 0 && job_generation == atomic_get(&generation) &&
	    atomic_cas(&active_publish_id, param.message_id, 0)) {
		LOG_WRN("MQTT publish fejlede: %d", err);
		(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, err);
	}
}

static K_WORK_DEFINE(mqtt_publish_work, mqtt_publish_work_handler);

int aitsm_mqtt_publish_payload(const char *payload, size_t payload_length)
{
	if (payload == NULL || payload_length == 0) {
		return -EINVAL;
	}
	if (payload_length > sizeof(publish_payload)) {
		return -EMSGSIZE;
	}
	if (!initialized) {
		return -EAGAIN;
	}
	if (!atomic_get(&lte_available) || !atomic_get(&helper_connected)) {
		return -ENOTCONN;
	}
	if (!atomic_cas(&publish_pending, 0, 1)) {
		return -EBUSY;
	}

	memcpy(publish_payload, payload, payload_length);
	publish_length = payload_length;
	publish_generation = atomic_get(&generation);
	int err = k_work_submit_to_queue(&mqtt_workqueue, &mqtt_publish_work);
	if (err < 0) {
		atomic_clear(&publish_pending);
		return err;
	}
	return 0;
}

static void mqtt_connect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!atomic_get(&lte_available)) {
		atomic_clear(&connect_pending);
		return;
	}
	atomic_val_t attempt_generation = atomic_get(&generation);
	atomic_set(&connect_generation, attempt_generation);

	int64_t started = k_uptime_get();
	LOG_INF("Starter MQTT-forbindelse på netværkskø");
	int err = mqtt_helper_connect(&conn_params);
	LOG_INF("MQTT-connect returnerede %d efter %lld ms", err,
		(long long)(k_uptime_get() - started));
	if (err != 0) {
		atomic_clear(&connect_pending);
		LOG_ERR("Kunne ikke starte MQTT-forbindelse: %d", err);
		if (atomic_get(&lte_available) && attempt_generation == atomic_get(&generation)) {
			(void)aitsm_app_post_event(AITSM_APP_EVENT_MQTT_ERROR, err);
		}
	}
}

static K_WORK_DEFINE(mqtt_connect_work, mqtt_connect_work_handler);

static void mqtt_disconnect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (atomic_get(&helper_connected)) {
		int err = mqtt_helper_disconnect();
		if (err != 0) {
			LOG_WRN("MQTT-disconnect returnerede %d", err);
		}
	}
}
static K_WORK_DEFINE(mqtt_disconnect_work, mqtt_disconnect_work_handler);

int aitsm_mqtt_disconnect(void)
{
	if (!initialized) {
		return -EAGAIN;
	}
	atomic_inc(&generation);
	atomic_clear(&active_publish_id);
	return k_work_submit_to_queue(&mqtt_workqueue, &mqtt_disconnect_work);
}

void aitsm_mqtt_set_lte_available(bool available)
{
	if (available) {
		atomic_set(&lte_available, 1);
	} else if (atomic_cas(&lte_available, 1, 0)) {
		(void)aitsm_mqtt_disconnect();
	}
}

int aitsm_mqtt_init(void)
{
	if (initialized) {
		return -EALREADY;
	}
	if (strlen(AITSM_MQTT_PASSWORD) == 0) {
		LOG_ERR("MQTT password mangler; angiv AITSM_MQTT_PASSWORD ved build");
		return -EINVAL;
	}

	int err = mqtt_helper_init(&mqtt_cfg);
	if (err != 0) {
		LOG_ERR("MQTT-helper initialisering fejlede: %d", err);
		return err;
	}

	k_work_queue_start(&mqtt_workqueue, mqtt_workqueue_stack,
			   K_THREAD_STACK_SIZEOF(mqtt_workqueue_stack),
			   CONFIG_AITSM_MQTT_WORKQUEUE_PRIORITY, NULL);
	(void)k_thread_name_set(&mqtt_workqueue.thread, "aitsm_mqtt");
	initialized = true;
	LOG_INF("MQTT TLS-klient initialiseret til %s:8883", hostname);
	return 0;
}

int aitsm_mqtt_connect(void)
{
	if (!initialized) {
		return -EAGAIN;
	}
	if (!atomic_get(&lte_available)) {
		return -ENETDOWN;
	}
	if (atomic_get(&helper_connected) || !atomic_cas(&connect_pending, 0, 1)) {
		return -EALREADY;
	}
	int err = k_work_submit_to_queue(&mqtt_workqueue, &mqtt_connect_work);
	if (err < 0) {
		atomic_clear(&connect_pending);
	}
	return err;
}
