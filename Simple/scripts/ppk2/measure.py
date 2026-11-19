"""Measure current with a PPK2 in ampere-meter mode.

Usage: measure.py <port> <seconds> <out.csv> [--power-on]

Writes one row per 10 ms window (time_s, avg_uA, max_uA) and prints the
overall average, peak and sample count at the end.
"""
import sys
import time

from ppk2_api.ppk2_api import PPK2_MP

port, seconds, out_path = sys.argv[1], float(sys.argv[2]), sys.argv[3]
power_on = "--power-on" in sys.argv

ppk = PPK2_MP(port, buffer_max_size_seconds=5, buffer_chunk_seconds=0.01, timeout=1, write_timeout=1, exclusive=True)
ppk.get_modifiers()
ppk.use_ampere_meter()
# Ampere-meter mode first, so the regulator is never routed to VOUT. The library
# requires a reference voltage anyway; 3.7 V is the Li-Po nominal voltage.
ppk.set_source_voltage(3700)
if power_on:
    ppk.toggle_DUT_power("ON")
ppk.start_measuring()

WINDOW = 1000  # samples at 100 kHz = 10 ms
total = 0.0
count = 0
peak = 0.0
pending = []
t_start = time.time()
print(f"start_epoch={t_start:.3f}", flush=True)
with open(out_path, "w") as out:
    out.write("time_s,avg_uA,max_uA\n")
    window_index = 0
    while time.time() - t_start < seconds:
        data = ppk.get_data()
        if not data:
            time.sleep(0.001)
            continue
        samples, _ = ppk.get_samples(data)
        if not samples:
            continue
        total += sum(samples)
        count += len(samples)
        chunk_max = max(samples)
        if chunk_max > peak:
            peak = chunk_max
        pending.extend(samples)
        while len(pending) >= WINDOW:
            window = pending[:WINDOW]
            del pending[:WINDOW]
            out.write(f"{window_index * 0.01:.2f},{sum(window) / WINDOW:.1f},{max(window):.1f}\n")
            window_index += 1

ppk.stop_measuring()
elapsed = time.time() - t_start
print(f"samples={count} rate={count / elapsed:.0f}/s avg_uA={total / count:.1f} peak_uA={peak:.1f} windows={window_index}")
