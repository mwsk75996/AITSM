#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * MQTT over TLS to the VPS with ESP-IDF's esp-mqtt client. It runs in its own
 * task and reconnects by itself; loop() handles what happened through
 * mqtt_link_next_event().
 */

enum mqtt_link_event_type {
	MQTT_LINK_CONNECTED,
	MQTT_LINK_DISCONNECTED,
	/* The broker acknowledged a QoS 1 publish (PUBACK); msg_id says which. */
	MQTT_LINK_PUBLISHED,
	/* A message arrived on SPARKPLUG_NORDIC_NDATA_TOPIC. */
	MQTT_LINK_NORDIC_BATCH,
};

struct mqtt_link_event {
	enum mqtt_link_event_type type;
	int msg_id;
};

/*
 * Start the client. The last will (will_payload) is published by the broker
 * if the connection is lost without a disconnect.
 */
bool mqtt_link_start(const char *will_topic, const uint8_t *will_payload, size_t will_length);

/* Replace the last will before the next reconnect (a new bdSeq). */
void mqtt_link_set_will(const uint8_t *will_payload, size_t will_length);

/* Publish with QoS 1. Returns the msg_id, or -1 if it could not be queued. */
int mqtt_link_publish(const char *topic, const uint8_t *payload, size_t length);

bool mqtt_link_subscribe(const char *topic);

/* Take the next event, if any, without waiting. */
bool mqtt_link_next_event(struct mqtt_link_event *event);
