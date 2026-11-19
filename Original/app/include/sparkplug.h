#ifndef AITSM_SPARKPLUG_H_
#define AITSM_SPARKPLUG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <data_transmission.h>

/** Sparkplug B v1.0 message types used by the edge node. */
enum aitsm_sparkplug_message_type {
	AITSM_SPARKPLUG_NBIRTH,
	AITSM_SPARKPLUG_NDATA,
};

/** Sequence numbers wrap from 255 to 0 (Sparkplug B v1.0, section 6.4.19). */
#define AITSM_SPARKPLUG_SEQ_MODULO 256U

/** Metric names shared by NBIRTH and NDATA. */
#define AITSM_SPARKPLUG_METRIC_TEMPERATURE "temperature"
#define AITSM_SPARKPLUG_METRIC_BATTERY "battery"
#define AITSM_SPARKPLUG_METRIC_BDSEQ "bdSeq"

/**
 * Upper bound for an NDATA payload with count measurements: payload header
 * (timestamp and seq) plus two metrics per measurement, each with name,
 * timestamp, datatype, historical flag and a float value.
 */
#define AITSM_SPARKPLUG_NDATA_MAX_SIZE(count) (16U + (count) * 70U)

/**
 * Write the topic spBv1.0/<group_id>/<message type>/<edge_node_id>.
 * Returns -EMSGSIZE if it does not fit, including the terminating NUL.
 */
int aitsm_sparkplug_topic(char *buffer, size_t capacity,
			  enum aitsm_sparkplug_message_type type);

/** Return the next sequence number after seq, wrapping at 256. */
uint8_t aitsm_sparkplug_next_seq(uint8_t seq);

/**
 * Encode an NBIRTH payload with seq 0, the bdSeq metric and the metrics that
 * later NDATA messages carry (without values).
 */
int aitsm_sparkplug_encode_nbirth(uint8_t *buffer, size_t capacity, size_t *length,
				  uint64_t timestamp_ms, uint8_t bd_seq);

/**
 * Encode an NDATA payload with temperature and battery metrics for each
 * measurement. Metrics of buffered readings are marked historical, so the
 * consumer can tell them from live values. Measurement timestamps are
 * converted from seconds to the milliseconds Sparkplug B uses.
 */
int aitsm_sparkplug_encode_ndata(uint8_t *buffer, size_t capacity, size_t *length,
				 const struct aitsm_measurement *measurements, size_t count,
				 uint8_t seq, bool historical);

#endif /* AITSM_SPARKPLUG_H_ */
