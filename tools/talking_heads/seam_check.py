#!/usr/bin/env python3
"""Talking Heads: no secondary effect may reveal where a bust's pieces meet (the owner, stop 4: "see if its possible to
eliminate this seam that gets exposed by some of the secondary entity effects").

One head is held at rest (pose_stills.posed), its routes removed, and each effect is set by hand to full strength:
- rest: nothing;
- wash: the possession wash at its open maximum, plus the flash;
- glow: the entity glow at full on every piece;
- bands: the signal bands;
- tear: the world-space shear;
- ghosts: both spectral copies, grown and pulled apart as on a flash;
- spill: its light on.
Each is rendered close on the neck, front and three-quarter. A strip down the neck's middle is averaged across, and the
largest colour step between neighbouring rows within +-10 px of the head/neck seam is compared with the same image's
largest step in the rows 12-30 px above and below it: a seam is a step at the boundary that its surroundings do not have.
Passes when the seam step exceeds its surroundings by under --tol (0..255 per channel). Writes each close-up and
exits non-zero on a failure. Every render goes through tools/gpu-lock.sh.

    python3 tools/talking_heads/seam_check.py --project <talking-heads.json> --out DIR [--prefix th2] [--tol 6]
"""
import argparse
import copy
import json
import math
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import anchor_check as ac  # noqa: E402
import pose_stills as ps  # noqa: E402
from tools.sonic_vfx.scenes import talking_heads as th  # noqa: E402

EFFECTS = ("rest", "wash", "glow", "bands", "tear", "ghosts", "spill")


def apply_effect(p, s, P, effect):
    progs = {g["name"]: g for g in s.get("materialPrograms", [])}
    if effect == "wash":
        for part in ("body", "head"):
            o = th.PROGRAM_OPS[P + "-" + part]
            op = progs[P + "-" + part]["ops"][o.at["washAmount"] - 1]
            op["constant"] = [0.0, 1.0, 0.12, 0.9]
    if effect == "bands":
        for part in ("body", "head"):
            o = th.PROGRAM_OPS[P + "-" + part]
            progs[P + "-" + part]["ops"][o.at["bands"] - 1]["value"] = 0.82
    for e in p["effects"]:
        if effect == "glow" and e["type"] == "glow" and e["id"].startswith(P + "-glow"):
            e["parameters"]["glow"] = 1.0
    for n in s["nodes"]:
        if effect == "tear" and n["name"] in (P + "-bust", P + "-neck", P + "-head"):
            for d in n["procedural"]["deformers"]:
                if d["kind"] == "sine":
                    d["amount"] = 0.006
                    d["phase"] = 1.3
        if effect == "ghosts" and n["name"] in (P + "-ghostAh", P + "-ghostBh"):
            sg = 1.0 if "ghostA" in n["name"] else -1.0
            n["scale"] = [1.0, 1.0, 1.0]
            n["position"] = [0.028 * sg, 0.011 * sg, 0.0]
    if effect == "spill":
        for l in s.get("lights", []):
            if l.get("name") == P + "-spill" or l.get("id") == P + "-spill":
                l["intensity"] = 1.6
    return p, s


def step_metric(img, x0, x1, ys):
    px = img.load()
    rows = []
    for y in ys:
        acc = [0.0, 0.0, 0.0]
        for x in range(x0, x1):
            c = px[x, y]
            for k in range(3):
                acc[k] += c[k]
        rows.append([v / (x1 - x0) for v in acc])
    return [max(abs(a[k] - b[k]) for k in range(3)) for a, b in zip(rows, rows[1:])]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--prefix", default="th2")
    ap.add_argument("--tol", type=float, default=6.0)
    ap.add_argument("--effects", default=",".join(EFFECTS))
    a = ap.parse_args()
    from PIL import Image, ImageDraw
    ac.W, ac.H = 1280, 720
    base = os.path.dirname(os.path.abspath(a.project))
    project = json.load(open(a.project))
    sp = project["assets"]["scene"]["path"]
    scene = json.load(open(sp if os.path.isabs(sp) else os.path.join(base, sp)))
    for n in scene["nodes"]:
        src = n.get("procedural", {}).get("source", {})
        if "asset" in src and not os.path.isabs(src["asset"]):
            src["asset"] = os.path.join(base, src["asset"])
        if "asset" in n and not os.path.isabs(n["asset"]):
            n["asset"] = os.path.join(base, n["asset"])
    P = a.prefix
    th.build()   # fills PROGRAM_OPS (op labels -> indices)
    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="th-seam-")
    fails = []
    try:
        for view in ("front", "three-quarter"):
            for effect in a.effects.split(","):
                pp, ss = ps.posed(project, scene, P, 0.0, 0.0, 0.0, rays_on=False)
                pp["routes"] = [r for r in pp["routes"] if not r["target"].startswith(
                    ("material/%s-" % P, "fx/%s-" % P, "lights/%s-" % P, "particles/%s-" % P))]
                pp, ss = apply_effect(pp, ss, P, effect)
                nodes = {n["name"]: n for n in ss["nodes"]}
                bust = nodes[P + "-bust"]
                hd = bust.get("rotation", [0.0, 0.0, 0.0])[1]
                # the seam: the head piece's lowest ring, at the pivot (the neck's top), in world space
                seam = ac.apply(ac.node_world(nodes, P + "-pivot"), [0.0, -0.004, 0.0])
                aim = [seam[0], seam[1] + 0.02, seam[2]]
                yaw = hd + (0.0 if view == "front" else -32.0)
                cam = [aim[0] + 0.45 * math.sin(math.radians(yaw)), aim[1] + 0.02,
                       aim[2] + 0.45 * math.cos(math.radians(yaw))]
                ss["camera"] = {"mode": 1, "position": cam, "target": aim, "fov": 40.0, "orbitSpeed": 0.0}
                png = ac.render(pp, ss, work, "seam")
                img = Image.open(png).convert("RGB")
                focal = float(pp["parameters"].get("camera/lens/focalLength", 35.0))
                u, v = ac.to_screen(cam, aim, focal, seam)
                # the neck's middle: a strip a quarter of its width either side of the seam's centre line
                front = ac.apply(ac.node_world(nodes, P + "-pivot"), [0.0, -0.004, 0.035])
                fu, _ = ac.to_screen(cam, aim, focal, front)
                cx = int(round(fu if view == "three-quarter" else u))
                x0, x1 = cx - 12, cx + 12
                vy = int(round(v))
                near = step_metric(img, x0, x1, range(vy - 10, vy + 11))
                around = step_metric(img, x0, x1, list(range(vy - 30, vy - 12)) + list(range(vy + 12, vy + 30)))
                seam_step, around_step = max(near), max(around)
                ok = seam_step - around_step < a.tol
                dr = ImageDraw.Draw(img)
                dr.rectangle([x0, vy - 10, x1, vy + 10], outline=(0, 255, 0) if ok else (255, 0, 0))
                name = "seam-%s-%s.png" % (effect, view)
                img.save(os.path.join(a.out, name))
                line = "%-13s %-6s seam step %5.1f, around it %5.1f -> %s" % (view, effect, seam_step, around_step,
                                                                              "ok" if ok else "SEAM")
                print(line)
                if not ok:
                    fails.append(line)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        sys.exit("%d effect(s) show the seam" % len(fails))
    print("no effect reveals the head/neck seam")


if __name__ == "__main__":
    main()
