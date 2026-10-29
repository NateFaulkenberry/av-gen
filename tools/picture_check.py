#!/usr/bin/env python3
"""Picture comparison across commits, from a LOCAL store (ADR-1810).

    python3 tools/picture_check.py capture --label <name> [--bin build/release] [--scenes a,b] [--size 960x540]
                                           [--tier <t>] [--store <dir>]
    python3 tools/picture_check.py compare <labelA> <labelB> [--store <dir>] [--tol 2] [--heatmaps]
    python3 tools/picture_check.py list [--store <dir>]

The live perf gate (tools/live_perf_baseline.py) cannot see a picture change, and the in-run determinism hashes only
compare a build with itself. This captures fixed frames of the reference live scenes with a given build and diffs two
captures, so "did this change the picture?" has an answer across commits.

The store is LOCAL and outside the repository, always: the frames show commercial assets and the repository is public.
Default `~/Library/Application Support/avgen/picture-store` (or `$AVGEN_PICTURE_STORE`); a store inside the repository
is refused. Nothing it writes is ever committed.

`capture` renders, per scene, three single frames through the offline renderer (`avgen --headless --render`, the
deterministic path: the same build renders the same bytes) at fixed seconds inside the perf gate's measured window,
at `--size` (default 960x540, so a capture of all five scenes takes minutes rather than an hour). Every render holds the
GPU lock. It records the git revision, whether the tree was dirty, the binary and the arguments in `manifest.json`.

`compare` reports, per frame: the share of pixels whose largest channel moved by more than `--tol` 8-bit levels, the
mean absolute channel change (in levels), the largest change and where the change sits. Verdicts:
  SAME     byte-identical;
  WITHIN   at most 0.1% of pixels beyond the tolerance and a mean change of at most 0.1 level (rounding-scale: the
           render audit's acceptance for an image-neutral change, R1);
  DIFFERS  anything more. Exit 1 if any frame DIFFERS, 0 otherwise, 2 if the captures do not match up.
A seeded 2% exposure change is DIFFERS on every frame; a clean re-capture of the same build is SAME (Rift: WITHIN,
1 level; docs/testing.md).
`--heatmaps` writes a difference image beside each DIFFERS frame in the store (white = the largest change).
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from image_stats import read_png  # noqa: E402
from live_perf_baseline import ROOT, SCENES, SCENE_AUDIO, available  # noqa: E402

DEFAULT_STORE = os.environ.get("AVGEN_PICTURE_STORE") or os.path.expanduser(
    "~/Library/Application Support/avgen/picture-store")
# Seconds into each scene's measured window (the perf gate measures 30 s from its start + 5 s warm-up).
OFFSETS = (7.0, 20.0, 33.0)
FPS = 30.0
WITHIN_SHARE = 0.001   # 0.1% of pixels
WITHIN_MEAN = 0.1      # levels


def store_dir(a):
    store = os.path.abspath(os.path.expanduser(a.store))
    if os.path.commonpath([store, ROOT]) == ROOT:
        sys.exit("refusing a picture store inside the repository (%s): the frames show commercial assets and the "
                 "repository is public" % store)
    return store


def scene_start(scene):
    return float(SCENE_AUDIO[scene][1]) if scene in SCENE_AUDIO else 60.0


def git(*args, cwd=ROOT):
    return subprocess.run(["git"] + list(args), cwd=cwd, capture_output=True, text=True).stdout.strip()


def capture(a):
    store = store_dir(a)
    out = os.path.join(store, a.label)
    if os.path.exists(out):
        sys.exit("%s already exists; pick another --label or delete it" % out)
    names = list(SCENES) if not a.scenes else a.scenes.split(",")
    avgen = os.path.join(os.path.abspath(a.bin), "src", "avgen")
    if not os.path.exists(avgen):
        sys.exit("no avgen at %s" % avgen)
    checkout = os.path.dirname(os.path.dirname(os.path.abspath(a.bin)))
    os.makedirs(out)
    manifest = {"label": a.label, "captured": datetime.now().isoformat(timespec="seconds"),
                # the checkout the binary was built from (<checkout>/build/<preset>), not necessarily this one
                "revision": git("rev-parse", "--short=8", "HEAD", cwd=checkout),
                "dirty": bool(git("status", "--porcelain", "--untracked-files=no", cwd=checkout)),
                "binary": avgen, "size": a.size, "tier": a.tier or "project", "fps": FPS, "frames": {},
                "qualityArm": a.quality_arm or ""}
    failed = []
    env = dict(os.environ, AVGEN_GPU_LOCK_TIMEOUT=os.environ.get("AVGEN_GPU_LOCK_TIMEOUT", "14400"))
    for scene in names:
        why = available(scene)
        if why:
            print("-- %s SKIPPED: %s" % (scene, why))
            failed.append(scene)
            continue
        os.makedirs(os.path.join(out, scene))
        for offset in OFFSETS:
            t = scene_start(scene) + offset
            tmp = tempfile.mkdtemp(prefix="avgen-picture-")
            args = ["--headless", "--project", SCENES[scene][0], "--render", tmp + "/", "--size", a.size,
                    "--fps", "%g" % FPS, "--range", "%.4f:%.4f" % (t, t + 0.5 / FPS)]
            if a.tier:
                args += ["--tier", a.tier]
            if a.quality_arm:
                args += ["--quality-arm", a.quality_arm] # e.g. the "before" of a change behind an arm, same binary
            log = os.path.join(out, scene, "t%06.2f.log" % t)
            with open(log, "w") as f:
                rc = subprocess.run([os.path.join(ROOT, "tools", "gpu-lock.sh"), avgen] + args, cwd=ROOT, env=env,
                                    stdout=f, stderr=subprocess.STDOUT).returncode
            pngs = sorted(p for p in os.listdir(tmp) if p.endswith(".png"))
            dest = os.path.join(out, scene, "t%06.2f.png" % t)
            if rc != 0 or len(pngs) != 1:
                print("   %s %.2f s: FAILED (exit %d, %d frame(s)); see %s" % (scene, t, rc, len(pngs), log))
                failed.append("%s@%.2f" % (scene, t))
            else:
                shutil.move(os.path.join(tmp, pngs[0]), dest)
                manifest["frames"].setdefault(scene, []).append(os.path.basename(dest))
                print("   %s %.2f s -> %s" % (scene, t, dest))
            shutil.rmtree(tmp, ignore_errors=True)
    manifest["failed"] = failed
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("captured %s (%s%s) into %s" % (a.label, manifest["revision"], ", dirty" if manifest["dirty"] else "", out))
    return 1 if failed else 0


def write_png(path, w, h, grey):
    raw = b"".join(b"\x00" + bytes(grey[y * w:(y + 1) * w]) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def diff_frame(pa, pb, tol, heatmap=None):
    wa, ha, ca, a = read_png(pa)
    wb, hb, cb, b = read_png(pb)
    if (wa, ha) != (wb, hb):
        return {"error": "sizes differ: %dx%d vs %dx%d" % (wa, ha, wb, hb)}
    n = wa * ha
    beyond = total = worst = 0
    lo, hi = [wa, ha], [-1, -1]
    deltas = bytearray(n) if heatmap else None
    for i in range(n):
        ia, ib = i * ca, i * cb
        d0 = abs(a[ia] - b[ib])
        d1 = abs(a[ia + 1] - b[ib + 1])
        d2 = abs(a[ia + 2] - b[ib + 2])
        d = max(d0, d1, d2)
        if d:
            total += d0 + d1 + d2
            worst = max(worst, d)
            if d > tol:
                beyond += 1
                x, y = i % wa, i // wa
                lo = [min(lo[0], x), min(lo[1], y)]
                hi = [max(hi[0], x), max(hi[1], y)]
            if deltas is not None:
                deltas[i] = d
    share = beyond / n
    mean = total / (3.0 * n)
    verdict = "SAME" if worst == 0 else ("WITHIN" if share <= WITHIN_SHARE and mean <= WITHIN_MEAN else "DIFFERS")
    if heatmap and verdict == "DIFFERS" and worst:
        write_png(heatmap, wa, ha, bytes(min(255, d * 255 // worst) for d in deltas))
    return {"share": share, "mean": mean, "max": worst, "verdict": verdict,
            "bbox": None if hi[0] < 0 else [lo[0], lo[1], hi[0], hi[1]]}


def compare(a):
    store = store_dir(a)
    da, db = os.path.join(store, a.a), os.path.join(store, a.b)
    ma = json.load(open(os.path.join(da, "manifest.json")))
    mb = json.load(open(os.path.join(db, "manifest.json")))
    for label, m in ((a.a, ma), (a.b, mb)):
        print("%s %s: %s%s, %s, tier %s%s" % ("A" if m is ma else "B", label, m["revision"], " (dirty)" if m["dirty"] else "",
                                             m["size"], m["tier"], ", arm " + m["qualityArm"] if m.get("qualityArm") else ""))
    if (ma["size"], ma["tier"]) != (mb["size"], mb["tier"]):
        print("the captures were taken at different sizes or tiers: not comparable")
        return 2
    differs = mismatched = False
    print("%-16s %-12s %8s %7s %4s  %-8s %s" % ("scene", "frame", ">tol", "mean", "max", "verdict", "where"))
    for scene in sorted(set(ma["frames"]) | set(mb["frames"])):
        for frame in sorted(set(ma["frames"].get(scene, [])) | set(mb["frames"].get(scene, []))):
            pa, pb = os.path.join(da, scene, frame), os.path.join(db, scene, frame)
            if not (os.path.exists(pa) and os.path.exists(pb)):
                print("%-16s %-12s missing from %s" % (scene, frame, a.a if not os.path.exists(pa) else a.b))
                mismatched = True
                continue
            heat = os.path.join(db, scene, frame[:-4] + ".diff-vs-%s.png" % a.a) if a.heatmaps else None
            r = diff_frame(pa, pb, a.tol, heat)
            if "error" in r:
                print("%-16s %-12s %s" % (scene, frame, r["error"]))
                mismatched = True
                continue
            differs = differs or r["verdict"] == "DIFFERS"
            print("%-16s %-12s %7.3f%% %7.3f %4d  %-8s %s" % (scene, frame[:-4], 100 * r["share"], r["mean"], r["max"],
                                                            r["verdict"], r["bbox"] or ""))
    result = "MISMATCHED" if mismatched else ("DIFFERS" if differs else "SAME OR WITHIN")
    print("RESULT:", result)
    return 2 if mismatched else (1 if differs else 0)


def list_store(a):
    store = store_dir(a)
    if not os.path.isdir(store):
        print("no store at", store)
        return 0
    for label in sorted(os.listdir(store)):
        mp = os.path.join(store, label, "manifest.json")
        if os.path.exists(mp):
            m = json.load(open(mp))
            print("%-24s %s%s %s %s, %d frame(s)" % (label, m["revision"], "+dirty" if m["dirty"] else "",
                                                    m["captured"], m["size"],
                                                    sum(len(v) for v in m["frames"].values())))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--label", required=True)
    c.add_argument("--bin", default=os.path.join(ROOT, "build", "release"))
    c.add_argument("--scenes", help="comma-separated subset of: " + ", ".join(SCENES))
    c.add_argument("--size", default="960x540")
    c.add_argument("--tier", help="render tier (default: each project's own render settings)")
    c.add_argument("--quality-arm", help="quality arms (the --ab names, comma separated) applied to every render")
    c.add_argument("--store", default=DEFAULT_STORE)
    d = sub.add_parser("compare")
    d.add_argument("a")
    d.add_argument("b")
    d.add_argument("--store", default=DEFAULT_STORE)
    d.add_argument("--tol", type=int, default=2, help="levels of 8-bit change a pixel may move and not count")
    d.add_argument("--heatmaps", action="store_true")
    s = sub.add_parser("list")
    s.add_argument("--store", default=DEFAULT_STORE)
    a = ap.parse_args()
    return {"capture": capture, "compare": compare, "list": list_store}[a.command](a)


if __name__ == "__main__":
    sys.exit(main())
