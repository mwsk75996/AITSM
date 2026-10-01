import os
import sys
from pathlib import Path

# ingest.py reads its MQTT credentials when it is imported.
os.environ.setdefault("MQTT_INGEST_USER", "test-user")
os.environ.setdefault("MQTT_INGEST_PASSWORD", "test-password")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
