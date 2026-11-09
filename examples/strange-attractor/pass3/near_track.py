"""Pass 3 regression guard: how much of the frame is geometry within N metres of the camera, every half second.

The night pass of M3g crept past one boulder for 18 s with 2.46 m clearance: the clearance trace and the luma track
both passed it. This measures what was wrong -- a rock face filling half the frame -- from the renderer's own depth.

  python3 near_track.py <out.txt> [--range 93.5:121.2] [--near 20] [--flag 0.5] [--hold 4] [--project <project.json>]
Renders a depth AOV at 384x216, 2 fps (through tools/gpu-lock.sh), then for each frame the share of pixels whose depth
is under --near metres. A run of --hold seconds or more with that share over --flag is reported as a REGRESSION? line.
Calibrated on the M3g night (pass3-env/08-regression): the boulder sat at 11-17 m (median depth under 20 m for 12 s),
so the defaults flag "half the frame is one near surface for 4 s"; the camera's passes beside walls are shorter."""
import argparse, os, shutil, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import read_exr  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--range", default="0:211")
ap.add_argument("--near", type=float, default=20.0)
ap.add_argument("--flag", type=float, default=0.5)
ap.add_argument("--hold", type=float, default=4.0)
ap.add_argument("--fps", type=float, default=2.0)
ap.add_argument("--project", default=os.path.join(ROOT, "examples/strange-attractor/strange-attractor.json"))
a = ap.parse_args()
t0 = float(a.range.split(":")[0])
tmp = tempfile.mkdtemp(prefix="p3near-")
env = {**os.environ, "AVGEN_GPU_LOCK_TIMEOUT": os.environ.get("AVGEN_GPU_LOCK_TIMEOUT", "14400")}
r = subprocess.run([os.path.join(ROOT, "tools/gpu-lock.sh"), os.path.join(ROOT, "build/release/src/avgen"), "--headless",
                    "--project", a.project, "--render", tmp, "--format", "png", "--range", a.range, "--fps",
                    str(a.fps), "--size", "384x216", "--aov", "depth"], cwd=ROOT, env=env, capture_output=True, text=True)
files = sorted(f for f in os.listdir(tmp) if f.endswith(".depth.exr"))
if r.returncode != 0 or not files:
    print(r.stdout[-1500:], r.stderr[-1500:]); sys.exit(f"render failed ({r.returncode})")
rows = []
for i, f in enumerate(files):
    w, h, ch = read_exr.read_exr(os.path.join(tmp, f))
    d = np.array(ch.get("R", ch.get("Z", next(iter(ch.values()))))).reshape(h, w)
    valid = np.isfinite(d) & (d > 0)
    dv = np.where(valid, d, 1e6)
    rows.append((t0 + i / a.fps, float(((d < a.near) & valid).mean()), float(np.percentile(dv, 25)),
                 float(np.percentile(dv, 50))))
shutil.rmtree(tmp, ignore_errors=True)
flags, run = [], []
for t, share, _, _ in rows:
    if share > a.flag:
        run.append(t)
    else:
        if run and run[-1] - run[0] >= a.hold - 1e-6:
            flags.append((run[0], run[-1]))
        run = []
if run and run[-1] - run[0] >= a.hold - 1e-6:
    flags.append((run[0], run[-1]))
lines = [f"share of the frame within {a.near:g} m, every {1 / a.fps:g} s ({a.range}); flag over {a.flag:g} for {a.hold:g} s+"]
lines += [f"REGRESSION? {s:.1f}-{e:.1f} s: geometry within {a.near:g} m covers over {a.flag:.0%} of the frame"
          for s, e in flags]
lines += [f"{t:7.2f} {share:5.2f}  p25 {q25:8.1f} m  p50 {q50:8.1f} m" + ("  <--" if share > a.flag else "")
          for t, share, q25, q50 in rows]
open(a.out, "w").write("\n".join(lines) + "\n")
print(lines[0]); print("\n".join(l for l in lines if l.startswith("REGRESSION")) or "no flags")
