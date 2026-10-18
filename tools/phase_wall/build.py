#!/usr/bin/env python3
"""PHASE WALL: the generator (docs/prototypes/phase-wall/02-plan.md).

    python3 tools/phase_wall/build.py                         # writes examples/experiments/phase-wall/
    python3 tools/phase_wall/build.py --variant --out <v.json> [--wav <w>] [--camera x,y,z:tx,ty,tz]
                                      [--set path=json] [--track path=v@t,...] [--autonomous]

A closed hall whose four walls are one kinetic installation: about eight thousand metal blades, each swung about its
vertical hinge by its own phase oscillator. One Kuramoto-Sakaguchi network (ADR-1450) runs as a single strip, the
hall's perimeter long and its height tall, wrapped so that its ends meet. Each wall reads its own stretch of that strip
through a pair of grid readers whose frames take the wall's unrolled coordinate onto the grid, so islands, fronts and
defects cross the corners and circle the room.

Conventions: metres, +Y up, the hall centred on the origin with its long axis on z; colours linear (sRGB references
converted by hexrgb). Walls are walked anticlockwise from the front-left corner: west, north (the end wall), east,
south (the entrance wall).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.generative_rd import common  # noqa: E402
from tools.generative_rd.common import R, hexrgb  # noqa: E402

# The R&D's osc-flaps example is already "Phase Wall" in the index (proto/generative-rd); this scene is listed as
# "Phase Hall" so the two never share a name. The coordinator settles the final name at the merge.
SID, TITLE = "phase-wall", "Phase Hall"
OUT = os.path.join(REPO, "examples", "experiments", SID)

# ================================================================================================ the hall
L, W, H = 128.0, 48.0, 30.0       # length (z), width (x), height
BAY = 16.0                        # bay piers on the long walls
PIER = 3.2                        # corner piers (square)
BASE = 0.6                        # the blades start this high (a dark plinth below)
PX, PY = 0.75, 1.6                # blade pitch: across, up
BLADE = (0.745, 1.585, 0.04)      # blade size: width, height, thickness (closed, the screen has no see-through gaps)
CELL = float(os.environ.get("PW_CELL", "1.6"))                        # network cell (m): with radius 3, a few large 2D islands per wall
SWING = 1.0                       # blade swing (rad) at |sin theta| = 1


def hall_walls():
    """(name, start (x, z), direction (dx, 0, dz), length, unrolled start u0); inward normal = (-dz, 0, dx)."""
    a, b = (-W / 2, L / 2), (-W / 2, -L / 2)
    c, d = (W / 2, -L / 2), (W / 2, L / 2)
    out, u = [], 0.0
    for name, s, e in (("west", a, b), ("north", b, c), ("east", c, d), ("south", d, a)):
        dx, dz = e[0] - s[0], e[1] - s[1]
        n = math.hypot(dx, dz)
        out.append((name, (s[0], s[1]), (dx / n, 0.0, dz / n), n, u))
        u += n
    return out, u


WALLS, PERIM = hall_walls()


def yaw_of(d):
    return math.degrees(math.atan2(-d[2], d[0]))


def reader_frame(st, d, u0):
    """A grid reader's frame for one wall: local x along the wall (= unrolled u - P/2), local z up (= y - H/2).
    Rotation Ry(psi) Rx(-90) (euler XYZ degrees, glm's R = Rz Ry Rx); verified at every corner (02-plan.md §0)."""
    pos = [st[0] - d[0] * (u0 - PERIM / 2), H / 2, st[1] - d[2] * (u0 - PERIM / 2)]
    return pos, [-90.0, yaw_of(d), 0.0]


def unrolled(wall, t, y):
    """The network's own (x, z) for a point `t` metres along `wall` at height y (for fields in the grid's frame)."""
    for name, st, d, n, u0 in WALLS:
        if name == wall:
            return (u0 + t - PERIM / 2, 0.0, y - H / 2)
    raise KeyError(wall)


# ================================================================================================ palettes (sRGB refs)
# Role-based (02-plan.md §6). Every colour in the hall is a role of the active palette; the project palette (ADR-1043)
# blends states in OKLab and its selector (ADR-1420) chooses, locks (CC 9 / CC 10) and advances them, with the
# ADR-1421 change fallback when no phrase arrives. Roles:
#   environment  the air: haze, ceiling, the darks
#   back         the backplane: the colour between and behind the blades, poured through as they turn edge-on
#   trough       a blade face turned away (sin theta = -1): dark, but coloured
#   mid          the face between (sin theta = 0): the palette's body colour
#   crest        the face at its crest (sin theta = +1): the bright
#   wash         the light: the key and the four wall washes
#   front        strain: fronts and defect seams
#   peak         the hottest defect cores
#   trim         pier strips (the light the bands stand for) and the brass inlay
#   floor        the floor's lacquer: what it reflects takes this tint
ROLES = ("environment", "back", "trough", "mid", "crest", "wash", "front", "peak", "trim", "floor")
PALETTES = [
    # name           environment back      trough     mid        crest      wash       front      peak       trim       floor
    ("Bronze Tide",   "#061315", "#0f5560", "#0e2a2c", "#7a3f1e", "#f0a860", "#ffd9b0", "#4ff0dc", "#ffb35c", "#ffcf9a", "#0f2a2e"),
    ("Ember",         "#140605", "#7a1a0a", "#2e0c08", "#9a2c10", "#ff9a40", "#ffc890", "#ffd23c", "#fff4d0", "#ff9a50", "#2c0d07"),
    ("Gilt Orchid",   "#0d0513", "#561070", "#24092e", "#8a2a7a", "#ffd36a", "#fff0d8", "#ff3ab0", "#ffe0f4", "#ffcf7a", "#200a2a"),
    ("Coral Lagoon",  "#041211", "#087070", "#0c2626", "#c0484a", "#ffb48c", "#fff0e0", "#40ffe0", "#ffffff", "#ffb0a0", "#0b2c2c"),
    ("Verdigris",     "#041310", "#0c6a50", "#0c2a1e", "#2f8a60", "#a6f0b8", "#eaffdc", "#ff7a5c", "#fff0a0", "#9fffd0", "#0a2a1e"),
    ("Aurora",        "#030c12", "#0a5a48", "#0d1630", "#4430a0", "#60ffb0", "#e0f0ff", "#ff60c0", "#fff6a0", "#80ffc0", "#081c28"),
    ("Cobalt Ice",    "#040a1a", "#1032a0", "#0a1840", "#2050b8", "#a0d8ff", "#d8ecff", "#6af0ff", "#ffffff", "#8ac8ff", "#0a1840"),
    ("Nocturne",      "#0a0616", "#40188a", "#1c0e34", "#5a2aa0", "#d0a8ff", "#d8c8ff", "#ff5ad8", "#ffd27a", "#b48cff", "#180e30"),
]
# the automatic journey (ADR-1420 order): round the colour wheel by neighbours -- warm to cool and back
JOURNEY = list(range(len(PALETTES)))
START = int(os.environ.get("PW_PALETTE", "0"))
CHANGE = {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"], "fast": 3.0, "slow": 24.0,
          "threshold": 0.06, "rearm": 0.03, "primaryTimeout": 30.0, "maxHold": 90.0}
P = dict(zip(ROLES, PALETTES[START][1:]))
TUNE = {"rough": 0.36, "key": 1.0, "wash": 2.6, "back": 0.7, "bands": 2.0, "metallic": 0.55, "bounce": 2.5}


def C(key, s=1.0):
    return hexrgb(P[key], s)


def bind(s, role, target, gain=1.0):
    """Record a palette binding: `target` (a colour parameter) is the active palette's `role` times `gain`."""
    s.binds.append({"role": role, "target": target, "gain": gain} if gain != 1.0 else {"role": role, "target": target})


def palette_block(s):
    states = [{"name": row[0], "colors": {r: hexrgb(h) for r, h in zip(ROLES, row[1:])}, "scalars": {}}
              for row in PALETTES]
    return {"states": states, "bindings": s.binds, "position": float(START), "saturation": 1.0, "value": 1.0,
            "selector": {"select": START, "transition": 6.0, "lock": False, "hold": 40.0,
                         "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": JOURNEY,
                                     "change": CHANGE}}}


def op_path(g, label, field="constant"):
    return "material/%s/op/%d/%s/%s" % (g.name, g[label], g.ops[g[label] - 1]["kind"], field)


# ================================================================================================ the network
def network(s):
    """The strip: PERIM x H, wrapped, islands regime; the coupling breathes on the timeline (the R&D's 96 s arc)."""
    g = {"name": "net", "mode": "oscillator", "wrap": "wrap",
         "resolution": [int(round(PERIM / CELL)), 1, int(round(H / CELL))],
         "boundsMin": [-PERIM / 2, -40.0, -H / 2], "boundsMax": [PERIM / 2, 40.0, H / 2],
         "frequency": 0.18, "simRate": 60.0, "maxSubSteps": 4, "seed": 3, "persistence": 0.6, "links": 0,
         "linkRange": 48.0, "checkpointInterval": 5.0, "coupling": 4.0, "phaseLag": 0.3, "couplingRadius": int(os.environ.get("PW_RADIUS", "3")),
         "spread": 0.17, "seedAmount": 1.0, "noise": 0.02}
    s.grid(**g)
    # a periodic frequency tilt (one sine round the hall, no seam at the wrap): unison still rolls as a slow wave
    s.field("tilt", {"kind": "wave", "waveGeometry": "planar", "waveShape": "sine", "axis": [1.0, 0.0, 0.0],
                     "point": [0.0, 0.0, 0.0], "wavelength": PERIM, "waveSpeed": 0.0, "waveWidth": 0.0,
                     "amplitude": 1.0, "strength": float(os.environ.get("PW_TILT", "0.006"))})
    g["frequencyField"] = "tilt"
    # the breath (96 s): disorder -> islands (K ~4.5, held 16-34 s) -> unison (K 12, 44-56 s) -> islands -> disorder.
    # On the 1.2 m lattice K 10 already locks the room, so the islands band is held on purpose
    keys = [(0, 1.5), (16, 4.0), (34, 5.5), (44, 12.0), (56, 12.0), (66, 5.5), (80, 4.0), (96, 1.5)]
    s.track("grid/net/coupling", [{"time": float(t), "value": [k], "interp": "smooth"} for t, k in keys], loop=96.0)
    return g


# ================================================================================================ the blades
ACT = tuple(float(v) for v in os.environ.get("PW_ACT", "0.2,0.5").split(","))      # activity -> front glow (the coarse lattice's strain is lower than the R&D's)
ACT_PEAK = tuple(float(v) for v in os.environ.get("PW_PEAK", "0.5,1.4").split(","))  # activity -> the defect core


def blade_program(wall):
    """One wall's blades.
    - Tone: the phase, over the full sin theta range, through a three-stop ramp (trough, mid, crest): dark, body, bright.
    - The blade as a blade: a shading profile across its width (a louvre is slightly dished, one edge catching the
      wash and the other falling off), a lift toward its top (the washes come from above), and soft shadow lines at
      its four edges, all in the blade's own frame (localPosition). With the bevel's edge highlights, each blade
      reads as a turning plate, not a flat pixel.
    - Light: the wall washes and the reflection bands; strain lights whole blades `front`, the cores `peak`."""
    g = common.Prog("blade_" + wall)
    bw, bh = BLADE[0], BLADE[1]
    g.op(None, "field", 0, field="ph_" + wall)                      # r0 = sin theta
    g.op(None, "remap", 0, a=0, k=[-1.0, 1.0, 0.0, 1.0], f=1.0)    # -> 0..1
    g.op(None, "smoothstep", 0, a=0, k=[0.08, 0.92, 0, 0])
    g.op("tone", "ramp", 1, a=0, k=C("trough") + [1], k2=C("mid") + [1], k3=C("crest") + [1])   # r1 = colour
    g.op(None, "input", 2, inp="localPosition")                     # r2 = blade frame
    g.op(None, "gradient", 3, a=2, k=[1, 0, 0, 0.5], f=1.0 / bw)    # r3 = u across (0..1)
    g.op(None, "gradient", 4, a=2, k=[-1, 0, 0, 0.5], f=1.0 / bw)   # r4 = 1 - u
    g.op(None, "smoothstep", 5, a=3, k=[0.0, 0.16, 0, 0])
    g.op(None, "smoothstep", 6, a=4, k=[0.0, 0.16, 0, 0])
    g.op(None, "multiply", 5, a=5, b=6)                             # r5 = side shadow lines
    g.op(None, "gradient", 6, a=2, k=[0, 1, 0, 0.5], f=1.0 / bh)    # r6 = v up (0..1)
    g.op(None, "gradient", 7, a=2, k=[0, -1, 0, 0.5], f=1.0 / bh)   # r7 = 1 - v
    g.op(None, "smoothstep", 7, a=7, k=[0.0, 0.07, 0, 0])
    g.op(None, "multiply", 5, a=5, b=7)                             # x top/bottom shadow lines
    g.op(None, "remap", 5, a=5, k=[0.0, 1.0, 0.3, 1.0], f=1.0)      # lines sink to 30%, never black
    g.op(None, "remap", 6, a=6, k=[0.0, 1.0, 0.72, 1.12], f=1.0)    # lift toward the top
    g.op(None, "multiply", 5, a=5, b=6)
    g.op(None, "remap", 3, a=3, k=[0.0, 1.0, 1.15, 0.7], f=1.0)     # the dish: one edge bright, the other falling off
    g.op(None, "multiply", 5, a=5, b=3)
    g.op(None, "multiply", 1, a=1, b=5)                             # r1 = base
    g.op(None, "input", 4, inp="instanceEmissive")                  # r4 = activity, one value per blade (effector)
    g.op(None, "smoothstep", 2, a=4, k=[ACT[0], ACT[1], 0, 0])
    g.op(None, "multiply", 2, a=2, b=2)                             # squared: the seam's core, not its shoulders
    g.op("front", "constant", 6, k=C("front", 1.4) + [1])
    g.op(None, "multiply", 2, a=2, b=6)                             # r2 = front glow
    g.op("frontGain", "constant", 6, k=[1.0, 1.0, 1.0, 1.0])        # the breath's glow gain (timeline, below)
    g.op(None, "multiply", 2, a=2, b=6)
    g.op(None, "smoothstep", 4, a=4, k=[ACT_PEAK[0], ACT_PEAK[1], 0, 0])
    g.op("peak", "constant", 6, k=C("peak", 2.5) + [1])
    g.op(None, "multiply", 4, a=4, b=6)
    g.op(None, "add", 2, a=2, b=4)                                  # r2 = emission
    g.op("rough", "constant", 6, k=[TUNE["rough"]] * 3 + [1])
    g.op(None, "roughnessFilter", 6, a=6, f=1.0)
    g.base, g.emit, g.rough = 1, 2, 6
    return g


def walls(s):
    for name, st, d, n, u0 in WALLS:
        pos, rot = reader_frame(st, d, u0)
        for ch, fn in ((1, "ph_"), (3, "act_")):   # sin theta; activity (strain + kicks)
            s.field(fn + name, {"kind": "grid", "reference": "net", "channel": ch, "strength": 1.0,
                                "rotation": rot}, position=pos)
        g = blade_program(name)
        s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
        bind(s, "trough", op_path(g, "tone", "constant"))
        bind(s, "mid", op_path(g, "tone", "constant2"))
        bind(s, "crest", op_path(g, "tone", "constant3"))
        bind(s, "front", op_path(g, "front"), 1.4)
        bind(s, "peak", op_path(g, "peak"), 2.5)
        # disorder strains the whole wall, so the seams only read once there are islands to sit between: the front
        # glow follows the breath (an eighth of it in disorder, all of it from the islands on)
        gk = [(0, 0.12), (10, 0.12), (18, 1.0), (78, 1.0), (86, 0.12), (96, 0.12)]
        s.track(op_path(g, "frontGain"), [{"time": float(tt), "value": [v, v, v, 1.0], "interp": "smooth"}
                                           for tt, v in gk], loop=96.0)
        inset = PIER / 2 + 0.2
        nx = int((n - 2 * inset) // PX)
        ny = int((H - BASE - 0.4) // PY)
        cx, cz = st[0] + d[0] * n / 2, st[1] + d[2] * n / 2
        nrm = (-d[2], 0.0, d[0])
        yaw = yaw_of(d)
        s.proc("blades_" + name, {"kind": "box", "size": list(BLADE), "bevel": 0.018, "bevelSegments": 1},
               distribution={"kind": "grid", "gridCount": [nx, ny, 1], "gridSpacing": [PX, PY, 1.0]},
               position=(cx, BASE + ny * PY / 2, cz), rotation=(0.0, yaw, 0.0),
               material={"program": g.name, "baseColor": [1, 1, 1], "roughness": TUNE["rough"],
                         "metallic": TUNE["metallic"]},
               extra={"effectors": [{"field": "ph_" + name, "op": "rotation", "blend": "add", "strength": SWING,
                                     "axis": [0, 1, 0]},
                                    # strain lights whole blades (a blade is one oscillator): crisp, never a cloud
                                    {"field": "act_" + name, "op": "emission", "blend": "replace", "strength": 1.0}],
                      # hinge play: no two blades hang exactly alike (Zimoun, Kahn)
                      "variation": {"seed": 11, "randomRotation": [0.0, 0.05, 0.0]},
                      "materialVariation": {"valueRandom": 0.08}})
        # the backplane, set back behind the blades: the colour that pours through as they turn edge-on
        s.proc("back_" + name, {"kind": "box", "size": [n, H + 2.0, 0.4]},
               position=(cx - nrm[0] * 0.9, H / 2, cz - nrm[2] * 0.9), rotation=(0.0, yaw, 0.0),
               material={"baseColor": [0.0, 0.0, 0.0], "roughness": 0.9, "metallic": 0.0,
                         "emissiveColor": C("back", TUNE["back"]), "emissiveIntensity": 1.0})
        bind(s, "back", "procedural/back_%s/material/emissiveColor" % name, TUNE["back"])


# ================================================================================================ the room
REACH = 16.0  # how far out from a wall the floor carries its colour
CEIL_GLOW = 3.0     # the ceiling's panels glow faintly in the air's colour: the dark coffers read against them
FLOOR_ALBEDO = 2.6   # the lacquer's own (unlit) colour: dark, in the palette's floor role


def floor_program():
    """The floor: dark lacquer that carries the walls' colour, since the engine has no planar reflection. Near each
    wall the floor shows that wall's own tone -- its phase sampled through the wall's reader at the floor (the reader's
    frame maps a floor point to the wall's bottom row at the same place along it), through the same three-stop ramp --
    fading out over REACH metres. As the islands turn, their colour moves across the floor beneath them."""
    g = common.Prog("floorLacquer")
    g.op(None, "input", 0, inp="worldPosition")
    for i, (name, st, d, n, u0) in enumerate(WALLS):
        nrm = (-d[2], 0.0, d[0])                                     # inward
        half = abs(st[0]) if abs(nrm[0]) > 0.5 else abs(st[1])
        # proximity: 1 at the wall, 0 at REACH (gradient along the outward normal), squared
        g.op(None, "gradient", 2, a=0, k=[-nrm[0], 0.0, -nrm[2], 1.0 - half / REACH], f=1.0 / REACH)
        g.op(None, "power", 2, a=2, f=2.0)
        g.op(None, "field", 3, field="ph_" + name)
        g.op(None, "remap", 3, a=3, k=[-1.0, 1.0, 0.0, 1.0], f=1.0)
        g.op(None, "smoothstep", 3, a=3, k=[0.08, 0.92, 0, 0])
        g.op("tone_" + name, "ramp", 3, a=3, k=C("trough") + [1], k2=C("mid") + [1], k3=C("crest") + [1])
        g.op(None, "multiply", 3, a=3, b=2)
        g.op(None, "add", 1, a=1, b=3)                               # r1 accumulates (registers start at zero)
    g.op("gain", "constant", 2, k=[0.35, 0.35, 0.35, 1])
    g.op(None, "multiply", 1, a=1, b=2)                              # r1 = emission (the walls' colour)
    g.op("lacquer", "constant", 4, k=C("floor", FLOOR_ALBEDO) + [1])
    # the stone is mottled (low-frequency fbm in the floor plane), so even its darkest stretch has structure
    g.op(None, "noise", 2, a=0, k=[0.0, 0.0, 0.0, 0.0], f=0.09)
    g.op(None, "remap", 2, a=2, k=[0.2, 0.8, 0.45, 1.6], f=1.0)
    g.op(None, "multiply", 4, a=4, b=2)
    g.base, g.emit = 4, 1
    return g


def room(s):
    nb = int(round(L / BAY))
    stone = {"baseColor": C("floor", 0.6), "roughness": 0.5, "metallic": 0.0}
    piers = []
    for i, (cx, cz) in enumerate(((-W / 2, L / 2), (-W / 2, -L / 2), (W / 2, -L / 2), (W / 2, L / 2))):
        s.proc("pier%d" % i, {"kind": "box", "size": [PIER, H + 2.0, PIER]}, position=(cx, H / 2, cz), material=stone)
        piers.append("pier%d" % i)
    for side, x in (("w", -W / 2), ("e", W / 2)):
        inward = 1.0 if side == "w" else -1.0
        s.proc("bays_" + side, {"kind": "box", "size": [1.6, H + 2.0, 1.4]},
               distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1.0, 1.0, L / nb]},
               position=(x + inward * 0.5, H / 2, 0.0), material=stone)
        piers.append("bays_" + side)
        s.proc("strips_" + side, {"kind": "box", "size": [0.1, H - 4.0, 0.2]},
               distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1.0, 1.0, L / nb]},
               position=(x + inward * 1.32, H / 2, 0.0),
               material={"baseColor": [0, 0, 0], "roughness": 0.5, "metallic": 0.0,
                         "emissiveColor": C("trim"), "emissiveIntensity": 1.6})
        bind(s, "trim", "procedural/strips_%s/material/emissiveColor" % side)
    for name, st, d, n, u0 in WALLS:  # the plinth under the blades
        cx, cz = st[0] + d[0] * n / 2, st[1] + d[2] * n / 2
        nrm = (-d[2], 0.0, d[0])
        s.proc("plinth_" + name, {"kind": "box", "size": [n, BASE, 0.8]},
               position=(cx + nrm[0] * 0.1, BASE / 2, cz + nrm[2] * 0.1), rotation=(0.0, yaw_of(d), 0.0),
               material=stone)
        piers.append("plinth_" + name)
    for pn in piers:
        bind(s, "floor", "procedural/%s/material/baseColor" % pn, 0.6)
    # the ceiling: deep coffers, mostly in shadow, in the air's colour
    dark = {"baseColor": C("environment", 3.0), "roughness": 0.85, "metallic": 0.0}
    s.proc("beams_x", {"kind": "box", "size": [W + 4, 2.4, 0.8]},
           distribution={"kind": "grid", "gridCount": [1, 1, nb * 2 + 1], "gridSpacing": [1, 1, L / (nb * 2)]},
           position=(0.0, H - 0.2, 0.0), material=dark)
    s.proc("beams_z", {"kind": "box", "size": [0.8, 2.4, L + 4]},
           distribution={"kind": "grid", "gridCount": [5, 1, 1], "gridSpacing": [W / 4, 1, 1]},
           position=(0.0, H - 0.2, 0.0), material=dark)
    s.proc("ceiling", {"kind": "box", "size": [W + 4, 1.0, L + 4]}, position=(0.0, H + 1.5, 0.0),
           material={"baseColor": C("environment", 0.5), "roughness": 0.9, "metallic": 0.0,
                     "emissiveColor": C("environment", CEIL_GLOW), "emissiveIntensity": 1.0})
    for bn in ("beams_x", "beams_z"):
        bind(s, "environment", "procedural/%s/material/baseColor" % bn, 3.0)
    bind(s, "environment", "procedural/ceiling/material/emissiveColor", CEIL_GLOW)
    # the floor (lacquer carrying the walls' colour) and its inlay
    fp = floor_program()
    s.program(fp.doc(base=fp.base, emission=fp.emit))
    for name, *_ in WALLS:
        bind(s, "trough", op_path(fp, "tone_" + name, "constant"))
        bind(s, "mid", op_path(fp, "tone_" + name, "constant2"))
        bind(s, "crest", op_path(fp, "tone_" + name, "constant3"))
    bind(s, "floor", op_path(fp, "lacquer"), FLOOR_ALBEDO)
    s.proc("floor", {"kind": "box", "size": [W + 8, 1.0, L + 8]}, position=(0.0, -0.5, 0.0),
           material={"program": fp.name, "baseColor": C("floor"), "roughness": 0.5, "metallic": 0.0,
                     # unlit: no light reaches it, so no sheen of the bands or the washes; its colour is the
                     # lacquer plus the walls' colour pooled beneath them (the program)
                     "unlit": True})
    brass = {"baseColor": C("trim", 0.15), "roughness": 0.4, "metallic": 1.0}
    s.proc("inlay_z", {"kind": "box", "size": [0.12, 0.02, L]},
           distribution={"kind": "grid", "gridCount": [5, 1, 1], "gridSpacing": [W / 6, 1, 1]},
           position=(0.0, 0.005, 0.0), material=brass)
    s.proc("inlay_x", {"kind": "box", "size": [W, 0.02, 0.12]},
           distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1, 1, L / nb]},
           position=(0.0, 0.005, 0.0), material=brass)
    for nn in ("inlay_z", "inlay_x"):
        bind(s, "trim", "procedural/%s/material/baseColor" % nn, 0.15)


def light(s):
    env = C("environment")
    s.environment = {
        "intensity": 0.05, "background": env, "fogColor": env, "volumeDensity": 0.0,
        "fogSky": 0.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0, "shadowRange": 160.0,
        "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": env, "horizonColor": env, "groundColor": env, "haze": 1.0,
                "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
        # the flash: vertical great circles (the opposite piers' strips, in direction space). A blade turning
        # about y sweeps its reflection across them; 0.12 wide on 0.36-rough metal passes the strobe gate
        "bands": {"phase": 0.0, "rotation": 0.0, "gain": 1.0,
                  "softbox": {"intensity": 0.0, "azimuth": 0.6, "elevation": 0.7, "falloff": 2.6, "skyFill": 0.0},
                  "strips": [{"axis": [math.cos(k * math.pi / 4), 0.0, math.sin(k * math.pi / 4)], "offset": 0.0,
                              "width": 0.12, "intensity": TUNE["bands"], "segments": 0, "warmth": 0.7 - 0.15 * k,
                              "rate": 0.0} for k in range(4)]},
    }
    for k in ("env/sky/zenithColor", "env/sky/horizonColor", "env/sky/groundColor", "scene/fogColor"):
        bind(s, "environment", k)
    # the key: high, raking the floor and the piers through an implied clerestory
    s.light("key", "directional", direction=[0.45, -0.8, -0.4], color=C("wash"), intensity=TUNE["key"],
            # no shadows: the key is diffuse-only fill for the floor and piers, and 8k bevelled blades in a shadow map
            # cost 6 ms live for shading nobody could see
            castsShadow=False, contactShadow=False, softness=0.35, diffuseOnly=True)
    bind(s, "wash", "lights/key/color")
    # four washes, one per wall, from high in front of it (Rozin's lamps above the tiles): a wall is lit only by its
    # own wash (the opposite wall faces away; the side walls take it at grazing), so a blade's value reads its angle
    for name, st, d, n, u0 in WALLS:
        nrm = (-d[2], 0.0, d[0])
        el = 0.9   # steep: the floor mirrors a wash only when looking well down, out of every review framing
        s.light("wash_" + name, "directional", direction=[-nrm[0] * math.cos(el), -math.sin(el), -nrm[2] * math.cos(el)],
                color=C("wash"), intensity=TUNE["wash"], castsShadow=False, contactShadow=False,
                # diffuse only: four warm suns in a polished floor are a white sheen across every frame; the
                # blades' specular light is the bands' job
                diffuseOnly=True)
        bind(s, "wash", "lights/wash_%s/color" % name)
    # the bounce: the lit walls and the backplanes throw their colour up under the coffers and the beams' soffits
    s.light("bounce", "directional", direction=[0.15, 1.0, 0.1], color=C("back"), intensity=TUNE["bounce"],
            castsShadow=False, contactShadow=False, diffuseOnly=True)
    bind(s, "back", "lights/bounce/color")


# ================================================================================================ the music
# (02-plan.md §5) The music writes into the network, not into the image: onsets are phase kicks, held sound is
# coupling, a build is the lag. Every audio route's depth is Audio Influence (CC 4, half at rest).
KICK_FROM = ("north", W / 2, 9.0)     # the end wall's centre, low: a kick's ring runs round both corners
SNARE_FROM = ("east", L / 2, 20.0)    # the east wall's middle, high: a second, quicker source
MIDI_CC = [
    (1, "coupling", "COUPLING: pulls the hall toward unison"),
    (2, "complexity", "COMPLEXITY: widens the spread of natural frequencies (more islands, more seams)"),
    (3, "motion", "MOTION: the network's speed"),
    (4, "influence", "AUDIO INFLUENCE: how hard the music drives the network (half at rest)"),
    (5, "lag", "LAG: the Sakaguchi phase lag, rings toward spirals"),
    (6, "light", "LIGHT: the reflected strips and the backplanes"),
    (9, "palette", "PALETTE: choose one of the eight (a direct blend to it)"),
    (10, "paletteLock", "PALETTE LOCK: hold the chosen palette (no automatic changes)"),
]
MIDI_NOTES = [(36, "pace")]


def music(s, net):
    G = "grid/net/"
    for name, (wall, t_, y) in (("kickFront", KICK_FROM), ("snareFront", SNARE_FROM)):
        pt = list(unrolled(wall, t_, y))
        f = ({"kind": "onset", "onsetSource": "low", "audioSpeed": 14.0, "onsetWidth": 3.5, "onsetDecay": 0.9}
             if name == "kickFront" else
             {"kind": "onset", "onsetSource": "mid", "audioSpeed": 22.0, "onsetWidth": 2.0, "onsetDecay": 1.6})
        f.update({"point": pt, "waveGeometry": "spherical"})
        s.field(name, f)
    s.field("kicks", {"kind": "compound", "combine": "add", "children": ["kickFront", "snareFront"]})
    net["injectField"], net["injectRate"] = "kicks", 2.0
    # MIDI note 36 holds a pacemaker on the end wall: a cell tuned faster sends target waves while the note is held
    s.field("pace", {"kind": "radial", "radius": 6.0, "strength": 0.0}, position=unrolled("north", W / 2 - 10.0, 18.0))
    s.field("tune", {"kind": "compound", "combine": "add", "children": ["tilt", "pace"]})
    net["frequencyField"] = "tune"
    A = common.audio
    s.route(A("response.sustain", G + "coupling", 6.0, attackMs=3000, decayMs=6000),
            A("response.bass", G + "frequency", 0.25, attackMs=300, decayMs=1500),
            A("response.hat", G + "noise", 0.12, attackMs=40, decayMs=400),
            A("response.intensity", G + "phaseLag", 0.35, attackMs=6000, decayMs=12000),
            # loud passages turn the reflection rig: even a locked hall's flashes travel round the room
            A("response.level", "scene/bands/rotation", 0.15, integrate=True))
    common.instrument(s, MIDI_CC, MIDI_NOTES)
    CC = common.CC
    s.route(R("control.coupling", G + "coupling", 12.0, **CC),
            R("control.complexity", G + "spread", 0.2, **CC),
            R("control.motion", G + "speed", 1.5, **CC),
            R("control.lag", G + "phaseLag", 1.0, **CC),
            R("control.light", "scene/bands/gain", 1.0, op="multiply", gain=2.0, offset=1.0, **CC),
            R("control.palette", "palette/select", float(len(PALETTES) - 1), **CC),
            R("control.paletteLock", "palette/lock", 1.0, **CC),
            R("control.pace", "field/pace/strength", 0.35, attackMs=80, decayMs=2500))
    for name, *_ in WALLS:
        s.route(R("control.light", "procedural/back_%s/material/emissive" % name, 1.0, op="multiply", gain=1.5,
                  offset=1.0, **CC))


# ================================================================================================ camera
CAMERAS = {  # review framings: oblique and off-axis (the walls are the hero; never a dead-centre corridor)
    "nave": ("-15,6,42", "17,13,-30"),
    "east": ("-12,9,-2", "24,14,-26"),
    "corner": ("13,5,-38", "-24,19,-64"),
    "end": ("9,8,-22", "-8,14,-64"),
    "up": ("2,3,8", "22,24,-22"),
}


# The promenade (02-plan.md §4, coordinator's note 3): a visitor's walk round the hall, 120 s a loop, every framing
# oblique or off-axis. It never looks straight down the axis for long; the end wall is met close and at an angle,
# as a wall, not a vanishing point. (position, target) every 15 s, smooth; the loop closes on its first key.
PROMENADE = [
    ((-15.0, 6.0, 42.0), (24.0, 13.0, 4.0)),      # the nave, oblique: the east wall at 45 degrees, receding
    ((-8.0, 8.0, 22.0), (24.0, 14.0, 0.0)),       # turning to the east wall
    ((4.0, 10.0, -4.0), (24.0, 16.0, -34.0)),     # along the east wall toward the north-east corner
    ((12.0, 6.0, -30.0), (-24.0, 18.0, -56.0)),   # across to the north-west corner, low and looking up
    ((6.0, 4.0, -46.0), (-10.0, 14.0, -64.0)),    # the end wall, close and at an angle
    ((-12.0, 9.0, -36.0), (-24.0, 15.0, -8.0)),   # the west wall, near-frontal
    ((-6.0, 12.0, -6.0), (-24.0, 17.0, 30.0)),    # the west wall receding toward the entrance
    ((8.0, 7.0, 24.0), (-20.0, 12.0, 56.0)),      # the south-west corner and the entrance wall
]
LOOP = 120.0


def camera(s):
    n = len(PROMENADE)
    kp = [{"time": LOOP * i / n, "value": list(PROMENADE[i % n][0]), "interp": "smooth"} for i in range(n + 1)]
    kt = [{"time": LOOP * i / n, "value": list(PROMENADE[i % n][1]), "interp": "smooth"} for i in range(n + 1)]
    s.track("camera/position", kp, loop=LOOP)
    s.track("camera/target", kt, loop=LOOP)
    s.camera = {"mode": 1, "position": list(PROMENADE[0][0]), "target": list(PROMENADE[0][1]), "fov": 40.0,
                "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": 24.0, "post/bloom/intensity": 0.28, "post/output/vignette": 0.3,
               "post/grade/contrast": 1.08, "post/look/atmospheric": 0.12, "post/look/atmosphericDistance": 140.0,
               "post/look/atmosphericTint": C("environment", 2.0)})
    bind(s, "environment", "post/look/atmosphericTint", 2.0)


# ================================================================================================ build
def build(autonomous=False):
    s = common.Scene(SID, TITLE, {"category": "experiment", "experiment": "A coupled oscillators",
                                  "thesis": "A hall whose four walls are one kinetic installation: eight thousand "
                                            "metal blades, each swung by its own oscillator, falling into step and "
                                            "breaking apart round the room."})
    s.binds = []
    net = network(s)
    walls(s)
    room(s)
    light(s)
    camera(s)
    if autonomous:
        s.response = dict(common.RESPONSE)
    else:
        music(s, net)
    s.palette = palette_block(s)
    return s


def register(s):
    """The Examples > Live > Experiments entry (live: the project carries sonic.live and no audio file)."""
    cc = common.describe_ccs(MIDI_CC, "note 36 holds a pacemaker on the end wall")
    text = ("Play live audio (any mix) and, optionally, a MIDI controller: " + cc + ". " + s.design["thesis"] +
            " Held sound pulls the hall into unison, kicks launch fronts from the end wall that run round both "
            "corners, a build turns the lag; eight palettes take turns at phrase boundaries (or on a big change in the "
            "music when there is no tempo). See docs/prototypes/phase-wall/.")
    common.index_entry(s.title, "experiments/%s/%s.json" % (SID, SID), text, live=True)


def write_variant(pp, out, wav=None, cam=None, sets=(), tracks=(), palette=None):
    """A review project. `palette` (an index) selects and locks one state: the selector's values live in the palette
    block, since its parameters register after a project's parameter values are read (Resonant Silk's note)."""
    if cam and cam in CAMERAS:
        cam = "%s:%s" % CAMERAS[cam]
    params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in sets}
    out = common.variant(pp, out, wav, cam, params, list(tracks))
    if palette is not None:
        with open(out) as f:
            doc = json.load(f)
        doc["palette"]["selector"].update({"select": int(palette), "lock": True})
        doc["palette"]["position"] = float(palette)
        with open(out, "w") as f:
            json.dump(doc, f, indent=1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--wav")
    ap.add_argument("--camera")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--track", action="append", default=[])
    ap.add_argument("--palette", type=int)
    ap.add_argument("--autonomous", action="store_true", help="no audio fields or routes (review harness)")
    a = ap.parse_args()
    s = build(a.autonomous)
    pp, sp = s.write(OUT)
    nf = sum(1 for n in s.nodes if n["kind"] == "field")
    blades = sum(n["procedural"]["distribution"]["gridCount"][0] * n["procedural"]["distribution"]["gridCount"][1]
                 for n in s.nodes if n["name"].startswith("blades_"))
    assert nf <= 16, "%d GPU fields (16 at most)" % nf
    print(pp, "fields=%d blades=%d grid=%s" % (nf, blades, s.grids[0]["resolution"]))
    if not a.variant and not a.autonomous:
        register(s)
    if a.variant:
        print(write_variant(pp, a.out, a.wav, a.camera, a.set, a.track, a.palette))


if __name__ == "__main__":
    main()
