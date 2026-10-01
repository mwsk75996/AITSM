"""Restart the real ingest process against disposable MQTT and QuestDB."""
import json
import os
from pathlib import Path
from queue import Empty, Queue
import subprocess
import sys
from threading import Thread
import time

import paho.mqtt.client as mqtt
import pytest

import ingest
import questdb_schema as schema


def test_batches_published_while_ingest_stopped_reach_questdb():
    url = os.getenv("QUESTDB_INTEGRATION_URL")
    host = os.getenv("MQTT_INTEGRATION_HOST")
    if not url or not host:
        pytest.skip("Requires disposable MQTT and QuestDB instances")
    if schema.query("SELECT table_name FROM tables() WHERE table_name='sensor_readings'", url):
        pytest.fail("Disposable session test database must not already contain sensor_readings")
    schema.query("CREATE TABLE sensor_readings (timestamp TIMESTAMP, device_id SYMBOL, "
                 "temperature DOUBLE, battery DOUBLE) TIMESTAMP(timestamp) PARTITION BY DAY WAL", url)
    schema.enable_dedup(url)
    port = int(os.getenv("MQTT_INTEGRATION_PORT", "1883"))
    env = dict(os.environ, MQTT_HOST=host, MQTT_PORT=str(port), QUESTDB_WRITE_URL=url + "/write")
    process = None
    publisher = mqtt.Client(client_id="session-test-publisher", protocol=mqtt.MQTTv5)
    messages = Queue()

    def start_ingest():
        process = subprocess.Popen([sys.executable, str(Path(ingest.__file__))], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        def read_log():
            for line in process.stdout:
                messages.put(line)
        Thread(target=read_log, daemon=True).start()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                line = messages.get(timeout=0.2)
            except Empty:
                if process.poll() is not None:
                    pytest.fail("Ingest exited before subscribing")
                continue
            if "subscription acknowledged" in line:
                return process
        process.terminate()
        process.wait(timeout=5)
        pytest.fail("Ingest did not establish its subscription")

    try:
        process = start_ingest()
        # SIGTERM ends the process and its socket, just like systemctl stop.
        process.terminate()
        process.wait(timeout=5)
        process = None
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
        process = start_ingest()  # Fresh Paho object, same fixed client-id.
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
        if process is not None:
            process.terminate()
            process.wait(timeout=5)
