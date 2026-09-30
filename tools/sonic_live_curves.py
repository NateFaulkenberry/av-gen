#!/usr/bin/env python3
"""The live filter-sweep and distortion tests' curves (ADR-1025; 01-brief-live.md PARTS 17-18).

    python3 tools/sonic_live_curves.py <probe-events.csv> <app-log.csv> <out-prefix> [--param cutoff|drive]

Aligns the app's --sonic-live-log to the probe's clock (seconds after the probe's start), prints a table at 1 s
steps -- the synth parameter, the MIDI-derived notes.* (which should hold) and the sound-derived sonic.*/timbre.*/
visual.* (which should move) -- and writes <out-prefix>.csv and, if matplotlib is present, <out-prefix>.png.
"""
import csv
import sys

SONIC = ["sonic.brightness", "sonic.warmth", "sonic.roughness", "sonic.sharpness", "sonic.smoothness",
         "sonic.energy", "sonic.inharmonicity", "sonic.complexity"]
NOTES = ["notes.active", "notes.pitch", "notes.velocity", "notes.polyphony"]
TIMBRE = ["timbre.rolloff", "timbre.dissonance", "timbre.flatness", "timbre.loudness"]


def load(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def main():
    probe, app, prefix = load(sys.argv[1]), load(sys.argv[2]), sys.argv[3]
    param = sys.argv[5] if len(sys.argv) > 5 else "cutoff"
    start = int(next(r for r in probe if r["kind"] == "start")["hostNs"])
    events = sorted((int(r["hostNs"]), float(r[param])) for r in probe if r["kind"] not in ("recording",))
    visual = [c for c in app[0].keys() if c.startswith("visual.")]
    cols = ["t", param] + NOTES + SONIC + TIMBRE + visual
    rows = []
    for r in app:
        t = (int(r["frameNs"]) - start) / 1e9
        if t < 0:
            continue
        # The synth parameter in force at this frame.
        value = next((v for ns, v in reversed(events) if ns <= int(r["frameNs"])), events[0][1])
        rows.append([t, value] + [float(r[c]) for c in cols[2:]])
    with open(prefix + ".csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols)
        w.writerows(rows)
    show = ["t", param, "notes.active", "notes.pitch", "notes.velocity", "sonic.brightness", "sonic.warmth",
            "sonic.roughness", "sonic.sharpness", "timbre.rolloff"] + [v for v in ("visual.glow", "visual.organic",
            "visual.crystalline", "visual.chaotic", "visual.edge", "visual.grain") if v in visual]
    idx = [cols.index(c) for c in show]
    print("  ".join(f"{c.replace('visual.', 'v.').replace('sonic.', 's.').replace('notes.', 'n.').replace('timbre.', 'tb.'):>9s}" for c in show))
    for sec in range(0, int(rows[-1][0]) + 1):
        near = min(rows, key=lambda r: abs(r[0] - sec))
        print("  ".join(f"{near[i]:9.3g}" for i in idx))
    plot(prefix + ".png", rows, cols, param, visual)


def plot(path, rows, cols, param, visual):
    """Four stacked panels drawn with PIL (matplotlib is not installed here): the synth parameter, notes.*,
    sonic.*, visual.*."""
    import math
    from PIL import Image, ImageDraw
    W, H, L, R = 1200, 1000, 120, 220
    img = Image.new("RGB", (W, H), (250, 250, 248))
    d = ImageDraw.Draw(img)
    palette = [(31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75),
               (227, 119, 194), (127, 127, 127)]
    t = [r[0] for r in rows]
    t0, t1 = min(t), max(t)
    panels = [
        (f"synth {param}", [(param, None)], param == "cutoff"),
        ("MIDI: notes.*", [(c, None) for c in ("notes.active", "notes.pitch", "notes.velocity")], False),
        ("sound: sonic.*", [(c, None) for c in SONIC[:6]], False),
        ("visual.*", [(c, None) for c in ("visual.glow", "visual.organic", "visual.crystalline", "visual.chaotic",
                                          "visual.edge", "visual.grain", "visual.radiance") if c in visual], False),
    ]
    ph = (H - 60) // len(panels)
    for p, (title, series, logy) in enumerate(panels):
        top = 20 + p * ph
        bottom = top + ph - 30
        d.rectangle([L, top, W - R, bottom], outline=(120, 120, 120))
        d.text((10, top + 4), title, fill=(0, 0, 0))
        values = [r[cols.index(c)] for c, _ in series for r in rows]
        lo, hi = (min(values), max(values)) if values else (0, 1)
        if not logy:
            lo, hi = min(lo, 0.0), max(hi, 1.0)
        if logy:
            lo, hi = math.log10(max(lo, 1e-3)), math.log10(max(hi, 1e-3))
        d.text((L - 60, top), f"{(10 ** hi if logy else hi):.3g}", fill=(80, 80, 80))
        d.text((L - 60, bottom - 12), f"{(10 ** lo if logy else lo):.3g}", fill=(80, 80, 80))
        for k, (c, _) in enumerate(series):
            colour = palette[k % len(palette)]
            pts = []
            for r in rows:
                v = r[cols.index(c)]
                if logy:
                    v = math.log10(max(v, 1e-3))
                x = L + (r[0] - t0) / max(t1 - t0, 1e-9) * (W - R - L)
                y = bottom - (v - lo) / max(hi - lo, 1e-9) * (bottom - top)
                pts.append((x, y))
            d.line(pts, fill=colour, width=2)
            d.text((W - R + 10, top + 4 + 14 * k), c, fill=colour)
    for sec in range(int(t0), int(t1) + 1, 2):
        x = L + (sec - t0) / max(t1 - t0, 1e-9) * (W - R - L)
        d.text((x - 4, H - 30), str(sec), fill=(80, 80, 80))
    d.text((W // 2 - 60, H - 16), "seconds (probe clock)", fill=(0, 0, 0))
    img.save(path)
    print("wrote", path)

if __name__ == "__main__":
    main()
