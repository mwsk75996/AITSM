#ifndef AITSM_MEASUREMENT_SOURCE_H_
#define AITSM_MEASUREMENT_SOURCE_H_
#include <stdint.h>
struct aitsm_measurement;
/** Read local sensors and valid UTC time; performs no MQTT transmission. */
int aitsm_measurement_read(struct aitsm_measurement *measurement);

/** Current UTC time in milliseconds, or a negative error if the time is not valid yet. */
int aitsm_measurement_time_ms(int64_t *timestamp_ms);
#endif
