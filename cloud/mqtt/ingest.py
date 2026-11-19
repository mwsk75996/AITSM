#!/usr/bin/env python3
"""Small MQTT to QuestDB bridge for Project C (Sparkplug B and legacy JSON)."""

import json
import logging
import math
import os
import queue
import struct
import threading
import time
from datetime import datetime, timezone
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import paho.mqtt.client as mqtt
from paho.mqtt.packettypes import PacketTypes
from paho.mqtt.properties import Properties

MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.environ["MQTT_INGEST_USER"]
MQTT_PASSWORD = os.environ["MQTT_INGEST_PASSWORD"]
# Sparkplug B v1.0: spBv1.0/<group_id>/<message_type>/<edge_node_id>. The JSON
# topic is kept until every device runs the Sparkplug B firmware.
MQTT_TOPICS = [
    topic.strip()
    for topic in os.getenv("MQTT_TOPICS", "spBv1.0/+/NBIRTH/+,spBv1.0/+/NDATA/+,aitsm/+/telemetry").split(",")
]
SPARKPLUG_NAMESPACE = "spBv1.0"
MQTT_CLIENT_ID = "projekt-c-questdb-ingest"
MQTT_SESSION_EXPIRY_SECONDS = 86400
# Unacknowledged deliveries the broker may send at once; the rest wait in its queue.
MQTT_RECEIVE_MAXIMUM = 10
QUESTDB_RETRY_INITIAL_SECONDS = 1
QUESTDB_RETRY_MAX_SECONDS = 30
QUESTDB_WRITE_URL = os.getenv("QUESTDB_WRITE_URL", "http://127.0.0.1:9000/write")
# Only the nRF9151 internal chip temperature and battery level are in scope.
NUMERIC_FIELDS = ("temperature", "battery")


def parse_timestamp(value):
    if value is None:
        return datetime.now(timezone.utc)
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        seconds = float(value) / 1000 if value > 10_000_000_000 else float(value)
        return datetime.fromtimestamp(seconds, timezone.utc)
    if isinstance(value, str):
        parsed = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
        return parsed.replace(tzinfo=parsed.tzinfo or timezone.utc).astimezone(timezone.utc)
    raise ValueError("timestamp skal være ISO-8601 eller Unix-tid")


def timestamp_ns(value):
    return int(parse_timestamp(value).timestamp() * 1_000_000_000)


def finite_number(value):
    if value is None or isinstance(value, bool):
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def escape_tag(value):
    return str(value).replace("\\", "\\\\").replace(",", "\\,").replace(" ", "\\ ").replace("=", "\\=")


def build_line(payload, topic):
    topic_parts = topic.split("/")
    default_device_id = topic_parts[1] if len(topic_parts) > 1 and topic_parts[1] else "unknown"
    device_id = payload.get("device_id") or payload.get("deviceId") or default_device_id
    if not isinstance(device_id, str) or not device_id.strip():
        raise ValueError("device_id mangler")

    values = payload.get("values", payload)
    if not isinstance(values, dict):
        raise ValueError("values skal være et JSON-objekt")

    fields = {}
    for name in NUMERIC_FIELDS:
        source_value = values.get("chip_temperature") if name == "temperature" else values.get(name)
        if name == "temperature" and source_value is None:
            source_value = values.get("temperature")
        number = finite_number(source_value)
        if number is not None:
            fields[name] = number
    if not fields:
        raise ValueError("ingen kendte numeriske sensorværdier")

    field_text = ",".join(f"{name}={value}" for name, value in fields.items())
    return f"sensor_readings,device_id={escape_tag(device_id.strip())} {field_text} {timestamp_ns(payload.get('timestamp'))}\n"


def build_lines(payload, topic):
    """Build one QuestDB line for a flat payload or each item in a batch."""

    readings = payload.get("readings")
    if readings is None:
        return [build_line(payload, topic)]
    if not isinstance(readings, list) or not readings:
        raise ValueError("readings skal være en ikke-tom JSON-liste")

    lines = []
    for reading in readings:
        if not isinstance(reading, dict):
            raise ValueError("hver reading skal være et JSON-objekt")

        reading_payload = dict(reading)
        for key in ("device_id", "deviceId"):
            if key in payload:
                reading_payload.setdefault(key, payload[key])
        lines.append(build_line(reading_payload, topic))
    return lines


def read_varint(data, position):
    result = 0
    shift = 0
    while True:
        if position >= len(data) or shift > 63:
            raise ValueError("ugyldig varint i Sparkplug B-payload")
        byte = data[position]
        position += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, position
        shift += 7


def decode_fields(data):
    """Yield (field number, wire type, value) for a protobuf message.

    Only the four wire types Sparkplug B uses are supported. Length-delimited
    values are returned as bytes, fixed-size values as bytes of that size.
    """

    position = 0
    while position < len(data):
        key, position = read_varint(data, position)
        field, wire_type = key >> 3, key & 7
        if wire_type == 0:
            value, position = read_varint(data, position)
        elif wire_type == 2:
            length, position = read_varint(data, position)
            value = data[position : position + length]
            position += length
            if len(value) != length:
                raise ValueError("Sparkplug B-felt er afkortet")
        elif wire_type in (1, 5):
            size = 8 if wire_type == 1 else 4
            value = data[position : position + size]
            position += size
            if len(value) != size:
                raise ValueError("Sparkplug B-felt er afkortet")
        else:
            raise ValueError(f"ukendt protobuf wire type {wire_type}")
        yield field, wire_type, value


def decode_metric(data):
    """Decode one Sparkplug B Payload.Metric into a dict."""

    metric = {"name": None, "timestamp": None, "is_null": False, "value": None}
    for field, _, value in decode_fields(data):
        if field == 1:
            metric["name"] = value.decode("utf-8")
        elif field == 3:
            metric["timestamp"] = value
        elif field == 7:
            metric["is_null"] = bool(value)
        elif field in (10, 11, 14):
            metric["value"] = value
        elif field == 12:
            metric["value"] = struct.unpack("<f", value)[0]
        elif field == 13:
            metric["value"] = struct.unpack("<d", value)[0]
    return metric


def decode_sparkplug_payload(data):
    """Decode a Sparkplug B Payload into (seq, timestamp, metrics)."""

    seq = timestamp = None
    metrics = []
    for field, _, value in decode_fields(data):
        if field == 1:
            timestamp = value
        elif field == 2:
            metrics.append(decode_metric(value))
        elif field == 3:
            seq = value
    return seq, timestamp, metrics


def build_sparkplug_lines(topic, data):
    """Build QuestDB lines for a Sparkplug B NDATA message; NBIRTH only logs."""

    parts = topic.split("/")
    if len(parts) < 4 or parts[0] != SPARKPLUG_NAMESPACE:
        raise ValueError("ugyldigt Sparkplug B-topic")
    message_type, edge_node_id = parts[2], parts[3]
    seq, payload_timestamp, metrics = decode_sparkplug_payload(data)

    if message_type == "NBIRTH":
        logging.info("Sparkplug B NBIRTH from %s with %d metric(s)", edge_node_id, len(metrics))
        return []
    if message_type not in ("NDATA", "DDATA"):
        raise ValueError(f"Sparkplug B-beskedtype {message_type} understøttes ikke")

    # One reading per metric timestamp: temperature and battery share it.
    readings = {}
    for metric in metrics:
        if metric["name"] not in NUMERIC_FIELDS or metric["is_null"] or metric["value"] is None:
            continue
        timestamp = metric["timestamp"] if metric["timestamp"] is not None else payload_timestamp
        if timestamp is None:
            raise ValueError("Sparkplug B-måling uden timestamp")
        readings.setdefault(timestamp, {})[metric["name"]] = round(float(metric["value"]), 2)
    if not readings:
        raise ValueError("ingen kendte numeriske sensorværdier")

    logging.info("Sparkplug B %s from %s seq=%s with %d reading(s)", message_type, edge_node_id, seq, len(readings))
    return [
        build_line({"device_id": edge_node_id, "timestamp": timestamp, **values}, topic)
        for timestamp, values in sorted(readings.items())
    ]


def write_questdb(lines):
    if isinstance(lines, str):
        lines = [lines]
    request = Request(
        QUESTDB_WRITE_URL,
        data="".join(lines).encode("utf-8"),
        headers={"Content-Type": "text/plain; charset=utf-8"},
        method="POST",
    )
    with urlopen(request, timeout=10) as response:
        if response.status < 200 or response.status >= 300:
            raise RuntimeError(f"QuestDB svarede HTTP {response.status}")


def on_connect(client, userdata, flags, rc, properties=None):
    if rc != 0:
        logging.error("MQTT connection failed: rc=%s", rc)
        return
    result, _ = client.subscribe([(topic, 1) for topic in MQTT_TOPICS])
    logging.info("Connected to MQTT; subscribe result=%s topics=%s", result, MQTT_TOPICS)


def on_subscribe(client, userdata, mid, granted_qos, properties=None):
    logging.info("MQTT subscription acknowledged: qos=%s", granted_qos)


def parse_message(message):
    if message.topic.startswith(SPARKPLUG_NAMESPACE + "/"):
        return build_sparkplug_lines(message.topic, message.payload)
    payload = json.loads(message.payload.decode("utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("payload skal være et JSON-objekt")
    return build_lines(payload, message.topic)


def store_message(message):
    """Parse and write one message synchronously, without MQTT acknowledgement."""
    lines = parse_message(message)
    if lines:
        write_questdb(lines)
    return lines


class DeliveryWorker:
    """Store messages in order and acknowledge each one only once it is handled.

    A message is handled when QuestDB has accepted its rows, or when the
    payload itself is invalid. Temporary write errors are retried with
    backoff; the message stays unacknowledged, so the persistent session
    redelivers it after a disconnect or restart.
    """

    def __init__(self, client, sleep=time.sleep):
        self.client = client
        self.sleep = sleep
        self.queue = queue.Queue(maxsize=MQTT_RECEIVE_MAXIMUM)
        self.lock = threading.Lock()
        self.connection = 0

    def submit(self, message):
        with self.lock:
            connection = self.connection
        try:
            self.queue.put_nowait((connection, message))
        except queue.Full:
            # Only if the broker ignores Receive Maximum. Without PUBACK the
            # message is redelivered on the next connection.
            logging.error("Delivery queue full; %s stays unacknowledged until reconnect", message.topic)

    def connection_lost(self):
        """Forget queued deliveries: the broker redelivers all unacknowledged ones."""
        with self.lock:
            self.connection += 1
        while True:
            try:
                self.queue.get_nowait()
            except queue.Empty:
                return

    def run(self):
        while True:
            self.process(*self.queue.get())

    def process(self, connection, message):
        try:
            lines = parse_message(message)
        except (ValueError, TypeError) as error:
            logging.warning("Rejected MQTT telemetry on %s: %s", message.topic, error)
            self.ack(connection, message)
            return

        if not lines:
            # Messages such as NBIRTH carry no readings; nothing to store.
            self.ack(connection, message)
            return

        delay = QUESTDB_RETRY_INITIAL_SECONDS
        while True:
            try:
                write_questdb(lines)
            except HTTPError as error:
                if error.code != 400:
                    self.retry_later(message, error, delay)
                else:
                    # QuestDB rejected the rows themselves; a retry cannot succeed.
                    logging.error("QuestDB rejected telemetry on %s: %s", message.topic, error)
                    break
            except Exception as error:
                # Connection errors, timeouts and 5xx may succeed later.
                self.retry_later(message, error, delay)
            else:
                logging.info("Stored %d telemetry row(s) from topic %s", len(lines), message.topic)
                break
            if not self.is_current(connection):
                logging.info("Connection lost while waiting; broker redelivers %s", message.topic)
                return
            delay = min(delay * 2, QUESTDB_RETRY_MAX_SECONDS)
        self.ack(connection, message)

    def retry_later(self, message, error, delay):
        logging.warning("QuestDB write failed for %s: %s; retrying in %s s", message.topic, error, delay)
        self.sleep(delay)

    def is_current(self, connection):
        with self.lock:
            return connection == self.connection

    def ack(self, connection, message):
        with self.lock:
            # A PUBACK for an old connection could acknowledge another message
            # that reuses the packet id; the redelivered copy is acked instead.
            if connection == self.connection:
                self.client.ack(message.mid, message.qos)


def on_message(client, userdata, message):
    logging.info("Received telemetry on %s", message.topic)
    userdata.submit(message)


def on_disconnect(client, userdata, rc, properties=None):
    logging.warning("MQTT disconnected: rc=%s", rc)
    userdata.connection_lost()


def create_client():
    """Keep one broker-side subscription across process restarts."""
    # manual_ack requires paho-mqtt 2.0; older versions fail here at startup.
    client = mqtt.Client(client_id=MQTT_CLIENT_ID, protocol=mqtt.MQTTv5, manual_ack=True)
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.reconnect_delay_set(min_delay=2, max_delay=60)
    return client


def connect_client(client):
    properties = Properties(PacketTypes.CONNECT)
    properties.SessionExpiryInterval = MQTT_SESSION_EXPIRY_SECONDS
    properties.ReceiveMaximum = MQTT_RECEIVE_MAXIMUM
    # False applies to the first connection too, including a new process.
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60,
                   clean_start=False, properties=properties)


def main():
    logging.basicConfig(level=os.getenv("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
    client = create_client()
    worker = DeliveryWorker(client)
    client.user_data_set(worker)
    connect_client(client)
    # With Paho's own network thread, ack() from the writer only queues the
    # PUBACK; the network thread sends it and handles reconnects.
    client.loop_start()
    worker.run()


if __name__ == "__main__":
    main()
