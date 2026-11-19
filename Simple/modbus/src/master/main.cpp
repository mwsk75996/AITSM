/*
 * Modbus TCP master and Sparkplug B edge node.
 *
 * - Joins the phone hotspot for internet access and runs its own access
 *   point for the Modbus network, which the slave joins.
 * - Reads the slave's temperature every 5 s over Modbus TCP and buffers it
 *   with the time from NTP.
 * - Sends the buffer to the VPS as Sparkplug B DDATA over MQTT/TLS right after
 *   the Thingy:91 X has sent a batch (it subscribes to the Thingy's NDATA), so
 *   both batches arrive together. If the buffer gets three quarters full
 *   without a Thingy batch, it is sent anyway.
 *
 * eModbus and esp-mqtt run in their own FreeRTOS tasks; their results reach
 * loop() through the reading buffer and the MQTT event queue.
 */
#include <Arduino.h>
#include <ModbusClientTCP.h>
#include <WiFi.h>
#include <time.h>

#include "credentials.h"
#include "modbus_map.h"
#include "mqtt_link.h"
#include "reading_buffer.h"
#include "sparkplug.h"
#include "wifi_connect.h"

#define MASTER_HOSTNAME "aitsm-modbus-master"
#define RESPONSE_TIMEOUT_MS 2000
/* Without a PUBACK in this time the DDATA is sent again later. */
#define PUBACK_TIMEOUT_MS 30000
/* Send without waiting for the Thingy once the buffer is this full. */
#define FALLBACK_COUNT (READING_BUFFER_CAPACITY * 3 / 4)
/* Unix time after 2024 means NTP has set the clock. */
#define VALID_TIME_SECONDS 1704067200

static WiFiClient tcp_client;
static ModbusClientTCP modbus(tcp_client);
static uint32_t last_poll_ms;
static uint32_t next_token;

/* Sparkplug session state; only loop() touches it. */
static bool mqtt_connected;
static uint8_t bd_seq;
static uint8_t seq;
static bool nordic_batch_seen;
static int awaiting_msg_id = -1;
static uint32_t awaiting_since_ms;
static size_t sent_count;
static uint8_t payload[SPARKPLUG_MAX_PAYLOAD];
static struct reading batch[SPARKPLUG_DDATA_MAX_READINGS];

static uint64_t now_ms(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	if (now.tv_sec < VALID_TIME_SECONDS) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000U + now.tv_usec / 1000U;
}

static void on_modbus_data(ModbusMessage response, uint32_t token)
{
	uint8_t byte_count;
	uint16_t registers[MODBUS_IREG_COUNT];
	uint16_t index = response.get(2, byte_count);

	(void)token;
	if (byte_count != sizeof(registers)) {
		Serial.printf("Uventet svar: %u bytes\n", byte_count);
		return;
	}
	for (uint16_t i = 0; i < MODBUS_IREG_COUNT; i++) {
		index = response.get(index, registers[i]);
	}
	if (registers[MODBUS_IREG_SENSOR_OK] != 1) {
		Serial.println("Slaven har ingen gyldig måling");
		return;
	}

	struct reading reading = {now_ms(), (int16_t)registers[MODBUS_IREG_TEMPERATURE]};

	if (reading.timestamp_ms == 0) {
		Serial.println("Uret er ikke sat endnu; måling ikke gemt");
		return;
	}
	if (!reading_buffer_add(&reading)) {
		Serial.printf("Bufferen er fuld; måling sprunget over (%u i alt)\n",
			      reading_buffer_dropped());
		return;
	}
	Serial.printf("Temperatur fra slaven: %d.%02d C (måling %u, %u i bufferen)\n",
		      reading.centi_celsius / 100, abs(reading.centi_celsius) % 100,
		      registers[MODBUS_IREG_READING_COUNT], reading_buffer_count());
}

static void on_modbus_error(Error error, uint32_t token)
{
	ModbusError modbus_error(error);

	(void)token;
	Serial.printf("Modbus-fejl %02X: %s (enheder på AP: %d)\n", (int)modbus_error,
		      (const char *)modbus_error, WiFi.softAPgetStationNum());
}

static void poll_slave(void)
{
	Error error = modbus.addRequest(next_token++, MODBUS_UNIT_ID, READ_INPUT_REGISTER,
					MODBUS_IREG_TEMPERATURE, MODBUS_IREG_COUNT);

	if (error != SUCCESS) {
		ModbusError modbus_error(error);

		Serial.printf("Kunne ikke sende forespørgsel: %s\n", (const char *)modbus_error);
	}
}

static int publish(enum sparkplug_message_type type, size_t length)
{
	char topic[SPARKPLUG_TOPIC_SIZE];

	if (!sparkplug_topic(topic, sizeof(topic), type)) {
		return -1;
	}
	return mqtt_link_publish(topic, payload, length);
}

static void set_will(void)
{
	size_t length;

	if (sparkplug_encode_ndeath(payload, sizeof(payload), &length, bd_seq)) {
		mqtt_link_set_will(payload, length);
	}
}

/* A new session: NBIRTH (seq 0), DBIRTH (seq 1) and the Thingy subscription. */
static void start_session(void)
{
	size_t length;

	seq = 0;
	if (sparkplug_encode_nbirth(payload, sizeof(payload), &length, now_ms(), bd_seq)) {
		publish(SPARKPLUG_NBIRTH, length);
	}
	seq = 1;
	if (sparkplug_encode_dbirth(payload, sizeof(payload), &length, now_ms(), seq)) {
		publish(SPARKPLUG_DBIRTH, length);
	}
	seq = 2;
	mqtt_link_subscribe(SPARKPLUG_NORDIC_NDATA_TOPIC);
	Serial.printf("MQTT forbundet; NBIRTH og DBIRTH sendt (bdSeq %u)\n", bd_seq);
}

static void handle_mqtt_events(void)
{
	struct mqtt_link_event event;

	while (mqtt_link_next_event(&event)) {
		switch (event.type) {
		case MQTT_LINK_CONNECTED:
			mqtt_connected = true;
			start_session();
			break;
		case MQTT_LINK_DISCONNECTED:
			if (mqtt_connected) {
				Serial.println("MQTT afbrudt; forbinder igen");
				/* The broker has sent NDEATH; the next session gets a new bdSeq. */
				bd_seq++;
				set_will();
			}
			mqtt_connected = false;
			awaiting_msg_id = -1;
			break;
		case MQTT_LINK_PUBLISHED:
			if (event.msg_id == awaiting_msg_id) {
				reading_buffer_remove(sent_count);
				awaiting_msg_id = -1;
				seq++;
				Serial.printf("DDATA bekræftet; %u målinger fjernet fra bufferen\n",
					      (unsigned int)sent_count);
			}
			break;
		case MQTT_LINK_NORDIC_BATCH:
			Serial.println("Batch fra Thingy:91 X set; sender egne målinger");
			nordic_batch_seen = true;
			break;
		}
	}
}

static void send_batch(void)
{
	size_t count = reading_buffer_count();
	size_t length;
	int msg_id;

	if (!mqtt_connected || count == 0) {
		return;
	}
	if (awaiting_msg_id >= 0) {
		if (millis() - awaiting_since_ms < PUBACK_TIMEOUT_MS) {
			return;
		}
		Serial.println("Ingen PUBACK; sender igen");
		awaiting_msg_id = -1;
	}
	if (!nordic_batch_seen && count < FALLBACK_COUNT) {
		return;
	}
	if (!nordic_batch_seen) {
		Serial.println("Ingen batch fra Thingy'en i lang tid; sender alligevel");
	}

	sent_count = reading_buffer_peek(batch, SPARKPLUG_DDATA_MAX_READINGS);
	if (!sparkplug_encode_ddata(payload, sizeof(payload), &length, batch, sent_count, seq)) {
		Serial.println("Kunne ikke kode DDATA");
		return;
	}
	msg_id = publish(SPARKPLUG_DDATA, length);
	if (msg_id < 0) {
		return;
	}
	awaiting_msg_id = msg_id;
	awaiting_since_ms = millis();
	/* Keep sending after the Thingy batch until the buffer is empty. */
	if (sent_count == count) {
		nordic_batch_seen = false;
	}
	Serial.printf("DDATA sendt: %u målinger, %u bytes, seq %u\n", (unsigned int)sent_count,
		      (unsigned int)length, seq);
}

void setup()
{
	char will_topic[SPARKPLUG_TOPIC_SIZE];
	size_t will_length;

	Serial.begin(115200);
	Serial.println("\nAITSM Modbus-master");

	/* The hotspot first: the access point must use the same channel. */
	wifi_connect(MASTER_HOSTNAME, WIFI_SSID, WIFI_PASSWORD);
	wifi_start_access_point(MODBUS_AP_SSID, MODBUS_AP_PASSWORD);
	configTime(0, 0, "pool.ntp.org", "time.google.com");

	modbus.onDataHandler(&on_modbus_data);
	modbus.onErrorHandler(&on_modbus_error);
	modbus.setTimeout(RESPONSE_TIMEOUT_MS);
	modbus.begin();
	modbus.setTarget(IPAddress(MODBUS_SLAVE_IP), MODBUS_TCP_PORT);
	Serial.printf("Læser slaven på %s:%u hvert %u. sekund\n",
		      IPAddress(MODBUS_SLAVE_IP).toString().c_str(), MODBUS_TCP_PORT,
		      MODBUS_INTERVAL_MS / 1000U);

	sparkplug_topic(will_topic, sizeof(will_topic), SPARKPLUG_NDEATH);
	sparkplug_encode_ndeath(payload, sizeof(payload), &will_length, bd_seq);
	if (!mqtt_link_start(will_topic, payload, will_length)) {
		Serial.println("Kunne ikke starte MQTT");
	}
}

void loop()
{
	if (millis() - last_poll_ms >= MODBUS_INTERVAL_MS) {
		last_poll_ms = millis();
		poll_slave();
	}
	handle_mqtt_events();
	send_batch();

	delay(10);
}
