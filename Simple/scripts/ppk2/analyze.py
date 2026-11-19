"""Analyse a measure.py CSV (10 ms windows) of the Thingy running Simple.

Usage: analyze.py <csv> <start_epoch> [battery_mAh]
"""
import statistics
import sys
from datetime import datetime, timezone

path, start_epoch = sys.argv[1], float(sys.argv[2])
battery_mah = float(sys.argv[3]) if len(sys.argv) > 3 else 1350.0

t, avg, mx = [], [], []
with open(path) as f:
    next(f)
    for line in f:
        a, b, c = line.split(",")
        t.append(float(a))
        avg.append(float(b))
        mx.append(float(c))

DT = 0.01
ACTIVE_UA = 20_000  # radio activity: TX/RX bursts are tens to hundreds of mA
GAP_S = 3.0  # bursts closer than this belong to the same event


def clock(seconds):
    return datetime.fromtimestamp(start_epoch + seconds, timezone.utc).strftime("%H:%M:%S")


# Group 10 ms windows above ACTIVE_UA into events.
events = []
for i, a in enumerate(avg):
    if a < ACTIVE_UA:
        continue
    if events and t[i] - events[-1]["end"] <= GAP_S:
        events[-1]["end"] = t[i]
    else:
        events.append({"start": t[i], "end": t[i]})

for event in events:
    i0 = max(0, int((event["start"] - 1.0) / DT))
    i1 = min(len(avg), int((event["end"] + 1.0) / DT) + 1)
    event["charge_mC"] = sum(avg[i0:i1]) * DT / 1000.0
    event["peak_mA"] = max(mx[i0:i1]) / 1000.0
    event["duration_s"] = event["end"] - event["start"] + 0.01

total_s = len(avg) * DT
print(f"Varighed: {total_s:.0f} s fra {clock(0)} UTC")
print(f"Middelstrøm, hele målingen: {statistics.fmean(avg) / 1000:.3f} mA")
print(f"Højeste spids (10 µs-sample): {max(mx) / 1000:.1f} mA")
print()
print("Hændelser over 20 mA:")
for n, e in enumerate(events):
    print(f"  {n:2d} {clock(e['start'])} varighed {e['duration_s']:6.1f} s  ladning {e['charge_mC']:8.1f} mC  spids {e['peak_mA']:6.1f} mA")

# Steady state: from the start of the first batch after boot to the start of
# the last one, a whole number of batch periods.
# A batch (connect, TLS, MQTT publish and PUBACK) takes about 15 s and well
# over 100 mC; the short bursts about 20 s after it are counted separately.
BATCH_MIN_MC = 100.0
batches = [e for e in events[1:] if e["charge_mC"] >= BATCH_MIN_MC]
followups = [e for e in events[1:] if e["charge_mC"] < BATCH_MIN_MC]
if len(batches) >= 2:
    s0, s1 = batches[0]["start"] - 1.0, batches[-1]["start"] - 1.0
    i0, i1 = int(s0 / DT), int(s1 / DT)
    steady = avg[i0:i1]
    periods = len(batches) - 1
    steady_mA = statistics.fmean(steady) / 1000
    print()
    print(f"Stabil drift: {clock(s0)}-{clock(s1)} ({(s1 - s0) / 60:.1f} min, {periods} batchperioder)")
    print(f"  Middelstrøm: {steady_mA:.3f} mA")
    gaps = [b["start"] - a["start"] for a, b in zip(batches, batches[1:])]
    print(f"  Tid mellem batches: {', '.join(f'{g:.0f}' for g in gaps)} s")
    quiet = sorted(steady)
    print(f"  Hvilestrøm (median af 10 ms-vinduer): {statistics.median(quiet) / 1000:.3f} mA")
    print(f"  Laveste 1 %-percentil: {quiet[len(quiet) // 100] / 1000:.3f} mA")
    active_share = sum(1 for a in steady if a >= ACTIVE_UA) / len(steady)
    over_1mA = sum(1 for a in steady if a >= 1000) / len(steady)
    print(f"  Andel af tiden over 20 mA: {active_share * 100:.1f} %, over 1 mA: {over_1mA * 100:.1f} %")
    batch_charge = statistics.fmean(b["charge_mC"] for b in batches)
    batch_duration = statistics.fmean(b["duration_s"] for b in batches)
    print(f"  Gns. batch: {batch_duration:.1f} s over 20 mA, spids {max(b['peak_mA'] for b in batches):.0f} mA")
    in_steady = [e for e in followups if s0 <= e["start"] < s1]
    print(f"  Korte efterfølgende spidser: {len(in_steady)} stk., i alt {sum(e['charge_mC'] for e in in_steady):.0f} mC")
    period_mC = statistics.fmean(steady) / 1000 * (s1 - s0) / periods
    rest_mC = period_mC - batch_charge
    print(f"  Ladning pr. 5-min-periode: {period_mC:.0f} mC, heraf batch {batch_charge:.0f} mC ({batch_charge / period_mC * 100:.0f} %) og resten {rest_mC:.0f} mC")
    print(f"  Gns. ladning pr. batch: {batch_charge:.0f} mC")
    hours = battery_mah / steady_mA
    print(f"  Batteritid med {battery_mah:.0f} mAh: {hours:.0f} timer = {hours / 24:.1f} døgn")
if events:
    boot = events[0]
    print()
    print(f"Opstart (første hændelse): {boot['duration_s']:.1f} s, {boot['charge_mC']:.0f} mC, spids {boot['peak_mA']:.1f} mA")
