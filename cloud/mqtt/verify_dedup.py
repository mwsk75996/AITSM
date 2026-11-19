#!/usr/bin/env python3
"""Replay existing readings through ingest without adding synthetic live data."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import time
from types import SimpleNamespace
import questdb_schema as schema


def verify(ingest_file, url):
    # Only the write path is exercised: these dummy credentials are never used to
    # connect to MQTT. Existing service credentials need not leave their file.
    os.environ["MQTT_INGEST_USER"] = "verification-unused"
    os.environ["MQTT_INGEST_PASSWORD"] = "verification-unused"
    os.environ["QUESTDB_WRITE_URL"] = url.rstrip("/") + "/write"
    spec = importlib.util.spec_from_file_location("live_ingest", ingest_file)
    ingest = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ingest)
    successful_writes = []
    write = ingest.write_questdb
    def verified_write(lines):
        write(lines)
        successful_writes.append(True)
    ingest.write_questdb = verified_write
    rows = schema.query("SELECT timestamp, device_id, temperature, battery "
                        "FROM sensor_readings ORDER BY timestamp DESC LIMIT 2", url)
    if len(rows) != 2:
        raise RuntimeError("Verifikation kræver mindst to eksisterende målinger")
    keys = " OR ".join("(timestamp = '" + row["timestamp"].replace("'", "''") +
                       "' AND device_id = '" + row["device_id"].replace("'", "''") + "')" for row in rows)
    sql = "SELECT timestamp, device_id, temperature, battery FROM sensor_readings WHERE " + keys + " ORDER BY timestamp, device_id"
    before = schema.query(sql, url)
    message = SimpleNamespace(topic="aitsm/verification/telemetry", payload=json.dumps({"readings": rows}).encode())
    # Ingest from before #88 wrote directly in on_message; newer versions
    # acknowledge from a writer thread and expose the write as store_message.
    store = getattr(ingest, "store_message", None) or (lambda m: ingest.on_message(None, None, m))
    store(message)
    store(message)
    if len(successful_writes) != 2:
        raise RuntimeError("Ingest gennemførte ikke begge HTTP-skrivninger")
    for _ in range(50):
        time.sleep(0.2)
        after = schema.query(sql, url)
        # Also wait for WAL application, rather than accepting an unchanged
        # view before the replays have been processed.
        pending = schema.query("SELECT wal_pending_row_count FROM tables() WHERE table_name = 'sensor_readings'", url)
        if pending[0]["wal_pending_row_count"] == 0:
            if after != before or len(after) != 2:
                raise RuntimeError("Gentagen ingest ændrede antal eller værdier for de eksisterende målinger")
            print("To eksisterende målinger gentaget to gange gennem ingest: stadig to rækker med samme værdier")
            return
    raise RuntimeError("QuestDB WAL nåede ikke at anvende de gentagne målinger")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=schema.DEFAULT_URL)
    parser.add_argument("--ingest-file", type=Path, default=Path(__file__).with_name("ingest.py"))
    args = parser.parse_args()
    verify(args.ingest_file, args.url)
