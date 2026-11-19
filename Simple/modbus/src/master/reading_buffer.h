#pragma once

#include <stddef.h>
#include <stdint.h>

/* One temperature reading from the slave, stamped with the master's clock. */
struct reading {
	uint64_t timestamp_ms;
	int16_t centi_celsius;
};

/* 120 readings at 5 s is 10 minutes, twice the Thingy's batch interval. */
#define READING_BUFFER_CAPACITY 120

/*
 * Store a reading. When the buffer is full the new reading is dropped and
 * counted, so nothing already waiting is overwritten. Safe to call from the
 * Modbus task while loop() reads the buffer.
 */
bool reading_buffer_add(const struct reading *reading);

size_t reading_buffer_count(void);

/* Copy up to max of the oldest readings to out and return how many. */
size_t reading_buffer_peek(struct reading *out, size_t max);

/* Remove the count oldest readings, once their DDATA has been acknowledged. */
void reading_buffer_remove(size_t count);

uint32_t reading_buffer_dropped(void);
