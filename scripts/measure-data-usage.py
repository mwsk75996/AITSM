#!/usr/bin/env python3
"""Mål dataforbrug og leveringstid for en transmissionsprofil på en Thingy:91 X (#32).

Firmwaren skal være bygget med ``app/overlay-at-shell.conf`` (AT-kommandoer over
den serielle konsol). Scriptet genstarter enheden, aktiverer modemmets
``AT%XCONNSTAT`` så snart modemmet er klar (før TLS-handshaket), venter på
NBIRTH-kvittering og læser tællerne igen efter ``--minutes`` minutter.

Modemmets tællere tæller IP-data i hele kilobyte (inkl. TCP/IP, TLS og MQTT), så
vælg en måleperiode på mindst 20 minutter for at få en brugbar opløsning.

    scripts/measure-data-usage.py --profile batch5 --minutes 20

Resultatet skrives som én JSON-linje på stdout og logfilen gemmes ved siden af.
Kræver pyserial.
"""

import argparse
import json
import re
import statistics
import time

import serial

SAMPLE = re.compile(r"\[(\d+):(\d+):(\d+\.\d+),\d+\].*Måling indsamlet")
TIME = re.compile(r"\[(\d+):(\d+):(\d+\.\d+),\d+\]")
PENDING = re.compile(r"afventer ack for (\d+) måling\(er\), (\d+) bytes")
CONNSTAT = re.compile(r"%XCONNSTAT: (\d+),(\d+),(\d+),(\d+),(\d+),(\d+)")


def seconds(match):
    return int(match.group(1)) * 3600 + int(match.group(2)) * 60 + float(match.group(3))


class Console:
    def __init__(self, port, log_path):
        self.port = serial.Serial(port, 115200, timeout=0.2)
        self.log = open(log_path, "w", encoding="utf-8")
        self.lines = []
        self.partial = ""

    def pump(self, duration=0.0):
        end = time.time() + duration
        while True:
            chunk = self.port.read(4096).decode("utf-8", errors="replace")
            if chunk:
                chunk = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", chunk).replace("\r", "")
                self.partial += chunk
                *done, self.partial = self.partial.split("\n")
                for line in done:
                    self.lines.append(line)
                    self.log.write(line + "\n")
                self.log.flush()
            if time.time() >= end:
                return

    def wait_for(self, text, timeout):
        start = len(self.lines)
        end = time.time() + timeout
        while time.time() < end:
            self.pump(0.5)
            if any(text in line for line in self.lines[start:]):
                return True
        return False

    def send(self, command):
        self.port.write((command + "\r\n").encode())

    def connstat(self):
        start = len(self.lines)
        self.send("at AT%XCONNSTAT?")
        end = time.time() + 10
        while time.time() < end:
            self.pump(0.5)
            for line in self.lines[start:]:
                match = CONNSTAT.search(line)
                if match:
                    sms_tx, sms_rx, tx, rx, max_packet, _ = map(int, match.groups())
                    return {"tx_kb": tx, "rx_kb": rx, "max_packet": max_packet}
        raise RuntimeError("intet svar på AT%XCONNSTAT?")


def analyse(lines):
    samples, latencies, payload_bytes, pending = [], [], [], None
    retries = reconnects = failures = 0
    for line in lines:
        if SAMPLE.search(line):
            samples.append(seconds(TIME.search(line)))
        match = PENDING.search(line)
        if match:
            pending = (int(match.group(1)), int(match.group(2)))
        if "Målepayload bekræftet og fjernet" in line and pending:
            ack = seconds(TIME.search(line))
            count, size = pending
            for sample in samples[:count]:
                latencies.append(ack - sample)
            del samples[:count]
            payload_bytes.append(size)
            pending = None
        retries += "PUBACK mangler" in line
        reconnects += "forbindelse lukket" in line
        failures += "Kunne ikke sende" in line or "ikke bekræftet" in line
    return {
        "publishes": len(payload_bytes),
        "payload_bytes_total": sum(payload_bytes),
        "payload_bytes_mean": round(statistics.mean(payload_bytes), 1) if payload_bytes else 0,
        "latency_mean_s": round(statistics.mean(latencies), 1) if latencies else None,
        "latency_max_s": round(max(latencies), 1) if latencies else None,
        "retries": retries,
        "reconnects": reconnects,
        "failures": failures,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--profile", required=True)
    parser.add_argument("--minutes", type=float, required=True)
    args = parser.parse_args()

    console = Console(args.port, f"data-usage-{args.profile}.log")
    console.send("kernel reboot cold")
    if not console.wait_for("Connecting to LTE network", 90):
        raise SystemExit("enheden startede ikke LTE-forbindelsen")
    console.send("at AT%XCONNSTAT=1")
    if not console.wait_for("NBIRTH bekræftet", 240):
        raise SystemExit("NBIRTH blev ikke bekræftet")
    connect = console.connstat()
    mark = len(console.lines)
    started = time.time()
    console.pump(args.minutes * 60)
    final = console.connstat()
    hours = (time.time() - started) / 3600
    steady = {key: final[key] - connect[key] for key in ("tx_kb", "rx_kb")}
    result = {
        "profile": args.profile,
        "minutes": round(hours * 60, 1),
        "connect_kb": {"tx": connect["tx_kb"], "rx": connect["rx_kb"]},
        "steady_kb": steady,
        "steady_kb_per_hour": round((steady["tx_kb"] + steady["rx_kb"]) / hours, 1),
        **analyse(console.lines[mark:]),
    }
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
