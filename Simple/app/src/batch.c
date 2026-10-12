#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "batch.h"

BUILD_ASSERT(CONFIG_AITSM_BATCH_SIZE <= CONFIG_AITSM_BATCH_CAPACITY,
	     "CONFIG_AITSM_BATCH_SIZE must fit in CONFIG_AITSM_BATCH_CAPACITY");

static struct aitsm_measurement buffer[CONFIG_AITSM_BATCH_CAPACITY];
static size_t count;

void aitsm_batch_reset(void)
{
	count = 0;
}

int aitsm_batch_add(const struct aitsm_measurement *measurement)
{
	if (count >= ARRAY_SIZE(buffer)) {
		return -ENOSPC;
	}

	buffer[count++] = *measurement;
	return 0;
}

size_t aitsm_batch_count(void)
{
	return count;
}

bool aitsm_batch_is_ready(void)
{
	return count >= CONFIG_AITSM_BATCH_SIZE;
}

const struct aitsm_measurement *aitsm_batch_data(void)
{
	return buffer;
}

void aitsm_batch_remove(size_t removed)
{
	removed = MIN(removed, count);

	/* Measurements added while waiting for the acknowledgement move to the front. */
	memmove(buffer, &buffer[removed], (count - removed) * sizeof(buffer[0]));
	count -= removed;
}
