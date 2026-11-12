#!/usr/bin/env python3
"""The Throw Lab (CPD-R7): a crest throwing an alien, isolated so its timing can be refined in minutes.

From a built Musical Geography scene and project (art's build.py output: the look, the alien, its clips and hop settings)
this writes a lab project into --out: one flat tile ground with the scene's material, one scripted planar wave (no music),
one alien standing still where the crest passes, and a fixed camera -- side-on at the alien's height, or closer to the
flight's view. Then, as asked:

  --measure  runs the hidden case "the throw lab..." (CPU, a few seconds) and writes lab.csv: per frame the drawn tile
             top under the feet, the crest alone, the body, the drawn root, the posed feet, the animation state, clip
             time and blend, the activity and the launch;
  --plot     draws lab.csv on one time axis (lab-plot.png);
  --render   renders the clip through tools/gpu-lock.sh (lab-<camera>.mp4).

The alien's GLB is third-party: it is read from assets/aliens/ (gitignored) and nothing derived from it is written here.

  python3 tools/throw_lab.py --scene <build>/musical-geography.scene.json --project <build>/musical-geography.json \\
      --out <dir> --measure --plot --render --camera side
"""

import argparse
import copy
import json
import math
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def lab_scene(base, a):
    s = copy.deepcopy(base)
    ground = next(n for n in s["nodes"] if n["name"] == a.ground)
    gen = ground["procedural"]["distribution"]["generator"]
    tiles = gen["tiles"]
    # flat land everywhere: no hills, coast, features or path
    for k in ("rivers", "ponds", "valleys", "ridges", "rises"):
        tiles.pop(k, None)
    tiles.update(hillHeight=0.0, landThreshold=0.0, continentWavelength=100000.0, path=[], shoreRise=0.5)
    gen.pop("morph", None)
    alien = next(n for n in s["nodes"] if n["name"] == a.body)
    alien["position"] = [a.x, 0.0, a.z]
    keep = {a.ground, a.body}
    if a.neighbour is not None:   # CPD-R9: a second alien whose landings spring the measured one's tile
        other = next(n for n in s["nodes"] if n["name"] == a.neighbour_body)
        other["position"] = [a.x + a.neighbour, 0.0, a.z]
        keep.add(a.neighbour_body)
    s["nodes"] = [n for n in s["nodes"] if n["name"] in keep]
    s["lights"] = [l for l in s.get("lights", []) if l.get("name") in ("sun", "moon", "fill")]
    s.pop("flight", None)
    ents = []
    for name, running in ((a.body, a.run), (a.neighbour_body, False)):
        if name != a.body and a.neighbour is None:
            continue
        ent = next(e for e in s["entities"] if e["name"] == name)
        for b in ent["behaviors"]:
            if b.get("kind") == "hop":
                b.update(homeRadius=0.0, hopChance=0.0, pauseMin=0.5, pauseMax=0.5)
                if running:   # CPD-R9: a body running about where the crest passes (short goals, short pauses)
                    b.update(homeRadius=12.0, runChance=1.0, pauseMin=0.2, pauseMax=0.4, goalMin=2, goalMax=4)
                for key in ("tossThreshold", "tossGain", "tossMax", "tossCooldown", "throwClipLift"):
                    v = getattr(a, key)
                    if v is not None:
                        b[key] = v
        ent.pop("reactions", None)  # no music: the hop's apex and strength stay as authored
        ents.append(ent)
    s["entities"] = ents
    # CPD-R12 (ADR-2007): beach balls -- chrome orbs bouncing on the tiles around the alien
    for i in range(a.balls):
        ang = 2.0 * math.pi * i / max(a.balls, 1)
        bx, bz = a.x + 6.0 * math.cos(ang), a.z + 6.0 * math.sin(ang)
        name = "ball%02d" % i
        s["nodes"].append({"name": name, "kind": "orb", "position": [bx, 4.0 + 1.5 * i, bz],
                           "scale": [a.ball_radius] * 3,
                           "material": {"baseColor": [0.95, 0.95, 0.97], "metallic": 1.0, "roughness": 0.04}})
        s["entities"].append({"name": name, "node": name, "seed": 9000 + i,
                              "behaviors": [{"kind": "ball", "radius": a.ball_radius, "homeRadius": 10.0,
                                             "velocity": [2.0 * math.sin(ang), 5.0 + i, -2.0 * math.cos(ang)]}]})
    # one planar train along +X whose crest reaches the alien at --crest (and every period after)
    phase = 0.25 + a.speed * a.crest / a.wavelength - a.x / a.wavelength
    dyn = s["environment"]["tileDynamics"]
    dyn.update(amplitude=1.0, breathing={"amplitude": 0.0, "period": 18, "wavelength": 800}, rolling=[0.0, 0.0])
    dyn["waves"] = [{"kind": "planar", "amplitude": a.amplitude, "wavelength": a.wavelength, "speed": a.speed,
                     "direction": 0, "sharpness": a.sharpness, "onLand": 1, "onWater": 1, "crest": 0.6,
                     "phase": round(phase - math.floor(phase), 6)}]
    if a.ripple is not None:   # CPD-R9: a second, shorter train riding the first -- a crest with two tops
        amp, wl, sp = a.ripple
        p2 = 0.25 + sp * (a.crest + a.ripple_lag) / wl - a.x / wl
        dyn["waves"].append({"kind": "planar", "amplitude": amp, "wavelength": wl, "speed": sp, "direction": 0,
                             "sharpness": 0.0, "onLand": 1, "onWater": 1, "crest": 0.6,
                             "phase": round(p2 - math.floor(p2), 6)})
    dyn.setdefault("rings", {})["amplitude"] = 0.0
    if a.ring is not None:   # CPD-R9: one authored ring (the scene's ring spec) passing over the crest -- several tops
        amp, dist, lag = a.ring
        dyn["rings"]["amplitude"] = amp
        gx, _, gz = ground.get("position", [0.0, 0.0, 0.0])
        when = a.crest + lag - dist / dyn["rings"].get("speed", 20.0)
        dyn["events"] = [{"time": round(when, 4), "position": [a.x - dist - gx, a.z - gz], "strength": 1.0}]
    eye = 1.4 + a.amplitude
    if a.camera == "side":   # side-on, at the alien's height, the wave crossing the frame left to right
        s["camera"] = {"mode": 1, "orbitSpeed": 0.0, "position": [a.x, eye, a.z + 22.0],
                       "target": [a.x, eye + 2.5, a.z], "fov": 40.0, "far": 200000.0}
    else:                    # nearer the flight's: above and behind, looking down at the throw
        s["camera"] = {"mode": 1, "orbitSpeed": 0.0, "position": [a.x - 16.0, 7.0, a.z + 20.0],
                       "target": [a.x, 2.5, a.z], "fov": 40.0, "far": 200000.0}
    return s


def lab_project(base_project, scene_name, a):
    p = {"format": "avgen-project", "version": 4,
         "assets": {"scene": {"kind": "composition", "path": scene_name}},
         "parameters": {k: v for k, v in base_project.get("parameters", {}).items() if k.startswith("post/")},
         "render": {"width": a.width, "height": a.height, "fps": 30, "output": "video", "path": "lab.mp4"}}
    return p


def plot(csv_path, out_path, title):
    import csv
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    rows = list(csv.DictReader(open(csv_path)))
    t = [float(r["t"]) for r in rows]
    f = lambda k: [float(r[k]) if r[k] not in ("", "nan") else float("nan") for r in rows]
    ink, ink2, surf, grid = "#0b0b0b", "#52514e", "#fcfcfb", "#e4e3df"
    blue, orange, aqua, violet = "#2a78d6", "#eb6834", "#1baf7a", "#6250d6"
    fig, (a0, a1) = plt.subplots(2, 1, figsize=(15, 8), sharex=True, facecolor=surf,
                                 gridspec_kw={"height_ratios": [3, 1]})
    for ax in (a0, a1):
        ax.set_facecolor(surf)
        ax.grid(axis="y", color=grid, lw=0.8)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)
    a0.plot(t, f("wave"), color=blue, lw=2.2, label="tile top under the feet (the crest alone)")
    a0.plot(t, f("tile"), color=blue, lw=1, ls=":", label="tile top drawn (with the landing spring)")
    a0.plot(t, f("feet"), color=orange, lw=1.8, label="the drawn feet (lowest posed vertex)")
    a0.plot(t, f("body"), color=ink2, lw=1, ls="--", label="the simulated body")
    for r, x in zip(rows, t):
        if r["launch"] == "1":
            for ax in (a0, a1):
                ax.axvline(x, color=ink, lw=1)
            a0.text(x + 0.03, a0.get_ylim()[1] * 0.95 if a0.get_ylim()[1] > 0 else 0, "launch %.2f s" % x, fontsize=8)
    w = f("wave")
    for i in range(1, len(w) - 1):   # the crest's top under the feet
        if w[i] > w[i - 1] and w[i] >= w[i + 1] and w[i] > 0.5 * max(x for x in w if x == x):
            a0.axvline(t[i], color=blue, lw=1, ls="--", alpha=0.7)
            a0.text(t[i] + 0.03, w[i] + 0.1, "crest %.2f s" % t[i], fontsize=8, color=blue)
    a0.set_ylabel("height (m)", color=ink2)
    a0.legend(loc="upper left", frameon=False, fontsize=8)
    a0.set_title(title, loc="left", color=ink, fontsize=10)
    # the animation state as a band, with the clip time inside it
    states = [r["state"] for r in rows]
    names = sorted(set(states))
    colors = dict(zip(names, [aqua, violet, orange, blue, "#eda100", "#e87ba4", ink2]))
    start = 0
    for i in range(1, len(rows) + 1):
        if i == len(rows) or states[i] != states[start]:
            a1.axvspan(t[start], t[min(i, len(rows) - 1)], color=colors[states[start]], alpha=0.35, lw=0)
            a1.text(t[start] + 0.02, 0.85, states[start], fontsize=8, color=ink, va="top")
            start = i
    ct = f("clipTime")
    top = max([x for x in ct if x == x] or [1.0])
    a1.plot(t, [x / top for x in ct], color=ink, lw=1, label="clip time (scaled)")
    a1.plot(t, f("blend"), color=ink2, lw=1, ls=":", label="blend weight")
    a1.set_ylim(0, 1.05)
    a1.set_yticks([])
    a1.set_xlabel("time (s)", color=ink2)
    a1.legend(loc="lower right", frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=120, facecolor=surf)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scene", required=True)
    ap.add_argument("--project", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ground", default="mgTiles1")
    ap.add_argument("--body", default="alien00")
    ap.add_argument("--x", type=float, default=2.0)
    ap.add_argument("--z", type=float, default=2.0)
    ap.add_argument("--amplitude", type=float, default=1.2, help="the wave's height, metres")
    ap.add_argument("--wavelength", type=float, default=48.0)
    ap.add_argument("--speed", type=float, default=8.0)
    ap.add_argument("--sharpness", type=float, default=0.55)
    ap.add_argument("--crest", type=float, default=3.0, help="when the first crest's top is under the alien (s)")
    ap.add_argument("--tossThreshold", type=float, default=None)
    ap.add_argument("--tossGain", type=float, default=None)
    ap.add_argument("--tossMax", type=float, default=None)
    ap.add_argument("--tossCooldown", type=float, default=None)
    ap.add_argument("--throwClipLift", type=float, default=None, help="the jump clip's own foot lift (m) to take off the arc")
    ap.add_argument("--ripple", type=float, nargs=3, default=None, metavar=("AMP", "WAVELENGTH", "SPEED"),
                    help="CPD-R9: a second planar train (a crest with two tops)")
    ap.add_argument("--ripple-lag", type=float, default=0.45, help="its crest's top this long after the first's (s)")
    ap.add_argument("--ring", type=float, nargs=3, default=None, metavar=("AMP", "DIST", "LAG"),
                    help="CPD-R9: one ring (the scene's ring spec, this amplitude) dropped DIST m up-wave, its front "
                         "reaching the alien LAG s after the crest's top")
    ap.add_argument("--neighbour", type=float, default=None, metavar="DX",
                    help="CPD-R9: a second alien DX metres along the wave, thrown too; its landings spring the tile")
    ap.add_argument("--neighbour-body", default="alien01")
    ap.add_argument("--run", action="store_true", help="CPD-R9: the measured alien runs about instead of standing")
    ap.add_argument("--balls", type=int, default=0, help="CPD-R12: this many beach balls bouncing around the alien")
    ap.add_argument("--ball-radius", type=float, default=0.6)
    ap.add_argument("--camera", default="side", choices=("side", "close"))
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--measure", action="store_true")
    ap.add_argument("--plot", action="store_true")
    ap.add_argument("--render", action="store_true")
    ap.add_argument("--tag", default="", help="a suffix for the output files")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    base = json.load(open(a.scene))
    base_project = json.load(open(a.project))
    scene = lab_scene(base, a)
    tag = ("-" + a.tag) if a.tag else ""
    scene_name = "throw-lab-%s%s.scene.json" % (a.camera, tag)
    json.dump(scene, open(os.path.join(a.out, scene_name), "w"), indent=1)
    proj_path = os.path.join(a.out, "throw-lab-%s%s.json" % (a.camera, tag))
    json.dump(lab_project(base_project, scene_name, a), open(proj_path, "w"), indent=1)
    print("wrote", proj_path)
    csv_path = os.path.join(a.out, "lab%s.csv" % tag)
    if a.measure:
        env = dict(os.environ, AVGEN_THROWLAB_PROJECT=proj_path, AVGEN_THROWLAB_CSV=csv_path,
                   AVGEN_THROWLAB_TO=str(a.seconds), AVGEN_THROWLAB_BODY=a.body)
        r = subprocess.run([os.path.join(REPO, "build/release/tests/avgen_tests"), "the throw lab*"], env=env,
                           capture_output=True, text=True)
        print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-400:])
        if r.returncode != 0:
            sys.exit(r.returncode)
        print("wrote", csv_path)
    if a.plot:
        out = os.path.join(a.out, "lab-plot%s.png" % tag)
        plot(csv_path, out, "Throw Lab%s: wave %.1f m, %.0f m, %.1f m/s, sharpness %.2f; crest at %.1f s" %
             (tag, a.amplitude, a.wavelength, a.speed, a.sharpness, a.crest))
        print("wrote", out)
    if a.render:
        clip = os.path.join(a.out, "lab-%s%s.mp4" % (a.camera, tag))
        cmd = [os.path.join(REPO, "tools/gpu-lock.sh"), os.path.join(REPO, "build/release/src/avgen"), "--project",
               proj_path, "--render", clip, "--range", "0:%g" % a.seconds]
        r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout[-800:], r.stderr[-800:])
            sys.exit(r.returncode)
        print("wrote", clip)


if __name__ == "__main__":
    main()
