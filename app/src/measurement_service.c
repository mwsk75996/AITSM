#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <data_transmission.h>
#include <measurement_service.h>
#include <measurement_source.h>
#include <mqtt_client.h>

LOG_MODULE_REGISTER(measurement_service, CONFIG_AITSM_LOG_LEVEL);

static char measurement_payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
/* State is owned by the system workqueue after initialization. Sampling has
 * its own cadence; MQTT availability only controls transmission.
 */
static size_t pending_measurement_count;
static uint32_t pending_publish_token;
static bool publish_in_flight;
static bool mqtt_connected;
static uint32_t dropped_samples;
static uint32_t drops_since_log;
static int64_t last_sample_timestamp;
static void measurement_work_handler(struct k_work *work);
static void publish_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(measurement_work, measurement_work_handler);
static K_WORK_DELAYABLE_DEFINE(publish_work, publish_work_handler);

/* Log hver indsamlet måling på info-niveau, så den kan følges lokalt. */
static void log_measurement(const struct aitsm_measurement *measurement)
{
	int temperature = measurement->temperature_centi_celsius;
	int battery = measurement->battery_centi_percent;

	LOG_INF("Måling indsamlet: temperatur %s%d.%02d C, batteri %s%d.%02d %%",
		temperature < 0 ? "-" : "", abs(temperature) / 100,
		abs(temperature) % 100,
		battery < 0 ? "-" : "", abs(battery) / 100, abs(battery) % 100);
}


static int publish_buffer(void)
{
	size_t formatted_count;
	if (!mqtt_connected || publish_in_flight) {
		return 0;
	}
	int err = aitsm_data_transmission_format(measurement_payload,
					       sizeof(measurement_payload), &formatted_count);
	if (err != 0) {
		if (err != -ENODATA) {
			LOG_ERR("Kunne ikke formatere målepayload: %d", err);
		}
		return err;
	}
	err = aitsm_mqtt_publish_payload(measurement_payload, strlen(measurement_payload),
					&pending_publish_token);
	if (err != 0) {
		LOG_WRN("Kunne ikke sende målepayload: %d", err);
		return err;
	}
	pending_measurement_count = formatted_count;
	publish_in_flight = true;
	LOG_INF("Målepayload lagt i MQTT-kø; afventer ack for %u måling(er)", formatted_count);
	return 0;
}

static void request_publish(k_timeout_t delay)
{
	if (mqtt_connected && !publish_in_flight) {
		/* Schedule keeps an existing retry deadline; new measurements must
		 * not turn one failed publish into a tight retry loop.
		 */
		(void)k_work_schedule(&publish_work, delay);
	}
}

static void publish_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int err = publish_buffer();
	if (err != 0 && err != -ENODATA) {
		request_publish(K_SECONDS(CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS));
	}
}

static void measurement_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	/* Sensor reads, PUBACK and reconnect do not shift the sampling period. */
	(void)k_work_schedule(&measurement_work,
			      K_SECONDS(CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS));
	if (!aitsm_data_transmission_has_capacity()) {
		if (dropped_samples != UINT32_MAX) {
			dropped_samples++;
		}
		if (drops_since_log == 0) {
			LOG_WRN("Målebuffer fuld; ældste data bevares, nye målepladser springes over");
		}
		if (drops_since_log != UINT32_MAX) {
			drops_since_log++;
		}
		/* No sensor/AT calls when there is nowhere to retain the result. */
		return;
	}
	if (drops_since_log != 0) {
		LOG_WRN("Buffer har plads igen; %u målepladser tabt, %u i alt",
			drops_since_log, dropped_samples);
		drops_since_log = 0;
	}
	struct aitsm_measurement measurement;
	int err = aitsm_measurement_read(&measurement);
	if (err != 0) {
		return;
	}
	err = aitsm_data_transmission_add(&measurement);
	if (err != 0) {
		LOG_WRN("Måling kunne ikke lægges i buffer: %d", err);
		return;
	}
	last_sample_timestamp = measurement.timestamp;
	log_measurement(&measurement);
	if (pending_measurement_count != 0 ||
	    aitsm_data_transmission_should_flush(last_sample_timestamp)) {
		request_publish(K_NO_WAIT);
	}
}

int aitsm_measurement_service_init(void)
{
	(void)k_work_cancel_delayable(&measurement_work);
	(void)k_work_cancel_delayable(&publish_work);
	pending_measurement_count = 0;
	pending_publish_token = 0;
	publish_in_flight = false;
	mqtt_connected = false;
	dropped_samples = drops_since_log = 0;
	last_sample_timestamp = 0;
	return 0;
}

int aitsm_measurement_service_start(void)
{
	return k_work_schedule(&measurement_work, K_NO_WAIT);
}

uint32_t aitsm_measurement_service_dropped_samples(void)
{
	return dropped_samples;
}

void aitsm_measurement_service_mqtt_connected(void)
{
	mqtt_connected = true;
	/* Drain retained offline data using the connection just established. */
	request_publish(K_NO_WAIT);
}

void aitsm_measurement_service_mqtt_disconnected(void)
{
	mqtt_connected = false;
	(void)k_work_cancel_delayable(&publish_work);
	publish_in_flight = false;
	/* Sampling continues; no retained or in-flight measurements are removed. */
}

bool aitsm_measurement_service_publish_result(uint32_t token, int result)
{
	if (!publish_in_flight || token != pending_publish_token) {
		return false;
	}
	publish_in_flight = false;
	if (result == 0) {
		/* Only the prefix copied into the acknowledged payload is removed;
		 * readings collected while waiting for PUBACK remain buffered.
		 */
		(void)aitsm_data_transmission_commit(pending_measurement_count);
		pending_measurement_count = 0;
		LOG_INF("Målepayload bekræftet og fjernet fra buffer");
		if (aitsm_data_transmission_should_flush(last_sample_timestamp)) {
			request_publish(K_NO_WAIT);
		}
	} else {
		LOG_WRN("Målepayload blev ikke bekræftet: %d; data bevares", result);
		request_publish(K_SECONDS(CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS));
	}
	return true;
}
