#ifndef AITSM_TEST_SPARKPLUG_DECODE_H_
#define AITSM_TEST_SPARKPLUG_DECODE_H_

/* Decodes Sparkplug B payloads in unit tests, so tests check the real
 * protobuf content instead of raw bytes.
 */
#include <string.h>

#include <zephyr/ztest.h>

#include <pb_decode.h>

#include <sparkplug_b.pb.h>

#define MAX_DECODED_METRICS 80

typedef org_eclipse_tahu_protobuf_Payload Payload;
typedef org_eclipse_tahu_protobuf_Payload_Metric Metric;

struct decoded {
	Payload payload;
	Metric metrics[MAX_DECODED_METRICS];
	size_t count;
};

static inline bool collect_metric(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	struct decoded *decoded = *arg;
	Metric metric = org_eclipse_tahu_protobuf_Payload_Metric_init_zero;

	(void)field;
	if (decoded->count >= MAX_DECODED_METRICS ||
	    !pb_decode(stream, org_eclipse_tahu_protobuf_Payload_Metric_fields, &metric)) {
		return false;
	}
	decoded->metrics[decoded->count++] = metric;
	return true;
}

static inline void decode(const uint8_t *buffer, size_t length, struct decoded *decoded)
{
	pb_istream_t stream = pb_istream_from_buffer(buffer, length);

	memset(decoded, 0, sizeof(*decoded));
	decoded->payload.metrics.funcs.decode = collect_metric;
	decoded->payload.metrics.arg = decoded;
	zassert_true(pb_decode(&stream, org_eclipse_tahu_protobuf_Payload_fields,
			       &decoded->payload), "Payload kunne ikke dekodes");
}

#endif /* AITSM_TEST_SPARKPLUG_DECODE_H_ */
