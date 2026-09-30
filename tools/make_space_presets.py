#!/usr/bin/env python3
"""Procedural Space POC: the art presets (brief Phases 7-8, section 18) and the showcase (sections 21, 41).

Writes examples/space/<preset>.json + <preset>.scene.json for every preset in PRESETS, sharing
examples/space/space.rig.json. Each project is one raymarched, compiled `sdf` node named `space`,
so every preset's parameters read `sdf/space/node/<name>/<field>`.

    python3 tools/make_space_presets.py            # all presets
    python3 tools/make_space_presets.py hall       # one (by key)

The trees are written with the helpers below; see docs/prototypes/procedural-space/ART-NOTES.md for
what each preset is, and why its rules move the way they do.
"""
import copy
import json
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "examples" / "space"


# ---- SDF node helpers --------------------------------------------------------------------------

def N(kind, *children, **kw):
    d = {"kind": kind}
    for k, v in kw.items():
        if v is None:
            continue
        d[k] = v
    if children:
        d["children"] = [c for c in children if c is not None]
    return d


def named(node, name):
    node["name"] = name
    return node


def box(size, name=None):
    return N("box", size=list(size), name=name)


def rbox(size, r, name=None):
    return N("roundedBox", size=list(size), rounding=r, name=name)


def plane(axis, offset, name=None):
    return N("plane", axis=list(axis), offset=offset, name=name)


def cyl(radius, height, name=None):
    return N("cylinder", radius=radius, height=height, name=name)


def torus(radius, minor, name=None):
    return N("torus", radius=radius, rounding=minor, name=name)


def T(t, child, name=None):
    return N("translate", child, translation=list(t), name=name)


def R(r, child, name=None):
    return N("rotate", child, rotation=list(r), name=name)


def U(*c, name=None):
    return N("union", *c, name=name)


def I(*c, name=None):
    return N("intersection", *c, name=name)


def D(a, *b, name=None):
    return N("difference", a, *b, name=name)


def SU(k, *c, name=None):
    return N("smoothUnion", *c, smooth=k, name=name)


def rep(size, child, count=None, name=None):
    return N("repeat", child, size=list(size), count=count, name=name)


def polar(count, child, name=None):
    return N("polarRepeat", child, count=count, name=name)


def mirror(mask, child, name=None):
    return N("mirror", child, size=list(mask), name=name)


def fold(axis, offset, child, name=None, enabled=None):
    n = math.sqrt(sum(a * a for a in axis))
    return N("fold", child, axis=[a / n for a in axis], offset=offset, name=name, enabled=enabled)


def twist(amount, child, name=None):
    return N("twist", child, amount=amount, name=name)


def bend(amount, child, name=None):
    return N("bend", child, amount=amount, name=name)


def recurse(child, count, scale, translation=(0, 0, 0), rotation=(0, 0, 0), mask=(0, 0, 0), name=None):
    return N("recurse", child, count=count, scale=scale, translation=list(translation), rotation=list(rotation),
             size=list(mask), name=name)


def morph(amount, *c, name=None):
    return N("morph", *c, amount=amount, name=name)


def slab_x(z, half_thick, y0, y1, name=None):
    """An infinite (along x) wall slab centred at z, from y0 to y1."""
    return T((0, (y0 + y1) / 2, z), box((4000, (y1 - y0) / 2, half_thick)), name=name)


def half_ring_yz(radius, minor, name=None):
    """A semicircular arch (upper half of a torus) standing in the y-z plane, springing at y = 0."""
    return I(R((0, 0, 90), torus(radius, minor, name=name)), plane((0, -1, 0), 0.0))


def barrel_x(radius, spring, name=None):
    """Solid above the springing line with a semicircular barrel vault (axis x) cut out of it."""
    return D(plane((0, -1, 0), -spring), T((0, spring, 0), R((0, 0, 90), cyl(radius, 8000))), name=name)


# ---- the look (section 26) ---------------------------------------------------------------------

# Deep blue-violet air; the geometry is darker than the air, so the far space glows faintly and every
# silhouette reads against it (depth by aerial perspective rather than by light).
AIR = [0.017, 0.011, 0.066]


def environment(density=0.028, air=None, volume=True, scattering=1.0):
    """The air: exponential distance fog to `air` (surfaces and misses fade to the same colour), plus the
    volumetric march, which scatters the scene's point lights into glowing halos in that air."""
    air = air or AIR
    env = {
        "intensity": 0.0,
        "background": list(air),
        "fogColor": list(air),
        "lightRig": "space-art.rig.json",
        "volumeDensity": density,
        "volumeAbsorption": 1.0,
        "volumeMaxDistance": 140.0 if volume else 0.0,
        "volumeScattering": scattering,
    }
    return env


def sdf_object(root, look=None, material=None, march=None, programs=None):
    look_block = {
        "aoStrength": 0.8,
        "aoDistance": 2.0,
        "edgeIntensity": 2.6,
        "edgeWidth": 0.05,
        "edgeColor": [0.20, 0.70, 1.0],
        "shadowStrength": 0.0,
        "shadowSoftness": 10.0,
        "shadowDirection": [0.35, 1.0, 0.25],
        "shadowSteps": 32,
    }
    look_block.update(look or {})
    mat = {
        "baseColor": [0.014, 0.015, 0.026],
        "emissiveColor": [0.15, 1.0, 0.55],
        "emissiveIntensity": 0.0,
        "roughness": 0.55,
        "metallic": 0.2,
    }
    mat.update(material or {})
    m = {
        "maxSteps": 160,
        "epsilon": 0.0012,
        "stepScale": 0.8,
        "normalEpsilon": 0.004,
        "maxDistance": 260.0,
    }
    m.update(march or {})
    obj = {
        "tree": {"root": root},
        "material": mat,
        "renderMode": "raymarch",
        "boundsMin": [-2000, -400, -2000],
        "boundsMax": [2000, 400, 2000],
        "look": look_block,
        "castShadows": False,
        # The depth prepass is off: GTAO then does not see the SDF (the look block's own AO covers it),
        # which removes a 2-pixel lattice GTAO left on grazing SDF floors, and saves the second march.
        "depthPrepass": False,
        "compile": True,
    }
    obj.update(m)
    return obj


def point_light(name, pos, color, intensity, rng):
    return {"name": name, "type": "point", "position": list(pos), "color": list(color), "intensity": intensity,
            "range": rng, "castsShadow": False}


def scene(name, root, camera, env=None, look=None, material=None, march=None, programs=None, lights=None):
    s = {
        "format": "avgen-scene",
        "version": 1,
        "name": name,
        "camera": camera,
        "environment": env or environment(),
        "nodes": [{"name": "space", "kind": "sdf", "sdf": sdf_object(root, look, material, march)}],
    }
    if lights:
        s["lights"] = lights
    if programs:
        s["materialPrograms"] = programs
        s["nodes"][0]["sdf"]["material"]["program"] = programs[0]["name"]
    return s


def still_camera(pos, target, fov=70.0):
    return {"mode": 1, "position": list(pos), "target": list(target), "fov": fov, "orbitSpeed": 0.0,
            "distance": 22.0, "height": 6.0}


BASE_PARAMS = {
    "lightrig/SpaceArt/key/intensity": 0.2,
    "lightrig/SpaceArt/rim/intensity": 0.08,
    "scene/brightness": 1.0,
    "camera/exposure/mode": 0,
    "camera/exposure/compensation": -0.2,
    "post/tonemap/operator": 1,
    "post/bloom/enabled": True,
    "post/bloom/intensity": 0.25,
    "post/bloom/threshold": 1.0,
    "post/bloom/emissionWeight": 1.0,
    "post/grade/contrast": 1.1,
    "post/grade/saturation": 1.25,
    "post/output/vignette": 0.4,
    # No film grain: it adds per-frame noise everywhere, which the Critic measures as shimmer (whole-frame
    # temporal std 0.95 with 0.015 grain, 0.03 without, on a static hall).
    "post/output/grain": 0.0,
    # A lower jitter on the volumetric march: the lamp halos' temporal noise falls by a third (0.61 -> 0.43)
    # at no cost; lower still slices the halo.
    "scene/volumeJitter": 0.5,
}


def route(source, target, amount, attack=None, decay=None, component=-1, op="add", polarity="unipolar", extra=None):
    r = {"source": source, "target": target, "amount": amount, "op": op, "polarity": polarity, "component": component}
    if attack is not None or decay is not None:
        r["chain"] = {"attackMs": attack or 0, "decayMs": decay or 0}
    if extra:
        r.update(extra)
    return r


def project(key, scene_file, params=None, routes=None, sources=None, presets=None, states=None, end=30.0):
    p = {
        "format": "avgen-project",
        "version": 4,
        "app": {"name": "avgen", "version": "0.1.0"},
        "assets": {
            "audio": {"path": "../../assets/audio/night-shift.wav"},
            "scene": {"kind": "composition", "path": scene_file},
        },
        "parameters": dict(BASE_PARAMS, **(params or {})),
        "sources": sources or [],
        "routes": routes or [],
        "presets": presets or [],
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "startSeconds": 0.0,
                   "endSeconds": end},
    }
    if states:
        p["states"] = states
    return p


# ---- sources shared by the changing spaces --------------------------------------------------------

def macro_source(knobs):
    """A macro rack: `macros/<knob>` is a parameter (states set it), `macro.<knob>` a signal (routes read it)."""
    return {"kind": "macro", "name": "macros", "settings": {"knobs": [{"name": k, "default": v} for k, v in knobs.items()]}}


def random_source(name, trigger, seed):
    """A new seeded value in [0, 1) on every `trigger` event, held until the next (deterministic)."""
    return {"kind": "random", "name": name, "settings": {"trigger": trigger, "seed": seed}}


def step_chain(level=0.5):
    """A binary threshold: the route's source becomes exactly 0 or 1 (a discrete structural step)."""
    return {"threshold": "binary", "thresholdLevel": level}


def P(node, field):
    return f"sdf/space/node/{node}/{field}"


# ---- the frame every space is authored in ------------------------------------------------------------
#
# The interpreter's point stack binds a tree to 8 nested unary operators on any path (docs/sdf.md,
# "Validation and limits"), so no node is spent on pivots: every space is authored with its floor at
# y = Y0, which puts the origin at the hall's mid-height, on its axis. Roll (a twist about x), bend
# (about z) and the radial repeat (about y) then all pivot on the eye's own line of sight, with no
# translate around them.

Y0 = -7.0
EYE = (0.0, Y0 + 3.2, 0.0)


def floor_plane():
    return plane((0, 1, 0), Y0)


# ---- Infinite Hall (normal architecture) ------------------------------------------------------------

BAY = 8.0        # the module: pier spacing along the hall
NAVE = 7.0       # the pier line (|z|); also the vault's radius
SPRING = 15.0    # the vault's springing height above the floor
WALL = 13.0      # the outer walls (|z|)


def hall_structure(imposts=True):
    y = lambda h: Y0 + h
    # Base, shaft and impost as three repeated rows (not one repeated union): one unary level fewer.
    piers = mirror((0, 0, 1), rep((BAY, 0, 0), T((0, y(SPRING / 2), NAVE), rbox((0.85, SPRING / 2, 0.85), 0.1, name="pier"),
                                                 name="pierAt"), name="piers"))
    bases = mirror((0, 0, 1), rep((BAY, 0, 0), T((0, y(0.35), NAVE), box((1.3, 0.35, 1.3)), name="baseAt"), name="bases"))
    imposts_ = mirror((0, 0, 1), rep((BAY, 0, 0), T((0, y(SPRING - 0.35), NAVE), box((1.2, 0.35, 1.2)), name="impostAt"),
                                     name="imposts"))
    ribs = rep((BAY, 0, 0), T((0, y(SPRING), 0), half_ring_yz(NAVE, 0.45, name="rib")), name="ribs")
    vault = D(plane((0, -1, 0), -y(SPRING)), T((0, y(SPRING), 0), R((0, 0, 90), cyl(NAVE, 8000, name="vaultBore"))))
    # Portals between the piers (half a bay along), through thin outer walls, onto the haze beyond.
    portals = T((BAY / 2, y(4.6), WALL), rep((BAY, 0, 0), box((1.5, 4.6, 1.2), name="portal"), name="portals"))
    walls = mirror((0, 0, 1), D(slab_x(WALL, 0.5, y(-1), y(SPRING + 1)), portals))
    parts = [floor_plane(), vault, walls, piers, bases] + ([imposts_] if imposts else []) + [ribs]
    return U(*parts, name="hall")


HALL_CAMERA = still_camera(EYE, (40, Y0 + 6.8, 0), 68.0)

BLUE_LIGHT = (0.22, 0.42, 1.0)
GREEN_LIGHT = (0.22, 1.0, 0.52)


def nave_lights(intensity=80.0, y=Y0 + 11.0):
    """Five lamps down the nave's axis; the two farthest are the restrained green."""
    cols = [BLUE_LIGHT] * 3 + [GREEN_LIGHT] * 2
    return [point_light(f"nave{i}", (10 + 16 * i, y, 0), c, intensity, 24.0) for i, c in enumerate(cols)]


def hall_widening_routes(amount, depth=None, rows=("pierAt", "baseAt", "impostAt")):
    """Bass slowly widens the nave: the pier rows, the vault bore and the ribs move together (one rule)."""
    x = {"depthSource": depth} if depth else None
    ch = dict(attack=1800, decay=6000, extra=x)
    r = [route("audio.bass", P(n, "translation"), amount, component=2, **ch) for n in rows]
    return r + [route("audio.bass", P("vaultBore", "radius"), amount, **ch),
                route("audio.bass", P("rib", "radius"), amount, **ch)]


def hall_rhythm_routes(amount, depth=None, rows=("piers", "bases", "imposts", "ribs")):
    """Mid tightens the rhythm: the bay contracts, so the far colonnade gathers towards the eye."""
    x = {"depthSource": depth} if depth else None
    return [route("audio.mid", P(n, "size"), amount, component=0, attack=900, decay=4500, extra=x)
            for n in rows]


def preset_hall():
    sc = scene("Infinite Hall", hall_structure(), HALL_CAMERA, env=environment(), lights=nave_lights())
    step = dict(step_chain(), attackMs=1100, decayMs=1100)
    routes = hall_widening_routes(3.0) + hall_rhythm_routes(-2.0) + [
        # Each bar re-spaces the colonnade or not: the rhythm of the architecture is re-ruled on the downbeat.
        *[route("random.bar", P(n, "size"), -2.5, component=0, extra={"chain": step})
          for n in ("piers", "bases", "imposts", "ribs")],
        # Treble lights the edges: fine detail emerges with the hats.
        route("audio.treble", "sdf/space/look/edge/intensity", 2.4, attack=60, decay=900),
    ]
    # Each phrase (8 bars) changes the order of the architecture: square piers, then round columns, then back.
    presets = [{"name": "order/square", "values": {P("pier", "rounding"): [0.1]}},
               {"name": "order/round", "values": {P("pier", "rounding"): [0.8]}}]
    states = {"initial": "Square", "states": [
        {"name": "Square", "preset": "order/square", "transition": {"seconds": 1.2, "easing": "smooth", "quantize": "bar"},
         "triggers": [{"kind": "bar", "every": 8, "from": "Round"}]},
        {"name": "Round", "preset": "order/round", "transition": {"seconds": 1.2, "easing": "smooth", "quantize": "bar"},
         "triggers": [{"kind": "bar", "every": 8, "from": "Square"}]}]}
    pr = project("hall", "infinite-hall.scene.json", routes=routes, presets=presets, states=states,
                 sources=[random_source("bar", "music.bar", 1004)])
    return {"infinite-hall": (pr, sc)}


# ---- Recursive Cathedral (impossible architecture) --------------------------------------------------

def rib_dome(radius, spring, n, minor, name=None):
    """Polar quarter-arc ribs from a ring of radius `radius` at height `spring` up to the apex on the axis."""
    rib = I(T((0, spring, 0), R((90, 0, 0), torus(radius, minor))), plane((0, -1, 0), -spring), plane((-1, 0, 0), 0.0))
    return polar(n, rib, name=name)


CR = 24.0        # the outer cathedral's radius
CH = 20.0        # its drum height (the dome springs here)


def rotunda_chamber(pilasters=True):
    """One domed rotunda with eight tall doorways, local floor at y = 0, axis on y.

    Its doorways sit at the polar sectors' centres, one facing -x (the eye): when every nested level is
    turned by a multiple of the sector (45 degrees) the doorways line up into one tunnel through all of
    them; any other turn closes it."""
    drum = D(T((0, CH / 2, 0), cyl(CR + 0.7, CH)), T((0, CH / 2, 0), cyl(CR - 0.7, CH + 2)),
             polar(8, T((CR, 7.5, 0), box((2.5, 7.5, 3.6), name="door")), name="doors"))
    dome = D(T((0, CH, 0), N("sphere", radius=CR + 0.7)), T((0, CH, 0), N("sphere", radius=CR - 0.7)),
             plane((0, 1, 0), CH), T((0, CH + CR, 0), cyl(CR * 0.2, 6.0, name="oculus")))
    cornice = T((0, CH, 0), torus(CR - 0.7, 0.55))
    # Half a sector off the doorways' angles (16 pilasters share the 8 doors' angles otherwise, and one
    # would stand in the doorway, blocking the tunnel through the levels).
    pilasters_ = R((0, 11.25, 0), polar(16, T((CR - 1.1, CH / 2, 0), rbox((0.45, CH / 2, 0.6), 0.08)), name="pilasters"))
    return U(drum, dome, cornice, *([pilasters_] if pilasters else []), name="chamber")


CATHEDRAL_AT = 22.0


def cathedral_structure(count=3, pilasters=True):
    """The rotunda nested in itself: each level 1/scale the size, turned against its parent, all on one floor.

    `recurse` scales about the chamber's floor centre (p_{l+1} = conj(R) p_l scale), so every level stands on
    the same floor and each smaller rotunda stands inside the larger one: a building inside a building."""
    nest = recurse(rotunda_chamber(pilasters), count, 2.1, rotation=(0, 0, 0), name="nest")
    return U(floor_plane(), T((CATHEDRAL_AT, Y0, 0), nest, name="nestAt"), name="cathedral")


CATHEDRAL_CAMERA = still_camera((0, Y0 + 2.4, 0), (CATHEDRAL_AT, Y0 + 5.0, 0), 66.0)


def preset_cathedral():
    # One lamp at the heart of the recursion (inside the smallest rotunda: its light leaves through every
    # nested doorway) and one under the outer dome.
    lights = [point_light("heart", (CATHEDRAL_AT, Y0 + 1.2, 0), GREEN_LIGHT, 40.0, 16.0),
              point_light("dome", (CATHEDRAL_AT, Y0 + 30.0, 0), BLUE_LIGHT, 160.0, 40.0),
              point_light("inner", (CATHEDRAL_AT - 6.0, Y0 + 9.0, 0), BLUE_LIGHT, 70.0, 18.0)]
    sc = scene("Recursive Cathedral", cathedral_structure(4), CATHEDRAL_CAMERA, env=environment(), lights=lights)
    routes = [
        # High-mid energy opens further levels of the nesting: the fine detail is the smaller cathedrals.
        route("audio.highMid", P("nest", "count"), 2.0, attack=1200, decay=5000),
        # Bass sets how tightly the levels nest (the ratio between one cathedral and the next).
        route("audio.bass", P("nest", "scale"), 0.3, attack=1500, decay=6000),
        # Each bar either lines the nested doorways up into one tunnel (a turn of 0) or closes it (22.5 degrees).
        route("random.bar", P("nest", "rotation"), 22.5, component=1,
              extra={"chain": dict(step_chain(), attackMs=700, decayMs=700)}),
        route("audio.treble", "sdf/space/look/edge/intensity", 2.0, attack=60, decay=900),
    ]
    pr = project("cathedral", "recursive-cathedral.scene.json", routes=routes,
                 sources=[random_source("bar", "music.bar", 1005)])
    return {"recursive-cathedral": (pr, sc)}


# ---- the rule chain ------------------------------------------------------------------------------

def roll(content, name):
    """A twist about the view axis (x): the far space turns over about the line of sight."""
    return R((0, 0, 90), twist(0.0, R((0, 0, -90), content), name=name))


def fold_axis(deg_xy=0.0, deg_xz=0.0):
    """A fold plane's normal pointing back at the eye (-x), tilted up by deg_xy and sideways by deg_xz."""
    a, b = math.radians(deg_xy), math.radians(deg_xz)
    v = (-math.cos(a) * math.cos(b), math.sin(a), math.sin(b) * math.cos(a))
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


OFF = -1000.0   # a fold plane this far behind the eye reflects nothing


def hall_rules(prefix=""):
    """The hall under three rules: roll (3 unary levels), bend and one fold. With the hall's own 3 that is
    the interpreter's limit of 8."""
    content = fold((-1, 0, 0), OFF, hall_structure(), name=prefix + "fold")
    return roll(bend(0.0, content, name=prefix + "bend"), prefix + "roll")


# ---- Folding Space -------------------------------------------------------------------------------
#
# The hall under its rules, stepped by the bar: every 4 bars the space folds one step further (bend,
# then roll, then the fold plane), and each phrase end lets it spring back flat. The steps are scene
# states (discrete, quantised to the bar); the bands move the same rules continuously inside each step.

def fold_states(prefix, steps, every=4, cycle=16, seconds=3.0):
    """States named <prefix>0..n-1: each advances on a bar count, the last returns to the first."""
    out = []
    n = len(steps)
    for i in range(n):
        prev = f"{prefix}{(i - 1) % n}"
        trig = [{"kind": "bar", "every": every, "from": prev}] if i > 0 else [{"kind": "bar", "every": cycle, "from": prev}]
        out.append({"name": f"{prefix}{i}", "preset": f"{prefix.lower()}/{i}",
                    "transition": {"seconds": seconds, "easing": "smooth", "quantize": "bar"}, "triggers": trig})
    return {"initial": f"{prefix}0", "states": out}


def preset_folding():
    sc = scene("Folding Space", hall_rules(), HALL_CAMERA, env=environment(), lights=nave_lights(),
               march={"stepScale": 0.6, "maxSteps": 200})
    steps = [  # (bend, roll, fold offset): flat -> curled -> turned over -> folded
        (0.0, 0.0, FOLD_NEAR_OFF),
        (0.016, 0.0, FOLD_NEAR_OFF),
        (0.016, 0.02, FOLD_NEAR_OFF),
        (0.008, 0.02, -18.0),
    ]
    presets = [{"name": f"fold/{i}", "values": {P("bend", "amount"): [b], P("roll", "amount"): [r], P("fold", "offset"): [o]}}
               for i, (b, r, o) in enumerate(steps)]
    routes = [
        route("audio.lowMid", P("bend", "amount"), 0.004, attack=2500, decay=7000),
        route("audio.energy", P("roll", "amount"), 0.004, attack=3000, decay=8000),
        route("audio.treble", "sdf/space/look/edge/intensity", 2.0, attack=60, decay=900),
    ] + hall_widening_routes(1.5)
    params = {P("fold", "axis"): fold_axis(30, 30), P("fold", "offset"): FOLD_NEAR_OFF}
    pr = project("folding", "folding-space.scene.json", routes=routes, params=params, presets=presets,
                 states=fold_states("Fold", steps))
    return {"folding-space": (pr, sc)}


# ---- Mathematical space: the hall's module as a lattice ---------------------------------------------

LCELL = 16.0


def lattice_structure():
    """The hall's cross-section (two piers and an arch) repeated in all three axes: an infinite grid of
    vaulted arcades, stacked with no floors between them. The pier bases of one row stand on the eye's
    floor level (y = Y0)."""
    h = LCELL / 2
    # piers from the cell floor to 3 below its top, an arch of radius 5 springing there (apex at the top)
    piers = mirror((0, 0, 1), T((0, -h + (2 * h - 5.0) / 2, 5.0), box((0.5, (2 * h - 5.0) / 2, 0.5))))
    arch = T((0, h - 5.0, 0), half_ring_yz(5.0, 0.45))
    cell = U(piers, arch, name="cell")
    return T((h, Y0 + h, 0), rep((LCELL, LCELL, LCELL), cell, name="lattice"), name="latticeAt")


def lattice_rules(content, p=""):
    """Abstraction: the lattice turned into a kaleidoscope about the line of sight. The two rotations make
    the view axis (x) the local y axis, so the twist and the radial repeat both act about it: the radial
    repeat gives an n-fold symmetry centred on the vanishing point, the twist turns that symmetry into a
    spiral with depth. Four unary levels, which with the lattice's own four is the interpreter's limit."""
    return R((0, 0, 90), twist(0.0, polar(0, R((0, 0, -90), content), name=p + "radial"), name=p + "twist"))


# ---- Radial Architecture ----------------------------------------------------------------------------

def rotunda_structure(chapels=True):
    y = lambda h: Y0 + h
    colA = polar(12, T((12, y(9), 0), rbox((0.6, 9, 0.6), 0.1)), name="ringA")
    entA = T((0, y(18), 0), torus(12, 0.5))
    colB = polar(24, T((24, y(13), 0), rbox((0.9, 13, 0.9), 0.12)), name="ringB")
    entB = T((0, y(26), 0), torus(24, 0.7))
    dome = rib_dome(24, y(26), 24, 0.45, name="domeRibs")
    # Radial chapels: walls on the spokes from r = 30 to r = 46, each pierced by a doorway.
    ch = polar(16, D(T((38, y(12), 0), box((8, 12, 0.5))), T((38, y(5), 0), box((1.8, 5, 1.0)))), name="chapels")
    return U(floor_plane(), colA, entA, colB, entB, dome, *([ch] if chapels else []), name="rotunda")


def radial_lights():
    # A dim lamp in the oculus (looked at straight on, a bright one fills the frame with its halo), four
    # blue lamps between the rings, and the restrained green low in the chapels' ring.
    return [point_light("oculus", (0, Y0 + 34, 0), BLUE_LIGHT, 90.0, 40.0)] + \
           [point_light(f"ring{i}", (18 * math.cos(a), Y0 + 20, 18 * math.sin(a)), BLUE_LIGHT, 45.0, 18.0)
            for i, a in enumerate([k * math.pi / 2 + math.pi / 4 for k in range(4)])] + \
           [point_light(f"chapel{i}", (36 * math.cos(a), Y0 + 3, 36 * math.sin(a)), GREEN_LIGHT, 20.0, 12.0)
            for i, a in enumerate([k * math.pi / 2 for k in range(4)])]


def preset_radial():
    # The eye stands at the centre and looks straight up: every polar repeat becomes rotational symmetry
    # about the view axis. The twist is about that same (vertical) axis, so it winds columns and ribs into
    # a spiral without breaking the symmetry.
    root = R((0, 0, 0), twist(0.0, rotunda_structure(), name="spiral"), name="turn")
    sc = scene("Radial Architecture", root, still_camera((0.25, Y0 + 1.7, 0.15), (0.6, Y0 + 70.0, 0.35), 84.0),
               env=environment(0.022), lights=radial_lights(), march={"stepScale": 0.7})
    routes = [
        # Mid sets the radial density: the inner ring gains columns as the mids rise.
        route("audio.mid", P("ringA", "count"), 8.0, attack=900, decay=4000),
        route("audio.highMid", P("domeRibs", "count"), 12.0, attack=900, decay=4000),
        # Low-mids wind the whole rotunda into a spiral about the view axis, slowly.
        route("audio.lowMid", P("spiral", "amount"), 0.035, attack=2500, decay=7000),
        # Each bar turns the rotunda by a whole bay of the outer ring or back: a combination lock.
        route("random.bar", P("turn", "rotation"), 15.0, component=1,
              extra={"chain": dict(step_chain(), attackMs=600, decayMs=600)}),
        route("audio.treble", "sdf/space/look/edge/intensity", 1.6, attack=60, decay=900),
    ]
    pr = project("radial", "radial-architecture.scene.json", routes=routes,
                 params={"sdf/space/look/edge/intensity": 3.2},
                 sources=[random_source("bar", "music.bar", 1009)])
    return {"radial-architecture": (pr, sc)}


# ---- Geometry Explosion -----------------------------------------------------------------------------

def gate_structure():
    """A single arched gateway: two piers and a round arch. Local floor at y = 0."""
    return U(T((0, 6, 4.5), rbox((0.8, 6, 0.8), 0.1)), T((0, 6, -4.5), rbox((0.8, 6, 0.8), 0.1)),
             T((0, 12, 0), half_ring_yz(4.5, 0.55)), name="gate")


def preset_explosion():
    nest = recurse(gate_structure(), 0, 1.7, translation=(0, 0, 9), rotation=(0, 0, 0), mask=(0, 0, 1), name="nest")
    content = polar(0, T((22, Y0, 0), rep((0, 0, 0), nest, count=2, name="rows"), name="gateAt"), name="radial")
    # Twisting here is about the vertical axis through the eye (one unary level, not the roll's three):
    # the radial copies spiral as they rise.
    root = U(floor_plane(), twist(0.0, content, name="roll"))
    lights = [point_light("gateLight", (22, Y0 + 9, 0), BLUE_LIGHT, 130.0, 28.0),
              point_light("far", (60, Y0 + 12, 0), GREEN_LIGHT, 60.0, 30.0)]
    sc = scene("Geometry Explosion", root, still_camera(EYE, (40, Y0 + 8.0, 0), 70.0), env=environment(0.024),
               lights=lights, march={"stepScale": 0.65, "maxSteps": 200})
    # One stage per 2 bars: repetition, recursion (with its mirror symmetry), radial structure, twisting,
    # then everything at once.
    steps = [  # rows size, rows count, nest count, radial count, roll
        dict(size=0.0, count=2, nest=0, radial=0, roll=0.0),
        dict(size=14.0, count=3, nest=0, radial=0, roll=0.0),
        dict(size=14.0, count=3, nest=3, radial=0, roll=0.0),
        dict(size=14.0, count=3, nest=3, radial=6, roll=0.0),
        dict(size=14.0, count=3, nest=3, radial=6, roll=0.03),
        # (a row spacing of 11 put a gate's plane through the eye at x = 22 - 2 * 11; 13 keeps the nearest 4 behind it)
        dict(size=13.0, count=4, nest=4, radial=9, roll=0.05),
    ]
    presets = [{"name": f"explode/{i}", "values": {
        P("rows", "size"): [st["size"], 0.0, 0.0], P("rows", "count"): [st["count"]],
        P("nest", "count"): [st["nest"]], P("radial", "count"): [st["radial"]], P("roll", "amount"): [st["roll"]]}}
        for i, st in enumerate(steps)]
    states = fold_states("Explode", steps, every=2, cycle=8, seconds=1.2)
    routes = [
        route("audio.highMid", "sdf/space/look/edge/intensity", 2.5, attack=200, decay=2500),
        route("audio.bass", P("nest", "scale"), 0.25, attack=1500, decay=5000),       # extrusion of the nest
        route("audio.mid", P("roll", "amount"), 0.006, attack=2000, decay=6000),
    ]
    params = {}
    pr = project("explosion", "geometry-explosion.scene.json", routes=routes, params=params, presets=presets,
                 states=states)
    return {"geometry-explosion": (pr, sc)}


# ---- The showcase (sections 21, 22, 41) ----------------------------------------------------------------
#
# normal -> strange -> impossible -> mathematical -> abstraction -> reformation, under a still camera.
# One morph holds four structures; each carries its own rules, so a stage is a set of rule values, and the
# song's phrase structure (bar counts, and the drop) moves the space from stage to stage. A macro knob,
# `macros/rules`, is the depth of every band route that bends a rule: 0 in the normal hall, rising stage by
# stage, so the music's authority over the space grows with the song.

SHOW_STAGES = [
    # name, transition seconds, easing, trigger bar (counted from the song's first downbeat; from the previous stage)
    ("Normal", 0.0, "smooth", None),
    ("Proportion", 6.0, "smooth", 16),    # bar 17 (30 s): the first fill; the hall re-proportions itself
    ("Strange", 8.0, "smooth", 32),       # bar 33 (59.6 s): the hats enter
    ("Folded", 6.0, "smooth", 48),        # bar 49 (89 s): the mids start to climb
    ("Cathedral", 8.0, "smooth", 64),     # bar 65 (118.6 s)
    ("Mathematical", 8.0, "smooth", 80),  # bar 81 (148 s): the build to the drop
    ("Abstraction", 2.0, "easeIn", 88),   # bar 89 (163 s): the drop (music.drop is a second trigger)
    ("Unwinding", 8.0, "smooth", 104),    # bar 105 (192.5 s)
    ("Reformed", 10.0, "smooth", 112),    # bar 113 (207 s): the last section
]

FOLD_NEAR_OFF = -110.0   # a fold plane just beyond the visible fog: its approach is visible from the start
AXIS_EYE = [0.0, 0.0, 0.0]


def show_values(stage):
    eye, hall_t = list(EYE), [40.0, Y0 + 6.8, 0.0]
    v = {  # every stage writes every value, so every transition interpolates all of them
        "state": 0.0, "hroll": 0.0, "hfold": FOLD_NEAR_OFF, "hrad": 0, "nave": NAVE, "bay": BAY, "round": 0.1,
        "nestCount": 0, "rules": 0.0, "spin": 0.0, "density": 0.028, "eye": eye, "target": hall_t, "heart": 0.0,
        "edge": 2.6, "edgeW": 0.05, "fov": 68.0, "lampY": Y0 + 11.0, "lamp": 80.0, "turn": 0.0, "scatter": 1.0,
        "ev": -0.2,
    }
    if stage == "Proportion":
        v.update(nave=8.5, bay=9.5, rules=0.2)
    elif stage == "Strange":
        v.update(nave=8.5, bay=9.5, hroll=0.006, rules=0.55)
    elif stage == "Folded":
        v.update(nave=8.5, bay=9.5, hroll=0.004, hfold=-20.0, rules=0.8, heart=12.0, ev=-0.1)
    elif stage == "Cathedral":
        v.update(nave=8.5, bay=9.5, state=1.0, hroll=0.004, hfold=-20.0, nestCount=4, rules=0.8,
                 heart=30.0, target=[22.0, Y0 + 5.5, 0.0], density=0.026, ev=-0.05)
    elif stage == "Mathematical":
        # the hall returns as pure symmetry: four copies of its vault about the line of sight
        # (the chamber turned a quarter so each sector holds a colonnade, its portals and a strip of floor)
        v.update(hrad=4, rules=0.4, eye=AXIS_EYE, target=[40.0, 0.0, 0.0], fov=58.0, lampY=0.0, lamp=10.0,
                 density=0.028, turn=90.0, edgeW=0.06, scatter=0.6, ev=0.05)
    elif stage == "Abstraction":
        v.update(hrad=6, hroll=0.02, hfold=-24.0, rules=1.0, spin=1.0, eye=AXIS_EYE, target=[40.0, 0.0, 0.0],
                 fov=50.0, lampY=0.0, lamp=12.0, density=0.028, edge=3.6, edgeW=0.075, turn=90.0, scatter=0.6,
                 ev=0.25)
    elif stage == "Unwinding":
        v.update(hrad=3, hroll=0.008, rules=0.5, spin=0.5, eye=AXIS_EYE, target=[40.0, 0.0, 0.0], fov=58.0,
                 lampY=0.0, lamp=16.0, density=0.026, turn=90.0, edgeW=0.06, scatter=0.7, ev=0.05)
    elif stage == "Reformed":
        # the hall again, under new rules: a wider nave, a slower rhythm, round columns
        v.update(nave=10.0, bay=11.0, round=0.8, rules=0.3)
    y = lambda h: Y0 + h
    out = {
        P("state", "amount"): [v["state"]], P("hroll", "amount"): [v["hroll"]], P("hfold", "offset"): [v["hfold"]],
        P("hradial", "count"): [v["hrad"]],
        P("pierAt", "translation"): [0.0, y(SPRING / 2), v["nave"]], P("baseAt", "translation"): [0.0, y(0.35), v["nave"]],
        P("impostAt", "translation"): [0.0, y(SPRING - 0.35), v["nave"]], P("vaultBore", "radius"): [v["nave"]],
        P("rib", "radius"): [v["nave"]], P("pier", "rounding"): [v["round"]],
        P("piers", "size"): [v["bay"], 0.0, 0.0], P("bases", "size"): [v["bay"], 0.0, 0.0],
        P("imposts", "size"): [v["bay"], 0.0, 0.0], P("ribs", "size"): [v["bay"], 0.0, 0.0],
        P("nest", "count"): [v["nestCount"]], "macros/rules": [v["rules"]], "macros/spin": [v["spin"]],
        "scene/volumeDensity": [v["density"]], "camera/position": v["eye"], "camera/target": v["target"],
        "camera/fov": [v["fov"]], "lights/heart/intensity": [v["heart"]], "sdf/space/look/edge/intensity": [v["edge"]],
        "sdf/space/look/edge/width": [v["edgeW"]], P("hspin", "rotation"): [0.0, v["turn"], 90.0],
        "scene/volumeScattering": [v["scatter"]], "camera/exposure/compensation": [v["ev"]],
    }
    for i in range(5):
        out[f"lights/nave{i}/position"] = [10.0 + 16.0 * i, v["lampY"], 0.0]
        out[f"lights/nave{i}/intensity"] = [v["lamp"]]
    return out


def showcase_hall():
    """The hall under its whole rule chain. The two rotations turn the view axis (x) into the local y axis,
    so the twist ("hroll", a roll that grows with distance), the radial repeat ("hradial", a kaleidoscope
    about the line of sight) and the outer rotation's own angle ("hspin", turning the space inside the
    kaleidoscope, as a kaleidoscope's object chamber turns) all act about the line of sight. Then one fold
    plane. Five unary levels, with the hall's own three: the interpreter's limit of eight."""
    content = fold((-1, 0, 0), FOLD_NEAR_OFF, hall_structure(), name="hfold")
    return R((0, 0, 90), twist(0.0, polar(0, R((0, 0, -90), content), name="hradial"), name="hroll"), name="hspin")


def preset_showcase():
    root = morph(0.0, showcase_hall(), cathedral_structure(0), name="state")
    lights = nave_lights() + [point_light("heart", (CATHEDRAL_AT, Y0 + 1.2, 0), GREEN_LIGHT, 0.0, 16.0)]
    sc = scene("Procedural Space showcase", root, HALL_CAMERA, env=environment(), lights=lights,
               march={"stepScale": 0.65, "maxSteps": 200})
    rules = {"depthSource": "macro.rules"}
    step = lambda ms: dict(step_chain(), attackMs=ms, decayMs=ms)
    routes = hall_widening_routes(1.8) + hall_rhythm_routes(-1.5) + [
        # the hall's rules, with the music's authority set by the stage
        route("audio.energy", P("hroll", "amount"), 0.010, attack=3000, decay=8000, extra=rules),
        # each phrase squares the piers or rounds them into columns (in every stage: it is an order, not a distortion)
        route("random.phrase", P("pier", "rounding"), 0.7, extra={"chain": step(900)}),
        # each bar re-spaces the colonnade or not (the rhythm), and tilts the fold plane or not
        route("random.bar", P("piers", "size"), -2.5, component=0, extra={"chain": step(1100), **rules}),
        route("random.bar", P("bases", "size"), -2.5, component=0, extra={"chain": step(1100), **rules}),
        route("random.bar", P("imposts", "size"), -2.5, component=0, extra={"chain": step(1100), **rules}),
        route("random.bar", P("ribs", "size"), -2.5, component=0, extra={"chain": step(1100), **rules}),
        route("random.bar", P("hfold", "axis"), 0.9, component=1, extra={"chain": step(900), **rules}),
        # the kaleidoscope: each phrase adds or removes sectors, each bar two more or not; it turns slowly
        route("random.phrase", P("hradial", "count"), 4.0, extra={"chain": step_chain(), "depthSource": "macro.spin"}),
        route("random.bar2", P("hradial", "count"), 2.0, extra={"chain": step_chain(), "depthSource": "macro.spin"}),
        # and each bar turns the kaleidoscope's chamber to a new angle (eased over a second and a half)
        route("random.turn", P("hspin", "rotation"), 60.0, component=1, attack=1500, decay=1500,
              extra={"depthSource": "macro.spin"}),
        # the cathedral's: high-mids open nesting levels, bass sets the ratio, each bar aligns or turns the levels
        route("audio.highMid", P("nest", "count"), 2.0, attack=1200, decay=5000, extra=rules),
        route("audio.bass", P("nest", "scale"), 0.3, attack=1500, decay=6000),
        route("random.bar", P("nest", "rotation"), 22.5, component=1, extra={"chain": step(700), **rules}),
        # light: treble lights the edges; the overall energy raises the lamps, slowly
        route("audio.treble", "sdf/space/look/edge/intensity", 1.2, attack=60, decay=900),
        route("audio.highMid", "sdf/space/look/edge/intensity", 1.2, attack=400, decay=3000, extra=rules),
    ] + [route("audio.energy", f"lights/nave{i}/intensity", 40.0, attack=2000, decay=6000) for i in range(5)] + [
        # a drift of five units down the axis over the whole song: barely perceptible, never the motion
        route("lfo.drift", "camera/position", 5.0, component=0), route("lfo.drift", "camera/target", 5.0, component=0),
    ]
    presets = []
    states = []
    for i, (name, secs, ease, bar) in enumerate(SHOW_STAGES):
        presets.append({"name": f"show/{name.lower()}", "values": show_values(name)})
        st = {"name": name, "preset": f"show/{name.lower()}"}
        if bar is not None:
            prev = SHOW_STAGES[i - 1][0]
            st["transition"] = {"seconds": secs, "easing": ease, "quantize": "bar"}
            st["triggers"] = [{"kind": "bar", "every": bar, "from": prev}]
            if name == "Abstraction":
                st["triggers"].append({"kind": "signal", "signal": "music.drop", "threshold": 0.5, "from": prev})
        states.append(st)
    params = {k: (v if len(v) != 1 else v[0]) for k, v in show_values("Normal").items()}
    params[P("hfold", "axis")] = fold_axis(30, 30)
    pr = project("showcase", "showcase.scene.json", routes=routes, params=params, presets=presets,
                 states={"initial": "Normal", "states": states},
                 sources=[macro_source({"rules": 0.0, "spin": 0.0}), random_source("bar", "music.bar", 1011),
                          random_source("bar2", "music.bar", 1013), random_source("phrase", "music.phrase", 1012),
                          random_source("turn", "music.bar", 1014),
                          {"kind": "lfo", "name": "drift", "settings": {"shape": "saw"}}], end=226.3)
    pr["parameters"]["sources/drift/rate"] = 1.0 / 226.3
    pr["parameters"]["sources/drift/phase"] = 0.0
    return {"showcase": (pr, sc)}


# ---- validation (the interpreter's limits, docs/sdf.md) ----------------------------------------------

UNARY = {"translate", "rotate", "scale", "twist", "bend", "repeat", "polarRepeat", "mirror", "fold", "recurse",
         "displaceNoise", "displaceVoronoi", "displaceWave", "displaceField"}


def check_tree(root, label):
    count = 0
    names = set()

    def walk(n, depth, unary):
        nonlocal count
        count += 1
        if "name" in n:
            assert n["name"] not in names, f"{label}: duplicate node name {n['name']}"
            names.add(n["name"])
        u = unary + (1 if n["kind"] in UNARY else 0)
        worst = (depth, u)
        for c in n.get("children", []):
            d2, u2 = walk(c, depth + 1, u)
            worst = (max(worst[0], d2), max(worst[1], u2))
        return worst

    depth, unary = walk(root, 1, 0)
    ok = count <= 96 and depth <= 12 and unary <= 8
    print(f"  {label}: {count} nodes, depth {depth}, unary nesting {unary}" + ("" if ok else "  <-- OVER THE LIMIT"))
    assert ok, label


PRESETS = {
    "hall": preset_hall,
    "cathedral": preset_cathedral,
    "folding": preset_folding,
    "radial": preset_radial,
    "explosion": preset_explosion,
    "showcase": preset_showcase,
}


def write(files):
    for stem, (pr, sc) in files.items():
        check_tree(sc["nodes"][0]["sdf"]["tree"]["root"], stem)
        (OUT / f"{stem}.scene.json").write_text(json.dumps(sc, indent=1) + "\n")
        (OUT / f"{stem}.json").write_text(json.dumps(pr, indent=1) + "\n")
        print("wrote", OUT / f"{stem}.json")


if __name__ == "__main__":
    keys = sys.argv[1:] or list(PRESETS)
    for k in keys:
        write(PRESETS[k]())
