#ifndef AITSM_MEASUREMENT_SERVICE_H_
#define AITSM_MEASUREMENT_SERVICE_H_

#include <stdint.h>

/** Initialize the periodic sensor sampling service. */
int aitsm_measurement_service_init(void);


/** Start periodic sampling after the modem is initialized (nonblocking). */
int aitsm_measurement_service_start(void);

/** Cumulative skipped sample slots due to a full buffer; system workqueue only. */
uint32_t aitsm_measurement_service_dropped_samples(void);

/** Enable MQTT transmission and drain retained readings. */
void aitsm_measurement_service_mqtt_connected(void);

/** Stop transmission; sampling and retention continue during MQTT outages. */
void aitsm_measurement_service_mqtt_disconnected(void);

/** Complete or retry the currently pending MQTT transmission. */
void aitsm_measurement_service_publish_result(int result);

#endif /* AITSM_MEASUREMENT_SERVICE_H_ */
