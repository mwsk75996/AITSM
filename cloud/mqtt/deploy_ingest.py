#!/usr/bin/env python3
"""Atomically update only the installed ingest script, with rollback on failure."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time
import os

SERVICE = "projekt-c-ingest"


def service_command():
    command = subprocess.check_output(["systemctl", "show", SERVICE, "--property=ExecStart", "--value"], text=True)
    match = re.search(r"argv\[\]=(.*?) ;", command)
    argv = shlex.split(match.group(1)) if match else []
    target = next((Path(a).resolve() for a in argv if a.endswith("/ingest.py")), None)
    if not argv or target is None or not target.is_file():
        raise RuntimeError("Kunne ikke finde service-interpreter og ingest.py")
    return argv[0], target


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
        log = subprocess.check_output(["journalctl", "-u", SERVICE, "--since", f"@{since}", "-o", "cat"], text=True)
        active = subprocess.run(["systemctl", "is-active", "--quiet", SERVICE]).returncode == 0
        if active and "MQTT subscription acknowledged" in log:
            return
        time.sleep(1)
    raise RuntimeError("Ingest etablerede ikke sit abonnement efter deploy")


def deploy():
    interpreter, target = service_command()
    # Check the service's existing interpreter and Paho installation; no
    # environment files or credentials need to be read or replaced.
    subprocess.run([interpreter, "-c", "from paho.mqtt.packettypes import PacketTypes; "
                    "from paho.mqtt.properties import Properties; "
                    "p=Properties(PacketTypes.CONNECT); p.SessionExpiryInterval=86400"], check=True)
    content = Path(__file__).with_name("ingest.py").read_bytes()
    compile(content, str(target), "exec")
    previous = target.read_bytes()
    if content == previous:
        subprocess.run(["systemctl", "is-active", "--quiet", SERVICE], check=True)
        print("Installeret ingest.py er allerede identisk", flush=True)
        return
    stat = target.stat()
    since = int(time.time())
    try:
        replace_script(target, content, stat)
        subprocess.run(["systemctl", "restart", SERVICE], check=True)
        wait_for_subscription(since)
    except BaseException:
        replace_script(target, previous, stat)
        subprocess.run(["systemctl", "restart", SERVICE], check=True)
        raise
    print("Ingest opdateret og QoS 1-abonnement etableret; miljø og øvrige services uændrede", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-outage", action="store_true")
    args = parser.parse_args()
    deploy()
    if args.verify_outage:
        from verify_ingest_outage import verify
        verify()
