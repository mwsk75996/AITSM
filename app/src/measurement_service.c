#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <date_time.h>
#include <modem/modem_info.h>

#include <data_transmission.h>
#include <measurement_service.h>
#include <mqtt_client.h>

LOG_MODULE_REGISTER(measurement_service, LOG_LEVEL_INF);

#define BATTERY_EMPTY_MV 3200
#define BATTERY_FULL_MV 4200

static const struct device *const battery_device =
	DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

static char measurement_payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
static size_t pending_measurement_count;
static bool publish_in_flight;
static bool service_running;

static void measurement_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(measurement_work, measurement_work_handler);

static uint16_t battery_percent_from_voltage(int64_t voltage_mv)
{
	if (voltage_mv <= BATTERY_EMPTY_MV) {
		return 0;
	}
	if (voltage_mv >= BATTERY_FULL_MV) {
		return 10000;
	}

	return (uint16_t)(((voltage_mv - BATTERY_EMPTY_MV) * 10000) /
			  (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

static int read_measurement(struct aitsm_measurement *measurement)
{
	struct sensor_value battery_voltage;
	int64_t timestamp_ms;
	int temperature_celsius;
	int err;

	if (!device_is_ready(battery_device)) {
		LOG_ERR("nPM1300-batterien er ikke klar");
		return -ENODEV;
	}

	err = date_time_now(&timestamp_ms);
	if (err != 0) {
		LOG_WRN("UTC-tid er endnu ikke gyldig: %d", err);
		return err;
	}

	err = modem_info_get_temperature(&temperature_celsius);
	if (err != 0) {
		LOG_WRN("Kunne ikke læse modemtemperatur: %d", err);
		return err;
	}

	err = sensor_sample_fetch(battery_device);
	if (err != 0) {
		LOG_WRN("Kunne ikke læse nPM1300-batteriet: %d", err);
		return err;
	}

	err = sensor_channel_get(battery_device, SENSOR_CHAN_GAUGE_VOLTAGE,
				 &battery_voltage);
	if (err != 0) {
		LOG_WRN("Kunne ikke hente batterispænding: %d", err);
		return err;
	}

	measurement->timestamp = timestamp_ms / 1000;
	measurement->temperature_centi_celsius = temperature_celsius * 100;
	measurement->battery_centi_percent = battery_percent_from_voltage(
		sensor_value_to_milli(&battery_voltage));

	return 0;
}

static int publish_buffer(void)
{
	size_t formatted_count;
	int err;

	if (publish_in_flight) {
		return 0;
	}

	err = aitsm_data_transmission_format(measurement_payload,
					     sizeof(measurement_payload), &formatted_count);
	if (err == -ENODATA) {
		return 0;
	}
	if (err != 0) {
		LOG_ERR("Kunne ikke formatere målepayload: %d", err);
		return err;
	}

	err = aitsm_mqtt_publish_payload(measurement_payload,
					 strlen(measurement_payload));
	if (err != 0) {
		LOG_WRN("Kunne ikke sende målepayload: %d", err);
		return err;
	}

	pending_measurement_count = formatted_count;
	publish_in_flight = true;
	LOG_INF("Målepayload sendt; afventer MQTT-ack for %u måling(er)",
		formatted_count);
	return 0;
}

static void schedule_next_measurement(void)
{
	if (service_running) {
		(void)k_work_schedule(&measurement_work,
				      K_SECONDS(CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS));
	}
}

static void measurement_work_handler(struct k_work *work)
{
	struct aitsm_measurement measurement;
	int64_t now;
	int err;

	ARG_UNUSED(work);

	if (!service_running) {
		return;
	}

	/* Retry an unacknowledged payload before taking another measurement. */
	if (pending_measurement_count != 0) {
		(void)publish_buffer();
		schedule_next_measurement();
		return;
	}

	err = read_measurement(&measurement);
	if (err != 0) {
		schedule_next_measurement();
		return;
	}

	err = aitsm_data_transmission_add(&measurement);
	if (err != 0) {
		LOG_WRN("Måling kunne ikke lægges i buffer: %d", err);
		schedule_next_measurement();
		return;
	}

	now = measurement.timestamp;
	if (aitsm_data_transmission_should_flush(now)) {
		(void)publish_buffer();
	}

	schedule_next_measurement();
}

int aitsm_measurement_service_init(void)
{
	pending_measurement_count = 0;
	publish_in_flight = false;
	service_running = false;

	if (!device_is_ready(battery_device)) {
		LOG_WRN("nPM1300-batterien er ikke klar endnu; målinger prøves igen senere");
	}

	return 0;
}

void aitsm_measurement_service_mqtt_connected(void)
{
	service_running = true;
	(void)k_work_schedule(&measurement_work, K_NO_WAIT);
}

void aitsm_measurement_service_mqtt_disconnected(void)
{
	service_running = false;
	(void)k_work_cancel_delayable(&measurement_work);
	publish_in_flight = false;
}

void aitsm_measurement_service_publish_result(int result)
{
	if (!publish_in_flight) {
		return;
	}

	publish_in_flight = false;
	if (result == 0) {
		(void)aitsm_data_transmission_commit(pending_measurement_count);
		pending_measurement_count = 0;
		LOG_INF("Målepayload bekræftet og fjernet fra buffer");
	} else {
		LOG_WRN("Målepayload blev ikke bekræftet: %d; data bevares", result);
	}

	if (service_running) {
		(void)k_work_schedule(&measurement_work, K_NO_WAIT);
	}
}
