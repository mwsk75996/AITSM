#!/usr/bin/env python3
"""Repeat testcase 4 using existing device data; always restart stopped ingest."""
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import questdb_schema as schema

SERVICE = "projekt-c-ingest"


def seconds(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def broker_limits():
    # Inspect only retention directives; never log credentials, ACLs or keys.
    allowed = {"persistence", "max_queued_messages", "max_queued_bytes", "persistent_client_expiration", "autosave_interval"}
    for path in [Path("/etc/mosquitto/mosquitto.conf"), *sorted(Path("/etc/mosquitto/conf.d").glob("*.conf"))]:
        for line in path.read_text().splitlines():
            parts = line.split()
            if parts and parts[0] in allowed:
                print(f"Mosquitto {parts[0]}={' '.join(parts[1:])}", flush=True)


def verify():
    subprocess.run(["systemctl", "is-active", "--quiet", SERVICE], check=True)
    broker_limits()
    latest = schema.query("SELECT timestamp FROM sensor_readings WHERE device_id='thingy91x' ORDER BY timestamp DESC LIMIT 1")
    if not latest or time.time() - seconds(latest[0]["timestamp"]) > 600:
        raise RuntimeError("Testen kræver en aktiv Thingy med nylig standardbatch")
    last = latest[0]["timestamp"]
    sql = ("SELECT timestamp, temperature, battery FROM sensor_readings WHERE device_id='thingy91x' "
           f"AND timestamp > '{last}' ORDER BY timestamp")
    stopped = time.time()
    print(f"Stopper ingest i 360 s; seneste måling {last}", flush=True)
    try:
        subprocess.run(["systemctl", "stop", SERVICE], check=True)
        for elapsed in range(0, 360, 30):
            time.sleep(30)
            print(f"Ingest stoppet: {elapsed + 30}/360 s", flush=True)
        if schema.query(sql):
            raise RuntimeError("Nye Thingy-rækker kom frem, mens ingest var stoppet")
    finally:
        subprocess.run(["systemctl", "start", SERVICE], check=True)
    resumed = time.time()
    for _ in range(120):
        rows = schema.query(sql)
        if len(rows) >= 20 and any(stopped <= seconds(r["timestamp"]) <= resumed for r in rows):
            timestamps = [seconds(last), *(seconds(row["timestamp"]) for row in rows)]
            if any(b - a != 15 for a, b in zip(timestamps, timestamps[1:])):
                raise RuntimeError("Huller eller dubletter i standardprofilens 15 s-målinger")
            if any(row["temperature"] is None or row["battery"] is None for row in rows):
                raise RuntimeError("Genleverede målinger mangler sensorværdier")
            subprocess.run(["systemctl", "is-active", "--quiet", SERVICE], check=True)
            print(f"BESTÅET: {len(rows)} målinger efter ingest-genstart, 15 s mellem alle rækker", flush=True)
            print("GENLEVERET_JSON=" + json.dumps(rows, separators=(",", ":")), flush=True)
            return
        time.sleep(1)
    raise RuntimeError("Ventende batch med målinger under udfaldet nåede ikke QuestDB")


if __name__ == "__main__":
    verify()
