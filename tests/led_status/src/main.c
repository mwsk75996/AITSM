#include <errno.h>

#include <zephyr/ztest.h>

#include <led_status.h>

ZTEST_SUITE(led_status, NULL, NULL, NULL, NULL, NULL);

ZTEST(led_status, test_indication_colours)
{
	struct led_indication indication;

	indication = led_status_indication(LED_STATUS_SEARCHING);
	zassert_equal(indication.color.red, 0, NULL);
	zassert_equal(indication.color.green, 0, NULL);
	zassert_equal(indication.color.blue, UINT8_MAX, NULL);
	zassert_equal(indication.pattern, LED_PATTERN_BLINK, NULL);

	indication = led_status_indication(LED_STATUS_LTE_CONNECTED);
	zassert_equal(indication.color.red, 0, NULL);
	zassert_equal(indication.color.green, UINT8_MAX, NULL);
	zassert_equal(indication.color.blue, 0, NULL);
	zassert_equal(indication.pattern, LED_PATTERN_SOLID, NULL);

	indication = led_status_indication(LED_STATUS_MQTT_CONNECTED);
	zassert_equal(indication.color.red, 0, NULL);
	zassert_equal(indication.color.green, UINT8_MAX, NULL);
	zassert_equal(indication.color.blue, UINT8_MAX, NULL);
	zassert_equal(indication.pattern, LED_PATTERN_SOLID, NULL);

	indication = led_status_indication(LED_STATUS_DISCONNECTED);
	zassert_equal(indication.color.red, UINT8_MAX, NULL);
	zassert_equal(indication.color.green, 0, NULL);
	zassert_equal(indication.color.blue, 0, NULL);

	indication = led_status_indication(LED_STATUS_ERROR);
	zassert_equal(indication.color.red, UINT8_MAX, NULL);

	indication = led_status_indication(LED_STATUS_PUBLISH_OK);
	zassert_equal(indication.color.red, UINT8_MAX, NULL);
	zassert_equal(indication.color.green, UINT8_MAX, NULL);
	zassert_equal(indication.color.blue, UINT8_MAX, NULL);

	indication = led_status_indication(LED_STATUS_PUBLISH_ERROR);
	zassert_equal(indication.color.red, UINT8_MAX, NULL);
	zassert_equal(indication.color.green, 0, NULL);
	zassert_equal(indication.color.blue, 0, NULL);
}

ZTEST(led_status, test_transient_returns_to_stable)
{
	zassert_ok(led_status_init(), NULL);
	zassert_ok(led_status_set(LED_STATUS_LTE_CONNECTED), NULL);
	zassert_equal(led_status_stable(), LED_STATUS_LTE_CONNECTED, NULL);

	/* A success flash must not overwrite the stable LTE status. */
	zassert_ok(led_status_set(LED_STATUS_PUBLISH_OK), NULL);
	zassert_equal(led_status_displayed(), LED_STATUS_PUBLISH_OK, NULL);
	zassert_equal(led_status_stable(), LED_STATUS_LTE_CONNECTED, NULL);

	k_sleep(K_MSEC(2 * 300 + 200));
	zassert_equal(led_status_displayed(), LED_STATUS_LTE_CONNECTED, NULL);

	/* Same for a failure flash after MQTT has connected. */
	zassert_ok(led_status_set(LED_STATUS_MQTT_CONNECTED), NULL);
	zassert_ok(led_status_set(LED_STATUS_PUBLISH_ERROR), NULL);
	zassert_equal(led_status_displayed(), LED_STATUS_PUBLISH_ERROR, NULL);
	zassert_equal(led_status_stable(), LED_STATUS_MQTT_CONNECTED, NULL);

	k_sleep(K_MSEC(2 * 300 + 200));
	zassert_equal(led_status_displayed(), LED_STATUS_MQTT_CONNECTED, NULL);
}

ZTEST(led_status, test_stable_status_changes)
{
	zassert_ok(led_status_init(), NULL);

	zassert_ok(led_status_set(LED_STATUS_SEARCHING), NULL);
	zassert_equal(led_status_stable(), LED_STATUS_SEARCHING, NULL);

	zassert_ok(led_status_set(LED_STATUS_MQTT_CONNECTED), NULL);
	zassert_equal(led_status_stable(), LED_STATUS_MQTT_CONNECTED, NULL);

	zassert_ok(led_status_set(LED_STATUS_ERROR), NULL);
	zassert_equal(led_status_stable(), LED_STATUS_ERROR, NULL);
}

ZTEST(led_status, test_invalid_status_rejected)
{
	zassert_equal(led_status_set((enum led_status)99), -EINVAL, NULL);
}
