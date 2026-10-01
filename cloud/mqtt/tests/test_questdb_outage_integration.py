"""QuestDB write failures while the real broker and ingest process keep running (#88)."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from threading import Thread
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import paho.mqtt.client as mqtt
import pytest

import ingest
import questdb_schema as schema

TOPIC = "aitsm/thingy91x/telemetry"
FIRST_TIMESTAMP = 1790143200


class QuestDBSwitch:
    """HTTP front of the real QuestDB that can be taken down and restored."""

    def __init__(self, questdb_url):
        self.questdb_url = questdb_url
        self.status = None
        self.server = None
        self.port = 0
        self.up()
        self.url = f"http://127.0.0.1:{self.port}"

    def up(self):
        self.status = None
        if self.server is None:
            self._listen()

    def unavailable(self):
        """QuestDB answers, but refuses the write (HTTP 503)."""
        self.status = 503

    def down(self):
        """Nothing listens: connection refused, as when QuestDB is stopped."""
        if self.server is not None:
            self.server.shutdown()
            self.server.server_close()
            self.server = None

    def _listen(self):
        switch = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                if switch.status is not None:
                    self.send_response(switch.status)
                    self.end_headers()
                    return
                request = Request(switch.questdb_url + self.path, data=body, method="POST",
                                  headers={"Content-Type": self.headers["Content-Type"]})
                try:
                    with urlopen(request, timeout=10) as response:
                        status, reply = response.status, response.read()
                except HTTPError as error:
                    status, reply = error.code, error.read()
                self.send_response(status)
                self.send_header("Content-Length", str(len(reply)))
                self.end_headers()
                self.wfile.write(reply)

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", self.port), Handler)
        self.port = self.server.server_address[1]
        Thread(target=self.server.serve_forever, daemon=True).start()


@pytest.fixture
def services(disposable_services):
    url, host, port = disposable_services
    switch = QuestDBSwitch(url)
    publisher = mqtt.Client(client_id="questdb-outage-publisher", protocol=mqtt.MQTTv5)
    publisher.connect(host, port, keepalive=60)
    publisher.loop_start()
    env = dict(os.environ, MQTT_HOST=host, MQTT_PORT=str(port), QUESTDB_WRITE_URL=switch.url + "/write")
    try:
        yield url, switch, publisher, env
    finally:
        publisher.disconnect()
        publisher.loop_stop()
        switch.down()


def readings(first, count):
    return [{"timestamp": FIRST_TIMESTAMP + i * 15, "temperature": 23 + i / 100,
             "battery": 98 - i / 100} for i in range(first, first + count)]


def publish(publisher, payload):
    if not isinstance(payload, bytes):
        payload = json.dumps({"device_id": "thingy91x", "readings": payload})
    receipt = publisher.publish(TOPIC, payload, qos=1)
    receipt.wait_for_publish(timeout=5)
    assert receipt.is_published(), "Broker did not acknowledge the publish"


def stored(url):
    return schema.query("SELECT cast(timestamp AS LONG) AS ts, device_id, temperature, battery "
                        "FROM sensor_readings ORDER BY timestamp", url)


def expected(rows):
    return [{"ts": row["timestamp"] * 1_000_000, "device_id": "thingy91x",
             "temperature": row["temperature"], "battery": row["battery"]} for row in rows]


def wait_for_rows(url, rows, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        actual = stored(url)
        if actual == expected(rows):
            return
        time.sleep(0.2)
    pytest.fail(f"QuestDB has {len(actual)} rows, expected exactly {len(rows)} original readings")


def test_readings_survive_questdb_outage_while_ingest_runs(services, start_ingest):
    url, switch, publisher, env = services
    ingest_process = start_ingest(env)
    before = readings(0, 5)
    publish(publisher, b"ikke json")
    publish(publisher, before)
    wait_for_rows(url, before)  # The invalid payload did not block the queue.
    received_before = ingest_process.count("Received telemetry")

    switch.down()
    batches = [readings(5 + 5 * i, 5) for i in range(12)]
    # 13 messages; the last repeats a stored batch, as a redelivery would.
    for batch in batches + [before]:
        publish(publisher, batch)
    ingest_process.wait_for("QuestDB write failed", count=2)
    assert stored(url) == expected(before), "Rows were written while QuestDB was down"
    received_during = ingest_process.count("Received telemetry") - received_before
    assert received_during <= ingest.MQTT_RECEIVE_MAXIMUM, "Broker sent more than Receive Maximum"

    switch.up()
    wait_for_rows(url, before + [row for batch in batches for row in batch])
    assert ingest_process.process.poll() is None


def test_restart_while_waiting_redelivers_unacked_readings_only(services, start_ingest):
    url, switch, publisher, env = services
    first = start_ingest(env)
    publish(publisher, b'{"timestamp": 1}')  # Permanently invalid: acknowledged and dropped.
    first.wait_for("Rejected MQTT telemetry")

    switch.unavailable()
    rows = readings(0, 60)
    for batch in (rows[:20], rows[20:40], rows[40:]):
        publish(publisher, batch)
    first.wait_for("QuestDB write failed")
    first.stop()
    assert stored(url) == []

    switch.up()
    second = start_ingest(env)
    wait_for_rows(url, rows)
    assert second.count("Received telemetry") == 3, "Only the three unacked batches are redelivered"
    assert second.count("Rejected MQTT telemetry") == 0, "Acknowledged invalid payload came back"
