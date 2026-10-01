#!/usr/bin/env python3
"""Draw the director plan's colour script for All You Got as one image, the way a colour script is read.

The palette states (K0-K12), the times the slow colour voice moves between them, and each section's intent (camera
speed, how strongly the music may move the world) live in `tools/liminal/all-you-got.sections.json`, next to the
section map. This renders them against the song's timeline: four strips (the far air, the walls, the key light,
the accent) interpolated in OKLab the way the engine's palette block will interpolate them, the section bands and
bar numbers, the synch points, and the planned camera speed and coupling ranges beneath. It is a planning picture:
the implementation keys `palette/position` from the same timeline, and a render can be held against it.

    tools/liminal/colour_script.py tools/liminal/all-you-got.sections.json \\
        --out ~/Desktop/av-gen-review/24-liminal-space/plan/colour-script.png

Needs numpy and matplotlib.
"""
import argparse
import json
import os

import numpy as np

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
SERIES = ["#2a78d6", "#eb6834"]


def hex_to_rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], float) / 255.0


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def rgb_to_oklab(rgb):
    """Ottosson (2020), from sRGB in 0-1."""
    r, g, b = srgb_to_linear(np.asarray(rgb, float)).T
    l = np.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b)
    m = np.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b)
    s = np.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b)
    return np.stack([0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                     1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                     0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s], -1)


def oklab_to_rgb(lab):
    L, a, b = np.asarray(lab, float).T
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    rgb = np.stack([4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                    -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s], -1)
    return linear_to_srgb(rgb)


def palette_at(t, timeline, keys, role):
    """The slow colour voice at time t: hold the previous key, then smoothstep to the next over [start, full]."""
    lab_prev = rgb_to_oklab(hex_to_rgb(keys[timeline[0][2]][role]))
    for start, full, key in timeline:
        lab_key = rgb_to_oklab(hex_to_rgb(keys[key][role]))
        if t < start:
            return lab_prev
        if t < full:
            u = (t - start) / max(full - start, 1e-6)
            u = u * u * (3 - 2 * u)
            return lab_prev + (lab_key - lab_prev) * u
        lab_prev = lab_key
    return lab_prev


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sections")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    d = json.load(open(args.sections))
    keys, timeline, secs = d["palette_keys"], d["palette_timeline"], d["sections"]
    b0, b1 = 240 / d["grid"]["tempo"][0][1], 240 / d["grid"]["tempo"][1][1]
    bar76 = d["grid"]["tempo"][1][0]

    def bar_t(n):
        return (n - 1) * b0 if n <= bar76 else (bar76 - 1) * b0 + (n - bar76) * b1

    dur = bar_t(d["grid"]["bars"] + 1)
    for s in secs:
        s["t0"], s["t1"] = bar_t(s["bar0"]), bar_t(s["bar1"] + 1)

    plt.rcParams.update({"figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "text.color": INK,
                         "axes.edgecolor": INK2, "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2,
                         "font.size": 9})
    fig = plt.figure(figsize=(17, 8.4))
    gs = fig.add_gridspec(3, 1, height_ratios=[3.2, 0.9, 0.9], hspace=0.12)
    ax = fig.add_subplot(gs[0])
    ts = np.linspace(0, dur, 1400)
    roles = [("air", "air (far fog, void, sky)"), ("walls", "walls (plaster)"), ("light", "key light"),
             ("accent", "accent (beacon, planes, glimpse)")]
    img = np.zeros((len(roles), len(ts), 3))
    for i, (role, _) in enumerate(roles):
        labs = np.array([palette_at(t, timeline, keys, role) for t in ts])
        img[i] = oklab_to_rgb(labs)
    ax.imshow(img, aspect="auto", extent=(0, dur, len(roles) - 0.5, -0.5), interpolation="nearest")
    ax.set_yticks(range(len(roles)))
    ax.set_yticklabels([lab for _, lab in roles])
    ax.set_xlim(0, dur)
    ax.tick_params(axis="x", labelbottom=False)
    for j, s in enumerate(secs):
        ax.axvline(s["t0"], color=SURFACE, lw=1.2)
        ax.text((s["t0"] + s["t1"]) / 2, -0.62 - 0.36 * (j % 2), f'{s["id"]}  {s["bar0"]}-{s["bar1"]}', ha="center",
                va="bottom", fontsize=8, fontweight="bold", color=INK)
    for start, full, key in timeline:
        ax.text(full, len(roles) - 0.42, key, ha="left", va="top", fontsize=7.5, color=INK2)
    for t, label in d.get("synch_points", []):
        ax.plot([t, t], [len(roles) - 0.5, len(roles) - 0.3], color=INK, lw=1.0, clip_on=False)
    ax.set_title("All You Got: the colour script (the slow colour voice, K0-K12, interpolated in OKLab); "
                 "sections with their bars on top, synch points as ticks below", loc="left", fontsize=11,
                 fontweight="bold", pad=62)
    for spine in ax.spines.values():
        spine.set_visible(False)

    for k, (field, label, ymax, colour) in enumerate([("speed", "camera speed (m/s), planned range", 2.4, SERIES[0]),
                                                     ("coupling", "coupling: how far the music may move the world", 1.0,
                                                      SERIES[1])]):
        axk = fig.add_subplot(gs[1 + k], sharex=ax)
        for s in secs:
            it = s["intent"]
            if field == "speed":
                lo, hi = it["speed"]
            else:
                lo = hi = it["coupling"]
            axk.fill_between([s["t0"], s["t1"]], [lo, lo], [max(hi, lo + ymax * 0.02)] * 2, color=colour, alpha=0.85,
                             lw=0, step=None)
        axk.set_ylim(0, ymax)
        axk.set_ylabel(label, fontsize=8, rotation=0, ha="right", va="center")
        axk.grid(True, color=GRID, lw=0.6)
        axk.set_axisbelow(True)
        for s in secs:
            axk.axvline(s["t0"], color=INK2, lw=0.4, alpha=0.5)
        for spine in ("top", "right"):
            axk.spines[spine].set_visible(False)
        if k == 0:
            axk.tick_params(axis="x", labelbottom=False)
        else:
            axk.set_xlabel("time (s)")
    fig.subplots_adjust(left=0.2, right=0.965, top=0.8, bottom=0.08)
    os.makedirs(os.path.dirname(os.path.expanduser(args.out)), exist_ok=True)
    fig.savefig(os.path.expanduser(args.out), dpi=130)
    print("->", args.out)


if __name__ == "__main__":
    main()
