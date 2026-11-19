#!/usr/bin/env python3
"""Neon Dreams: write the scene and the project (iteration 3: the Lowlight Mooring from the art agent's layout).

    python3 examples/neon-dreams/build.py [--out DIR] [--audio DIR] [--palette uv] [--name N] [--no-clouds]

Reads art/layout.json (instances of the kit in examples/neon-dreams/meshes, lights, inhabitants, beams, vehicles, the
journey's waypoints) and art/cloud_design.json (lobe masses, far towers, the 16 glows, the sea, the moon), both in the
engine frame (Y up, x east, z = -north; the Mooring at the origin, the journey toward +x), plus art/palette.json.

- Every kit instance becomes part of one procedural node per (asset, tint): ADR-2101 `partMaterials` give each light
  role its palette colour, and run one window program on the pane materials (glass, win_warm, win_cool) that decides
  lit or dark per window from the pane's own UV hash, its floor and the instance -- the light sits on modelled panes.
- The clouds: the design's lobes on the Cloud Environment v2 with the ADR-2101 relief sea and the designed break.
- The Glowmere cast (gitignored, symlinked from the main checkout, never committed) stands at the layout's priority
  spots and walks its paths as timeline tracks (linear keys: deterministic and seek-safe).
- Lighthouse beams: the kit's unit cone, additive, swept by a looping rotation track.
- The camera rides the layout's waypoints (camera mode 2, arc-length keys from a monotone cubic of distance over time).

Writes <out>/<name>.scene.json and <out>/<name>.json (out defaults to scene/, gitignored). Deterministic.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
SABERTOOTH = os.path.expanduser("~/Desktop/sabertooth")
SONG = 70.18  # seconds
ART = os.path.join(HERE, "art")
MESH = "../meshes/"                  # relative to the scene folder
ALIENS = "../../../assets/aliens/"   # gitignored commercial cast (symlinked from the main checkout); never committed


def r3(v):
    return [round(float(x), 3) for x in v]


with open(os.path.join(ART, "palette.json")) as f:
    PALETTE = json.load(f)
with open(os.path.join(ART, "cloud_design.json")) as f:
    DESIGN = json.load(f)
with open(os.path.join(ART, "layout.json")) as f:
    LAYOUT = json.load(f)
STATE_NAMES = {"uv": "Ultraviolet Dream", "blue": "Electric Blue", "rose": "Neon Rose", "amber": "Amber Eclipse"}


def pal(key, state="uv"):
    st = PALETTE["states"][STATE_NAMES[state]]
    if key in st:
        return st[key]["linear"]
    return PALETTE["fixed"][key]["linear"]


def bearing_dir(bearing_deg, elev_deg):
    """The art notes measure bearings from +x (east) toward north (-z)."""
    b, e = math.radians(bearing_deg), math.radians(elev_deg)
    return [math.cos(e) * math.cos(b), math.sin(e), -math.cos(e) * math.sin(b)]


MOON = DESIGN["moon"]
LIGHT_DIR = bearing_dir(MOON["light_from_bearing_deg"], MOON["light_elevation_deg"])   # toward the key


# ---------------------------------------------------------------------------------------------------------------
# materials
def window_program(name, tints, intensity):
    """Lit panes on the kit's modelled windows. Pane UV: u = brightness level (16 steps), v = a per-window hash.
    Lit or dark per window = a hash of (v, instance, floor) under the building's own lit fraction (12-50 percent,
    from instanceRandom), with whole wings dark from a coarser (instance, 8 m column, floor) hash. Each lit pane keeps
    its own brightness (0.2 + 0.8 u^2) and takes one of four tints biased toward the first."""
    ops = [
        {"kind": "input", "input": "uv", "dst": 0},                                 # (u, v)
        {"kind": "input", "input": "instanceRandom", "dst": 1},
        {"kind": "input", "input": "localPosition", "dst": 2},
        # window key: (inst * 1000, v * 1000, floor)
        {"kind": "constant", "dst": 3, "constant": [0.0, 1000.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 4, "srcA": 0, "srcB": 3},
        {"kind": "constant", "dst": 3, "constant": [1000.0, 0.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 5, "srcA": 1, "srcB": 3},                       # (inst*1000, 0, 0, 0)
        {"kind": "add", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "constant", "dst": 3, "constant": [0.125, 0.3125, 0.0, 0.0]},      # (x / 8, y / 3.2)
        {"kind": "multiply", "dst": 6, "srcA": 2, "srcB": 3},
        {"kind": "swizzle", "dst": 6, "srcA": 6, "constant": [2.0, 0.0, 1.0, 2.0]},  # (0, x/8, y/3.2, 0)
        {"kind": "swizzle", "dst": 7, "srcA": 6, "constant": [0.0, 3.0, 2.0, 3.0]},  # (0, 0, y/3.2, 0)
        {"kind": "add", "dst": 7, "srcA": 4, "srcB": 7},                            # (inst, v, floor)
        {"kind": "quantize", "dst": 7, "srcA": 7, "value": 1.0, "seed": 17},        # w = per-window hash
        {"kind": "add", "dst": 6, "srcA": 5, "srcB": 6},                            # (inst, x/8, floor)
        {"kind": "quantize", "dst": 6, "srcA": 6, "value": 1.0, "seed": 29},        # w = per-wing hash
        {"kind": "swizzle", "dst": 6, "srcA": 6, "constant": [3.0, 3.0, 3.0, 3.0]},
        {"kind": "smoothstep", "dst": 6, "srcA": 6, "constant": [0.28, 0.30, 0.0, 0.0]},  # wing on (72 %)
        {"kind": "gradient", "dst": 6, "srcA": 6, "constant": [1.25, 0.0, 0.0, 0.12], "value": 1.0},
        {"kind": "gradient", "dst": 5, "srcA": 1, "constant": [0.0, 0.40, 0.0, 0.10], "value": 1.0},  # lit fraction
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},                       # effective fraction
        {"kind": "constant", "dst": 3, "constant": [1.0, 0.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 3},                       # (frac, 0, 0, 0)
        {"kind": "swizzle", "dst": 6, "srcA": 7, "constant": [1.0, 3.0, 1.0, 1.0]},  # (v, hash, ...)
        {"kind": "constant", "dst": 3, "constant": [0.0, 1.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 3},                       # (0, hash, 0, 0)
        {"kind": "add", "dst": 5, "srcA": 5, "srcB": 6},                            # (frac, hash)
        {"kind": "gradient", "dst": 5, "srcA": 5, "constant": [1.0, -1.0, 0.0, 0.0], "value": 80.0},  # lit 0/1
        # brightness from the pane's level
        {"kind": "power", "dst": 6, "srcA": 0, "value": 2.0},
        {"kind": "gradient", "dst": 6, "srcA": 6, "constant": [0.8, 0.0, 0.0, 0.2], "value": 1.0},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
        # tint: a second hash of the window, biased toward the first stop
        {"kind": "swizzle", "dst": 6, "srcA": 7, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "quantize", "dst": 6, "srcA": 4, "value": 1.0, "seed": 43},
        {"kind": "swizzle", "dst": 6, "srcA": 6, "constant": [3.0, 3.0, 3.0, 3.0]},
        {"kind": "power", "dst": 6, "srcA": 6, "value": 1.7},
        {"kind": "oklabRamp", "dst": 6, "srcA": 6, "constant": tints[0] + [1.0], "constant2": tints[1] + [1.0],
         "constant3": tints[2] + [1.0], "constant4": tints[3] + [1.0], "value": 1.0},
        {"kind": "multiply", "dst": 7, "srcA": 6, "srcB": 5},                       # emission
        {"kind": "constant", "dst": 0, "constant": [0.012, 0.011, 0.02, 1.0]},     # dark glass
    ]
    return {"name": name, "ops": ops, "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 7,
            "emissionIntensity": intensity, "opacity": -1}


def moon_program(name, disc, light_dir, scale):
    """A wide crescent: N . L soft terminator, dark maria from a slow noise, a faint earthshine."""
    ops = [
        {"kind": "input", "input": "normal", "dst": 0},
        {"kind": "gradient", "dst": 1, "srcA": 0, "constant": light_dir + [0.0], "value": 1.0},
        {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.1, 0.35, 0, 0]},
        {"kind": "input", "input": "worldPosition", "dst": 2},
        {"kind": "noise", "dst": 3, "srcA": 2, "value": scale, "seed": 5},
        {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.35, 0.65, 0, 0]},
        {"kind": "gradient", "dst": 3, "srcA": 3, "constant": [-0.45, 0.0, 0.0, 1.0], "value": 1.0},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
        {"kind": "gradient", "dst": 1, "srcA": 1, "constant": [0.97, 0.0, 0.0, 0.03], "value": 1.0},
        {"kind": "constant", "dst": 4, "constant": disc + [1.0]},
        {"kind": "multiply", "dst": 5, "srcA": 4, "srcB": 1},
        {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 1.0]},
    ]
    return {"name": name, "ops": ops, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 5,
            "emissionIntensity": 1.0, "opacity": -1}


def ocean_program(name, deep, bright, scale, intensity):
    ops = [
        {"kind": "input", "input": "worldPosition", "dst": 0},
        {"kind": "input", "input": "time", "dst": 1},
        {"kind": "constant", "dst": 2, "constant": [4.0, 0.0, 2.5, 0.0]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},
        {"kind": "noise", "dst": 3, "srcA": 0, "value": scale, "seed": 5},
        {"kind": "noise", "dst": 4, "srcA": 0, "value": scale * 5.3, "seed": 9},
        {"kind": "mix", "dst": 3, "srcA": 3, "srcB": 4, "value": 0.35},
        {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.3, 0.8, 0, 0]},
        {"kind": "oklabRamp", "dst": 5, "srcA": 3, "constant": [0.0, 0.0, 0.0, 1.0], "constant2": deep + [1.0],
         "constant3": bright + [1.0], "constant4": [1.0, 0.8, 1.0, 1.0], "value": 1.0},
        {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 1.0]},
    ]
    return {"name": name, "ops": ops, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 5,
            "emissionIntensity": intensity, "opacity": -1}


def flat_emit_program(name, color, intensity):
    return {"name": name, "ops": [{"kind": "constant", "dst": 0, "constant": color + [1.0]},
                                  {"kind": "constant", "dst": 1, "constant": [0.0, 0.0, 0.0, 1.0]}],
            "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 0, "emissionIntensity": intensity,
            "opacity": -1}



PANES = ("glass", "win_warm", "win_cool")
TINT_DEFAULTS = {"win_warm": "win_d", "win_cool": "win_a", "strip": "eave", "beacon": "beacon", "landmark": "landmark",
                 "garden": "garden_a", "lantern": "amber", "keel": "bounce_a", "neon_a": "sign_a",
                 "beam_lamp": "moon_disc", "cond_glow": "cond_a", "cond_lamp": "cond_a", "cond_work": "cond_c"}
# emissive strength per light role (scene-linear multipliers on the palette colour; art-direction section 3)
ROLE_GAIN = {"strip": 7.0, "beacon": 40.0, "landmark": 6.0, "garden": 10.0, "lantern": 7.0, "keel": 7.0,
             "neon_a": 14.0, "beam_lamp": 14.0, "cond_glow": 9.0, "cond_lamp": 18.0, "cond_work": 8.0}


def part_materials(tint, state="uv"):
    pm = {name: {"program": "ndPanes"} for name in PANES}
    for role, default in TINT_DEFAULTS.items():
        if role in PANES:
            continue
        key = tint.get(role, default)
        pm[role] = {"emissiveColor": pal(key, state), "emissiveIntensity": ROLE_GAIN[role]}
    return pm


def kit_nodes(state="uv"):
    """One procedural node per (asset, tint) group of the layout's instances."""
    groups = {}
    for inst in LAYOUT["instances"]:
        tint = inst.get("tint", {})
        key = (inst["asset"], tuple(sorted(tint.items())))
        groups.setdefault(key, []).append(inst)
    nodes = []
    used = set()
    for (asset, tint), insts in sorted(groups.items()):
        base = os.path.splitext(os.path.basename(asset))[0]
        name = base
        k = 1
        while name in used:
            name = "%s_%d" % (base, k)
            k += 1
        used.add(name)
        pts = [r3(i["pos"]) + r3(i["quat"]) + r3(i["scale"]) for i in insts]
        shadow = asset.startswith("buildings/") or asset.startswith("landmarks/")
        nodes.append({"name": name, "kind": "procedural", "position": [0, 0, 0], "procedural": {
            "source": {"kind": "mesh", "asset": MESH + asset},
            "distribution": {"kind": "points", "points": pts},
            "castsShadow": shadow,
            "partMaterials": part_materials(dict(tint), state)}})
    return nodes


def light_nodes(state="uv"):
    """The layout's practical lights: small emissive bulbs are in the kit; these are the light they cast."""
    out = []
    for l in LAYOUT["lights"]:
        rad = l.get("radius_m", 9.0)
        out.append({"name": l["name"], "type": "point", "position": r3(l["pos"]), "color": pal(l["key"], state),
                    "intensity": round(0.45 * rad * rad * l.get("intensity", 1.0), 2), "range": rad * 2.5,
                    "castsShadow": False, "volumetric": 0.0})
    return out


# ---------------------------------------------------------------------------------------------------------------
# the inhabitants: the Glowmere cast at the layout's spots and on its paths
CAST = ["alien-scout", "alien-elder", "alien-diver", "alien-pilot", "alien-ranger", "alien-trooper"]


def inhabitant_nodes(max_spots=14, scale=None):
    inh = LAYOUT["inhabitants"]
    s = scale if scale is not None else inh.get("scale", 1.0)
    r = random.Random(77)
    nodes, tracks = [], []
    k = 0

    def node(name, pos, yaw_deg, state):
        return {"name": name, "kind": "gltf", "asset": ALIENS + CAST[k % len(CAST)] + ".glb", "position": r3(pos),
                "rotation": [0.0, round(yaw_deg, 1), 0.0], "scale": [s, s, s],
                "animation": {"state": state, "blend": 0.3, "speed": round(r.uniform(0.85, 1.1), 3), "updateHz": 30,
                              "nearDistance": 40.0, "farHz": 15.0, "cullDistance": 300.0}}
    spots = sorted([sp for sp in inh["spots"] if sp["priority"] == 1], key=lambda sp: sp["id"])
    step = max(1, len(spots) // max_spots)
    for sp in spots[::step][:max_spots]:
        f = sp["forward"]
        nodes.append(node("alien%02d" % k, sp["pos"], math.degrees(math.atan2(f[0], f[2])),
                          r.choice(sp.get("clips", ["Idle"]))))
        k += 1
    for p in inh["paths"]:
        if p["priority"] != 1:
            continue
        pts = p["points"]
        seg = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
        length = sum(seg)
        speed = p.get("speed_mps", 1.2)
        for w in range(min(p.get("walkers", 1), 1)):
            name = "alien%02d" % k
            # pingpong along the polyline: position keys at every vertex, a facing step key on every segment
            route = pts + pts[-2::-1]
            cum = [0.0]
            for i in range(len(route) - 1):
                cum.append(cum[-1] + math.dist(route[i], route[i + 1]))
            period = cum[-1] / speed
            offset = r.uniform(0, period)
            pk, rk = [], []
            t0 = -offset
            while t0 < SONG + 1:
                for i in range(len(route)):
                    t = t0 + cum[i] / speed
                    if -period < t < SONG + period:
                        pk.append({"time": round(t, 3), "value": r3(route[i]), "interp": "linear"})
                    if i + 1 < len(route):
                        d = [route[i + 1][j] - route[i][j] for j in range(3)]
                        rk.append({"time": round(t, 3), "value": [0.0, round(math.degrees(math.atan2(d[0], d[2])), 1), 0.0],
                                   "interp": "step"})
                t0 += period
            pk = sorted([kk for kk in pk if kk["time"] >= -period], key=lambda kk: kk["time"])
            rk = sorted([kk for kk in rk if kk["time"] >= -period], key=lambda kk: kk["time"])
            # keys must start at or after 0: clip the first segment by interpolating it
            pk = clip_keys(pk)
            rk = [kk for kk in rk if kk["time"] >= 0] or rk[-1:]
            if rk and rk[0]["time"] > 0:
                prev = [kk for kk in sorted(rk, key=lambda kk: kk["time"])]
                rk.insert(0, {"time": 0.0, "value": prev[0]["value"], "interp": "step"})
            nodes.append(node(name, pk[0]["value"], rk[0]["value"][1] if rk else 0.0, p.get("clip", "Walking")))
            tracks.append({"target": "nodes/%s/position" % name, "component": -1, "enabled": True,
                           "timeBase": "seconds", "mode": "replace", "loopLength": 0.0, "keys": pk})
            tracks.append({"target": "nodes/%s/rotation" % name, "component": -1, "enabled": True,
                           "timeBase": "seconds", "mode": "replace", "loopLength": 0.0, "keys": rk})
            k += 1
    return nodes, tracks


def clip_keys(keys):
    """Linear keys trimmed to start at t = 0 (the first key interpolated there)."""
    out = [kk for kk in keys if kk["time"] >= 0]
    before = [kk for kk in keys if kk["time"] < 0]
    if before and out:
        a, b = before[-1], out[0]
        u = -a["time"] / (b["time"] - a["time"])
        out.insert(0, {"time": 0.0, "value": r3([a["value"][j] + (b["value"][j] - a["value"][j]) * u for j in range(3)]),
                       "interp": "linear"})
    return out


# ---------------------------------------------------------------------------------------------------------------
# vehicles on the layout's splines (position tracks: seek-safe)
def vehicle_nodes(state="uv"):
    nodes, tracks = [], []
    for i, v in enumerate(LAYOUT["vehicles"]):
        pts = LAYOUT["splines"][v["spline"]]
        hang = v.get("hang_below_m", 0.0)
        pts = [[p[0], p[1] - hang, p[2]] for p in pts]
        route = pts + pts[-2::-1]
        cum = [0.0]
        for j in range(len(route) - 1):
            cum.append(cum[-1] + math.dist(route[j], route[j + 1]))
        period = cum[-1] / v["speed_mps"]
        t0 = -v.get("phase", 0.0) * period
        pk, rk = [], []
        while t0 < SONG + 1:
            for j in range(len(route)):
                t = t0 + cum[j] / v["speed_mps"]
                pk.append({"time": round(t, 3), "value": r3(route[j]), "interp": "linear"})
                if j + 1 < len(route):
                    d = [route[j + 1][q] - route[j][q] for q in range(3)]
                    rk.append({"time": round(t, 3), "value": [0.0, round(math.degrees(math.atan2(-d[2], d[0])), 1), 0.0],
                               "interp": "step"})
            t0 += period
        pk = clip_keys(sorted(pk, key=lambda kk: kk["time"]))
        rk = sorted(rk, key=lambda kk: kk["time"])
        first = [kk for kk in rk if kk["time"] <= 0]
        rk = [kk for kk in rk if kk["time"] > 0]
        rk.insert(0, {"time": 0.0, "value": (first[-1] if first else rk[0])["value"], "interp": "step"})
        name = "vehicle%d" % i
        tint = v.get("tint", {})
        nodes.append({"name": name, "kind": "procedural", "position": pk[0]["value"], "procedural": {
            "source": {"kind": "mesh", "asset": MESH + v["asset"]},
            "distribution": {"kind": "points", "points": [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]]},
            "castsShadow": False, "partMaterials": part_materials(tint, state)}})
        tracks.append({"target": "nodes/%s/position" % name, "component": -1, "enabled": True, "timeBase": "seconds",
                       "mode": "replace", "loopLength": 0.0, "keys": pk})
        tracks.append({"target": "nodes/%s/rotation" % name, "component": -1, "enabled": True, "timeBase": "seconds",
                       "mode": "replace", "loopLength": 0.0, "keys": rk})
    return nodes, tracks


# ---------------------------------------------------------------------------------------------------------------
# lighthouse beams: the kit's unit cone (1 m along +x), additive, swept by a looping yaw track on the node
def beam_program(name, color, intensity):
    """Bright at the lamp, fading along the beam (local x 0..1) and toward its rim."""
    ops = [
        {"kind": "input", "input": "localPosition", "dst": 0},
        {"kind": "gradient", "dst": 1, "srcA": 0, "constant": [-1.0, 0.0, 0.0, 1.0], "value": 1.0},   # 1 - x
        {"kind": "power", "dst": 1, "srcA": 1, "value": 1.6},
        {"kind": "fresnel", "dst": 2, "value": 1.5},
        {"kind": "gradient", "dst": 2, "srcA": 2, "constant": [-1.0, 0.0, 0.0, 1.0], "value": 1.0},   # facing core
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 3, "constant": color + [1.0]},
        {"kind": "multiply", "dst": 4, "srcA": 3, "srcB": 1},
        {"kind": "constant", "dst": 5, "constant": [0.0, 0.0, 0.0, 1.0]},
        {"kind": "constant", "dst": 6, "constant": [0.12, 0.12, 0.12, 0.12]},
        {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 1},                                          # opacity
    ]
    return {"name": name, "ops": ops, "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 4,
            "emissionIntensity": intensity, "opacity": 6}


def beam_nodes(state="uv"):
    nodes, tracks = [], []
    for b in LAYOUT["beams"]:
        L = b["length_m"]
        rr = L * math.tan(math.radians(b["half_angle_deg"])) / 0.12
        pitch = math.radians(b["elevation_deg"])
        pts = []
        for c in range(b["count"]):
            yaw = 2 * math.pi * c / b["count"]
            # yaw about +y, then the pitch about the beam's own z (quat = qy * qz)
            qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
            qz = [0.0, 0.0, math.sin(pitch / 2), math.cos(pitch / 2)]
            q = [qy[3] * qz[0] + qy[0] * qz[3] + qy[1] * qz[2] - qy[2] * qz[1],
                 qy[3] * qz[1] - qy[0] * qz[2] + qy[1] * qz[3] + qy[2] * qz[0],
                 qy[3] * qz[2] + qy[0] * qz[1] - qy[1] * qz[0] + qy[2] * qz[3],
                 qy[3] * qz[3] - qy[0] * qz[0] - qy[1] * qz[1] - qy[2] * qz[2]]
            pts.append([0, 0, 0] + r3(q) + [L, round(rr, 3), round(rr, 3)])
        name = "beam_" + b["name"]
        nodes.append({"name": name, "kind": "procedural", "position": r3(b["origin"]), "procedural": {
            "source": {"kind": "mesh", "asset": MESH + "landmarks/beam_cone_unit.glb"},
            "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
            "material": {"baseColor": [0, 0, 0], "opacity": 0.1, "alphaMode": "blend", "unlit": False,
                         "doubleSided": True, "program": "ndBeam"}}})
        period = 60.0 / b["rpm"]
        ph = b.get("phase", 0.0) * 360.0
        tracks.append({"target": "nodes/%s/rotation" % name, "component": -1, "enabled": True, "timeBase": "seconds",
                       "mode": "replace", "loopLength": round(period, 4),
                       "keys": [{"time": 0.0, "value": [0.0, round(ph - 180.0, 2), 0.0], "interp": "linear"},
                                {"time": round(period, 4), "value": [0.0, round(ph + 180.0, 2), 0.0],
                                 "interp": "linear"}]})
    return nodes, tracks


# ---------------------------------------------------------------------------------------------------------------
# clouds: the design's lobes on the v2 block with the relief sea
def clouds_block(state="uv"):
    lobes = []
    for name, ls in DESIGN["masses"].items():
        lobes += [dict(l) for l in ls]
    for name, ls in DESIGN.get("far_masses", {}).items():
        lobes += [dict(l) for l in ls]
    cb = DESIGN["clouds_block"]
    glows = [{"name": g["name"], "position": g["position"], "color": pal(g["key"], state), "radius": g["radius"],
              "intensity": g["intensity"]} for g in cb["glows"]]
    sea = DESIGN["sea_design"]
    br = sea["break"]
    lo, hi = sea["top_range"]
    return {
        "lobes": lobes,
        "sea": {"base": sea["base"], "top": sea["top_mean"], "coverage": 1.0, "coverageScale": 0.0003,
                "density": 1.6, "relief": round((hi - lo) / 2.4, 1), "reliefScale": 2000.0, "billow": 24.0,
                "farFade": 0.35,
                "breaks": [{"centre": br["centre"], "radius": br["radius"], "elongation": br["elongation"],
                            "yaw": br["yaw_rad"]}]},
        "glows": glows, "glowGain": 0.35, "glowOcclusion": 2.0,
        "extinction": 0.02, "density": 1.0, "swell": 0.0,
        "steps": 72, "lightSteps": 2, "lightDistance": 160.0,
        "resolution": 0.5, "temporal": 0.85, "minStep": 3.0, "stepGrowth": 0.02, "maxDistance": 24000.0,
        "lightDirection": r3(LIGHT_DIR), "lightColor": pal("moon_light", state), "lightIntensity": 0.34,
        "anisotropy": 0.5,
        "ambientTop": [v * 0.6 for v in pal("sky_horizon", state)], "ambientBottom": [v * 0.12 for v in pal("haze", state)],
        "warmColor": [v * 0.15 for v in pal("horizon_glow", state)], "warmDirection": r3(cb["warmDirection"]),
        "warmHeight": 900.0,
        "noiseScale": 0.018, "noiseAmount": 0.72, "detailScale": 0.07, "detailAmount": 0.6,
        "drift": r3([v * 0.7 for v in cb["drift"]]),
        "hazeColor": pal("haze", state), "hazeDistance": 9000.0, "powder": 0.6, "seed": 7,
    }


# ---------------------------------------------------------------------------------------------------------------
# the journey: the layout's waypoints (engine frame)
WAYPOINTS = [(w["t"], w["pos"]) for w in LAYOUT["camera"]["waypoints"]]
if WAYPOINTS[-1][0] < SONG:
    WAYPOINTS[-1] = (SONG, WAYPOINTS[-1][1])
def catmull(p0, p1, p2, p3, t, alpha=0.5):
    def tj(ti, a, b):
        return ti + max(math.dist(a, b), 1e-6) ** alpha
    t0 = 0.0
    t1 = tj(t0, p0, p1)
    t2 = tj(t1, p1, p2)
    t3 = tj(t2, p2, p3)
    tt = t1 + (t2 - t1) * t

    def lerp(a, b, ta, tb):
        w = (tt - ta) / (tb - ta)
        return [a[i] + (b[i] - a[i]) * w for i in range(3)]
    a1 = lerp(p0, p1, t0, t1)
    a2 = lerp(p1, p2, t1, t2)
    a3 = lerp(p2, p3, t2, t3)
    b1 = lerp(a1, a2, t0, t2)
    b2 = lerp(a2, a3, t1, t3)
    return lerp(b1, b2, t1, t2)


def journey():
    pts = [list(p) for _, p in WAYPOINTS]
    ext = [[2 * pts[0][i] - pts[1][i] for i in range(3)]] + pts + [[2 * pts[-1][i] - pts[-2][i] for i in range(3)]]
    dense = []
    wp_index = []
    for k in range(len(pts) - 1):
        seg = math.dist(pts[k], pts[k + 1])
        n = max(4, int(seg / 4.0))
        wp_index.append(len(dense))
        for j in range(n):
            dense.append(catmull(ext[k], ext[k + 1], ext[k + 2], ext[k + 3], j / n))
    wp_index.append(len(dense))
    dense.append(pts[-1])
    cum = [0.0]
    for k in range(1, len(dense)):
        cum.append(cum[-1] + math.dist(dense[k - 1], dense[k]))
    total = cum[-1]
    ts = [t for t, _ in WAYPOINTS]
    ss = [cum[i] for i in wp_index]
    # Fritsch-Carlson monotone cubic s(t)
    n = len(ts)
    d = [(ss[i + 1] - ss[i]) / (ts[i + 1] - ts[i]) for i in range(n - 1)]
    m = [d[0]] + [(d[i - 1] + d[i]) / 2 if d[i - 1] * d[i] > 0 else 0.0 for i in range(1, n - 1)] + [d[-1]]
    for i in range(n - 1):
        if d[i] == 0:
            m[i] = m[i + 1] = 0
            continue
        a, b = m[i] / d[i], m[i + 1] / d[i]
        h = a * a + b * b
        if h > 9:
            tau = 3 / math.sqrt(h)
            m[i], m[i + 1] = tau * a * d[i], tau * b * d[i]

    def s_at(t):
        for i in range(n - 1):
            if t <= ts[i + 1] or i == n - 2:
                hh = ts[i + 1] - ts[i]
                u = min(max((t - ts[i]) / hh, 0.0), 1.0)
                h00 = 2 * u ** 3 - 3 * u ** 2 + 1
                h10 = u ** 3 - 2 * u ** 2 + u
                h01 = -2 * u ** 3 + 3 * u ** 2
                h11 = u ** 3 - u ** 2
                return h00 * ss[i] + h10 * hh * m[i] + h01 * ss[i + 1] + h11 * hh * m[i + 1]
    keys = []
    t = 0.0
    while t < SONG:
        keys.append({"time": round(t, 3), "value": round(s_at(t) / total, 6), "interp": "linear"})
        t += 0.25
    keys.append({"time": SONG, "value": 1.0, "interp": "linear"})
    return dense, keys, total, s_at



# ---------------------------------------------------------------------------------------------------------------
STILL_TIMES = [2.8, 17.0, 35.75, 50.5, 58.95, 66.0]
BEAMS = False


def scene_doc(state="uv", sky_map=None):
    nodes = kit_nodes(state)
    # the Drowned Moon: bearing 19, centre 2.2 degrees up, 8.2 degrees in radius, far out
    md = bearing_dir(MOON["disc_bearing_deg"], MOON["disc_centre_elevation_deg"])
    D = 60000.0  # placed by its point: a node position clamps at 10 km (nodes/<n>/position)
    R = D * math.tan(math.radians(MOON["disc_angular_radius_deg"]))
    up = [0.0, 1.0, 0.0]
    right = [md[2] * up[1] - md[1] * up[2], md[0] * up[2] - md[2] * up[0], md[1] * up[0] - md[0] * up[1]]
    rl = math.sqrt(sum(v * v for v in right))
    right = [v / rl for v in right]
    ml = [0.45 * up[i] + 0.8 * right[i] + 0.25 * md[i] for i in range(3)]   # upper left, a little behind
    mll = math.sqrt(sum(v * v for v in ml))
    ml = [round(v / mll, 4) for v in ml]
    nodes.append({"name": "moon", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "sphere", "radius": 1000.0, "segments": 96, "rings": 48},
        "distribution": {"kind": "points", "points": [r3([md[i] * D for i in range(3)]) + [0, 0, 0, 1] +
                                                      [round(R / 1000.0, 4)] * 3]},
        "castsShadow": False,
        "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndMoon"}}})
    # the cosmic ocean far under the break
    br = DESIGN["sea_design"]["break"]["centre"]
    nodes.append({"name": "cosmicOcean", "kind": "procedural", "position": [br[0], -900, br[1]], "procedural": {
        "source": {"kind": "cylinder", "radius": 900.0, "height": 40.0, "radialSegments": 64, "caps": True},
        "distribution": {"kind": "single"}, "castsShadow": False,
        "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndOcean"}}})
    aliens, alien_tracks = inhabitant_nodes()
    vehicles, vehicle_tracks = vehicle_nodes(state)
    # The beam cones draw opaque (a procedural mesh's blend/opacity is not honoured: found this iteration), so they are
    # built but left out until the beams have a transparent path; --beams puts them in.
    beams, beam_tracks = beam_nodes(state) if BEAMS else ([], [])
    nodes += aliens + vehicles + beams
    programs = [
        window_program("ndPanes", [pal("win_a", state), pal("win_b", state), pal("win_c", state), pal("win_d", state)],
                       4.0),
        moon_program("ndMoon", [v * 0.55 for v in pal("moon_disc", state)], ml, 3.0 / R),
        ocean_program("ndOcean", pal("ocean_a", state), pal("ocean_b", state), 0.0012, 2.5),
        beam_program("ndBeam", pal("moon_disc", state), 1.2),
    ]
    env = {
        "background": [0.001, 0.001, 0.004],
        "intensity": 0.15, "skyIntensity": 0.35, "lightFromEnvironment": False,
        "fogColor": pal("haze", state), "fogSky": 0.0, "fogSkyDistance": 20000.0,
        "volumeDensity": 0.0,
    }
    if sky_map:
        env["map"] = sky_map
        env["rotation"] = 0.0
    lights = [{"name": "moon", "type": "directional", "role": "key", "direction": r3([-v for v in LIGHT_DIR]),
               "color": pal("moon_light", state), "intensity": 0.35, "castsShadow": True, "volumetric": 0.0},
              {"name": "skyfill", "type": "directional", "role": "fill", "direction": [0.3, -0.6, 0.4],
               "color": [0.25, 0.15, 0.7], "intensity": 0.08, "castsShadow": False, "volumetric": 0.0}]
    lights += light_nodes(state)
    dense, keys, total, _ = journey()
    doc = {
        "format": "avgen-scene", "version": 1, "name": "neon-dreams",
        "_note": "Generated by examples/neon-dreams/build.py from art/layout.json. Do not edit.",
        "camera": {"mode": 2, "spline": "ride", "fov": 52.0, "orbitSpeed": 0.0, "near": 0.3, "far": 120000.0},
        "environment": env,
        "lights": lights,
        "effects": [{"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True,
                     "order": 0, "activation": "always",
                     "parameters": {"brightness": 1.6, "density": 0.45, "magnitudeSlope": 5.5, "colorSpread": 0.35,
                                    "twinkle": 0.2, "twinkleRate": 1.0, "horizonFade": 0.5, "band": 0.25,
                                    "bandTilt": 40.0, "daylight": 0.0}}],
        "nodes": nodes + [{"name": "ride", "kind": "spline", "spline": {
            "name": "ride", "kind": "catmullRom", "closed": False, "tension": 0.5, "generator": "points",
            "samplesPerSegment": 4, "points": [{"position": r3(p), "roll": 0.0, "scale": 1.0} for p in dense]}}],
        "materialPrograms": programs,
        "clouds": clouds_block(state),
    }
    return doc, [{"target": "camera/splineT", "component": -1, "enabled": True, "timeBase": "seconds",
                  "mode": "replace", "loopLength": 0.0, "keys": keys}] + alien_tracks + vehicle_tracks + beam_tracks


def cloud_routes():
    """The three ADR-2100 routes of iteration 2 (audio routing is iteration 4)."""
    def r(source, target, amount, **chain):
        out = {"source": source, "target": target, "amount": amount, "op": "add", "polarity": "unipolar"}
        if chain:
            out["chain"] = chain
        return out
    return [
        r("stem.bass.level", "clouds/swell", 0.25, attackMs=350.0, decayMs=1800.0),
        r("stem.kick.onset", "clouds/glow/0/intensity", 1.2, attackMs=15.0, decayMs=450.0),
        r("stem.snare.onset", "clouds/glow/2/intensity", 1.0, attackMs=15.0, decayMs=350.0),
    ]


def trim_track(track):
    """Keys past the song's end (a walker's or vehicle's last leg) made the render run on to the last key (254 s)."""
    keys = sorted(track["keys"], key=lambda k: k["time"])
    out = [k for k in keys if k["time"] <= SONG]
    later = [k for k in keys if k["time"] > SONG]
    if later and track.get("loopLength", 0.0) == 0.0:
        out.append(later[0] if later[0]["time"] - SONG < 0.05 else dict(later[0], time=round(SONG, 3)) if
                   later[0]["interp"] == "step" else interpolate_key(out[-1] if out else later[0], later[0], SONG))
    track = dict(track, keys=out if out else keys[:1])
    return track


def interpolate_key(a, b, t):
    u = (t - a["time"]) / max(b["time"] - a["time"], 1e-6)
    va, vb = a["value"], b["value"]
    v = [round(x + (y - x) * u, 3) for x, y in zip(va, vb)] if isinstance(va, list) else round(va + (vb - va) * u, 6)
    return {"time": round(t, 3), "value": v, "interp": b["interp"]}


def project_doc(scene_rel, audio_dir, tracks):
    stems = [("kick", "Kick", "drums"), ("snare", "Snare", "drums"), ("hh", "HH", "drums"), ("bass", "BASS", "bass"),
             ("moog", "Moog", "synth"), ("cloud", "Cloud Diving", "synth"), ("crash", "Crash", "drums"),
             ("toms", "Toms", "drums"), ("block", "tone block", "other")]
    return {
        "format": "avgen-project", "version": 4,
        "assets": {"scene": {"kind": "composition", "path": scene_rel},
                   "audio": os.path.join(audio_dir, "Sabertooth.wav"),
                   "stems": [{"key": k, "label": lbl, "role": role, "file": os.path.join(audio_dir, lbl + ".wav")}
                             for k, lbl, role in stems]},
        "sonic": {"live": False},
        "feeds": {"beat": "stem.kick", "hits": "stem.kick", "levels": "audio"},
        "parameters": {"camera/lookAhead": 45.0, "camera/splineBank": 5.0, "post/output/dither": 1.0},
        "routes": cloud_routes(),
        "timeline": {"enabled": True, "cues": [], "tracks": [trim_track(t) for t in tracks]},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/neon-dreams.mp4"},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "scene"))
    ap.add_argument("--audio", default=SABERTOOTH)
    ap.add_argument("--name", default="neon-dreams")
    ap.add_argument("--palette", default="uv", choices=sorted(STATE_NAMES))
    ap.add_argument("--sky", default="sky/sky_uv.hdr", help="equirect sky map relative to this folder ('' = analytic)")
    ap.add_argument("--no-clouds", action="store_true", help="the cloud block disabled (for an A/B of its cost)")
    ap.add_argument("--beams", action="store_true", help="include the lighthouse beam cones (they draw opaque today)")
    a = ap.parse_args()
    global BEAMS
    BEAMS = a.beams
    os.makedirs(a.out, exist_ok=True)
    sky = os.path.relpath(os.path.join(HERE, a.sky), a.out) if a.sky else None
    scene_path = os.path.join(a.out, a.name + ".scene.json")
    doc, tracks = scene_doc(a.palette, sky)
    if a.no_clouds:
        doc["clouds"]["enabled"] = False
    with open(scene_path, "w") as f:
        json.dump(doc, f, indent=1)
    proj_path = os.path.join(a.out, a.name + ".json")
    with open(proj_path, "w") as f:
        json.dump(project_doc(os.path.relpath(scene_path, a.out), a.audio, tracks), f, indent=1)
    print("wrote", scene_path, "and", proj_path)


if __name__ == "__main__":
    main()
