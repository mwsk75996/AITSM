#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <date_time.h>

#include "batch.h"
#include "led.h"
#include "mqtt_client.h"
#include "network.h"
#include "sensor.h"
#include "sparkplug.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Events that wake the application thread. */
#define EVENT_MEASUREMENT BIT(0)
#define EVENT_NETWORK BIT(1)
#define EVENT_MQTT_CONNECTED BIT(2)
#define EVENT_MQTT_DISCONNECTED BIT(3)
#define EVENT_MQTT_PUBACK BIT(4)
#define EVENT_ALL BIT_MASK(5)

#define MEASUREMENT_INTERVAL_MS (CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS * MSEC_PER_SEC)
#define MEASUREMENT_QUEUE_LENGTH 8
#define MEASUREMENT_STACK_SIZE 2048
#define MEASUREMENT_PRIORITY 7

#define RECONNECT_INITIAL_SECONDS 5
#define RECONNECT_MAX_SECONDS 60
/* A PUBACK normally arrives within 10 seconds on NB-IoT. */
#define PUBACK_TIMEOUT_SECONDS 120

#define TOPIC_SIZE 64
#define PAYLOAD_SIZE                                                                               \
	MAX(AITSM_SPARKPLUG_NDATA_MAX_SIZE(CONFIG_AITSM_BATCH_CAPACITY),                           \
	    AITSM_SPARKPLUG_NBIRTH_MAX_SIZE)

enum mqtt_state {
	MQTT_IDLE,
	MQTT_CONNECTING,
	MQTT_CONNECTED,
	MQTT_DISCONNECTING,
};

static K_EVENT_DEFINE(app_events);
/* Measurements travel from the measurement thread to the application thread. */
K_MSGQ_DEFINE(measurement_queue, sizeof(struct aitsm_measurement), MEASUREMENT_QUEUE_LENGTH, 8);

/* Everything below is only used by the application thread. */
static enum mqtt_state mqtt_state;
static uint32_t reconnect_delay_seconds = RECONNECT_INITIAL_SECONDS;
static int64_t reconnect_at_ms;
/* A Sparkplug session starts with an acknowledged NBIRTH; bdSeq counts the sessions. */
static bool birth_acknowledged;
static uint8_t bd_seq;
static uint8_t seq;
static bool awaiting_puback;
static int64_t puback_deadline_ms;
/* Number of measurements in the NDATA that waits for its PUBACK. */
static size_t sent_count;
static uint32_t skipped_measurements;
static uint8_t payload[PAYLOAD_SIZE];

/* The callbacks run in the LTE and MQTT threads; they only wake the application thread. */
static void network_handler(void)
{
	k_event_post(&app_events, EVENT_NETWORK);
}

static void mqtt_handler(enum aitsm_mqtt_event event)
{
	switch (event) {
	case AITSM_MQTT_EVENT_CONNECTED:
		k_event_post(&app_events, EVENT_MQTT_CONNECTED);
		break;
	case AITSM_MQTT_EVENT_DISCONNECTED:
		k_event_post(&app_events, EVENT_MQTT_DISCONNECTED);
		break;
	case AITSM_MQTT_EVENT_PUBACK:
		k_event_post(&app_events, EVENT_MQTT_PUBACK);
		break;
	}
}

static void measurement_thread(void *p1, void *p2, void *p3)
{
	int64_t next_ms;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Measurements need a UTC timestamp, which arrives when LTE is connected. */
	aitsm_sensor_wait_for_time();
	next_ms = k_uptime_get();

	while (true) {
		struct aitsm_measurement measurement;

		if (aitsm_sensor_read(&measurement) == 0) {
			if (k_msgq_put(&measurement_queue, &measurement, K_NO_WAIT) == 0) {
				k_event_post(&app_events, EVENT_MEASUREMENT);
			} else {
				LOG_WRN("Målekøen er fuld; måling sprunget over");
			}
		}

		/* Sleep until a fixed point in time, so the time spent reading the
		 * sensor does not shift the interval.
		 */
		next_ms += MEASUREMENT_INTERVAL_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

K_THREAD_DEFINE(measurement_thread_id, MEASUREMENT_STACK_SIZE, measurement_thread, NULL, NULL, NULL,
		MEASUREMENT_PRIORITY, 0, 0);

/* Move new measurements from the queue into the batch buffer. */
static void collect_measurements(void)
{
	struct aitsm_measurement measurement;

	while (k_msgq_get(&measurement_queue, &measurement, K_NO_WAIT) == 0) {
		if (aitsm_batch_add(&measurement) != 0) {
			skipped_measurements++;
			LOG_WRN("Batchbufferen er fuld; måling sprunget over (%u i alt)",
				skipped_measurements);
			continue;
		}

		LOG_INF("Måling %u/%u: temperatur %d.%02d C, batteri %u.%02u %%",
			(unsigned int)aitsm_batch_count(), CONFIG_AITSM_BATCH_CAPACITY,
			measurement.temperature_centi_celsius / 100,
			abs(measurement.temperature_centi_celsius) % 100,
			measurement.battery_centi_percent / 100U,
			measurement.battery_centi_percent % 100U);
	}
}

static void handle_puback(void)
{
	if (!awaiting_puback) {
		return;
	}
	awaiting_puback = false;

	if (!birth_acknowledged) {
		birth_acknowledged = true;
		/* NBIRTH has seq 0, so the first NDATA of the session has seq 1. */
		seq = 1;
		LOG_INF("NBIRTH bekræftet af brokeren");
		return;
	}

	/* Only now is the batch removed; until the PUBACK it stays in the buffer. */
	aitsm_batch_remove(sent_count);
	seq = aitsm_sparkplug_next_seq(seq);
	LOG_INF("NDATA bekræftet; %u målinger fjernet fra bufferen", (unsigned int)sent_count);
}

/* Wait 5, 10, 20, 40 and then at most 60 seconds between connection attempts. */
static void schedule_reconnect(void)
{
	mqtt_state = MQTT_IDLE;
	reconnect_at_ms = k_uptime_get() + reconnect_delay_seconds * MSEC_PER_SEC;
	LOG_INF("Nyt MQTT-forsøg om %u s", reconnect_delay_seconds);
	reconnect_delay_seconds = MIN(reconnect_delay_seconds * 2U, RECONNECT_MAX_SECONDS);
}

/*
 * A connection can be dead without the modem reporting an error. A missing
 * PUBACK is the sign: close the connection, so the normal reconnect takes over
 * and the batch is sent again.
 */
static void check_puback_timeout(void)
{
	if (!awaiting_puback || k_uptime_get() < puback_deadline_ms) {
		return;
	}

	LOG_WRN("Ingen PUBACK efter %d s; lukker MQTT-forbindelsen", PUBACK_TIMEOUT_SECONDS);
	awaiting_puback = false;
	mqtt_state = MQTT_DISCONNECTING;
	if (aitsm_mqtt_disconnect() != 0) {
		/* Nothing to close, so no callback will come; continue as disconnected. */
		k_event_post(&app_events, EVENT_MQTT_DISCONNECTED);
	}
}

static void update_connection(uint32_t events)
{
	if ((events & EVENT_MQTT_CONNECTED) != 0U && mqtt_state == MQTT_CONNECTING) {
		mqtt_state = MQTT_CONNECTED;
		reconnect_delay_seconds = RECONNECT_INITIAL_SECONDS;
	}

	if ((events & EVENT_MQTT_DISCONNECTED) != 0U && mqtt_state != MQTT_IDLE) {
		/* Unacknowledged measurements stay in the batch and are sent again. */
		awaiting_puback = false;
		birth_acknowledged = false;
		bd_seq = aitsm_sparkplug_next_seq(bd_seq);
		schedule_reconnect();
	}

	if (mqtt_state == MQTT_IDLE && aitsm_network_is_connected() &&
	    k_uptime_get() >= reconnect_at_ms) {
		LOG_INF("Forbinder til MQTT-brokeren");
		if (aitsm_mqtt_connect() == 0) {
			mqtt_state = MQTT_CONNECTING;
		} else {
			schedule_reconnect();
		}
	}
}

static int publish(enum aitsm_sparkplug_message_type type, size_t length)
{
	char topic[TOPIC_SIZE];
	int err = aitsm_sparkplug_topic(topic, sizeof(topic), type);

	if (err == 0) {
		err = aitsm_mqtt_publish(topic, payload, length);
	}
	if (err == 0) {
		awaiting_puback = true;
		puback_deadline_ms = k_uptime_get() + PUBACK_TIMEOUT_SECONDS * MSEC_PER_SEC;
	}
	return err;
}

/* Send NBIRTH first in a new session, and afterwards NDATA when a batch is ready. */
static void publish_next(void)
{
	size_t length;
	int err;

	if (mqtt_state != MQTT_CONNECTED || awaiting_puback) {
		return;
	}

	if (!birth_acknowledged) {
		int64_t now_ms = 0;

		(void)date_time_now(&now_ms);
		err = aitsm_sparkplug_encode_nbirth(payload, sizeof(payload), &length,
						    (uint64_t)now_ms, bd_seq);
		if (err == 0) {
			err = publish(AITSM_SPARKPLUG_NBIRTH, length);
		}
		if (err == 0) {
			LOG_INF("NBIRTH sendt (bdSeq %u); venter på PUBACK", bd_seq);
		}
	} else if (aitsm_batch_is_ready()) {
		sent_count = aitsm_batch_count();
		err = aitsm_sparkplug_encode_ndata(payload, sizeof(payload), &length,
						   aitsm_batch_data(), sent_count, seq);
		if (err == 0) {
			err = publish(AITSM_SPARKPLUG_NDATA, length);
		}
		if (err == 0) {
			LOG_INF("NDATA sendt: %u målinger, %u bytes, seq %u; venter på PUBACK",
				(unsigned int)sent_count, (unsigned int)length, seq);
		}
	} else {
		return;
	}

	if (err != 0) {
		/* The data stays in the buffer and is tried again at the next wake-up. */
		LOG_WRN("Afsendelse fejlede: %d", err);
	}
}

/* The LED shows how far the device is: network, broker and sending. */
static void update_led(void)
{
	if (awaiting_puback) {
		/* White: a message is sent and waits for its PUBACK. */
		aitsm_led_set(AITSM_LED_WHITE);
	} else if (mqtt_state == MQTT_CONNECTED) {
		/* Green: connected to the broker. */
		aitsm_led_set(AITSM_LED_GREEN);
	} else if (aitsm_network_is_connected()) {
		/* Blue: on the NB-IoT network, but not connected to the broker. */
		aitsm_led_set(AITSM_LED_BLUE);
	} else {
		/* Yellow: searching for the network. */
		aitsm_led_set(AITSM_LED_YELLOW);
	}
}

/* Sleep until an event arrives, a PUBACK is overdue or the next connection attempt is due. */
static k_timeout_t next_wakeup(void)
{
	if (awaiting_puback) {
		return K_TIMEOUT_ABS_MS(puback_deadline_ms);
	}
	if (mqtt_state == MQTT_IDLE && aitsm_network_is_connected()) {
		return K_TIMEOUT_ABS_MS(reconnect_at_ms);
	}
	return K_FOREVER;
}

int main(void)
{
	int err;

	LOG_INF("AITSM Simple starter");

	err = aitsm_led_init();
	if (err == 0) {
		err = aitsm_network_init(network_handler);
	}
	if (err == 0) {
		err = aitsm_sensor_init();
	}
	if (err == 0) {
		err = aitsm_mqtt_init(mqtt_handler);
	}
	if (err == 0) {
		err = aitsm_network_connect();
	}
	if (err != 0) {
		LOG_ERR("Opstart fejlede: %d", err);
		/* Red: the device could not start. */
		aitsm_led_set(AITSM_LED_RED);
		return err;
	}

	/* The main thread continues as the application thread. */
	while (true) {
		uint32_t events;

		update_led();
		events = k_event_wait_safe(&app_events, EVENT_ALL, false, next_wakeup());

		collect_measurements();
		if ((events & EVENT_MQTT_PUBACK) != 0U) {
			handle_puback();
		}
		check_puback_timeout();
		update_connection(events);
		publish_next();
	}

	return 0;
}
