#!/usr/bin/env python3
"""Derive one single-variant arm per tornado from the §47 showcase scene.

    python3 tools/make_tornado_showcase.py [--render] [--size 1280x720]

`examples/labs/tornado-showcase.scene.json` authors all seven of the brief's §47 variants in one
scene, which is the file §47 asks for and the shot to render the day the volumetric slot array
exists. **Today only one of them renders**: `EffectBucket::Tornado` caps at a single live medium,
so six resolve into `AtmosphericCounts::dropped` and contribute nothing to the picture.

So this writes seven derivative scenes, each keeping exactly one tornado and framing a camera for
it, and the contact sheet of those seven is the acceptance evidence available now. Every file it
writes begins with `_tc-` and is a *derivative*: regenerate after any edit to the showcase, or the
comparison stops comparing what ships.

**The arms differ from the showcase in two things and only two**: which tornado is enabled, and
where the camera stands. No variant's values are touched here -- they live in the showcase file,
which is the thing under test. `make_vortex2_arms.py` learned the same lesson one effect over and
says so: an arm that re-specifies what it is testing is testing itself.

**Why the camera is derived rather than authored per variant.** One rule applied to all seven,
rather than seven framings chosen by hand -- which matters because the wedge is the known weak
variant and a camera tuned per variant would flatter it. If it is weak from the same rule everyone
else got, it is weak.

**The first version of that rule was wrong, and wrong in a way that looked like the effect's fault.**
It framed on HEIGHT alone -- distance `1.7 * height`, from ADR-580 §2's measurement that a column of
height H fills about nine tenths of a 36-40 degree frame at 1.7 H. Applied to all seven, the wedge
subtended **71.7 degrees of horizontal arc in a 40 degree frame** and the thick cloud 50, so both
rendered as a featureless grey wall filling the picture. Read off the contact sheet that looks like
two variants failing; read off the geometry it is one rule failing on any variant wider than it is
tall -- which the WMO's definition of a wedge makes the *defining* property of one of them.

So the rule frames on `max(height, width)`, where width is the widest thing in the picture (the wall
cloud). **Framing on height alone was the biased rule**, not the fix for it: it flatters tall thin
variants and destroys wide ones, and half of §47's set is wide on purpose.

**The step count is derived too, and for the reason ADR-580 measured in Phase 2.** A medium narrower
than the march's step spacing is not rendered coarsely, it is rendered WRONG -- stepped straight past
and broken into disconnected blobs. The rope is 24 m across; at the showcase's 128 steps over 4 km
the spacing is 31 m and it came back in pieces. So each arm asks for a step length of about a third
of its own narrowest feature. That makes the rope expensive and the wedge cheap, which is the
correct relationship and the opposite of a fixed count.
"""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent
SHOWCASE = HERE / "examples" / "labs" / "tornado-showcase.scene.json"
OUT = HERE / "examples" / "labs"


def arms(scene: dict) -> list[tuple[str, dict]]:
    out = []
    # ADR-702: the one `effects` array; a tornado's own rows are under `parameters`.
    tornadoes = [e for e in scene["effects"] if e.get("type") == "tornado"]
    for effect in tornadoes:
        s = copy.deepcopy(scene)
        # One live tornado. The others stay in the file -- an arm that DELETES them would also be
        # testing a different scene graph, and the point is to change one thing. Matched by id,
        # which is what identifies an effect.
        for e in s["effects"]:
            if e.get("type") == "tornado":
                e["enabled"] = e["id"] == effect["id"]
        t = effect["parameters"]
        h = float(t["height"])
        base = t["base"]
        # The widest thing in the picture is the wall cloud, not the funnel.
        width = 2.0 * max(float(t["radiusBottom"]), float(t["radiusTop"]),
                          float(t["radiusTop"]) * float(t["cloudWidth"]))
        extent = max(h, width)
        distance = extent * 1.7
        s["camera"] = {
            "mode": 1,
            "position": [base[0], base[1] + max(h * 0.06, 2.0), base[2] + distance],
            "target": [base[0], base[1] + h * 0.55, base[2]],
            "fov": 40,
            "orbitSpeed": 0,
        }
        s["environment"] = dict(s["environment"])
        # Far enough to clear the far side of the storm, and no further: cost is coverage of
        # non-zero density (ADR-374), but the distance still bounds where the samples land.
        far = distance + extent * 2.0
        s["environment"]["volumeMaxDistance"] = round(far, 1)
        # A step of about a third of the narrowest feature. Below that the medium is not coarse,
        # it is wrong -- see the module docstring and ADR-580's rope measurement.
        narrowest = 2.0 * float(t["radiusBottom"])
        steps = int(max(96, min(512, round(far / max(narrowest / 3.0, 1e-3)))))
        s["environment"]["volumeSteps"] = steps
        # The cosmic variant makes its own light and wants a dark sky to make it against (§32).
        if float(t.get("emission", 0.0)) > 0.05:
            s["environment"]["background"] = [0.010, 0.012, 0.028]
        name = "_tc-" + effect["name"].split(" ", 1)[0] + "-" + \
               effect["name"].split(" ", 1)[1].lower().replace(" ", "-")
        s["name"] = "Tornado showcase: " + effect["name"]
        s["_note"] = (
            "Derived by tools/make_tornado_showcase.py from tornado-showcase.scene.json -- do not "
            "edit. One of §47's seven variants, alone, because the march has one medium slot "
            "today. The camera is derived by one rule for every variant (ground level at 1.7x the "
            "height, looking at 0.55x it) so that no variant is flattered by a framing chosen for "
            "it.")
        out.append((name, s))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", action="store_true", help="render each arm after writing it")
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--at", default="6", help="transport second to render")
    ap.add_argument("--out", default=None, help="directory for rendered frames")
    args = ap.parse_args()

    scene = json.loads(SHOWCASE.read_text())
    written = []
    for name, s in arms(scene):
        path = OUT / f"{name}.scene.json"
        path.write_text(json.dumps(s, indent=1))
        written.append(path)
        print(f"wrote {path.relative_to(HERE)}")

    if not args.render:
        return 0
    binary = HERE / "build" / "release" / "src" / "avgen"
    if not binary.exists():
        print(f"no binary at {binary}", file=sys.stderr)
        return 1
    outdir = pathlib.Path(args.out) if args.out else (HERE / "renders" / "tornado-showcase")
    outdir.mkdir(parents=True, exist_ok=True)
    for path in written:
        dest = outdir / path.stem
        cmd = [str(binary), "--headless", "--composition", str(path), "--render", str(dest),
               "--format", "png", "--range", f"{args.at}:{args.at}", "--fps", "30",
               "--size", args.size]
        r = subprocess.run(cmd, capture_output=True, text=True)
        # The exit code off the binary, never off a pipeline.
        tag = "ok " if r.returncode == 0 else f"EXIT {r.returncode}"
        hashes = [ln for ln in r.stderr.splitlines() + r.stdout.splitlines() if "sequence hash" in ln]
        print(f"  {tag} {path.stem}  {hashes[-1].split('sequence hash')[-1].strip() if hashes else ''}")
        if r.returncode != 0:
            print(r.stderr[-800:], file=sys.stderr)
            return r.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
