#!/usr/bin/env python3
"""Atomically update only the installed ingest script, with rollback on failure."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time
import os

# Ingest runs as the "ingest" service in cloud/docker/compose.yml, which
# bind-mounts the directory of TARGET read-only into the container.
SERVICE = "ingest"
COMPOSE = ["/usr/bin/docker", "compose", "-f", "/opt/aitsm/docker/compose.yml"]
TARGET = Path("/var/www/html/projekt-c/cloud/mqtt/ingest.py")
# Fails on Paho before 2.0, which cannot postpone PUBACK until QuestDB has the rows.
PAHO_CHECK = ("import paho.mqtt, paho.mqtt.client as mqtt; "
              "from paho.mqtt.packettypes import PacketTypes; "
              "from paho.mqtt.properties import Properties; "
              "p=Properties(PacketTypes.CONNECT); p.SessionExpiryInterval=86400; p.ReceiveMaximum=10; "
              "mqtt.Client(protocol=mqtt.MQTTv5, manual_ack=True); "
              "print('paho-mqtt', paho.mqtt.__version__, 'understøtter manuel ACK')")


def paho_check():
    # Runs in a fresh container from the service image, without dependencies.
    return [*COMPOSE, "run", "--rm", "--no-deps", "-T", SERVICE, "python", "-c", PAHO_CHECK]


def is_running():
    return [*COMPOSE, "exec", "-T", SERVICE, "true"]


def restart():
    return [*COMPOSE, "restart", SERVICE]


def replace_script(target, content, stat):
    with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".ingest-", delete=False) as f:
        temporary = Path(f.name)
        try:
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
            os.chmod(temporary, stat.st_mode & 0o777)
            os.chown(temporary, stat.st_uid, stat.st_gid)
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)


def wait_for_subscription(since):
    for _ in range(30):
        log = subprocess.check_output([*COMPOSE, "logs", "--no-log-prefix", "--since", str(since), SERVICE], text=True)
        active = subprocess.run(is_running(), capture_output=True).returncode == 0
        if active and "MQTT subscription acknowledged" in log:
            return
        time.sleep(1)
    raise RuntimeError("Ingest etablerede ikke sit abonnement efter deploy")


def deploy(source=None, target=TARGET):
    # Check the image's Paho installation; no environment files or
    # credentials need to be read or replaced.
    subprocess.run(paho_check(), check=True)
    content = (source or Path(__file__).with_name("ingest.py")).read_bytes()
    compile(content, str(target), "exec")
    previous = target.read_bytes()
    if content == previous:
        subprocess.run(is_running(), check=True)
        print("Installeret ingest.py er allerede identisk", flush=True)
        return
    stat = target.stat()
    since = int(time.time())
    try:
        replace_script(target, content, stat)
        subprocess.run(restart(), check=True)
        wait_for_subscription(since)
    except BaseException:
        replace_script(target, previous, stat)
        subprocess.run(restart(), check=True)
        raise
    print("Ingest opdateret og QoS 1-abonnement etableret; miljø og øvrige services uændrede", flush=True)


def inspect():
    print(f"Ingest-script: {TARGET}; parent writable={os.access(TARGET.parent, os.W_OK)}", flush=True)
    subprocess.run(["sudo", "-n", "-l"], check=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-outage", action="store_true")
    parser.add_argument("--inspect", action="store_true", help="Read deployment permissions without changing files or services")
    parser.add_argument("--source", type=Path, help="Reviewed ingest script staged by the deploy workflow")
    args = parser.parse_args()
    if args.inspect:
        inspect()
        raise SystemExit(0)
    deploy(args.source)
    if args.verify_outage:
        from verify_ingest_outage import verify
        verify()
