#include <zephyr/ztest.h>
#include <mqtt_reconnect.h>

ZTEST_SUITE(mqtt_reconnect, NULL, NULL, NULL, NULL, NULL);

ZTEST(mqtt_reconnect, test_offline_ignores_failures_and_timer)
{
	struct aitsm_reconnect_state s;
	aitsm_reconnect_init(&s);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE).action,
		      AITSM_RECONNECT_NONE, NULL);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_TIMER).action,
		      AITSM_RECONNECT_NONE, NULL);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_MQTT_UP).action,
		      AITSM_RECONNECT_NONE, NULL);
	zassert_equal(s.phase, AITSM_RECONNECT_OFFLINE, NULL);
}

ZTEST(mqtt_reconnect, test_backoff_doubles_and_caps)
{
	struct aitsm_reconnect_state s;
	aitsm_reconnect_init(&s);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_UP).action,
		      AITSM_RECONNECT_CONNECT, NULL);
	uint32_t delay = CONFIG_AITSM_MQTT_RECONNECT_INITIAL_DELAY_SECONDS;
	for (int i = 0; i < 12; i++) {
		struct aitsm_reconnect_result r = aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE);
		zassert_equal(r.action, AITSM_RECONNECT_SCHEDULE, NULL);
		zassert_equal(r.delay_seconds, delay, NULL);
		zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_TIMER).action,
			      AITSM_RECONNECT_CONNECT, NULL);
		delay = MIN(delay * 2U, CONFIG_AITSM_MQTT_RECONNECT_MAX_DELAY_SECONDS);
	}
}

ZTEST(mqtt_reconnect, test_duplicate_events_do_not_postpone_deadline)
{
	struct aitsm_reconnect_state s;
	aitsm_reconnect_init(&s);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_UP);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_UP).action,
		      AITSM_RECONNECT_NONE, NULL);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE);
	uint32_t next = s.next_delay_seconds;
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE).action,
		      AITSM_RECONNECT_NONE, NULL);
	zassert_equal(s.next_delay_seconds, next, NULL);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_UP).action,
		      AITSM_RECONNECT_NONE, NULL);
}

ZTEST(mqtt_reconnect, test_connected_resets_backoff_and_cancels_retry)
{
	struct aitsm_reconnect_state s;
	aitsm_reconnect_init(&s);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_UP);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_TIMER);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_MQTT_UP).action,
		      AITSM_RECONNECT_CANCEL, NULL);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_TIMER).action,
		      AITSM_RECONNECT_NONE, NULL);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE).delay_seconds,
		      CONFIG_AITSM_MQTT_RECONNECT_INITIAL_DELAY_SECONDS, NULL);
}

ZTEST(mqtt_reconnect, test_lte_loss_cancels_and_recovery_connects_immediately)
{
	struct aitsm_reconnect_state s;
	aitsm_reconnect_init(&s);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_UP);
	(void)aitsm_reconnect_handle(&s, AITSM_RECONNECT_FAILURE);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_DOWN).action,
		      AITSM_RECONNECT_CANCEL, NULL);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_TIMER).action,
		      AITSM_RECONNECT_NONE, NULL);
	zassert_equal(aitsm_reconnect_handle(&s, AITSM_RECONNECT_LTE_UP).action,
		      AITSM_RECONNECT_CONNECT, NULL);
	zassert_equal(s.next_delay_seconds,
		      CONFIG_AITSM_MQTT_RECONNECT_INITIAL_DELAY_SECONDS, NULL);
}
