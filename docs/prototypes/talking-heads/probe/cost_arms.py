"""Throwaway cost probe for Talking Heads: cumulative arms, each a project written next to this script.

Copy it to a scratch folder with talking_head_bust_probe.glb and bust_anchors.json (make_bust.py's outputs), run it,
then bench each arm under tools/gpu-lock.sh:
    avgen --headless --project A4.json --range 1.0: --frames 360 --bench-json A4.1.json --size 1920x1080
Results and reading: 02-research-engine.md section 4.

A0 empty dark ether + bloom | A1 +bust | A2 +16 beams (light-owned riding the bust) | A3 +2 ghosts | A4 +glitch tear/split
| A5 +frame echo | A6 = A4 with 24 beams | B2 = A1 + 16 WORLD-owned beams (to compare owners)
"""
import copy
import json
import math
import os
import sys

REPO = "/Users/natefaulkenberry/Documents/GitHub/av-gen-talkingheads"
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
GLB = "talking_head_bust_probe.glb"
AN = json.load(open(os.path.join(HERE, "bust_anchors.json")))["gltf"]
S = 1.0  # bust scale


def base(sid):
    s = kit.Scene(sid, "TH probe " + sid, {"thesis": "cost probe"})
    s.environment = {"intensity": 0.05, "background": [0.002, 0.0012, 0.006], "fogColor": [0.006, 0.004, 0.016],
                     "volumeDensity": 0.004, "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
                     "sky": {"enabled": True, "zenithColor": [0.004, 0.002, 0.012], "horizonColor": [0.010, 0.005, 0.03],
                             "groundColor": [0.002, 0.0012, 0.006], "haze": 0.2, "sunIntensity": 0.0, "intensity": 1.0,
                             "background": True, "useKeyLight": False}}
    s.camera = {"mode": 1, "position": [0.55, 0.47, 1.95], "target": [0.0, 0.36, 0.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.45})
    s.unshadowed_key()
    s.light("rim", "directional", direction=[-0.5, -0.3, 0.8], color=[0.6, 0.4, 1.0], intensity=2.0,
            castsShadow=False, contactShadow=False)
    return s


def bust(s):
    s.nodes.append({"name": "bust", "kind": "gltf", "asset": GLB, "position": [0, 0, 0], "rotation": [0, 0, 0],
                    "scale": [S, S, S], "castsShadow": False})
    s.entity("bust")


def beams(s, n_mouth=12, n_eye=4, owner="light"):
    m = AN["mouth"]
    eyes = [AN["eyeLeft"], AN["eyeRight"]]
    k = 0
    for i in range(n_mouth):
        a = -40 + 80 * i / max(1, n_mouth - 1)
        d = [math.sin(math.radians(a)), 0.25 + 0.15 * math.cos(i), math.cos(math.radians(a))]
        add_beam(s, "m%d" % k, m, d, angle=1.2 + (5.0 if i == n_mouth // 2 else 0.0), length=1.6, owner=owner,
                 color=[1.0, 0.3, 0.9])
        k += 1
    for j in range(n_eye):
        e = eyes[j % 2]
        d = [0.15 * (1 if j % 2 == 0 else -1), 0.05 * j, 1.0]
        add_beam(s, "e%d" % j, e, d, angle=0.7, length=1.2, owner=owner, color=[0.4, 0.6, 1.0])


def add_beam(s, bid, at, d, angle, length, owner, color):
    n = math.sqrt(sum(c * c for c in d))
    d = [c / n for c in d]
    if owner == "light":
        s.light("L" + bid, "spot", position=at, direction=d, color=color, intensity=0.01, range=length,
                innerCone=angle * 0.5, outerCone=angle, castsShadow=False,
                contactShadow=False, node="bust")
        s.effect("fx" + bid, "lightBeam", ("light", "L" + bid),
                 parameters={"intensity": 1.2, "aperture": 0.004, "dust": 0.3, "coreSharpness": 4.0,
                             "edgeSoftness": 0.4, "falloff": 1.5, "castLight": False, "steps": 8})
    else:
        tilt = math.degrees(math.acos(max(-1.0, min(1.0, -d[1]))))
        tilt = min(tilt, 89.0)
        yaw = math.degrees(math.atan2(d[0], d[2]))
        s.beam("fx" + bid, at, tilt=tilt, yaw=yaw, color=color, intensity=1.2, length=length, angle=angle,
               aperture=0.004, dust=0.3, light=0.0, coreSharpness=4.0, edgeSoftness=0.4, falloff=1.5, steps=8)


def ghosts(s):
    for name, off in (("ghostA", [0.025, 0.004, 0.0]), ("ghostB", [-0.025, -0.004, 0.0])):
        s.nodes.append({"name": name, "kind": "gltf", "asset": GLB, "position": off, "rotation": [0, 0, 0],
                        "scale": [1, 1, 1], "castsShadow": False, "parent": "bust", "emissiveBoost": 2.0})


def glitch(s):
    s.params_({"post/glitch/tear": 0.25, "post/glitch/tearShift": 40.0, "post/split/amount": 6.0})


def echo(s):
    s.params_({"temporal/echo/enabled": True, "temporal/echo/frames": 4.0, "temporal/echo/strength": 0.4})


def build(sid, steps):
    s = base(sid)
    for st in steps:
        st(s)
    s.write(HERE)
    print(sid, len(s.nodes), "nodes", len(s.effects), "effects", len(s.lights), "lights")


build("A0", [])
build("A1", [bust])
build("A2", [bust, beams])
build("A3", [bust, beams, ghosts])
build("A4", [bust, beams, ghosts, glitch])
build("A5", [bust, beams, ghosts, glitch, echo])
build("A6", [bust, lambda s: beams(s, 20, 4), ghosts, glitch])
build("B2", [bust, lambda s: beams(s, owner="world")])
