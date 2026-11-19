"""Capture with the PPK2 in ampere-meter mode and store 100 us mean and max.

Usage: capture.py <port> <seconds> <out.bin>

The output is two float32 arrays (mean, then max, in uA) for plot.py. Turning
on the PPK2 output restarts the Thingy, so capture at least 6 minutes to get a
whole batch after the start-up.
"""
import sys, time
from array import array
from ppk2_api.ppk2_api import PPK2_MP
port, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
ppk = PPK2_MP(port, buffer_max_size_seconds=5, buffer_chunk_seconds=0.01, timeout=1, write_timeout=1, exclusive=True)
ppk.get_modifiers(); ppk.use_ampere_meter(); ppk.set_source_voltage(3700)
ppk.toggle_DUT_power("ON"); ppk.start_measuring()
mean, mx, pending = array('f'), array('f'), []
t0 = time.time(); print(f"start_epoch={t0:.3f}", flush=True)
while time.time() - t0 < seconds:
    d = ppk.get_data()
    if not d: time.sleep(0.001); continue
    s, _ = ppk.get_samples(d)
    pending.extend(s)
    n = len(pending) // 10 * 10
    for i in range(0, n, 10):
        w = pending[i:i+10]; mean.append(sum(w)/10); mx.append(max(w))
    del pending[:n]
ppk.stop_measuring()
with open(out, 'wb') as f: mean.tofile(f); mx.tofile(f)
print(f"points={len(mean)}")
