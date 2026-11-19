#include "sparkplug.h"

#include <stdio.h>
#include <string.h>

#include <pb_encode.h>

#include "sparkplug_b.pb.h"

/* Sparkplug B DataType enumeration values used here. */
#define DATATYPE_INT64 4U
#define DATATYPE_FLOAT 9U

static const char *const message_type_names[] = {
	[SPARKPLUG_NBIRTH] = "NBIRTH",
	[SPARKPLUG_NDEATH] = "NDEATH",
	[SPARKPLUG_DBIRTH] = "DBIRTH",
	[SPARKPLUG_DDATA] = "DDATA",
};

struct metrics_context {
	enum sparkplug_message_type type;
	const struct reading *readings;
	size_t count;
	uint8_t bd_seq;
};

typedef org_eclipse_tahu_protobuf_Payload Payload;
typedef org_eclipse_tahu_protobuf_Payload_Metric Metric;

static bool encode_metric(pb_ostream_t *stream, const Metric *metric)
{
	return pb_encode_tag(stream, PB_WT_STRING, org_eclipse_tahu_protobuf_Payload_metrics_tag) &&
	       pb_encode_submessage(stream, org_eclipse_tahu_protobuf_Payload_Metric_fields, metric);
}

static bool encode_bdseq(pb_ostream_t *stream, uint8_t bd_seq)
{
	Metric metric = org_eclipse_tahu_protobuf_Payload_Metric_init_zero;

	metric.has_name = true;
	strcpy(metric.name, SPARKPLUG_METRIC_BDSEQ);
	metric.has_datatype = true;
	metric.datatype = DATATYPE_INT64;
	metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_long_value_tag;
	metric.value.long_value = bd_seq;
	return encode_metric(stream, &metric);
}

static bool encode_temperature(pb_ostream_t *stream, const struct reading *reading)
{
	Metric metric = org_eclipse_tahu_protobuf_Payload_Metric_init_zero;

	metric.has_name = true;
	strcpy(metric.name, SPARKPLUG_METRIC_TEMPERATURE);
	metric.has_datatype = true;
	metric.datatype = DATATYPE_FLOAT;
	if (reading == NULL) {
		/* DBIRTH declares the metric without a value. */
		metric.has_is_null = true;
		metric.is_null = true;
	} else {
		metric.has_timestamp = true;
		metric.timestamp = reading->timestamp_ms;
		/* Measured before it was sent, as part of a batch. */
		metric.has_is_historical = true;
		metric.is_historical = true;
		metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_float_value_tag;
		metric.value.float_value = (float)reading->centi_celsius / 100.0f;
	}
	return encode_metric(stream, &metric);
}

static bool encode_metrics(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct metrics_context *context = (const struct metrics_context *)*arg;

	(void)field;
	switch (context->type) {
	case SPARKPLUG_NBIRTH:
	case SPARKPLUG_NDEATH:
		return encode_bdseq(stream, context->bd_seq);
	case SPARKPLUG_DBIRTH:
		return encode_temperature(stream, NULL);
	case SPARKPLUG_DDATA:
		for (size_t i = 0; i < context->count; i++) {
			if (!encode_temperature(stream, &context->readings[i])) {
				return false;
			}
		}
		return true;
	}
	return false;
}

static bool encode_payload(uint8_t *buffer, size_t capacity, size_t *length,
			   const struct metrics_context *context, uint64_t timestamp_ms,
			   bool has_seq, uint8_t seq)
{
	Payload payload = org_eclipse_tahu_protobuf_Payload_init_zero;
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, capacity);

	payload.metrics.funcs.encode = encode_metrics;
	payload.metrics.arg = (void *)context;
	if (timestamp_ms != 0) {
		payload.has_timestamp = true;
		payload.timestamp = timestamp_ms;
	}
	if (has_seq) {
		payload.has_seq = true;
		payload.seq = seq;
	}
	if (!pb_encode(&stream, org_eclipse_tahu_protobuf_Payload_fields, &payload)) {
		return false;
	}
	*length = stream.bytes_written;
	return true;
}

bool sparkplug_topic(char *buffer, size_t capacity, enum sparkplug_message_type type)
{
	bool device_level = type == SPARKPLUG_DBIRTH || type == SPARKPLUG_DDATA;
	int written = device_level ? snprintf(buffer, capacity, "spBv1.0/%s/%s/%s/%s",
					      SPARKPLUG_GROUP_ID, message_type_names[type],
					      SPARKPLUG_EDGE_NODE_ID, SPARKPLUG_DEVICE_ID)
				   : snprintf(buffer, capacity, "spBv1.0/%s/%s/%s",
					      SPARKPLUG_GROUP_ID, message_type_names[type],
					      SPARKPLUG_EDGE_NODE_ID);

	return written > 0 && (size_t)written < capacity;
}

bool sparkplug_encode_nbirth(uint8_t *buffer, size_t capacity, size_t *length,
			     uint64_t timestamp_ms, uint8_t bd_seq)
{
	const struct metrics_context context = {SPARKPLUG_NBIRTH, NULL, 0, bd_seq};

	return encode_payload(buffer, capacity, length, &context, timestamp_ms, true, 0);
}

bool sparkplug_encode_ndeath(uint8_t *buffer, size_t capacity, size_t *length, uint8_t bd_seq)
{
	const struct metrics_context context = {SPARKPLUG_NDEATH, NULL, 0, bd_seq};

	/* NDEATH has no seq; the broker sends it long after it was encoded. */
	return encode_payload(buffer, capacity, length, &context, 0, false, 0);
}

bool sparkplug_encode_dbirth(uint8_t *buffer, size_t capacity, size_t *length,
			     uint64_t timestamp_ms, uint8_t seq)
{
	const struct metrics_context context = {SPARKPLUG_DBIRTH, NULL, 0, 0};

	return encode_payload(buffer, capacity, length, &context, timestamp_ms, true, seq);
}

bool sparkplug_encode_ddata(uint8_t *buffer, size_t capacity, size_t *length,
			    const struct reading *readings, size_t count, uint8_t seq)
{
	const struct metrics_context context = {SPARKPLUG_DDATA, readings, count, 0};

	if (readings == NULL || count == 0) {
		return false;
	}
	return encode_payload(buffer, capacity, length, &context, readings[count - 1].timestamp_ms,
			      true, seq);
}
