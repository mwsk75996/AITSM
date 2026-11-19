#include "temperature_sensor.h"

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

static OneWire one_wire(TEMPERATURE_SENSOR_PIN);
static DallasTemperature sensors(&one_wire);
static DeviceAddress address;
static uint32_t requested_at_ms;
static uint32_t conversion_ms;

bool temperature_sensor_init(void)
{
	sensors.begin();
	if (!sensors.getAddress(address, 0)) {
		return false;
	}
	sensors.setResolution(address, 12);
	/* Do not block in requestTemperatures(); Modbus must keep being served. */
	sensors.setWaitForConversion(false);
	conversion_ms = sensors.millisToWaitForConversion(12);
	return true;
}

void temperature_sensor_request(void)
{
	sensors.requestTemperaturesByAddress(address);
	requested_at_ms = millis();
}

bool temperature_sensor_ready(void)
{
	return millis() - requested_at_ms >= conversion_ms;
}

bool temperature_sensor_read(int16_t *centi_celsius)
{
	float celsius = sensors.getTempC(address);

	/* DEVICE_DISCONNECTED_C (-127) means no answer; 85 C is the power-on value. */
	if (celsius == DEVICE_DISCONNECTED_C || celsius == 85.0f) {
		return false;
	}
	*centi_celsius = (int16_t)lroundf(celsius * 100.0f);
	return true;
}
