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
	backend_enter();
	return connect_result;
}

int mqtt_helper_publish(const struct mqtt_publish_param *param)
{
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
	block_backend = true;
	zassert_true(aitsm_mqtt_connect() >= 0, NULL);
	zassert_ok(k_sem_take(&backend_started, K_SECONDS(1)), NULL);
	zassert_ok(aitsm_mqtt_publish_payload(payload, strlen(payload)), NULL);
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
