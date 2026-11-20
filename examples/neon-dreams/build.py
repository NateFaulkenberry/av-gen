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
- Lighthouse beams: ADR-2102 cloud beams (marched with the cloud: air scatter hidden behind cloud, the cloud they cross
  lit), turning at the layout's rpm.
- The Veil passage (iteration 4): a turret of lobes laid along the journey from 47 s, thinning from 53.5 s, so the camera
  is inside cloud from the garden's edge to the reveal; a rose glow inside answers the toms, an amber one ahead is the
  first hint of the Condensery.
- The camera rides the layout's waypoints (camera mode 2, arc-length keys from a monotone cubic of distance over time).

Writes <out>/<name>.scene.json and <out>/<name>.json (out defaults to scene/, gitignored). Deterministic.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nd_audio as AU  # noqa: E402  (iteration 5: audio reactivity and the palette arc)
import nd_flight as FL  # noqa: E402  (iteration 10, owner msg 117: jetpack flights)

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
# Iteration 5 (art, adopted by the orchestrator): the Drowned Moon is an identity element -- about 15 degrees across with
# more than a third under the sea horizon (the art data's bearing and centre), kept dim (MOON_GAIN, the limb darkening,
# the horizon extinction and the halo) so the lit architecture stays the brightest thing in frame. The cloud key and the
# Condensery's turn now live in the art data (the iteration-4 overrides are gone).
MOON_DISC = {"bearing": MOON["disc_bearing_deg"], "elevation": MOON["disc_centre_elevation_deg"], "radius": 7.5}
MOON_GAIN = 0.14
HORIZON_Y = 190.0   # world height of the sea horizon on the moon's sphere (the eye rides 118-258 m)
KEY = {"bearing": MOON["light_from_bearing_deg"], "elevation": MOON["light_elevation_deg"]}
LIGHT_DIR = bearing_dir(KEY["bearing"], KEY["elevation"])   # toward the key


# ---------------------------------------------------------------------------------------------------------------
# materials
def moon_program(name, disc, light_dir, scale, centre_y, radius):
    """The Drowned Moon: a soft terminator, two octaves of maria and craters, limb darkening, and extinction toward the
    sea horizon (the lower part dims and warms through the haze); the corona is the cloud block's halo (ADR-2102)."""
    deep = [disc[0] * 0.30, disc[1] * 0.16, disc[2] * 0.16]
    mid = [disc[0] * 0.62, disc[1] * 0.50, disc[2] * 0.62]
    ops = [
        {"kind": "input", "input": "normal", "dst": 0},
        {"kind": "gradient", "dst": 1, "srcA": 0, "constant": light_dir + [0.0], "value": 1.0},
        {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.0, 0.5, 0, 0]},
        {"kind": "input", "input": "worldPosition", "dst": 2},
        {"kind": "noise", "dst": 3, "srcA": 2, "value": scale, "seed": 5},
        {"kind": "noise", "dst": 4, "srcA": 2, "value": scale * 4.7, "seed": 11},
        {"kind": "mix", "dst": 3, "srcA": 3, "srcB": 4, "value": 0.35},
        {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.38, 0.62, 0, 0]},
        {"kind": "gradient", "dst": 3, "srcA": 3, "constant": [-0.42, 0.0, 0.0, 1.0], "value": 1.0},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
        {"kind": "gradient", "dst": 1, "srcA": 1, "constant": [0.96, 0.0, 0.0, 0.04], "value": 1.0},   # earthshine
        {"kind": "fresnel", "dst": 5, "value": 2.0},
        {"kind": "gradient", "dst": 5, "srcA": 5, "constant": [-0.55, 0.0, 0.0, 1.0], "value": 1.0},  # limb
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 5},
        # extinction: 0 at the disc's foot, 1 a little above its centre
        {"kind": "gradient", "dst": 6, "srcA": 2, "constant": [0.0, 1.0, 0.0, -(centre_y - radius) / (1.25 * radius)],
         "value": 1.0 / (1.25 * radius)},
        {"kind": "oklabRamp", "dst": 6, "srcA": 6, "constant": deep + [1.0], "constant2": mid + [1.0],
         "constant3": disc + [1.0], "constant4": disc + [1.0], "value": 1.0},
        {"kind": "multiply", "dst": 7, "srcA": 6, "srcB": 1},
        # iteration 5: sunk in the sea -- nothing of the disc glows below the horizon (a moon 15 degrees across showed
        # its lower half as a bright ellipse through the thin far sea)
        {"kind": "gradient", "dst": 5, "srcA": 2, "constant": [0.0, 1.0, 0.0, -HORIZON_Y / (0.03 * radius)],
         "value": 1.0 / (0.03 * radius)},
        {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 5},
        {"kind": "constant", "dst": 4, "constant": [0.0, 0.0, 0.0, 1.0]},
    ]
    # below the horizon the disc is cut out (alpha mask on r5), so the sea and the sky show where it is sunk
    return {"name": name, "ops": ops, "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 7,
            "emissionIntensity": 1.0, "opacity": 5}


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
TINT_DEFAULTS = {"win_stair": "win_a", "win_warm": "win_d", "win_cool": "win_a", "strip": "eave", "beacon": "beacon", "landmark": "landmark",
                 "garden": "garden_a", "lantern": "amber", "keel": "bounce_a", "neon_a": "sign_a",
                 "beam_lamp": "moon_disc", "cond_glow": "cond_a", "cond_lamp": "cond_a", "cond_work": "cond_c"}
# emissive strength per light role (scene-linear multipliers on the palette colour; art-direction section 3)
ROLE_GAIN = {"win_stair": 3.5, "strip": 4.5, "beacon": 40.0, "landmark": 1.6, "garden": 10.0, "lantern": 7.0, "keel": 7.0,
             "neon_a": 14.0, "beam_lamp": 14.0, "cond_glow": 9.0, "cond_lamp": 18.0, "cond_work": 8.0}


# Iteration 5: every light role runs a program of its own (nd_audio.py), so a stem reaches exactly the roles and the
# district it should, and the palette arc keys each program's colour at its system's rate.
NODE_OF = {}
PROGS = {}  # name -> {"prog": Prog, "doc": program, "system": arc system or None, "key": palette key, "group": route group}


def zclass_of(zone):
    return "cond" if zone == "condensery" else "lone" if zone == "lone_lamp" else "town"


def fixed_key(key):
    return key in PALETTE["fixed"]


def _reg(name, made, system, key, group):
    if name not in PROGS:
        prog, doc = made()
        PROGS[name] = {"prog": prog, "doc": doc, "system": None if fixed_key(key) else system, "key": key,
                       "group": group}
    return name


def role_program(role, key, zclass, asset, state):
    gain = ROLE_GAIN[role]
    col = pal(key, state)
    if zclass == "cond":   # the Condensery answers only the bass, as a slow ember; its colours are its own
        n = "ndCond_%s_%s" % (role, key)
        return _reg(n, lambda: AU.emit_program(n, col, gain), None, key, "cond")
    if zclass == "lone":   # the Lone Lamp answers only the pad
        n = "ndLone_%s_%s" % (role, key)
        return _reg(n, lambda: AU.emit_program(n, col, gain), None, key, "lone")
    if role == "neon_a":
        if "roundel" in asset:   # roundels blink with the tone block
            n = "ndRoundel_%s" % key
            return _reg(n, lambda: AU.emit_program(n, col, gain * 0.08, pattern=True), "sign", key, "block_roundel")
        n = "ndSign_%s" % key    # businesses A (sign_a, sign_c) and B (sign_b) answer the snare in turn
        return _reg(n, lambda: AU.emit_program(n, col, gain), "sign", key, "snareB" if key == "sign_b" else "snareA")
    if role == "strip":
        if "gangway" in asset:   # the gangway strips: the snare
            n = "ndGang_%s" % key
            return _reg(n, lambda: AU.emit_program(n, col, gain), "eave", key, "gangway")
        n = "ndStrip_%s" % key   # eave lines: the kick ring
        return _reg(n, lambda: AU.emit_program(n, col, gain, ring=1.8), "eave", key, None)
    if role == "beacon":
        n = "ndBeacon"
        return _reg(n, lambda: AU.emit_program(n, col, gain * 0.02, pattern=True), None, key, "block_beacon")
    if role == "garden":
        n = "ndGarden_%s" % key
        return _reg(n, lambda: AU.emit_program(n, col, gain, hh=1.4), "garden", key, "hh")
    if role == "lantern":
        n = "ndLantern_%s" % key
        return _reg(n, lambda: AU.emit_program(n, col, gain, ring=1.0, hh=0.5, toms=1.6), "garden", key, "lantern")
    if role == "keel":
        n = "ndKeel_%s" % key
        return _reg(n, lambda: AU.emit_program(n, col, gain, hh=0.7), "underlight", key, "hh")
    if role == "win_stair":   # the always-lit stair slots (art iteration 5): out of the lit/dark pane program
        n = "ndStair_%s" % key
        return _reg(n, lambda: AU.emit_program(n, col, gain), "practical", key, None)
    if role == "landmark":
        n = "ndBell"
        return _reg(n, lambda: AU.emit_program(n, col, gain), "bell", key, "bell")
    return None   # beam_lamp and the Condensery roles outside the works keep their colour


def pane_program(zclass, state):
    if zclass == "cond":
        t = [pal(k) for k in ("cond_a", "amber", "cond_c", "cond_b")]
        return _reg("ndPanesCond", lambda: AU.window_program("ndPanesCond", t, t, 3.0), None, "cond_a", "cond")
    if zclass == "lone":
        t = [pal("amber")] * 4
        return _reg("ndPanesLone", lambda: AU.window_program("ndPanesLone", t, t, 4.0), None, "amber", "lone")
    t = [pal(k, state) for k in ("win_a", "win_b", "win_c", "win_d")]
    return _reg("ndPanes", lambda: AU.window_program("ndPanes", t, t, 4.0, ring=0.6), "window", "win", "panes")


with open(os.path.join(HERE, "meshes", "kit_manifest.json")) as _f:
    KIT_MATERIALS = {a["asset"]: set(a["materials"]) for a in json.load(_f)["assets"] if a.get("materials")}


def part_materials(tint, state="uv", color=None, zclass="town", asset=""):
    has = KIT_MATERIALS.get(asset)   # only the roles the asset has (an unknown asset: all of them)
    pm = {name: {"program": pane_program(zclass, state)} for name in PANES if has is None or name in has}
    for role, default in TINT_DEFAULTS.items():
        if role in PANES or (has is not None and role not in has):
            continue
        key = tint.get(role, default)
        entry = {"emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": 1.0}
        prog = role_program(role, key, zclass, asset, state)
        if prog is None:
            entry = {"emissiveColor": pal(key, state), "emissiveIntensity": ROLE_GAIN[role]}
        else:
            entry["program"] = prog
        pm[role] = entry
    for role, rgb in (color or {}).items():   # ADR-2102: the layout's per-instance base colour (awning cloth)
        pm.setdefault(role, {})["baseColor"] = r3(rgb)
    return pm


def kit_nodes(state="uv"):
    """One procedural node per (asset, tint, colour, district) group of the layout's instances."""
    groups = {}
    for inst in LAYOUT["instances"]:
        tint = inst.get("tint", {})
        color = json.dumps(inst.get("color", {}), sort_keys=True)
        key = (inst["asset"], tuple(sorted(tint.items())), color, zclass_of(inst.get("zone", "")), inst.get("bob", ""))
        groups.setdefault(key, []).append(inst)
    nodes = []
    used = set()
    for (asset, tint, color, zclass, bob), insts in sorted(groups.items()):
        base = os.path.splitext(os.path.basename(asset))[0]
        name = base
        k = 1
        while name in used:
            name = "%s_%d" % (base, k)
            k += 1
        used.add(name)
        if asset.endswith("great_lantern_bell.glb"):
            NODE_OF["bell"] = name
        pts = [r3(i["pos"]) + r3(i["quat"]) + r3(i["scale"]) for i in insts]
        shadow = asset.startswith("buildings/") or asset.startswith("landmarks/")
        nodes.append({"name": name, "kind": "procedural", "position": [0, 0, 0], "procedural": {
            "source": {"kind": "mesh", "asset": MESH + asset},
            "distribution": {"kind": "points", "points": pts},
            "castsShadow": shadow,
            "partMaterials": part_materials(dict(tint), state, json.loads(color), zclass, asset)}})
        if bob:
            nodes[-1]["bob"] = bob   # ADR-2112: buildings on a bobbing island move with it and its cloud
    return nodes


def light_nodes(state="uv"):
    """The layout's practical lights: small emissive bulbs are in the kit; these are the light they cast."""
    out = []
    for l in LAYOUT["lights"]:
        rad = l.get("radius_m", 9.0)
        out.append({"name": l["name"], "type": "point", "position": r3(l["pos"]), "color": pal(l["key"], state),
                    "intensity": round(0.45 * rad * rad * l.get("intensity", 1.0), 2), "range": rad * 2.5,
                    "castsShadow": False, "volumetric": 0.0})
        if l.get("bob"):
            out[-1]["node"] = "bob_" + l["bob"]   # ADR-2112: a lamp on a bobbing island rides its formation's group
    return out


def bob_nodes():
    """ADR-2112: one empty group node per formation, at the origin, riding it (lights ride these)."""
    return [{"name": "bob_" + b["name"], "kind": "group", "position": [0.0, 0.0, 0.0], "bob": b["name"]}
            for b in DESIGN.get("bobs", [])]


# ---------------------------------------------------------------------------------------------------------------
# the inhabitants: the Glowmere cast at the layout's spots and on its paths
CAST = ["alien-scout", "alien-elder", "alien-diver", "alien-pilot", "alien-ranger", "alien-trooper"]


def inhabitant_nodes(max_spots=14, scale=None):
    inh = LAYOUT["inhabitants"]
    s = scale if scale is not None else inh.get("scale", 1.0)
    r = random.Random(77)
    nodes, tracks = [], []
    k = 0

    def node(name, pos, yaw_deg, state, bob=None):
        return {**({"bob": bob} if bob else {}), "name": name, "kind": "gltf", "asset": ALIENS + CAST[k % len(CAST)] + ".glb", "position": r3(pos),
                "rotation": [0.0, round(yaw_deg, 1), 0.0], "scale": [s, s, s],
                "animation": {"state": state, "blend": 0.3, "speed": round(r.uniform(0.85, 1.1), 3), "updateHz": 30,
                              "nearDistance": 40.0, "farHz": 15.0, "cullDistance": 300.0}}
    spots = sorted([sp for sp in inh["spots"] if sp["priority"] == 1], key=lambda sp: sp["id"])
    step = max(1, len(spots) // max_spots)
    for sp in spots[::step][:max_spots]:
        f = sp["forward"]
        nodes.append(node("alien%02d" % k, sp["pos"], math.degrees(math.atan2(f[0], f[2])),
                          r.choice(sp.get("clips", ["Idle"])), sp.get("bob")))
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
            nodes.append(node(name, pk[0]["value"], rk[0]["value"][1] if rk else 0.0, p.get("clip", "Walking"), p.get("bob")))
            tracks.append({"target": "nodes/%s/position" % name, "component": -1, "enabled": True,
                           "timeBase": "seconds", "mode": "replace", "loopLength": 0.0, "keys": pk})
            tracks.append({"target": "nodes/%s/rotation" % name, "component": -1, "enabled": True,
                           "timeBase": "seconds", "mode": "replace", "loopLength": 0.0, "keys": rk})
            k += 1
    return nodes, tracks


CROWD_TEST = 0   # --crowd-test N: a capacity probe (N aliens at every spot, then clones beside them); not the film


PHASES = 8                        # ADR-2116: a crowd's phases (clip length / 8), so rigs in step can share a pose
SPEEDS = (0.92, 1.0, 1.08)        # and its few clip rates, for the same reason


def clip_lengths():
    """Clip seconds per clip name, read from one cast GLB (the pack is shared); {} without the assets."""
    import struct
    try:
        with open(os.path.join(HERE, "scene", ALIENS, CAST[0] + ".glb"), "rb") as f:
            b = f.read()
    except OSError:
        return {}
    n = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + n])
    return {a["name"]: max(j["accessors"][sm["input"]]["max"][0] for sm in a["samplers"]) for a in j.get("animations", [])}


CLIP_LEN = clip_lengths()


def crowd_phase(r, clip):
    return round(CLIP_LEN.get(clip, 2.0) * r.randrange(PHASES) / PHASES, 4)


def crowd_test_nodes(n):
    """Iteration 10 capacity probe (msg 112): every spot, two walkers a path, then clones 1.1 m beside spots."""
    inh = LAYOUT["inhabitants"]
    nodes, tracks = inhabitant_nodes(max_spots=10 ** 6)
    spots = sorted(inh["spots"], key=lambda sp: (sp["priority"], sp["id"]))
    r = random.Random(5)
    k = 0
    while len(nodes) < n:
        sp = spots[k % len(spots)]
        ring = 1 + k // len(spots)
        f = sp["forward"]
        side = [f[2], 0.0, -f[0]]
        a = r.uniform(0, 2 * math.pi)
        pos = [sp["pos"][0] + 1.1 * ring * (math.cos(a) * side[0] + math.sin(a) * f[0]), sp["pos"][1],
               sp["pos"][2] + 1.1 * ring * (math.cos(a) * side[2] + math.sin(a) * f[2])]
        nodes.append({**({"bob": sp["bob"]} if sp.get("bob") else {}), "name": "alienc%03d" % k, "kind": "gltf",
                      "asset": ALIENS + CAST[k % len(CAST)] + ".glb", "position": r3(pos),
                      "rotation": [0.0, round(r.uniform(-180, 180), 1), 0.0], "scale": [1.0, 1.0, 1.0],
                      "animation": dict(nodes[0]["animation"], state=(c := r.choice(sp.get("clips", ["Idle"]))),
                                        speed=r.choice(SPEEDS), phase=crowd_phase(r, c), updateHz=0)})
        k += 1
    return nodes, tracks


WALK_STRIDE = 1.34   # m/s a Walking stance foot sweeps back at clip rate 1 (alien-pilot's feet: work/engine/it10/fly/stride.py)
TURN_RATE = 225.0    # degrees a second a walker turns on the spot at a path's corner (walking in place)


def population_nodes():
    """Iteration 10 (owner msg 112, ADR-2116): the whole cast. Every spot in the layout (all priorities), every walker
    on every path, riders on vehicles; phases and rates from a few buckets so rigs in step share a pose. Reads the
    art's population extensions when present: `cast`, `phase`, `speed`, `group`, `activity`, `bob`, `ride`."""
    inh = LAYOUT["inhabitants"]
    scale = inh.get("scale", 1.0)
    r = random.Random(112)
    nodes, tracks = [], []
    counter = [0]

    def node(name, pos, yaw_deg, clip, sp):
        k = counter[0]
        counter[0] += 1
        cast = sp.get("cast") or CAST[k % len(CAST)]
        anim = {"state": clip, "blend": 0.3, "speed": sp.get("speed", r.choice(SPEEDS)),
                "phase": sp.get("phase", crowd_phase(r, clip)), "updateHz": 0,
                "nearDistance": 40.0, "farHz": 15.0, "cullDistance": 400.0}
        n = {"name": name, "kind": "gltf", "asset": ALIENS + cast + ".glb", "position": r3(pos),
             "rotation": [0.0, round(yaw_deg, 1), 0.0], "scale": [scale] * 3, "animation": anim}
        if sp.get("bob"):
            n["bob"] = sp["bob"]
        if sp.get("ride"):
            n["parent"] = sp["ride"]
        return n
    spots = sorted(inh["spots"], key=lambda sp: (sp.get("priority", 1), sp["id"]))
    for sp in spots:
        f = sp.get("forward")
        yaw = math.degrees(math.atan2(f[0], f[2])) if f else 90.0 - sp.get("facing_deg", 0.0)
        clips = sp.get("clips") or ["Idle"]
        nodes.append(node("alien_%s" % sp["id"], sp["pos"], yaw, r.choice(clips), sp))
    for p in inh["paths"]:
        pts = p["points"]
        route = pts + pts[-2::-1] if p.get("mode", "pingpong") == "pingpong" else pts + pts[:1]
        speed = p.get("speed_mps", 1.2)
        rate = round(speed / WALK_STRIDE, 3)           # the feet sweep at the ground's speed: no skating
        # the timing of one lap: walk each leg, turn on the spot at each corner
        legs = []
        for i in range(len(route) - 1):
            d = [route[i + 1][j] - route[i][j] for j in range(3)]
            legs.append((math.dist(route[i], route[i + 1]), math.degrees(math.atan2(d[0], d[2]))))
        lap, sched = 0.0, []
        for i, (dist, yaw) in enumerate(legs):
            prev = legs[i - 1][1] if i > 0 else legs[-1][1]
            turn = abs((yaw - prev + 180) % 360 - 180)
            tt = max(0.12, turn / TURN_RATE) if turn > 2 else 0.0
            sched.append((lap, tt, dist / speed, i, prev, yaw))
            lap += tt + dist / speed
        walkers = max(1, int(p.get("walkers", 1)))
        for w in range(walkers):
            name = "alien_%s_%d" % (p["id"], w)
            offset = (w + r.uniform(0.15, 0.85)) / walkers * lap
            pk, rk = [], []
            t0 = -offset
            while t0 < SONG + 1 + lap * 0:
                if t0 + lap < 0:
                    t0 += lap
                    continue
                for start, tt, wt, i, prev, yaw in sched:
                    a = t0 + start
                    while yaw - prev > 180:
                        yaw -= 360
                    while yaw - prev < -180:
                        yaw += 360
                    pk.append({"time": round(a, 3), "value": r3(route[i]), "interp": "linear"})
                    rk.append({"time": round(a, 3), "value": [0.0, round(prev, 1), 0.0], "interp": "linear"})
                    pk.append({"time": round(a + tt + 1e-3, 3), "value": r3(route[i]), "interp": "linear"})
                    rk.append({"time": round(a + tt + 1e-3, 3), "value": [0.0, round(yaw, 1), 0.0], "interp": "linear"})
                    rk.append({"time": round(a + tt + wt - 2e-3, 3), "value": [0.0, round(yaw, 1), 0.0], "interp": "linear"})
                t0 += lap
            end = t0  # one more vertex after the song's end keeps the last leg's motion
            pk.append({"time": round(end, 3), "value": r3(route[0]), "interp": "linear"})
            pk = clip_keys(sorted(pk, key=lambda kk: kk["time"]))
            rk = sorted(rk, key=lambda kk: kk["time"])
            for q in range(1, len(rk)):   # one continuous yaw: never a full spin between two keys
                y, py = rk[q]["value"][1], rk[q - 1]["value"][1]
                rk[q]["value"][1] = round(py + ((y - py + 180) % 360 - 180), 1)
            rk = clip_keys(rk)
            sp = dict(p, speed=rate)
            n = node(name, pk[0]["value"], rk[0]["value"][1], p.get("clip", "Walking"), sp)
            n["animation"]["speed"] = rate
            nodes.append(n)
            tracks.append({"target": "nodes/%s/position" % name, "component": -1, "enabled": True,
                           "timeBase": "seconds", "mode": "replace", "loopLength": 0.0, "keys": pk})
            tracks.append({"target": "nodes/%s/rotation" % name, "component": -1, "enabled": True,
                           "timeBase": "seconds", "mode": "replace", "loopLength": 0.0, "keys": rk})
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
# iteration 10 (owner msg 117): jetpack flights. The layout's "flights" when the art has written them, else the
# engine's prototype. Only the cast with a jetpack mesh (Backpack_Jet: alien-pilot, alien-ranger) fly.
ENGINE_FLIGHTS = [
    {"name": "flyer00", "cast": "alien-pilot", "from": "spot_066", "to": "spot_077", "t0": 29.6, "td": 35.9, "alt": 9.0},
]
SEQUENCE_ACTORS = []


def spot_place(ref):
    if isinstance(ref, dict):
        return {"pos": ref["pos"], "yaw": ref.get("yaw", 0.0)}
    sp = next(s for s in LAYOUT["inhabitants"]["spots"] if s["id"] == ref)
    f = sp["forward"]
    return {"pos": sp["pos"], "yaw": math.degrees(math.atan2(f[0], f[2]))}


def flight_nodes(state="uv"):
    nodes, lights, tracks = [], [], []
    SEQUENCE_ACTORS.clear()
    for fdat in LAYOUT.get("flights", ENGINE_FLIGHTS):
        f = dict(fdat, asset=ALIENS + fdat.get("cast", "alien-pilot") + ".glb", **{"from": spot_place(fdat["from"]),
                                                                                  "to": spot_place(fdat["to"])})
        n, l, t, actor = FL.flight(f, r3, SONG, pal("amber", state), pal("amber", state))
        nodes += n
        lights += l
        tracks += t
        SEQUENCE_ACTORS.append(actor)
    return nodes, lights, tracks


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
# lighthouse beams: ADR-2102 cloud beams (soft cones marched with the cloud: they scatter in the clear air, are hidden
# behind cloud and light the cloud they cross), turning at the layout's rpm
BEAM_GAIN = 1.0


def cloud_beams(state="uv"):
    out = []
    for b in LAYOUT["beams"]:
        col = pal(b["key"], state)
        out.append({"name": b["name"], "origin": r3(b["origin"]), "count": b["count"],
                    "yaw": round(b.get("phase", 0.0) * 360.0, 2), "elevation": b["elevation_deg"], "rpm": b["rpm"],
                    "length": b["length_m"], "halfAngle": max(b["half_angle_deg"], 1.8), "radius": 1.2,
                    "color": col, "intensity": round(BEAM_GAIN * b.get("intensity", 1.0), 3)})
        if b.get("bob"):
            out[-1]["bob"] = b["bob"]   # ADR-2112: a lighthouse on a floating island rides it
    return out


# ---------------------------------------------------------------------------------------------------------------
# clouds: the design's lobes on the v2 block with the relief sea
# glows the beams replace (the layout's cloud_glow stand-ins for the lighthouse beams)
BEAM_STANDINS = {"harbour_light", "outer_light", "break_light"}
# the Condensery's keel glow lit one lobe of its column into a pink ball under the platform at the reveal
# ADR-2112: the design's glows that stand on a bobbing formation but do not say so themselves (art request, it9)
GLOW_BOB = {"condensery_throats": "condensery", "condensery_yard": "condensery", "condensery_keel": "condensery",
            "lone_lamp": "lone_lamp", "highwater": "highwater"}
GLOW_SCALE = {"condensery_keel": 0.35, "condensery_throats": 2.2, "veil_pocket5": 0.6, "veil_amber": 0.6}
GLOW_SHADOW = {"veil_pocket5": 0.15, "veil_amber": 0.15}   # ADR-2107: the art's pulses on the journey inside the plug


def clouds_block(state="uv", moon_dir=None):
    # ADR-2112: the art's bobbing formations ({name: {amplitude, period, phase, liftMax, masses}}); every lobe of a
    # listed mass rides it (a lobe's own "bob" wins)
    forms = DESIGN.get("formations", {})
    mass_bob = {m: f for f, d in forms.items() for m in d.get("masses", [])}
    lobes = []
    for name, ls in DESIGN["masses"].items():
        lobes += [dict(l, erode=MASS_ERODE[name]) if name in MASS_ERODE else dict(l) for l in ls]
        if name in mass_bob:
            for l in lobes[len(lobes) - len(ls):]:
                l.setdefault("bob", mass_bob[name])
    far = []   # ADR-2104: the far towers on their own grid (in one set they overflowed 18 hero cells)
    for name, ls in DESIGN.get("far_masses", {}).items():
        far += [dict(l) for l in ls]
        if name in mass_bob:
            for l in far[len(far) - len(ls):]:
                l.setdefault("bob", mass_bob[name])
    # iteration 7 (ADR-2107): the erosion eats every lobe inward; the plug that hides the Condensery until the crash
    # keeps most of its body (eroded fully it opened at 56.4 s, half a second early)
    for l in lobes + far:
        if l.get("tag") in LOBE_ERODE:
            l["erode"] = LOBE_ERODE[l["tag"]]
        if l.get("tag") in LOBE_DENSITY:
            l["density"] = round(l.get("density", 1.0) * LOBE_DENSITY[l["tag"]], 3)
    cb = DESIGN["clouds_block"]
    glows = [{"name": g["name"], "position": g["position"], "color": pal(g["key"], state), "radius": g["radius"],
              "intensity": g["intensity"] * GLOW_SCALE.get(g["name"], 1.0),
              **({"bob": g.get("bob") or GLOW_BOB[g["name"]]} if (g.get("bob") or g["name"] in GLOW_BOB) else {})}
             for g in cb["glows"] if g["name"] not in BEAM_STANDINS]
    known = {b["name"] for b in DESIGN.get("bobs", [])}
    for g in glows:   # a formation the design does not (yet) have: the glow stays put
        if g.get("bob") and g["bob"] not in known:
            g.pop("bob")
    # iteration 6: the Veil is the art's (cloud design v3: rooms built as absences in masses.veil, a plug before the
    # reveal); its glows come from the design under the names the routes use. The generated passage is gone.
    veil_glows = DESIGN.get("veil", {}).get("glows", [])
    for g in veil_glows:
        glows.append({"name": g["name"], "position": g["position"], "color": pal(g["key"], state), "radius": g["radius"],
                      "intensity": g["intensity"] * GLOW_SCALE.get(g["name"], 1.0),
                      "shadow": GLOW_SHADOW.get(g["name"], 1.0)})
    # iteration 7: the Condensery's underglow. A glow in the well under each stack (the art's `veil.wells`, at the
    # stacks' own plan positions from the layout), a little under the deck: it lights the heads round the well from
    # inside and below, so the pedestal reads lit by the works and not as one ember. Named condensery_* so they take
    # the ember route and the hold until the crash.
    for k, inst in enumerate(sorted((i for i in LAYOUT["instances"] if "cond_stack_" in i.get("asset", "")),
                                    key=lambda i: i["asset"])):
        x, y, z = inst["pos"]
        glows.append({"name": "condensery_well_%s" % "abc"[k], "position": [x, y + WELL_GLOW["lift"], z],
                      **({"bob": inst["bob"]} if inst.get("bob") else {}),
                      "radius": WELL_GLOW["radius"],
                      "color": pal("cond_a", state), "intensity": WELL_GLOW["intensity"], "key": "cond_a",
                      "shadow": WELL_GLOW["shadow"]})
    # iteration 7: the plug (54.9-56.9 s) was 1.5 s of flat colour, which is what the inside of an opaque cloud is. Lit
    # pockets placed beside the journey through it, alternating sides: as the camera passes them their light moves
    # across the cloud's billows (glowBreakup/glowShadow), giving the blind stretch depth and motion.
    for k, (t, side, up) in enumerate(PLUG_POCKETS):
        pos, fwd = cam_at(t)
        lat = [-fwd[2], 0.0, fwd[0]]
        n = math.hypot(lat[0], lat[2]) or 1.0
        glows.append({"name": "veil_plug%d" % k, "key": "garden_b" if k % 2 else "garden_a",
                      "position": [round(pos[0] + side * lat[0] / n, 2), round(pos[1] + up, 2),
                                   round(pos[2] + side * lat[2] / n, 2)],
                      "radius": PLUG_GLOW["radius"], "intensity": PLUG_GLOW["intensity"], "shadow": PLUG_GLOW["shadow"],
                      "color": pal("garden_b" if k % 2 else "garden_a", state)})
    GLOW_INDEX.clear()
    GLOW_BASE.clear()
    GLOW_BASE.update({g["name"]: g["intensity"] for g in glows})
    GLOW_INDEX.update({g["name"]: i for i, g in enumerate(glows)})
    GLOW_KEYS.clear()
    GLOW_KEYS.update({g["name"]: g["key"] for g in cb["glows"]})
    GLOW_KEYS.update({g["name"]: g["key"] for g in veil_glows})
    GLOW_KEYS.update({g["name"]: g["key"] for g in glows if "key" in g})
    for g in glows:
        g.pop("key", None)
    sea = DESIGN["sea_design"]
    br = sea["break"]
    lo, hi = sea["top_range"]
    out = {
        "lobes": lobes,
        "farLobes": far,
        "sea": {"base": sea["base"], "top": sea["top_mean"], "coverage": 1.0, "coverageScale": 0.0003,
                "density": 1.6, "relief": round((hi - lo) / 2.4, 1), "reliefScale": 2000.0, "billow": 24.0,
                "farFade": 0.35,
                "breaks": [{"centre": br["centre"], "radius": br["radius"], "elongation": br["elongation"],
                            "yaw": br["yaw_rad"]}]},
        "glows": glows, "glowGain": CLOUD_LOOK["glowGain"], "glowOcclusion": CLOUD_LOOK["glowOcclusion"],
        # ADR-2104: the glows light the cloud's structure (pockets and the billows facing them), not spheres
        "glowBreakup": CLOUD_LOOK["glowBreakup"], "glowShadow": CLOUD_LOOK["glowShadow"],
        "glowBreakupScale": 0.014, "glowShadowDistance": 16.0,
        "extinction": CLOUD_LOOK["extinction"], "density": 1.0, "swell": 0.0,
        "steps": CLOUD_Q["steps"], "lightSteps": CLOUD_Q["lightSteps"], "lightDistance": CLOUD_LOOK["lightDistance"],
        "resolution": CLOUD_Q["resolution"], "temporal": CLOUD_Q["temporal"], "minStep": CLOUD_Q["minStep"],
        "stepGrowth": CLOUD_Q["stepGrowth"], "maxDistance": 24000.0,
        "lightDirection": r3(LIGHT_DIR), "lightColor": pal("moon_light", state),
        "lightIntensity": CLOUD_LOOK["lightIntensity"], "anisotropy": CLOUD_LOOK["anisotropy"],
        "ambientTop": [v * CLOUD_LOOK["ambientTop"] for v in pal("sky_horizon", state)],
        "ambientBottom": [v * CLOUD_LOOK["ambientBottom"] for v in pal("haze", state)],
        "warmColor": [v * CLOUD_LOOK["warm"] for v in pal("horizon_glow", state)], "warmDirection": r3(cb["warmDirection"]),
        "warmHeight": 900.0,
        "noiseScale": 0.018, "noiseAmount": CLOUD_LOOK["noiseAmount"], "detailScale": 0.07, "detailAmount": 0.6,
        # ADR-2106: each lobe's surface eaten by billow octaves (cauliflower crowns, wispy flat bases); the key's light
        # through six squared taps with multiple scattering (two even taps cast every lobe as a black stencil)
        "erosion": CLOUD_LOOK["erosion"], "erosionScale": CLOUD_LOOK["erosionScale"], "erosionDetail": 1.0,
        "erosionCurl": 6.0, "erosionFlatten": 0.4, "erosionOctaves": CLOUD_Q["erosionOctaves"],
        # ADR-2115 (owner msg 105: the wall behind the garden "too blobby"): a crisper eroded edge
        "erosionEdge": CLOUD_LOOK["erosionEdge"],
        "lightSpacing": 2, "multiScatter": CLOUD_LOOK["multiScatter"],
        "drift": r3([v * 0.7 for v in cb["drift"]]),
        "hazeColor": pal("haze", state), "hazeDistance": 9000.0, "powder": CLOUD_LOOK["powder"], "seed": 7,
        # ADR-2102: the lighthouses
        "beams": cloud_beams(state), "beamAir": CLOUD_LOOK["beamAir"], "beamCloud": CLOUD_LOOK["beamCloud"],
    }
    bobs = [{"name": f, "amplitude": d.get("amplitude", [0.0, 2.0, 0.0]), "period": d.get("period", 8.0),
             "phase": d.get("phase", 0.0), "liftMax": d.get("liftMax", 0.0)} for f, d in sorted(forms.items())]
    bobs += [{k: b[k] for k in ("name", "amplitude", "period", "phase", "liftMax") if k in b}
             for b in DESIGN.get("bobs", [])]   # ADR-2112: the art's table (it9)
    if bobs:
        out["bobs"] = bobs
        names = {b["name"] for b in bobs}
        for l in lobes + far:
            if l.get("bob") and l["bob"] not in names:
                l.pop("bob")
    # ADR-2113: the wisp layer (the engine's first setting, under the art's `wisps` from the cloud design when it has one)
    w = dict(WISPS, **DESIGN.get("wisps", {}))
    if DESIGN.get("wisp_regions"):   # it9: the art's regions over the town replace the engine's first lens
        w["zones"] = [{k: r[k] for k in ("name", "centre", "radii", "yaw", "floor", "below", "above", "density", "bob")
                       if k in r} for r in DESIGN["wisp_regions"]]
    if w.get("enabled") and not NO_WISPS:
        out["wisps"] = w
    if moon_dir is not None:
        out["halo"] = {"direction": r3(moon_dir), "color": [v * 0.42 for v in pal("moon_disc", state)],
                       "radius": 3.0, "intensity": CLOUD_LOOK["halo"]}
    return out


# the cloud look (iteration 4: dark indigo bodies, rims toward the key, the city's light on them)
# iteration 7: key 0.11 -> 0.22 with six squared light taps (the two even taps let most crowns see the key unshadowed)
# iteration 9b (ADR-2115, owner msg 105): billows 60 -> 40 m and the eroded edge at half its width: at 50-150 m a 40 m
# lobe read as a soft round ball. The seat lobes keep their 0.3 share (no detail floor): a floor sank the decks' cloud.
CLOUD_LOOK = {"erosion": 1.0, "erosionScale": 40.0, "erosionEdge": 0.5, "multiScatter": 0.7, "noiseAmount": 0.72,
              "lightIntensity": 0.22, "anisotropy": 0.35, "lightDistance": 260.0, "ambientTop": 0.25,
              "ambientBottom": 0.06, "warm": 0.15, "powder": 0.85, "glowGain": 0.30, "beamAir": 0.06, "beamCloud": 0.9,
              "halo": 0.45, "glowBreakup": 0.75, "glowShadow": 1.2,
              # iteration 6: occlusion is per metre of extinction, so at 7.5x the extinction 1.6 buried every glow
              # inside its own cloud (the Condensery's wells read black); 0.8 lets the throats underlight the pedestal and keeps the Veil dark
              "glowOcclusion": 0.8,
              # iteration 6: the art's cloud design v3 is built for 0.15 ("designed_for"); at 0.02 every formation
              # read as fog
              "extinction": DESIGN.get("designed_for", {}).get("extinction", 0.02)}
# iteration 8: the cloud march's cost knobs, by quality. "offline" is the look every render so far was judged on; "live"
# is the 1080p30 soft target's preset (build.py --quality live writes neon-dreams-live.{scene.json,json} beside it)
CLOUD_QUALITY = {
    # iteration 9 (ADR-2111): 72 -> 128 steps. Leaving the Veil's plug, the rays spent their 72 on the plug and ended
    # before the cloud beyond it, each pixel a jittered step short or long: that cloud came in as a stipple for 0.2 s
    "offline": {"steps": 128, "lightSteps": 6, "resolution": 0.5, "temporal": 0.85, "minStep": 3.0, "stepGrowth": 0.02,
                "erosionOctaves": 3},
    # iteration 8, measured (--live-profile headless, 1080p, Medium): 31-42 ms against 80-115 ms for the offline preset
    # at Ultra. The march at 0.4 resolution, 40 steps growing 3.5 % of the distance, three light taps, two erosion octaves
    "live": {"steps": 40, "lightSteps": 3, "resolution": 0.4, "temporal": 0.9, "minStep": 3.0, "stepGrowth": 0.035,
             "erosionOctaves": 2},
}
CLOUD_Q = dict(CLOUD_QUALITY["offline"])
# live: no air march (8 ms; the air has no in-scatter, so the closed-form surface fog alone is within 1/255)
ENV_QUALITY = {"offline": {}, "live": {"volumeMaxDistance": 0.0}}   # environment keys by quality (merged over scene_doc's env)
ENV_Q = {}
KEY_SHADOW = {"offline": True, "live": False}   # live: the moon's shadow map is 12.7 ms of a 33 ms frame   # the moon key's shadow map, by quality
KEY_Q = {"shadow": True}
AIR = {"density": 0.0003, "scattering": 0.0, "fogSky": 0.5}   # extinction = density x volumeAbsorption 0.5
# iteration 9: the Condensery stands on a rock island; 6 m under the deck is inside the rock, so the wells sit 20 m over it
WELL_GLOW = {"radius": 95.0, "intensity": 6.0, "lift": 20.0, "shadow": 0.35}   # iteration 7: the Condensery's wells
LOBE_ERODE = {"plug": 0.3, "seat": 0.3}
MASS_ERODE = {"condensery_plume": 0.25}   # iteration 7: fully eroded, the stacks' thin plumes were eaten away   # ADR-2107: a lobe's share of the erosion, by the art's tag
# iteration 7: inside an opaque cloud the camera sees ~7 m of uniform colour (the flat card at 55-56.5 s). At 0.45 of
# its density the plug still holds an optical depth of ~5 over its 100 m, but the eye sees 20 to 40 m into it: its
# eroded billows and the pockets' light pass by
LOBE_DENSITY = {"plug": 1.0}
# (t, side m, up m). Iteration 7: none -- glows inside the plug lit it uniformly from within a few metres and the
# bloom made it a flat bright card; the art's two pulses on the journey (veil_pocket5, veil_amber) are kept, dimmed
PLUG_POCKETS = []
# ADR-2113: the wisp layer's first setting (iteration 9). Density multiplies the cloud's extinction (0.15/m) where the
# torn sheets are; between them it is clear, so it reads as strands, not a grey medium (density 0.04-0.25 on 70 m sheets
# was a flat veil). The band sits over the sea's mean top; the walking cast are its wakes. The art's cloud design `wisps` (same keys, plus zones round island rims) overrides any of it.
WISPS = {"enabled": True, "base": 0.0, "top": 0.0, "density": 1.8, "scale": 10.0, "coverage": 0.55, "tatter": 0.65,
         "stretch": 3.0, "drift": [0.9, 0.0, 0.5], "wakeRadius": 1.2, "wakeStrength": 0.8, "wakeSwirl": 1.4,
         "wakeHeight": 2.2, "wakeTrail": 2.6, "wakes": ["alien*", "flyer*"],
         # no band (the art: nowhere over open sea); the first lens, used when the design has no wisp_regions
         "zones": [{"name": "mooring", "centre": [0.0, 10.0], "floor": 131.0, "radii": [330.0, 280.0], "yaw": 0.0,
                    "below": 30.0, "above": 6.0, "density": 1.0}]}
NO_WISPS = False
PLUG_FADE = (55.7, 56.45, 0.3)   # iteration 9: the plug's light dims from 55.7 s to 30 % by 56.45 s, back by 57.95
PLUG_GLOW = {"radius": 20.0, "intensity": 3.0, "shadow": 0.1}   # ADR-2107: shadow 0.1, they sit in solid cloud


def cam_at(t):
    """The journey's camera position at timeline second t and its unit heading (from the same spline the camera follows)."""
    dense, _, total, s_at = journey()
    s = s_at(t)
    acc = 0.0
    for k in range(1, len(dense)):
        d = math.dist(dense[k - 1], dense[k])
        if acc + d >= s or k == len(dense) - 1:
            u = (s - acc) / d if d > 0 else 0.0
            p = [dense[k - 1][i] + (dense[k][i] - dense[k - 1][i]) * u for i in range(3)]
            f = [(dense[k][i] - dense[k - 1][i]) / max(d, 1e-6) for i in range(3)]
            return p, f
        acc += d
GLOW_INDEX = {}   # glow name -> index in clouds.glows (route targets)
GLOW_KEYS = {}    # glow name -> palette key
GLOW_BASE = {}    # glow name -> its rest intensity


# ---------------------------------------------------------------------------------------------------------------
# the journey: the layout's waypoints (engine frame)
# The layout's waypoints (iteration 5: the art data now carries the iteration-4 tail through the Veil).
WAYPOINTS = [(w["t"], w["pos"]) for w in LAYOUT["camera"]["waypoints"]]


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


def journey(full=False):
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
    if full:
        return dense, keys, total, s_at, dense, total
    return dense, keys, total, s_at



# ---------------------------------------------------------------------------------------------------------------
STILL_TIMES = [2.8, 17.0, 35.75, 50.5, 58.95, 66.0]
MOON_ARGS = {}
BELL_POS = next(i["pos"] for i in LAYOUT["instances"] if i["asset"].endswith("great_lantern_bell.glb"))


def fov_track():
    """The lens closes from 52 to 36 degrees as the camera leaves the Veil, so the Condensery (1.2-1.6 km out, the
    reveal's subject) fills the frame without flying closer than the song allows; it eases in over the thinning."""
    keys = [(0.0, 52.0), (54.0, 52.0), (55.5, 50.5), (57.0, 45.0), (58.95, 39.0), (62.0, 36.5), (SONG, 35.0)]
    return {"target": "camera/fov", "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "keys": [{"time": t, "value": v, "interp": "linear"} for t, v in keys]}


def scene_doc(state="uv", sky_map=None):
    nodes = kit_nodes(state)
    # the Drowned Moon (MOON_DISC: a backdrop right of the journey), far out
    md = bearing_dir(MOON_DISC["bearing"], MOON_DISC["elevation"])
    D = 60000.0  # placed by its point: a node position clamps at 10 km (nodes/<n>/position)
    R = D * math.tan(math.radians(MOON_DISC["radius"]))
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
        "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndMoon",
                     "alphaMode": "mask", "alphaCutoff": 0.5}}})
    # the cosmic ocean far under the break
    # iteration 7 (art round 4): where the art's `ocean_plane` puts it -- an ellipse a little larger than the break, just
    # under the sea's floor, so the reveal (looking 9 to 20 degrees down) sees the ocean in the break, not its far wall
    brk = DESIGN["sea_design"]["break"]
    br = brk["centre"]
    op = brk.get("ocean_plane", {"y": -900.0, "semi_axes": [900.0, 900.0], "yaw_rad": 0.0})
    ya = op.get("yaw_rad", 0.0)
    nodes.append({"name": "cosmicOcean", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "cylinder", "radius": 1.0, "height": 40.0, "radialSegments": 96, "caps": True},
        "distribution": {"kind": "points", "points": [[br[0], op["y"] - 20.0, br[1], 0.0, round(math.sin(ya / 2), 5), 0.0,
                                                       round(math.cos(ya / 2), 5), op["semi_axes"][0], 1.0,
                                                       op["semi_axes"][1]]]},
        "castsShadow": False,
        "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndOcean"}}})
    aliens, alien_tracks = population_nodes() if not CROWD_TEST else crowd_test_nodes(CROWD_TEST)
    vehicles, vehicle_tracks = vehicle_nodes(state)
    flyers, flyer_lights, flyer_tracks = flight_nodes(state)
    vehicle_tracks = vehicle_tracks + flyer_tracks
    nodes += aliens + flyers + vehicles + bob_nodes()
    programs = [
        moon_program("ndMoon", [v * MOON_GAIN for v in pal("moon_disc", state)], ml, 3.0 / R, md[1] * D, R),
        ocean_program("ndOcean", pal("ocean_a", state), pal("ocean_b", state), 0.0012, 2.5),
    ] + [PROGS[n]["doc"] for n in sorted(PROGS)]
    MOON_ARGS.update({"ml": ml, "R": R, "cy": md[1] * D})
    env = {
        "background": [0.001, 0.001, 0.004],
        "intensity": 0.15, "skyIntensity": 0.35, "lightFromEnvironment": False,
        "fogColor": pal("haze", state), "fogSky": AIR["fogSky"], "fogSkyDistance": 20000.0,
        "fogSkyBeyond": 40000.0,   # ADR-2108: the moon (a mesh 60 km out) is sky, not fogged
        # iteration 7 (art round 4, request 7): aerial perspective on the meshes -- the Condensery at 1.4 km, Highwater
        # at 2.2 km and the Long Arc at 22 km drew at full contrast beside cloud that fades with its hazeDistance. The
        # scene's own air (ADR-705), thin, with no in-scatter of its own (the cloud pass owns the visible air)
        "volumeDensity": AIR["density"], "volumeScattering": AIR["scattering"],
    }
    env.update(ENV_Q)
    if sky_map:
        env["map"] = sky_map
        env["rotation"] = 0.0
    lights = [{"name": "moon", "type": "directional", "role": "key", "direction": r3([-v for v in LIGHT_DIR]),
               "color": pal("moon_light", state), "intensity": 0.35, "castsShadow": KEY_Q["shadow"], "volumetric": 0.0},
              {"name": "skyfill", "type": "directional", "role": "fill", "direction": [0.3, -0.6, 0.4],
               "color": [0.25, 0.15, 0.7], "intensity": 0.08, "castsShadow": False, "volumetric": 0.0}]
    lights += light_nodes(state) + flyer_lights
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
        "clouds": clouds_block(state, md),
        # iteration 5: the kick ring leaves the Great Lantern's bell (kick stem onsets, feeds.historyOnsets)
        "fields": [AU.kick_ring_field(BELL_POS)],
    }
    return doc, [{"target": "camera/splineT", "component": -1, "enabled": True, "timeBase": "seconds",
                  "mode": "replace", "loopLength": 0.0, "keys": keys}, fov_track()] + alien_tracks + vehicle_tracks


def reactivity():
    """Iteration 5: the routes (stems -> systems) and the palette arc (timeline tracks). Returns (routes, tracks,
    sources). See nd_audio.py for the table."""
    R = AU.route
    routes, tracks = [], []
    # the bass stem's level sits at 0.4-0.8 while it plays: the routes hear the top of that range, so a phrase lifts
    # the bell and a rest lets it settle, instead of riding high all song
    shape = dict(gain=round(1 / 0.42, 4), offset=round(-0.38 / 0.42, 4), clampEnabled=True, clampMin=0.0, clampMax=1.0)
    lvl_slow = dict(attackMs=350.0, decayMs=1800.0, **shape)
    ember = dict(attackMs=600.0, decayMs=2600.0, **shape)
    hit = dict(attackMs=8.0, decayMs=260.0)
    G = GLOW_INDEX
    # --- BASS: the bell lifts and brightens (slow), its cloud glow, the cloud swell; the Condensery's ember
    routes += [
        R("stem.bass.level", "nodes/%s/position" % NODE_OF["bell"], 1.5, component=1, **lvl_slow),
        R("stem.bass.level", "material/ndBell/emissionIntensity", ROLE_GAIN["landmark"] * 0.25, **lvl_slow),
        R("stem.bass.level", "clouds/glow/%d/intensity" % G["great_lantern"], 0.6, **lvl_slow),
        R("stem.bass.level", "clouds/swell", 0.22, **lvl_slow),
    ]
    for k in ("keel_west", "keel_east"):
        if k in G:
            routes.append(R("stem.bass.level", "clouds/glow/%d/intensity" % G[k], 0.35, **lvl_slow))
    for k in sorted(G):   # the Condensery's glows (their names follow the art's cloud design)
        if k.startswith("condensery_"):
            routes.append(R("stem.bass.level", "clouds/glow/%d/intensity" % G[k], 0.9, **ember))
    for n, info in PROGS.items():
        if info["group"] == "cond":
            routes.append(R("stem.bass.level", "material/%s/emissionIntensity" % n,
                            0.4 * info["doc"]["emissionIntensity"], **ember))
    # --- kick: the ring is the kickRing onset field on the kick stem's onsets (feeds.historyOnsets); no route
    # --- snare: businesses A and B in turn (timeline.snareTurn steps 0, 1 on each snare onset); the gangway strips
    for n, info in PROGS.items():
        g = info["group"]
        if g in ("snareA", "snareB"):
            r = R("stem.snare.onset", "material/%s/emissionIntensity" % n, 1.1 * info["doc"]["emissionIntensity"],
                  attackMs=6.0, decayMs=320.0)
            r.update({"depthSource": "timeline.snareTurn", "depthMin": 1.0 if g == "snareA" else 0.0,
                      "depthMax": 0.0 if g == "snareA" else 1.0})
            routes.append(r)
        elif g == "gangway":
            routes.append(R("stem.snare.onset", "material/%s/emissionIntensity" % n,
                            1.4 * info["doc"]["emissionIntensity"], attackMs=6.0, decayMs=240.0))
    # --- HH: sparkle on the garden, the lantern bulbs, the keel lanterns; star twinkle
    for n, info in PROGS.items():
        if info["group"] in ("hh", "lantern") and "hh" in info["prog"].at:
            routes.append(R("stem.hh.onset", info["prog"].path("hh"), 1.0, **hit))
    routes.append(R("stem.hh.onset", "fx/stars/twinkle", 0.25, attackMs=10.0, decayMs=300.0))
    # --- toms: the lantern strings' ripple; the Veil's rose light and its pockets
    for n, info in PROGS.items():
        if info["group"] == "lantern":
            routes.append(R("stem.toms.onset", info["prog"].path("toms"), 1.0, attackMs=10.0, decayMs=420.0))
    # iteration 9 (owner msg 73: pops that do not read as music): the rose light no longer snaps the whole Veil pink
    # in one frame -- a softer swell (70 ms attack, lower peak) and the pockets after it in a slower travelling wave
    routes.append(R("stem.toms.onset", "clouds/glow/%d/intensity" % G["veil_rose"], 3.5, attackMs=70.0,
                    decayMs=650.0))
    for i in range(6):
        routes.append(R("stem.toms.onset", "clouds/glow/%d/intensity" % G["veil_pocket%d" % i], 2.4 + 0.5 * i,
                        attackMs=45.0, decayMs=450.0, delayMs=80.0 * i))
    for i in range(len(PLUG_POCKETS)):   # iteration 7: the plug's pockets answer the toms too
        routes.append(R("stem.toms.onset", "clouds/glow/%d/intensity" % G["veil_plug%d" % i], 3.0,
                        attackMs=8.0, decayMs=320.0, delayMs=30.0 * i))
    # --- crash: the sky's horizon lift and the lighthouses flaring (the palette steps are the tracks below)
    # iteration 9: the crash's sky lift and the lighthouse flare swell in (350 / 200 ms) instead of stepping the whole
    # frame up in one frame (the 2.8 s jump, owner msg 73)
    routes.append(R("stem.crash.onset", "env/skyIntensity", 0.16, attackMs=350.0, decayMs=2600.0))
    for i in range(len(LAYOUT["beams"])):
        routes.append(R("stem.crash.onset", "clouds/beam/%d/intensity" % i, 1.6, attackMs=200.0, decayMs=3200.0))
        routes.append(R("stem.cloud.level", "clouds/beam/%d/intensity" % i, 0.25, attackMs=800.0, decayMs=2500.0))
    # --- Moog: the windows (lit fraction +10 points, the tint mix, brightness)
    panes = PROGS["ndPanes"]["prog"]
    routes += [
        R("stem.moog.level", panes.path("litfrac"), 0.10, component=3, attackMs=250.0, decayMs=1200.0),
        R("stem.moog.level", panes.path("tintpow", "value"), -0.8, attackMs=250.0, decayMs=1500.0),
        R("stem.moog.level", "material/ndPanes/emissionIntensity", 1.2, attackMs=150.0, decayMs=900.0),
    ]
    # --- Cloud Diving: the clouds' inner glow, the nebula, the cosmic ocean, the Lone Lamp
    pad = dict(attackMs=900.0, decayMs=2400.0)
    routes += [
        R("stem.cloud.level", "clouds/glowGain", 0.10, **pad),
        R("stem.cloud.level", "env/skyIntensity", 0.10, **pad),
        R("stem.cloud.level", "material/ndOcean/emissionIntensity", 1.4, **pad),
    ]
    for n, info in PROGS.items():
        if info["group"] == "lone":
            routes.append(R("stem.cloud.level", "material/%s/emissionIntensity" % n,
                            1.2 * info["doc"]["emissionIntensity"], **pad))
    # --- tone block: beacons and roundels lit by the stem's level (dark while it rests), the lit quarter stepping
    for n, info in PROGS.items():
        if info["group"] in ("block_beacon", "block_roundel"):
            full = ROLE_GAIN["beacon"] * 0.9 if info["group"] == "block_beacon" else ROLE_GAIN["neon_a"] * 0.9
            routes.append(R("stem.block.level", "material/%s/emissionIntensity" % n, full, attackMs=20.0,
                            decayMs=180.0))
            routes.append(R("timeline.blockStep", info["prog"].path("chase", "value"), 0.75, op="replace"))
    # iteration 6: the alternation is a step timeline over the onsets AV Gen itself detects (audio/
    # sabertooth_engine_onsets.json), not a counting source: a count restarts at 0 on a seek, a timeline is a function of
    # time, so a seek lands on the same group play would. The k-th onset gives step k mod n of n even steps over [0, 1],
    # set half a frame before the onset's frame.
    with open(os.path.join(HERE, "audio", "sabertooth_engine_onsets.json")) as f:
        eng = json.load(f)
    half = 0.5 / eng["fps"]

    def steps(times, n):
        keys = [{"time": 0.0, "value": 0.0, "interp": "step"}]
        for k, t in enumerate(times):
            keys.append({"time": round(max(t - half, 0.0), 4), "value": round((k % n) / (n - 1), 4), "interp": "step"})
        return keys
    sources = [{"kind": "timeline", "name": "snareTurn", "settings": {"loopLength": 0.0, "keys": steps(eng["snare"], 2)}},
               {"kind": "timeline", "name": "blockStep", "settings": {"loopLength": 0.0, "keys": steps(eng["block"], 4)}}]

    # --- the palette arc
    def P(key):
        return lambda st: pal(key, st)
    for n, info in sorted(PROGS.items()):
        if info["system"] is None or n.startswith("ndPanes"):
            continue
        tracks.append(AU.track(info["prog"].path("colour"),
                               AU.arc_keys(lambda st, k=info["key"]: pal(k, st) + [1.0], info["system"], SONG)))
    # windows: from / to ramps stepped at each transition and a progress the buildings stagger on
    tints = lambda st: [pal(k, st) + [1.0] for k in ("win_a", "win_b", "win_c", "win_d")]
    fields = ("constant", "constant2", "constant3", "constant4")
    frm = {f: [{"time": 0.0, "value": tints("uv")[i], "interp": "step"}] for i, f in enumerate(fields)}
    to = {f: [{"time": 0.0, "value": tints("uv")[i], "interp": "step"}] for i, f in enumerate(fields)}
    prog_keys = [{"time": 0.0, "value": [0.0, -0.6, 0.0, 1.6], "interp": "step"}]
    prev = "uv"
    for (t0, st, dur) in AU.ARC["window"]:
        for i, f in enumerate(fields):
            frm[f].append({"time": t0, "value": tints(prev)[i], "interp": "step"})
            to[f].append({"time": t0, "value": tints(st)[i], "interp": "step"})
        prog_keys.append({"time": t0, "value": [0.0, -0.6, 0.0, 0.0], "interp": "linear"})
        for k in range(1, 7):
            prog_keys.append({"time": round(t0 + dur * k / 6, 3),
                              "value": [0.0, -0.6, 0.0, round(1.6 * AU.smooth(k / 6), 4)], "interp": "linear"})
        prev = st
    for f in fields:
        tracks.append(AU.track(panes.path("from", f), frm[f]))
        tracks.append(AU.track(panes.path("to", f), to[f]))
    tracks.append(AU.track(panes.path("progress"), prog_keys))
    # the practical lights (the windows' rate)
    for l in LAYOUT["lights"]:
        if not fixed_key(l["key"]):
            tracks.append(AU.track("lights/%s/color" % l["name"], AU.arc_keys(P(l["key"]), "practical", SONG)))
    # the cloud underlight keeps the old colour longest of the town's systems
    for name, i in sorted(GLOW_INDEX.items()):
        key = GLOW_KEYS.get(name)
        if key and not fixed_key(key):
            tracks.append(AU.track("clouds/glow/%d/color" % i, AU.arc_keys(P(key), "underlight", SONG)))
    # the air: the moon's key, the ambient, the warm horizon, the haze
    L = CLOUD_LOOK
    tracks += [
        AU.track("clouds/lightColor", AU.arc_keys(P("moon_light"), "air", SONG)),
        AU.track("clouds/ambientTop", AU.arc_keys(lambda st: [v * L["ambientTop"] for v in pal("sky_horizon", st)],
                                                  "air", SONG)),
        AU.track("clouds/ambientBottom", AU.arc_keys(lambda st: [v * L["ambientBottom"] for v in pal("haze", st)],
                                                     "air", SONG)),
        AU.track("clouds/warmColor", AU.arc_keys(lambda st: [v * L["warm"] for v in pal("horizon_glow", st)], "air", SONG)),
        AU.track("clouds/hazeColor", AU.arc_keys(P("haze"), "air", SONG)),
        AU.track("scene/fogColor", AU.arc_keys(P("haze"), "air", SONG)),   # iteration 7: the meshes' air
    ]
    # the far field: the nebula (the sky map is the Ultraviolet bake: a per-channel tint moves its hue), the moon's
    # disc, the cosmic ocean

    # per-channel ratios of the states' nebula colours to the Ultraviolet bake's, with green held at or under 1 so the
    # dark horizon never turns teal (the raw ratios lift green 2x for Electric Blue and Amber Eclipse)
    SKY_TINT = {"uv": [1.0, 1.0, 1.0], "rose": [1.7, 0.9, 0.75], "blue": [0.35, 0.95, 1.5], "amber": [1.35, 0.7, 0.3]}

    def neb(st):
        return SKY_TINT[st]
    tracks.append(AU.track("env/skyTint", AU.arc_keys(neb, "far", SONG)))

    def moon_stops(st):
        disc = [v * MOON_GAIN for v in pal("moon_disc", st)]
        return [[disc[0] * 0.30, disc[1] * 0.16, disc[2] * 0.16, 1.0], [disc[0] * 0.62, disc[1] * 0.50, disc[2] * 0.62, 1.0],
                disc + [1.0], disc + [1.0]]
    for i, f in enumerate(fields):
        tracks.append(AU.track("material/ndMoon/op/16/oklabRamp/%s" % f,
                               AU.arc_keys(lambda st, i=i: moon_stops(st)[i], "far", SONG)))
    tracks.append(AU.track("material/ndOcean/op/10/oklabRamp/constant2",
                           AU.arc_keys(lambda st: pal("ocean_a", st) + [1.0], "far", SONG)))
    tracks.append(AU.track("material/ndOcean/op/10/oklabRamp/constant3",
                           AU.arc_keys(lambda st: pal("ocean_b", st) + [1.0], "far", SONG)))
    # iteration 6 (art request 5): the Condensery is held at a tenth of its light until the crash that ends the
    # Veil (C_FAR, 56.88 s) and comes up over 0.85 s on it: the works lights up as the plug opens, and nothing of it can
    # show through the plug's last metres before then
    lit_at = AU.C_FAR
    hold = [(0.0, 0.1), (round(lit_at - 0.35, 3), 0.1), (round(lit_at, 3), 1.0)]  # it7: 0.85 -> 0.35 s (it glowed through the plug's last metres)

    def held(base):
        return [{"time": t, "value": round(base * f, 4), "interp": "linear"} for t, f in hold]
    for n, info in sorted(PROGS.items()):
        if info["group"] == "cond":
            tracks.append(AU.track("material/%s/emissionIntensity" % n, held(info["doc"]["emissionIntensity"])))
    for name, i in sorted(GLOW_INDEX.items()):
        if name.startswith("condensery_") or name == "veil_amber":   # it7: art round 4, request 5
            tracks.append(AU.track("clouds/glow/%d/intensity" % i, held(GLOW_BASE[name])))
    # iteration 9 (owner msg 73): leaving the plug the frame fell from 0.44 to 0.20 mean luma in four frames. The glows
    # that light it dim over the last half second before the exit, so the camera comes out of a fading cloud
    for name in ("veil_pocket5", "veil_rose"):
        if name in GLOW_INDEX:
            b = GLOW_BASE[name]
            tracks.append(AU.track("clouds/glow/%d/intensity" % GLOW_INDEX[name],
                                   [{"time": 0.0, "value": b, "interp": "linear"},
                                    {"time": PLUG_FADE[0], "value": b, "interp": "linear"},
                                    {"time": PLUG_FADE[1], "value": round(b * PLUG_FADE[2], 4), "interp": "linear"},
                                    {"time": PLUG_FADE[1] + 1.5, "value": b, "interp": "linear"}]))
    return routes, tracks, sources


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


def project_doc(scene_rel, audio_dir, tracks, routes, sources):
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
        # iteration 5: the audio history's onsets (what the kick ring field reads) come from the kick stem
        "feeds": {"beat": "stem.kick", "hits": "stem.kick", "levels": "audio", "historyOnsets": "stem.kick"},
        "sources": sources,
        "parameters": {"camera/lookAhead": 45.0, "camera/splineBank": 5.0, "post/output/dither": 1.0},
        "routes": routes,
        "timeline": {"enabled": True, "cues": [], "tracks": [trim_track(t) for t in tracks]},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/neon-dreams.mp4"},
        # iteration 10: the cast's clip cues (jetpack take-offs and landings); positions are timeline tracks above
        **({"sequence": {"name": "neon-dreams-cast", "duration": SONG, "actors": SEQUENCE_ACTORS}} if SEQUENCE_ACTORS else {}),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "scene"))
    ap.add_argument("--audio", default=SABERTOOTH)
    ap.add_argument("--name", default="neon-dreams")
    ap.add_argument("--palette", default="uv", choices=sorted(STATE_NAMES))
    ap.add_argument("--sky", default="sky/sky_uv.hdr", help="equirect sky map relative to this folder ('' = analytic)")
    ap.add_argument("--no-clouds", action="store_true", help="the cloud block disabled (for an A/B of its cost)")
    ap.add_argument("--no-arc", action="store_true", help="hold the starting palette (no palette arc tracks)")
    ap.add_argument("--no-wisps", action="store_true", help="ADR-2113 A/B: leave the wisp layer out")
    ap.add_argument("--wisp", action="append", default=[], metavar="KEY=VALUE",
                    help="override one wisp key (a number, or JSON); repeatable")
    ap.add_argument("--quality", default="offline", choices=sorted(CLOUD_QUALITY),
                    help="the cloud march's cost preset; 'live' writes <name>-live.* unless --name is given")
    ap.add_argument("--cloud", action="append", default=[], metavar="KEY=VALUE",
                    help="override one cloud quality knob (for measuring a preset); repeatable")
    ap.add_argument("--no-key-shadow", action="store_true", help="the moon key casts no shadow (measuring a preset)")
    ap.add_argument("--env", action="append", default=[], metavar="KEY=VALUE",
                    help="override one environment key (number); repeatable")
    ap.add_argument("--crowd-test", type=int, default=0, help="capacity probe: N aliens (not the film)")
    a = ap.parse_args()
    global NO_WISPS, CROWD_TEST
    CROWD_TEST = a.crowd_test
    NO_WISPS = a.no_wisps
    for kv in a.wisp:
        k, v = kv.split("=", 1)
        WISPS[k] = json.loads(v)
    CLOUD_Q.clear()
    CLOUD_Q.update(CLOUD_QUALITY[a.quality])
    for kv in a.cloud:
        k, v = kv.split("=", 1)
        CLOUD_Q[k] = int(v) if k in ("steps", "lightSteps", "erosionOctaves") else float(v)
    KEY_Q["shadow"] = KEY_SHADOW[a.quality] and not a.no_key_shadow
    ENV_Q.clear()
    ENV_Q.update(ENV_QUALITY[a.quality])
    for kv in a.env:
        k, v = kv.split("=", 1)
        ENV_Q[k] = float(v)
    if a.quality != "offline" and a.name == "neon-dreams":
        a.name = "neon-dreams-" + a.quality
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
        routes, arc, sources = reactivity()
        if a.palette != "uv" or a.no_arc:   # a still in one state: no arc
            arc = []
        json.dump(project_doc(os.path.relpath(scene_path, a.out), a.audio, tracks + arc, routes, sources), f, indent=1)
    print("wrote", scene_path, "and", proj_path)


if __name__ == "__main__":
    main()
