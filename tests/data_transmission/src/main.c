#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <data_transmission.h>
#include <sparkplug.h>
#include <sparkplug_decode.h>

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
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t length;
	static struct decoded decoded;
	size_t formatted_count;

	zassert_ok(aitsm_data_transmission_add(&first_measurement), NULL);

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_ok(aitsm_data_transmission_add(&second_measurement), NULL);
	zassert_false(aitsm_data_transmission_should_flush(384), NULL);
	zassert_true(aitsm_data_transmission_should_flush(385), NULL);
#else
	zassert_equal(aitsm_data_transmission_add(&second_measurement), -EBUSY,
		      NULL);
	zassert_true(aitsm_data_transmission_should_flush(100), NULL);
#endif

	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length,
						 &formatted_count), NULL);
	decode(payload, length, &decoded);
	zassert_equal(decoded.payload.seq, 1, "First NDATA after NBIRTH has seq 1");
	zassert_str_equal(decoded.metrics[0].name, "temperature", NULL);
	zassert_equal(decoded.metrics[0].timestamp, 100000ULL, NULL);
	zassert_within(decoded.metrics[0].value.float_value, 23.45f, 0.001f, NULL);
	zassert_str_equal(decoded.metrics[1].name, "battery", NULL);
	zassert_within(decoded.metrics[1].value.float_value, 98.76f, 0.001f, NULL);

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_equal(formatted_count, 2, NULL);
	zassert_equal(decoded.count, 4, NULL);
	zassert_within(decoded.metrics[2].value.float_value, -1.25f, 0.001f, NULL);
	zassert_true(decoded.metrics[0].is_historical, "Batch readings are historical");
#else
	zassert_equal(formatted_count, 1, NULL);
	zassert_equal(decoded.count, 2, NULL);
	zassert_false(decoded.metrics[0].is_historical, "Single readings are live");
#endif

	zassert_ok(aitsm_data_transmission_commit(formatted_count), NULL);
	zassert_equal(aitsm_data_transmission_format(payload, sizeof(payload), &length,
						     &formatted_count),
			      -ENODATA, NULL);
}

ZTEST(data_transmission, test_too_small_payload_buffer_keeps_measurements)
{
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t length;
	static struct decoded decoded;
	uint8_t small_payload[16];
	size_t formatted_count = 0;

	zassert_ok(aitsm_data_transmission_add(&first_measurement), NULL);

	zassert_equal(aitsm_data_transmission_format(small_payload,
						     sizeof(small_payload), &length,
						     &formatted_count),
		      -EMSGSIZE, NULL);
	/* A failed format must not report measurements as ready to commit. */
	zassert_equal(formatted_count, 0, NULL);

	/* The measurement is still buffered and can be sent with enough room. */
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length,
						 &formatted_count), NULL);
	zassert_equal(formatted_count, 1, NULL);
	decode(payload, length, &decoded);
	zassert_equal(decoded.metrics[0].timestamp, 100000ULL, NULL);
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
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t length;
	size_t formatted_count;

	for (size_t i = 0; i < TEST_MAX_MEASUREMENTS; i++) {
		zassert_ok(aitsm_data_transmission_add(&worst_case_measurement), NULL);
	}

	/* The configured payload size must hold a full buffer of the longest
	 * possible readings, otherwise a full batch could never be sent.
	 */
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length,
						 &formatted_count),
		   "Payload size %d is too small for %d worst-case measurements",
		   AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE, TEST_MAX_MEASUREMENTS);
	zassert_equal(formatted_count, TEST_MAX_MEASUREMENTS, NULL);
	zassert_true(length <= AITSM_SPARKPLUG_NDATA_MAX_SIZE(TEST_MAX_MEASUREMENTS),
		     "Encoded %d bytes exceeds the BUILD_ASSERT bound", length);
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
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t length;
	static struct decoded decoded;
	size_t count;
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
	decode(payload, length, &decoded);
	zassert_equal(decoded.metrics[0].timestamp, 100000ULL, NULL);
	zassert_ok(aitsm_data_transmission_commit(1), NULL);
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
	sample.timestamp = 999;
	zassert_ok(aitsm_data_transmission_add(&sample), NULL);
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
	decode(payload, length, &decoded);
	zassert_equal(decoded.metrics[0].timestamp, TEST_MAX_MEASUREMENTS > 1 ? 101000ULL : 999000ULL,
		      "Oldest sent reading was removed");
	zassert_equal(decoded.metrics[decoded.count - 2].timestamp, 999000ULL, NULL);
	zassert_equal(count, TEST_MAX_MEASUREMENTS, NULL);
}

ZTEST(data_transmission, test_seq_advances_only_when_commit_confirms)
{
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t length, count;
	static struct decoded decoded;

	zassert_ok(aitsm_data_transmission_add(&first_measurement), NULL);
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
	/* A resend after a lost PUBACK reuses the seq. */
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
	decode(payload, length, &decoded);
	zassert_equal(decoded.payload.seq, 1, NULL);

	zassert_ok(aitsm_data_transmission_commit(count), NULL);
	zassert_ok(aitsm_data_transmission_add(&second_measurement), NULL);
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
	decode(payload, length, &decoded);
	zassert_equal(decoded.payload.seq, 2, NULL);

	/* A new session (NBIRTH) restarts the numbering. */
	aitsm_data_transmission_start_session();
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
	decode(payload, length, &decoded);
	zassert_equal(decoded.payload.seq, 1, NULL);
}
