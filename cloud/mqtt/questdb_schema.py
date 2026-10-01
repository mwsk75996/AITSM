#!/usr/bin/env python3
"""Apply and verify the telemetry table's idempotent deduplication migration."""
import argparse
import json
from pathlib import Path
import time
from urllib.parse import urlencode
from urllib.request import urlopen

DEFAULT_URL = "http://127.0.0.1:9000"
MIGRATION = Path(__file__).with_name("migrations") / "001_enable_dedup.sql"


def query(sql, url=DEFAULT_URL):
    with urlopen(url.rstrip("/") + "/exec?" + urlencode({"query": sql}), timeout=20) as response:
        result = json.load(response)
    if "error" in result:
        raise RuntimeError("QuestDB afviste SQL: " + result["error"])
    return [dict(zip((column["name"] for column in result["columns"]), row))
            for row in result.get("dataset", [])] if "columns" in result else []


def enable_dedup(url=DEFAULT_URL):
    tables = query("SELECT walEnabled, designatedTimestamp, dedup FROM tables() "
                   "WHERE table_name = 'sensor_readings'", url)
    if len(tables) != 1 or not tables[0]["walEnabled"] or tables[0]["designatedTimestamp"] != "timestamp":
        raise RuntimeError("sensor_readings skal findes som WAL-tabel med designated timestamp 'timestamp'")
    columns = query("SELECT * FROM table_columns('sensor_readings')", url)
    if not {"timestamp", "device_id", "temperature", "battery"}.issubset(c["column"] for c in columns):
        raise RuntimeError("sensor_readings mangler de forventede kolonner")
    duplicates = query("SELECT count() AS duplicates FROM (SELECT timestamp, device_id, count() AS n "
                       "FROM sensor_readings GROUP BY timestamp, device_id) WHERE n > 1", url)[0]["duplicates"]
    # Enabling DEDUP does not remove historical duplicates. Do not silently
    # rewrite existing readings; a separate data repair is needed if any exist.
    if duplicates:
        raise RuntimeError(f"{duplicates} eksisterende dubletnøgler kræver separat datagennemgang")
    query(MIGRATION.read_text(), url)
    for _ in range(50):
        after = query("SELECT dedup FROM tables() WHERE table_name = 'sensor_readings'", url)
        keys = {c["column"] for c in query("SELECT * FROM table_columns('sensor_readings')", url)
                if c["upsertKey"]}
        if after[0]["dedup"] and keys == {"timestamp", "device_id"}:
            print("sensor_readings: WAL, DEDUP og UPSERT KEYS(timestamp, device_id) verificeret")
            return
        time.sleep(0.1)
    raise RuntimeError("QuestDB bekræftede ikke de forventede deduplikationsnøgler")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=DEFAULT_URL)
    enable_dedup(parser.parse_args().url)
