#!/usr/bin/env python3
"""DIGITAL MOSH: the source of truth for the scene and its projects.

    python3 examples/digital-mosh/build.py

Writes digital-mosh.scene.json (the world) and the projects beside it. Edit this file (and forms.py, land.py), not
the JSON: the palettes, the land, the forms' skeletons (shared by their SDFs and their voxel shells), the camera's
vantages, the stage table and the audio mapping all live here so they stay consistent.

Docs: docs/prototypes/digital-mosh/ -- 01 research, 02 design, 03 implementation, 05 palettes (where every colour
below comes from).
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import forms  # noqa: E402
import land  # noqa: E402
from forms import add, mul, norm  # noqa: E402

# ============================================================================================ palettes
# Every colour is a cluster extracted from a reproduction of a named painting (05-palettes.md), sRGB hex -> linear.


def lin(hexs, gain=1.0):
    def c(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return [round(c(int(hexs[i:i + 2], 16)) * gain, 4) for i in (1, 3, 5)]


def light_of(hexs):
    """A light's colour from a painted colour: the same chromaticity at unit peak."""
    v = lin(hexs)
    m = max(v)
    return [round(x / m, 4) for x in v]


# The contagion: Ernst's *Europe After the Rain II* infecting the dream (05-palettes.md).
INK = lin("#2d0c1f")                   # Ernst's aubergine: a failed cell
STRAIN = [0.9132, 0.003, 0.104]        # Ernst's oxblood at full chroma: impossible red
STRAIN_TO_CHARTREUSE = 0.331           # + this many OKLCH turns = Dali's Temptation green at full chroma
BARK = [lin("#201f1f"), lin("#3c3427"), lin("#574625")]   # Persistence of Memory: its darkest earths
STONE = lin("#b6b8af")                 # Dream Caused by the Flight of a Bee: the pale grey of its rocks

PAL = {
    # Dali, Dream Caused by the Flight of a Bee + The Persistence of Memory
    # Persistence's own sky band: Cap de Creus teal over olive-gold; the Bee's ochre for the pan
    "dream": dict(zenith=lin("#3f798d"), horizon=lin("#e4d7bf"), sun=light_of("#e4d7bf"), fog=lin("#b1c9d8"),
                  land=[lin("#c6bfae"), lin("#c1ae5e"), lin("#73512a")], groundSky=lin("#9f9985")),
    # de Chirico, The Disquieting Muses + Mystery and Melancholy of a Street
    "uncanny": dict(zenith=lin("#3b6e65"), horizon=lin("#efe1ab"), sun=light_of("#e49420"), fog=lin("#f6e6be"),
                    land=[lin("#c6bfae"), lin("#c1ae5e"), lin("#73512a")], groundSky=lin("#223b37")),
    # Tanguy, Indefinite Divisibility: the ochre drains to grey-blue infinity
    "infection": dict(zenith=lin("#84add1"), horizon=lin("#d7e2da"), sun=light_of("#d7e2da"), fog=lin("#b8c9c6"),
                      land=[lin("#d7e2da"), lin("#9ab3bc"), lin("#79898d")], groundSky=lin("#505655")),
    # Ernst, Europe After the Rain II: the rot
    "corruption": dict(zenith=lin("#4f1a23"), horizon=lin("#c7d2ce"), sun=light_of("#b47222"), fog=lin("#5f4c46"),
                       land=[lin("#a58971"), lin("#975736"), lin("#732e26")], groundSky=lin("#2d0c1f")),
    # Dali, The Elephants (its blood sky) over Tanguy, Slowly Toward the North (its night land)
    "nightmare": dict(zenith=lin("#8f2b30"), horizon=lin("#b35a43"), sun=light_of("#c27f54"), fog=lin("#563529"),
                      land=[lin("#4f697b"), lin("#36444e"), lin("#1d2c35")], groundSky=lin("#1d2c35")),
    # Tanguy, Multiplication of the Arcs: the debris greys, burning out to its own white
    "collapse": dict(zenith=lin("#373943"), horizon=lin("#aaaeb6"), sun=light_of("#ebeeec"), fog=lin("#9098a3"),
                     land=[lin("#747a84"), lin("#555962"), lin("#373943")], groundSky=lin("#181c26")),
    "light": dict(zenith=lin("#ebeeec"), horizon=lin("#ebeeec"), sun=light_of("#ebeeec"), fog=lin("#ebeeec"),
                  land=[lin("#c7c9cf"), lin("#aaaeb6"), lin("#9098a3")], groundSky=lin("#c7c9cf")),
    # Magritte, The Empire of Light: a day sky over a land still in night
    "respite": dict(zenith=lin("#94c0d9"), horizon=lin("#e1e4d9"), sun=light_of("#e1e4d9"), fog=lin("#505655"),
                    land=[lin("#505655"), lin("#323637"), lin("#25292a")], groundSky=lin("#25292a")),
}

# ============================================================================================ layout
# The stage is the salt pan (land.PAN, centred at (4, -8), 110 m across). Heights are the engine's (land.py).
TREE_XZ = (-6.0, 0.0)
TANGUY_XZ = (12.0, -14.0)
DOUBLE_XZ = (-34.0, -46.0)
HIDDEN = [0.0, -400.0, 0.0]                # parked under the land (its shadow cannot reach the ground)
TANGUY_HOVER = 1.55 + 0.11                 # its needle's tip (local y -1.55) a hand's width above its shadow


def on_ground(xz, lift=0.0):
    return [xz[0], round(land.height(*xz) + lift, 3), xz[1]]


OLIVE = forms.Olive(seed=7)
TREE_AT = on_ground(TREE_XZ, -0.05)
TANGUY_AT = on_ground(TANGUY_XZ, TANGUY_HOVER)
DOUBLE_AT = on_ground(DOUBLE_XZ, TANGUY_HOVER)
# the macroblock that does not refresh (Recovery): on the long limb, two thirds of the way out
_b0, _d0, _s0 = OLIVE.limbs[0]
STUCK_AT = add(TREE_AT, forms.lerp(_s0[1][0], _s0[1][1], 0.6))
SUN_DIR = norm([0.42, -0.2, 0.88])         # the direction the light travels: low (~11 deg), from behind-left

# ============================================================================================ helpers


def field(name, position=None, **f):
    f.setdefault("falloff", {"kind": "none"})
    return {"name": name, "kind": "field", "position": position or [0, 0, 0], "field": f}


def op(kind, dst, **kw):
    o = {"kind": kind, "dst": dst}
    o.update(kw)
    return o


def program_op(prog_fn, kind, nth=1):
    """The 1-based index of the nth op of `kind` in a program (material/<prog>/op/<i>/<kind>/...)."""
    seen = 0
    for k, o in enumerate(prog_fn()["ops"], start=1):
        if o["kind"] == kind:
            seen += 1
            if seen == nth:
                return k
    raise KeyError(kind)


# ============================================================================================ fields
def fields():
    return [
        # the contagion, as every consumer reads it
        field("stain", kind="grid", reference="contagion", strength=1.0),
        # its source: the Tanguy object (the first block that went bad)
        field("infect", position=TANGUY_AT, kind="sphere", radius=1.8, softness=2.5, strength=0.0),
        # how it creeps: along curl noise and a little outward (ink in water; Ernst's decalcomania)
        field("creep", kind="curlNoise", frequency=0.11, speed=0.04, seed=5, strength=1.0),
        field("outward", position=TANGUY_AT, kind="radialVector", strength=0.22),
        field("flow", kind="compound", children=["creep", "outward"], combine="add", strength=1.0),
        # infection climbs: 1 below the plane, which rises with the stage
        field("climb", position=[0, 0.0, 0], kind="plane", axis=[0, 1, 0], softness=1.2, invert=True, strength=1.0),
        # rot: how eaten a point is. ONE compound level (a compound inside a compound is 0 on the GPU); the grid's
        # ceiling (ADR-1163) bounds it to [0, 1].
        field("rot", kind="compound", children=["stain", "climb"], combine="multiply", strength=1.0),
        # the stuck macroblock (Recovery): a box of space that did not refresh
        field("stuck", position=STUCK_AT, kind="box", size=[0.45, 0.45, 0.45], softness=0.02, strength=0.0),
        # the kick: fronts travelling out across the pan from the Tanguy object
        field("kick", position=TANGUY_AT, kind="onset", onsetSource="low", onsetDecay=1.2, onsetWidth=4.0,
              audioSpeed=24.0, waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
        # the fragments' release: up, and a tumble
        field("lift", kind="direction", axis=[0, 1, 0], strength=1.0),
        field("tumble", kind="curlNoise", frequency=0.4, speed=0.3, seed=9, strength=1.0),
    ]


CONTAGION = {
    "name": "contagion", "enabled": True, "mode": "scalar", "wrap": "clamp",
    "resolution": [128, 1, 128], "boundsMin": [-60, -6, -68], "boundsMax": [68, 6, 60],
    "injectField": "infect", "injectRate": 1.0, "velocityField": "flow", "advect": 0.0,
    "diffusion": 0.4, "diffuseIterations": 4, "dissipation": 0.0, "ceiling": 1.0,
    "simRate": 30.0, "maxSubSteps": 4, "seed": 2026, "seedAmount": 0.0, "checkpointInterval": 5.0,
}


# ============================================================================================ material programs
def infected(name, cells_per_m, fieldname, base_ops, remap_stain=False, seed=3, gate_far=0.0):
    """An infected surface (ADR-1162): every cell of a square lattice fails when the contagion passes the cell's own
    random. A failed cell is ink (dark, glossy); the cells that failed most recently -- the growth front -- glow, and
    a few failed cells glow forever (stuck pixels). The glow is the strain, turned by timbre (hueShift, routed),
    flaring with the treble and the kick's front. `base_ops` write the healthy colour into r6 (may use r5)."""
    ops = [
        op("input", 0, input="worldPosition"),
        op("quantize", 1, srcA=0, value=cells_per_m, seed=seed),
        op("field", 2, field=fieldname),
        op("field", 3, field="stuck"),
        op("add", 2, srcA=2, srcB=3),
    ]
    if remap_stain:
        ops.append(op("remap", 2, srcA=2, value=1, constant=[0.04, 0.9, 0.0, 1.0]))
    ops += [
        op("swizzle", 4, srcA=1, constant=[3, 3, 3, 3]),
        op("constant", 5, constant=[-1, -1, -1, -1]),
        op("multiply", 4, srcA=4, srcB=5),
        op("add", 4, srcA=2, srcB=4),                                     # d = contagion - cell random
        op("smoothstep", 7, srcA=4, constant=[0.0, 0.035, 0, 0]),
        op("remap", 7, srcA=7, value=1, constant=[0.0, 1.0, 1.0, 0.0]),
        op("smoothstep", 4, srcA=4, constant=[0.0, 0.004, 0, 0]),         # m: the cell has failed
        op("multiply", 7, srcA=7, srcB=4),                                # the growth front
        op("noise", 3, srcA=1, value=31.0, seed=seed + 7),
        op("smoothstep", 3, srcA=3, constant=[0.73, 0.75, 0, 0]),
        op("multiply", 3, srcA=3, srcB=4),                                # stuck pixels among the failed
        op("constant", 5, constant=[0.5, 0.5, 0.5, 0.5]),
        op("multiply", 3, srcA=3, srcB=5),
        op("add", 7, srcA=7, srcB=3),                                     # the glow mask
    ]
    ops += base_ops
    ops += [
        op("constant", 5, constant=INK + [1]),
        op("mixBy", 6, srcA=6, srcB=5, srcC=4),
        op("constant", 5, constant=STRAIN + [1]),
        op("constant", 3, constant=[0, 0, 0, 0]),
        op("hueShift", 5, srcA=5, srcB=3, value=0.0),                     # timbre turns the strain (routed)
        op("multiply", 7, srcA=7, srcB=5),
        op("input", 3, input="audio"),
        op("swizzle", 3, srcA=3, constant=[3, 3, 3, 3]),                  # treble
        op("field", 2, field="kick"),
        op("add", 3, srcA=3, srcB=2),
        op("remap", 3, srcA=3, value=0, constant=[0.0, 1.0, 0.25, 1.6]),
        op("multiply", 7, srcA=7, srcB=3),
        op("constant", 2, constant=[0.92, 0.92, 0.92, 1]),
        op("constant", 3, constant=[0.16, 0.16, 0.16, 1]),
        op("mixBy", 2, srcA=2, srcB=3, srcC=4),                           # ink is glossy: it reflects the sky
    ]
    prog = {"name": name, "ops": ops, "baseColor": 6, "metallic": -1, "roughness": 2, "emission": 7,
            "emissionIntensity": 0.0, "opacity": -1}
    if gate_far > 0:
        prog["gate"] = {"near": 0.0, "far": gate_far}
    return prog


def land_colour_ops(world_pos_reg=0):
    """The land's healthy colour into r6: a ramp over the slope (flat pan -> slope -> cliff) from the stage's painting,
    mottled at the scale of dunes."""
    p = PAL["dream"]["land"]
    return [
        op("input", 6, input="uv"),
        op("swizzle", 6, srcA=6, constant=[1, 1, 1, 1]),                  # slope
        op("remap", 6, srcA=6, value=1, constant=[0.0, 0.42, 0.0, 1.0]),
        op("ramp", 6, srcA=6, constant=p[0] + [1], constant2=p[1] + [1], constant3=p[2] + [1]),
        op("noise", 5, srcA=world_pos_reg, value=0.035, seed=7),
        op("remap", 5, srcA=5, value=1, constant=[0.25, 0.75, 0.84, 1.12]),
        op("multiply", 6, srcA=6, srcB=5),
    ]


def ground_program():
    # The near land: the stage's painting by slope, mottled, and the contagion's ink and front on it.
    return infected("ground", 2.2, "stain", land_colour_ops(), remap_stain=True, seed=3)


def ground_far_program():
    # The far land: the same colours, nothing else (the contagion never reaches it). Cheap: it covers the horizon.
    ops = [op("input", 0, input="worldPosition")] + land_colour_ops()
    return {"name": "groundFar", "ops": ops, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 0.0, "opacity": -1}


def bark_program():
    return infected("bark", 6.0, "rot", [
        op("noise", 6, srcA=0, value=2.3, seed=5),
        op("ramp", 6, srcA=6, constant=BARK[0] + [1], constant2=BARK[1] + [1], constant3=BARK[2] + [1]),
    ], seed=11)


def stone_program():
    return infected("skin", 5.0, "rot", [op("constant", 6, constant=STONE + [1])], seed=13)


def blocks_program(name, colour_a, colour_b):
    # The blocks are the object's own matter, cut on the codec's grid: most keep the object's colour; one in six is
    # a stuck block, lit in the strain and flaring with the kick's front.
    return {"name": name, "ops": [
        op("input", 0, input="instanceRandom"),
        op("swizzle", 1, srcA=0, constant=[1, 1, 1, 1]),
        op("ramp", 2, srcA=1, constant=colour_a + [1], constant2=colour_b + [1], constant3=colour_a + [1]),
        op("swizzle", 3, srcA=0, constant=[0, 0, 0, 0]),
        op("smoothstep", 3, srcA=3, constant=[0.83, 0.84, 0, 0]),
        op("constant", 4, constant=STRAIN + [1]),
        op("multiply", 4, srcA=4, srcB=3),
        op("field", 5, field="kick"),
        op("remap", 5, srcA=5, value=0, constant=[0.0, 1.0, 0.6, 3.0]),
        op("multiply", 4, srcA=4, srcB=5),
        op("constant", 6, constant=INK + [1]),
        op("mixBy", 2, srcA=2, srcB=6, srcC=3),
    ], "baseColor": 2, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 0.0, "opacity": -1}


# ============================================================================================ the camera
# The camera travels across the land (brief §13, and the owner's ask). Each stage has two or three vantages -- (eye,
# target, fov, roll), eyes given as (x, metres above the ground, z) and lifted onto the land's real height -- and the
# camera moves between them on the music's phrases (bars, later), in long smooth moves early and abrupt jumps late.
# A slow drift (LFOs, deeper with the energy) keeps it floating between moves. Every move is a straight line between
# two eyes on the land; check_moves() asks the engine for the land's height along each one.


def eye(x, above, z):
    return [x, round(land.height(x, z) + above, 3), z]


VANTAGES = {
    # floating, observational: the classic frame; low along the riverbed; the shadow line leading to the viewer
    "Dream": [(eye(2, 1.5, 26), [-2, 3.4, -4], 30, 0), (eye(-44, 2.4, 30), [-2, 3.2, -6], 32, 0),
              (eye(30, 2.6, 22), [-5, 3.0, -4], 28, 0)],
    # less predictable: from behind (the double revealed against the mesas), a slow crane up, the low ground
    "Uncanny": [(eye(16, 1.2, 30), [-4, 3.0, -8], 38, 0), (eye(-26, 1.8, -34), [8, 2.6, -8], 34, 0),
                (eye(6, 9.0, 42), [0, 1.0, -6], 36, 0)],
    # toward the first bad block; the stain at the tree's roots; a wide reveal of the spread across the pan
    "Infection": [(eye(26, 1.8, 6), [12, 1.6, -14], 36, 0), (eye(0, 2.4, 12), [-6, 2.2, 0], 40, 0),
                  (eye(44, 6.0, -38), [0, 1.0, 0], 34, 0)],
    # lower, closer, under the drooping limbs
    "Corruption": [(eye(-2, 1.0, 9), [-6, 4.2, 0], 46, 0), (eye(18, 1.3, -6), [-6, 3.0, 0], 42, 0),
                   (eye(-20, 3.6, 12), [4, 1.2, -10], 44, 0)],
    # impossible: inside the tree, a sudden height, the ground at ankle height (P5: the horizon tilts, not shakes)
    "Nightmare": [(eye(-4.2, 2.9, 1.9), [12, 2.6, -14], 58, 7), (eye(8, 15.0, 18), [-6, 1.5, 0], 52, -5),
                  (eye(-14, 0.5, -4), [12, 2.4, -14], 60, 11)],
    "Collapse": [(eye(10, 6.0, 30), [0, 3.0, -6], 48, 0)],
    "Decay": [(eye(26, 14.0, 40), [0, 2.0, -6], 50, 0)],
    "Pixels": [(eye(10, 26.0, 58), [0, 0.0, -10], 52, 0)],
    "Light": [(eye(10, 26.0, 58), [0, 0.0, -10], 52, 0)],
    "Respite": [(eye(16, 1.6, 28), [-2, 3.0, -6], 32, 0)],
    "Recovery": [(eye(2, 1.5, 26), [-2, 3.4, -4], 30, 0)],
}
# how a stage moves between its own vantages: (trigger kind, every, seconds, easing)
MOVES = {"Dream": ("phrase", 1, 16.0, "smooth"), "Uncanny": ("phrase", 1, 12.0, "smooth"),
         "Infection": ("phrase", 1, 9.0, "easeInOut"), "Corruption": ("bar", 4, 5.0, "easeInOut"),
         "Nightmare": ("bar", 2, 1.2, "easeIn")}


def check_moves():
    """Every eye and every straight move between a stage's vantages clears the land by at least 0.4 m."""
    problems = []
    for st, vs in VANTAGES.items():
        eyes = [v[0] for v in vs]
        for i, a in enumerate(eyes):
            b = eyes[(i + 1) % len(eyes)]
            pts = [forms.lerp(a, b, k / 24) for k in range(25)]
            hs = land.heights([(p[0], p[2]) for p in pts])
            for p, h in zip(pts, hs):
                if p[1] < h + 0.4:
                    problems.append(f"{st} move {i}: {[round(x, 1) for x in p]} is {p[1] - h:.2f} m above the land")
                    break
    return problems


# ============================================================================================ the scene
TREE_PARTS = ["trunk", "limb0", "limb1", "limb2"]
TREE_CELL = 0.16
TANGUY_CELL = 0.2


def sdf_node(name, at, tree, program, lo, hi, base_colour, roughness, steps=128, cast=True):
    material = {"baseColor": base_colour, "roughness": roughness, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                "emissiveIntensity": 1.0}
    if program:
        material["program"] = program
    return {"name": name, "kind": "sdf", "position": at, "sdf": {
        "tree": tree, "material": material,
        "renderMode": "raymarch", "boundsMin": lo, "boundsMax": hi,
        "compile": True, "maxSteps": steps, "stepScale": 0.8, "epsilon": 0.0008, "normalEpsilon": 0.003,
        "maxDistance": 900.0, "castShadows": cast, "depthPrepass": True,
        "look": {"aoStrength": 0.6, "aoDistance": 0.6}}}


def limb_at(li):
    return add(TREE_AT, OLIVE.limbs[li][0])


def blocks_node(name, at, pts, program, roughness):
    return {"name": name, "kind": "procedural", "position": at, "procedural": {
        "source": {"kind": "box", "size": [1.0, 1.0, 1.0], "subdivisions": 1},
        "variation": {"seed": 3},
        "distribution": {"kind": "points", "points": pts},
        "effectors": [
            {"field": "rot", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            {"field": "lift", "op": "positionOffset", "blend": "add", "strength": 0.0},
            {"field": "tumble", "op": "positionOffset", "blend": "add", "strength": 0.0},
            {"field": "tumble", "op": "rotation", "blend": "add", "strength": 0.0},
            {"field": "kick", "op": "positionOffset", "blend": "add", "strength": 0.0, "axis": [0, 1, 0]},
        ],
        "lod": {"cull": True, "count": 1},
        "material": {"baseColor": INK, "roughness": roughness, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                     "emissiveIntensity": 1.0, "program": program}}}


def scene():
    d = PAL["dream"]
    sky = {"enabled": True, "background": True, "useKeyLight": False, "sunDirection": mul(SUN_DIR, -1.0),
           "zenithColor": d["zenith"], "horizonColor": d["horizon"], "groundColor": d["groundSky"],
           "sunColor": d["sun"], "haze": 0.16, "sunIntensity": 1.0, "sunSize": 0.02, "sunGlow": 0.3,
           "intensity": 1.0}
    nodes = fields() + [
        {"name": "land", "kind": "terrain", "position": [0, 0, 0], "world": land.world(False),
         "terrain": {"chunkSize": 30.0, "resolution": 24, "lodLevels": 4, "lodDistance": 60.0,
                     "viewDistance": 1300.0, "shadowDistance": 200.0, "skirtDepth": 2.0, "groundMottle": False},
         "material": {"program": "ground", "baseColor": d["land"][1], "roughness": 0.92, "metallic": 0.0}},
        {"name": "far", "kind": "terrain", "position": [0, -0.8, 0], "world": land.world(True),
         "terrain": {"chunkSize": 400.0, "resolution": 32, "lodLevels": 3, "lodDistance": 500.0,
                     "viewDistance": 7500.0, "shadowDistance": 400.0, "skirtDepth": 6.0, "groundMottle": False},
         "material": {"program": "groundFar", "baseColor": d["land"][1], "roughness": 0.95, "metallic": 0.0}},
    ]
    nodes.append(sdf_node("trunk", TREE_AT, OLIVE.trunk_tree(), "bark", [-2.2, -0.6, -2.2], [2.6, 4.2, 2.2],
                          BARK[1], 0.85))
    for li in range(3):
        blo, bhi = OLIVE.limb_bounds(li)
        nodes.append(sdf_node(f"limb{li}", limb_at(li), OLIVE.limb_tree(li), "bark",
                              [v - 1.2 for v in blo], [v + 1.2 for v in bhi], BARK[1], 0.85))
    nodes.append(sdf_node("tanguy", TANGUY_AT, forms.tanguy_tree(), "skin", [-1.6, -1.75, -1.4], [1.6, 3.1, 1.4],
                          STONE, 0.3, steps=96))
    # the double: the same form, the same light, somewhere it should not be (P2). Parked until the Uncanny.
    double = forms.tanguy_tree()
    double["root"] = double["root"]["children"][0]   # no contagion on the double: it is a memory, not the thing
    nodes.append(sdf_node("double", HIDDEN, double, "", [-1.6, -1.75, -1.4], [1.6, 3.1, 1.4], STONE, 0.3, steps=96))
    nodes += [
        blocks_node("treeBlocks", TREE_AT, OLIVE.blocks(TREE_CELL), "treeBlocks", 0.75),
        blocks_node("tanguyBlocks", TANGUY_AT, forms.tanguy_blocks(TANGUY_CELL), "tanguyBlocks", 0.4),
        {"name": "motes", "kind": "particles", "particles": {
            # dust in the dream light; later the infected ground's spores
            "capacity": 20000, "seed": 4, "shape": "disc", "position": [2, 0.4, -6], "extent": [45, 0, 45],
            "direction": [0, 1, 0], "spread": 0.6, "spawnRate": 40, "speedMin": 0.05, "speedMax": 0.25,
            "gravity": [0, 0.04, 0], "drag": 0.2, "turbulence": 0.25, "turbulenceScale": 0.15,
            "turbulenceSpeed": 0.15, "sizeStart": 0.035, "sizeEnd": 0.01, "sizeVariance": 0.6,
            "lifetimeMin": 6.0, "lifetimeMax": 11.0, "colorStart": [1.0, 0.85, 0.6, 0.9],
            "colorEnd": [1.0, 0.7, 0.5, 0.0], "emissive": 2.0, "blend": "additive", "softness": 0.6}},
    ]
    e0, t0, fov0, _r = VANTAGES["Dream"][0]
    return {
        "format": "avgen-scene", "version": 1, "name": "DIGITAL MOSH",
        "_note": "Generated by build.py; edit that, not this file.",
        "camera": {"mode": 1, "position": e0, "target": t0, "fov": fov0},
        "environment": {
            "intensity": 0.22, "background": d["horizon"], "fogColor": d["fog"],
            "shadowRange": 200.0, "shadowCascades": 3,
            # aerial perspective: thin near the ground, the sky's own colour with distance -- the land never ends,
            # it dissolves (S10; the owner's dead-space rule). The far ranges sit 3 km out in it.
            "volumeDensity": 0.0006, "volumeScattering": 0.9, "volumeAbsorption": 0.12, "volumeAnisotropy": 0.25,
            "volumeSteps": 24, "volumeMaxDistance": 4000.0, "fogHeight": 0.0, "fogHeightFalloff": 0.012,
            "fogSky": 1.0, "fogSkyDistance": 3200.0,
            "sky": sky,
        },
        "lights": [
            {"name": "sun", "id": "sun", "type": "directional", "role": "key", "direction": SUN_DIR,
             "color": d["sun"], "intensity": 9.0, "castsShadow": True, "shadowStrength": 1.0, "softness": 0.35},
            # the first fracture's light: the strain, at the Tanguy object, off until the infection
            {"name": "fracture", "id": "fracture", "type": "point", "position": add(TANGUY_AT, [0, 0.5, 0]),
             "color": STRAIN, "intensity": 0.0, "range": 30.0, "radius": 0.6, "castsShadow": False,
             "volumetric": 1.0},
        ],
        "grids": [CONTAGION],
        "materialPrograms": [ground_program(), ground_far_program(), bark_program(), stone_program(),
                             blocks_program("treeBlocks", BARK[0], BARK[1]),
                             blocks_program("tanguyBlocks", STONE, lin("#9f9985"))],
        "nodes": nodes,
    }


# ============================================================================================ the stages
# One preset per stage (and per vantage). Continuous expression rides on top as routes gated by macros the stage sets
# (restraint early, brief §16). Each stage breaks ONE more law than the last (research S2).
BASE = dict(pal="dream", inject=0.0, advect=0.0, dissip=0.6, climb=0.0, eat=0.0, eatStone=0.0,
            melt=[0.0, 0.0, 0.0], lift=[0.0, 0.0, 0.0], bark=0.016, barkSpeed=0.0,
            rise=0.0, tumble=0.0, spin=0.0, kick=0.0, sunYaw=0.0, fracture=0.0, double=False, motes=40.0,
            echo=0.0, mosh=0.0, moshBlock=32.0, glitch=0.0, pixel=0.0, poster=0.0, sort=0.0, split=0.0,
            exposure=0.0, bloom=0.08, volDen=0.0006, sun=9.0,
            gMicro=0.15, gRhythm=0.0, gMosh=0.0, gGlitch=0.0, gMelt=0.0, gLift=0.0,
            glowPlain=0.0, glowBark=0.0, glowBlocks=0.0, keyframe=0.0, heal=0.0)


def stage(**kw):
    v = dict(BASE)
    v.update(kw)
    return v


COLLAPSE_COMMON = dict(inject=5.0, advect=4.0, climb=12.0, eat=1.0, eatStone=1.0, fracture=5000.0, double=True,
                       glowPlain=8.0, glowBark=6.0, gMicro=1.0, gRhythm=1.0, gMosh=1.0, gMelt=1.0, gLift=1.0)
STAGES = {
    # Beautiful, quiet, hypnotic. Only the light, the dust and the camera move.
    "Dream": stage(),
    # Relation errors only (Magritte, S12; de Chirico's light, D1/D2): the shadows swing against the sky's sun, the
    # Tanguy object appears twice, the long limb lifts a little against gravity, the sky turns Chirico's green.
    "Uncanny": stage(pal="uncanny", sunYaw=26.0, double=True, melt=[-0.05, 0.0, 0.0], echo=0.08, gMicro=0.3,
                     gRhythm=0.1, volDen=0.0007),
    # The first block goes bad: the strain at the Tanguy object -- its light, its stain on the pan, the first blocks.
    # The land drains toward Tanguy's grey-blue.
    "Infection": stage(pal="infection", inject=1.4, advect=0.9, dissip=0.02, climb=1.6, eat=0.18, eatStone=0.35,
                       melt=[0.06, 0.0, 0.0], fracture=700.0, double=True, sunYaw=8.0, glowPlain=2.5, glowBark=1.8,
                       glowBlocks=3.0, echo=0.1, gMicro=0.5, gRhythm=0.35, gMosh=0.15, gMelt=0.2, motes=70.0,
                       exposure=-0.1, volDen=0.0008),
    # Ernst's rot: the tree eaten from the root up, its bark flowing like wax, limbs drooping, the first blocks lifting.
    "Corruption": stage(pal="corruption", inject=2.4, advect=2.0, dissip=0.0, climb=4.6, eat=0.32, eatStone=0.5,
                        melt=[0.16, 0.08, -0.06], bark=0.05, barkSpeed=0.5, rise=0.35, tumble=0.25, spin=0.4,
                        kick=0.25, fracture=1900.0, double=True, glowPlain=4.0, glowBark=3.0, glowBlocks=5.0,
                        echo=0.18, mosh=0.0, exposure=-0.4, gMicro=0.75, gRhythm=0.65, gMosh=0.12, gMelt=0.5,
                        gLift=0.3, motes=160.0, volDen=0.0011),
    # The Elephants' blood sky over Tanguy's night land: the tree is blocks, its limbs float free.
    "Nightmare": stage(pal="nightmare", inject=3.5, advect=3.4, dissip=0.0, climb=9.5, eat=0.55, eatStone=0.7,
                       melt=[0.26, 0.18, -0.14], lift=[1.3, 0.7, 1.0], bark=0.09, barkSpeed=1.0, rise=1.8,
                       tumble=1.1, spin=1.5, kick=0.6, sunYaw=-34.0, fracture=3200.0, double=True, glowPlain=6.0,
                       glowBark=4.5, glowBlocks=7.0, echo=0.3, mosh=0.03, moshBlock=24.0, exposure=-0.7,
                       gMicro=1.0, gRhythm=1.0, gMosh=0.85, gGlitch=0.6, gMelt=1.0, gLift=1.0, motes=420.0,
                       volDen=0.0014),
    # The collapse, through the representations the renderer built the world from (G10, brief §15), on the bar grid:
    # geometry...
    "Collapse": stage(pal="nightmare", melt=[0.4, 0.3, -0.3], lift=[3.0, 2.0, 2.5], bark=0.12, barkSpeed=1.5,
                      rise=5.0, tumble=2.5, spin=3.0, kick=1.2, glowBlocks=9.0, echo=0.35, mosh=0.25, moshBlock=48.0,
                      exposure=-0.6, gGlitch=0.8, motes=900.0, volDen=0.0016, **COLLAPSE_COMMON),
    #   ... fragments become particles and temporal fragments
    "Decay": stage(pal="nightmare", melt=[0.4, 0.3, -0.3], lift=[5.0, 3.5, 4.0], bark=0.12, barkSpeed=1.5, rise=9.0,
                   tumble=4.0, spin=5.0, kick=1.6, glowBlocks=10.0, echo=0.5, mosh=0.6, moshBlock=64.0,
                   exposure=-0.5, gGlitch=1.0, motes=2600.0, volDen=0.0018, **COLLAPSE_COMMON),
    #   ... pixels, then colour
    "Pixels": stage(pal="collapse", melt=[0.4, 0.3, -0.3], lift=[6.0, 4.5, 5.0], bark=0.12, barkSpeed=1.5,
                    rise=12.0, tumble=5.0, spin=6.0, kick=1.6, glowBlocks=10.0, echo=0.4, mosh=0.5, moshBlock=96.0,
                    pixel=18.0, poster=5.0, sort=0.6, exposure=-0.3, gGlitch=1.0, motes=2600.0, volDen=0.0018,
                    **COLLAPSE_COMMON),
    #   ... and light: Tanguy's white.
    "Light": stage(pal="light", lift=[6.0, 4.5, 5.0], rise=12.0, tumble=5.0, spin=6.0, glowBlocks=10.0, echo=0.6,
                   mosh=0.3, pixel=40.0, poster=3.0, exposure=4.0, bloom=1.5, gGlitch=1.0, motes=2600.0,
                   volDen=0.002, **COLLAPSE_COMMON),
    # A breakdown's respite: Magritte's Empire of Light -- a day sky over a land still in night; the stain stays.
    "Respite": stage(pal="respite", sun=1.2, dissip=0.25, double=True, keyframe=1.0, heal=0.45, glowPlain=1.0, gMicro=0.3,
                     echo=0.05, volDen=0.0007),
    # The keyframe: the dream exactly as it was (P8). One macroblock did not refresh.
    "Recovery": stage(dissip=9.0, keyframe=1.0, heal=1.0),
}
LADDER = ["Dream", "Uncanny", "Infection", "Corruption", "Nightmare", "Collapse"]
DEPTH_AT = {"Uncanny": 0.08, "Infection": 0.28, "Corruption": 0.48, "Nightmare": 0.66, "Collapse": 0.86}
ENTRY = {"Dream": (8, "smooth"), "Uncanny": (12, "smooth"), "Infection": (6, "smooth"), "Corruption": (3, "easeIn"),
         "Nightmare": (2, "easeIn"), "Collapse": (4, "easeIn")}


def variant_names(st):
    return [st] + [f"{st} {k + 1}" for k in range(1, len(VANTAGES[st]))]


def sun_azimuth(yaw_deg):
    """The key's azimuth (Composition::lightAngles) turned by yaw: the shadows swing; the sky's sun stays."""
    return math.degrees(math.atan2(-SUN_DIR[0], -SUN_DIR[2])) + yaw_deg


def stage_values(name, v):
    pal = PAL[v["pal"]]
    hue_op = program_op(ground_program, "hueShift")
    vals = {
        "field/infect/strength": [v["inject"]], "grid/contagion/advect": [v["advect"]],
        "grid/contagion/dissipation": [v["dissip"]], "field/climb/position": [0.0, v["climb"], 0.0],
        "sdf/tanguy/node/eaten/amount": [v["eatStone"]],
        "lights/fracture/intensity": [v["fracture"]], "lights/sun/azimuth": [sun_azimuth(v["sunYaw"])],
        "lights/sun/color": pal["sun"], "lights/sun/intensity": [v["sun"]],
        "material/ground/emissionIntensity": [v["glowPlain"]], "material/bark/emissionIntensity": [v["glowBark"]],
        "material/skin/emissionIntensity": [v["glowBark"]],
        "material/treeBlocks/emissionIntensity": [v["glowBlocks"]],
        "material/tanguyBlocks/emissionIntensity": [v["glowBlocks"]],
        f"material/ground/op/{hue_op}/hueShift/value": [0.0],
        "nodes/double/position": DOUBLE_AT if v["double"] else HIDDEN,
        "particles/motes/spawnRate": [v["motes"]],
        "temporal/echo/strength": [v["echo"]], "temporal/mosh/amount": [v["mosh"]],
        "temporal/mosh/block": [v["moshBlock"]], "post/glitch/amount": [v["glitch"]],
        "post/display/pixelate": [v["pixel"]], "post/display/posterize": [v["poster"]],
        "post/sort/amount": [v["sort"]], "post/split/amount": [v["split"]],
        "camera/exposure/compensation": [v["exposure"]], "post/bloom/intensity": [v["bloom"]],
        "scene/volumeDensity": [v["volDen"]], "scene/fogColor": pal["fog"],
        "env/sky/zenithColor": pal["zenith"], "env/sky/horizonColor": pal["horizon"],
        "env/sky/groundColor": pal["groundSky"], "env/sky/sunColor": pal["sun"],
        "macros/keyframe": [v["keyframe"]], "macros/heal": [v["heal"]],
    }
    for g in ("gMicro", "gRhythm", "gMosh", "gGlitch", "gMelt", "gLift"):
        vals[f"macros/{g}"] = [v[g]]
    # the land takes the stage's painting (both lands, so the seam never shows)
    for prog_fn, prog in ((ground_program, "ground"), (ground_far_program, "groundFar")):
        i = program_op(prog_fn, "ramp")
        vals[f"material/{prog}/op/{i}/ramp/constant"] = pal["land"][0] + [1]
        vals[f"material/{prog}/op/{i}/ramp/constant2"] = pal["land"][1] + [1]
        vals[f"material/{prog}/op/{i}/ramp/constant3"] = pal["land"][2] + [1]
    for part in TREE_PARTS:
        vals[f"sdf/{part}/node/eaten/amount"] = [v["eat"]]
        vals[f"sdf/{part}/node/bark/amount"] = [v["bark"]]
        vals[f"sdf/{part}/node/bark/speed"] = [v["barkSpeed"]]
    for li in range(3):
        vals[f"sdf/limb{li}/node/melt/amount"] = [v["melt"][li]]
        vals[f"nodes/limb{li}/position"] = add(limb_at(li), [0.15 * v["lift"][li], v["lift"][li], 0.0])
    for blocks in ("treeBlocks", "tanguyBlocks"):
        vals[f"procedural/{blocks}/effector/2/strength"] = [v["rise"]]
        vals[f"procedural/{blocks}/effector/3/strength"] = [v["tumble"]]
        vals[f"procedural/{blocks}/effector/4/strength"] = [v["spin"]]
        vals[f"procedural/{blocks}/effector/5/strength"] = [v["kick"]]
    if name == "Recovery":
        vals["field/stuck/strength"] = [1.0]  # set here only: it persists for the rest of the piece
    return vals


def presets():
    out = []
    for name, v in STAGES.items():
        base = stage_values(name, v)
        for k, vname in enumerate(variant_names(name)):
            e, t, fov, roll = VANTAGES[name][k]
            vals = dict(base)
            vals.update({"camera/position": e, "camera/target": t, "camera/fov": [fov], "camera/roll": [roll]})
            out.append({"name": vname.lower(), "values": vals})
    return out


def states():
    def held(signal, threshold, frm, falling=False):
        t = {"kind": "signal", "signal": signal, "threshold": threshold, "from": frm, "hold": True}
        if falling:
            t["falling"] = True
        return t

    def after(seconds, frm):
        return {"kind": "elapsed", "threshold": seconds, "from": frm}

    S = []
    for k, name in enumerate(LADDER):
        variants = variant_names(name)
        entry = ENTRY[name]
        for vi, vname in enumerate(variants):
            trig = []
            if vi == 0:
                if k > 0:
                    # the ladder, from every vantage of the previous stage (ADR-1164 hold)
                    trig += [held("visual.depth", DEPTH_AT[name], f) for f in variant_names(LADDER[k - 1])]
                if name == "Uncanny":
                    trig.append(held("visual.lift", 0.55, "Respite"))
                if name == "Dream":
                    trig.append(after(20.0, "Recovery"))
            # travel: the previous vantage moves here on the music's phrases (or bars, later)
            if len(variants) > 1 and name in MOVES:
                kind, every, _s, _e = MOVES[name]
                # idle (ADR-1164): a camera move never interrupts a stage's own transition
                trig.append({"kind": kind, "every": every, "from": variants[vi - 1], "idle": True})
            seconds, easing = entry if vi == 0 else MOVES[name][2:4]
            S.append({"name": vname, "preset": vname.lower(), "transition": {"seconds": seconds, "easing": easing},
                      "triggers": trig})
    S.append({"name": "Decay", "preset": "decay", "transition": {"seconds": 3, "easing": "easeIn", "quantize": "bar"},
              "triggers": [after(6.0, "Collapse")]})
    S.append({"name": "Pixels", "preset": "pixels",
              "transition": {"seconds": 2, "easing": "easeIn", "quantize": "bar"}, "triggers": [after(5.0, "Decay")]})
    S.append({"name": "Light", "preset": "light", "transition": {"seconds": 3, "easing": "easeIn", "quantize": "bar"},
              "triggers": [after(4.0, "Pixels")]})
    S.append({"name": "Respite", "preset": "respite", "transition": {"seconds": 1.5, "easing": "smooth"},
              "triggers": [held("visual.lift", 0.28, f, falling=True)
                           for st in ("Infection", "Corruption", "Nightmare") for f in variant_names(st)]})
    # the keyframe: an instant cut from white to the calm dream (brief §15)
    S.append({"name": "Recovery", "preset": "recovery", "transition": {"seconds": 0.0, "easing": "linear"},
              "triggers": [after(3.5, "Light")]})
    return {"initial": "Dream", "states": S}


# ============================================================================================ the listening
LISTEN = {"kind": "interpret", "name": "listen", "settings": {"mappings": [
    {"name": "drive", "combine": "mean", "inputs": [
        {"signal": "audio.energy", "weight": 1.0}, {"signal": "audio.trebleLevel", "weight": 1.0}],
     "bias": -0.95, "gain": 3.0, "curve": 1.0},
    {"name": "bright", "combine": "mean", "inputs": [{"signal": "sonic.brightness.slow", "weight": 1.0}],
     "bias": -0.25, "gain": 2.0, "curve": 1.0},
]}}
ARC = {"kind": "interpret", "name": "arc", "settings": {"mappings": [
    {"name": "lift", "combine": "sum", "inputs": [
        {"signal": "macro.energy", "weight": 1.0}, {"signal": "macro.baseline", "weight": 1.0, "invert": True}],
     "bias": -2.5, "gain": 3.0, "curve": 1.0},
    {"name": "pace", "combine": "sum", "inputs": [
        {"signal": "macro.energy", "weight": 0.3}, {"signal": "visual.lift", "weight": 0.8},
        # weights are >= 0 (parameters), so heal enters inverted: + (1 - heal), and the bias pays the 1 back
        {"signal": "macro.heal", "weight": 1.0, "invert": True}], "bias": -1.05, "gain": 1.0, "curve": 1.0},
    {"name": "depth", "combine": "product", "inputs": [
        {"signal": "macro.dose", "weight": 1.0}, {"signal": "macro.keyframe", "weight": 1.0, "invert": True}],
     "bias": 0.0, "gain": 1.0, "curve": 1.0},
]}}
GATES = ["gMicro", "gRhythm", "gMosh", "gGlitch", "gMelt", "gLift"]


def macros():
    m = [{"name": n, "label": n.upper(), "default": 0.0, "targets": []}
         for n in ("energy", "baseline", "dose", "keyframe", "heal")]
    m.append({"name": "sensitivity", "label": "SENSITIVITY", "default": 0.25, "targets": []})
    m += [{"name": g, "label": g.upper(), "default": BASE[g], "targets": []} for g in GATES]
    return m


def gated(source, target, amount, gate, chain=None, component=None):
    r = {"source": source, "target": target, "op": "add", "amount": amount, "depthSource": f"macro.{gate}",
         "depthMin": 0.0, "depthMax": 1.0}
    if chain:
        r["chain"] = chain
    if component is not None:
        r["component"] = component
    return r


def PEAK(hold, fall):
    return {"envelope": "peakhold", "envelopeHoldMs": hold, "envelopeFallPerSecond": fall}


ZERO_DEFAULTS = [
    {"source": "audio.bass", "target": "root/scale", "op": "add", "amount": 0.0},
    {"source": "audio.mid", "target": "root/rotationSpeed", "op": "add", "amount": 0.0},
    {"source": "audio.rms", "target": "scene/brightness", "op": "add", "amount": 0.0},
    {"source": "audio.onset", "target": "root/impulse", "op": "add", "amount": 0.0},
]


def routes():
    hue_op = program_op(ground_program, "hueShift")
    r = list(ZERO_DEFAULTS)
    # ---- LAYER 4: the arc (03-implementation.md, "The arc")
    r.append({"source": "visual.drive", "target": "macros/energy", "op": "add", "amount": 1.0,
              "depthSource": "macro.sensitivity", "depthMin": 0.0, "depthMax": 4.0,
              "chain": {"attackMs": 1500, "decayMs": 3000}})
    r.append({"source": "visual.drive", "target": "macros/baseline", "op": "add", "amount": 1.0,
              "depthSource": "macro.sensitivity", "depthMin": 0.0, "depthMax": 4.0,
              "chain": {"attackMs": 30000, "decayMs": 30000}})
    r.append({"source": "visual.pace", "target": "macros/dose", "op": "add", "amount": 1.0,
              "chain": {"curve": "power", "curveAmount": 0.25, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                        "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": -0.05,
                        "remapOutMax": 0.0105, "integrate": True, "integrateMin": 0.0, "integrateMax": 1.0}})
    # ---- LAYER 1, micro: high frequencies are fine detail -- dust glints, the infected cells shimmer
    r.append(gated("audio.treble", "particles/motes/spawnRate", 260.0, "gMicro", {"attackMs": 40, "decayMs": 600}))
    r.append(gated("audio.onsetHigh", "material/ground/emissionIntensity", 3.0, "gRhythm", PEAK(20, 6.0)))
    for blocks in ("treeBlocks", "tanguyBlocks"):
        r.append(gated("audio.onsetHigh", f"material/{blocks}/emissionIntensity", 4.0, "gRhythm", PEAK(20, 6.0)))
    r.append(gated("audio.trebleLevel", "post/glitch/amount", 0.05, "gGlitch", {"attackMs": 30, "decayMs": 300}))
    # ---- LAYER 2, rhythmic: the kick fractures, the snare spreads
    r.append(gated("audio.onsetLow", "lights/fracture/intensity", 2500.0, "gRhythm", PEAK(30, 5.0)))
    for blocks in ("treeBlocks", "tanguyBlocks"):
        r.append(gated("audio.onsetLow", f"procedural/{blocks}/effector/5/strength", 0.6, "gRhythm", PEAK(40, 4.0)))
    r.append(gated("audio.onsetMid", "field/infect/strength", 4.0, "gRhythm", PEAK(60, 3.0)))
    r.append(gated("audio.onsetMid", "temporal/mosh/amount", 0.10, "gMosh", PEAK(60, 6.0)))
    r.append(gated("audio.onsetMid", "post/glitch/tear", 0.35, "gGlitch", PEAK(50, 6.0)))
    r.append(gated("audio.onsetLow", "post/split/amount", 6.0, "gGlitch", PEAK(15, 12.0)))
    # ---- LAYER 3, musical: bass is mass and gravity -- the limbs sag and lift, the bark flows, the haze breathes
    for li in range(3):
        r.append(gated("audio.bassLevel", f"sdf/limb{li}/node/melt/amount", 0.10 if li < 2 else -0.08, "gMelt",
                       {"attackMs": 400, "decayMs": 1200}))
        r.append(gated("audio.bass", f"nodes/limb{li}/position", 0.6, "gLift", {"attackMs": 300, "decayMs": 1500},
                       component=1))
    for part in TREE_PARTS:
        r.append(gated("audio.bassLevel", f"sdf/{part}/node/bark/amount", 0.04, "gMelt",
                       {"attackMs": 200, "decayMs": 900}))
    r.append(gated("macro.energy", "scene/volumeDensity", 0.0006, "gRhythm", {"attackMs": 2000, "decayMs": 4000}))
    # timbre turns the strain: bright music -> Temptation's chartreuse, dark -> Ernst's impossible red
    r.append(gated("visual.bright", f"material/ground/op/{hue_op}/hueShift/value", STRAIN_TO_CHARTREUSE, "gRhythm",
                   {"attackMs": 3000, "decayMs": 3000}))
    r.append(gated("visual.bright", "grid/contagion/advect", 1.5, "gRhythm", {"attackMs": 2000, "decayMs": 2000}))
    # ---- the camera floats between its moves: slow incommensurate drifts, deeper as the music grows (not shake)
    for src, comp, amt in (("driftA", 0, 1.6), ("driftB", 2, 1.6), ("driftC", 1, 0.3)):
        r.append({"source": f"lfo.{src}.bipolar", "target": "camera/position", "op": "add", "amount": amt,
                  "component": comp, "depthSource": "macro.energy", "depthMin": 0.6, "depthMax": 1.4})
    r.append({"source": "lfo.driftB.bipolar", "target": "camera/target", "op": "add", "amount": 0.6, "component": 0})
    r.append({"source": "lfo.driftA.bipolar", "target": "camera/target", "op": "add", "amount": 0.25, "component": 1})
    return r


POST = {
    "post/bloom/enabled": True, "post/bloom/threshold": 1.4, "post/bloom/emissionWeight": 0.7,
    "post/tonemap/chroma-retention": 0.55, "post/output/vignette": 0.24,
    "post/grade/contrast": 1.1, "post/grade/saturation": 1.08,
    "camera/exposure/mode": 0, "camera/mode": 1,
    "temporal/echo/enabled": True, "temporal/echo/frames": 8.0, "temporal/echo/decay": 0.7,
    "temporal/mosh/enabled": True, "temporal/mosh/frames": 16.0, "temporal/mosh/smear": 22.0,
    "temporal/mosh/rate": 10.0,
    "sources/driftA/rate": 0.031, "sources/driftB/rate": 0.0197, "sources/driftC/rate": 0.047,
}


def project(name, audio, live=False, sensitivity=None):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "digital-mosh.scene.json"}},
        "live": {"qualityStrategy": "effects_first", "targetFps": 60},
        "parameters": dict(POST),
        "sources": [LISTEN, ARC] + [{"kind": "lfo", "name": n, "settings": {"shape": "sine"}}
                                    for n in ("driftA", "driftB", "driftC")],
        "sonic": {},
        "routes": routes(),
        "worldMacros": macros(),
        "presets": presets(),
        "states": states(),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/digital-mosh.mp4"},
    }
    if audio:
        p["assets"]["audio"] = {"path": audio}
    if sensitivity is not None:
        p["parameters"]["macros/sensitivity"] = sensitivity
    if live:
        p["sonic"] = {"live": True}
    return p


def main():
    problems = check_moves()
    for pr in problems:
        print("CAMERA:", pr)
    (HERE / "digital-mosh.scene.json").write_text(json.dumps(scene(), indent=1) + "\n")
    projects = {
        "digital-mosh.json": project("DIGITAL MOSH", "../../assets/audio/feline-footwear.wav"),
        "digital-mosh-trench.json": project("DIGITAL MOSH / Trench", "../../assets/audio/trench.wav"),
        "digital-mosh-live.json": project("DIGITAL MOSH LIVE", None, live=True),
    }
    for n, p in projects.items():
        (HERE / n).write_text(json.dumps(p, indent=1) + "\n")
    print("wrote", HERE / "digital-mosh.scene.json", *projects,
          f"(tree at {TREE_AT}, tanguy at {TANGUY_AT}; {len(problems)} camera problems)")


if __name__ == "__main__":
    main()
