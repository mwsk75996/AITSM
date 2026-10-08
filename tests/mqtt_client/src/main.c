#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>
#include <net/mqtt_helper.h>
#include <app_controller.h>
#include <mqtt_client.h>
#include <data_transmission.h>

struct event { enum aitsm_app_event_type type; int value; uint32_t token; };
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
#define TEST_TOPIC "spBv1.0/aitsm/NDATA/thingy91x"

static char sent_payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
static size_t sent_length;
static uint16_t sent_id;
static uint32_t submitted_token;
static bool sent_duplicate, ack_during_publish;
static int64_t sent_at;

int aitsm_app_post_event(enum aitsm_app_event_type type, int value)
{
	struct event event = { type, value, 0 };
	return k_msgq_put(&events, &event, K_NO_WAIT);
}

int aitsm_app_post_publish_result(uint32_t token, int result)
{
	struct event event = { AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, result, token };
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
	sent_id = param->message_id;
	sent_duplicate = param->dup_flag;
	sent_at = k_uptime_get();
	backend_enter();
	sent_length = param->message.payload.len;
	memcpy(sent_payload, param->message.payload.data, sent_length);
	sent_id = param->message_id;
	zassert_equal(param->message.topic.qos, MQTT_QOS_1_AT_LEAST_ONCE, NULL);
	zassert_equal(param->message.topic.topic.size, strlen(TEST_TOPIC), NULL);
	zassert_mem_equal(param->message.topic.topic.utf8, TEST_TOPIC, strlen(TEST_TOPIC), NULL);
	if (ack_during_publish) { callbacks.cb.on_puback(sent_id, 0); }
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
	ack_during_publish = false;
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
	if (type == AITSM_APP_EVENT_MQTT_PUBLISH_RESULT) {
		zassert_equal(event.token, submitted_token, "Completion for wrong publish");
	}
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
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)payload, strlen(payload), &submitted_token), NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	memset(payload, 'x', strlen(payload));
	zassert_equal(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"new", 3, &submitted_token), -EBUSY, NULL);
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
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"payload", 7, &submitted_token), NULL);
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
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"payload", 7, &submitted_token), NULL);
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
	zassert_equal(aitsm_mqtt_publish_payload(TEST_TOPIC, NULL, 1, &submitted_token), -EINVAL, NULL);
	zassert_equal(aitsm_mqtt_publish_payload(NULL, (const uint8_t *)"payload", 7, &submitted_token), -EINVAL, NULL);
	zassert_equal(aitsm_mqtt_publish_payload("", (const uint8_t *)"payload", 7, &submitted_token), -EMSGSIZE, NULL);
	zassert_equal(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)payload, 0, &submitted_token), -EINVAL, NULL);
	zassert_equal(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)payload, sizeof(payload), &submitted_token), -EMSGSIZE, NULL);
	zassert_equal(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"payload", 7, NULL), -EINVAL, NULL);
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
	zassert_equal(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"payload", 7, &submitted_token), -ENOTCONN, NULL);
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
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"first", 5, &submitted_token), NULL);
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
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"second", 6, &submitted_token), NULL);
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
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"payload", 7, &submitted_token), NULL);
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
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"payload", 7, &submitted_token), NULL);
	aitsm_mqtt_set_lte_available(false);
	k_sem_give(&backend_release);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_equal(publish_calls, 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_DISCONNECTED, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
}

static void first_publish(void)
{
	connected();
	k_sem_reset(&backend_started);
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"original", 8, &submitted_token), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	k_sem_reset(&backend_started);
	zassert_false(sent_duplicate, NULL);
}

static void wait_for_retry(uint16_t original_id, int64_t previous, int seconds)
{
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(seconds + 1)), "Missing PUBACK retry");
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_equal(sent_id, original_id, NULL);
	zassert_true(sent_duplicate, "Retry must set DUP");
	zassert_equal(sent_length, 8, NULL);
	zassert_mem_equal(sent_payload, "original", 8, NULL);
	zassert_true(sent_at - previous >= seconds * 1000 - 10, "Retry ran before deadline");
}

ZTEST(mqtt_client, test_waiting_puback_protects_payload_and_completion_token)
{
	first_publish();
	uint32_t original_token = submitted_token, unused;
	zassert_equal(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"new", 3, &unused), -EBUSY, NULL);
	callbacks.cb.on_puback(sent_id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
	zassert_equal(submitted_token, original_token, NULL);
}

ZTEST(mqtt_client, test_missing_puback_retries_on_same_connection_and_ack_cancels)
{
	first_publish();
	uint16_t id = sent_id;
	wait_for_retry(id, sent_at, 1);
	zassert_equal(publish_calls, 2, NULL);
	zassert_equal(connect_calls, 1, NULL);
	zassert_equal(disconnect_calls, 0, NULL);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
	callbacks.cb.on_puback(id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
	k_sleep(K_MSEC(2200));
	zassert_equal(publish_calls, 2, "Retry continued after ACK");
	uint32_t old_token = submitted_token;
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"second", 6, &submitted_token), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_not_equal(submitted_token, old_token, NULL);
	zassert_false(sent_duplicate, "New publication inherited DUP");
	callbacks.cb.on_puback(id, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
	callbacks.cb.on_puback(sent_id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
	zassert_equal(connect_calls, 1, NULL);
}

ZTEST(mqtt_client, test_puback_retry_delay_doubles_and_stays_bounded)
{
	first_publish();
	uint16_t id = sent_id;
	const int delays[] = { 1, 2, 4, 4 };
	for (size_t i = 0; i < ARRAY_SIZE(delays); i++) {
		wait_for_retry(id, sent_at, delays[i]);
	}
	zassert_equal(publish_calls, 5, NULL);
	zassert_equal(connect_calls, 1, NULL);
	callbacks.cb.on_puback(id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
}

ZTEST(mqtt_client, test_lte_loss_cancels_missing_puback_retry)
{
	first_publish();
	uint16_t old_id = sent_id;
	aitsm_mqtt_set_lte_available(false);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_DISCONNECTED, 0);
	k_sleep(K_MSEC(1200));
	zassert_equal(publish_calls, 1, NULL);
	callbacks.cb.on_puback(old_id, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, NULL);
}

ZTEST(mqtt_client, test_retry_failure_then_old_puback_cannot_complete_new_publish)
{
	first_publish();
	uint16_t old_id = sent_id;
	uint32_t old_token = submitted_token;
	publish_result = -EIO;
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(2)), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, -EIO);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	publish_result = 0;
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"second", 6, &submitted_token), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_not_equal(submitted_token, old_token, NULL);
	callbacks.cb.on_puback(old_id, 0);
	zassert_equal(k_msgq_num_used_get(&events), 0, "Old ACK completed newer data");
	callbacks.cb.on_puback(sent_id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
}

ZTEST(mqtt_client, test_ack_before_socket_write_returns_leaves_no_retry)
{
	connected();
	ack_during_publish = true;
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"original", 8, &submitted_token), NULL);
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
	k_sleep(K_MSEC(1200));
	zassert_equal(publish_calls, 1, NULL);
}


ZTEST(mqtt_client, test_ack_during_blocked_write_cannot_release_payload_early)
{
	connected();
	block_backend = true;
	k_sem_reset(&backend_started);
	zassert_ok(aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"original", 8, &submitted_token), NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	callbacks.cb.on_puback(sent_id, 0);
	assert_event(AITSM_APP_EVENT_MQTT_PUBLISH_RESULT, 0);
	uint32_t unused;
	int result = aitsm_mqtt_publish_payload(TEST_TOPIC, (const uint8_t *)"replacement", 11, &unused);
	k_sem_give(&backend_release);
	zassert_equal(result, -EBUSY, "ACK released payload before write finished");
	zassert_true(k_work_queue_drain(worker, false) >= 0, NULL);
	zassert_mem_equal(sent_payload, "original", 8, NULL);
}
