#pragma once

#include <stdint.h>

/* Shared between slave and master, so both agree on network, addresses and units. */

/*
 * The master runs its own access point (MODBUS_AP_SSID in credentials.h)
 * for the Modbus network and is 192.168.4.1 on it. The slave joins with a
 * fixed address, so the master always knows where to find it.
 */
#define MODBUS_SLAVE_IP 192, 168, 4, 10
#define MODBUS_MASTER_IP 192, 168, 4, 1
#define MODBUS_NETMASK 255, 255, 255, 0
#define MODBUS_TCP_PORT 502
/* Modbus unit (server) id; the slave only answers requests for this id. */
#define MODBUS_UNIT_ID 1

/* Input registers (function code 04, read-only). */
enum modbus_input_register : uint16_t {
	/* Temperature in hundredths of a degree Celsius, signed (2345 = 23.45 C). */
	MODBUS_IREG_TEMPERATURE = 0,
	/* 1 when the last DS18B20 reading succeeded, 0 otherwise. */
	MODBUS_IREG_SENSOR_OK = 1,
	/* Counts successful readings; wraps at 65535. Shows the value is fresh. */
	MODBUS_IREG_READING_COUNT = 2,
	MODBUS_IREG_COUNT = 3,
};

/*
 * Measurement and polling interval: 15 s like the Thingy, so the two kinds of
 * devices have the same number of readings in QuestDB.
 */
#define MODBUS_INTERVAL_MS 15000U
