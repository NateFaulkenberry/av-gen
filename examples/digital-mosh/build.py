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
import eye as eyeform  # noqa: E402
import flight  # noqa: E402
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
STONE = lin("#b6b8af")
RUST = lin("#975736")                  # Europe After the Rain II: its rust -- the land the stain has reached                 # Dream Caused by the Flight of a Bee: the pale grey of its rocks

PAL = {
    # Dali, Dream Caused by the Flight of a Bee + The Persistence of Memory
    # Persistence's own sky band: Cap de Creus teal over olive-gold; the Bee's ochre for the pan
    "dream": dict(zenith=lin("#3f798d"), horizon=lin("#e4d7bf"), sun=light_of("#e4d7bf"), fog=lin("#e4d7bf"),
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
PAN_C = (4.0, -8.0)                        # the pan's centre (land.PAN)
TREE_XZ = land.KNOLL_XZ                    # the olive stands on its knoll, on the riverbed's bank
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
# Pass 5's landmarks: sparse, each with a beat of its own (02-design.md, "The landmarks")
FLOWER_AT = on_ground((-4.0, -22.0), -0.1)          # near the stone: the first neighbour the contagion reaches
def _beside_flight(index, side, up):
    """A point beside the flight path (pass 5b): `side` metres to its left, `up` above it, at sample `index`."""
    s = flight.samples(10.0)
    a, b = s[index], s[(index + 1) % len(s)]
    t = norm([b[0] - a[0], 0.0, b[2] - a[2]])
    left = [-t[2], 0.0, t[0]]
    return [round(a[0] + left[0] * side, 2), round(a[1] + up, 2), round(a[2] + left[2] * side, 2)]


ROCK_AT = _beside_flight(13, 18.0, 4.0)              # over the riverbed, beside the flight: it passes the castle
EYE_AT = on_ground((100.0, 30.0), 30.0)             # the floating eye, inside the east petal's loop, over the dunes


def gaze(target):
    """The eye node's rotation (Euler XYZ degrees) that turns its local +Z toward `target`."""
    d = norm([target[i] - EYE_AT[i] for i in range(3)])
    return [round(-math.degrees(math.asin(d[1])), 2), round(math.degrees(math.atan2(d[0], d[2])), 2), 0.0]


GAZE_SKY = [-58.0, -150.0, 0.0]       # the Dream: it looks up, past the camera, at the sky
GIANT_SCALE = 7.0
GIANT_AT = on_ground((10.0, -160.0), TANGUY_HOVER * GIANT_SCALE)  # the object again, at the escarpment's foot
# the macroblock that does not refresh (Recovery): on the long limb, two thirds of the way out
_b0, _d0, _s0 = OLIVE.limbs[0]
STUCK_AT = add(TREE_AT, forms.lerp(_s0[1][0], _s0[1][1], 0.6))
SUN_DIR = norm([0.9, -0.22, 0.4])          # the direction the light travels: low (~12 deg), raking from the left

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
        # the collapse's gate on the land's own blocks (0 until the Collapse), and the wind the clouds drift on
        field("collapse", kind="constant", strength=0.0),
        field("wind", kind="direction", axis=[1, 0, 0.25], strength=1.0),
    ]


CONTAGION = {
    "name": "contagion", "enabled": True, "mode": "scalar", "wrap": "clamp",
    "resolution": [128, 1, 128], "boundsMin": [-60, -40, -68], "boundsMax": [68, 40, 60],
    "injectField": "infect", "injectRate": 1.0, "velocityField": "flow", "advect": 0.0,
    "diffusion": 0.4, "diffuseIterations": 4, "dissipation": 0.0, "ceiling": 1.0,
    "simRate": 30.0, "maxSubSteps": 4, "seed": 2026, "seedAmount": 0.0, "checkpointInterval": 5.0,
}


# ============================================================================================ material programs
def infected(name, cells_per_m, fieldname, base_ops, remap_stain=False, seed=3, gate_far=0.0):
    """An infected surface (ADR-1162): every cell of a square lattice fails when the contagion passes the cell's own
    random. A failed cell is ink (dark, glossy); the cells that failed most recently -- the growth front -- glow,
    though not under the lens. The glow is the strain, turned by timbre (hueShift, routed),
    flaring with the treble and the kick's front. `base_ops` write the healthy colour into r6 (may use r5)."""
    ops = [
        op("input", 0, input="worldPosition"),
        op("quantize", 1, srcA=0, value=cells_per_m, seed=seed),
        op("field", 2, field=fieldname),
    ] + ([] if fieldname == "stain" else [   # the stuck macroblock is on a limb: the land never reads it
        op("field", 3, field="stuck"),
        op("add", 2, srcA=2, srcB=3),
    ])
    if remap_stain:
        ops.append(op("remap", 2, srcA=2, value=1, constant=[0.04, 0.9, 0.0, 1.0]))
    ops += [
        op("swizzle", 4, srcA=1, constant=[3, 3, 3, 3]),
        op("remap", 4, srcA=4, value=1, constant=[0.0, 1.0, 0.0, -1.0]),  # -cell random
        op("add", 4, srcA=2, srcB=4),                                     # d = contagion - cell random
        op("smoothstep", 7, srcA=4, constant=[0.0, 0.035, 0, 0]),
        op("remap", 7, srcA=7, value=1, constant=[0.0, 1.0, 1.0, 0.0]),
        op("smoothstep", 4, srcA=4, constant=[0.0, 0.004, 0, 0]),         # m: the cell has failed
        op("multiply", 7, srcA=7, srcB=4),                                # the growth front: the glow mask
        # under the lens the glow would be a floor of lit tiles (a game, not a fracture): it lives at a distance
        op("input", 3, input="depth"),
        op("smoothstep", 3, srcA=3, constant=[2.5, 8.0, 0, 0]),
        op("multiply", 7, srcA=7, srcB=3),
        # far away a cell is smaller than a pixel or two: it would shimmer as the camera moves (the Critic's
        # temporal_shimmer). There the cells give way to the smooth stain, and their glow fades out.
        op("input", 3, input="footprint"),
        op("smoothstep", 3, srcA=3, constant=[0.12 / cells_per_m, 0.45 / cells_per_m, 0, 0]),
        op("mixBy", 4, srcA=4, srcB=2, srcC=3),
        op("remap", 3, srcA=3, value=1, constant=[0.0, 1.0, 1.0, 0.0]),
        op("multiply", 7, srcA=7, srcB=3),
    ]
    ops += base_ops
    ops += [
        op("constant", 5, constant=INK + [1]),
        op("mixBy", 6, srcA=6, srcB=5, srcC=4),
        op("constant", 5, constant=STRAIN + [1]),
        op("constant", 3, constant=[0, 0, 0, 0]),
        op("hueShift", 5, srcA=5, srcB=3, value=0.0),                     # timbre turns the strain (routed)
        op("multiply", 7, srcA=7, srcB=5),
        op("field", 3, field="kick"),                                     # the kick's front (the treble: a route)
        op("remap", 3, srcA=3, value=0, constant=[0.0, 1.0, 0.4, 1.6]),
        op("multiply", 7, srcA=7, srcB=3),
        op("remap", 2, srcA=4, value=1, constant=[0.0, 1.0, 0.92, 0.16]),  # ink is glossy: it reflects the sky
    ]
    prog = {"name": name, "ops": ops, "baseColor": 6, "metallic": -1, "roughness": 2, "emission": 7,
            "emissionIntensity": 0.0, "opacity": -1}
    if gate_far > 0:
        prog["gate"] = {"near": 0.0, "far": gate_far}
    return prog


def land_colour_ops(world_pos_reg=0, contagion=False):
    """The land's healthy colour into r6: a ramp over the slope (flat pan -> slope -> cliff) from the stage's painting,
    mottled at the scale of dunes. With `contagion` (the near land, inside infected(), where r2 still holds the stain):
    the colour itself is reached first -- the painting's earth turns to Ernst's rust a few metres ahead of the ink."""
    p = PAL["dream"]["land"]
    # The sand's surface (pass 5): wind ripples -- noise stretched along the crests, faded with the pixel footprint so
    # it can never shimmer (microDetail) -- and the dunes' own form: convex crests bleached, hollows darker.
    texture = ([
        op("constant", 3, constant=[1.0, 1.0, 0.15, 0.0]),
        op("multiply", 5, srcA=0, srcB=3),
        op("microDetail", 5, srcA=5, value=1.7, seed=11),
        op("remap", 5, srcA=5, value=1, constant=[0.3, 0.7, 0.88, 1.08]),
        op("multiply", 6, srcA=6, srcB=5),
    ] if contagion else []) + [
        op("input", 5, input="convexity"),
        op("remap", 5, srcA=5, value=1, constant=[0.0, 1.0, 0.86, 1.1]),
        op("multiply", 6, srcA=6, srcB=5),
    ]
    reach = [
        op("smoothstep", 5, srcA=2, constant=[0.0, 0.3, 0, 0]),
        op("constant", 3, constant=RUST + [1]),
        op("mixBy", 6, srcA=6, srcB=3, srcC=5),
    ] if contagion else []
    return [
        op("input", 6, input="uv"),
        op("swizzle", 6, srcA=6, constant=[1, 1, 1, 1]),                  # slope
        op("remap", 6, srcA=6, value=1, constant=[0.0, 0.42, 0.0, 1.0]),
        op("ramp", 6, srcA=6, constant=p[0] + [1], constant2=p[1] + [1], constant3=p[2] + [1]),
        op("noise", 5, srcA=world_pos_reg, value=0.035, seed=7),
        op("remap", 5, srcA=5, value=1, constant=[0.25, 0.75, 0.84, 1.12]),
        op("multiply", 6, srcA=6, srcB=5),
    ] + texture + reach


def mottle_remap_op():
    """The 1-based index of the ground's mottle remap (the one land_colour_ops writes after its noise)."""
    for k, o in enumerate(ground_program()["ops"], start=1):
        if o["kind"] == "remap" and o.get("constant") == [0.25, 0.75, 0.84, 1.12]:
            return k
    raise KeyError("mottle remap")


def ground_program():
    # The near land: the stage's painting by slope, mottled, and the contagion's ink and front on it.
    # the mottle is read at the cell's centre (r1): invisible at 45 cm cells, a world of pixels when the Pixels
    # stratum grows the cells and the mottle's frequency (the healthy land quantised too, not only the failed cells)
    return infected("ground", 2.2, "stain", land_colour_ops(world_pos_reg=1, contagion=True), remap_stain=True, seed=3)




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


TX, TY, TZ = TREE_AT                      # the olive on its knoll
SX, SY, SZ = TANGUY_AT                    # the Tanguy object over the pan

# Pass 5: the camera flies (flight.py: one closed path of three petals, each returning over the pan). Its speed is
# integrated from the stage's own pace plus the music's energy, so it never stops between moves; a stage's variants,
# moved between on its phrases or bars as before, change how it flies -- its altitude over the path (`lift`), how far
# ahead it looks (`look`), its lens (`fov`), its lean into turns (`bank`), an extra roll (`roll`) -- and, in the
# Nightmare only, `jump`: the path itself is cut forward or back, so the camera swoops to somewhere else.
def fly(lift=0.0, look=32.0, fov=40.0, bank=8.0, roll=0.0, jump=0.0):
    return dict(lift=lift, look=look, fov=fov, bank=bank, roll=roll, jump=jump)


FLY = {
    # slow, floating: a long low glide, then up into the air, then down to the dune crests
    "Dream": [fly(0.0, 36, 40, 8), fly(9.0, 44, 44, 7), fly(-2.5, 30, 38, 9)],
    "Uncanny": [fly(1.0, 34, 38, 10), fly(14.0, 46, 46, 8), fly(-2.0, 28, 36, 12)],
    "Infection": [fly(0.0, 30, 42, 10), fly(3.0, 32, 44, 9), fly(-2.0, 26, 40, 12)],
    "Corruption": [fly(-1.5, 26, 46, 14), fly(4.0, 30, 50, 15), fly(9.0, 36, 52, 13, 4.0)],  # (unused: circles)
    # disorienting: wide, leaning hard, the path cut forward and back on the bar
    "Nightmare": [fly(0.0, 22, 58, 20, 7.0), fly(10.0, 26, 60, 22, -6.0, 0.04), fly(-2.0, 18, 62, 24, 11.0, -0.025)],
    "Respite": [fly(10.0, 48, 38, 6)],
    "Recovery": [fly(0.0, 36, 40, 8)],   # (unused: the Recovery holds on the eye still watching the stone)
}
# metres per second along the path, as the macro `flight` (x 20): the energy adds up to 4 m/s on top
FLIGHT_SPEED = {"Dream": 6.0, "Uncanny": 7.0, "Infection": 8.0, "Corruption": 10.0, "Nightmare": 15.0,
                "Respite": 4.0, "Recovery": 5.0, "Collapse": 2.0, "Decay": 1.0, "Pixels": 0.5, "Light": 0.5}
FLIGHT_LENGTH = flight.length()
FLIGHT_START = 0.3    # pass 5b: the flight opens on the east petal, where the eye floats over the dunes

# The Collapse stalls the flight: the camera holds over the pan while the world decomposes (camera mode 1).
VANTAGES = {
    # the untrustworthy keyframe: the dream as it was -- except the eye, which still watches the stone
    "Recovery": [([TANGUY_AT[0] - 16.0, land.height(TANGUY_AT[0] - 16.0, TANGUY_AT[2] - 12.0) + 4.0,
                   TANGUY_AT[2] - 12.0], [EYE_AT[0] - 18.0, EYE_AT[1] - 6.0, EYE_AT[2] - 4.0], 42, 0)],
    "Collapse": [(eye(22, 24.0, 30), [2, 0.0, -10], 50, 0)],
    "Decay": [(eye(36, 32.0, 40), [2, 0.0, -10], 52, 0)],
    "Pixels": [(eye(18, 44.0, 52), [0, 0.0, -10], 54, 0)],
    "Light": [(eye(18, 44.0, 52), [0, 0.0, -10], 54, 0)],
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


def land_blocks():
    """The land's own blocks (pass 5, the Collapse in the world): cubes 2.3 m on a 2.4 m lattice over the pan and its
    banks, sunk to the land's surface, scale 0 until the Collapse opens them where the stain is."""
    pts = []
    xs = [PAN_C[0] - 38.4 + 2.4 * i for i in range(33)]
    zs = [PAN_C[1] - 38.4 + 2.4 * j for j in range(33)]
    hs = land.heights([(x, z) for z in zs for x in xs])
    for k, (z, x) in enumerate([(z, x) for z in zs for x in xs]):
        pts.append([round(x, 3), round(hs[k] - 1.05, 3), round(z, 3), 0, 0, 0, 1, 2.3, 2.3, 2.3])
    return {"name": "landBlocks", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "box", "size": [1.0, 1.0, 1.0], "subdivisions": 1},
        "variation": {"seed": 5},
        "distribution": {"kind": "points", "points": pts},
        "effectors": [
            {"field": "collapse", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            {"field": "stain", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            {"field": "lift", "op": "positionOffset", "blend": "add", "strength": 0.0},
            {"field": "tumble", "op": "positionOffset", "blend": "add", "strength": 0.0},
            {"field": "tumble", "op": "rotation", "blend": "add", "strength": 0.0},
        ],
        "lod": {"cull": True, "count": 1},
        "material": {"baseColor": RUST, "roughness": 0.85, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                     "emissiveIntensity": 1.0, "program": "landBlocks"}}}


# Magritte's clouds (pass 5): sculpted, solid-looking cumulus far out over the land, lit by the same raking sun.
# Magritte's clouds, pass 5b: few and far -- four long banks 3-4 km out, low over the horizon, each built of many
# overlapping, flattened masses (no single puff reads), so the haze takes them into the sky's own colour.
CLOUDS = [(-3400, 300, -3600, 1.9), (3800, 260, -2600, 1.7)]


def clouds():
    import random
    rnd = random.Random(1953)
    pts = []
    for cx, cy, cz, k in CLOUDS:
        ang = math.atan2(cz, cx) + math.pi / 2           # a bank lies across the line of sight from the stage
        ux, uz = math.cos(ang), math.sin(ang)
        for _ in range(90):
            u = rnd.random()
            r = (70 - 40 * u) * k * rnd.uniform(0.85, 1.15)
            along = rnd.gauss(0, 330 * k * (1.1 - 0.5 * u))
            across = rnd.gauss(0, 50 * k)
            oy = u * u * 140 * k
            pts.append([cx + ux * along - uz * across, cy + oy, cz + uz * along + ux * across, 0, 0, 0, 1,
                        r * 1.6, r * 0.62, r * 1.3])
    return {"name": "clouds", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "sphere", "radius": 1.0, "segments": 24},
        "distribution": {"kind": "points", "points": [[round(v, 2) for v in p] for p in pts]},
        "effectors": [{"field": "wind", "op": "positionOffset", "blend": "add", "strength": 0.0}],
        "castsShadow": False, "lod": {"cull": True, "count": 1},
        "material": {"baseColor": lin("#ece6dc"), "roughness": 1.0, "metallic": 0.0}}}


def scene():
    d = PAL["dream"]
    sky = {"enabled": True, "background": True, "useKeyLight": False, "sunDirection": mul(SUN_DIR, -1.0),
           "zenithColor": d["zenith"], "horizonColor": d["horizon"], "groundColor": d["groundSky"],
           "sunColor": d["sun"], "haze": 0.16, "sunIntensity": 1.0, "sunSize": 0.02, "sunGlow": 0.3,
           "intensity": 1.0}
    nodes = fields() + [
        {"name": "land", "kind": "terrain", "position": [0, 0, 0], "world": land.world(False),
         "terrain": {"chunkSize": 30.0, "resolution": 30, "lodLevels": 4, "lodDistance": 180.0,
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
    # the landmarks: the flower carries the contagion (the stone's program), the rock floats, the giant is the object
    # again at a size nothing nearby lets you judge
    nodes.append(sdf_node("flower", FLOWER_AT, forms.flower_tree(), "skin", [-1.9, -0.4, -1.8], [2.3, 7.8, 2.0],
                          STONE, 0.3, steps=96))
    nodes.append(sdf_node("rock", HIDDEN, forms.rock_tree(), "", [-4.6, -4.0, -4.0], [4.6, 5.8, 4.0],
                          lin("#8a7f72"), 0.9, steps=96))
    giant = forms.tanguy_tree()
    giant["root"] = giant["root"]["children"][0]
    g = sdf_node("giant", GIANT_AT, giant, "", [-1.6, -1.75, -1.4], [1.6, 3.1, 1.4], STONE, 0.3, steps=96)
    g["scale"] = [GIANT_SCALE] * 3
    nodes.append(g)
    elo, ehi = eyeform.bounds()
    en = sdf_node("eye", EYE_AT, eyeform.tree(), "eye", elo, ehi, eyeform.CREAM, 0.1, steps=96)
    en["sdf"]["material"]["emissiveIntensity"] = 1.0
    en["rotation"] = GAZE_SKY
    nodes.append(en)
    nodes.append(land_blocks())
    nodes.append(clouds())
    nodes.append(flight.spline_node())
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
        {"name": "spores", "kind": "particles", "particles": {
            # the contagion's second carrier, after its light: the strain's own colour, drifting out from the stone
            "capacity": 12000, "seed": 9, "shape": "sphere", "position": add(TANGUY_AT, [0, 0.4, 0]),
            "extent": [1.2, 1.2, 1.2], "direction": [0, 1, 0], "spread": 1.0, "spawnRate": 0, "speedMin": 0.2,
            "speedMax": 1.1, "gravity": [0, 0.05, 0], "drag": 0.15, "turbulence": 0.6, "turbulenceScale": 0.08,
            "turbulenceSpeed": 0.2, "sizeStart": 0.05, "sizeEnd": 0.015, "sizeVariance": 0.5,
            "lifetimeMin": 7.0, "lifetimeMax": 14.0, "colorStart": STRAIN + [0.95], "colorEnd": INK + [0.0],
            "emissive": 4.0, "blend": "additive", "softness": 0.5}},
    ]
    f0 = FLY["Dream"][0]
    return {
        "format": "avgen-scene", "version": 1, "name": "DIGITAL MOSH",
        "_note": "Generated by build.py; edit that, not this file.",
        "camera": {"mode": 2, "spline": "flight", "fov": f0["fov"]},
        "environment": {
            "intensity": 0.12, "background": d["horizon"], "fogColor": d["fog"],
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
             "color": d["sun"], "intensity": 9.0, "castsShadow": True, "shadowStrength": 1.0, "softness": 0.35,
             "shadowBias": 0.006},  # a grazing sun on 1.25 m quads: without it the dunes' lee slopes are acne
            # the first fracture's light: the strain, at the Tanguy object, off until the infection
            {"name": "fracture", "id": "fracture", "type": "point", "position": add(TANGUY_AT, [0, 0.5, 0]),
             "color": STRAIN, "intensity": 0.0, "range": 30.0, "radius": 0.6, "castsShadow": False,
             "volumetric": 1.0},
        ],
        "grids": [CONTAGION],
        "materialPrograms": [ground_program(), ground_far_program(), bark_program(), stone_program(),
                             blocks_program("treeBlocks", BARK[0], BARK[1]),
                             blocks_program("tanguyBlocks", STONE, lin("#9f9985")),
                             blocks_program("landBlocks", RUST, INK), eyeform.program(STRAIN)],
        "nodes": nodes,
    }


# ============================================================================================ the stages
# One preset per stage (and per vantage). Continuous expression rides on top as routes gated by macros the stage sets
# (restraint early, brief §16). Each stage breaks ONE more law than the last (research S2).
BASE = dict(pal="dream", landPal="dream", inject=0.0, advect=0.0, dissip=0.6, climb=0.0, eat=0.0, eatStone=0.0,
            melt=[0.0, 0.0, 0.0], lift=[0.0, 0.0, 0.0], bark=0.016, barkSpeed=0.0,
            rise=0.0, tumble=0.0, spin=0.0, kick=0.0, sunYaw=0.0, fracture=0.0, double=False, motes=40.0, spores=0.0, fracRange=10.0, eyeGaze="sky", moon=0.0, hole=0.0, skyI=0.6, drift=0.5, rock=False, collapseGate=0.0, landRise=0.0, landTumble=0.0, cells=1.0, mottle=0.035, mottleRange=[0.25, 0.75, 0.84, 1.12],
            echo=0.0, mosh=0.0, moshBlock=32.0, glitch=0.0, pixel=0.0, poster=0.0, sort=0.0, split=0.0,
            exposure=0.0, bloom=0.08, volDen=0.0006, sun=11.0,
            gMicro=0.15, gRhythm=0.0, gMosh=0.0, gGlitch=0.0, gMelt=0.0, gLift=0.0,
            glowPlain=0.0, glowBark=0.0, glowBlocks=0.0, keyframe=0.0, heal=0.0)


def stage(**kw):
    v = dict(BASE)
    v.update(kw)
    return v


COLLAPSE_COMMON = dict(spores=400.0, fracRange=60.0, rock=True, drift=1.0, collapseGate=1.0, eyeGaze="stone",
                       moon=1.0, hole=1.0, mosh=0.0, gMosh=0.0, inject=5.0, advect=4.0, climb=12.0, eat=1.0, eatStone=1.0, fracture=5000.0, double=True,
                       glowPlain=8.0, glowBark=6.0, gMicro=1.0, gRhythm=1.0, gMelt=1.0, gLift=1.0)
STAGES = {
    # Beautiful, quiet, hypnotic. Only the light, the dust and the camera move.
    "Dream": stage(volDen=0.00038),
    # Relation errors only (Magritte, S12; de Chirico's light, D1/D2): the shadows swing against the sky's sun, the
    # Tanguy object appears twice, the long limb lifts a little against gravity, the sky turns Chirico's green.
    "Uncanny": stage(pal="uncanny", eyeGaze="stone", drift=0.0, rock=True, sunYaw=26.0, double=True, melt=[-0.05, 0.0, 0.0], echo=0.08, gMicro=0.3,
                     gRhythm=0.1, volDen=0.0007),
    # The first block goes bad: the strain at the Tanguy object. It spreads in order (brief §7): its light, then its
    # spores, then the haze it lights, then the ground (rust a few metres ahead of the ink), then the tree. Everything
    # it has not reached keeps the painting.
    "Infection": stage(pal="uncanny", eyeGaze="stone", rock=True, inject=1.4, advect=0.9, dissip=0.02, climb=1.6, eat=0.18, eatStone=0.35,
                       melt=[0.06, 0.0, 0.0], fracture=700.0, double=True, sunYaw=8.0, glowPlain=2.5, glowBark=1.8,
                       glowBlocks=3.0, echo=0.1, gMicro=0.5, gRhythm=0.35, gMosh=0.15, gMelt=0.2, motes=70.0, spores=30.0, fracRange=12.0,
                       exposure=-0.1, volDen=0.0008),
    # Ernst's rot: the tree eaten from the root up, its bark flowing like wax, limbs drooping, the first blocks lifting.
    "Corruption": stage(pal="uncanny", eyeGaze="stone", moon=1.0, rock=True, drift=0.9, inject=2.4, advect=2.0, dissip=0.0, climb=4.6, eat=0.32, eatStone=0.5,
                        melt=[0.16, 0.08, -0.06], bark=0.05, barkSpeed=0.5, rise=0.35, tumble=0.25, spin=0.4,
                        kick=0.25, fracture=1900.0, double=True, glowPlain=4.0, glowBark=3.0, glowBlocks=5.0,
                        echo=0.18, mosh=0.0, exposure=-0.4, gMicro=0.75, gRhythm=0.65, gMosh=0.12, gMelt=0.5,
                        gLift=0.3, motes=160.0, spores=140.0, fracRange=28.0, volDen=0.0011),
    # The Elephants' blood sky over Tanguy's night land: the tree is blocks, its limbs float free.
    "Nightmare": stage(pal="nightmare", landPal="nightmare", eyeGaze="stone", moon=1.0, hole=1.0, rock=True, drift=2.0, inject=3.5, advect=3.4, dissip=0.0, climb=9.5, eat=0.55, eatStone=0.7,
                       melt=[0.26, 0.18, -0.14], lift=[1.3, 0.7, 1.0], bark=0.09, barkSpeed=1.0, rise=1.8,
                       tumble=1.1, spin=1.5, kick=0.6, sunYaw=-34.0, fracture=3200.0, double=True, glowPlain=3.0,
                       glowBark=4.5, glowBlocks=7.0, echo=0.3, mosh=0.0, moshBlock=24.0, exposure=-0.7,
                       gMicro=1.0, gRhythm=1.0, gMosh=0.0, gGlitch=0.6, gMelt=1.0, gLift=1.0, motes=420.0, spores=320.0, fracRange=50.0,
                       volDen=0.0014),
    # The collapse, through the representations the renderer built the world from (G10, brief §15), on the bar grid:
    # geometry...
    "Collapse": stage(pal="nightmare", landPal="nightmare", landRise=0.6, landTumble=0.3, melt=[0.4, 0.3, -0.3], lift=[3.0, 2.0, 2.5], bark=0.12, barkSpeed=1.5,
                      rise=5.0, tumble=2.5, spin=3.0, kick=1.2, glowBlocks=9.0, echo=0.35, moshBlock=48.0,
                      exposure=-0.6, gGlitch=0.8, motes=900.0, volDen=0.0016, **COLLAPSE_COMMON),
    #   ... fragments become particles and temporal fragments
    "Decay": stage(pal="nightmare", landPal="nightmare", landRise=3.5, landTumble=1.6, melt=[0.4, 0.3, -0.3], lift=[5.0, 3.5, 4.0], bark=0.12, barkSpeed=1.5, rise=9.0,
                   tumble=4.0, spin=5.0, kick=1.6, glowBlocks=10.0, echo=0.5, moshBlock=64.0, cells=0.4, mottle=0.25, mottleRange=[0.2, 0.8, 0.7, 1.25],
                   exposure=-0.5, gGlitch=1.0, motes=2600.0, volDen=0.0018, **COLLAPSE_COMMON),
    #   ... pixels, then colour
    "Pixels": stage(pal="nightmare", landPal="nightmare", landRise=7.0, landTumble=3.0, melt=[0.4, 0.3, -0.3], lift=[6.0, 4.5, 5.0], bark=0.12, barkSpeed=1.5,
                    rise=12.0, tumble=5.0, spin=6.0, kick=1.6, glowBlocks=10.0, echo=0.4, moshBlock=96.0,
                    cells=0.12, mottle=0.9, mottleRange=[0.2, 0.8, 0.45, 1.45], pixel=0.0, poster=0.0, sort=0.15, exposure=-0.3, gGlitch=1.0, motes=2600.0, volDen=0.0018,
                    **COLLAPSE_COMMON),
    #   ... and light: Tanguy's white.
    "Light": stage(pal="light", landPal="light", skyI=1.0, landRise=9.0, landTumble=4.0, lift=[6.0, 4.5, 5.0], rise=12.0, tumble=5.0, spin=6.0, glowBlocks=10.0, echo=0.6,
                   pixel=0.0, poster=0.0, exposure=2.6, bloom=1.0, gGlitch=1.0, motes=2600.0,
                   volDen=0.002, **COLLAPSE_COMMON),
    # A breakdown's respite: Magritte's Empire of Light -- a day sky over a land still in night; the stain stays.
    "Respite": stage(pal="respite", landPal="respite", skyI=1.0, drift=0.2, rock=True, sun=1.2, dissip=0.25, double=True, keyframe=1.0, heal=0.45, glowPlain=1.0, gMicro=0.3,
                     echo=0.05, volDen=0.0007),
    # The keyframe: the dream exactly as it was (P8). One macroblock did not refresh.
    "Recovery": stage(eyeGaze="stone", dissip=9.0, keyframe=1.0, heal=1.0, volDen=0.00038),
}
LADDER = ["Dream", "Uncanny", "Infection", "Corruption", "Nightmare", "Collapse"]
DEPTH_AT = {"Uncanny": 0.08, "Infection": 0.28, "Corruption": 0.48, "Nightmare": 0.66, "Collapse": 0.86}
ENTRY = {"Dream": (8, "smooth"), "Uncanny": (12, "smooth"), "Infection": (6, "smooth"), "Corruption": (3, "easeIn"),
         "Nightmare": (2, "easeIn"), "Collapse": (4, "easeIn")}


def variant_names(st):
    n = len(FLY[st]) if st in FLY else len(VANTAGES[st])
    return [st] + [f"{st} {k + 1}" for k in range(1, n)]


def sun_azimuth(yaw_deg):
    """The key's azimuth (Composition::lightAngles) turned by yaw: the shadows swing; the sky's sun stays."""
    return math.degrees(math.atan2(-SUN_DIR[0], -SUN_DIR[2])) + yaw_deg


def stage_values(name, v):
    pal = PAL[v["pal"]]
    hue_op = program_op(ground_program, "hueShift")
    vals = {
        "field/infect/strength": [v["inject"]], "grid/contagion/advect": [v["advect"]],
        "grid/contagion/dissipation": [v["dissip"]], "field/climb/position": [0.0, TREE_AT[1] + v["climb"], 0.0],
        "sdf/tanguy/node/eaten/amount": [v["eatStone"]],
        "lights/fracture/intensity": [v["fracture"]], "lights/fracture/range": [v["fracRange"]], "lights/sun/azimuth": [sun_azimuth(v["sunYaw"])],
        "lights/sun/color": pal["sun"], "lights/sun/intensity": [v["sun"]],
        "material/ground/emissionIntensity": [v["glowPlain"]],
        "material/bark/emissionIntensity": [v["glowBark"]],
        "material/skin/emissionIntensity": [v["glowBark"]],
        "material/treeBlocks/emissionIntensity": [v["glowBlocks"]],
        "material/tanguyBlocks/emissionIntensity": [v["glowBlocks"]],
        f"material/ground/op/{hue_op}/hueShift/value": [0.0],
        "nodes/double/position": DOUBLE_AT if v["double"] else HIDDEN,
        f"material/ground/op/{program_op(ground_program, 'quantize')}/quantize/value": [2.2 * v["cells"]],
        f"material/ground/op/{program_op(ground_program, 'noise')}/noise/value": [v["mottle"]],
        f"material/ground/op/{mottle_remap_op()}/remap/constant": v["mottleRange"],
        "nodes/eye/rotation": GAZE_SKY if v["eyeGaze"] == "sky" else gaze(TANGUY_AT),
        f"material/eye/op/{eyeform.program_constant_index(eyeform.program(STRAIN), 5)}/constant/constant": [v["moon"]] * 4,
        f"material/eye/op/{eyeform.program_constant_index(eyeform.program(STRAIN), 7)}/constant/constant": [v["hole"]] * 4,
        "material/eye/emissionIntensity": [6.0 * v["hole"]],
        "macros/drift": [v["drift"]], "nodes/rock/position": ROCK_AT if v["rock"] else HIDDEN,
        "field/collapse/strength": [v["collapseGate"]],
        "procedural/landBlocks/effector/3/strength": [v["landRise"]],
        "procedural/landBlocks/effector/4/strength": [v["landTumble"]],
        "procedural/landBlocks/effector/5/strength": [v["landTumble"] * 0.6],
        "particles/motes/spawnRate": [v["motes"]], "particles/spores/spawnRate": [v["spores"]],
        "temporal/echo/strength": [v["echo"]], "temporal/mosh/amount": [v["mosh"]],
        "temporal/mosh/block": [v["moshBlock"]], "post/glitch/amount": [v["glitch"]],
        "post/display/pixelate": [v["pixel"]], "post/display/posterize": [v["poster"]],
        "post/sort/amount": [v["sort"]], "post/split/amount": [v["split"]],
        "camera/exposure/compensation": [v["exposure"]], "post/bloom/intensity": [v["bloom"]],
        "scene/volumeDensity": [v["volDen"]], "scene/fogColor": pal["fog"],
        "env/sky/intensity": [v["skyI"]],  # the sky's fill: lower, so shadowed faces read dark against lit ones
        "env/sky/zenithColor": pal["zenith"], "env/sky/horizonColor": pal["horizon"],
        "env/sky/groundColor": pal["groundSky"], "env/sky/sunColor": pal["sun"],
        "macros/keyframe": [v["keyframe"]], "macros/heal": [v["heal"]],
    }
    for g in ("gMicro", "gRhythm", "gMosh", "gGlitch", "gMelt", "gLift"):
        vals[f"macros/{g}"] = [v[g]]
    # the land keeps its painting until the contagion reaches it (both lands, so the seam never shows): only the
    # systemic stages change it everywhere
    landp = PAL[v["landPal"]]["land"]
    for prog_fn, prog in ((ground_program, "ground"), (ground_far_program, "groundFar")):
        i = program_op(prog_fn, "ramp")
        vals[f"material/{prog}/op/{i}/ramp/constant"] = landp[0] + [1]
        vals[f"material/{prog}/op/{i}/ramp/constant2"] = landp[1] + [1]
        vals[f"material/{prog}/op/{i}/ramp/constant3"] = landp[2] + [1]
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
            vals = dict(base)
            vals["macros/flight"] = [FLIGHT_SPEED[name] / 20.0]
            vals["macros/circle"] = [{"Corruption": 0.5, "Infection": 1.0}.get(name, 0.0)]
            if name in ("Corruption", "Infection"):
                # pass 5b: the Infection circles wide round the stone, the Corruption close round the spreading
                # stain, both looking at it (LFO routes: seek-exact). Radius = 92 m x macros/circle.
                h, fov = ([(12.0, 44), (21.0, 48), (8.0, 50)] if name == "Corruption"
                          else [(16.0, 38), (28.0, 40), (10.0, 36)])[k]
                vals.update({"camera/mode": [1], "camera/position": [PAN_C[0], land.height(*PAN_C) + h, PAN_C[1]],
                             "camera/target": [TANGUY_AT[0], TANGUY_AT[1] - 1.0, TANGUY_AT[2]], "camera/fov": [fov],
                             "camera/roll": [0.0]})
            elif name in FLY and name not in ("Corruption", "Infection", "Recovery"):
                f = FLY[name][k]
                vals.update({"camera/mode": [2], "camera/splineOffset": [0.0, f["lift"], 0.0],
                             "camera/lookAhead": [f["look"]], "camera/fov": [f["fov"]],
                             "camera/splineBank": [f["bank"]], "camera/roll": [f["roll"]],
                             "camera/splineT": [FLIGHT_START + f["jump"]]})
            else:
                e, t, fov, roll = VANTAGES[name][k]
                vals.update({"camera/mode": [1], "camera/position": e, "camera/target": t, "camera/fov": [fov],
                             "camera/roll": [roll]})
            out.append({"name": vname.lower(), "values": vals})
    return out


PADS = {"Dream": 36, "Uncanny": 37, "Infection": 38, "Corruption": 39, "Nightmare": 40, "Collapse": 41,
        "Respite": 42, "Recovery": 43}


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
            if vi == 0 and name in PADS:
                trig.append({"kind": "signal", "signal": f"control.pad{name}", "threshold": 0.3})
            transition = {"seconds": seconds, "easing": easing}
            if vi > 0:
                transition["quantize"] = "beat"   # a camera move starts on the beat
            S.append({"name": vname, "preset": vname.lower(), "transition": transition, "triggers": trig})
    S.append({"name": "Decay", "preset": "decay", "transition": {"seconds": 3, "easing": "easeIn", "quantize": "bar"},
              "triggers": [after(6.0, "Collapse")]})
    S.append({"name": "Pixels", "preset": "pixels",
              "transition": {"seconds": 2, "easing": "easeIn", "quantize": "bar"}, "triggers": [after(5.0, "Decay")]})
    S.append({"name": "Light", "preset": "light", "transition": {"seconds": 3, "easing": "easeIn", "quantize": "bar"},
              "triggers": [after(4.0, "Pixels")]})
    S.append({"name": "Respite", "preset": "respite", "transition": {"seconds": 1.5, "easing": "smooth"},
              "triggers": [held("visual.lift", 0.28, f, falling=True)
                           for st in ("Infection", "Corruption", "Nightmare") for f in variant_names(st)]
              + [{"kind": "signal", "signal": "control.padRespite", "threshold": 0.3}]})
    # the keyframe: an instant cut from white to the calm dream (brief §15)
    S.append({"name": "Recovery", "preset": "recovery", "transition": {"seconds": 0.0, "easing": "linear"},
              "triggers": [after(2.0, "Light"), {"kind": "signal", "signal": "control.padRecovery", "threshold": 0.3}]})
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
         for n in ("energy", "baseline", "dose", "keyframe", "heal", "flight", "drift", "circle")]
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
    # ---- the flight (pass 5): the path position integrates the stage's pace and the music's energy -- never a stop
    integ = {"integrate": True}
    r.append({"source": "macro.flight", "target": "camera/splineT", "op": "add", "amount": 20.0 / FLIGHT_LENGTH,
              "chain": dict(integ)})
    r.append({"source": "macro.energy", "target": "camera/splineT", "op": "add", "amount": 4.0 / FLIGHT_LENGTH,
              "chain": dict(integ)})
    # the clouds drift on the wind (metres): a stage that stops the drift stops a cloud
    r.append({"source": "macro.drift", "target": "procedural/clouds/effector/1/strength", "op": "add",
              "amount": 6.0, "chain": dict(integ)})
    # the flying camera floats a little off its path, more as the music grows
    for src, comp, amt in (("driftA", 0, 2.4), ("driftC", 1, 1.2)):
        r.append({"source": f"lfo.{src}.bipolar", "target": "camera/splineOffset", "op": "add", "amount": amt,
                  "component": comp, "depthSource": "macro.energy", "depthMin": 0.5, "depthMax": 1.0})
    # ---- the Corruption's circle round the stain (pass 5b): sine and cosine of time, so a seek lands where play does
    for src, comp in (("circleA", 0), ("circleB", 2)):
        r.append({"source": f"lfo.{src}.bipolar", "target": "camera/position", "op": "add", "amount": 92.0,
                  "component": comp, "depthSource": "macro.circle", "depthMin": 0.0, "depthMax": 1.0})
    # ---- the held camera floats between its moves: slow incommensurate drifts, deeper as the music grows
    for src, comp, amt in (("driftA", 0, 1.6), ("driftB", 2, 1.6), ("driftC", 1, 0.3)):
        r.append({"source": f"lfo.{src}.bipolar", "target": "camera/position", "op": "add", "amount": amt,
                  "component": comp, "depthSource": "macro.energy", "depthMin": 0.6, "depthMax": 1.0})
    # the Uncanny's "a distant object briefly changes scale" (brief §5): the double swells for a phrase on the
    # music's own swells, with nothing else moving -- gated by the micro gate, so the Dream never does it
    r.append(gated("visual.lift", "nodes/double/scale", 0.9, "gMicro", {"attackMs": 4000, "decayMs": 6000,
                                                                          "offset": -0.55, "clampEnabled": True,
                                                                          "clampMin": 0.0, "clampMax": 1.0}))
    r.append({"source": "lfo.driftB.bipolar", "target": "camera/target", "op": "add", "amount": 0.6, "component": 0})
    r.append({"source": "lfo.driftA.bipolar", "target": "camera/target", "op": "add", "amount": 0.25, "component": 1})
    return r


POST = {
    "post/bloom/enabled": True, "post/bloom/threshold": 1.4, "post/bloom/emissionWeight": 0.7,
    "post/tonemap/chroma-retention": 0.55, "post/output/vignette": 0.24,
    "post/grade/contrast": 1.1, "post/grade/saturation": 1.08,
    "camera/exposure/mode": 0, "camera/mode": 2, "camera/lookAhead": 36.0, "camera/splineBank": 8.0,
    "temporal/echo/enabled": True, "temporal/echo/frames": 8.0, "temporal/echo/decay": 0.7,
    "temporal/mosh/enabled": True, "temporal/mosh/frames": 16.0, "temporal/mosh/smear": 22.0,
    "temporal/mosh/rate": 10.0,
    "sources/driftA/rate": 0.031, "sources/driftB/rate": 0.0197, "sources/driftC/rate": 0.047,
    "sources/circleA/rate": 0.021, "sources/circleB/rate": 0.021, "sources/circleB/phase": 0.25,
}


def project(name, audio, live=False, sensitivity=None):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "digital-mosh.scene.json"}},
        "live": {"qualityStrategy": "effects_first", "targetFps": 60},
        "parameters": dict(POST),
        "sources": [LISTEN, ARC] + [{"kind": "lfo", "name": n, "settings": {"shape": "sine"}}
                                    for n in ("driftA", "driftB", "driftC", "circleA", "circleB")],
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
        # the instrument: SENSITIVITY trims the arc to the input (CC 1); pads force
        # a stage (the arc carries on from there) -- General MIDI drum notes 36..43
        bindings = [{"source": "*", "channel": -1, "kind": "cc", "number": 1, "parameter": "macros/sensitivity",
                     "component": 0, "min": 0.0, "max": 1.0}]
        bindings += [{"source": "*", "channel": -1, "kind": "noteEvent", "number": n, "signal": f"pad{st}"}
                     for st, n in PADS.items()]
        p["control"] = {"midi": {"enabled": True, "filter": "*", "bindings": bindings}}
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
