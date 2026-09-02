#ifndef AITSM_MEASUREMENT_SERVICE_H_
#define AITSM_MEASUREMENT_SERVICE_H_

/** Initialize the periodic sensor sampling service. */
int aitsm_measurement_service_init(void);

/** Start sampling after MQTT has connected. */
void aitsm_measurement_service_mqtt_connected(void);

/** Stop active sampling while retaining unacknowledged measurements. */
void aitsm_measurement_service_mqtt_disconnected(void);

/** Complete or retry the currently pending MQTT transmission. */
void aitsm_measurement_service_publish_result(int result);

#endif /* AITSM_MEASUREMENT_SERVICE_H_ */
