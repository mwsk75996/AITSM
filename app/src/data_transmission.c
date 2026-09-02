#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <data_transmission.h>

LOG_MODULE_REGISTER(data_transmission, LOG_LEVEL_INF);

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
#define AITSM_MAX_MEASUREMENTS CONFIG_AITSM_BATCH_MAX_SAMPLES
#else
#define AITSM_MAX_MEASUREMENTS 1
#endif

static struct aitsm_measurement measurement_buffer[AITSM_MAX_MEASUREMENTS];
static size_t measurement_count;
static int64_t first_measurement_timestamp;
static bool initialized;
static struct k_mutex measurement_mutex;

static int append_measurement_json(char *buffer, size_t capacity, size_t *offset,
				   const struct aitsm_measurement *measurement,
				   bool include_device_id, bool add_comma)
{
	int64_t temperature = measurement->temperature_centi_celsius;
	uint64_t absolute_temperature = temperature < 0 ? (uint64_t)-temperature :
								 (uint64_t)temperature;
	const char *sign = temperature < 0 ? "-" : "";
	uint32_t temperature_whole = absolute_temperature / 100U;
	uint32_t temperature_fraction = absolute_temperature % 100U;
	uint32_t battery_whole = measurement->battery_centi_percent / 100U;
	uint32_t battery_fraction = measurement->battery_centi_percent % 100U;
	int written;

	if (include_device_id) {
		written = snprintk(buffer + *offset, capacity - *offset,
			"%s{\"device_id\":\"thingy91x\",\"timestamp\":%lld,"
			"\"temperature\":%s%u.%02u,\"battery\":%u.%02u}",
			add_comma ? "," : "", (long long)measurement->timestamp,
			sign, temperature_whole, temperature_fraction,
			battery_whole, battery_fraction);
	} else {
		written = snprintk(buffer + *offset, capacity - *offset,
			"%s{\"timestamp\":%lld,\"temperature\":%s%u.%02u,"
			"\"battery\":%u.%02u}",
			add_comma ? "," : "", (long long)measurement->timestamp,
			sign, temperature_whole, temperature_fraction,
			battery_whole, battery_fraction);
	}

	if (written < 0 || (size_t)written >= capacity - *offset) {
		return -EMSGSIZE;
	}

	*offset += (size_t)written;
	return 0;
}

int aitsm_data_transmission_init(void)
{
	k_mutex_init(&measurement_mutex);
	k_mutex_lock(&measurement_mutex, K_FOREVER);
	memset(measurement_buffer, 0, sizeof(measurement_buffer));
	measurement_count = 0;
	first_measurement_timestamp = 0;
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
	flush = measurement_count >= ARRAY_SIZE(measurement_buffer) ||
		(now >= first_measurement_timestamp &&
		 now - first_measurement_timestamp >= CONFIG_AITSM_BATCH_INTERVAL_SECONDS);
#endif

	k_mutex_unlock(&measurement_mutex);
	return flush;
}

int aitsm_data_transmission_format(char *buffer, size_t capacity,
				   size_t *formatted_measurement_count)
{
	size_t offset = 0;
	int err;

	if (buffer == NULL || formatted_measurement_count == NULL || capacity == 0) {
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

#if defined(CONFIG_AITSM_TRANSMISSION_SINGLE)
	err = append_measurement_json(buffer, capacity, &offset,
				       &measurement_buffer[0], true, false);
#else
	int written = snprintk(buffer, capacity,
				"{\"device_id\":\"thingy91x\",\"readings\":[");
	if (written < 0 || (size_t)written >= capacity) {
		err = -EMSGSIZE;
	} else {
		offset = (size_t)written;
		err = 0;
		for (size_t i = 0; i < measurement_count; i++) {
			err = append_measurement_json(buffer, capacity, &offset,
					       &measurement_buffer[i], false, i != 0);
			if (err != 0) {
				break;
			}
		}
		if (err == 0) {
			written = snprintk(buffer + offset, capacity - offset, "]}");
			if (written < 0 || (size_t)written >= capacity - offset) {
				err = -EMSGSIZE;
			} else {
				offset += (size_t)written;
			}
		}
	}
#endif

	if (err == 0) {
		*formatted_measurement_count = measurement_count;
	}

	k_mutex_unlock(&measurement_mutex);
	return err;
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
		first_measurement_timestamp = measurement_count == 0 ? 0 :
			measurement_buffer[0].timestamp;
	}

	k_mutex_unlock(&measurement_mutex);
	return 0;
}
