"""Run only against a fresh disposable QuestDB via QUESTDB_INTEGRATION_URL."""
import os
from pathlib import Path
from types import SimpleNamespace
import json
import time
import pytest
import ingest
import questdb_schema as schema
import verify_dedup


def test_real_ingest_replays_and_key_identity(monkeypatch):
    url = os.getenv("QUESTDB_INTEGRATION_URL")
    if not url:
        pytest.skip("Requires disposable QuestDB instance")
    if schema.query("SELECT table_name FROM tables() WHERE table_name='sensor_readings'", url):
        pytest.fail("Disposable integration database must not already contain sensor_readings")
    schema.query("CREATE TABLE sensor_readings (timestamp TIMESTAMP, device_id SYMBOL, "
                 "temperature DOUBLE, battery DOUBLE) TIMESTAMP(timestamp) PARTITION BY DAY WAL", url)
    schema.enable_dedup(url)
    schema.enable_dedup(url)  # Safe to run again.
    monkeypatch.setattr(ingest, "QUESTDB_WRITE_URL", url + "/write")
    rows = [{"timestamp": 1790143200, "temperature": 23.45, "battery": 98.76},
            {"timestamp": 1790143215, "temperature": -1.25, "battery": 98.0}]
    def send(device, readings):
        message = SimpleNamespace(topic="aitsm/"+device+"/telemetry", payload=json.dumps(
            {"device_id": device, "readings": readings}).encode())
        ingest.on_message(None, None, message)
    send("thingy91x", rows)
    send("thingy91x", rows)
    send("other-device", [rows[0]])
    changed = dict(rows[0], temperature=24.5)
    send("thingy91x", [changed])
    for _ in range(100):
        result = schema.query("SELECT device_id, temperature FROM sensor_readings ORDER BY timestamp, device_id", url)
        if result == [{"device_id": "other-device", "temperature": 23.45},
                      {"device_id": "thingy91x", "temperature": 24.5},
                      {"device_id": "thingy91x", "temperature": -1.25}]:
            break
        time.sleep(0.1)
    else:
        pytest.fail("Replay duplicates, different devices or last-write-wins semantics were incorrect")
    verify_dedup.verify(Path(ingest.__file__), url)
