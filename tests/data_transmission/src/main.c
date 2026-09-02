#include <errno.h>
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

static void *data_transmission_setup(void)
{
	zassert_ok(aitsm_data_transmission_init(), NULL);
	return NULL;
}

ZTEST_SUITE(data_transmission, NULL, data_transmission_setup, NULL, NULL, NULL);

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
