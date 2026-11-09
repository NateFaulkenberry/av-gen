"""Pass 3 regression guard: per-0.5 s mean luma and near-black share of the film, from either one full render or the
per-leg clips, compared against an earlier render. Flags any half-second whose luma fell by more than half, or that
went near-black (over 90% of pixels below 3/255) when it was not before.

  python3 luma_track.py <clips_dir | full.mp4> <before: clips_dir | full.mp4> [--out report.txt]"""
import argparse, os, subprocess, sys
import numpy as np

LEGS = [("canyon", 0.0), ("moon", 32.025), ("dusk", 47.87), ("mars", 69.655), ("night", 93.46), ("ocean", 121.217),
        ("dawn", 147.017)]


def frames(path, start):
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-vf", "fps=30,scale=96:54,format=gray", "-f",
                          "rawvideo", "-"], capture_output=True, check=True).stdout
    a = np.frombuffer(raw, np.uint8).reshape(-1, 54 * 96)
    return [(start + i / 30.0, a[i]) for i in range(len(a))]


def series(src):
    out = []
    if os.path.isdir(src):
        for leg, t0 in LEGS:
            p = os.path.join(src, f"leg-{leg}.mp4")
            if os.path.exists(p):
                out += frames(p, t0)
    else:
        out = frames(src, 0.0)
    bins = {}
    for t, f in out:
        bins.setdefault(int(t * 2), []).append(f)
    return {k / 2.0: (float(np.mean(v)), float(np.mean([(x < 3).mean() for x in v]))) for k, v in bins.items()}


ap = argparse.ArgumentParser()
ap.add_argument("now"); ap.add_argument("before"); ap.add_argument("--out")
a = ap.parse_args()
n, b = series(a.now), series(a.before)
lines = ["t      luma now/before   black now/before"]
flags = []
for t in sorted(set(n) & set(b)):
    (ln, kn), (lb, kb) = n[t], b[t]
    flag = (lb > 8 and ln < 0.5 * lb) or (kn > 0.9 and kb < 0.5)
    lines.append(f"{t:6.1f} {ln:6.1f} / {lb:6.1f}   {kn:4.2f} / {kb:4.2f}" + ("   <-- REGRESSION?" if flag else ""))
    if flag:
        flags.append(t)
summary = f"{len(flags)} flagged half-seconds: " + ", ".join(f"{t:.1f}" for t in flags)
text = summary + "\n" + "\n".join(lines)
if a.out:
    open(a.out, "w").write(text)
print(summary)
