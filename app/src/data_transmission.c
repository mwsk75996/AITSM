#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <data_transmission.h>
#include <sparkplug.h>

LOG_MODULE_REGISTER(data_transmission, CONFIG_AITSM_LOG_LEVEL);

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
#define AITSM_MAX_MEASUREMENTS CONFIG_AITSM_BATCH_MAX_SAMPLES
#else
#define AITSM_MAX_MEASUREMENTS 1
#endif

/* A full buffer must always fit in one NDATA payload, otherwise it could
 * never be sent and the buffer would stay full.
 */
BUILD_ASSERT(AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE >=
		     AITSM_SPARKPLUG_NDATA_MAX_SIZE(AITSM_MAX_MEASUREMENTS),
	     "CONFIG_AITSM_TRANSMISSION_PAYLOAD_SIZE is too small for a full batch "
	     "of CONFIG_AITSM_BATCH_MAX_SAMPLES worst-case measurements");

static struct aitsm_measurement measurement_buffer[AITSM_MAX_MEASUREMENTS];
static size_t measurement_count;
static int64_t first_measurement_timestamp;
static uint8_t next_seq = 1;
static bool initialized;
static struct k_mutex measurement_mutex;

int aitsm_data_transmission_init(void)
{
	k_mutex_init(&measurement_mutex);
	k_mutex_lock(&measurement_mutex, K_FOREVER);
	memset(measurement_buffer, 0, sizeof(measurement_buffer));
	measurement_count = 0;
	first_measurement_timestamp = 0;
	next_seq = 1;
	initialized = true;
	k_mutex_unlock(&measurement_mutex);

	LOG_INF("Data transmission initialiseret: %s, måleinterval %d sekunder",
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
		"batch",
#else
		"single",
#endif
		CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS);

	return 0;
}

enum aitsm_transmission_mode aitsm_data_transmission_mode(void)
{
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	return AITSM_TRANSMISSION_MODE_BATCH;
#else
	return AITSM_TRANSMISSION_MODE_SINGLE;
#endif
}

int aitsm_data_transmission_add(const struct aitsm_measurement *measurement)
{
	if (measurement == NULL) {
		return -EINVAL;
	}

	if (!initialized) {
		return -EAGAIN;
	}

	k_mutex_lock(&measurement_mutex, K_FOREVER);

#if defined(CONFIG_AITSM_TRANSMISSION_SINGLE)
	if (measurement_count != 0) {
		k_mutex_unlock(&measurement_mutex);
		return -EBUSY;
	}
#endif

	if (measurement_count >= ARRAY_SIZE(measurement_buffer)) {
		k_mutex_unlock(&measurement_mutex);
		LOG_ERR("Målebuffer fuld; måling forkastet");
		return -ENOSPC;
	}

	if (measurement_count == 0) {
		first_measurement_timestamp = measurement->timestamp;
	}

	measurement_buffer[measurement_count++] = *measurement;
	k_mutex_unlock(&measurement_mutex);

	return 0;
}

bool aitsm_data_transmission_has_capacity(void)
{
	if (!initialized) {
		return false;
	}
	k_mutex_lock(&measurement_mutex, K_FOREVER);
	bool available = measurement_count < ARRAY_SIZE(measurement_buffer);
	k_mutex_unlock(&measurement_mutex);
	return available;
}

bool aitsm_data_transmission_should_flush(int64_t now)
{
	bool flush;

	if (!initialized) {
		return false;
	}

	k_mutex_lock(&measurement_mutex, K_FOREVER);

#if defined(CONFIG_AITSM_TRANSMISSION_SINGLE)
	flush = measurement_count > 0;
#else
	flush = measurement_count > 0 && (measurement_count >= ARRAY_SIZE(measurement_buffer) ||
		(now >= first_measurement_timestamp &&
		 /* The batch covers the interval: send when the next measurement would
		  * fall outside it, so 300 s at 15 s gives 20 measurements.
		  */
		 now - first_measurement_timestamp + CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS >=
			 CONFIG_AITSM_BATCH_INTERVAL_SECONDS));
#endif

	k_mutex_unlock(&measurement_mutex);
	return flush;
}

int aitsm_data_transmission_format(uint8_t *buffer, size_t capacity, size_t *length,
				   size_t *formatted_count)
{
	int err;

	if (buffer == NULL || length == NULL || formatted_count == NULL || capacity == 0) {
		return -EINVAL;
	}

	if (!initialized) {
		return -EAGAIN;
	}

	k_mutex_lock(&measurement_mutex, K_FOREVER);
	if (measurement_count == 0) {
		k_mutex_unlock(&measurement_mutex);
		return -ENODATA;
	}

	/* The seq is only advanced by commit, so a resend after a lost PUBACK
	 * carries the same seq as the first attempt.
	 */
	err = aitsm_sparkplug_encode_ndata(buffer, capacity, length, measurement_buffer,
					   measurement_count, next_seq,
					   aitsm_data_transmission_mode() ==
						   AITSM_TRANSMISSION_MODE_BATCH);
	if (err == 0) {
		*formatted_count = measurement_count;
	}

	k_mutex_unlock(&measurement_mutex);
	return err;
}

void aitsm_data_transmission_start_session(void)
{
	k_mutex_lock(&measurement_mutex, K_FOREVER);
	next_seq = 1;
	k_mutex_unlock(&measurement_mutex);
}

int aitsm_data_transmission_commit(size_t count)
{
	if (!initialized) {
		return -EAGAIN;
	}

	k_mutex_lock(&measurement_mutex, K_FOREVER);
	if (count > measurement_count) {
		k_mutex_unlock(&measurement_mutex);
		return -EINVAL;
	}

	if (count != 0) {
		memmove(measurement_buffer, &measurement_buffer[count],
			(measurement_count - count) * sizeof(measurement_buffer[0]));
		measurement_count -= count;
		next_seq = aitsm_sparkplug_next_seq(next_seq);
		first_measurement_timestamp = measurement_count == 0 ? 0 :
			measurement_buffer[0].timestamp;
	}

	k_mutex_unlock(&measurement_mutex);
	return 0;
}
