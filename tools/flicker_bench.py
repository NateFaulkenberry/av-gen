#!/usr/bin/env python3
"""Attribute temporal instability to subsystems, reproducibly.

The Priority 1 inventory of the Level 3 mandate was measured once, by hand, from an ad-hoc capture
harness that is not in this repository -- and its numbers then had to be withdrawn, because that
harness never simulated particles. Neither failure was in the *detector*; `temporal_stats.py` was
right both times. They were failures of the thing that produced the frames, and the remedy for that
is a harness anybody can run again rather than a number in a document.

So this script owns the whole measurement: it renders every arm, proves each arm is not vacuous,
runs the detector, and prints the table. Three rules are built into it rather than left to whoever
runs it.

**The sequence is rendered by `--render`, not by a bespoke loop.** That is the path that produces
deliverables, so a subsystem that is inert in it is a real defect rather than a harness artifact --
which is exactly the distinction the withdrawn numbers could not make. It also means the fixed-step
clock, the single seek and the one-shot target sizing come from the shipped code.

**The tail is analysed, not the head.** A particle pool starts empty: at t = 0 there is nothing to
flicker, and the frames while it fills are a transient of the harness rather than of the renderer.
`--warmup` seconds are rendered and discarded.

**Every arm must be shown to change the frames before its result is reported** (ADR-182). An arm that
turns off a subsystem the camera cannot see produces a byte-identical sequence, and its flicker
number is then the baseline's wearing a different name -- the exact shape of the particles mistake.
A vacuous arm is reported as VACUOUS and its number is withheld.

Usage:
    tools/flicker_bench.py --scene examples/world/glowmere-stylized.scene.json \\
        --arms particles,water,volume,post --out /tmp/flicker

The GPU lock is taken around the whole run (ADR-170), so arms cannot interleave with another agent's
frames. Timings are not reported and must not be read from this: it renders, it does not benchmark.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from temporal_stats import analyse, sequence
from image_stats import read_png

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AVGEN = os.path.join(ROOT, "build", "release", "src", "avgen")


def sceneWithCamera(scene, spec, out):
    """A copy of the scene with its camera replaced, written beside the output.

    The view is half the experiment -- water's contribution is a function of how much of the frame
    it occupies, and the first inventory's numbers cannot be reproduced precisely because the camera
    that took them was never written down. So a view used for a measurement is a file, and the file
    is named in the result.

    `spec` is "px,py,pz:tx,ty,tz[:fov]". Mode 1 is the static camera: whatever the scene authored
    for orbit or track must not move under a measurement that assumes a still view.
    """
    parts = spec.split(":")
    if len(parts) < 2:
        raise SystemExit("--camera expects px,py,pz:tx,ty,tz[:fov]")
    pos = [float(v) for v in parts[0].split(",")]
    tgt = [float(v) for v in parts[1].split(",")]
    fov = float(parts[2]) if len(parts) > 2 else None
    with open(scene) as f:
        doc = json.load(f)
    cam = doc.setdefault("camera", {})
    cam["mode"] = 1
    cam["position"] = pos
    cam["target"] = tgt
    cam["orbitSpeed"] = 0.0
    if fov is not None:
        cam["fov"] = fov
    os.makedirs(out, exist_ok=True)
    # Beside the original, because a composition resolves its assets relative to its own directory.
    path = os.path.join(os.path.dirname(os.path.abspath(scene)),
                        "_flicker_view.scene.json")
    with open(path, "w") as f:
        json.dump(doc, f, indent=1)
    shutil.copy2(path, os.path.join(out, "view.scene.json"))
    return path


def patched(scene, patches, out, name):
    """A copy of the scene with `patches` applied: dotted paths into the document, each set to a
    JSON value. `post.bloomEnabled=false`, `terrain.water.ripple=0`.

    This exists because `--disable post` is not a per-stage arm. It removes the whole chain including
    the tone map, so the frame's transfer function moves and every threshold in the detector moves
    with it -- the result is a real difference that attributes nothing in particular. An arm that
    turns one authored term off and leaves the rest of the chain intact does not have that problem,
    and it is also the arm this project's own discipline asks for: change what the *scene* says
    before changing what the shader does.

    The node a dotted path lands in is searched for by name under `nodes` when the first segment is
    not a top-level key, so `valley.terrain.water.ripple` addresses one terrain's water.
    """
    with open(scene) as f:
        doc = json.load(f)
    for path, raw in patches:
        value = json.loads(raw)
        segments = path.split(".")
        target = doc
        if segments[0] not in doc:
            node = next((n for n in doc.get("nodes", []) if n.get("name") == segments[0]), None)
            if node is None:
                raise SystemExit(f"{path}: no top-level key or node named '{segments[0]}'")
            target = node
            segments = segments[1:]
        for seg in segments[:-1]:
            target = target.setdefault(seg, {})
        target[segments[-1]] = value
    path = os.path.join(os.path.dirname(os.path.abspath(scene)), f"_flicker_{name}.scene.json")
    with open(path, "w") as f:
        json.dump(doc, f, indent=1)
    os.makedirs(out, exist_ok=True)
    shutil.copy2(path, os.path.join(out, "arm.scene.json"))
    return path


def render(scene, out, width, height, seconds, arm, extra):
    """One arm's sequence. Returns the sequence hash the renderer reports, or None."""
    os.makedirs(out, exist_ok=True)
    cmd = [AVGEN, "--composition", scene, "--size", f"{width}x{height}",
           "--render", out, "--range", f"0:{seconds}"]
    if arm:
        cmd += ["--disable", arm]
    cmd += extra
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        return None
    for line in (proc.stdout + proc.stderr).splitlines():
        if "sequence hash" in line:
            return line.split("sequence hash", 1)[1].split(",")[0].strip()
    return None


def tail(directory, keep, warmup_frames):
    """Move the frames to analyse into a `tail` subdirectory, discarding the warm-up."""
    paths = sequence(directory)
    chosen = paths[warmup_frames:][-keep:] if len(paths) > warmup_frames else paths[-keep:]
    dest = os.path.join(directory, "tail")
    os.makedirs(dest, exist_ok=True)
    for p in chosen:
        shutil.copy2(p, os.path.join(dest, os.path.basename(p)))
    return dest


def frames_differ(a, b):
    """Whether two sequences differ anywhere. The non-vacuity test, on the frames that are analysed
    rather than on the whole render -- an arm that only moves the discarded warm-up has not been
    shown to affect the measurement."""
    pa, pb = sequence(a), sequence(b)
    if len(pa) != len(pb):
        return True, "different frame counts"
    worst = 0
    changed = 0
    for x, y in zip(pa, pb):
        w1, h1, c1, px1 = read_png(x)
        w2, h2, c2, px2 = read_png(y)
        if (w1, h1, c1) != (w2, h2, c2):
            return True, "different geometry"
        for i in range(0, len(px1), c1):
            d = max(abs(px1[i] - px2[i]), abs(px1[i + 1] - px2[i + 1]), abs(px1[i + 2] - px2[i + 2]))
            if d > 0:
                changed += 1
                worst = max(worst, d)
    return changed > 0, f"{changed} px differ, max delta {worst}"


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", required=True)
    ap.add_argument("--arms", default="particles,water,volume,post",
                    help="comma-separated --disable names; the baseline is always run")
    ap.add_argument("--out", required=True)
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--height", type=int, default=540)
    ap.add_argument("--seconds", type=float, default=3.0, help="rendered, including the warm-up")
    ap.add_argument("--warmup", type=float, default=2.0, help="seconds discarded before analysis")
    ap.add_argument("--keep", type=int, default=24, help="frames analysed, from the end")
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--threshold", type=float, default=6.0)
    ap.add_argument("--extra", default="", help="further avgen arguments, space separated")
    ap.add_argument("--scene-arm", action="append", default=[],
                    help="a scene-patch arm: name=path=json (repeatable), e.g. "
                         "bloom=post.bloomEnabled=false or ripple=valley.terrain.water.ripple=0")
    ap.add_argument("--camera", default="",
                    help="override the scene camera: px,py,pz:tx,ty,tz[:fov]. The view is written "
                         "beside the results as view.scene.json so the measurement is repeatable.")
    args = ap.parse_args(argv[1:])

    extra = args.extra.split() if args.extra else []
    scene = args.scene
    if args.camera:
        scene = sceneWithCamera(args.scene, args.camera, args.out)
    warmup_frames = int(args.warmup * args.fps)
    arms = [a.strip() for a in args.arms.split(",") if a.strip()]

    results = {}
    print(f"scene {scene} at {args.width}x{args.height}, "
          f"{args.seconds:g}s rendered, {args.warmup:g}s discarded, last {args.keep} analysed")

    base_dir = os.path.join(args.out, "baseline")
    base_hash = render(scene, base_dir, args.width, args.height, args.seconds, None, extra)
    if base_hash is None:
        print("baseline render failed")
        return 1
    base_tail = tail(base_dir, args.keep, warmup_frames)
    base_stats, err = analyse(base_tail, args.threshold)
    if err:
        print(err)
        return 1
    results["baseline"] = (base_stats, base_hash, True, "")

    jobs = [(a, scene, a) for a in arms]
    for spec in args.scene_arm:
        name, _, rest = spec.partition("=")
        path, _, raw = rest.partition("=")
        if not name or not path or not raw:
            raise SystemExit(f"--scene-arm expects name=path=json, got '{spec}'")
        jobs.append((name, patched(scene, [(path, raw)], os.path.join(args.out, name), name), None))

    for arm, armScene, disable in jobs:
        d = os.path.join(args.out, arm)
        h = render(armScene, d, args.width, args.height, args.seconds, disable, extra)
        if h is None:
            print(f"{arm}: render failed")
            continue
        t = tail(d, args.keep, warmup_frames)
        differs, why = frames_differ(base_tail, t)
        stats, err = analyse(t, args.threshold)
        if err:
            print(f"{arm}: {err}")
            continue
        results[arm] = (stats, h, differs, why)

    base_pct = results["baseline"][0]["flickering_percent"]
    print()
    print(f"{'arm':<14}{'flicker %':>11}{'share':>9}{'peak':>8}  non-vacuity")
    for name, (stats, h, differs, why) in results.items():
        pct = stats["flickering_percent"]
        if name == "baseline":
            print(f"{name:<14}{pct:>10.3f}%{'—':>9}{stats['peak']:>8.1f}  hash {h}")
            continue
        if not differs:
            print(f"{name:<14}{'withheld':>11}{'—':>9}{'—':>8}  VACUOUS — identical frames, {why}")
            continue
        share = 100.0 * (pct - base_pct) / base_pct if base_pct > 0 else 0.0
        print(f"{name:<14}{pct:>10.3f}%{share:>8.0f}%{stats['peak']:>8.1f}  {why}")

    # The arm scenes have to live beside the original -- a composition resolves its assets relative
    # to its own directory -- so they are removed rather than left in `examples/world` for someone to
    # find later and wonder about. A copy of each is already in the result directory.
    for stray in os.listdir(os.path.dirname(os.path.abspath(args.scene))):
        if stray.startswith("_flicker_") and stray.endswith(".scene.json"):
            os.remove(os.path.join(os.path.dirname(os.path.abspath(args.scene)), stray))

    print()
    print("share is the change in flickering area relative to the baseline; negative means the arm")
    print("removed flicker, which is the direction that attributes it. A VACUOUS arm attributes")
    print("nothing: its sequence is the baseline's, so its number would be too.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
