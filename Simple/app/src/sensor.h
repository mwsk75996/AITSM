#ifndef AITSM_SENSOR_H_
#define AITSM_SENSOR_H_

#include <stdint.h>

/** One measurement with the time it was taken. */
struct aitsm_measurement {
	/** UTC time in seconds since 1970. */
	int64_t timestamp;
	/** Temperature in hundredths of a degree Celsius. */
	int32_t temperature_centi_celsius;
	/** Battery level in hundredths of a percent. */
	uint16_t battery_centi_percent;
};

/** Check the battery sensor and start listening for UTC time from the network. */
int aitsm_sensor_init(void);

/** Block until the device has received UTC time from the network. */
void aitsm_sensor_wait_for_time(void);

/** Read modem temperature, battery level and UTC time. */
int aitsm_sensor_read(struct aitsm_measurement *measurement);

#endif /* AITSM_SENSOR_H_ */
