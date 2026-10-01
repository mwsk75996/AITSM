#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

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

BUILD_ASSERT(CONFIG_AITSM_MQTT_PUBACK_TIMEOUT_SECONDS <=
	     CONFIG_AITSM_MQTT_PUBACK_MAX_DELAY_SECONDS,
	     "PUBACK timeout must not exceed maximum retry delay");

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
static atomic_t active_publish_token;
static atomic_t next_publish_token;
/* Serialize timer updates with ACK/disconnect. An old ACK must never cancel
 * the timeout belonging to a newer publication after reconnect.
 */
static struct k_spinlock publish_state_lock;
static atomic_val_t publish_generation;
static uint32_t publish_token;
/* Delay is owned by the MQTT worker. The copied payload remains unchanged
 * until PUBACK; publish_pending additionally protects each socket write.
 */
static uint32_t puback_delay_seconds;
static void mqtt_puback_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(puback_timeout, mqtt_puback_timeout_handler);
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
	k_spinlock_key_t key = k_spin_lock(&publish_state_lock);
	atomic_clear(&helper_connected);
	atomic_clear(&connect_pending);
	atomic_clear(&active_publish_id);
	(void)k_work_cancel_delayable(&puback_timeout);
	atomic_inc(&generation);
	k_spin_unlock(&publish_state_lock, key);
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
	k_spinlock_key_t key = k_spin_lock(&publish_state_lock);
	const uint32_t token = (uint32_t)atomic_get(&active_publish_token);
	if (!atomic_get(&lte_available) ||
	    atomic_get(&active_publish_generation) != atomic_get(&generation) ||
	    !atomic_cas(&active_publish_id, message_id, 0)) {
		k_spin_unlock(&publish_state_lock, key);
		LOG_DBG("Ignorerer PUBACK fra tidligere publish/session: %u", message_id);
		return;
	}
	(void)k_work_cancel_delayable(&puback_timeout);
	k_spin_unlock(&publish_state_lock, key);
	if (result == 0) {
		LOG_INF("MQTT-målepayload bekræftet, message id: %u", message_id);
		(void)aitsm_app_post_publish_result(token, 0);
		return;
	}

	LOG_ERR("MQTT-målepayload blev ikke bekræftet, message id: %u, resultat: %d",
		message_id, result);
	(void)aitsm_app_post_publish_result(token, result);
}

static struct mqtt_helper_cfg mqtt_cfg = {
	.cb = {
		.on_connack = mqtt_on_connack,
		.on_disconnect = mqtt_on_disconnect,
		.on_puback = mqtt_on_puback,
		.on_error = mqtt_on_error,
	},
};

static int send_copied_payload(uint16_t message_id, bool duplicate)
{
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
		.message_id = message_id,
		.dup_flag = duplicate,
	};
	return mqtt_helper_publish(&param);
}

static void publish_sent(uint16_t message_id, atomic_val_t job_generation,
			 uint32_t token, int err)
{
	bool failed = false;
	k_spinlock_key_t key = k_spin_lock(&publish_state_lock);
	if (err != 0 && job_generation == atomic_get(&generation) &&
	    atomic_cas(&active_publish_id, message_id, 0)) {
		(void)k_work_cancel_delayable(&puback_timeout);
		failed = true;
	} else if (err == 0 && job_generation == atomic_get(&generation) &&
		   atomic_get(&active_publish_id) == message_id) {
		(void)k_work_reschedule_for_queue(&mqtt_workqueue, &puback_timeout,
						 K_SECONDS(puback_delay_seconds));
	}
	k_spin_unlock(&publish_state_lock, key);
	if (failed) {
		LOG_WRN("MQTT publish fejlede: %d", err);
		(void)aitsm_app_post_publish_result(token, err);
	}
	/* Keep payload ownership through all state/timer updates, including
	 * when an ACK arrived before the socket write returned.
	 */
	atomic_clear(&publish_pending);
}

static void mqtt_publish_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	const atomic_val_t job_generation = publish_generation;
	k_spinlock_key_t key = k_spin_lock(&publish_state_lock);
	if (!atomic_get(&lte_available) || !atomic_get(&helper_connected) ||
	    job_generation != atomic_get(&generation)) {
		k_spin_unlock(&publish_state_lock, key);
		atomic_clear(&publish_pending);
		return;
	}
	uint16_t message_id = mqtt_helper_msg_id_get();
	uint32_t token = publish_token;
	puback_delay_seconds = CONFIG_AITSM_MQTT_PUBACK_TIMEOUT_SECONDS;
	atomic_set(&active_publish_generation, job_generation);
	atomic_set(&active_publish_token, (atomic_val_t)token);
	atomic_set(&active_publish_id, message_id);
	k_spin_unlock(&publish_state_lock, key);
	int err = send_copied_payload(message_id, false);
	publish_sent(message_id, job_generation, token, err);
}

static void mqtt_puback_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!atomic_cas(&publish_pending, 0, 1)) {
		return;
	}
	uint16_t message_id = atomic_get(&active_publish_id);
	atomic_val_t job_generation = atomic_get(&active_publish_generation);
	uint32_t token = (uint32_t)atomic_get(&active_publish_token);
	if (!message_id || !atomic_get(&lte_available) || !atomic_get(&helper_connected) ||
	    job_generation != atomic_get(&generation)) {
		atomic_clear(&publish_pending);
		return;
	}
	LOG_WRN("PUBACK mangler; genforsøger samme målepayload, message id: %u", message_id);
	puback_delay_seconds = MIN(puback_delay_seconds * 2U,
				   CONFIG_AITSM_MQTT_PUBACK_MAX_DELAY_SECONDS);
	int err = send_copied_payload(message_id, true);
	publish_sent(message_id, job_generation, token, err);
}

static K_WORK_DEFINE(mqtt_publish_work, mqtt_publish_work_handler);

int aitsm_mqtt_publish_payload(const char *payload, size_t payload_length, uint32_t *token)
{
	if (payload == NULL || payload_length == 0 || token == NULL) {
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
	if (atomic_get(&active_publish_id) != 0) {
		atomic_clear(&publish_pending);
		return -EBUSY;
	}

	memcpy(publish_payload, payload, payload_length);
	publish_length = payload_length;
	publish_generation = atomic_get(&generation);
	publish_token = (uint32_t)atomic_inc(&next_publish_token) + 1U;
	*token = publish_token;
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
	k_spinlock_key_t key = k_spin_lock(&publish_state_lock);
	atomic_inc(&generation);
	atomic_clear(&active_publish_id);
	(void)k_work_cancel_delayable(&puback_timeout);
	k_spin_unlock(&publish_state_lock, key);
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
