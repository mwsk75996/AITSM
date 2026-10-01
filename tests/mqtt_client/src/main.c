#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <net/mqtt_helper.h>
#include <app_controller.h>
#include <mqtt_client.h>
#include <data_transmission.h>

struct event { enum aitsm_app_event_type type; int value; };
K_MSGQ_DEFINE(events, sizeof(struct event), 16, 4);
K_SEM_DEFINE(backend_started, 0, 1);
K_SEM_DEFINE(backend_release, 0, 1);
K_SEM_DEFINE(probe_done, 0, 1);
static struct mqtt_helper_cfg callbacks;
static struct k_work_q *worker;
static k_tid_t backend_thread;
static bool block_backend;
static int connect_result;
static int publish_result;
static int disconnect_calls;
static int connect_calls;
static int publish_calls;
static char sent_payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
static size_t sent_length;
static uint16_t sent_id;

int aitsm_app_post_event(enum aitsm_app_event_type type, int value)
{
	struct event event = { type, value };
	return k_msgq_put(&events, &event, K_NO_WAIT);
}

int mqtt_helper_init(struct mqtt_helper_cfg *cfg)
{
	callbacks = *cfg;
	return 0;
}

static void backend_enter(void)
{
	backend_thread = k_current_get();
	k_sem_give(&backend_started);
	if (block_backend) {
		zassert_ok(k_sem_take(&backend_release, K_SECONDS(2)), NULL);
	}
}

int mqtt_helper_connect(struct mqtt_helper_conn_params *params)
{
	zassert_equal(strcmp(params->hostname.ptr, "aitsm.vps.webdock.cloud"), 0, NULL);
	connect_calls++;
	backend_enter();
	return connect_result;
}

int mqtt_helper_disconnect(void)
{
	zassert_equal(k_current_get(), &worker->thread, NULL);
	disconnect_calls++;
	callbacks.cb.on_disconnect(0);
	return 0;
}

int mqtt_helper_publish(const struct mqtt_publish_param *param)
{
	publish_calls++;
	backend_enter();
	sent_length = param->message.payload.len;
	memcpy(sent_payload, param->message.payload.data, sent_length);
	sent_id = param->message_id;
	zassert_equal(param->message.topic.qos, MQTT_QOS_1_AT_LEAST_ONCE, NULL);
	return publish_result;
}

uint16_t mqtt_helper_msg_id_get(void)
{
	static uint16_t id;
	return ++id;
}

static void probe_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	zassert_equal(k_current_get(), &k_sys_work_q.thread, NULL);
	k_sem_give(&probe_done);
}
K_WORK_DEFINE(probe, probe_handler);

static void *setup(void)
{
	zassert_ok(aitsm_mqtt_init(), NULL);
	aitsm_mqtt_set_lte_available(true);
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	zassert_not_equal(backend_thread, &k_sys_work_q.thread, NULL);
	worker = CONTAINER_OF(backend_thread, struct k_work_q, thread);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	block_backend = false;
	aitsm_mqtt_set_lte_available(false);
	callbacks.cb.on_disconnect(0);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	aitsm_mqtt_set_lte_available(true);
	disconnect_calls = connect_calls = publish_calls = 0;
	connect_result = 0;
	publish_result = 0;
	sent_length = 0;
	k_msgq_purge(&events);
	k_sem_reset(&backend_started);
	k_sem_reset(&backend_release);
	k_sem_reset(&probe_done);
}

static void assert_event(enum aitsm_app_event_type type, int value)
{
	struct event event;
	zassert_ok(k_msgq_get(&events, &event, K_SECONDS(1)), NULL);
	zassert_equal(event.type, type, NULL);
	zassert_equal(event.value, value, NULL);
}

static void connected(void)
{
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	callbacks.cb.on_connack(MQTT_CONNECTION_ACCEPTED, false);
	assert_event(AITSM_APP_EVENT_MQTT_CONNECTED, 0);
}

ZTEST_SUITE(mqtt_client, NULL, setup, before, NULL, NULL);

ZTEST(mqtt_client, test_slow_connect_does_not_block_system_workqueue)
{
	block_backend = true;
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	zassert_true(k_work_submit(&probe) >= 0, NULL);
	int result = k_sem_take(&probe_done, K_MSEC(500));
	k_sem_give(&backend_release);
	zassert_ok(result, "System workqueue blocked during connect");
}

ZTEST(mqtt_client, test_publish_copies_payload_and_rejects_overwrite)
{
	char payload[] = "original";
	connected();
	block_backend = true;
	zassert_ok(aitsm_mqtt_publish_payload(payload, strlen(payload)), NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	memset(payload, 'x', strlen(payload));
	zassert_equal(aitsm_mqtt_publish_payload("new", 3), -EBUSY, NULL);
	block_backend = false;
	k_sem_give(&backend_release);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_mem_equal(sent_payload, "original", 8, NULL);
	zassert_equal(sent_length, 8, NULL);
	zassert_equal(k_msgq_num_used_get(&events), 0, "Only PUBACK completes a publish");
	callbacks.cb.on_puback(sent_id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
}

ZTEST(mqtt_client, test_slow_publish_does_not_block_system_workqueue)
{
	connected();
	block_backend = true;
	zassert_ok(aitsm_mqtt_publish_payload("payload", 7), NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	zassert_true(k_work_submit(&probe) >= 0, NULL);
	int result = k_sem_take(&probe_done, K_MSEC(500));
	k_sem_give(&backend_release);
	zassert_ok(result, "System workqueue blocked during publish");
}

ZTEST(mqtt_client, test_publish_failure_posts_result)
{
	connected();
	publish_result = -EIO;
	zassert_ok(aitsm_mqtt_publish_payload("payload", 7), NULL);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, -EIO);
}

ZTEST(mqtt_client, test_connect_failure_posts_error)
{
	connect_result = -ETIMEDOUT;
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_ERROR, -ETIMEDOUT);
}

ZTEST(mqtt_client, test_invalid_or_oversized_payload_is_rejected)
{
	char payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE + 1];
	zassert_equal(aitsm_mqtt_publish_payload(NULL, 1), -EINVAL, NULL);
	zassert_equal(aitsm_mqtt_publish_payload(payload, 0), -EINVAL, NULL);
	zassert_equal(aitsm_mqtt_publish_payload(payload, sizeof(payload)), -EMSGSIZE, NULL);
}

ZTEST(mqtt_client, test_duplicate_connect_rejected_until_connack)
{
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_equal(aitsm_mqtt_connect(), -EALREADY, NULL);
	callbacks.cb.on_connack(MQTT_CONNECTION_ACCEPTED, false);
	assert_event(AITSM_APP_EVENT_MQTT_CONNECTED, 0);
	zassert_equal(aitsm_mqtt_connect(), -EALREADY, NULL);
	zassert_equal(connect_calls, 1, NULL);
}

ZTEST(mqtt_client, test_offline_operations_rejected)
{
	aitsm_mqtt_set_lte_available(false);
	zassert_equal(aitsm_mqtt_connect(), -ENETDOWN, NULL);
	zassert_equal(aitsm_mqtt_publish_payload("payload", 7), -ENOTCONN, NULL);
}

ZTEST(mqtt_client, test_late_connack_after_lte_loss_disconnects_on_worker)
{
	block_backend = true;
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	aitsm_mqtt_set_lte_available(false);
	k_sem_give(&backend_release);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	callbacks.cb.on_connack(MQTT_CONNECTION_ACCEPTED, false);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_DISCONNECTED, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
	zassert_equal(disconnect_calls, 1, NULL);
}

ZTEST(mqtt_client, test_stale_connect_error_suppressed_after_lte_loss)
{
	connect_result = -ETIMEDOUT;
	block_backend = true;
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	aitsm_mqtt_set_lte_available(false);
	k_sem_give(&backend_release);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
}

ZTEST(mqtt_client, test_duplicate_and_stale_pubacks_do_not_complete_new_publish)
{
	connected();
	zassert_ok(aitsm_mqtt_publish_payload("first", 5), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	uint16_t old_id = sent_id;
	callbacks.cb.on_puback(old_id + 1, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
	callbacks.cb.on_puback(old_id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
	callbacks.cb.on_puback(old_id, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
	aitsm_mqtt_set_lte_available(false);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_DISCONNECTED, 0);
	aitsm_mqtt_set_lte_available(true);
	k_sem_reset(&backend_started);
	connected();
	zassert_ok(aitsm_mqtt_publish_payload("second", 6), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	callbacks.cb.on_puback(old_id, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
	callbacks.cb.on_puback(sent_id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
}

ZTEST(mqtt_client, test_lte_loss_during_publish_suppresses_completion)
{
	connected();
	block_backend = true;
	publish_result = -EIO;
	zassert_ok(aitsm_mqtt_publish_payload("payload", 7), NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	aitsm_mqtt_set_lte_available(false);
	k_sem_give(&backend_release);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_DISCONNECTED, 0);
	callbacks.cb.on_puback(sent_id, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
}

static void gate_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	backend_enter();
}
K_WORK_DEFINE(gate, gate_handler);
static void block_worker(void)
{
	block_backend = true;
	zassert_true(k_work_submit_to_queue(worker, &gate) >= 0, NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
}

ZTEST(mqtt_client, test_queued_connect_cancelled_before_backend_entry)
{
	block_worker();
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	aitsm_mqtt_set_lte_available(false);
	k_sem_give(&backend_release);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_equal(connect_calls, 0, NULL);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
}

ZTEST(mqtt_client, test_queued_publish_discarded_after_lte_loss)
{
	connected();
	block_worker();
	zassert_ok(aitsm_mqtt_publish_payload("payload", 7), NULL);
	aitsm_mqtt_set_lte_available(false);
	k_sem_give(&backend_release);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_equal(publish_calls, 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_DISCONNECTED, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
}
