#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <data_transmission.h>

static const struct aitsm_measurement first_measurement = {
	.timestamp = 100,
	.temperature_centi_celsius = 2345,
	.battery_centi_percent = 9876,
};

static const struct aitsm_measurement second_measurement = {
	.timestamp = 115,
	.temperature_centi_celsius = -125,
	.battery_centi_percent = 9800,
};

/* Largest possible field values, giving the longest serialized reading. */
static const struct aitsm_measurement worst_case_measurement = {
	.timestamp = INT64_MAX,
	.temperature_centi_celsius = INT32_MIN,
	.battery_centi_percent = UINT16_MAX,
};

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
#define TEST_MAX_MEASUREMENTS CONFIG_AITSM_BATCH_MAX_SAMPLES
#else
#define TEST_MAX_MEASUREMENTS 1
#endif

static void data_transmission_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Each test starts with an empty buffer, independent of test order. */
	zassert_ok(aitsm_data_transmission_init(), NULL);
}

ZTEST_SUITE(data_transmission, NULL, NULL, data_transmission_before, NULL, NULL);

ZTEST(data_transmission, test_selected_profile)
{
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_equal(aitsm_data_transmission_mode(),
		      AITSM_TRANSMISSION_MODE_BATCH, NULL);
#else
	zassert_equal(aitsm_data_transmission_mode(),
		      AITSM_TRANSMISSION_MODE_SINGLE, NULL);
#endif
}

ZTEST(data_transmission, test_measurements_are_formatted_and_committed)
{
	char payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t formatted_count;

	zassert_ok(aitsm_data_transmission_add(&first_measurement), NULL);

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_ok(aitsm_data_transmission_add(&second_measurement), NULL);
	zassert_false(aitsm_data_transmission_should_flush(399), NULL);
	zassert_true(aitsm_data_transmission_should_flush(400), NULL);
#else
	zassert_equal(aitsm_data_transmission_add(&second_measurement), -EBUSY,
		      NULL);
	zassert_true(aitsm_data_transmission_should_flush(100), NULL);
#endif

	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload),
						 &formatted_count), NULL);
	zassert_not_null(strstr(payload, "\"timestamp\":100"), NULL);
	zassert_not_null(strstr(payload, "\"temperature\":23.45"), NULL);
	zassert_not_null(strstr(payload, "\"battery\":98.76"), NULL);

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_equal(formatted_count, 2, NULL);
	zassert_not_null(strstr(payload, "\"readings\":["), NULL);
	zassert_not_null(strstr(payload, "\"temperature\":-1.25"), NULL);
#else
	zassert_equal(formatted_count, 1, NULL);
	zassert_is_null(strstr(payload, "\"readings\":["), NULL);
#endif

	zassert_ok(aitsm_data_transmission_commit(formatted_count), NULL);
	zassert_equal(aitsm_data_transmission_format(payload, sizeof(payload),
						     &formatted_count),
			      -ENODATA, NULL);
}

ZTEST(data_transmission, test_too_small_payload_buffer_keeps_measurements)
{
	char payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	char small_payload[16];
	size_t formatted_count = 0;

	zassert_ok(aitsm_data_transmission_add(&first_measurement), NULL);

	zassert_equal(aitsm_data_transmission_format(small_payload,
						     sizeof(small_payload),
						     &formatted_count),
		      -EMSGSIZE, NULL);
	/* A failed format must not report measurements as ready to commit. */
	zassert_equal(formatted_count, 0, NULL);

	/* The measurement is still buffered and can be sent with enough room. */
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload),
						 &formatted_count), NULL);
	zassert_equal(formatted_count, 1, NULL);
	zassert_not_null(strstr(payload, "\"timestamp\":100"), NULL);
}

ZTEST(data_transmission, test_full_buffer_rejects_and_requests_flush)
{
	struct aitsm_measurement measurement = first_measurement;

	for (size_t i = 0; i < TEST_MAX_MEASUREMENTS; i++) {
		measurement.timestamp = first_measurement.timestamp + i;
		zassert_ok(aitsm_data_transmission_add(&measurement), NULL);
	}

	/* A full buffer is flushed at once, without waiting for the interval. */
	zassert_true(aitsm_data_transmission_should_flush(first_measurement.timestamp),
		     NULL);

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_equal(aitsm_data_transmission_add(&second_measurement), -ENOSPC,
		      NULL);
#else
	zassert_equal(aitsm_data_transmission_add(&second_measurement), -EBUSY,
		      NULL);
#endif
}

ZTEST(data_transmission, test_worst_case_full_buffer_fits_payload)
{
	char payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t formatted_count;

	for (size_t i = 0; i < TEST_MAX_MEASUREMENTS; i++) {
		zassert_ok(aitsm_data_transmission_add(&worst_case_measurement), NULL);
	}

	/* The configured payload size must hold a full buffer of the longest
	 * possible readings, otherwise a full batch could never be sent.
	 */
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload),
						 &formatted_count),
		   "Payload size %d is too small for %d worst-case measurements",
		   AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE, TEST_MAX_MEASUREMENTS);
	zassert_equal(formatted_count, TEST_MAX_MEASUREMENTS, NULL);
	zassert_not_null(strstr(payload, "\"temperature\":-21474836.48"), NULL);
	zassert_not_null(strstr(payload, "\"battery\":655.35"), NULL);
}

ZTEST(data_transmission, test_empty_buffer_does_not_flush_and_has_capacity)
{
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
	zassert_false(aitsm_data_transmission_should_flush(INT64_MAX), NULL);
}

ZTEST(data_transmission, test_full_buffer_preserves_oldest_and_recovers_after_ack)
{
	struct aitsm_measurement sample = first_measurement;
	for (size_t i = 0; i < TEST_MAX_MEASUREMENTS; i++) {
		sample.timestamp = first_measurement.timestamp + i;
		zassert_ok(aitsm_data_transmission_add(&sample), NULL);
	}
	zassert_false(aitsm_data_transmission_has_capacity(), NULL);
	char payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t count;
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &count), NULL);
	zassert_not_null(strstr(payload, "\"timestamp\":100"), NULL);
	zassert_ok(aitsm_data_transmission_commit(1), NULL);
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
	sample.timestamp = 999;
	zassert_ok(aitsm_data_transmission_add(&sample), NULL);
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &count), NULL);
	zassert_is_null(strstr(payload, "\"timestamp\":100"), NULL);
	zassert_not_null(strstr(payload, "\"timestamp\":999"), NULL);
	zassert_equal(count, TEST_MAX_MEASUREMENTS, NULL);
}
