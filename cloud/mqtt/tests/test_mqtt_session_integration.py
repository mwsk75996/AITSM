"""Restart the real ingest process against disposable MQTT and QuestDB."""
import json
import os
import time

import paho.mqtt.client as mqtt
import pytest

import questdb_schema as schema


def test_batches_published_while_ingest_stopped_reach_questdb(disposable_services, start_ingest):
    url, host, port = disposable_services
    env = dict(os.environ, MQTT_HOST=host, MQTT_PORT=str(port), QUESTDB_WRITE_URL=url + "/write")
    publisher = mqtt.Client(client_id="session-test-publisher", protocol=mqtt.MQTTv5)
    try:
        start_ingest(env).stop()
        publisher.connect(host, port, keepalive=60)
        publisher.loop_start()
        rows = [{"timestamp": 1790143200 + i * 15, "temperature": 23 + i / 100,
                 "battery": 98 - i / 100} for i in range(40)]
        for batch in (rows[:20], rows[20:], rows[:20]):
            payload = json.dumps({"device_id": "thingy91x", "readings": batch})
            receipt = publisher.publish("aitsm/thingy91x/telemetry", payload, qos=1)
            receipt.wait_for_publish(timeout=5)
            assert receipt.is_published(), "Broker did not acknowledge offline batch"
        assert schema.query("SELECT count() AS n FROM sensor_readings", url) == [{"n": 0}]
        start_ingest(env)  # Fresh Paho object, same fixed client-id.
        expected = [{"temperature": row["temperature"], "battery": row["battery"]} for row in rows]
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            actual = schema.query("SELECT temperature, battery FROM sensor_readings ORDER BY timestamp", url)
            if actual == expected:
                break
            time.sleep(0.1)
        else:
            pytest.fail("Queued batches lost, changed or duplicated after ingest restart")
    finally:
        publisher.disconnect()
        publisher.loop_stop()
