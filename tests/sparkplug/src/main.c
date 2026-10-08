#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <sparkplug.h>
#include <sparkplug_decode.h>

ZTEST(sparkplug, test_topics_follow_sparkplug_namespace)
{
	char topic[64];

	zassert_ok(aitsm_sparkplug_topic(topic, sizeof(topic), AITSM_SPARKPLUG_NBIRTH), NULL);
	zassert_str_equal(topic, "spBv1.0/aitsm/NBIRTH/thingy91x", NULL);
	zassert_ok(aitsm_sparkplug_topic(topic, sizeof(topic), AITSM_SPARKPLUG_NDATA), NULL);
	zassert_str_equal(topic, "spBv1.0/aitsm/NDATA/thingy91x", NULL);
}

ZTEST(sparkplug, test_topic_rejects_small_buffer)
{
	char topic[8];

	zassert_equal(aitsm_sparkplug_topic(topic, sizeof(topic), AITSM_SPARKPLUG_NDATA),
		      -EMSGSIZE, NULL);
}

ZTEST(sparkplug, test_seq_wraps_after_255)
{
	zassert_equal(aitsm_sparkplug_next_seq(0), 1, NULL);
	zassert_equal(aitsm_sparkplug_next_seq(254), 255, NULL);
	zassert_equal(aitsm_sparkplug_next_seq(255), 0, NULL);
}

ZTEST(sparkplug, test_nbirth_has_seq_zero_bdseq_and_metric_declarations)
{
	uint8_t buffer[256];
	size_t length;
	static struct decoded decoded;

	zassert_ok(aitsm_sparkplug_encode_nbirth(buffer, sizeof(buffer), &length, 1700000000000ULL,
						 7), NULL);
	decode(buffer, length, &decoded);

	zassert_true(decoded.payload.has_seq, NULL);
	zassert_equal(decoded.payload.seq, 0, NULL);
	zassert_equal(decoded.payload.timestamp, 1700000000000ULL, NULL);
	zassert_equal(decoded.count, 3, NULL);
	zassert_str_equal(decoded.metrics[0].name, "bdSeq", NULL);
	zassert_equal(decoded.metrics[0].datatype, 4, NULL);
	zassert_equal(decoded.metrics[0].value.long_value, 7, NULL);
	zassert_str_equal(decoded.metrics[1].name, "temperature", NULL);
	zassert_equal(decoded.metrics[1].datatype, 9, NULL);
	zassert_true(decoded.metrics[1].is_null, NULL);
	zassert_str_equal(decoded.metrics[2].name, "battery", NULL);
	zassert_true(decoded.metrics[2].is_null, NULL);
}

ZTEST(sparkplug, test_ndata_single_measurement_is_live)
{
	const struct aitsm_measurement measurement = {
		.timestamp = 1700000000,
		.temperature_centi_celsius = 2850,
		.battery_centi_percent = 9740,
	};
	uint8_t buffer[128];
	size_t length;
	static struct decoded decoded;

	zassert_ok(aitsm_sparkplug_encode_ndata(buffer, sizeof(buffer), &length, &measurement, 1,
						 5, false), NULL);
	decode(buffer, length, &decoded);

	zassert_equal(decoded.payload.seq, 5, NULL);
	zassert_equal(decoded.payload.timestamp, 1700000000000ULL, NULL);
	zassert_equal(decoded.count, 2, NULL);
	zassert_str_equal(decoded.metrics[0].name, "temperature", NULL);
	zassert_within(decoded.metrics[0].value.float_value, 28.5f, 0.001f, NULL);
	zassert_equal(decoded.metrics[0].timestamp, 1700000000000ULL, NULL);
	zassert_false(decoded.metrics[0].is_historical, NULL);
	zassert_str_equal(decoded.metrics[1].name, "battery", NULL);
	zassert_within(decoded.metrics[1].value.float_value, 97.4f, 0.001f, NULL);
}

ZTEST(sparkplug, test_ndata_batch_keeps_every_reading_and_marks_historical)
{
	const struct aitsm_measurement measurements[] = {
		{ .timestamp = 100, .temperature_centi_celsius = -150, .battery_centi_percent = 5000 },
		{ .timestamp = 115, .temperature_centi_celsius = 2000, .battery_centi_percent = 4990 },
		{ .timestamp = 130, .temperature_centi_celsius = 2100, .battery_centi_percent = 4980 },
	};
	uint8_t buffer[256];
	size_t length;
	static struct decoded decoded;

	zassert_ok(aitsm_sparkplug_encode_ndata(buffer, sizeof(buffer), &length, measurements,
						 ARRAY_SIZE(measurements), 255, true), NULL);
	decode(buffer, length, &decoded);

	zassert_equal(decoded.payload.seq, 255, NULL);
	zassert_equal(decoded.payload.timestamp, 130000ULL, NULL);
	zassert_equal(decoded.count, 6, NULL);
	zassert_within(decoded.metrics[0].value.float_value, -1.5f, 0.001f, NULL);
	zassert_equal(decoded.metrics[0].timestamp, 100000ULL, NULL);
	zassert_equal(decoded.metrics[2].timestamp, 115000ULL, NULL);
	zassert_equal(decoded.metrics[4].timestamp, 130000ULL, NULL);
	for (size_t i = 0; i < decoded.count; i++) {
		zassert_true(decoded.metrics[i].is_historical, "metric %d", i);
	}
}

ZTEST(sparkplug, test_small_buffer_returns_emsgsize)
{
	const struct aitsm_measurement measurement = {
		.timestamp = 1, .temperature_centi_celsius = 1, .battery_centi_percent = 1,
	};
	uint8_t buffer[8];
	size_t length = 0;

	zassert_equal(aitsm_sparkplug_encode_ndata(buffer, sizeof(buffer), &length, &measurement,
						   1, 0, false), -EMSGSIZE, NULL);
	zassert_equal(aitsm_sparkplug_encode_nbirth(buffer, sizeof(buffer), &length, 1, 0),
		      -EMSGSIZE, NULL);
}

ZTEST(sparkplug, test_invalid_arguments_are_rejected)
{
	uint8_t buffer[32];
	size_t length;

	zassert_equal(aitsm_sparkplug_encode_ndata(buffer, sizeof(buffer), &length, NULL, 0, 0,
						   false), -EINVAL, NULL);
	zassert_equal(aitsm_sparkplug_encode_nbirth(NULL, 0, &length, 1, 0), -EINVAL, NULL);
}

ZTEST_SUITE(sparkplug, NULL, NULL, NULL, NULL, NULL);
