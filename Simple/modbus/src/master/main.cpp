/*
 * Modbus TCP master (client): joins the phone hotspot for internet access and
 * runs its own access point for the Modbus network, which the slave joins.
 * Reads the slave's input registers every 5 s. Sending to the VPS comes later.
 *
 * eModbus sends the request and waits for the answer in its own FreeRTOS
 * task; the result arrives in on_data() or on_error().
 */
#include <Arduino.h>
#include <ModbusClientTCP.h>
#include <WiFi.h>

#include "modbus_map.h"
#include "wifi_connect.h"
#include "wifi_credentials.h"

#define MASTER_HOSTNAME "aitsm-modbus-master"
#define RESPONSE_TIMEOUT_MS 2000

static WiFiClient tcp_client;
static ModbusClientTCP modbus(tcp_client);
static uint32_t last_poll_ms;
static uint32_t next_token;

static void on_data(ModbusMessage response, uint32_t token)
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

	int16_t centi_celsius = (int16_t)registers[MODBUS_IREG_TEMPERATURE];

	Serial.printf("Temperatur fra slaven: %d.%02d C (måling %u)\n", centi_celsius / 100,
		      abs(centi_celsius) % 100, registers[MODBUS_IREG_READING_COUNT]);
}

static void on_error(Error error, uint32_t token)
{
	ModbusError modbus_error(error);

	(void)token;
	Serial.printf("Modbus-fejl %02X: %s (slaver på AP: %d)\n", (int)modbus_error,
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

void setup()
{
	Serial.begin(115200);
	Serial.println("\nAITSM Modbus-master");

	/* The hotspot first: the access point must use the same channel. */
	wifi_connect(MASTER_HOSTNAME, WIFI_SSID, WIFI_PASSWORD);
	wifi_start_access_point(MODBUS_AP_SSID, MODBUS_AP_PASSWORD);

	modbus.onDataHandler(&on_data);
	modbus.onErrorHandler(&on_error);
	modbus.setTimeout(RESPONSE_TIMEOUT_MS);
	modbus.begin();
	modbus.setTarget(IPAddress(MODBUS_SLAVE_IP), MODBUS_TCP_PORT);
	Serial.printf("Læser slaven på %s:%u hvert %u. sekund\n",
		      IPAddress(MODBUS_SLAVE_IP).toString().c_str(), MODBUS_TCP_PORT,
		      MODBUS_INTERVAL_MS / 1000U);
}

void loop()
{
	if (millis() - last_poll_ms >= MODBUS_INTERVAL_MS) {
		last_poll_ms = millis();
		poll_slave();
	}

	delay(10);
}
