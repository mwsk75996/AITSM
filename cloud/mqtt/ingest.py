#!/usr/bin/env python3
"""Small JSON-over-MQTT to QuestDB bridge for Project C."""

import json
import logging
import math
import os
from datetime import datetime, timezone
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

import paho.mqtt.client as mqtt

MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.environ["MQTT_INGEST_USER"]
MQTT_PASSWORD = os.environ["MQTT_INGEST_PASSWORD"]
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "aitsm/+/telemetry")
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
    result, _ = client.subscribe(MQTT_TOPIC, qos=1)
    logging.info("Connected to MQTT; subscribe result=%s topic=%s", result, MQTT_TOPIC)


def on_subscribe(client, userdata, mid, granted_qos, properties=None):
    logging.info("MQTT subscription acknowledged: qos=%s", granted_qos)


def on_message(client, userdata, message):
    logging.info("Received telemetry on %s", message.topic)
    try:
        payload = json.loads(message.payload.decode("utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("payload skal være et JSON-objekt")
        lines = build_lines(payload, message.topic)
        write_questdb(lines)
        logging.info("Stored %d telemetry row(s) from topic %s", len(lines), message.topic)
    except (ValueError, TypeError, json.JSONDecodeError, HTTPError, URLError, OSError) as error:
        logging.warning("Rejected MQTT telemetry on %s: %s", message.topic, error)


def main():
    logging.basicConfig(level=os.getenv("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
    client = mqtt.Client(client_id="projekt-c-questdb-ingest", protocol=mqtt.MQTTv5)
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=2, max_delay=60)
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    client.loop_forever()


if __name__ == "__main__":
    main()
