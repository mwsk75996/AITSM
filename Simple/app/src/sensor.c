#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <date_time.h>
#include <modem/modem_info.h>

#include "sensor.h"

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

#define BATTERY_EMPTY_MV 3200
#define BATTERY_FULL_MV 4200

static const struct device *const battery_device = DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

/* Given once the date_time library has UTC time from the modem. */
static K_SEM_DEFINE(time_ready, 0, 1);

static void date_time_handler(const struct date_time_evt *event)
{
	if (event->type != DATE_TIME_NOT_OBTAINED) {
		k_sem_give(&time_ready);
	}
}

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

int aitsm_sensor_init(void)
{
	if (!device_is_ready(battery_device)) {
		LOG_ERR("nPM1300-batterimåleren er ikke klar");
		return -ENODEV;
	}

	date_time_register_handler(date_time_handler);
	return 0;
}

void aitsm_sensor_wait_for_time(void)
{
	(void)k_sem_take(&time_ready, K_FOREVER);
	LOG_INF("UTC-tid modtaget fra netværket");
}

int aitsm_sensor_read(struct aitsm_measurement *measurement)
{
	struct sensor_value battery_voltage;
	int64_t timestamp_ms;
	int temperature_celsius;
	int err;

	err = date_time_now(&timestamp_ms);
	if (err != 0) {
		LOG_WRN("UTC-tid er ikke gyldig: %d", err);
		return err;
	}

	err = modem_info_get_temperature(&temperature_celsius);
	if (err != 0) {
		LOG_WRN("Kunne ikke læse modemtemperatur: %d", err);
		return err;
	}

	err = sensor_sample_fetch(battery_device);
	if (err == 0) {
		err = sensor_channel_get(battery_device, SENSOR_CHAN_GAUGE_VOLTAGE,
					 &battery_voltage);
	}
	if (err != 0) {
		LOG_WRN("Kunne ikke læse batterispænding: %d", err);
		return err;
	}

	measurement->timestamp = timestamp_ms / 1000;
	measurement->temperature_centi_celsius = temperature_celsius * 100;
	measurement->battery_centi_percent =
		battery_percent_from_voltage(sensor_value_to_milli(&battery_voltage));
	return 0;
}
