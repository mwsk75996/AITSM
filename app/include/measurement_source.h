#ifndef AITSM_MEASUREMENT_SOURCE_H_
#define AITSM_MEASUREMENT_SOURCE_H_
struct aitsm_measurement;
/** Read local sensors and valid UTC time; performs no MQTT transmission. */
int aitsm_measurement_read(struct aitsm_measurement *measurement);
#endif
