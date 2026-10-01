#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <date_time.h>
#include <modem/modem_info.h>
#include <data_transmission.h>
#include <measurement_source.h>
LOG_MODULE_REGISTER(measurement_source, CONFIG_AITSM_LOG_LEVEL);
#define BATTERY_EMPTY_MV 3200
#define BATTERY_FULL_MV 4200
static const struct device *const battery_device =
	DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

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

int aitsm_measurement_read(struct aitsm_measurement *measurement)
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
