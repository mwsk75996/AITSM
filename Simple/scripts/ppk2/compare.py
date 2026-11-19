"""Draw two measure.py CSVs (10 ms windows) above each other on the same axes.

Usage: compare.py <out.png> <csv> <label> <sel_start_s> <sel_end_s> <csv> <label> <sel_start_s> <sel_end_s>

Each panel shows the whole measurement and the average, peak, time and charge
of its selected steady-state window, so a before and after can be compared.
"""
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

DT = 0.01
LINE = "#2a6fdb"
SELECT = "#f2a516"
INK = "#1f2933"
MUTED = "#616e7c"
GRID = "#e4e7eb"
BUCKET = 50  # 0.5 s per plotted point


def load(path):
    mean, peak = [], []
    with open(path) as f:
        for line in f:
            if line[0].isdigit():
                _, a, b = line.split(",")
                mean.append(float(a))
                peak.append(float(b))
    return mean, peak


def danish(value, decimals):
    return f"{value:.{decimals}f}".replace(".", ",")


out = sys.argv[1]
panels = [sys.argv[2:6], sys.argv[6:10]]

fig = plt.figure(figsize=(11, 8.6), dpi=150)
fig.patch.set_facecolor("white")
fig.text(0.07, 0.955, "Strømforbrug før og efter PSM (#23), batch hvert 5. minut",
         fontsize=14, fontweight="bold", color=INK)
results = []
for k, (path, label, a, b) in enumerate(panels):
    mean, peak = load(path)
    i0, i1 = int(float(a) / DT), int(float(b) / DT)
    s_mean = sum(mean[i0:i1]) / (i1 - i0)
    s_max = max(peak[i0:i1])
    s_time = (i1 - i0) * DT
    results.append(s_mean)

    top = 0.875 - k * 0.44
    fig.text(0.07, top, label, fontsize=11.5, fontweight="bold", color=INK)
    stats = [("Gennemsnit", f"{danish(s_mean / 1000, 2)} mA"),
             ("Maks", f"{danish(s_max / 1000, 0)} mA"),
             ("Tid", f"{danish(s_time / 60, 0)} min"),
             ("Batteritid, 1350 mAh", f"{danish(1350 / (s_mean / 1000) / 24, 1)} døgn")]
    for j, (head, value) in enumerate(stats):
        x0 = 0.07 + j * 0.23
        fig.text(x0, top - 0.035, head, fontsize=9, color=MUTED)
        fig.text(x0, top - 0.075, value, fontsize=15, fontweight="bold", color=INK)

    ax = fig.add_axes([0.07, top - 0.37, 0.91, 0.26])
    t, m = [], []
    for i in range(0, len(mean), BUCKET):
        t.append(i * DT / 60)
        m.append(max(sum(mean[i:i + BUCKET]) / len(mean[i:i + BUCKET]), 1) / 1000)
    ax.plot(t, m, color=LINE, linewidth=0.9, drawstyle="steps-mid")
    ax.axvspan(i0 * DT / 60, i1 * DT / 60, color=SELECT, alpha=0.12, linewidth=0)
    ax.set_yscale("log")
    ax.set_ylim(0.3, 500)
    ax.set_xlim(0, 30)
    ax.set_ylabel("Strøm (mA)", color=MUTED)
    ax.grid(color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=MUTED)
    if k == 1:
        ax.set_xlabel("Minutter efter tænding", color=MUTED)

fig.text(0.98, 0.006, "Linje: gennemsnit pr. 0,5 s, log-skala · markeret: stabil drift (PPK2, 100 kHz)",
         fontsize=8, color=MUTED, ha="right")
fig.savefig(out)
print(f"{out}: {results[0] / 1000:.3f} mA -> {results[1] / 1000:.3f} mA, faktor {results[0] / results[1]:.1f}")
