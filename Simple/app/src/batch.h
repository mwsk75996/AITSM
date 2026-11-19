#ifndef AITSM_BATCH_H_
#define AITSM_BATCH_H_

#include <stdbool.h>
#include <stddef.h>

#include "sensor.h"

/*
 * Fixed buffer of measurements that are sent together in one message.
 * The buffer has no locking: it is only used from the application thread.
 */

/** Empty the buffer. */
void aitsm_batch_reset(void);

/** Add a measurement. Returns -ENOSPC when the buffer is full; nothing is overwritten. */
int aitsm_batch_add(const struct aitsm_measurement *measurement);

/** Number of measurements in the buffer. */
size_t aitsm_batch_count(void);

/** True when the buffer holds CONFIG_AITSM_BATCH_SIZE measurements or more. */
bool aitsm_batch_is_ready(void);

/** The buffered measurements, oldest first. */
const struct aitsm_measurement *aitsm_batch_data(void);

/** Remove the count oldest measurements after the broker has acknowledged them. */
void aitsm_batch_remove(size_t count);

#endif /* AITSM_BATCH_H_ */
