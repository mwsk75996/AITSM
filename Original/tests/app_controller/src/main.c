#include <errno.h>
#include <zephyr/ztest.h>
#include <app_controller.h>
#include <mqtt_client.h>
#include <measurement_service.h>
#include <led_status.h>

K_SEM_DEFINE(probe_done, 0, 1);
K_SEM_DEFINE(connect_called, 0, 16);
static int connect_calls, disconnect_calls, starts, stops;
static int connect_result;
static bool lte, accept_completion;
static uint32_t completed_token;
static int completed_result;
static enum led_status led;

int aitsm_mqtt_connect(void)
{
	zassert_equal(k_current_get(), &k_sys_work_q.thread, NULL);
	zassert_true(lte, "Connect without LTE");
	connect_calls++;
	k_sem_give(&connect_called);
	return connect_result;
}
int aitsm_mqtt_disconnect(void) { disconnect_calls++; return 0; }
void aitsm_mqtt_set_lte_available(bool available) { lte = available; }
void aitsm_measurement_service_mqtt_connected(void) { starts++; }
void aitsm_measurement_service_mqtt_disconnected(void) { stops++; }
bool aitsm_measurement_service_publish_result(uint32_t token, int result)
{ completed_token = token; completed_result = result; return accept_completion; }
int led_status_set(enum led_status status) { led = status; return 0; }

static void probe_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	k_sem_give(&probe_done);
}
K_WORK_DEFINE(probe, probe_handler);
static void flush(void)
{
	zassert_true(k_work_submit(&probe) >= 0, NULL);
	zassert_ok(k_sem_take(&probe_done, K_SECONDS(1)), NULL);
}
static void post(enum aitsm_app_event_type type)
{
	zassert_true(aitsm_app_post_event(type, 0) >= 0, NULL);
	flush();
}
static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	post(AITSM_APP_EVENT_LTE_DISCONNECTED);
	aitsm_app_controller_init();
	connect_calls = disconnect_calls = starts = stops = connect_result = 0;
	accept_completion = true; completed_token = 0;
	k_sem_reset(&connect_called);
}
ZTEST_SUITE(app_controller, NULL, NULL, before, NULL, NULL);

ZTEST(app_controller, test_lte_up_connects_once_and_starts_only_on_connack)
{
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	zassert_equal(connect_calls, 1, NULL);
	zassert_equal(starts, 0, NULL);
	post(AITSM_APP_EVENT_MQTT_CONNECTED);
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	zassert_equal(starts, 1, NULL);
	zassert_equal(led, LED_STATUS_MQTT_CONNECTED, NULL);
}

ZTEST(app_controller, test_disconnect_retries_without_lte_event)
{
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	post(AITSM_APP_EVENT_MQTT_CONNECTED);
	k_sem_reset(&connect_called);
	post(AITSM_APP_EVENT_MQTT_DISCONNECTED);
	zassert_equal(stops, 1, NULL);
	zassert_ok(k_sem_take(&connect_called, K_MSEC(1500)), NULL);
	zassert_equal(connect_calls, 2, NULL);
	post(AITSM_APP_EVENT_MQTT_CONNECTED);
	zassert_equal(starts, 2, NULL);
}

ZTEST(app_controller, test_error_and_disconnect_keep_original_deadline_and_led)
{
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	k_sem_reset(&connect_called);
	post(AITSM_APP_EVENT_MQTT_ERROR);
	k_sleep(K_MSEC(600));
	post(AITSM_APP_EVENT_MQTT_DISCONNECTED);
	zassert_equal(led, LED_STATUS_ERROR, NULL);
	zassert_ok(k_sem_take(&connect_called, K_MSEC(650)),
		   "Duplicate disconnect postponed original retry");
	zassert_equal(connect_calls, 2, NULL);
}

ZTEST(app_controller, test_lte_down_cancels_retry_and_notifies_transmission_offline)
{
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	post(AITSM_APP_EVENT_MQTT_CONNECTED);
	post(AITSM_APP_EVENT_MQTT_DISCONNECTED);
	k_sem_reset(&connect_called);
	post(AITSM_APP_EVENT_LTE_DISCONNECTED);
	zassert_true(stops >= 2, NULL);
	zassert_false(lte, NULL);
	zassert_not_equal(k_sem_take(&connect_called, K_MSEC(1200)), 0, NULL);
	zassert_equal(connect_calls, 1, NULL);
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	zassert_equal(connect_calls, 2, NULL);
}

ZTEST(app_controller, test_late_connack_while_offline_does_not_start_measurements)
{
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	post(AITSM_APP_EVENT_LTE_DISCONNECTED);
	post(AITSM_APP_EVENT_MQTT_CONNECTED);
	zassert_equal(starts, 0, NULL);
	zassert_equal(disconnect_calls, 1, NULL);
	zassert_equal(led, LED_STATUS_DISCONNECTED, NULL);
}

ZTEST(app_controller, test_connect_submission_failure_retries)
{
	connect_result = -EIO;
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	k_sem_reset(&connect_called);
	zassert_equal(led, LED_STATUS_ERROR, NULL);
	connect_result = 0;
	zassert_ok(k_sem_take(&connect_called, K_MSEC(1500)), NULL);
	zassert_equal(connect_calls, 2, NULL);
}


ZTEST(app_controller, test_completion_token_forwarded_and_stale_result_leaves_led_unchanged)
{
	post(AITSM_APP_EVENT_LTE_CONNECTED);
	post(AITSM_APP_EVENT_MQTT_CONNECTED);
	zassert_true(aitsm_app_post_publish_result(73, 0) >= 0, NULL);
	flush();
	zassert_equal(completed_token, 73, NULL);
	zassert_equal(completed_result, 0, NULL);
	zassert_equal(led, LED_STATUS_PUBLISH_OK, NULL);
	accept_completion = false;
	zassert_true(aitsm_app_post_publish_result(72, -EIO) >= 0, NULL);
	flush();
	zassert_equal(completed_token, 72, NULL);
	zassert_equal(led, LED_STATUS_PUBLISH_OK, "Stale completion changed LED");
}
