/*
 * Modbus TCP slave (server): measures the DS18B20 every 5 s and keeps the
 * latest value in input registers that the master reads.
 *
 * eModbus answers requests in its own FreeRTOS task, so the registers are
 * shared between that task and loop() and guarded by a spinlock.
 */
#include <Arduino.h>
#include <ModbusServerWiFi.h>

#include "modbus_map.h"
#include "temperature_sensor.h"
#include "wifi_connect.h"
#include "wifi_credentials.h"

#define SLAVE_HOSTNAME "aitsm-modbus-slave"

/* Up to two masters at once; a connection idle for 20 s is closed. */
#define MAX_CLIENTS 2
#define CLIENT_TIMEOUT_MS 20000

static ModbusServerWiFi server;
static uint16_t input_registers[MODBUS_IREG_COUNT];
static portMUX_TYPE registers_lock = portMUX_INITIALIZER_UNLOCKED;
static bool sensor_found;
static bool conversion_pending;
static uint32_t last_request_ms;
static uint16_t reading_count;

static void set_registers(int16_t centi_celsius, bool sensor_ok)
{
	portENTER_CRITICAL(&registers_lock);
	if (sensor_ok) {
		input_registers[MODBUS_IREG_TEMPERATURE] = (uint16_t)centi_celsius;
		input_registers[MODBUS_IREG_READING_COUNT] = reading_count;
	}
	input_registers[MODBUS_IREG_SENSOR_OK] = sensor_ok ? 1 : 0;
	portEXIT_CRITICAL(&registers_lock);
}

/* Function code 04: return count registers starting at address. */
static ModbusMessage read_input_registers(ModbusMessage request)
{
	uint16_t address;
	uint16_t count;
	ModbusMessage response;

	request.get(2, address, count);
	if (count == 0 || address + count > MODBUS_IREG_COUNT) {
		response.setError(request.getServerID(), request.getFunctionCode(),
				  ILLEGAL_DATA_ADDRESS);
		return response;
	}

	response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(count * 2));
	portENTER_CRITICAL(&registers_lock);
	for (uint16_t i = 0; i < count; i++) {
		response.add(input_registers[address + i]);
	}
	portEXIT_CRITICAL(&registers_lock);
	return response;
}

static void measure(void)
{
	int16_t centi_celsius;

	if (!sensor_found) {
		/* Try again, so a sensor connected after boot is picked up. */
		sensor_found = temperature_sensor_init();
		if (!sensor_found) {
			Serial.println("Ingen DS18B20 fundet på GPIO 4");
			set_registers(0, false);
			return;
		}
		Serial.println("DS18B20 fundet");
	}

	if (!temperature_sensor_read(&centi_celsius)) {
		Serial.println("Ugyldig måling fra DS18B20");
		set_registers(0, false);
		sensor_found = false;
		return;
	}

	reading_count++;
	set_registers(centi_celsius, true);
	Serial.printf("Måling %u: %d.%02d C\n", reading_count, centi_celsius / 100,
		      abs(centi_celsius) % 100);
}

void setup()
{
	Serial.begin(115200);
	Serial.println("\nAITSM Modbus-slave");

	sensor_found = temperature_sensor_init();
	Serial.println(sensor_found ? "DS18B20 fundet" : "Ingen DS18B20 fundet på GPIO 4");

	/* Join the master's access point with the fixed address the master polls. */
	wifi_connect(SLAVE_HOSTNAME, MODBUS_AP_SSID, MODBUS_AP_PASSWORD,
		     IPAddress(MODBUS_SLAVE_IP));

	server.registerWorker(MODBUS_UNIT_ID, READ_INPUT_REGISTER, &read_input_registers);
	server.start(MODBUS_TCP_PORT, MAX_CLIENTS, CLIENT_TIMEOUT_MS);
	Serial.printf("Modbus TCP-server på port %u, unit id %u\n", MODBUS_TCP_PORT,
		      MODBUS_UNIT_ID);
}

void loop()
{
	if (!conversion_pending && millis() - last_request_ms >= MODBUS_INTERVAL_MS) {
		last_request_ms = millis();
		if (sensor_found) {
			temperature_sensor_request();
			conversion_pending = true;
		} else {
			measure();
		}
	}
	if (conversion_pending && temperature_sensor_ready()) {
		conversion_pending = false;
		measure();
	}

	delay(10);
}
