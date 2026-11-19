#pragma once

#include <stddef.h>
#include <stdint.h>

#include "reading_buffer.h"

/*
 * Sparkplug B v1.0 roles: the master is the Edge Node, the slave with the
 * DS18B20 is a Device under it. The device id is what ends up as device_id in
 * QuestDB.
 */
#define SPARKPLUG_GROUP_ID "aitsm"
#define SPARKPLUG_EDGE_NODE_ID "esp32-gateway"
#define SPARKPLUG_DEVICE_ID "esp32-modbus"

/* The Thingy's batches; the gateway sends its own batch right after one. */
#define SPARKPLUG_NORDIC_NDATA_TOPIC "spBv1.0/aitsm/NDATA/thingy91x"

#define SPARKPLUG_METRIC_TEMPERATURE "temperature"
#define SPARKPLUG_METRIC_BDSEQ "bdSeq"

enum sparkplug_message_type {
	SPARKPLUG_NBIRTH,
	SPARKPLUG_NDEATH,
	SPARKPLUG_DBIRTH,
	SPARKPLUG_DDATA,
};

/* At most this many readings in one DDATA, so it fits SPARKPLUG_MAX_PAYLOAD. */
#define SPARKPLUG_DDATA_MAX_READINGS 60
#define SPARKPLUG_MAX_PAYLOAD 2048
#define SPARKPLUG_TOPIC_SIZE 64

/* Write spBv1.0/<group>/<type>/<edge node>[/<device>] to buffer. */
bool sparkplug_topic(char *buffer, size_t capacity, enum sparkplug_message_type type);

/* NBIRTH: seq 0 and the bdSeq of this MQTT session. */
bool sparkplug_encode_nbirth(uint8_t *buffer, size_t capacity, size_t *length,
			     uint64_t timestamp_ms, uint8_t bd_seq);

/* NDEATH, sent by the broker as last will: the same bdSeq as the NBIRTH. */
bool sparkplug_encode_ndeath(uint8_t *buffer, size_t capacity, size_t *length, uint8_t bd_seq);

/* DBIRTH: declares the temperature metric that DDATA carries. */
bool sparkplug_encode_dbirth(uint8_t *buffer, size_t capacity, size_t *length,
			     uint64_t timestamp_ms, uint8_t seq);

/* DDATA: one historical temperature metric per reading, with its own timestamp. */
bool sparkplug_encode_ddata(uint8_t *buffer, size_t capacity, size_t *length,
			    const struct reading *readings, size_t count, uint8_t seq);
