#!/usr/bin/env python3
"""Talking Heads: one head held in a static pose, close up, with the scene's own look (the owner's stop-4 mouth
review: closed, half and fully open, front and three-quarter, at a tilt of 0 and 32 degrees).

The project is copied with the posed head's pose, scan, eye, lid and ray routes removed. The pose is written straight
into the scene:
- the neck's bend and the head's placement on it (as anchor_check does);
- the mouth's Hinge angle;
- the rays at the brightness the scene gives that opening (RAY_BRIGHTNESS x weight x opening / MOUTH_OPEN).
The eyes look ahead and the lids are open unless --lids is given. Every render goes through tools/gpu-lock.sh.

    python3 tools/talking_heads/pose_stills.py --project <talking-heads.json> --out DIR \
        [--poses 0:0,0:0.5,0:1,32:0,32:0.5,32:1] [--views front,three-quarter] [--prefix th2] [--size 1920x1080] \
        [--no-rays] [--lids 0..1] [--tag mouth]

A pose is tilt (degrees, back +, forward -):opening (0 = shut, 1 = MOUTH_OPEN)[:yaw (degrees at the neck)].
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
from tools.sonic_vfx.scenes import talking_heads as th  # noqa: E402


def posed(project, scene, P, tilt, opening, lids, rays_on=True, yaw=0.0):
    p, s = copy.deepcopy(project), copy.deepcopy(scene)
    mine = ("procedural/%s-" % P, "nodes/%s-" % P, "fx/%s-mouth" % P)
    p["routes"] = [r for r in p.get("routes", []) if not r["target"].startswith(mine)]
    p.setdefault("timeline", {})["tracks"] = [t for t in p["timeline"].get("tracks", [])
                                              if not t["target"].startswith(mine + ("camera/",))]
    p.pop("control", None)
    rays = sorted((e for e in p["effects"] if e["type"] == "lightBeam" and e["id"].startswith(P + "-mouth")),
                  key=lambda e: int(e["id"][len(P + "-mouth"):]))
    for i, e in enumerate(rays):
        e["parameters"]["intensity"] = th.RAY_BRIGHTNESS * th.RAY_WEIGHT[i] * opening * (1.0 if rays_on else 0.0)
    for n in s["nodes"]:
        if n["name"] == P + "-pivot":
            n["rotation"] = [-tilt, 0.0, 0.0]
            if tilt:
                F = th.BEND_LEN
                t = math.radians(abs(tilt))
                R_ = F / t
                sg = 1.0 if tilt > 0 else -1.0   # forward: the head's top moves toward the face
                n["position"] = [n["position"][0], n["position"][1] + R_ * math.sin(t) - F,
                                 n["position"][2] - sg * R_ * (1.0 - math.cos(t))]
        if n["name"] == P + "-head":
            n["rotation"] = [0.0, yaw, 0.0]
            for d in n["procedural"]["deformers"]:
                if d["kind"] == "hinge":
                    d["amount"] = th.MOUTH_OPEN * opening
        if n["name"] == P + "-neck":
            for d in n["procedural"]["deformers"]:
                if d["kind"] == "twist":   # the swivel's yaw, as the scene's routes drive it
                    d["amount"] = math.radians(yaw) / th.BEND_LEN
                # tilt > 0 bends back (the back bend), < 0 forward (the forward bend)
                if d["kind"] == "bend" and tilt and (d["displacementAxis"][2] < 0) == (tilt > 0):
                    d["amount"] = math.radians(abs(tilt)) / d["falloff"]
        if lids and n["name"] in (P + "-lidUL", P + "-lidUR"):
            n["rotation"] = [th.LID_UPPER_CLOSED * lids, 0.0, 0.0]
        if lids and n["name"] in (P + "-lidLL", P + "-lidLR"):
            n["rotation"] = [-th.LID_LOWER_CLOSED * lids, 0.0, 0.0]
    return p, s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--poses", default="0:0,0:0.5,0:1,32:0,32:0.5,32:1")
    ap.add_argument("--views", default="front,three-quarter")
    ap.add_argument("--prefix", default="th2")
    ap.add_argument("--lids", type=float, default=0.0, help="0 open .. 1 shut")
    ap.add_argument("--dist", type=float, default=0.55)
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--tag", default="mouth")
    ap.add_argument("--no-rays", action="store_true", help="the rays off: a close camera sits inside their cones")
    a = ap.parse_args()
    ac.W, ac.H = (int(v) for v in a.size.split("x"))
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
    bust = next(n for n in scene["nodes"] if n["name"] == P + "-bust")
    bp, hd = bust.get("position", [0.0, 0.0, 0.0]), bust.get("rotation", [0.0, 0.0, 0.0])[1]
    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="th-pose-")
    try:
        for pose in a.poses.split(","):
            vals = [float(v) for v in pose.split(":")]
            tilt, opening, nyaw = (vals + [0.0])[:3]
            for view in a.views.split(","):
                # aim at the mouth of the posed head, in world space; look from the head's front (+ its heading)
                pp, ss = posed(project, scene, P, tilt, opening, a.lids, not a.no_rays, nyaw)
                nodes = {n["name"]: n for n in ss["nodes"]}
                mouth = ac.apply(ac.node_world(nodes, P + "-head"), th.MOUTH)
                aim = [mouth[0], mouth[1] + 0.03, mouth[2]]
                yaw = hd + (0.0 if view == "front" else -32.0)
                cam = [aim[0] + a.dist * math.sin(math.radians(yaw)), aim[1] + 0.03,
                       aim[2] + a.dist * math.cos(math.radians(yaw))]
                ss["camera"] = {"mode": 1, "position": cam, "target": aim, "fov": 40.0, "orbitSpeed": 0.0}
                png = ac.render(pp, ss, work, "pose")
                name = "%s-%s-tilt%02d-open%03d%s.png" % (a.tag, view, int(tilt), int(round(opening * 100)),
                                                         "-yaw%d" % int(nyaw) if nyaw else "")
                shutil.copy(png, os.path.join(a.out, name))
                print(os.path.join(a.out, name))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
