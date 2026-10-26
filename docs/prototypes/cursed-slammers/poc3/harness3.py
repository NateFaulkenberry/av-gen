"""Cursed Slammers POC 3: the POC 2 harness with opposing layers. The face (muzzle-deepened, cs-face-p3.glb) with its
back volume and two separate eyeballs sits on the key plane; a NEAR layer (two thin polished bars) in front of it and
the FAR mottled backdrop well behind it move in opposite directions as the viewpoint sweeps. No material program on
the face. Writes kit projects only; nothing is registered.

    python3 harness3.py --assets assets/cursed-slammers --out <dir> --name p3 --mode front|sweep|rail
        [--views 24] [--span 11] [--period 8] [--no-near] [--no-eyes] [--no-back]

Geometry (world units = face widths): camera rail at z = +2.2, key plane z = 0, near bars z = +0.40, far backdrop
z = -3.2. Every camera aims at the key plane's centre.
"""
import argparse
import json
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402

DIST = 2.2
NEAR_Z = 0.40
FAR_Z = -3.2
TARGET = [0.0, 0.0, 0.0]
LENS = 50.0
BARS_X = [-0.40, 0.40]


def build(name, a, out, cam_keys=None, cam_x=0.0):
    A = os.path.abspath(a.assets)
    info = json.load(open(os.path.join(A, "poc3_assets.json")))
    s = kit.Scene(name, "Cursed Slammers POC3 " + name, {"thesis": "optical depth harness, opposing layers"})
    s.environment = {"intensity": 0.02, "background": [0.006, 0.006, 0.007], "fogColor": [0.0, 0.0, 0.0],
                     "volumeDensity": 0.0, "skyIntensity": 1.0,
                     "sky": {"enabled": True, "zenithColor": [0.006, 0.006, 0.007],
                             "horizonColor": [0.008, 0.008, 0.009], "groundColor": [0.004, 0.004, 0.005],
                             "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True,
                             "useKeyLight": False}}
    s.params_({"camera/lens/focalLength": LENS, "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.2,
               "post/output/vignette": 0.25, "post/output/grain": 0.0})
    s.light("key", "spot", position=[-1.3, 1.3, 1.9], direction=[0.5, -0.45, -0.74], color=[1.0, 0.95, 0.9],
            intensity=22.0, range=9.0, innerCone=18.0, outerCone=34.0, castsShadow=False, contactShadow=False)
    s.light("fill", "point", position=[1.6, -0.4, 1.8], color=[0.85, 0.88, 1.0], intensity=1.6, range=8.0,
            castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.3, -0.6, 0.74], color=[0.9, 0.9, 1.0], intensity=0.9,
            castsShadow=False, contactShadow=False)
    # a small light just above the lens: the eyes' catchlight, and a hair-line highlight down each bar
    s.light("catch", "point", position=[0.0, 0.45, DIST - 0.1], color=[0.95, 0.96, 1.0], intensity=0.5, range=4.0,
            castsShadow=False, contactShadow=False)
    # far: the mottled backdrop, deeper and larger (its mottle scaled with it, so it reads the same)
    s.program({"name": "bg", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.8, "seed": 3},
        {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.2, 0.8, 0.45, 1.6], "value": 1.0},
        {"kind": "constant", "dst": 2, "constant": [0.03, 0.03, 0.034, 1.0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 1}],
        "baseColor": 2, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    s.proc("backdrop", {"kind": "box", "size": [20.0, 12.0, 0.05]},
           material={"baseColor": [0.03, 0.03, 0.034], "roughness": 0.9, "metallic": 0.0, "emissiveColor": [0, 0, 0],
                     "emissiveIntensity": 0.0, "program": "bg"},
           transform={"position": [0.0, 0.0, FAR_Z], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    # key plane: the face, its back volume, its eyes
    def node(nm, glb, pos):
        s.nodes.append({"name": nm, "kind": "gltf", "asset": os.path.join(A, glb), "position": [float(c) for c in pos],
                        "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "castsShadow": False})
        s.entity(nm)
    node("face", "cs-face-p3.glb", (0, 0, 0))
    if not a.no_back:
        node("back", "cs-back.glb", (0, 0, 0))
    if not a.no_eyes:
        for k, e in enumerate(info["eyesGltf"]):
            node("eye%d" % k, "cs-eye.glb", e)
    # near: two thin polished bars
    if not a.no_near:
        s.proc("bars", {"kind": "cylinder", "radius": 0.014, "height": 4.0, "radialSegments": 24, "caps": False},
               distribution={"kind": "linear", "count": len(BARS_X), "start": [BARS_X[0], 0.0, NEAR_Z],
                             "end": [BARS_X[-1], 0.0, NEAR_Z]},
               material={"baseColor": [0.55, 0.55, 0.58], "roughness": 0.12, "metallic": 1.0,
                         "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0})
    if cam_keys:
        s.track("camera/position", cam_keys[0], loop=cam_keys[1])
        s.track("camera/target", [{"time": 0.0, "value": TARGET, "interp": "linear"},
                                  {"time": cam_keys[1], "value": TARGET, "interp": "linear"}], loop=cam_keys[1])
    s.camera = {"mode": 1, "position": [cam_x, 0.0, DIST], "target": TARGET, "fov": 27.0, "orbitSpeed": 0.0}
    s.write(out)
    return os.path.join(out, name + ".json")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--mode", choices=["front", "sweep", "rail"], required=True)
    ap.add_argument("--span", type=float, default=11.0)
    ap.add_argument("--views", type=int, default=24)
    ap.add_argument("--period", type=float, default=8.0)
    ap.add_argument("--no-near", action="store_true")
    ap.add_argument("--no-eyes", action="store_true")
    ap.add_argument("--no-back", action="store_true")
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    X = DIST * math.tan(math.radians(a.span))
    if a.mode == "front":
        print(build(a.name, a, out))
    elif a.mode == "sweep":
        keys = [{"time": round(a.period * i / 64, 4),
                 "value": [round(-X * math.cos(2 * math.pi * i / 64), 5), 0.0, DIST], "interp": "smooth"}
                for i in range(65)]
        print(build(a.name, a, out, cam_keys=(keys, a.period)))
    else:
        for i in range(a.views):
            ang = -a.span + 2 * a.span * i / (a.views - 1)
            print(build("%s-v%02d" % (a.name, i), a, out, cam_x=DIST * math.tan(math.radians(ang))))


if __name__ == "__main__":
    main()
