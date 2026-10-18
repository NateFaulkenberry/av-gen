#!/usr/bin/env python3
"""PHASE WALL, stage 0: the cheap proof renders behind docs/prototypes/phase-wall/02-plan.md.

Not the scene. A throwaway generator that settles what the debate (01-research.md section 3) could not settle on
paper, writing variant projects to a directory outside the repository:

    python3 tools/phase_wall/proof.py --out <dir> [--flap square|blade] [--back lit|dark] [--pitch 1.2] [--hall]

The one structural idea it proves: a closed hall whose four walls read ONE wrapped oscillator network through four
grid readers, each reader's frame turned so its wall's unrolled coordinate lands on its own stretch of the grid.
The waves cross the corners and circle the room; four walls cost 8 of the 16 GPU field slots.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
from tools.generative_rd import common  # noqa: E402
from tools.generative_rd.common import R, hexrgb  # noqa: E402


def euler_deg(d):
    """Field/node rotation (euler XYZ degrees, glm: R = Rz Ry Rx) taking local x to the wall direction d, local z to
    world up and local y to up x d: Ry(psi) Rx(-90)."""
    psi = math.degrees(math.atan2(-d[2], d[0]))
    return [-90.0, psi, 0.0]


def hall_walls(length, width):
    """The four walls of a hall centred on the origin, long axis along z, walked anticlockwise from the front-left
    corner: (name, start, direction, length, unrolled start). Each wall's inward normal is (-dz, 0, dx)."""
    a, b = (-width / 2, length / 2), (-width / 2, -length / 2)
    c, d = (width / 2, -length / 2), (width / 2, length / 2)
    out, u = [], 0.0
    for name, s, e in (("west", a, b), ("north", b, c), ("east", c, d), ("south", d, a)):
        dx, dz = e[0] - s[0], e[1] - s[1]
        n = math.hypot(dx, dz)
        out.append((name, (s[0], s[1]), (dx / n, 0.0, dz / n), n, u))
        u += n
    return out, u


PAL = {
    "metal": "#b08a5a", "back": "#0d2a33", "backGain": 0.5, "front": "#5ff2e0", "frontGain": 2.2,
    "floor": "#07090b", "ceiling": "#06080a", "pier": "#0b0d10", "fog": "#0a1a1e", "key": "#ffe2c0",
}


def C(k, s=1.0):
    return hexrgb(PAL[k], s)


def build(a):
    s = common.Scene("phase-wall-proof", "Phase Wall proof", {"category": "experiment", "thesis": "proof"})
    L, W, H, p = a.length, a.width, a.height, a.pitch
    walls, P = hall_walls(L, W)
    if not a.hall:  # the R&D baseline for comparison: one wall, the north one
        walls = [w for w in walls if w[0] == "north"]
    rows = int(H // p)
    # the network: one strip, P long and H tall, wrapped so the waves cross every corner and circle the hall
    cells = 1.0 / a.cell
    g = {"name": "net", "mode": "oscillator", "wrap": "wrap",
         "resolution": [int(round(P * cells)), 1, int(round(H * cells))],
         "boundsMin": [-P / 2, -40.0, -H / 2], "boundsMax": [P / 2, 40.0, H / 2],
         "frequency": 0.18, "simRate": 60.0, "maxSubSteps": 4, "seed": 3, "persistence": 0.6, "links": 0,
         "linkRange": 48.0, "checkpointInterval": 5.0, "coupling": 4.0, "phaseLag": 0.3, "couplingRadius": 4,
         "spread": 0.15, "seedAmount": 1.0, "noise": 0.05}
    s.grid(**g)
    # a periodic tilt (one sine round the hall): unison still rolls as one long slow wave, with no seam at the wrap
    s.field("tilt", {"kind": "wave", "waveGeometry": "planar", "waveShape": "sine", "axis": [1.0, 0.0, 0.0],
                     "point": [0.0, 0.0, 0.0], "wavelength": P, "waveSpeed": 0.0, "waveWidth": 0.0,
                     "amplitude": 1.0, "strength": 0.02})
    g["frequencyField"] = "tilt"
    # the flap: one program for every wall; per-flap values arrive through the instance emissive (activity)
    pr = common.Prog("flap")
    pr.op(None, "input", 0, inp="instanceEmissive")
    pr.op(None, "smoothstep", 1, a=0, k=[0.8, 3.0, 0, 0])
    pr.op("front", "constant", 2, k=C("front", PAL["frontGain"]) + [1])
    pr.op(None, "multiply", 3, a=1, b=2)
    pr.op("metal", "constant", 4, k=C("metal") + [1])
    pr.op(None, "constant", 5, k=[a.rough] * 3 + [1])
    pr.op(None, "roughnessFilter", 5, a=5, f=1.0)
    s.program(pr.doc(base=4, emission=3, roughness=5))
    if a.flap == "blade":
        sx, sy = 0.75 * p, 1.6 * p
        size = [0.9 * sx, 0.94 * sy, 0.04]
    else:
        size, sx, sy = [0.88 * p, 0.88 * p, 0.035], p, p
    for name, st, d, n, u0 in walls:
        inset = 1.6  # the corner piers
        nx = int((n - 2 * inset) // sx)
        ny = int((H - 1.0) // sy)
        span = nx * sx
        mid = n / 2
        cx, cz = st[0] + d[0] * mid, st[1] + d[2] * mid
        nrm = (-d[2], 0.0, d[0])
        yc = 0.5 + ny * sy / 2
        # this wall's readers: local x along the wall (unrolled u - P/2), local z up (y - H/2)
        T = [st[0] - d[0] * (u0 - P / 2), H / 2, st[1] - d[2] * (u0 - P / 2)]
        for ch, fn in ((1, "ph"), (3, "act")):
            s.field(fn + name, {"kind": "grid", "reference": "net", "channel": ch, "strength": 1.0,
                                "rotation": euler_deg(d)}, position=T)
        yaw = math.degrees(math.atan2(-d[2], d[0]))
        s.proc("flaps" + name, {"kind": "box", "size": size},
               distribution={"kind": "grid", "gridCount": [nx, ny, 1], "gridSpacing": [sx, sy, 1.0]},
               position=(cx, yc, cz), rotation=(0.0, yaw, 0.0),
               material={"program": "flap", "baseColor": [1, 1, 1], "roughness": a.rough, "metallic": 0.85},
               extra={"effectors": [
                   {"field": "ph" + name, "op": "rotation", "blend": "add", "strength": a.swing, "axis": [0, 1, 0]},
                   {"field": "act" + name, "op": "emission", "blend": "replace", "strength": 1.0}],
                   "variation": {"seed": 11, "randomRotation": [0.0, 0.05, 0.0]},
                   "materialVariation": {"valueRandom": 0.10}})
        # the backplane: the colour behind the louvres
        bk = C("back", PAL["backGain"]) if a.back == "lit" else [0.0, 0.0, 0.0]
        s.proc("back" + name, {"kind": "box", "size": [n, H + 2.0, 0.4]},
               position=(cx - nrm[0] * 0.9, H / 2, cz - nrm[2] * 0.9), rotation=(0.0, yaw, 0.0),
               material={"baseColor": C("pier"), "roughness": 0.8, "metallic": 0.0, "emissiveColor": bk,
                         "emissiveIntensity": 1.0})
    if a.hall:
        for i, (cx, cz) in enumerate(((-W / 2, L / 2), (-W / 2, -L / 2), (W / 2, -L / 2), (W / 2, L / 2))):
            s.proc("pier%d" % i, {"kind": "box", "size": [3.2, H + 2.0, 3.2]}, position=(cx, H / 2, cz),
                   material={"baseColor": C("pier"), "roughness": 0.5, "metallic": 0.2})
        # bay piers on the long walls every `bay` metres: proud of the flaps, with a thin lit strip on their face
        nb = int(round(L / a.bay))
        for side, x in (("w", -W / 2), ("e", W / 2)):
            inward = 1.0 if side == "w" else -1.0
            s.proc("bays" + side, {"kind": "box", "size": [1.6, H + 2.0, 1.4]},
                   distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1.0, 1.0, L / nb]},
                   position=(x + inward * 0.5, H / 2, 0.0),
                   material={"baseColor": C("pier"), "roughness": 0.5, "metallic": 0.2})
            s.proc("strips" + side, {"kind": "box", "size": [0.12, H - 2.0, 0.25]},
                   distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1.0, 1.0, L / nb]},
                   position=(x + inward * 1.32, H / 2, 0.0),
                   material={"baseColor": [0, 0, 0], "roughness": 0.5, "metallic": 0.0,
                             "emissiveColor": C("key", 1.0), "emissiveIntensity": 2.5})
        # a coffered ceiling: beams across and along
        s.proc("beamsX", {"kind": "box", "size": [W + 4, 1.6, 0.9]},
               distribution={"kind": "grid", "gridCount": [1, 1, nb * 2 + 1], "gridSpacing": [1, 1, L / (nb * 2)]},
               position=(0.0, H - 0.3, 0.0), material={"baseColor": C("pier"), "roughness": 0.6, "metallic": 0.1})
        s.proc("beamsZ", {"kind": "box", "size": [0.9, 1.6, L + 4]},
               distribution={"kind": "grid", "gridCount": [5, 1, 1], "gridSpacing": [W / 4, 1, 1]},
               position=(0.0, H - 0.3, 0.0), material={"baseColor": C("pier"), "roughness": 0.6, "metallic": 0.1})
        s.proc("ceiling", {"kind": "box", "size": [W + 4, 1.0, L + 4]}, position=(0.0, H + 1.0, 0.0),
               material={"baseColor": C("ceiling"), "roughness": 0.7, "metallic": 0.0,
                         "emissiveColor": C("back", 0.06), "emissiveIntensity": 1.0})
    if a.inlay:  # brass inlay in the floor: leading lines down the hall and joints across it, catching the bands
        s.proc("inlayZ", {"kind": "box", "size": [0.16, 0.02, L]},
               distribution={"kind": "grid", "gridCount": [7, 1, 1], "gridSpacing": [W / 8, 1, 1]},
               position=(0.0, 0.005, 0.0), material={"baseColor": C("metal"), "roughness": 0.25, "metallic": 1.0})
        s.proc("inlayX", {"kind": "box", "size": [W, 0.02, 0.16]},
               distribution={"kind": "grid", "gridCount": [1, 1, int(L / 8) - 1], "gridSpacing": [1, 1, 8.0]},
               position=(0.0, 0.005, 0.0), material={"baseColor": C("metal"), "roughness": 0.25, "metallic": 1.0})
    s.proc("floor", {"kind": "box", "size": [W + 400, 1.0, L + 400]}, position=(0.0, -0.5, 0.0),
           material={"baseColor": C("floor"), "roughness": 0.18, "metallic": 0.0})
    s.environment = {
        "intensity": 0.05, "background": C("fog", 0.3), "fogColor": C("fog"), "volumeDensity": 0.0,
        "fogSky": 1.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0, "shadowRange": 160.0,
        "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C("fog", 0.3), "horizonColor": C("fog"), "groundColor": C("fog", 0.3),
                "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
        "bands": {"phase": 0.0, "rotation": 0.0, "gain": 1.0,
                  "softbox": {"intensity": 0.25, "azimuth": 0.6, "elevation": 0.7, "falloff": 2.6, "skyFill": 0.04},
                  "strips": [{"axis": [1, 0, 0], "offset": 0.0, "width": 0.02, "intensity": 7.0, "segments": 0,
                              "warmth": 0.6, "rate": 0.0},
                             {"axis": [0, 0, 1], "offset": 0.0, "width": 0.02, "intensity": 7.0, "segments": 0,
                              "warmth": 0.3, "rate": 0.0}]},
    }
    s.light("key", "directional", direction=[0.3, -0.75, -0.55], color=C("key"), intensity=3.0, castsShadow=True,
            contactShadow=False, softness=0.3)
    s.track("grid/net/coupling", [{"time": 0.0, "value": [1.5], "interp": "smooth"},
                                  {"time": 40.0, "value": [14.0], "interp": "smooth"},
                                  {"time": 56.0, "value": [14.0], "interp": "smooth"},
                                  {"time": 96.0, "value": [1.5], "interp": "smooth"}], loop=96.0)
    s.camera = {"mode": 1, "position": [0.0, 7.0, L / 2 - 8.0], "target": [0.0, 12.0, -L / 2], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": 24.0, "post/bloom/intensity": 0.25, "post/output/vignette": 0.3,
               "post/grade/contrast": 1.06})
    s.response = dict(common.RESPONSE)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--flap", default="square")
    ap.add_argument("--back", default="lit")
    ap.add_argument("--pitch", type=float, default=1.2)
    ap.add_argument("--cell", type=float, default=0.6)
    ap.add_argument("--swing", type=float, default=1.3)
    ap.add_argument("--rough", type=float, default=0.3)
    ap.add_argument("--length", type=float, default=128.0)
    ap.add_argument("--width", type=float, default=48.0)
    ap.add_argument("--height", type=float, default=30.0)
    ap.add_argument("--bay", type=float, default=16.0)
    ap.add_argument("--inlay", action="store_true")
    ap.add_argument("--single", dest="hall", action="store_false")
    a = ap.parse_args()
    s = build(a)
    pp, sp = s.write(a.out)
    n = sum(1 for x in s.nodes if x["kind"] == "field")
    flaps = sum(x["procedural"]["distribution"]["gridCount"][0] * x["procedural"]["distribution"]["gridCount"][1]
                for x in s.nodes if x["name"].startswith("flaps"))
    print(pp, "fields=%d flaps=%d grid=%s" % (n, flaps, s.grids[0]["resolution"]))


if __name__ == "__main__":
    main()
