#!/usr/bin/env python3
"""The Living Prism, Stage 6: the functional tests as traces (no GPU).

    python3 docs/prototypes/living-prism/scripts/functional_tests.py build/prism/func

Writes five variants of the offline project (the three stems; no drum stem; a silent drum stem; the stereo mix
alone; silence) into <dir>, traces each with `avgen --sonic-trace` (30 fps, the whole song), and prints every
driver's median and maximum in four windows: the drums alone (0.3-8 s), the typical window, after the drums stop
dead (184-194 s), and the last second.
"""
import copy
import csv
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
out = os.path.abspath(sys.argv[1])
os.makedirs(out, exist_ok=True)
silent = os.path.join(out, "silent.wav")
if not os.path.exists(silent):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo", "-t", "210.5",
                    "-c:a", "pcm_s24le", silent], check=True)
d = os.path.join(ROOT, "examples", "living-prism")
base = json.load(open(os.path.join(d, "living-prism-offline.json")))
base["assets"]["scene"]["path"] = os.path.join(d, base["assets"]["scene"]["path"])
base["assets"]["audio"]["path"] = os.path.normpath(os.path.join(d, base["assets"]["audio"]["path"]))
for s in base["assets"]["stems"]:
    s["file"] = os.path.normpath(os.path.join(d, s["file"]))
V = {"stems": copy.deepcopy(base)}
p = copy.deepcopy(base); p["assets"]["stems"] = [s for s in p["assets"]["stems"] if s["key"] != "drums"]; V["nodrums"] = p
p = copy.deepcopy(base)
for s in p["assets"]["stems"]:
    if s["key"] == "drums":
        s["file"] = silent
V["silentdrums"] = p
p = copy.deepcopy(base); p["assets"].pop("stems"); V["stereo"] = p
p = copy.deepcopy(base); p["assets"].pop("stems"); p["assets"]["audio"]["path"] = silent; V["silence"] = p
SIG = ["hit", "front", "accent", "glint", "bass", "synth", "bright", "energy", "section"]
W = {"drums alone 0.3-8": (0.3, 8), "typical 65.7-85.5": (65.7, 85.5), "drums stopped 184-194": (184, 194),
     "last second 209-210.4": (209, 210.4)}
for k, p in V.items():
    pj = os.path.join(out, k + ".json")
    json.dump(p, open(pj, "w"), indent=1)
    csvp = os.path.join(out, k + ".csv")
    r = subprocess.run([os.path.join(ROOT, "build/release/src/avgen"), "--project", pj, "--sonic-trace", csvp,
                        "--fps", "30", "--log", "warn"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rows = list(csv.DictReader(open(csvp)))
    print("== %s (exit %d)" % (k, r.returncode))
    for wn, (a, b) in W.items():
        w = [row for row in rows if a <= float(row["time"]) < b]
        cells = []
        for s in SIG:
            v = sorted(float(row["signal." + s]) for row in w)
            cells.append("%s %.2f/%.2f" % (s, v[len(v) // 2], v[-1]))
        print("  %-22s %s" % (wn, " ".join(cells)))
