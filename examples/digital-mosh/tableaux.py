"""DIGITAL MOSH, pass 6: the world as tableaux standing in one mirror (03-correction-plan.md).

There is no ground. The sky reflects itself below the horizon (ADR-1167), so the world is a still mirror to every
horizon, and every object carries its own reflection, built: an SDF `mirror` across y = 0, or a different object
where the reflection lies (the eye's reflection is a moon). A second, upward key -- the sun off the mirror -- lights
the undersides, as a mirror does.

Imported by build.py. Coordinates are metres; the eye is at the origin; the mirror is y = 0.
"""
from __future__ import annotations

import math

import forms
from forms import add, sdf


def lin(hexs, gain=1.0):
    def c(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return [round(c(int(hexs[i:i + 2], 16)) * gain, 4) for i in (1, 3, 5)]


def norm(v):
    m = math.sqrt(sum(x * x for x in v))
    return [x / m for x in v]


def mirrored(tree_root):
    """The object and its reflection in the mirror: the node sits on the mirror (y = 0)."""
    return sdf("mirror", [tree_root], size=[0, 1, 0])


# ============================================================================================ the palette (§7)
# Pale Dream, from the paintings (05-palettes.md): cream, ochre, dusty rose, pale turquoise, lavender.
CREAM = lin("#e9dfca")
OCHRE = lin("#c1ae5e")
ROSE = lin("#c99a92")
TURQUOISE = lin("#7fb0b4")
LAVENDER = lin("#9c93b0")
INK = lin("#2d0c1f")
PORCELAIN = lin("#efe9e1")
CHALK = lin("#ddd2bd")
EARTH = [lin("#574625"), lin("#8c6f36"), lin("#b1b081")]   # Persistence of Memory's earths
NIGHT = [lin("#0d1424"), lin("#25334f"), lin("#53607a")]     # Empire of Light: its night, deep to the lamp's dusk

# ============================================================================================ the layout
EYE_AT = [0.0, 0.0, 0.0]
EYE_R = 26.0
EYE_LOOK = norm([0.42, 0.78, 0.46])          # where the iris looks: up, toward the south-east approach
SLAB_AT = [-240.0, 0.0, -170.0]               # the hanging land (its node sits on the mirror; the slab is 96 m up)
SLAB_Y = 96.0
SLAB_HALF = [78.0, 9.0, 52.0]
DOOR_AT = [175.0, 0.0, -70.0]
DOOR_YAW = -38.0                              # the door faces the flight that passes through it
STAIR_AT = [70.0, 0.0, 160.0]
STAIR_YAW = -14.0
FLOWER_AT = [560.0, 0.0, 430.0]
FLOWER_SCALE = 46.0
TANGUY_FORMS = [([-46.0, 0.0, 74.0], 3.0, -0.9), ([58.0, 0.0, 42.0], 1.4, -0.3), ([-96.0, 0.0, -22.0], 6.0, -2.4)]


# ============================================================================================ the forms
def eye_tree():
    """A sphere on the mirror, half above and half below. Above it is an eye; below, its reflection, it is a moon --
    the material program tells them apart by the sign of the local y."""
    return {"root": sdf("displaceField", [sdf("sphere", radius=EYE_R)], amount=0.0, reference="rot", name="eaten")}


def slab_tree(olive):
    """The hanging land: a thick slab of the dream's earth, its underside worn, hanging over the mirror -- and its
    reflection under the mirror, the right way up. The olive grows down from the underside."""
    slab = sdf("translate", [sdf("warp", [sdf("roundedBox", size=SLAB_HALF, rounding=6.0)], amount=7.0,
                                 frequency=0.035, size=[1.0, 0.6, 1.0], seed=31)], translation=[0, SLAB_Y, 0])
    return {"root": mirrored(slab)}


def hanging(tree_root, at, scale):
    """A form hung upside down from the slab's underside at `at` (slab-local, world metres), with its reflection.
    The node carries the scale (so the tree stays within ADR-1005's 96 nodes): positions here are divided by it."""
    inv = sdf("rotate", [tree_root], rotation=[180, 0, 0])
    return {"root": mirrored(sdf("translate", [inv], translation=[v / scale for v in at]))}


def door_frame_tree():
    """Magritte's door: a frame standing in the mirror, its reflection below."""
    outer = sdf("box", size=[4.6, 9.2, 0.55])
    inner = sdf("box", size=[3.7, 8.4, 1.2])
    frame = sdf("translate", [sdf("difference", [outer, sdf("translate", [inner], translation=[0, -0.45, 0])])],
                translation=[0, 9.2, 0])
    lintel = sdf("translate", [sdf("roundedBox", size=[5.1, 0.45, 0.8], rounding=0.2)], translation=[0, 18.6, 0])
    return {"root": mirrored(sdf("union", [frame, lintel]))}


def door_panel_tree():
    """What is through the door: night. A thin panel filling the opening; its reflection below shows day."""
    return {"root": mirrored(sdf("translate", [sdf("box", size=[3.7, 8.4, 0.06])], translation=[0, 8.75, 0]))}


def stair_tree():
    """Scale collapse: six flights of eight steps, each flight half again larger than the last -- miniature where it
    leaves the mirror, enormous where it meets the sky."""
    flights = []
    x = y = 0.0
    for k in range(6):
        s = 1.5 ** k
        st = sdf("stairs", size=[0.8, 0.55, 1.6], count=8, height=0.7)
        flights.append(sdf("translate", [sdf("scale", [st], scale=s)], translation=[x, y, -0.0]))
        x += 0.8 * 8 * s
        y += 0.55 * 8 * s
    return {"root": mirrored(sdf("union", flights))}


def stair_extent():
    x = sum(0.8 * 8 * 1.5 ** k for k in range(6))
    y = sum(0.55 * 8 * 1.5 ** k for k in range(6))
    w = 1.6 * 1.5 ** 5
    return x, y, w


def flower_tree():
    t = forms.flower_tree()
    return {"root": mirrored(t["root"])}


def tanguy_mirrored(sink):
    t = forms.tanguy_tree()
    body = t["root"]["children"][0]           # no contagion on these
    return {"root": mirrored(sdf("translate", [body], translation=[0, sink, 0]))}


# ============================================================================================ the materials (§8)
def op(kind, dst, **kw):
    o = {"kind": kind, "dst": dst}
    o.update(kw)
    return o


def eye_program():
    """Above the mirror an eye of wax-stone: a wet, wax-cream white with faint dusty-rose veins, an iris of the Bee's
    turquoise warming to ochre round an ink pupil, a dark limbal ring. Below the mirror (local y < 0), its
    reflection is a moon: pale, cratered, cold."""
    look = EYE_LOOK
    return {"name": "eye", "ops": [
        op("input", 0, input="localPosition"),
        # how near the look axis: 1 at the pupil's centre
        op("gradient", 1, srcA=0, value=1.0 / EYE_R, constant=look + [0.0]),
        op("smoothstep", 2, srcA=1, constant=[0.905, 0.915, 0, 0]),             # the iris disc
        op("smoothstep", 3, srcA=1, constant=[0.972, 0.978, 0, 0]),             # the pupil
        op("remap", 4, srcA=1, value=1, constant=[0.91, 0.975, 0.0, 1.0]),     # across the iris, out to in
        op("ramp", 4, srcA=4, constant=lin("#3d5d60") + [1], constant2=TURQUOISE + [1], constant3=OCHRE + [1]),
        op("noise", 5, srcA=0, value=0.55, seed=21),                            # the iris's fibres
        op("remap", 5, srcA=5, value=1, constant=[0.3, 0.7, 0.62, 1.25]),
        op("multiply", 4, srcA=4, srcB=5),
        # the sclera: wax-cream, its veins dusty rose
        op("voronoiEdge", 5, srcA=0, value=0.16, seed=5),
        op("smoothstep", 5, srcA=5, constant=[0.0, 0.035, 0, 0]),
        op("constant", 6, constant=ROSE + [1]),
        op("constant", 7, constant=CREAM + [1]),
        op("mixBy", 6, srcA=6, srcB=7, srcC=5),
        op("mixBy", 6, srcA=6, srcB=4, srcC=2),                                 # sclera -> iris
        op("smoothstep", 5, srcA=1, constant=[0.895, 0.905, 0, 0]),             # the limbal ring
        op("remap", 7, srcA=2, value=1, constant=[0.0, 1.0, 1.0, 0.0]),
        op("multiply", 5, srcA=5, srcB=7),
        op("constant", 7, constant=lin("#1f2a2c") + [1]),
        op("mixBy", 6, srcA=6, srcB=7, srcC=5),
        op("constant", 7, constant=INK + [1]),
        op("mixBy", 6, srcA=6, srcB=7, srcC=3),                                 # the pupil
        # below the mirror: the moon
        op("swizzle", 5, srcA=0, constant=[1, 1, 1, 1]),
        op("smoothstep", 5, srcA=5, constant=[0.25, -0.25, 0, 0]),              # 1 below, 0 above
        op("voronoi", 7, srcA=0, value=0.09, seed=13),
        op("remap", 7, srcA=7, value=1, constant=[0.1, 0.9, 0.55, 0.92]),
        op("multiply", 7, srcA=7, srcB=7),
        op("constant", 2, constant=lin("#c9cdd0") + [1]),
        op("multiply", 7, srcA=7, srcB=2),
        op("mixBy", 6, srcA=6, srcB=7, srcC=5),
        # wet eye, dry moon
        op("remap", 1, srcA=5, value=1, constant=[0.0, 1.0, 0.16, 0.9]),
    ], "baseColor": 6, "metallic": -1, "roughness": 1, "emission": -1, "emissionIntensity": 0.0, "opacity": -1}


def night_program():
    """Through the door it is night: the Empire of Light's sky, deep at the top to the dusk of a lamp near the sill,
    glowing as a painted sky glows. Its reflection (y < 0) is the day the door stands in."""
    return {"name": "night", "ops": [
        op("input", 0, input="localPosition"),
        op("swizzle", 1, srcA=0, constant=[1, 1, 1, 1]),
        op("remap", 2, srcA=1, value=1, constant=[0.4, 17.0, 1.0, 0.0]),
        op("ramp", 3, srcA=2, constant=NIGHT[0] + [1], constant2=NIGHT[1] + [1], constant3=NIGHT[2] + [1]),
        op("smoothstep", 4, srcA=1, constant=[0.2, -0.2, 0, 0]),                 # 1 in the reflection
        op("remap", 5, srcA=1, value=1, constant=[-17.0, -0.4, 0.0, 1.0]),
        op("ramp", 5, srcA=5, constant=lin("#4f86a8") + [1], constant2=lin("#94c0d9") + [1],
           constant3=lin("#e1e4d9") + [1]),
        op("mixBy", 3, srcA=3, srcB=5, srcC=4),
    ], "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0, "opacity": -1}


# ============================================================================================ the nodes
def sdf_node(name, at, tree, colour, lo, hi, roughness=0.6, program="", steps=128, scale=None, rotation=None,
             cast=True):
    material = {"baseColor": colour, "roughness": roughness, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                "emissiveIntensity": 1.0}
    if program:
        material["program"] = program
    n = {"name": name, "kind": "sdf", "position": at, "sdf": {
        "tree": tree, "material": material, "renderMode": "raymarch", "boundsMin": lo, "boundsMax": hi,
        "compile": True, "maxSteps": steps, "stepScale": 0.8, "epsilon": 0.0008, "normalEpsilon": 0.003,
        "maxDistance": 6000.0, "castShadows": cast, "depthPrepass": True,
        "look": {"aoStrength": 0.5, "aoDistance": 1.5}}}
    if scale is not None:
        n["scale"] = [scale] * 3
    if rotation is not None:
        n["rotation"] = rotation
    return n


def nodes(olive):
    out = []
    out.append(sdf_node("eye", EYE_AT, eye_tree(), CREAM, [-EYE_R - 1] * 3, [EYE_R + 1] * 3, 0.2, program="eye"))
    sx, sy, sz = SLAB_HALF
    out.append(sdf_node("slab", SLAB_AT, slab_tree(olive), EARTH[1],
                        [-sx - 12, -SLAB_Y - sy - 12, -sz - 12], [sx + 12, SLAB_Y + sy + 12, sz + 12], 0.95))
    # the olive, hung from the underside -- trunk and three limbs, each its own object (ADR-1160 shadows), scaled 3
    k = 3.2
    under = SLAB_Y - sy + 0.6
    parts = [("hangTrunk", olive.trunk_tree(), [0.0, 0.0, 0.0])]
    for li in range(3):
        parts.append((f"hangLimb{li}", olive.limb_tree(li), olive.limbs[li][0]))
    for name, tree, off in parts:
        # rotate 180 about X: (x, y, z) -> (x, -y, -z); then translate to the underside
        at = [12.0 + off[0] * k, under - off[1] * k, 6.0 - off[2] * k]
        lo = [12.0 - 16.0, -(under + 2.0), 6.0 - 16.0]
        hi = [12.0 + 16.0, under + 2.0, 6.0 + 16.0]
        root = tree["root"]
        if root.get("name") == "bark":
            root = root["children"][0]          # the hanging olive's bark is still: its nodes go to the mirror
        out.append(sdf_node(name, SLAB_AT, hanging(root, at, k), EARTH[0], [v / k for v in lo], [v / k for v in hi],
                            0.85, program="bark", scale=k))
    hz = forms.tanguy_tree()
    kt = 2.6
    out.append(sdf_node("hangTanguy", SLAB_AT, hanging(hz["root"]["children"][0], [-34.0, under - 5.2, 18.0], kt),
                        lin("#b6b8af"), [-40 / kt, -under / kt, 12 / kt], [-28 / kt, under / kt, 24 / kt], 0.3,
                        scale=kt))
    out.append(sdf_node("door", DOOR_AT, door_frame_tree(), CHALK, [-5.6, -19.6, -1.2], [5.6, 19.6, 1.2], 0.7,
                        rotation=[0, DOOR_YAW, 0]))
    out.append(sdf_node("doorNight", DOOR_AT, door_panel_tree(), NIGHT[1], [-3.8, -17.3, -0.2], [3.8, 17.3, 0.2],
                        0.9, program="night", rotation=[0, DOOR_YAW, 0], cast=False))
    ex, ey, ew = stair_extent()
    out.append(sdf_node("stair", STAIR_AT, stair_tree(), CHALK, [-1, -ey - 2, -ew - 1], [ex + 1, ey + 2, ew + 1], 0.8,
                        rotation=[0, STAIR_YAW, 0]))
    out.append(sdf_node("colossus", FLOWER_AT, flower_tree(), PORCELAIN, [-1.9, -7.8, -1.8], [2.3, 7.8, 2.0], 0.25,
                        scale=FLOWER_SCALE))
    for i, (at, s, sink) in enumerate(TANGUY_FORMS):
        out.append(sdf_node(f"form{i}", at, tanguy_mirrored(sink), lin("#b6b8af"), [-1.7, -3.2, -1.5],
                            [1.7, 3.2, 1.5], 0.35, scale=s))
    return out


# ============================================================================================ the flight
# (x, y, z) in world metres: low over the mirror past the eye, under the hanging land between it and its
# reflection, through the door, up beside the stair, down past the forms, home.
FLIGHT = [
    (60, 4, 250), (52, 3.5, 120), (40, 5, 46), (-10, 6, -40), (-90, 12, -100), (-170, 34, -150), (-240, 40, -175),
    (-300, 36, -200), (-250, 22, -290), (-120, 12, -260), (40, 9, -150), (122, 9, -110), (175, 8.8, -70),
    (225, 9, -32), (270, 14, 40), (230, 40, 110), (175, 75, 175), (110, 95, 205), (20, 70, 240), (-30, 30, 230),
    (-60, 8, 160), (-20, 4, 110), (30, 3.5, 190),
]


def flight_node():
    return {"name": "flight", "kind": "spline", "spline": {
        "name": "flight", "kind": "catmullRom", "closed": True, "tension": 0.5, "generator": "points",
        "samplesPerSegment": 24, "up": [0, 1, 0],
        "points": [{"position": [float(x), float(y), float(z)], "roll": 0.0, "scale": 1.0} for x, y, z in FLIGHT]}}


def flight_length():
    pts = [list(p) for p in FLIGHT]
    n = len(pts)
    total = 0.0
    for i in range(n):
        p0, p1, p2, p3 = pts[(i - 1) % n], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
        prev = p1
        for j in range(1, 17):
            t = j / 16
            t2, t3 = t * t, t * t * t
            q = [0.5 * ((2 * p1[a]) + (-p0[a] + p2[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t2
                        + (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t3) for a in range(3)]
            total += math.dist(prev, q)
            prev = q
    return total
