"""Cursed Slammers POC 2: the isolated optical-depth harness. One face, a dark neutral backdrop, simple controlled
lights, NO material program on the face (nothing can paint stripes on it). Writes kit projects only; nothing is
registered, no production file is touched.

    python3 harness.py --face <glb> --out <dir> --name <id> --mode front|view|sweep [--x 0.1] [--period 8]
    python3 harness.py --face <glb> --out <dir> --name <id> --mode rail --views 8 --span 6

Modes:
  front   the fixed frontal reference camera (x = 0)
  view    one fixed camera at horizontal offset --x on the rail, aimed at the key plane (the toggle for a hand test)
  sweep   the camera moves along the rail, x = -X..+X..-X once per --period (the parallax-only stage)
  rail    N fixed cameras evenly spaced across +-span degrees, converged on the key plane: the N views of a
          lenticular capture rig (one project per view, <name>-v00 .. -v(N-1))

Geometry: the face's key plane is z = 0 (depth_variants.py recentres every variant on it), the backdrop at
z = BACK_Z. The rail is at distance DIST from the key plane. All cameras aim at the key plane's centre, so features in
front of it move against the camera and the backdrop moves with it.
"""
import argparse
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402

DIST = 2.2
BACK_Z = -1.6
TARGET = [0.0, 0.0, 0.0]
LENS = 50.0


def build(name, face, out, cam_keys=None, cam_x=0.0):
    s = kit.Scene(name, "Cursed Slammers POC2 " + name, {"thesis": "optical depth harness"})
    s.environment = {"intensity": 0.02, "background": [0.006, 0.006, 0.007], "fogColor": [0.0, 0.0, 0.0],
                     "volumeDensity": 0.0, "skyIntensity": 1.0,
                     "sky": {"enabled": True, "zenithColor": [0.006, 0.006, 0.007],
                             "horizonColor": [0.008, 0.008, 0.009], "groundColor": [0.004, 0.004, 0.005],
                             "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True,
                             "useKeyLight": False}}
    s.params_({"camera/lens/focalLength": LENS, "post/bloom/intensity": 0.15, "post/bloom/threshold": 1.5,
               "post/output/vignette": 0.25, "post/output/grain": 0.0})
    # key: upper left, grazing enough to model the brow, sockets and muzzle; fill: weak, low right; rim: behind above
    s.light("key", "spot", position=[-1.3, 1.3, 1.9], direction=[0.5, -0.45, -0.74], color=[1.0, 0.95, 0.9],
            intensity=22.0, range=9.0, innerCone=18.0, outerCone=34.0, castsShadow=False, contactShadow=False)
    s.light("fill", "point", position=[1.6, -0.4, 1.8], color=[0.85, 0.88, 1.0], intensity=1.6, range=8.0,
            castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.3, -0.6, 0.74], color=[0.9, 0.9, 1.0], intensity=0.9,
            castsShadow=False, contactShadow=False)
    # the backdrop: dark neutral, a very soft large mottle so its parallax against the face is visible
    s.program({"name": "bg", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 0, "value": 1.4, "seed": 3},
        {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.2, 0.8, 0.5, 1.5], "value": 1.0},
        {"kind": "constant", "dst": 2, "constant": [0.03, 0.03, 0.034, 1.0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 1}],
        "baseColor": 2, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    s.proc("backdrop", {"kind": "box", "size": [12.0, 8.0, 0.05]},
           material={"baseColor": [0.03, 0.03, 0.034], "roughness": 0.9, "metallic": 0.0, "emissiveColor": [0, 0, 0],
                     "emissiveIntensity": 0.0, "program": "bg"},
           transform={"position": [0.0, 0.0, BACK_Z], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.nodes.append({"name": "face", "kind": "gltf", "asset": os.path.abspath(face), "position": [0.0, 0.0, 0.0],
                    "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "castsShadow": False})
    s.entity("face")
    if cam_keys:
        s.track("camera/position", cam_keys[0], loop=cam_keys[1])
        s.track("camera/target", [{"time": 0.0, "value": TARGET, "interp": "linear"},
                                  {"time": cam_keys[1], "value": TARGET, "interp": "linear"}], loop=cam_keys[1])
    s.camera = {"mode": 1, "position": [cam_x, 0.0, DIST], "target": TARGET, "fov": 27.0, "orbitSpeed": 0.0}
    s.write(out)
    return os.path.join(out, name + ".json")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--face", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--mode", choices=["front", "view", "sweep", "rail"], required=True)
    ap.add_argument("--x", type=float, default=0.0)
    ap.add_argument("--span", type=float, default=6.0, help="half-angle of the rail, degrees")
    ap.add_argument("--views", type=int, default=8)
    ap.add_argument("--period", type=float, default=8.0)
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    X = DIST * math.tan(math.radians(a.span))
    if a.mode == "front":
        print(build(a.name, a.face, out))
    elif a.mode == "view":
        print(build(a.name, a.face, out, cam_x=a.x))
    elif a.mode == "sweep":
        keys = []
        n = 64
        for i in range(n + 1):
            ph = i / n
            keys.append({"time": round(a.period * ph, 4),
                         "value": [round(-X * math.cos(2 * math.pi * ph), 5), 0.0, DIST], "interp": "smooth"})
        print(build(a.name, a.face, out, cam_keys=(keys, a.period)))
    else:
        for i in range(a.views):
            ang = -a.span + 2 * a.span * i / (a.views - 1)
            print(build("%s-v%02d" % (a.name, i), a.face, out, cam_x=DIST * math.tan(math.radians(ang))))


if __name__ == "__main__":
    main()
