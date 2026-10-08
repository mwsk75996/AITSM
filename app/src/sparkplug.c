#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include <pb_encode.h>

#include <sparkplug.h>
#include <sparkplug_b.pb.h>

/* Sparkplug B DataType enumeration values used here. */
#define SPARKPLUG_DATATYPE_INT64 4U
#define SPARKPLUG_DATATYPE_FLOAT 9U

static const char *const message_type_names[] = {
	[AITSM_SPARKPLUG_NBIRTH] = "NBIRTH",
	[AITSM_SPARKPLUG_NDEATH] = "NDEATH",
	[AITSM_SPARKPLUG_NDATA] = "NDATA",
};

struct metrics_context {
	enum aitsm_sparkplug_message_type type;
	const struct aitsm_measurement *measurements;
	size_t count;
	bool historical;
	uint8_t bd_seq;
};

static bool encode_metric(pb_ostream_t *stream,
			  const org_eclipse_tahu_protobuf_Payload_Metric *metric)
{
	return pb_encode_tag(stream, PB_WT_STRING,
			     org_eclipse_tahu_protobuf_Payload_metrics_tag) &&
	       pb_encode_submessage(stream, org_eclipse_tahu_protobuf_Payload_Metric_fields,
				    metric);
}

static bool encode_bdseq_metric(pb_ostream_t *stream, uint8_t bd_seq)
{
	org_eclipse_tahu_protobuf_Payload_Metric metric =
		org_eclipse_tahu_protobuf_Payload_Metric_init_zero;

	metric.has_name = true;
	strcpy(metric.name, AITSM_SPARKPLUG_METRIC_BDSEQ);
	metric.has_datatype = true;
	metric.datatype = SPARKPLUG_DATATYPE_INT64;
	metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_long_value_tag;
	metric.value.long_value = bd_seq;
	return encode_metric(stream, &metric);
}

static bool encode_float_metric(pb_ostream_t *stream, const char *name, float value,
				uint64_t timestamp_ms, bool historical, bool is_null)
{
	org_eclipse_tahu_protobuf_Payload_Metric metric =
		org_eclipse_tahu_protobuf_Payload_Metric_init_zero;

	metric.has_name = true;
	strncpy(metric.name, name, sizeof(metric.name) - 1);
	metric.has_datatype = true;
	metric.datatype = SPARKPLUG_DATATYPE_FLOAT;
	if (!is_null) {
		metric.has_timestamp = true;
		metric.timestamp = timestamp_ms;
		metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_float_value_tag;
		metric.value.float_value = value;
	} else {
		metric.has_is_null = true;
		metric.is_null = true;
	}
	if (historical) {
		metric.has_is_historical = true;
		metric.is_historical = true;
	}
	return encode_metric(stream, &metric);
}

static bool encode_metrics(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct metrics_context *context = *arg;

	(void)field;
	if (context->type != AITSM_SPARKPLUG_NDATA) {
		if (!encode_bdseq_metric(stream, context->bd_seq)) {
			return false;
		}
		if (context->type == AITSM_SPARKPLUG_NDEATH) {
			return true;
		}
		/* NBIRTH declares the metrics that NDATA carries, without values. */
		return encode_float_metric(stream, AITSM_SPARKPLUG_METRIC_TEMPERATURE, 0.0f, 0,
					   false, true) &&
		       encode_float_metric(stream, AITSM_SPARKPLUG_METRIC_BATTERY, 0.0f, 0,
					   false, true);
	}

	for (size_t i = 0; i < context->count; i++) {
		const struct aitsm_measurement *measurement = &context->measurements[i];
		uint64_t timestamp_ms = (uint64_t)measurement->timestamp * 1000U;

		if (!encode_float_metric(stream, AITSM_SPARKPLUG_METRIC_TEMPERATURE,
					 (float)measurement->temperature_centi_celsius / 100.0f,
					 timestamp_ms, context->historical, false) ||
		    !encode_float_metric(stream, AITSM_SPARKPLUG_METRIC_BATTERY,
					 (float)measurement->battery_centi_percent / 100.0f,
					 timestamp_ms, context->historical, false)) {
			return false;
		}
	}
	return true;
}

static int encode_payload(uint8_t *buffer, size_t capacity, size_t *length,
			  const struct metrics_context *context, uint64_t timestamp_ms,
			  bool has_seq, uint8_t seq)
{
	org_eclipse_tahu_protobuf_Payload payload = org_eclipse_tahu_protobuf_Payload_init_zero;
	pb_ostream_t stream;

	if (buffer == NULL || length == NULL) {
		return -EINVAL;
	}

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

	stream = pb_ostream_from_buffer(buffer, capacity);
	if (!pb_encode(&stream, org_eclipse_tahu_protobuf_Payload_fields, &payload)) {
		return -EMSGSIZE;
	}

	*length = stream.bytes_written;
	return 0;
}

int aitsm_sparkplug_topic(char *buffer, size_t capacity,
			  enum aitsm_sparkplug_message_type type)
{
	int written;

	if (buffer == NULL || (size_t)type >= ARRAY_SIZE(message_type_names)) {
		return -EINVAL;
	}

	written = snprintf(buffer, capacity, "spBv1.0/%s/%s/%s",
			   CONFIG_AITSM_SPARKPLUG_GROUP_ID, message_type_names[type],
			   CONFIG_AITSM_SPARKPLUG_EDGE_NODE_ID);
	if (written < 0 || (size_t)written >= capacity) {
		return -EMSGSIZE;
	}

	return 0;
}

uint8_t aitsm_sparkplug_next_seq(uint8_t seq)
{
	return (uint8_t)((seq + 1U) % AITSM_SPARKPLUG_SEQ_MODULO);
}

int aitsm_sparkplug_encode_nbirth(uint8_t *buffer, size_t capacity, size_t *length,
				  uint64_t timestamp_ms, uint8_t bd_seq)
{
	const struct metrics_context context = {
		.type = AITSM_SPARKPLUG_NBIRTH,
		.bd_seq = bd_seq,
	};

	return encode_payload(buffer, capacity, length, &context, timestamp_ms, true, 0);
}

int aitsm_sparkplug_encode_ndeath(uint8_t *buffer, size_t capacity, size_t *length,
				  uint8_t bd_seq)
{
	const struct metrics_context context = {
		.type = AITSM_SPARKPLUG_NDEATH,
		.bd_seq = bd_seq,
	};

	return encode_payload(buffer, capacity, length, &context, 0, false, 0);
}

int aitsm_sparkplug_encode_ndata(uint8_t *buffer, size_t capacity, size_t *length,
				 const struct aitsm_measurement *measurements, size_t count,
				 uint8_t seq, bool historical)
{
	const struct metrics_context context = {
		.type = AITSM_SPARKPLUG_NDATA,
		.measurements = measurements,
		.count = count,
		.historical = historical,
	};

	if (measurements == NULL || count == 0) {
		return -EINVAL;
	}

	return encode_payload(buffer, capacity, length, &context,
			      (uint64_t)measurements[count - 1U].timestamp * 1000U, true, seq);
}
