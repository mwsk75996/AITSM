#ifndef AITSM_DATA_TRANSMISSION_H_
#define AITSM_DATA_TRANSMISSION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE \
	CONFIG_AITSM_TRANSMISSION_PAYLOAD_SIZE

/** Selected way of grouping measurements for MQTT transmission. */
enum aitsm_transmission_mode {
	AITSM_TRANSMISSION_MODE_SINGLE,
	AITSM_TRANSMISSION_MODE_BATCH,
};

/** One measurement in the transmission buffer. */
struct aitsm_measurement {
	/** Device timestamp in seconds since the device time base. */
	int64_t timestamp;
	/** Temperature in hundredths of a degree Celsius. */
	int32_t temperature_centi_celsius;
	/** Battery percentage in hundredths of a percent. */
	uint16_t battery_centi_percent;
};

/** Initialize and empty the fixed-size transmission buffer. */
int aitsm_data_transmission_init(void);

/** Return the Kconfig-selected transmission mode. */
enum aitsm_transmission_mode aitsm_data_transmission_mode(void);

/** Store a measurement without allocating memory dynamically. */
int aitsm_data_transmission_add(const struct aitsm_measurement *measurement);

/** Return whether the current buffer should be sent now. */
bool aitsm_data_transmission_should_flush(int64_t now);

/**
 * Serialize the current measurements into the transport payload.
 *
 * The JSON envelope is deliberately kept behind this API so the serializer can
 * later be replaced by SparkplugB without changing the buffer or MQTT client.
 */
int aitsm_data_transmission_format(char *buffer, size_t capacity,
				   size_t *measurement_count);

/** Remove measurements after their MQTT publish has been acknowledged. */
int aitsm_data_transmission_commit(size_t measurement_count);

#endif /* AITSM_DATA_TRANSMISSION_H_ */
