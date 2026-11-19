#pragma once

#include <stdint.h>

/* DS18B20 on GPIO 4 with a 4.7 kOhm pull-up to 3.3 V. */
#define TEMPERATURE_SENSOR_PIN 4

/* Find the sensor and set 12-bit resolution. Returns false if none is found. */
bool temperature_sensor_init(void);

/* Start a conversion; it takes up to 750 ms at 12-bit resolution. */
void temperature_sensor_request(void);

/* True when the conversion started by temperature_sensor_request() is done. */
bool temperature_sensor_ready(void);

/* Read the result in hundredths of a degree. Returns false on a bad reading. */
bool temperature_sensor_read(int16_t *centi_celsius);
