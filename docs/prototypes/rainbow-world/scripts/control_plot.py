#!/usr/bin/env python3
"""Rainbow World 2: the musical control layer over Feline Footwear, emulated from `avgen --sonic-trace` (ADR-1571).

Reads a sonic trace of a project with the three stems (see 14-stem-analysis.md) and applies the route chains of
`examples/rainbow-world/blender/music.py` -- gain, offset, clamp, then the dt-based attack/decay follower -- to plot
what each effect is driven by, against the arrangement. A check that the mapping reads the music, not a render.

  python3 docs/prototypes/rainbow-world/scripts/control_plot.py build/rw2/trace/feline-stems.csv out.png
"""
import csv
import math
import sys

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

rows = list(csv.DictReader(open(sys.argv[1])))
t = np.array([float(r["time"]) for r in rows])
col = lambda n: np.array([float(r[n]) for r in rows])  # noqa: E731


def chain(x, gain=1.0, offset=0.0, clamp=True, attack=0.0, decay=0.0):
    y = x * gain + offset
    if clamp:
        y = np.clip(y, 0.0, 1.0)
    out = np.zeros_like(y)
    s = 0.0
    for i in range(len(y)):
        dt = t[i] - t[i - 1] if i else 0.0
        ms = attack if y[i] > s else decay
        k = 1.0 if ms <= 0 else 1.0 - math.exp(-dt * 1000.0 / ms)
        s += (y[i] - s) * k
        out[i] = s
    return out


ring = chain(col("stem.drums.bass"), 3.0, 0.0, True, 4, 160) * 1.1 * (1 - 0.88 * col("beat.phase"))
front = chain(col("stem.drums.highMid"), 2.5, -1.55, True, 4, 280)
glint = chain(col("stem.drums.treble"), 1.6, -0.35, True, 3, 110)
swell = np.clip(chain(col("stem.bass.level"), 1.6, -0.55, True, 60, 500), 0, 1)
synth = np.clip(0.4 * chain(col("stem.synth.level"), 2.2, -0.95, True, 250, 1200)
                + 0.7 * chain(col("stem.synth.rms"), 4.5, 0.0, True, 300, 1200), 0, 1)
lift = chain(col("stem.synth.centroid"), 6.0, -3.75, True, 1500, 3000) * synth

fig, axs = plt.subplots(6, 1, figsize=(16, 11), sharex=True)
for ax, (y, name, c) in zip(axs, [(ring, "kick ring gain", "#E8603C"), (front, "clap fronts (gate)", "#C83C64"),
                                  (glint, "hat glints", "#C8A030"), (swell, "swell (bass level part)", "#3C78D8"),
                                  (synth, "synth light (level + rms)", "#A64CC8"), (lift, "lift (synth centroid)", "#444")]):
    ax.plot(t, y, lw=0.5, color=c)
    ax.set_ylabel(name, fontsize=8)
    ax.set_ylim(-0.05, 1.25)
    for m, lab in ((47.85, "drop"), (101.4, "fill"), (111.3, "16th hats"), (164.9, "pedal"), (182.7, "drums out"),
                   (194.6, "synth alone")):
        ax.axvline(m, color="#999", lw=0.6, ls="--")
        if ax is axs[0]:
            ax.text(m, 1.1, lab, fontsize=7)
axs[-1].set_xlabel("seconds")
fig.suptitle("Rainbow World 2: the controls the stems drive (emulated route chains on the engine's stem features)")
fig.tight_layout()
fig.savefig(sys.argv[2], dpi=60)
