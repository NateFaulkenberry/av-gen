#!/usr/bin/env python3
"""Talking Heads: do the beams start where the anchors say? (the owner's stop-1 note: "they look like they come from
below the eyes and mouth").

For each pose (head tilt, mouth angle: the head's Hinge deformer, in degrees), renders the scene twice from a fixed close camera -- every Light Beam on, then
every Light Beam off -- with routes, tracks and motion effects removed, and takes the difference: the pixels the beams
add. Each beam's origin (its owner node's world transform, walked up the scene's parents, times its local offset) is
projected through the same camera, and the check passes when, for every beam, beam pixels lie within `--tol` pixels of
that projected origin and the projected origin is the beam's nearest-to-source end (no beam pixels further back along
the beam's axis than the tolerance). Writes an annotated PNG per pose (the origins as rings) and exits non-zero on a
miss. Every render goes through tools/gpu-lock.sh.

    python3 tools/talking_heads/anchor_check.py --project <talking-heads.json> --out DIR [--poses 0:0,20:0,32:18]
"""
import argparse
import copy
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
AVGEN = os.environ.get("AVGEN") or os.path.join(REPO, "build", "release", "src", "avgen")
LOCK = os.path.join(REPO, "tools", "gpu-lock.sh")
W, H = 1280, 720
PFX = "th"


# ------------------------------------------------------------------------------------------- tiny linear algebra
def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def translate(t):
    return [[1, 0, 0, t[0]], [0, 1, 0, t[1]], [0, 0, 1, t[2]], [0, 0, 0, 1]]


def rot_xyz(deg):
    """Euler degrees applied X, then Y, then Z (R = Rz Ry Rx). The poses here turn about one axis at a time."""
    x, y, z = (math.radians(d) for d in deg)
    rx = [[1, 0, 0, 0], [0, math.cos(x), -math.sin(x), 0], [0, math.sin(x), math.cos(x), 0], [0, 0, 0, 1]]
    ry = [[math.cos(y), 0, math.sin(y), 0], [0, 1, 0, 0], [-math.sin(y), 0, math.cos(y), 0], [0, 0, 0, 1]]
    rz = [[math.cos(z), -math.sin(z), 0, 0], [math.sin(z), math.cos(z), 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    return mat_mul(rz, mat_mul(ry, rx))


def apply(m, p):
    return [sum(m[i][k] * (list(p) + [1.0])[k] for k in range(4)) for i in range(3)]


def node_world(nodes, name):
    n = nodes[name]
    pos = n.get("position", [0, 0, 0])
    rot = n.get("rotation", [0, 0, 0])
    m = mat_mul(translate(pos), rot_xyz(rot))
    if n.get("parent"):
        m = mat_mul(node_world(nodes, n["parent"]), m)
    return m


def to_screen(cam_pos, cam_tgt, focal, p):
    f = [b - a for a, b in zip(cam_pos, cam_tgt)]
    n = math.sqrt(sum(c * c for c in f))
    f = [c / n for c in f]
    r = [-f[2], 0.0, f[0]]
    rn = math.sqrt(sum(c * c for c in r))
    r = [c / rn for c in r]
    u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]
    d = [b - a for a, b in zip(cam_pos, p)]
    x, y, z = (sum(a * b for a, b in zip(d, v)) for v in (r, u, f))
    fy = focal / 12.0
    fx = fy / (W / H)
    return ((0.5 + 0.5 * fx * x / z) * W, (0.5 - 0.5 * fy * y / z) * H)


# ------------------------------------------------------------------------------------------- the check
def pose_docs(project, scene, tilt, jaw, cam_pos, cam_tgt, beams_on):
    p, s = copy.deepcopy(project), copy.deepcopy(scene)
    p["routes"] = []
    p.setdefault("timeline", {})["tracks"] = []
    p["sources"] = [x for x in p.get("sources", []) if x.get("kind") not in ("interpret",)]
    p["effects"] = [e for e in p.get("effects", []) if e["type"] not in ("shake", "float", "glow")]
    for e in p["effects"]:
        if e["type"] == "lightBeam":
            e["parameters"]["intensity"] = 2.0 if beams_on else 0.0
            e["parameters"]["length"] = 0.5
            e["parameters"]["dust"] = 0.0
    p["parameters"]["temporal/echo/enabled"] = False
    p["parameters"]["post/bloom/enabled"] = False
    p.pop("control", None)
    s["camera"] = {"mode": 1, "position": cam_pos, "target": cam_tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s["nodes"] = [n for n in s["nodes"] if n["kind"] != "particles" and not n["name"].startswith("th-ghost")]
    for n in s["nodes"]:
        if n["name"] == PFX + "-pivot":   # the head hangs from the pivot at the neck's top (stop 4)
            n["rotation"] = [-tilt, 0.0, 0.0]
            if tilt:   # onto the bent neck's top, as the scene's routes place it (applyBend: R = F / theta)
                F = next(d["falloff"] for m in s["nodes"] if m["name"] == PFX + "-neck"
                         for d in m["procedural"]["deformers"] if d["kind"] == "bend")
                th = math.radians(tilt)
                R_ = F / th
                n["position"] = [n["position"][0], n["position"][1] + R_ * math.sin(th) - F,
                                 n["position"][2] - R_ * (1.0 - math.cos(th))]
        if n["name"] == PFX + "-head":   # the mouth: the head's Hinge deformer (ADR-1672), `jaw` degrees
            for d in n["procedural"].get("deformers", []):
                if d["kind"] == "hinge":
                    d["amount"] = math.radians(jaw)
        if n["name"] == PFX + "-neck" and tilt:
            for d in n["procedural"].get("deformers", []):
                if d["kind"] == "bend" and d.get("displacementAxis", [0, 0, -1])[2] < 0:   # the back bend
                    d["amount"] = math.radians(tilt) / d["falloff"]
    return p, s


def render(p, s, work, tag):
    pj, sj = os.path.join(work, tag + ".json"), os.path.join(work, tag + ".scene.json")
    p = copy.deepcopy(p)
    p["assets"]["scene"] = {"kind": "composition", "path": sj}
    json.dump(p, open(pj, "w"))
    json.dump(s, open(sj, "w"))
    d = os.path.join(work, tag)
    r = subprocess.run([LOCK, AVGEN, "--headless", "--project", pj, "--render", d, "--format", "png", "--range",
                        "0.5:0.51", "--size", "%dx%d" % (W, H), "--fps", "30"], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        sys.exit("avgen exited %d" % r.returncode)
    return os.path.join(d, sorted(os.listdir(d))[-1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--poses", default="0:0,20:0,32:18", help="tilt:mouth degrees, comma-separated")
    ap.add_argument("--tol", type=float, default=12.0, help="pixels (the origin sits inside the mouth, so the lip can hide its first few)")
    ap.add_argument("--view", default="three-quarter", choices=["front", "three-quarter"])
    ap.add_argument("--prefix", default="th", help="the head to pose and look at (th, th1, th2...)")
    a = ap.parse_args()
    from PIL import Image, ImageChops, ImageDraw
    base = os.path.dirname(os.path.abspath(a.project))
    project = json.load(open(a.project))
    sp = project["assets"]["scene"]["path"]
    scene = json.load(open(sp if os.path.isabs(sp) else os.path.join(base, sp)))
    # make the scene's relative asset paths absolute for the temporary copies
    for n in scene["nodes"]:
        src = n.get("procedural", {}).get("source", {})
        if "asset" in src and not os.path.isabs(src["asset"]):
            src["asset"] = os.path.join(base, src["asset"])
        if "asset" in n and not os.path.isabs(n["asset"]):
            n["asset"] = os.path.join(base, n["asset"])
    focal = float(project["parameters"].get("camera/lens/focalLength", 35.0))
    # The cause of the stop-1 misalignment: the head was tilted by a route on `procedural/<node>/transform/rotation`,
    # which turns the DRAWN mesh (the procedural's own transform) but not the node, and the beams ride the node. Any
    # such route on a beam's owner or one of its ancestors is refused here.
    nodes0 = {n["name"]: n for n in scene["nodes"]}
    chain = set()
    for e in project.get("effects", []):
        if e["type"] == "lightBeam" and e["owner"].get("kind") == "entity":
            n = e["owner"]["name"]
            while n:
                chain.add(n)
                n = nodes0.get(n, {}).get("parent")
    bad = [r["target"] for r in project.get("routes", []) + project.get("timeline", {}).get("tracks", [])
           for n in chain if r["target"].startswith("procedural/%s/transform" % n)]
    if bad:
        print("routes move a beam owner's drawn mesh without its node (use nodes/<name>/...):", sorted(set(bad)))
        sys.exit(1)
    global PFX
    PFX = a.prefix
    head = next(n for n in scene["nodes"] if n["name"] == PFX + "-pivot")
    bust = next(n for n in scene["nodes"] if n["name"] == PFX + "-bust")
    bp = bust.get("position", [0.0, 0.0, 0.0])
    aim = [bp[0] + head["position"][0], bp[1] + head["position"][1] + 0.12, bp[2] + head["position"][2] + 0.06]
    yaw = 0.0 if a.view == "front" else -32.0
    dist = 0.75
    cam = [aim[0] + dist * math.sin(math.radians(yaw)), aim[1] + 0.02, aim[2] + dist * math.cos(math.radians(yaw))]
    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="th-anchor-")
    failures = []
    try:
        for pose in a.poses.split(","):
            tilt, jaw = (float(v) for v in pose.split(":"))
            on_p, on_s = pose_docs(project, scene, tilt, jaw, cam, aim, True)
            off_p, off_s = pose_docs(project, scene, tilt, jaw, cam, aim, False)
            on = Image.open(render(on_p, on_s, work, "on")).convert("RGB")
            off = Image.open(render(off_p, off_s, work, "off")).convert("RGB")
            diff = ImageChops.difference(on, off).convert("L")
            px = diff.load()
            beam_px = [(x, y) for y in range(H) for x in range(W) if px[x, y] > 24]
            nodes = {n["name"]: n for n in on_s["nodes"]}
            img = on.copy()
            dr = ImageDraw.Draw(img)
            for e in on_p["effects"]:
                if e["type"] != "lightBeam" or not e["owner"].get("name", "").startswith(PFX + "-"):
                    continue
                q = e["parameters"]
                owner = e["owner"]["name"]
                m = node_world(nodes, owner)
                origin = apply(m, [q["offsetX"], q["offsetY"], q["offsetZ"]])
                u, v = to_screen(cam, aim, focal, origin)
                near = min((math.hypot(x - u, y - v) for x, y in beam_px), default=1e9)
                ok = near <= a.tol
                dr.ellipse([u - 6, v - 6, u + 6, v + 6], outline=(0, 255, 0) if ok else (255, 0, 0), width=2)
                line = "pose %s %s: origin (%.0f, %.0f) px, nearest beam pixel %.1f px -> %s" % (
                    pose, e["id"], u, v, near, "ok" if ok else "MISS")
                print(line)
                if not ok:
                    failures.append(line)
            img.save(os.path.join(a.out, "anchor-check-%s-tilt%02d-mouth%02d.png" % (a.view, int(tilt), int(jaw))))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if failures:
        print("%d beam origin(s) off their anchors" % len(failures))
        sys.exit(1)
    print("every beam starts at its anchor")


if __name__ == "__main__":
    main()
