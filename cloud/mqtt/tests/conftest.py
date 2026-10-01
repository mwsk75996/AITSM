import os
import sys
from pathlib import Path
from queue import Empty, Queue
import subprocess
from threading import Thread
import time

import pytest

# ingest.py reads its MQTT credentials when it is imported.
os.environ.setdefault("MQTT_INGEST_USER", "test-user")
os.environ.setdefault("MQTT_INGEST_PASSWORD", "test-password")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import ingest  # noqa: E402
import questdb_schema as schema  # noqa: E402


@pytest.fixture
def disposable_services():
    """Fresh sensor_readings in a disposable QuestDB next to a test broker."""
    url = os.getenv("QUESTDB_INTEGRATION_URL")
    host = os.getenv("MQTT_INTEGRATION_HOST")
    if not url or not host:
        pytest.skip("Requires disposable MQTT and QuestDB instances")
    if schema.query("SELECT table_name FROM tables() WHERE table_name='sensor_readings'", url):
        pytest.fail("Disposable test database must not already contain sensor_readings")
    schema.query("CREATE TABLE sensor_readings (timestamp TIMESTAMP, device_id SYMBOL, "
                 "temperature DOUBLE, battery DOUBLE) TIMESTAMP(timestamp) PARTITION BY DAY WAL", url)
    schema.enable_dedup(url)
    try:
        yield url, host, int(os.getenv("MQTT_INTEGRATION_PORT", "1883"))
    finally:
        # Only the table this fixture created, so the next test starts empty.
        schema.query("DROP TABLE sensor_readings", url)


class IngestProcess:
    """The real ingest.py as a separate process, as systemd runs it."""

    def __init__(self, env):
        self.process = subprocess.Popen([sys.executable, str(Path(ingest.__file__))], env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.lines = Queue()
        self.log = []
        Thread(target=self._read_log, daemon=True).start()

    def _read_log(self):
        for line in self.process.stdout:
            self.lines.put(line)

    def wait_for(self, text, count=1, timeout=10):
        deadline = time.monotonic() + timeout
        while self.count(text) < count:
            try:
                self.log.append(self.lines.get(timeout=max(0.01, min(0.2, deadline - time.monotonic()))))
            except Empty:
                if self.process.poll() is not None:
                    pytest.fail("Ingest exited:\n" + "".join(self.log))
                if time.monotonic() >= deadline:
                    pytest.fail(f"Ingest log never showed {count} x {text!r}:\n" + "".join(self.log))

    def count(self, text):
        while True:
            try:
                self.log.append(self.lines.get_nowait())
            except Empty:
                return sum(text in line for line in self.log)

    def stop(self):
        # SIGTERM ends the process and its socket, just like systemctl stop.
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=5)


@pytest.fixture
def start_ingest():
    processes = []

    def start(env):
        process = IngestProcess(env)
        processes.append(process)
        process.wait_for("subscription acknowledged")
        return process

    yield start
    for process in processes:
        process.stop()
