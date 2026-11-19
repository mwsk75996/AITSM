#include "reading_buffer.h"

#include <Arduino.h>

static struct reading readings[READING_BUFFER_CAPACITY];
static size_t head;
static size_t count;
static uint32_t dropped;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

bool reading_buffer_add(const struct reading *reading)
{
	bool added = false;

	portENTER_CRITICAL(&lock);
	if (count < READING_BUFFER_CAPACITY) {
		readings[(head + count) % READING_BUFFER_CAPACITY] = *reading;
		count++;
		added = true;
	} else {
		dropped++;
	}
	portEXIT_CRITICAL(&lock);
	return added;
}

size_t reading_buffer_count(void)
{
	size_t result;

	portENTER_CRITICAL(&lock);
	result = count;
	portEXIT_CRITICAL(&lock);
	return result;
}

size_t reading_buffer_peek(struct reading *out, size_t max)
{
	size_t n;

	portENTER_CRITICAL(&lock);
	n = count < max ? count : max;
	for (size_t i = 0; i < n; i++) {
		out[i] = readings[(head + i) % READING_BUFFER_CAPACITY];
	}
	portEXIT_CRITICAL(&lock);
	return n;
}

void reading_buffer_remove(size_t n)
{
	portENTER_CRITICAL(&lock);
	if (n > count) {
		n = count;
	}
	head = (head + n) % READING_BUFFER_CAPACITY;
	count -= n;
	portEXIT_CRITICAL(&lock);
}

uint32_t reading_buffer_dropped(void)
{
	uint32_t result;

	portENTER_CRITICAL(&lock);
	result = dropped;
	portEXIT_CRITICAL(&lock);
	return result;
}
