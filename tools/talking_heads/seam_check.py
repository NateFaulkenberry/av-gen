#!/usr/bin/env python3
"""Talking Heads: no secondary effect may reveal where a bust's pieces meet (the owner, stop 4: "see if its possible to
eliminate this seam that gets exposed by some of the secondary entity effects"; fx1: "I can still see visible seams
under the chins too").

One head is held still (pose_stills.posed), its routes removed, and each effect is set by hand to full strength:
- rest: nothing;
- wash: the possession wash at its open maximum, plus the flash;
- glow: the entity glow at full on every piece;
- bands (tracking): the static bands open;
- codec (codec): a large share of macroblocks lost to flat smeared colour;
- static (broadcast): the static patches and the scanlines open wide;
- ghosts (tracking, broadcast, ghosting): the copies, opaque and split as on an onset;
- blocks (tracking, codec): the block deformer at full share. It is world space and identical on every piece, so the
  pieces move as one by construction (tests/unit/test_procedural.cpp [adr1674]). Its own block edges are steps
  wherever they fall, so its close-ups are written for the eye and not scored.
The head is rendered alone (the other busts removed: a side camera would see their necks first), close on the
head/neck seam, in four views: front and three-quarter at rest, and JAW-SIDE and JAW-THREE-QUARTER: the mouth fully
open, the neck bent back 32 degrees and the camera below the chin, looking up at the jawline and the under-chin.
- The piece seam: a strip down the neck's middle is averaged across. The largest colour step between neighbouring rows
  within +-10 px of the seam is compared with the same image's largest step 12-30 px above and below it. A seam is a
  step at the boundary that its surroundings do not have.
- The chin (jaw views only; the regression check for fx1's "visible seams under the chins", which was a crease inside
  the head piece where the mouth's hinge mask ended): the colour along the under-chin, from a quarter of the way back from the chin's tip to
  the neck seam (40 samples, 5x5 px each), and its largest step between neighbouring samples. The baseline is the same view with
  the mouth CLOSED ("closed"); the open mouth, alone and under every effect, must not add a step larger than --tol.
Passes when each excess is under --tol (0..255 per channel). Writes each close-up and exits non-zero on a failure.
Every render goes through tools/gpu-lock.sh.

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

EFFECTS = ("rest", "wash", "glow", "bands", "codec", "static", "ghosts", "blocks")
UNSCORED = ("blocks",)
CHIN = [0.0, 0.032, 0.092]   # the chin's tip under the lower lip, in the head's frame (anchors: mouthLip y 0.081)
# view: (camera yaw from the bust's heading, neck tilt, mouth opening, camera height below the aim)
VIEWS = {"front": (0.0, 0.0, 0.0, -0.02), "three-quarter": (-32.0, 0.0, 0.0, -0.02),
         "jaw-side": (-70.0, 32.0, 1.0, 0.10), "jaw-three-quarter": (-32.0, 32.0, 1.0, 0.10)}


def apply_effect(p, s, P, effect):
    """Set one effect to full strength on head P. Returns False when P's profile has no such effect."""
    progs = {g["name"]: g for g in s.get("materialPrograms", [])}
    ops = th.PROGRAM_OPS[P + "-head"].at

    def setop(label, field, value):
        for part in ("body", "head"):
            o = th.PROGRAM_OPS[P + "-" + part]
            progs[P + "-" + part]["ops"][o.at[label] - 1][field] = value

    if effect == "wash":
        setop("washAmount", "constant", [0.0, 1.0, 0.12, 0.9])
    elif effect == "bands":
        if "bands" not in ops:
            return False
        setop("bands", "value", 0.6)
    elif effect == "codec":
        if "cMask" not in ops:
            return False
        setop("cMask", "value", 0.55)
    elif effect == "static":
        if "patch" not in ops:
            return False
        setop("patch", "constant", [0.45, 0.51, 0.0, 0.0])
        setop("slArea", "constant", [0.2, 0.5, 0.0, 0.0])
        setop("slOn", "value", 0.4)
        setop("slCol", "constant", [1.2, 0.7, 0.3, 0.0])
    for e in p["effects"]:
        if effect == "glow" and e["type"] == "glow" and e["id"].startswith(P + "-glow"):
            e["parameters"]["glow"] = 1.0
    found = effect not in ("blocks", "ghosts")
    for n in s["nodes"]:
        if effect == "blocks" and n["name"].startswith(P + "-") and "procedural" in n:
            for d in n["procedural"].get("deformers", []):
                if d["kind"] == "blocks":
                    d["amount"] = 0.04
                    d["falloff"] = 0.6
                    d["phase"] = 3.0
                    found = True
        if effect == "ghosts" and n["name"] in (P + "-ghostAh", P + "-ghostBh"):
            sg = 1.0 if "ghostA" in n["name"] else -1.0
            n["scale"] = [1.004] * 3
            n["position"] = [0.016 * sg, 0.004, 0.0]
            found = True
            for prog in progs.values():
                if prog["name"] == n.get("program"):
                    prog["ops"][3]["constant"] = [0.8, 0.0, 0.0, 0.0]
    return found


def path_metric(img, pts):
    """The largest colour step between neighbouring samples along a screen path (each a 5x5 mean)."""
    px = img.load()
    vals = []
    for u, v in pts:
        acc, n = [0.0, 0.0, 0.0], 0
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                x, y = int(round(u)) + dx, int(round(v)) + dy
                if 0 <= x < img.width and 0 <= y < img.height:
                    c = px[x, y]
                    for k in range(3):
                        acc[k] += c[k]
                    n += 1
        vals.append([a / max(n, 1) for a in acc])
    return max(max(abs(a[k] - b[k]) for k in range(3)) for a, b in zip(vals, vals[1:]))


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
    ap.add_argument("--views", default=",".join(VIEWS))
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
    # (the bust is shown at full size: the scene rests every slot at scale 1 and hides inactive ones by route)
    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="th-seam-")
    fails = []
    try:
        others = tuple(h["prefix"] + "-" for h in th.HEADS if h["prefix"] != P)
        for view in a.views.split(","):
            vyaw, tilt, opening, below = VIEWS[view]
            jaw = view.startswith("jaw")
            closed_step = None
            for effect in (["closed"] if jaw else []) + a.effects.split(","):
                pp, ss = ps.posed(project, scene, P, tilt, 0.0 if effect == "closed" else opening, 0.0,
                                  rays_on=False)
                ss["nodes"] = [n for n in ss["nodes"] if not n["name"].startswith(others)]
                pp["effects"] = [e for e in pp["effects"] if not e["id"].startswith(others)]
                if effect == "closed":
                    effect_ = "rest"
                else:
                    effect_ = effect
                pp["routes"] = [r for r in pp["routes"] if not r["target"].startswith(
                    ("material/%s-" % P, "fx/%s-" % P, "lights/%s-" % P, "particles/%s-" % P))]
                if not apply_effect(pp, ss, P, effect_):
                    continue   # not this head's profile
                nodes = {n["name"]: n for n in ss["nodes"]}
                bust = nodes[P + "-bust"]
                hd = bust.get("rotation", [0.0, 0.0, 0.0])[1]
                # the seam: the head piece's lowest ring, at the pivot (the neck's top), in world space
                seam = ac.apply(ac.node_world(nodes, P + "-pivot"), [0.0, -0.004, 0.0])
                aim = [seam[0], seam[1] + 0.02, seam[2]]
                yaw = hd + vyaw
                cam = [aim[0] + 0.45 * math.sin(math.radians(yaw)), aim[1] - below,
                       aim[2] + 0.45 * math.cos(math.radians(yaw))]
                ss["camera"] = {"mode": 1, "position": cam, "target": aim, "fov": 40.0, "orbitSpeed": 0.0}
                png = ac.render(pp, ss, work, "seam")
                img = Image.open(png).convert("RGB")
                focal = float(pp["parameters"].get("camera/lens/focalLength", 35.0))
                u, v = ac.to_screen(cam, aim, focal, seam)
                # the neck's middle: a strip a quarter of its width either side of the seam's centre line
                front = ac.apply(ac.node_world(nodes, P + "-pivot"), [0.0, -0.004, 0.035])
                fu, _ = ac.to_screen(cam, aim, focal, front)
                cx = int(round(u if view == "front" else fu))
                x0, x1 = cx - 12, cx + 12
                vy = int(round(v))
                near = step_metric(img, x0, x1, range(vy - 10, vy + 11))
                around = step_metric(img, x0, x1, list(range(vy - 30, vy - 12)) + list(range(vy + 12, vy + 30)))
                seam_step, around_step = max(near), max(around)
                scored = effect not in UNSCORED
                ok = seam_step - around_step < a.tol or not scored
                chin_txt = ""
                if jaw:
                    head_w = ac.node_world(nodes, P + "-head")
                    tip = ac.apply(head_w, CHIN)
                    # from a quarter of the way back under the jaw (the tip itself is the silhouette once the jaw
                    # drops) to the neck seam
                    pts = [ac.to_screen(cam, aim, focal,
                                        [tip[k] + (front[k] - tip[k]) * (0.25 + 0.75 * i / 39.0) for k in range(3)])
                           for i in range(40)]
                    chin_step = path_metric(img, pts)
                    if effect == "closed":
                        closed_step = chin_step
                        chin_txt = ", chin path %5.1f (the baseline)" % chin_step
                    else:
                        chin_ok = chin_step - closed_step < a.tol or not scored
                        ok = ok and chin_ok
                        chin_txt = ", chin path %5.1f vs closed %5.1f%s" % (chin_step, closed_step,
                                                                            "" if chin_ok else " CHIN")
                    dr0 = ImageDraw.Draw(img)
                    dr0.line([tuple(map(round, q)) for q in pts], fill=(255, 255, 0), width=1)
                dr = ImageDraw.Draw(img)
                dr.rectangle([x0, vy - 10, x1, vy + 10], outline=(0, 255, 0) if ok else (255, 0, 0))
                name = "seam-%s-%s-%s.png" % (P, effect, view)
                img.save(os.path.join(a.out, name))
                line = "%-17s %-6s seam step %5.1f, around it %5.1f%s -> %s" % (
                    view, effect, seam_step, around_step, chin_txt,
                    ("ok" if ok else "SEAM") if scored else "(not scored)")
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
