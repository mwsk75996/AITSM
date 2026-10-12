#include <errno.h>

#include <zephyr/ztest.h>

#include "batch.h"

/* prj.conf sets CONFIG_AITSM_BATCH_SIZE=3 and CONFIG_AITSM_BATCH_CAPACITY=5. */

static void add_measurements(int64_t first_timestamp, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		const struct aitsm_measurement measurement = {
			.timestamp = first_timestamp + (int64_t)i,
		};

		zassert_ok(aitsm_batch_add(&measurement), NULL);
	}
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	aitsm_batch_reset();
}

ZTEST(batch, test_starts_empty)
{
	zassert_equal(aitsm_batch_count(), 0, NULL);
	zassert_false(aitsm_batch_is_ready(), NULL);
}

ZTEST(batch, test_keeps_measurements_in_order)
{
	const struct aitsm_measurement measurement = {
		.timestamp = 1700000000,
		.temperature_centi_celsius = 2850,
		.battery_centi_percent = 9740,
	};

	zassert_ok(aitsm_batch_add(&measurement), NULL);
	add_measurements(1700000015, 1);

	zassert_equal(aitsm_batch_count(), 2, NULL);
	zassert_equal(aitsm_batch_data()[0].timestamp, 1700000000, NULL);
	zassert_equal(aitsm_batch_data()[0].temperature_centi_celsius, 2850, NULL);
	zassert_equal(aitsm_batch_data()[0].battery_centi_percent, 9740, NULL);
	zassert_equal(aitsm_batch_data()[1].timestamp, 1700000015, NULL);
}

ZTEST(batch, test_is_ready_at_batch_size)
{
	add_measurements(100, CONFIG_AITSM_BATCH_SIZE - 1);
	zassert_false(aitsm_batch_is_ready(), NULL);

	add_measurements(200, 1);
	zassert_true(aitsm_batch_is_ready(), NULL);
}

ZTEST(batch, test_full_buffer_rejects_new_measurement_without_overwriting)
{
	const struct aitsm_measurement extra = { .timestamp = 999 };

	add_measurements(100, CONFIG_AITSM_BATCH_CAPACITY);

	zassert_equal(aitsm_batch_add(&extra), -ENOSPC, NULL);
	zassert_equal(aitsm_batch_count(), CONFIG_AITSM_BATCH_CAPACITY, NULL);
	for (size_t i = 0; i < CONFIG_AITSM_BATCH_CAPACITY; i++) {
		zassert_equal(aitsm_batch_data()[i].timestamp, 100 + (int64_t)i, "index %zu", i);
	}
}

ZTEST(batch, test_remove_keeps_measurements_added_while_waiting_for_puback)
{
	/* Three measurements are sent, and one more arrives before the PUBACK. */
	add_measurements(100, 3);
	add_measurements(200, 1);

	aitsm_batch_remove(3);

	zassert_equal(aitsm_batch_count(), 1, NULL);
	zassert_equal(aitsm_batch_data()[0].timestamp, 200, NULL);
	zassert_false(aitsm_batch_is_ready(), NULL);
}

ZTEST(batch, test_buffer_can_be_filled_again_after_remove)
{
	add_measurements(100, CONFIG_AITSM_BATCH_CAPACITY);
	aitsm_batch_remove(CONFIG_AITSM_BATCH_CAPACITY);
	zassert_equal(aitsm_batch_count(), 0, NULL);

	add_measurements(300, CONFIG_AITSM_BATCH_CAPACITY);
	zassert_equal(aitsm_batch_count(), CONFIG_AITSM_BATCH_CAPACITY, NULL);
	zassert_equal(aitsm_batch_data()[0].timestamp, 300, NULL);
}

ZTEST(batch, test_remove_more_than_stored_empties_buffer)
{
	add_measurements(100, 2);

	aitsm_batch_remove(10);

	zassert_equal(aitsm_batch_count(), 0, NULL);
}

ZTEST_SUITE(batch, NULL, NULL, before_each, NULL, NULL);
