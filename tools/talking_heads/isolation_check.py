#!/usr/bin/env python3
"""Talking Heads: every head's effects are its own source's (the owner, stop 4: "the secondary effects on the
entities should all be unique to the sound source").

Runs the engine's sonic trace (no GPU) on Feline Footwear's stems, then again with ONE stem replaced by silence of the
same length, and compares every column a head owns: its signals (`signal.<p>...`, `visual.<p>...`) and every route
target's final value under its prefix (`nodes/<p>-`, `fx/<p>-`, `material/<p>-`, `lights/<p>-`, `procedural/<p>-`).
The muted stem's head must change, and every other head must be bit-identical on every frame. Exits non-zero
otherwise.

    python3 tools/talking_heads/isolation_check.py --project <talking-heads.json> [--mute drums|bass|synth|all]
"""
import argparse
import csv
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)
import review as rv  # noqa: E402
from tools.sonic_vfx.scenes import talking_heads as th  # noqa: E402

AVGEN = os.environ.get("AVGEN") or os.path.join(REPO, "build", "release", "src", "avgen")


def trace(project, work, tag, mute=None):
    proj = rv.variant(project, "feline", True, work)
    import json
    doc = json.load(open(proj))
    if mute:
        for st in doc["assets"]["stems"]:
            if st["role"] == mute:
                silent = os.path.join(work, "silent-%s.wav" % mute)
                dur = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                      st["file"]], capture_output=True, text=True).stdout.strip()
                subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                                "anullsrc=r=48000:cl=stereo", "-t", dur, silent], check=True)
                st["file"] = silent
    pj = os.path.join(work, tag + ".json")
    json.dump(doc, open(pj, "w"), indent=1)
    out = os.path.join(work, tag + ".csv")
    r = subprocess.run([AVGEN, "--project", pj, "--sonic-trace", out, "--trace-finals"], capture_output=True,
                       text=True)
    if r.returncode != 0:
        sys.exit("avgen exited %d\n%s" % (r.returncode, r.stderr[-2000:]))
    return list(csv.DictReader(open(out)))


def owned(col, P):
    return (col.startswith(("signal." + P, "visual." + P)) or
            any(col.startswith("final:%s/%s-" % (k, P)) for k in ("nodes", "fx", "material", "lights", "procedural")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--mute", default="all")
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="th-iso-")
    base = trace(a.project, work, "base")
    roles = {h["role"]: h["prefix"] for h in th.HEADS}
    fails = 0
    for mute in (list(roles) if a.mute == "all" else [a.mute]):
        rows = trace(a.project, work, "mute-" + mute, mute)
        n = min(len(rows), len(base))
        for role, P in roles.items():
            cols = [c for c in base[0] if owned(c, P)]
            diff = sum(1 for c in cols for i in range(n) if rows[i][c] != base[i][c])
            changed = sorted({c for c in cols for i in range(n) if rows[i][c] != base[i][c]})
            if role == mute:
                ok = diff > 0
                print("mute %-5s -> %s (its own head): %d of %d columns changed%s" % (
                    mute, P, len(changed), len(cols), "" if ok else "  FAIL: muting its stem changed nothing"))
            else:
                ok = diff == 0
                print("mute %-5s -> %s: %d columns x %d frames, %s" % (
                    mute, P, len(cols), n, "bit-identical" if ok else "CHANGED: %s" % changed[:8]))
            fails += 0 if ok else 1
    if fails:
        sys.exit("%d isolation failure(s)" % fails)
    print("every head's effects are its own source's")


if __name__ == "__main__":
    main()
