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
import nd_lights as NL  # noqa: E402  (iteration 11, owner msg 130: the city's lights play more of the song)
import nd_cast as NC  # noqa: E402  (iteration 10, owner msg 112: the population from art/population.json)
import nd_band as NB  # noqa: E402  (iteration 12, owner msg 149: the band, its spots and beams)

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


CITY_OFF = False   # --city-off: A/B of iteration 11's light systems
OCEAN_RING = {}   # it11 (msg 130): ndOcean's ring op indices and its radius, for the tracks in reactivity()


def ocean_breathing(prog, centre, radius):
    """Iteration 11 (msg 130, the art's audio idea 8): the cosmic ocean breathes -- a ring from the break's centre on
    each pad phrase (nd_lights.ocean_ops)."""
    ops, ri, ai = NL.ocean_ops(prog["ops"], (centre[0], centre[1]), radius)
    OCEAN_RING.update({"radius_op": ri, "amount_op": ai, "radius": radius})
    return dict(prog, ops=ops)


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


def zclass_of(zone, asset=""):
    return "cond" if zone == "condensery" else "lone" if zone == "lone_lamp" else "town"


def fixed_key(key):
    return key in PALETTE["fixed"]


def _reg(name, made, system, key, group):
    if name not in PROGS:
        prog, doc = made()
        PROGS[name] = {"prog": prog, "doc": doc, "system": None if fixed_key(key) else system, "key": key,
                       "group": group}
    return name


def _moog(gain, isle=False):
    """it12 (msg 149): the Moog's travelling pulse (it11: the districts in turn); reg 4 sums the district masks (the
    town) for the isle ring's term after it."""
    if isle:
        return lambda p: (NL.moog_pulse_term(p, 1, 3, 2, 5, 6, gain), NL.town_mask(p, 3, 4, 5),
                          NL.isle_term(p, 1, 4, 5, 6))
    return lambda p: NL.moog_pulse_term(p, 1, 3, 2, 5, 6, gain)


def _isle():
    return lambda p: (NL.town_mask(p, 3, 4, 5), NL.isle_term(p, 1, 4, 5, 6))


def _chase(label, space, cfg):
    return lambda p: NL.chase_term(p, 1, 5, 6, label, space, cfg["period"], cfg["gain"], cfg.get("power", 6.0))


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
        # it11: signs answer the Moog by district and chase down the street on the snare (it5-10: A/B in turn)
        n = "ndSign_%s" % key
        return _reg(n, lambda: AU.emit_program(n, col, gain, extra=(
            _moog(NL.MOOG_GAIN["sign"]), _chase("snare", "worldPosition", NL.SNARE_CHASE))), "sign", key, "sign")
    if role == "strip":
        if "gangway" in asset:   # the gangway strips: the snare
            n = "ndGang_%s" % key
            return _reg(n, lambda: AU.emit_program(n, col, gain), "eave", key, "gangway")
        # eave lines: the kick ring; it11: the crowns' strips answer the Moog by district, the isles' the second ring.
        # sign_c strips are only the landmark crowns (the Great Lantern's frame, the Mooring Mast): the bass swells them
        n = "ndStrip_%s" % key
        return _reg(n, lambda: AU.emit_program(n, col, gain, ring=NL.TOWN_RING_GAIN["strip"], extra=(
            _moog(NL.MOOG_GAIN["strip"], isle=True),)), "eave", key, "crown" if key == "sign_c" else "strip")
    if role == "beacon":
        n = "ndBeacon"
        return _reg(n, lambda: AU.emit_program(n, col, gain * 0.02, pattern=True), None, key, "block_beacon")
    if role == "garden":
        n = "ndGarden_%s" % key   # it11: the isles' garden lamps answer the second kick ring
        return _reg(n, lambda: AU.emit_program(n, col, gain, hh=1.4, extra=(_isle(),)), "garden", key, "hh")
    if role == "lantern":   # it11: the strings chase on the hats (a lamp group a hat along each string)
        n = "ndLantern_%s" % key
        return _reg(n, lambda: AU.emit_program(n, col, gain, ring=NL.TOWN_RING_GAIN["lantern"], hh=0.5, toms=1.6,
                                               extra=(_chase("hh", "localPosition", NL.HH_CHASE),)),
                    "garden", key, "lantern")
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
    # it11 (msg 130): the windows answer the Moog by district and flick floor by floor on the pad's phrase changes
    return _reg("ndPanes", lambda: AU.window_program(
        "ndPanes", t, t, 4.0, ring=NL.TOWN_RING_GAIN["panes"],
        extra=(lambda p: NL.moog_pulse_term(p, 5, 2, 0, 3, 6, NL.MOOG_GAIN["panes"]), lambda p: NL.flick_term(p, 5, 3, 6))),
        "window", "win", "panes")


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
        key = (inst["asset"], tuple(sorted(tint.items())), color, zclass_of(inst.get("zone", ""), inst["asset"]),
               inst.get("bob", ""))
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
    # msg 123: off spot_066 (under its roof terrace's pergola) and spot_077 (under an eave), to the open-sky search's nearest
    {"name": "flyer00", "cast": "alien-pilot", "from": {"pos": [-47.9, 147.31, 1.3], "yaw": -141.8}, "to": {"pos": [5.5, 134.51, 3.1]},
     "t0": 29.6, "td": 35.9, "alt": 9.0},
]
SEQUENCE_ACTORS = []


def spot_place(ref):
    if isinstance(ref, dict):
        return {"pos": ref["pos"], "yaw": ref.get("yaw")}
    sp = next(s for s in LAYOUT["inhabitants"]["spots"] if s["id"] == ref)
    f = sp["forward"]
    return {"pos": sp["pos"], "yaw": math.degrees(math.atan2(f[0], f[2]))}


POP_PATH = os.path.join(ART, "population.json")
POPULATION = json.load(open(POP_PATH)) if os.path.exists(POP_PATH) else None
CROWDS = []          # ADR-2117: the crowd rings the population made (routes target them)
WAKES = ["walkA*", "flyer*"]


def kick_onsets():
    return [round(t, 3) for t, _ in AU.EVENTS["stems"]["kick"]["onsets"]]


def cast_nodes(state="uv"):
    """Iteration 10: the population (art/population.json) with its flyers, or the older layout cast without it."""
    SEQUENCE_ACTORS.clear()
    if POPULATION is None or CROWD_TEST:
        a, at = population_nodes() if not CROWD_TEST else crowd_test_nodes(CROWD_TEST)
        f, fl, ft = flight_nodes(state)
        return a + f, fl, at + ft
    amber = pal("amber", state)
    nodes, tracks, lights, actors, crowds, wakes = NC.build(
        POPULATION, LAYOUT, DESIGN, ALIENS, CLIP_LEN, BELL_POS, SONG, r3, logged_flight(amber), kick_onsets(),
        FLIGHT_FIXES, GROUND_FIXES)
    SEQUENCE_ACTORS.extend(actors)
    CROWDS[:] = crowds
    if PROTO_FLIGHT:   # the msg 117/121 close-up's prototype flight, beside the population's
        n, l, t = flight_nodes(state, clear=False)
        nodes, lights, tracks = nodes + n, lights + l, tracks + t
    return nodes, lights, tracks


FLIGHT_LOG = []   # msg 123: every flight as flown (its ends and path), for --check-flights


def logged_flight(color):
    def fly(f):
        out = FL.flight(f, r3, SONG, color, color)
        FLIGHT_LOG.append(f)
        return out
    return fly


def check_flights(scene_dir):
    """Owner msg 123: open sky over every take-off and landing, and the whole path clear of the town (Blender BVH,
    blender/nd_flight_check.py). Prints each flight's verdict; returns the failures."""
    import subprocess
    req = {"flights": [{"id": f["name"], "takeoff": f["from"]["pos"], "landing": f["to"]["pos"],
                        "lift_a": f["_lift"][0], "lift_b": f["_lift"][1], "samples": f["_samples"]} for f in FLIGHT_LOG],
           "open_near": OPEN_NEAR}
    a, b = os.path.join(scene_dir, "flight_check_in.json"), os.path.join(scene_dir, "flight_check.json")
    json.dump(req, open(a, "w"))
    subprocess.run([BLENDER, "-b", "--factory-startup", "-P", os.path.join(HERE, "blender", "nd_flight_check.py"), "--",
                    a, b], check=True, stdout=subprocess.DEVNULL)
    rep = json.load(open(b))
    bad = [r for r in rep["flights"] if not r["ok"]]
    for r in rep["flights"]:
        print("flight check: %-28s %s (take-off %s, landing %s, clearance %s m at %s s)" % (
            r["id"], "ok" if r["ok"] else "FAIL", "open" if r["takeoff_open"] else "BLOCKED",
            "open" if r["landing_open"] else "BLOCKED", r["min_clearance_m"], r["worst_t"]))
    for k, v in rep.get("open", {}).items():
        print("open sky near %s: %s" % (k, v))
    return bad


def ground_request(doc, tracks):
    """msg 130: every figure as the scene places it, for blender/nd_ground_check.py."""
    tk = {t["target"]: t["keys"] for t in tracks}
    figs = []
    for n in doc["nodes"]:
        nm = n["name"]
        if n.get("kind") != "gltf" or "aliens/" not in n.get("asset", ""):
            continue
        if nm.startswith("flyer"):
            continue   # the flights' ends come from the population below
        if n.get("parent", "").startswith("vehicle"):
            vi = int(n["parent"][len("vehicle"):])
            figs.append({"id": nm, "kind": "rider", "cabin": LAYOUT["vehicles"][vi]["asset"],
                         "samples": [[n["position"], n["rotation"][1]]]})
            continue
        pk = tk.get("nodes/%s/position" % nm)
        if not pk:
            figs.append({"id": nm, "kind": "spot", "bob": n.get("bob"), "samples": [[n["position"], n["rotation"][1]]]})
            continue
        # a walker: every leg its keys run (the whole song), resampled every 0.3 m; each distinct leg once
        segs, samples = set(), []
        for a, b in zip(pk, pk[1:]):
            pa, pb = a["value"], b["value"]
            L = math.dist(pa, pb)
            key = (tuple(round(v, 2) for v in pa), tuple(round(v, 2) for v in pb))
            if L < 1e-3 or key in segs or key[::-1] in segs:
                continue
            segs.add(key)
            yaw = math.degrees(math.atan2(pb[0] - pa[0], pb[2] - pa[2]))
            for s in range(int(L / 0.3) + 1):
                u = min(1.0, s * 0.3 / L)
                samples.append([r3([pa[i] + (pb[i] - pa[i]) * u for i in range(3)]), round(yaw, 1)])
            samples.append([r3(pb), round(yaw, 1)])
        pid = nm.split("_", 1)[1].rsplit("_", 1)[0]
        path = next((q for q in (POPULATION or {}).get("paths", []) if q["id"] == pid), None)
        figs.append({"id": nm, "kind": "walker", "bob": n.get("bob"), "samples": samples, "path_id": pid,
                     "path": path["points"] if path else None})
    for fd in (POPULATION or {}).get("flyers", []):
        if "flyer_" + fd["id"] in FLIGHT_FIXES.get("_drop", []):
            continue
        k = fd["keys"]
        for end, key in (("takeoff", k[0]), ("landing", k[-1])):
            figs.append({"id": "flyer_%s@%s" % (fd["id"], end), "kind": "flyer", "bob": fd[end].get("bob"),
                         "samples": [[fd[end]["pos"], key.get("yaw_deg") or 0.0]]})
    return {"figures": figs}


def check_ground(doc, tracks, scene_dir, tag="ground_check"):
    """Owner msg 130: every figure stands on something (Blender BVH, blender/nd_ground_check.py). Returns the report."""
    import subprocess
    a, b = os.path.join(scene_dir, tag + "_in.json"), os.path.join(scene_dir, tag + ".json")
    json.dump(ground_request(doc, tracks), open(a, "w"))
    subprocess.run([BLENDER, "-b", "--factory-startup", "-P", os.path.join(HERE, "blender", "nd_ground_check.py"), "--",
                    a, b], check=True, stdout=subprocess.DEVNULL)
    rep = json.load(open(b))
    for r in rep["figures"]:
        if not r["ok"]:
            w = r["worst"] or {}
            print("ground check: %-30s %-6s %-14s gap %s on %s inside %s  fix %s" % (
                r["id"], r["kind"], ",".join(r["reasons"]), w.get("gap_m"), w.get("on"), w.get("inside"), r.get("fix")))
    print("ground check:", json.dumps(rep["summary"]))
    return rep


def fixes_from_ground(rep):
    """msg 130: the check's fixes into art/ground_fixes.json: spots moved (and the formation they then ride) or dropped,
    walkers' paths re-laid on the floor (the longest standing run; a path shorter than 2 m is dropped), riders set on
    their cabin's floor. Each fix records what it fixed (`from`): nd_cast applies it only while the population still
    has that, so an art edit of the same figure is never overridden by a stale fix (the check then sees the new one)."""
    g = GROUND_FIXES
    pop = POPULATION or {}
    src = {"spot": {x["id"]: x["pos"] for x in pop.get("spots", [])},
           "rider": {x["id"]: x["local_pos"] for x in pop.get("riders", [])}}
    paths = {x["id"]: x["points"] for x in pop.get("paths", [])}
    for r in rep["figures"]:
        fx = r.get("fix")
        if r["ok"] or not fx:
            continue
        if r["kind"] in ("spot", "rider"):
            key = r["id"][len("alien_"):]
            frm = src[r["kind"]].get(key)
            if fx.get("drop"):
                g.setdefault("drop", {})[key] = {"from": frm, "why": r["reasons"]}
            else:
                g.setdefault("spots" if r["kind"] == "spot" else "riders", {})[key] = {
                    "pos": fx["move"], "bob": fx.get("bob"), "why": r["reasons"], "from": frm}
        elif r["kind"] == "walker":
            pid = r["id"].split("_", 1)[1].rsplit("_", 1)[0]
            pts = fx["path"]
            L = sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))
            if L < 2.0:
                g.setdefault("drop_paths", {})[pid] = {"from": paths.get(pid), "why": r["reasons"]}
            else:
                g.setdefault("paths", {})[pid] = {"points": pts, "why": r["reasons"], "lost": fx["lost"], "of": fx["of"],
                                                  "from": paths.get(pid)}
    g["_note"] = ("msg 130, build.py --check-ground --fix-ground (blender/nd_ground_check.py): spots moved to the nearest "
                  "standing point (and the formation under it) or dropped; walker paths re-laid every 0.3 m on the floor; "
                  "riders on the cabin floor. `from`: what was fixed (a fix whose `from` no longer matches is ignored)")


BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
# msg 130: spot moves and drops the ground check found (art/ground_fixes.json), applied by nd_cast
GFIX_PATH = os.path.join(ART, "ground_fixes.json")
GROUND_FIXES = json.load(open(GFIX_PATH)) if os.path.exists(GFIX_PATH) else {}
# msg 123: pushes the clearance check found it needed (nearest surface, push vector), applied to the flights' via points
FIX_PATH = os.path.join(ART, "flight_fixes.json")
FLIGHT_FIXES = json.load(open(FIX_PATH)) if os.path.exists(FIX_PATH) else {}
OPEN_NEAR = []


PROTO_FLIGHT = False


def flight_nodes(state="uv", clear=True):
    nodes, lights, tracks = [], [], []
    if clear:
        SEQUENCE_ACTORS.clear()
    for fdat in LAYOUT.get("flights", ENGINE_FLIGHTS):
        f = dict(fdat, asset=ALIENS + fdat.get("cast", "alien-pilot") + ".glb", **{"from": spot_place(fdat["from"]),
                                                                                  "to": spot_place(fdat["to"])})
        n, l, t, actor = logged_flight(pal("amber", state))(f)
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


# iteration 10 (the art's audio idea 4): the lighthouses keep the bar -- one turn per 8 bars of the 1.4 s bar clock,
# a quarter bar apart -- and whip half a turn (to the next arm) on each section crash, 120 ms in and 700 ms out
BAR = 1.4
# the pad's (Cloud Diving) phrases, about 4 s every 5.6 s (the art's measure, notes/audio-ideas-it10.md)
PAD_PHRASES = [2.8, 8.5, 14.1, 19.7, 25.3, 30.9, 36.6, 42.2, 47.8, 59.1, 63.3]
BAR_RPM = round(60.0 / (8 * BAR), 4)
WHIP = (0.12, 0.7, 180.0)


def beam_yaw0(i, b):
    return round(b.get("phase", 0.0) * 360.0 + i * 360.0 / 32.0, 2)


WHIP_SKIPPED = []   # it11 (msg 130): (beam, crash) whips left out because an arm would sweep the lens


def whip_sweeps_lens(i, b, yaw_at, t0, t1, near=160.0, view=40.0):
    """Iteration 11 (msg 130, the 2.8 s glare): does an arm of beam i pass within `near` m of the lens, at a point in
    front of it (within `view` degrees of the heading), between t0 and t1? yaw_at(t): the beam's yaw track. Measured on
    the it10b path: the Harbour Light at 2.8 s (145 m, 35 deg) and the Outer Light at 47.0 s (101 m, 20 deg); every other
    whip passes 290 m or more away or behind the camera."""
    o = b["origin"]
    el = math.radians(b["elevation_deg"])
    t = t0
    while t <= t1:
        c, hd = cam_at(t)
        v = [c[k] - o[k] for k in range(3)]
        for arm in range(b["count"]):
            a = math.radians(yaw_at(t) + BAR_RPM * 6.0 * t + 360.0 * arm / b["count"])
            dv = [math.cos(el) * math.cos(a), math.sin(el), -math.cos(el) * math.sin(a)]
            s = max(0.0, min(b["length_m"], sum(v[k] * dv[k] for k in range(3))))
            w = [o[k] + dv[k] * s - c[k] for k in range(3)]
            d = math.sqrt(sum(x * x for x in w))
            if d < near:
                ang = math.degrees(math.acos(max(-1.0, min(1.0, sum(w[k] * hd[k] for k in range(3)) / max(d, 1e-6)))))
                if ang < view:
                    return True
        t += 1.0 / 60.0
    return False


def beam_whip_keys(yaw0, bi=None, b=None):
    """The beams keep the bar and whip half a turn on each section crash (it10, ADR-2118). Iteration 11 (msg 130): a
    whip that would sweep an arm across the lens is left out (with two arms half a turn apart, a skipped whip leaves
    the sweep in the same phase afterwards); the 2.8 s crash's whip of the Harbour Light was one."""
    keys = [{"time": 0.0, "value": yaw0, "interp": "linear"}]
    turned = 0.0
    for c in AU.CRASH:
        a, d = c - WHIP[0], c + WHIP[1]
        cand = []
        for k in range(9):
            u = k / 8
            e = u * u * (3 - 2 * u)
            cand.append((a + (d - a) * u, yaw0 + turned + WHIP[2] * e))
        if b is not None and b.get("count", 1) == 2:
            def yaw_at(t, cand=cand):
                for (ta, va), (tb, vb) in zip(cand, cand[1:]):
                    if ta <= t <= tb:
                        return va + (vb - va) * (t - ta) / max(tb - ta, 1e-6)
                return cand[-1][1]
            if whip_sweeps_lens(bi, b, yaw_at, a, d):
                WHIP_SKIPPED.append((b["name"], round(c, 2)))
                continue
        keys += [{"time": round(t, 3), "value": round(v, 2), "interp": "linear"} for t, v in cand]
        turned += WHIP[2]
    return keys


BAND = {}   # it12 (msg 149): nd_band.build's results for this state (nodes, lights, beams, tracks, routes, actors)


def glb_clip_len(path, clip):
    import struct
    try:
        with open(path, "rb") as f:
            b = f.read()
    except OSError:
        return 4.0
    n = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + n])
    for a in j.get("animations", []):
        if a["name"] == clip:
            return max(j["accessors"][sm["input"]]["max"][0] for sm in a["samplers"])
    return 4.0


def band(state="uv"):
    if BAND.get("state") != state:
        out = NB.build(LAYOUT, os.path.join(HERE, "scene"), os.path.normpath(os.path.join(HERE, "..", "..")),
                       lambda k: pal(k, state), r3, glb_clip_len, SONG)
        BAND.clear()
        BAND.update(dict(zip(("nodes", "lights", "beams", "tracks", "routes", "actors", "info"), out)), state=state)
    return BAND


def band_beams(state="uv"):
    return [{k: v for k, v in b.items() if not k.startswith("_")} for b in band(state)["beams"]]


def cloud_beams(state="uv"):
    out = []
    for bi, b in enumerate(LAYOUT["beams"]):
        col = pal(b["key"], state)
        out.append({"name": b["name"], "origin": r3(b["origin"]), "count": b["count"],
                    "yaw": beam_yaw0(bi, b), "elevation": b["elevation_deg"], "rpm": BAR_RPM,
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
    vd = vortex_design()
    for name, ls in DESIGN["masses"].items():
        if name == "vortex" and vd:   # it12: ADR-2120 draws the funnel; the art's orbiting shelf lobes are its fallback
            continue
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
    eg = (vd or {}).get("_art", {}).get("eye_glow")
    if eg:   # it12: the art's eye glow under the vortex's throat
        glows.append({"name": "vortex_eye", "position": eg["position"], "radius": eg["radius_m"],
                      "intensity": eg["intensity"], "color": pal(eg["key"], state), "key": eg["key"], "shadow": 0.3})
    GLOW_INDEX.clear()
    GLOW_BASE.clear()
    GLOW_BASE.update({g["name"]: g["intensity"] for g in glows})
    GLOW_INDEX.update({g["name"]: i for i, g in enumerate(glows)})
    GLOW_POS.clear()
    GLOW_POS.update({g["name"]: g["position"] for g in glows})
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
        "beams": cloud_beams(state) + band_beams(state), "beamAir": CLOUD_LOOK["beamAir"], "beamCloud": CLOUD_LOOK["beamCloud"],
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
    # ADR-2119 (msg 149): mist sheets at their own heights and winds (the art's `mist_layers` replace mine)
    if not NO_MIST:
        w["layers"] = [dict(l) for l in DESIGN.get("mist_layers", MIST_LAYERS)]
    if w.get("enabled") and not NO_WISPS:
        out["wisps"] = w
    # ADR-2119 (msg 149): the clouds churn in place; their thin edges take the air's colour
    out["churn"] = dict(CHURN, **DESIGN.get("churn", {}))
    out["edgeAir"] = DESIGN.get("edgeAir", EDGE_AIR)
    # ADR-2120 (msg 149): the ocean break becomes a vortex (the art's `vortex` over my first setting)
    v = vortex_design()
    if v:
        out["vortex"] = {k: v[k] for k in v if not k.startswith("_")}
        out["vortex"]["wind"] = abs(out["vortex"].get("wind", 1.8))
        out["vortex"]["glowColor"] = pal(v.get("_glowKey", "ocean_b"), state)
        out["sea"]["breaks"] = []
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
         "wakeHeight": 2.2, "wakeTrail": 2.6, "wakes": ["walkA*", "flyer*"],
         # no band (the art: nowhere over open sea); the first lens, used when the design has no wisp_regions
         "zones": [{"name": "mooring", "centre": [0.0, 10.0], "floor": 131.0, "radii": [330.0, 280.0], "yaw": 0.0,
                    "below": 30.0, "above": 6.0, "density": 1.0}]}
NO_WISPS = False
NO_MIST = False
NO_VORTEX = False
# ADR-2119 (owner msg 149: "clouds all around more animated ... fog layers or distortion/movement"): the billows boil in
# place over 7 s clocks (1.2 m/s, rising and rolling along the curl field) and their tatters writhe; thin edges take
# 35 % of the air's colour (59.6 s: the edge "too CG")
CHURN = {"amount": 1.0, "period": 7.0, "boil": 1.2, "writhe": 0.8}
EDGE_AIR = 0.35
# ADR-2119 (msg 149: "clouds around the city more dynamic, blended with layers of fog"; 62 s: "more cloud layers / fog
# layers"): mist sheets. The town's decks are at 121-153 m; the stilts' sheet lies under them, a wider low bank blends
# the town's cloud into the sea, and two banks over the open sea and round the vortex give the reveal its layers.
MIST_LAYERS = [
    {"name": "stilts", "centre": [0.0, 10.0], "radius": 520.0, "base": 92.0, "top": 121.0, "density": 0.9,
     "scale": 26.0, "coverage": 0.5, "stretch": 3.5, "drift": [1.4, 0.0, 0.5]},
    {"name": "harbour_low", "centre": [0.0, 10.0], "radius": 1400.0, "base": 40.0, "top": 95.0, "density": 0.6,
     "scale": 60.0, "coverage": 0.45, "stretch": 3.0, "drift": [2.2, 0.0, 0.8]},
    {"name": "open_sea", "centre": [1300.0, -30.0], "radius": 1300.0, "base": 15.0, "top": 75.0, "density": 0.5,
     "scale": 80.0, "coverage": 0.42, "stretch": 3.0, "drift": [2.5, 0.0, 1.0]},
    {"name": "vortex_shelf", "centre": [2090.0, -20.0], "radius": 950.0, "base": -25.0, "top": 45.0, "density": 0.7,
     "scale": 55.0, "coverage": 0.5, "stretch": 3.0, "drift": [0.4, 0.0, 0.2]},
]
# ADR-2120 (msg 149: "turn the ocean break into a CLOUD VORTEX"): my first setting, under the art's `vortex` from the
# cloud design. Centred on the break, the eye a little narrower than the break was, the funnel down to the cosmic
# ocean, which moves down under the eye (`_oceanY`).
VORTEX = {"enabled": True, "radius": 620.0, "eye": 150.0, "elongation": 1.6, "depth": 175.0, "spin": 0.12,
          "twist": 0.8, "wind": 1.8, "shelves": 5, "pitch": 1.5, "arms": 3, "armDepth": 0.45, "glow": 6.0,
          "glowHeight": 90.0,
          "pulse": 0.0, "column": 0.0, "columnDensity": 0.8, "columnPitch": 180.0,
          "_glowKey": "ocean_b", "_oceanY": -228.0}


def vortex_design():
    """The vortex block (centre and yaw from the break unless the art's design names them), or None."""
    if NO_VORTEX:
        return None
    brk = DESIGN["sea_design"]["break"]
    v = dict(VORTEX, centre=brk["centre"], yaw=brk.get("yaw_rad", 0.0))
    v.update(DESIGN.get("vortex", {}))
    a = brk.get("vortex")
    if a and "rim_semi_axes" in a:   # it12: the art's design (sea_design.break.vortex), in ADR-2120's terms
        rim, throat = a["rim_semi_axes"], a["throat_semi_axes"]
        shear = a.get("shear_exponent", 1.5)
        eye_dps = a.get("rotation_dps_rim", 1.2) * (rim[1] / throat[1]) ** shear   # the art's law at the throat
        ccw = "counter" in a.get("direction", "counter")
        v.update({"centre": a["centre"], "yaw": a.get("yaw_rad", v["yaw"]), "elongation": round(rim[0] / rim[1], 3),
                  "radius": round(rim[1] / 0.85, 1),      # the funnel's outer edge (df = 0) at the rim
                  "eye": throat[1], "depth": a.get("depth_m", 84.0), "shelves": a.get("shelves", 7),
                  "arms": a.get("arms", 3), "armDepth": a.get("arm_contrast", 0.55),
                  # ADR-2120's law spin / (1 + (r / 1.5 eye)^2) matched at the throat; ADR-2120 turns +x -> +z
                  # (clockwise seen from above) for a positive spin
                  "spin": round(math.radians(eye_dps) * 1.44 * (-1.0 if ccw else 1.0), 4),
                  "_glowKey": a.get("underlight", {}).get("key", "ocean_a"),
                  "glow": a.get("underlight", {}).get("throat_gain", 1.6) * 2.5,
                  "glowHeight": round(0.6 * a.get("depth_m", 84.0), 1), "_oceanY": None, "_art": a})
    return v if v.get("enabled", True) else None
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
GLOW_POS = {}     # glow name -> its position (it11: the isle ring's delays)


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
    vd = vortex_design()
    if vd and vd.get("_oceanY") is not None:   # ADR-2120: the ocean under the eye (the art's plane when it designs one)
        e = vd["eye"] * 1.35
        op = {"y": vd.get("_oceanY", -228.0), "semi_axes": [round(e * vd["elongation"], 1), round(e, 1)],
              "yaw_rad": vd["yaw"]}
    ya = op.get("yaw_rad", 0.0)
    nodes.append({"name": "cosmicOcean", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "cylinder", "radius": 1.0, "height": 40.0, "radialSegments": 96, "caps": True},
        "distribution": {"kind": "points", "points": [[br[0], op["y"] - 20.0, br[1], 0.0, round(math.sin(ya / 2), 5), 0.0,
                                                       round(math.cos(ya / 2), 5), op["semi_axes"][0], 1.0,
                                                       op["semi_axes"][1]]]},
        "castsShadow": False,
        "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndOcean"}}})
    aliens, flyer_lights, alien_tracks = cast_nodes(state)
    vehicles, vehicle_tracks = vehicle_nodes(state)
    nodes += aliens + vehicles + bob_nodes()
    bd = band(state)   # it12 (msg 149): the band
    nodes += bd["nodes"]
    flyer_lights = flyer_lights + bd["lights"]
    alien_tracks = alien_tracks + bd["tracks"]
    SEQUENCE_ACTORS.extend(bd["actors"])
    programs = [
        moon_program("ndMoon", [v * MOON_GAIN for v in pal("moon_disc", state)], ml, 3.0 / R, md[1] * D, R),
        ocean_breathing(ocean_program("ndOcean", pal("ocean_a", state), pal("ocean_b", state),
                                      0.004 if vd else 0.0012, 3.5 if vd else 2.5), br,
                        max(op["semi_axes"])),
        FL.plume_program("ndJetPlume"),   # iteration 10b (msg 121): the jet plumes
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
        "fields": [AU.kick_ring_field(BELL_POS), NL.isle_ring_field(BELL_POS)],
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
            r = R("stem.hh.onset", info["prog"].path("hh"), 1.0, **hit)
            if not CITY_OFF:
                # it11 (msg 130): in the town the strings chase on the hats instead (one hat system at a time there);
                # the gardens' sparkle rests to a third
                r.update({"depthSource": "timeline.outsideTown", "depthMin": 0.0 if info["group"] == "lantern" else 0.3,
                          "depthMax": 1.0})
            routes.append(r)
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
    vd = vortex_design()
    if vd:   # ADR-2120 (msg 149): the vortex's eye glows with the pad and its funnel breathes on the bass
        routes += [R("stem.cloud.level", "clouds/vortex/glow", round(vd["glow"] * 0.9, 3), **pad),
                   R("stem.bass.level", "clouds/vortex/pulse", 0.6, **lvl_slow)]
    for n, info in PROGS.items():
        if info["group"] == "lone":
            routes.append(R("stem.cloud.level", "material/%s/emissionIntensity" % n,
                            1.2 * info["doc"]["emissionIntensity"], **pad))
    # --- tone block: beacons and roundels lit by the stem's level (dark while it rests), the lit quarter stepping
    for n, info in PROGS.items():
        if info["group"] in ("block_beacon", "block_roundel"):
            full = ROLE_GAIN["beacon"] * 0.9 if info["group"] == "block_beacon" else ROLE_GAIN["neon_a"] * 0.9
            r = R("stem.block.level", "material/%s/emissionIntensity" % n, full, attackMs=20.0, decayMs=180.0)
            if not CITY_OFF:   # it11 (msg 130): the beacons rest to a third in the town, where the new systems play
                r.update({"depthSource": "timeline.outsideTown", "depthMin": 0.35, "depthMax": 1.0})
            routes.append(r)
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
    a, b = NL.TOWN
    sources = [{"kind": "timeline", "name": "outsideTown", "settings": {"loopLength": 0.0, "keys": [
                   {"time": 0.0, "value": 1.0, "interp": "linear"}, {"time": a - 0.3, "value": 1.0, "interp": "linear"},
                   {"time": a, "value": 0.0, "interp": "linear"}, {"time": b, "value": 0.0, "interp": "linear"},
                   {"time": b + 0.3, "value": 1.0, "interp": "linear"}]}},
               {"kind": "timeline", "name": "snareTurn", "settings": {"loopLength": 0.0, "keys": steps(eng["snare"], 2)}},
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
    # --- iteration 10 (owner msg 112; the art's audio ideas 2, 4, 6; ADR-2117/2118): bodies that listen
    # heads to the Lantern on each pad phrase (the bass plays all song, so its level could not make a gesture: traced
    # at 0.3-0.8 throughout), ring by ring outward one second apart (the wave runs at 60 m/s). Timeline keys: exact.
    for ring in [c for c in CROWDS if c.startswith("ring")]:   # it12: the band's crowd watches the stage instead
        k = int(ring[4:])
        keys = [{"time": 0.0, "value": 0.0, "interp": "linear"}]
        for i, p in enumerate(PAD_PHRASES):
            a = p + min(4.0, k + 0.5)
            end = min(a + 4.9, (PAD_PHRASES[i + 1] + min(4.0, k + 0.5)) if i + 1 < len(PAD_PHRASES) else 1e9)
            keys += [{"time": round(a, 3), "value": 0.0, "interp": "smooth"},
                     {"time": round(a + 0.6, 3), "value": 1.0, "interp": "smooth"},
                     {"time": round(min(a + 2.4, end - 1.2), 3), "value": 1.0, "interp": "smooth"},
                     {"time": round(end, 3), "value": 0.0, "interp": "smooth"}]
        tracks.append(AU.track("crowds/%s/look" % ring, keys))
    for ring in CROWDS:
        # the groups sway on the drum bus (kick and snare levels), at two beats a period, dying in the drum breaks
        for stem in ("kick", "snare"):
            routes.append(R("stem.%s.level" % stem, "crowds/%s/sway" % ring, 0.8 if ring == "band" else 0.4,
                            attackMs=800.0, decayMs=3000.0,
                            gain=1.5, clampEnabled=True, clampMin=0.0, clampMax=1.0))
    # the beams: bar-locked, whipping on the crashes (timeline tracks: exactly seek-safe)
    for bi, b in enumerate(LAYOUT["beams"]):
        tracks.append(AU.track("clouds/beam/%d/yaw" % bi, beam_whip_keys(beam_yaw0(bi, b), bi, b)))
    routes, tracks = city_lights(routes, tracks, lvl_slow)
    # it12 (msg 149): the band's routes, the stage beams' sway and whip; the look-ahead the art keyed for the band
    bd = band()
    routes += bd["routes"]
    bt, br = NB.beam_tracks(bd["beams"], len(LAYOUT["beams"]), AU.CRASH, bd["info"]["breath_keys"])
    tracks += bt
    routes += br
    la = LAYOUT["camera"].get("look_ahead")
    if la:
        tracks.append(AU.track("camera/lookAhead", [{"time": t, "value": v, "interp": "smooth"} for t, v in la]))
    return routes, tracks, sources


def city_lights(routes, tracks, lvl_slow):
    """Iteration 11 (owner msg 130: "the lights in the city could react more"): nd_lights.py's systems, as timeline
    tracks from the measured onsets (exactly seek-safe) and two bass routes."""
    R = AU.route
    if CITY_OFF:   # A/B: the it11 systems' envelopes left at 0 (the programs and the stronger rings stay)
        return routes, tracks
    turns = NL.moog_turns()
    hh_phase = NL.step_keys(NL.ONSETS["hh"], NL.HH_CHASE["steps"], gate=NL.TOWN)
    hh_amt = NL.env_keys([t for t in NL.ONSETS["hh"] if not NL.in_fill(t)], *NL.HH_CHASE["ad"], gate=NL.TOWN)
    sn_phase = NL.step_keys(NL.ONSETS["snare"], NL.SNARE_CHASE["steps"], gate=NL.TOWN)
    sn_amt = NL.env_keys(NL.ONSETS["snare"], *NL.SNARE_CHASE["ad"], gate=NL.TOWN)
    moog = {d: NL.env_keys(ts, *NL.MOOG_AD) for d, ts in turns.items()}
    for n, info in sorted(PROGS.items()):
        at = info["prog"].at
        if "moog_quay" in at:
            for d, keys in moog.items():
                tracks.append(AU.track(info["prog"].path("moog_" + d), keys))
        if "mpulse_front0" in at:   # it12 (msg 149): the Moog's pulse down the town from the lens
            tracks += NL.moog_pulse_tracks(info["prog"].path, cam_at)
        if "hh_phase" in at:
            tracks += [AU.track(info["prog"].path("hh_phase", "value"), hh_phase),
                       AU.track(info["prog"].path("hh_amt"), hh_amt)]
        if "snare_phase" in at:
            tracks += [AU.track(info["prog"].path("snare_phase", "value"), sn_phase),
                       AU.track(info["prog"].path("snare_amt"), sn_amt)]
        if "flick" in at:
            # the phrases the camera spends in the town (the approach: the Moog's skyline; the reveal: the ocean)
            # (25.3 and 36.6 sit on the arrival and Quay crashes with the toms' fills: left to them)
            tracks += NL.flick_tracks(info["prog"].path, NL.FLICK_PHRASES)
        if info["group"] == "crown":   # the landmark crowns swell on the bass's swells (its top: 0.7-0.9)
            routes.append(R("stem.bass.level", "material/%s/emissionIntensity" % n,
                            NL.CROWN_BASS * info["doc"]["emissionIntensity"], attackMs=350.0, decayMs=1800.0,
                            gain=5.0, offset=-3.5, clampEnabled=True, clampMin=0.0, clampMax=1.0))
    # the Lantern's bell itself: +25 % -> +70 %
    routes.append(R("stem.bass.level", "material/ndBell/emissionIntensity", ROLE_GAIN["landmark"] * 0.45, **lvl_slow))
    # the isles' cloud glow answers the second ring as it passes (240 m/s from the bell, fading to 0 by 450 m)
    for name in ("lampwick", "gardens"):
        if name in GLOW_INDEX:
            gp = GLOW_POS[name]
            d = math.dist([gp[0], gp[2]], [BELL_POS[0], BELL_POS[2]])
            w = max(0.0, 1.0 - d / NL.ISLE_RING["outer"])
            if w > 0:
                routes.append(R("stem.kick.onset", "clouds/glow/%d/intensity" % GLOW_INDEX[name],
                                round(1.2 * w * GLOW_BASE[name], 3), attackMs=20.0, decayMs=400.0,
                                delayMs=round(1000.0 * d / NL.ISLE_RING["speed"], 1)))
    # the cosmic ocean: a ring on the reveal's phrases, the break's rim glow swelling with it
    if OCEAN_RING:
        tracks += NL.ocean_tracks("ndOcean", OCEAN_RING["radius_op"], OCEAN_RING["amount_op"], OCEAN_RING["radius"],
                                  [p for p in PAD_PHRASES if p > 56.0])
    if "ocean_break" in GLOW_INDEX:
        b = GLOW_BASE["ocean_break"]
        keys = [{"time": 0.0, "value": b, "interp": "linear"}]
        for p in [p for p in PAD_PHRASES if p > 56.0]:
            keys += [{"time": p, "value": b, "interp": "smooth"}, {"time": p + 0.9, "value": round(b * 1.8, 3),
                                                                     "interp": "smooth"},
                     {"time": p + 3.3, "value": b, "interp": "smooth"}]
        tracks.append(AU.track("clouds/glow/%d/intensity" % GLOW_INDEX["ocean_break"], keys))
    return routes, tracks


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
    ap.add_argument("--no-mist", action="store_true", help="ADR-2119 A/B: no mist sheets")
    ap.add_argument("--no-churn", action="store_true", help="ADR-2119 A/B: no churn, no edge air")
    ap.add_argument("--no-vortex", action="store_true", help="ADR-2120 A/B: the ocean break as it was")
    ap.add_argument("--wisp", action="append", default=[], metavar="KEY=VALUE",
                    help="override one wisp key (a number, or JSON); repeatable")
    ap.add_argument("--quality", default="offline", choices=sorted(CLOUD_QUALITY),
                    help="the cloud march's cost preset; 'live' writes <name>-live.* unless --name is given")
    ap.add_argument("--cloud", action="append", default=[], metavar="KEY=VALUE",
                    help="override one cloud quality knob (for measuring a preset); repeatable")
    ap.add_argument("--no-key-shadow", action="store_true", help="the moon key casts no shadow (measuring a preset)")
    ap.add_argument("--env", action="append", default=[], metavar="KEY=VALUE",
                    help="override one environment key (number); repeatable")
    ap.add_argument("--plug-fade", default="", help="A/B: start,end,floor of the plug light's dip (iteration 10)")
    ap.add_argument("--check-flights", action="store_true", help="msg 123: test every flight against the town (Blender)")
    ap.add_argument("--open-near", action="append", default=[], metavar="ID:X,Y,Z:R",
                    help="with --check-flights: find the nearest open-sky floor point")
    ap.add_argument("--check-ground", action="store_true",
                    help="msg 130: every figure on a floor, not in a wall (Blender); --fix-ground writes the fixes")
    ap.add_argument("--fix-ground", action="store_true", help="with --check-ground: move/drop failing figures, re-check")
    ap.add_argument("--city-off", action="store_true", help="A/B: iteration 11's city light envelopes off")
    ap.add_argument("--proto-flight", action="store_true", help="add the prototype flight (flyer00) to the cast")
    ap.add_argument("--crowd-test", type=int, default=0, help="capacity probe: N aliens (not the film)")
    a = ap.parse_args()
    global NO_WISPS, CROWD_TEST, PLUG_FADE, EDGE_AIR
    CROWD_TEST = a.crowd_test
    global PROTO_FLIGHT, CITY_OFF
    CITY_OFF = a.city_off
    PROTO_FLIGHT = a.proto_flight
    if a.plug_fade:
        PLUG_FADE = tuple(float(v) for v in a.plug_fade.split(","))
    NO_WISPS = a.no_wisps
    global NO_MIST, NO_VORTEX
    NO_MIST = a.no_mist
    NO_VORTEX = a.no_vortex
    if a.no_churn:
        CHURN["amount"] = 0.0
        globals()["EDGE_AIR"] = 0.0
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
    if a.check_ground:
        rep = check_ground(doc, tracks, a.out)
        if rep["summary"]["fail"] and a.fix_ground:
            fixes_from_ground(rep)
            json.dump(GROUND_FIXES, open(GFIX_PATH, "w"), indent=1)
            doc, tracks = scene_doc(a.palette, sky)
            with open(scene_path, "w") as f:
                json.dump(doc, f, indent=1)
            with open(proj_path, "w") as f:
                routes, arc, sources = reactivity()
                json.dump(project_doc(os.path.relpath(scene_path, a.out), a.audio, tracks + arc, routes, sources), f, indent=1)
            rep = check_ground(doc, tracks, a.out, "ground_check_after")
        if rep["summary"]["fail"]:
            print("WARNING: %d figure(s) fail the ground check" % rep["summary"]["fail"])
            sys.exit(4)
    if a.check_flights:
        for q in a.open_near:
            i, p, r = q.split(":")
            OPEN_NEAR.append({"id": i, "near": [float(v) for v in p.split(",")], "radius": float(r), "lift": 10.0})
        bad = check_flights(a.out)
        for rnd in range(5):   # repair: push the failing flights' via points away from what they pass too close to
            if not bad:
                break
            for r in bad:
                if rnd >= 1:   # pushes did not clear it: fly it level over the top of its route, 2.5 m higher a round
                    lifts = FLIGHT_FIXES.setdefault("_lift", {})
                    lifts[r["id"]] = lifts.get(r["id"], 0.0) + 2.5
                fx = FLIGHT_FIXES.setdefault(r["id"], [])
                for t, p, q, d in r["violations"]:
                    n = [p[i] - q[i] for i in range(3)]
                    ln = math.sqrt(sum(v * v for v in n)) or 1.0
                    fx.append([r3(q), r3([v / ln * (0.9 - d) for v in n])])
            json.dump(FLIGHT_FIXES, open(FIX_PATH, "w"), indent=0)
            print("flight repair round %d: %s" % (rnd + 1, ", ".join(r["id"] for r in bad)))
            FLIGHT_LOG.clear()
            doc, tracks = scene_doc(a.palette, sky)
            with open(scene_path, "w") as f:
                json.dump(doc, f, indent=1)
            with open(proj_path, "w") as f:
                routes, arc, sources = reactivity()
                json.dump(project_doc(os.path.relpath(scene_path, a.out), a.audio, tracks + arc, routes, sources), f, indent=1)
            bad = check_flights(a.out)
        if bad:
            print("WARNING: %d flight(s) fail the clearance check: %s" % (len(bad), ", ".join(r["id"] for r in bad)))
            sys.exit(3)


if __name__ == "__main__":
    main()
