#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/atomic.h>
#include <measurement_source.h>
#include <measurement_service.h>
#include <data_transmission.h>
#include <mqtt_client.h>
#include <sparkplug.h>
#include <sparkplug_decode.h>

#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
#define CAPACITY CONFIG_AITSM_BATCH_MAX_SAMPLES
#else
#define CAPACITY 1
#endif
K_SEM_DEFINE(read_done, 0, 16);
K_SEM_DEFINE(action_done, 0, 1);
struct sent {
	char topic[64];
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE];
	size_t length;
};
K_MSGQ_DEFINE(sent_queue, sizeof(struct sent), 8, 4);
static struct sent output;
static atomic_t read_count, publish_calls, birth_calls;
static int submit_result, birth_result;
static int source_result;
static int64_t first_timestamp;
static uint32_t drops, current_token, next_token, stale_token;

int aitsm_measurement_read(struct aitsm_measurement *sample)
{
	zassert_equal(k_current_get(), &k_sys_work_q.thread, NULL);
	int number = atomic_inc(&read_count);
	*sample = (struct aitsm_measurement) {
		.timestamp = 100 + k_uptime_get() / 1000,
		.temperature_centi_celsius = 2300 + number,
		.battery_centi_percent = 9800,
	};
	if (number == 0) { first_timestamp = sample->timestamp; }
	k_sem_give(&read_done);
	return source_result;
}

int aitsm_measurement_time_ms(int64_t *timestamp_ms)
{
	*timestamp_ms = 1700000000000LL;
	return 0;
}

int aitsm_mqtt_publish_payload(const char *topic, const uint8_t *payload, size_t length,
			       uint32_t *token)
{
	zassert_equal(k_current_get(), &k_sys_work_q.thread, NULL);
	const bool birth = strstr(topic, "/NBIRTH/") != NULL;
	atomic_inc(birth ? &birth_calls : &publish_calls);
	if (birth ? birth_result : submit_result) { return birth ? birth_result : submit_result; }
	*token = current_token = ++next_token;
	strncpy(output.topic, topic, sizeof(output.topic) - 1);
	memcpy(output.payload, payload, length);
	output.length = length;
	zassert_ok(k_msgq_put(&sent_queue, &output, K_NO_WAIT), NULL);
	return 0;
}

enum action { RESET, START, ONLINE, OFFLINE, ACK, FAIL, STATS, STALE_ACK, BARRIER };
static enum action next_action;
static void action_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	switch (next_action) {
	case RESET:
		zassert_ok(aitsm_measurement_service_init(), NULL);
		zassert_ok(aitsm_data_transmission_init(), NULL);
		break;
	case START: zassert_true(aitsm_measurement_service_start() >= 0, NULL); break;
	case ONLINE: aitsm_measurement_service_mqtt_connected(); break;
	case OFFLINE: aitsm_measurement_service_mqtt_disconnected(); break;
	case ACK: zassert_true(aitsm_measurement_service_publish_result(current_token, 0), NULL); break;
	case FAIL: zassert_true(aitsm_measurement_service_publish_result(current_token, -EIO), NULL); break;
	case STALE_ACK: zassert_false(aitsm_measurement_service_publish_result(stale_token, 0), NULL); break;
	case STATS: drops = aitsm_measurement_service_dropped_samples(); break;
	case BARRIER: break;
	}
	k_sem_give(&action_done);
}
K_WORK_DEFINE(action_work, action_handler);
static void act(enum action action)
{
	next_action = action;
	zassert_true(k_work_submit(&action_work) >= 0, NULL);
	zassert_ok(k_sem_take(&action_done, K_SECONDS(1)), NULL);
}
static void sample(void)
{
	zassert_ok(k_sem_take(&read_done, K_SECONDS(6)), NULL);
	act(BARRIER);  /* Wait until the sample has been stored, not just read. */
}
static struct decoded decoded;
/* Wait for the next published message and decode its Sparkplug B payload. */
static const struct decoded *sent(void)
{
	zassert_ok(k_msgq_get(&sent_queue, &output, K_SECONDS(1)), NULL);
	decode(output.payload, output.length, &decoded);
	return &decoded;
}
/* Connect and acknowledge the NBIRTH, so that NDATA can follow. */
static void online(void)
{
	act(ONLINE);
	(void)sent();
	zassert_not_null(strstr(output.topic, "/NBIRTH/"), "NBIRTH must come first");
	act(ACK);
}
static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	act(RESET);
	atomic_clear(&read_count); atomic_clear(&publish_calls); atomic_clear(&birth_calls);
	submit_result = birth_result = source_result = 0; drops = 0;
	k_sem_reset(&read_done);
	k_msgq_purge(&sent_queue);
}
ZTEST_SUITE(measurement_service, NULL, NULL, before, NULL, NULL);

ZTEST(measurement_service, test_sampling_before_mqtt_and_reconnect_drain)
{
	act(START);
	for (int i = 0; i < CAPACITY; i++) { sample(); }
	zassert_equal(atomic_get(&publish_calls), 0, "Offline sampling sent MQTT");
	online();
	const struct decoded *payload = sent();
	zassert_not_null(strstr(output.topic, "/NDATA/"), NULL);
	zassert_equal(payload->payload.seq, 1, "First NDATA after NBIRTH has seq 1");
	zassert_equal(payload->metrics[0].timestamp, (uint64_t)first_timestamp * 1000U, NULL);
	act(ACK);
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
}

ZTEST(measurement_service, test_full_buffer_skips_reads_and_reports_loss)
{
	act(START);
	for (int i = 0; i < CAPACITY; i++) { sample(); }
	k_sleep(K_SECONDS(6));
	act(STATS);
	zassert_equal(atomic_get(&read_count), CAPACITY, "Unretainable sensor read wastes power");
	zassert_true(drops >= 1, "Lost sample slots must be visible");
	zassert_equal(atomic_get(&publish_calls), 0, NULL);
	online(); (void)sent(); act(ACK);
	sample();
	zassert_equal(atomic_get(&read_count), CAPACITY + 1, NULL);
}

ZTEST(measurement_service, test_disconnect_retains_unacked_prefix_and_new_readings)
{
	act(START); sample();
	online(); (void)sent();
	act(OFFLINE);
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	sample();
	zassert_equal(atomic_get(&read_count), 2, NULL);
#endif
	online();
	const struct decoded *payload = sent();
	zassert_equal(payload->metrics[0].timestamp, (uint64_t)first_timestamp * 1000U,
		      "Old unacked reading lost");
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_within(payload->metrics[2].value.float_value, 23.01f, 0.001f,
		       "Offline reading absent");
#endif
	act(ACK);
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
}

ZTEST(measurement_service, test_delayed_puback_only_commits_sent_prefix)
{
	act(START); sample();
	online(); (void)sent();
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	sample();
#endif
	zassert_equal(atomic_get(&publish_calls), 1, "Parallel publish before ACK");
	act(ACK);
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE]; size_t length, count;
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
	zassert_equal(count, 1, NULL);
	decode(payload, length, &decoded);
	zassert_within(decoded.metrics[0].value.float_value, 23.01f, 0.001f, NULL);
#else
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
#endif
}

ZTEST(measurement_service, test_submission_error_retries_without_sampling_loop)
{
	act(START); sample();
	submit_result = -EBUSY;
	online(); act(BARRIER);
	zassert_equal(atomic_get(&publish_calls), 1, NULL);
	submit_result = 0;
	k_sleep(K_MSEC(200));
	zassert_equal(atomic_get(&publish_calls), 1, "Tight retry loop");
	zassert_ok(k_msgq_get(&sent_queue, &output, K_SECONDS(6)), NULL);
	zassert_equal(atomic_get(&publish_calls), 2, NULL);
	act(ACK);
}

ZTEST(measurement_service, test_disconnect_cancels_pending_publish_retry)
{
	act(START); sample();
	online(); (void)sent();
	act(FAIL); act(OFFLINE);
	k_sleep(K_SECONDS(6));
	zassert_equal(atomic_get(&publish_calls), 1, NULL);
	online(); (void)sent(); act(ACK);
}

ZTEST(measurement_service, test_invalid_time_or_sensor_error_is_not_buffered)
{
	source_result = -ENODATA;
	act(START); sample();
	act(ONLINE); act(BARRIER);
	zassert_equal(atomic_get(&publish_calls), 0, NULL);
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
	source_result = 0;
	sample(); act(OFFLINE); online(); (void)sent(); act(ACK);
}

ZTEST(measurement_service, test_queued_old_completion_cannot_remove_new_publish)
{
	act(START); sample();
	online(); (void)sent();
	stale_token = current_token;
	act(OFFLINE);
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	sample();
#endif
	online(); (void)sent();
	zassert_not_equal(current_token, stale_token, NULL);
	act(STALE_ACK);
	uint8_t payload[AITSM_DATA_TRANSMISSION_PAYLOAD_SIZE]; size_t length, count;
	zassert_ok(aitsm_data_transmission_format(payload, sizeof(payload), &length, &count), NULL);
#if defined(CONFIG_AITSM_TRANSMISSION_BATCH)
	zassert_equal(count, 2, "Stale completion removed unacknowledged readings");
#else
	zassert_equal(count, 1, NULL);
#endif
	zassert_equal(atomic_get(&publish_calls), 2, NULL);
	act(ACK); act(STALE_ACK);
	zassert_true(aitsm_data_transmission_has_capacity(), NULL);
}

ZTEST(measurement_service, test_nbirth_precedes_ndata_and_carries_bdseq)
{
	act(START); sample();
	act(ONLINE);
	const struct decoded *birth = sent();
	zassert_not_null(strstr(output.topic, "/NBIRTH/"), NULL);
	zassert_equal(birth->payload.seq, 0, "NBIRTH has seq 0");
	zassert_str_equal(birth->metrics[0].name, "bdSeq", NULL);
	zassert_equal(birth->metrics[0].value.long_value, 0, "First session has bdSeq 0");
	act(BARRIER);
	zassert_equal(atomic_get(&publish_calls), 0, "NDATA before NBIRTH was acknowledged");
	act(ACK);
	(void)sent();
	zassert_not_null(strstr(output.topic, "/NDATA/"), NULL);
	act(ACK);
}

ZTEST(measurement_service, test_bdseq_increases_for_every_connection)
{
	act(START); sample();
	online(); (void)sent(); act(ACK);
	act(OFFLINE);
	act(ONLINE);
	const struct decoded *birth = sent();
	zassert_equal(birth->metrics[0].value.long_value, 1, NULL);
	act(ACK);
}

ZTEST(measurement_service, test_failed_nbirth_is_retried_before_any_ndata)
{
	act(START); sample();
	birth_result = -EBUSY;
	act(ONLINE); act(BARRIER);
	zassert_equal(atomic_get(&birth_calls), 1, NULL);
	birth_result = 0;
	zassert_ok(k_msgq_get(&sent_queue, &output, K_SECONDS(6)), NULL);
	zassert_not_null(strstr(output.topic, "/NBIRTH/"), NULL);
	zassert_equal(atomic_get(&publish_calls), 0, NULL);
	act(ACK);
	(void)sent();
	zassert_not_null(strstr(output.topic, "/NDATA/"), NULL);
	act(ACK);
}

ZTEST(measurement_service, test_seq_restarts_after_new_nbirth)
{
	act(START); sample();
	online();
	zassert_equal(sent()->payload.seq, 1, NULL);
	act(ACK);
	act(OFFLINE);
	sample();
	online();
	zassert_equal(sent()->payload.seq, 1, "New session restarts seq at 1");
	act(ACK);
}
