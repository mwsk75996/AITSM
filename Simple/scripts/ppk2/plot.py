"""Draw PPK2 figures with a selection summary, in the style of a power profiler.

Usage: plot.py <fine.bin> <start_epoch> <out_dir>
       plot.py <measure.csv> <start_epoch> <out_dir> <sel_start_s> <sel_end_s>

fine.bin holds two float32 arrays of equal length (100 us mean, then 100 us
max, in uA) as written by capture.py. The script finds a batch and a
measurement wake-up in it and draws three figures. With a 10 ms CSV from
measure.py it draws one overview of the whole measurement, with the steady
state between sel_start_s and sel_end_s selected.

Options after the positional arguments, as key=value:
  prefix=foer-       prefix for the file names
  version=" (før)"   added to every title
  batch_at=280       search for the batch from this second (default 60)
  keepalive_at=120   also draw the radio event that starts after this second
                     as a keepalive figure
  wake_at=150        search for a measurement wake-up from this second
  psm=0              the idle figure says "uden PSM" instead of "modem i PSM"
"""
import sys
from array import array
from datetime import datetime, timezone
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

DT = 1e-4  # 100 us per point; 10 ms for a measure.py CSV
LINE = "#2a6fdb"
BAND = "#9dbdf0"
SELECT = "#f2a516"
INK = "#1f2933"
MUTED = "#616e7c"
GRID = "#e4e7eb"

opts = dict(a.split("=", 1) for a in sys.argv[1:] if "=" in a)
args = [a for a in sys.argv[1:] if "=" not in a]
path, start_epoch, out_dir = args[0], float(args[1]), Path(args[2])
PREFIX = opts.get("prefix", "")
VERSION = opts.get("version", "")
overview = path.endswith(".csv")
if overview:
    DT = 0.01
    mean, peak = array("f"), array("f")
    with open(path) as f:
        for line in f:
            if line[0].isdigit():
                _, a, b = line.split(",")
                mean.append(float(a))
                peak.append(float(b))
    n = len(mean)
else:
    raw = array("f")
    with open(path, "rb") as f:
        raw.frombytes(f.read())
    n = len(raw) // 2
    mean, peak = raw[:n], raw[n:]


def clock(seconds):
    return datetime.fromtimestamp(start_epoch + seconds, timezone.utc).strftime("%H:%M:%S")


def decimate(a, b, i0, i1, points=4000):
    """Mean and max per bucket so a long window still shows the peaks."""
    step = max(1, (i1 - i0) // points)
    t, m, x = [], [], []
    for i in range(i0, i1, step):
        j = min(i + step, i1)
        t.append(i * DT)
        m.append(sum(a[i:j]) / (j - i))
        x.append(max(b[i:j]))
    return t, m, x


def fmt_current(ua):
    return f"{ua / 1000:.2f} mA" if ua >= 1000 else f"{ua:.0f} µA"


def fmt_time(s):
    return f"{s:.1f} s" if s >= 1 else f"{s * 1000:.0f} ms"


def figure(name, title, i0, i1, sel=None, log=False, x_unit="s", band=True):
    sel = sel or (i0, i1)
    s_mean = sum(mean[sel[0]:sel[1]]) / (sel[1] - sel[0])
    s_max = max(peak[sel[0]:sel[1]])
    s_time = (sel[1] - sel[0]) * DT
    s_charge = s_mean * s_time / 1000  # uA * s / 1000 = mC

    t, m, x = decimate(mean, peak, i0, i1)
    scale = {"s": 1, "ms": 1000, "min": 1 / 60}[x_unit]
    t0 = i0 * DT
    tx = [(v - t0) * scale for v in t]
    ma = [v / 1000 for v in m]
    xa = [v / 1000 for v in x]

    fig = plt.figure(figsize=(11, 5.2), dpi=150)
    fig.patch.set_facecolor("white")
    ax = fig.add_axes([0.07, 0.13, 0.91, 0.62])
    if band:
        ax.fill_between(tx, ma, xa, color=BAND, alpha=0.55, linewidth=0, step="mid")
    ax.plot(tx, ma, color=LINE, linewidth=1.0, drawstyle="steps-mid")
    if sel != (i0, i1):
        ax.axvspan((sel[0] * DT - t0) * scale, (sel[1] * DT - t0) * scale,
                   color=SELECT, alpha=0.12, linewidth=0)
    if log:
        ax.set_yscale("log")
        ax.set_ylim(0.3, 400)
    else:
        ax.set_ylim(0, max(xa) * 1.1)
    ax.set_xlim(tx[0], tx[-1])
    ax.set_ylabel("Strøm (mA)", color=MUTED)
    ax.set_xlabel(f"Tid ({x_unit}) fra {clock(t0)} UTC", color=MUTED)
    ax.grid(color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=MUTED)

    fig.text(0.07, 0.93, title + VERSION, fontsize=14, fontweight="bold", color=INK)
    label = "Markering" if sel != (i0, i1) else "Hele vinduet"
    stats = [("Gennemsnit", fmt_current(s_mean)), ("Maks", fmt_current(s_max)),
             ("Tid", fmt_time(s_time)), ("Ladning", f"{s_charge:.1f} mC")]
    fig.text(0.07, 0.86, label.upper(), fontsize=8.5, color=MUTED)
    for k, (head, value) in enumerate(stats):
        x0 = 0.20 + k * 0.19
        fig.text(x0, 0.865, head, fontsize=9, color=MUTED)
        fig.text(x0, 0.81, value.replace(".", ","), fontsize=15, fontweight="bold", color=INK)
    res = "10 ms" if DT >= 0.01 else "100 µs"
    legend = f"Linje: gennemsnit pr. {res}" + (" · bånd: højeste sample" if band else "") + " (PPK2, 100 kHz)"
    fig.text(0.98, 0.02, legend,
             fontsize=8, color=MUTED, ha="right")
    fig.savefig(out_dir / (PREFIX + name))
    plt.close(fig)
    print(f"{name}: {label} {fmt_current(s_mean)} maks {fmt_current(s_max)} {fmt_time(s_time)} {s_charge:.1f} mC")


if overview:
    out_dir.mkdir(parents=True, exist_ok=True)
    a, b = int(float(args[3]) / DT), int(float(args[4]) / DT)
    figure("ppk2-30-minutter.png", "30 minutter, batch hvert 5. minut, stabil drift markeret",
           0, n, sel=(a, b), log=True, x_unit="min", band=False)
    sys.exit(0)

# Skip the first minute: powering the PPK2 output restarts the Thingy, and the
# start-up is not part of normal operation.
SKIP = int(60 / DT)

win = 100


def radio_event(start):
    """First run of 10 ms windows above 20 mA from start; returns (begin, end)."""
    begin = end = None
    for i in range(start, n - win, win):
        if sum(mean[i:i + win]) / win > 20_000:
            if begin is None:
                begin = i
            end = i
        elif begin is not None and (i - end) * DT > 3:
            break
    return begin, end


# Find the batch: the first radio event after the start-up (or after batch_at).
batch_start, batch_end = radio_event(int(float(opts.get("batch_at", 60)) / DT))

# A measurement wake-up: the first 10 ms window above 3 mA after the start-up.
wake = None
for i in range(int(float(opts.get("wake_at", 60)) / DT), batch_start - int(20 / DT), win):
    if sum(mean[i:i + win]) / win > 3000:
        wake = i
        break

out_dir.mkdir(parents=True, exist_ok=True)
b0, b1 = batch_start - int(2 / DT), batch_end + int(3 / DT)
figure("ppk2-batch.png", "Batch: netværk, TLS, MQTT-afsendelse og PUBACK",
       b0 - int(3 / DT), b1 + int(10 / DT), sel=(b0, b1))
# Start the selection where the 100 us mean first rises above 8 mA; the
# periodic 3 mA pulses in the idle current stay below that.
w0 = next(i for i in range(wake - int(0.05 / DT), n) if mean[i] > 8000) - int(0.005 / DT)
figure("ppk2-maaling.png", "Måling hvert 15. sekund: sensoraflæsning mellem batches",
       w0 - int(0.1 / DT), w0 + int(0.15 / DT), sel=(w0, w0 + int(0.03 / DT)), x_unit="ms")
if "keepalive_at" in opts:
    k_start, k_end = radio_event(int(float(opts["keepalive_at"]) / DT))
    k0, k1 = k_start - int(2 / DT), k_end + int(3 / DT)
    figure("ppk2-keepalive.png", "MQTT-keepalive: forbindelse til nettet for et PINGREQ",
           k0 - int(3 / DT), k1 + int(10 / DT), sel=(k0, k1))
idle = "modem i PSM" if opts.get("psm", "1") != "0" else "uden PSM"
figure("ppk2-hvile.png", f"Hvile mellem to målinger ({idle})",
       wake + int(1 / DT), wake + int(14 / DT))
