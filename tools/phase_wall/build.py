#!/usr/bin/env python3
"""PHASE HALL: the generator (docs/prototypes/phase-wall/; the sphere: docs/prototypes/generative-pass/01-plan.md).

    python3 tools/phase_wall/build.py                         # writes examples/experiments/phase-wall/
    python3 tools/phase_wall/build.py --variant --out <v.json> [--wav <w>] [--camera x,y,z:tx,ty,tz | <name>]
                                      [--set path=json] [--track path=v@t,...] [--palette n] [--autonomous]

An immense membrane of luminous vertical blades round the inside of a sphere. About eighteen thousand metal blades and chips, in
columns round the sphere, each swung about its own hinge (ADR-1500) by its own phase oscillator: one
Kuramoto-Sakaguchi network (ADR-1450), a strip once round the sphere from its lower to its upper fringe, read through
the sphere projection (ADR-1501), so islands, fronts and seams circle the viewer with no seam. Each blade is a
luminous band: a narrow hot core down its middle, softer light either side, dark edges, so every column reads as a
tall band of light between dark gaps. The columns end in a ragged fringe short of each pole (no rays converging on a
pole); beyond it, and behind every gap, the membrane itself glows in slow pools of colour.

The music travels through the surface: the bass history rolls out across the membrane as rings of displacement from
low in front, a broad swell undulates it with the held sound, each kick pushes a ring through it and launches a phase
front through the network, the snare a quicker front, hats run streaks of light up the blades.

Conventions: metres, +Y up, the sphere centred on the origin, colours linear (sRGB references converted by hexrgb).
Azimuth theta = atan2(z, x) (the projection's), latitude phi = asin(y / r); the performance camera looks toward -Z,
which is theta = -pi/2.

Proof switches (environment, review only): PW_R (radius), PW_COLS (columns), PW_PALETTE (the start palette).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.generative_rd import common  # noqa: E402
from tools.generative_rd.common import R, hexrgb  # noqa: E402

# The R&D's osc-flaps example is already "Phase Wall" in the index (proto/generative-rd); this scene is listed as
# "Phase Hall" so the two never share a name.
SID, TITLE = "phase-wall", "Phase Hall"
OUT = os.path.join(REPO, "examples", "experiments", SID)

# ================================================================================================ the sphere
RS = float(os.environ.get("PW_R", "150"))         # the blades' radius (m)
BACK = RS + 2.2                                   # the membrane behind them
NCOL = int(os.environ.get("PW_COLS", "288"))      # columns once round (3.27 m pitch on the equator)
BLADE = (2.2, 5.5, 0.05)                          # width (on the equator), height (along the meridian), thickness
ROW = 5.6                                         # row pitch along the meridian (m): the rows nearly touch, so a column
                                                  # reads as one tall band
DPHI = ROW / RS
SWING = 0.9                                       # blade swing (rad) at |sin theta| = 1
CHIP = (2.4, 4.6, 0.05)                           # the caps' chips
CHIP_PITCH = 3.5                                  # their spacing (m)
FRONT_THETA = -math.pi / 2                        # the azimuth the performance camera faces


def hash01(i, seed=1):
    x = (i * 374761393 + seed * 668265263) & 0xFFFFFFFF
    x = ((x ^ (x >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((x ^ (x >> 16)) & 0xFFFFFF) / float(0x1000000)


def fringe(theta, h):
    """Where each column starts to dissolve, at the top and the bottom (radians), and how far the dissolve runs: the
    columns thin out blade by blade and shrink into the membrane over 12-20 degrees, starting at a latitude that
    wanders round the sphere, so there is no edge and no circle (a circle there reads as a dome's rim or an oculus)."""
    top = (math.radians(49.0) + math.radians(4.0) * math.sin(3 * theta + 1.0) + math.radians(4.0) * math.sin(7 * theta + 2.3)
           + math.radians(4.0) * (h - 0.5))
    bot = (-math.radians(45.0) - math.radians(4.0) * math.sin(2 * theta + 0.5) - math.radians(4.0) * math.sin(5 * theta + 4.0)
           - math.radians(4.0) * (h - 0.5))
    run = math.radians(10.0) + math.radians(5.0) * h
    return top, bot, run


def quat_from_basis(x, y, z):
    """The unit quaternion (x, y, z, w) of the rotation whose columns are the basis vectors x, y, z."""
    m = np.array([x, y, z]).T
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        w, qx, qy, qz = 0.25 * s, (m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w, qx, qy, qz = (m[2, 1] - m[1, 2]) / s, 0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w, qx, qy, qz = (m[0, 2] - m[2, 0]) / s, (m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w, qx, qy, qz = (m[1, 0] - m[0, 1]) / s, (m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s
    return qx, qy, qz, w


def blade_points():
    """Every blade: [px, py, pz, qx, qy, qz, qw, sx, sy, sz]. Local +Y is the meridian (north), +Z faces the centre,
    +X is east; a column keeps its azimuth from fringe to fringe (the vertical bands), its blades narrowing with
    cos(latitude) so the gaps keep their proportion, and the last rows taper into the fringe."""
    pts = []
    k0 = int(math.floor(math.radians(-62.0) / DPHI))
    k1 = int(math.ceil(math.radians(70.0) / DPHI))
    for c in range(NCOL):
        th = -math.pi + (c + 0.5) * 2 * math.pi / NCOL
        top, bot, run = fringe(th, hash01(c, 3))
        for k in range(k0, k1 + 1):
            ph = k * DPHI
            beyond = max(ph - top, bot - ph, 0.0) / run          # 0 in the belt, 1 at the end of the dissolve
            if beyond >= 1.0 or hash01(c * 977 + k, 7) < beyond ** 0.8:
                continue
            cp, sp, ct, st = math.cos(ph), math.sin(ph), math.cos(th), math.sin(th)
            n = (cp * ct, sp, cp * st)                       # outward
            north = (-sp * ct, cp, -sp * st)
            east = (-st, 0.0, ct)
            inward = (-n[0], -n[1], -n[2])
            q = quat_from_basis(east, north, inward)
            taper = 1.0 - 0.6 * beyond                         # the dissolving blades shrink as they thin
            sx = cp * taper
            sy = taper
            pts.append([round(v, 4) for v in (RS * n[0], RS * n[1], RS * n[2], q[0], q[1], q[2], q[3], sx, sy, 1.0)])
    return pts


def capness(theta, phi, h):
    """0 in the belt, 1 where the columns have dissolved: the caps' chips fill in as the columns thin out."""
    top, bot, run = fringe(theta, h)
    return max(phi - top, bot - phi, 0.0) / run


def chip_points():
    """The caps, their own pattern (the meridians never run into a pole): over each pole a hexagonal lattice laid on the
    sphere by the Lambert equal-area projection (even spacing, no spirals, no rings), every chip's long axis along one
    direction across the cap (the world Z projected on the sphere: a parallel field, nowhere radial, its own
    singularities on the equator far outside the cap). The chips appear as the columns thin out (complementary odds),
    so there is no edge, and they glow as bright as the columns."""
    pts = []
    pitch_u, pitch_v = CHIP_PITCH, CHIP_PITCH * 1.6           # across and along the chips' axis
    reach = 2 * RS * math.sin(math.radians(50.0) / 2)         # Lambert radius of colatitude 50 degrees
    nu, nv = int(reach / pitch_u) + 2, int(reach / pitch_v) + 2
    i = 0
    for pole in (1.0, -1.0):
        for jv in range(-nv, nv + 1):
            for iu in range(-nu, nu + 1):
                i += 1
                u = (iu + 0.5 * (jv & 1)) * pitch_u
                v = jv * pitch_v
                r = math.hypot(u, v)
                if r > reach:
                    continue
                colat = 2 * math.asin(min(1.0, r / (2 * RS)))
                az = math.atan2(v, u)
                ph = pole * (math.pi / 2 - colat)
                th = az if pole > 0 else -az
                c = int((th + math.pi) / (2 * math.pi) * NCOL) % NCOL
                k = capness(th, ph, hash01(c, 3))
                if k <= 0.0 or hash01(i, 11) > min(1.0, k * 2.5) ** 0.5:   # ahead of the columns' thinning
                    continue
                cp, sp, ct, st = math.cos(ph), math.sin(ph), math.cos(th), math.sin(th)
                nrm = np.array((cp * ct, sp, cp * st))
                d = np.array((0.0, 0.0, 1.0)) - nrm * nrm[2]       # world Z projected on the tangent plane: along the
                # columns where the performance camera looks (-Z), so the cap continues them there
                d /= np.linalg.norm(d)
                inward = -nrm
                x = np.cross(d, inward)                              # so that (x, d, inward) is right-handed
                sc = 0.85 + 0.15 * hash01(i, 17)
                q = quat_from_basis(tuple(x), tuple(d), tuple(inward))
                pts.append([round(float(t), 4) for t in (RS * nrm[0], RS * nrm[1], RS * nrm[2], q[0], q[1], q[2], q[3],
                                                         sc, sc, 1.0)])
    return pts


# ================================================================================================ palettes (sRGB refs)
# Role-based. Every colour is a role of the active palette; the project palette (ADR-1043) blends states in OKLab and
# its selector (ADR-1420) chooses, locks (CC 9 / CC 10) and advances them, with the ADR-1421 change fallback. Roles:
#   environment  the air and the far dark: background, fog, the sky
#   back         the membrane's own dark: the deep saturated colour between the bands
#   trough       a blade face turned away (sin theta = -1): dark, but coloured
#   mid          the face between: the palette's body colour
#   crest        the face at its crest (sin theta = +1): the bright
#   core         the luminous core down each blade
#   glint        the narrowest highlights: streaks
#   glow         the membrane's pools of light, the heart light, the soft spill
#   front        strain: fronts and seams between islands
#   peak         the hottest highlights (reserved for the kick's ring)
ROLES = ("environment", "back", "trough", "mid", "crest", "core", "glint", "glow", "front", "peak")
PALETTES = [
    # name                  environment back       trough     mid        crest      core       glint      glow       front      peak
    ("Polar Glass",         "#01030b", "#04122c", "#040e26", "#0a2e60", "#1f80c8", "#5ad6ff", "#e2f4ff", "#1460e0", "#9ae6ff", "#f2fbff"),
    ("Glacial Teal",        "#010808", "#02191b", "#042426", "#08605a", "#22d6bc", "#6cffd8", "#e6fff6", "#0c9a88", "#aaffe4", "#f4fffb"),
    ("Acid Cathedral",      "#010308", "#030d16", "#0a1610", "#3a5418", "#b8f020", "#e0ff38", "#f8ffcc", "#5c8c0c", "#a2ff58", "#fdffe0"),
    ("Molten Signal",       "#070102", "#1c0305", "#260406", "#860c10", "#ff3418", "#ff6618", "#ffd088", "#b81406", "#ffac36", "#fff0c4"),
    ("Sunset Interference", "#0b0312", "#250830", "#360c3a", "#b82c64", "#ff8248", "#ff9858", "#ffe0c6", "#f0307e", "#ffae7c", "#fff0e2"),
    ("Ultraviolet",         "#04020d", "#12062e", "#18083a", "#461894", "#a844ff", "#dc4cff", "#eed6ff", "#8028f4", "#ff58d4", "#fff0ff"),
]
JOURNEY = list(range(len(PALETTES)))   # round the wheel by neighbours: ice, teal, acid, molten, sunset, violet, ice
START = int(os.environ.get("PW_PALETTE", "0"))
CHANGE = {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"], "fast": 3.0, "slow": 24.0,
          "threshold": 0.06, "rearm": 0.03, "primaryTimeout": 30.0, "maxHold": 90.0}
P = dict(zip(ROLES, PALETTES[START][1:]))


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
# The strip, in its own coordinates: x = RS * azimuth (once round, wrapped), z = RS * latitude, so a cell is a few
# metres of membrane wherever it is read, and a point's place on the strip is (RS * theta, RS * phi).
CELLS_X = NCOL // 2
NET_Z = (-math.radians(64.0) * RS, math.radians(72.0) * RS)
CELLS_Z = int(round((NET_Z[1] - NET_Z[0]) / (2 * math.pi * RS / CELLS_X)))


def on_strip(theta, phi):
    return (round(RS * theta, 3), 0.0, round(RS * phi, 3))


def on_sphere(theta, phi, r=RS):
    return (round(r * math.cos(phi) * math.cos(theta), 3), round(r * math.sin(phi), 3),
            round(r * math.cos(phi) * math.sin(theta), 3))


def network(s):
    """The strip: once round the sphere, wrapped, islands regime; the coupling breathes on the timeline (96 s)."""
    g = {"name": "net", "mode": "oscillator", "wrap": "wrap",
         "resolution": [CELLS_X, 1, CELLS_Z],
         "boundsMin": [-math.pi * RS, -40.0, NET_Z[0]], "boundsMax": [math.pi * RS, 40.0, NET_Z[1]],
         "frequency": 0.18, "simRate": 60.0, "maxSubSteps": 4, "seed": 3, "persistence": 0.6, "links": 0,
         "linkRange": 48.0, "checkpointInterval": 5.0, "coupling": 4.0, "phaseLag": 0.3, "couplingRadius": 3,
         "spread": 0.17, "seedAmount": 1.0, "noise": 0.02}
    s.grid(**g)
    # a periodic frequency tilt (one sine round the sphere, no seam at the wrap): unison still rolls as a slow wave
    s.field("tilt", {"kind": "wave", "waveGeometry": "planar", "waveShape": "sine", "axis": [1.0, 0.0, 0.0],
                     "point": [0.0, 0.0, 0.0], "wavelength": 2 * math.pi * RS, "waveSpeed": 0.0, "waveWidth": 0.0,
                     "amplitude": 1.0, "strength": 0.006})
    g["frequencyField"] = "tilt"
    keys = [(0, 1.5), (16, 4.0), (34, 5.5), (44, 12.0), (56, 12.0), (66, 5.5), (80, 4.0), (96, 1.5)]
    s.track("grid/net/coupling", [{"time": float(t), "value": [k], "interp": "smooth"} for t, k in keys], loop=96.0)
    return g


# ================================================================================================ the membrane's motion
KICK_AT = (FRONT_THETA + 0.75, math.radians(-4.0))    # where a kick lands: right of the frame, low (arcs cross it)
SNARE_AT = (FRONT_THETA - 0.9, math.radians(24.0))    # the snare: left, higher
SWELL_AT = (FRONT_THETA - 1.3, math.radians(-38.0))   # the bass rolls out from low on the left, out of frame: its
                                                      # rings cross the frame as arcs, never a target in its middle
DISP = 1.0                                            # the displacement effector's gain (m per unit of field)


def motion_fields(s):
    """The membrane's displacement, in metres outward (world frame, the sphere's centre at the origin):
      swell     the bass history laid out across the membrane: rings of it roll out from low in front at 34 m/s
      breath    a broad planar swell, 260 m long, travelling at 7 m/s: the held sound's undulation
      kickPush  each kick, a ring of push from where it lands, travelling at 70 m/s, gone in about a second
    `disp` adds them; the blades move along their own normals by it (ADR-1500) and the membrane behind them by the
    same field."""
    s.field("swell", {"kind": "spectrum", "audioBand": "range", "bandLow": 0.0, "bandHigh": 0.16,
                      "point": list(on_sphere(*SWELL_AT)), "waveGeometry": "spherical", "audioSpeed": 34.0,
                      "audioSmooth": 0.15, "strength": 5.0})
    s.field("breath", {"kind": "wave", "waveGeometry": "planar", "waveShape": "sine",
                       "axis": [0.35, 0.25, -0.9], "point": [0.0, 0.0, 0.0], "wavelength": 260.0, "waveSpeed": 7.0,
                       "waveWidth": 0.0, "amplitude": 1.0, "strength": 2.5})
    s.field("kickPush", {"kind": "onset", "onsetSource": "low", "point": list(on_sphere(*KICK_AT)),
                         "waveGeometry": "spherical", "audioSpeed": 70.0, "onsetWidth": 9.0, "onsetDecay": 1.4,
                         "strength": 7.0})   # onset rings are 0.1-0.5 at a kick's crest (measured): 1-3 m of push
    s.field("disp", {"kind": "compound", "combine": "add", "children": ["swell", "breath", "kickPush"]})


# ================================================================================================ the blades
ACT = (0.2, 0.5)        # activity -> front glow
TONE = 0.3              # the blades' body under their light (the bands carry the light, the faces stay deep)
CORE = 2.2              # the core's gain over its palette colour


def blade_program():
    """A blade is a luminous band, not a stripe:
    - Tone: the phase over the full sin theta range through a three-stop ramp (trough, mid, crest): the body.
    - Light down the middle: a narrow hot core (u(1-u) to a high power) in the core colour, a softer shoulder either
      side, the edges dark (the gaps between columns stay dark).
    - A hierarchy of brightness: a floor (quiet passages keep a lit field), the crest of its own phase, the membrane's
      displacement (where the swell lifts it, it brightens: pools of light travel with the motion), the kick's ring.
    - Streaks: a short band of light travelling up each column, every column at its own phase, its gain the hats.
    - Strain lights the blade's middle `front` (the network's activity, an emission effector)."""
    g = common.Prog("bladeSphere")
    bw, bh = BLADE[0], BLADE[1]
    g.op(None, "field", 0, field="ph")                              # r0 = sin theta
    g.op(None, "remap", 0, a=0, k=[-1.0, 1.0, 0.0, 1.0], f=1.0)     # -> 0..1
    g.op(None, "smoothstep", 0, a=0, k=[0.08, 0.92, 0, 0])          # r0 = crest weight
    g.op("tone", "ramp", 1, a=0, k=C("trough", TONE) + [1], k2=C("mid", TONE) + [1], k3=C("crest", TONE) + [1])  # r1 = body
    g.op(None, "input", 2, inp="localPosition")                     # r2 = blade frame
    g.op(None, "gradient", 3, a=2, k=[1, 0, 0, 0.5], f=1.0 / bw)    # r3 = u across (0..1)
    g.op(None, "gradient", 4, a=2, k=[-1, 0, 0, 0.5], f=1.0 / bw)   # r4 = 1 - u
    g.op(None, "multiply", 3, a=3, b=4)                             # r3 = u (1 - u): 0.25 in the middle
    g.op(None, "gradient", 4, a=2, k=[0, 1, 0, 0.5], f=1.0 / bh)    # r4 = v up (0..1)
    g.op(None, "smoothstep", 5, a=3, k=[0.0, 0.05, 0, 0])           # r5 = the body, dark at its two edges
    g.op(None, "multiply", 1, a=1, b=5)
    # the light across the blade: core (4u(1-u))^14 and a shoulder 0.22 (4u(1-u))^2.5
    g.op(None, "remap", 3, a=3, k=[0.0, 0.25, 0.0, 1.0], f=1.0)     # r3 = 4u(1-u)
    # ... gone at the blade's two ends, so a swinging blade's end faces never show as a dot of light
    g.op(None, "gradient", 6, a=2, k=[0, -1, 0, 0.5], f=1.0 / bh)   # 1 - v
    g.op(None, "multiply", 6, a=6, b=4)                             # v (1 - v)
    g.op(None, "smoothstep", 6, a=6, k=[0.0, 0.03, 0, 0])
    g.op(None, "multiply", 3, a=3, b=6)
    g.op(None, "power", 5, a=3, f=22.0)                             # r5 = core
    g.op(None, "power", 6, a=3, f=2.5)
    g.op("shoulder", "remap", 6, a=6, k=[0.0, 1.0, 0.0, 0.12])      # r6 = shoulder
    g.op(None, "add", 5, a=5, b=6)                                  # r5 = the profile
    # brightness: floor + crest + displacement lift + the kick's ring
    g.op(None, "power", 6, a=0, f=2.0)
    g.op("floor", "remap", 6, a=6, k=[0.0, 1.0, 0.1, 1.7])          # r6 = floor + crest^2: islands in step glow
    g.op(None, "field", 7, field="disp")                            # r7 = displacement (m)
    g.op(None, "remap", 7, a=7, k=[-2.5, 6.0, 0.0, 1.4], f=1.0)     # zones: the swell's crests bright, troughs dark
    g.op(None, "add", 6, a=6, b=7)
    g.op(None, "field", 7, field="kickPush")
    g.op(None, "remap", 7, a=7, k=[0.3, 2.4, 0.0, 2.0], f=1.0)      # the ring's crest
    g.op(None, "add", 6, a=6, b=7)                                  # r6 = intensity
    g.op(None, "multiply", 5, a=5, b=6)
    g.op("core", "constant", 7, k=C("core", CORE) + [1])             # (routes multiply it: CC 6 light, the bass)
    g.op(None, "multiply", 5, a=5, b=7)                             # r5 = emission (core light)
    # streaks: cos(2 pi 0.15 (v + random - 0.6 t))^60, a short glint running up a few blades at a time; the hats lift it
    g.op(None, "input", 6, inp="instanceRandom")
    g.op(None, "add", 4, a=4, b=6)                                  # v + random
    g.op(None, "input", 6, inp="time")
    g.op("streakRate", "remap", 6, a=6, k=[0.0, 1.0, 0.0, -0.6])    # -0.6 t: up the blade at 3 m/s
    g.op(None, "add", 4, a=4, b=6)
    g.op(None, "palette", 4, a=4, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[0.15, 0.15, 0.15, 0],
         k4=[0, 0, 0, 0])                                           # one crest every ~7 blade lengths: sparse
    g.op(None, "power", 4, a=4, f=200.0)                            # r4 = the streak: a short glint (about 1 m)
    g.op(None, "multiply", 4, a=4, b=3)                             # down the middle only
    g.op("glint", "constant", 6, k=C("glint", 1.6) + [1])           # (routes multiply it: the hats, CC 11)
    g.op(None, "multiply", 4, a=4, b=6)
    g.op(None, "add", 5, a=5, b=4)
    # strain (activity, one value per blade from the effector)
    g.op(None, "input", 4, inp="instanceEmissive")
    g.op(None, "smoothstep", 2, a=4, k=[ACT[0], ACT[1], 0, 0])
    g.op("front", "constant", 6, k=C("front", 1.2) + [1])
    g.op(None, "multiply", 2, a=2, b=6)
    g.op(None, "multiply", 2, a=2, b=3)                             # in the blade's middle
    g.op(None, "add", 5, a=5, b=2)
    g.base, g.emit = 1, 5
    return g


def blades(s):
    for ch, fn in ((1, "ph"), (3, "act")):   # sin theta; activity (strain + kicks)
        s.field(fn, {"kind": "grid", "reference": "net", "channel": ch, "strength": 1.0, "projection": "sphere",
                     "radius": RS})
    g = blade_program()
    s.program(g.doc(base=g.base, emission=g.emit))
    bind(s, "trough", op_path(g, "tone", "constant"), TONE)
    bind(s, "mid", op_path(g, "tone", "constant2"), TONE)
    bind(s, "crest", op_path(g, "tone", "constant3"), TONE)
    bind(s, "core", op_path(g, "core"), CORE)
    bind(s, "glint", op_path(g, "glint"), 1.6)
    bind(s, "front", op_path(g, "front"), 1.2)
    pts = blade_points()
    s.blade_count = len(pts)
    s.blade_program = g
    s.proc("blades", {"kind": "box", "size": list(BLADE)},
           distribution={"kind": "points", "points": pts},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": 0.42, "metallic": 0.55},
           extra={"effectors": [
               # the membrane's motion along each blade's own normal: outward is local -Z (ADR-1500)
               {"field": "disp", "op": "positionOffset", "blend": "add", "strength": DISP, "axis": [0, 0, -1],
                "local": True},
               # the swing about the blade's own hinge, its long axis (ADR-1500)
               {"field": "ph", "op": "rotation", "blend": "add", "strength": SWING, "axis": [0, 1, 0], "local": True},
               # strain lights whole blades (a blade is one oscillator): crisp, never a cloud
               {"field": "act", "op": "emission", "blend": "replace", "strength": 1.0}],
               "materialVariation": {"valueRandom": 0.06}})
    return g


def membrane_program():
    """The membrane behind the blades and beyond the fringes: its own deep colour, lifted in slow pools of glow that
    drift (a low-frequency noise in the sphere's frame, moving), brighter where the swell pushes it out, and the kick's
    ring crossing it. Unlit: it is the light behind the bands."""
    g = common.Prog("membrane")
    g.op(None, "input", 0, inp="worldPosition")
    g.op(None, "input", 1, inp="time")
    g.op("drift", "constant", 2, k=[0.9, 0.35, -0.6, 0])
    g.op(None, "multiply", 1, a=1, b=2)
    g.op(None, "add", 0, a=0, b=1)                                  # the pools drift
    g.op(None, "noise", 2, a=0, k=[0, 0, 0, 0], f=0.018)            # r2 = pools (0..1)
    g.op(None, "smoothstep", 2, a=2, k=[0.45, 0.85, 0, 0])
    g.op(None, "field", 3, field="disp")
    g.op(None, "remap", 3, a=3, k=[0.0, 6.0, 0.0, 0.8], f=1.0)
    g.op(None, "add", 2, a=2, b=3)
    g.op(None, "field", 3, field="kickPush")
    g.op(None, "remap", 3, a=3, k=[0.3, 2.4, 0.0, 1.2], f=1.0)
    g.op(None, "add", 2, a=2, b=3)                                  # r2 = glow weight
    # a fine cellular skin (the cells' edges, 4 m across): the membrane reads as a surface at every distance, and
    # at a pole it is cells, not rays
    g.op(None, "input", 3, inp="worldPosition")
    g.op(None, "voronoiEdge", 3, a=3, k=[0, 0, 0, 0], f=0.25)        # x = F2 - F1
    g.op(None, "smoothstep", 3, a=3, k=[0.12, 0.0, 0, 0])            # 1 on the edges
    g.op(None, "remap", 3, a=3, k=[0.0, 1.0, 0.25, 1.0])
    g.op(None, "multiply", 2, a=2, b=3)                              # the pools light the skin's edges most
    g.op("glow", "constant", 4, k=C("glow", 0.5) + [1])
    g.op(None, "multiply", 4, a=4, b=2)
    g.op("back", "constant", 5, k=C("back", 1.0) + [1])
    g.op(None, "add", 4, a=4, b=5)                                  # r4 = emission
    g.op(None, "constant", 6, k=[0, 0, 0, 1])
    g.base, g.emit = 6, 4
    return g


def membrane(s):
    g = membrane_program()
    s.program(g.doc(base=g.base, emission=g.emit))
    bind(s, "glow", op_path(g, "glow"), 0.5)
    bind(s, "back", op_path(g, "back"))
    pos, nrm, uv, idx = common.sphere_mesh(BACK, 160, 80)
    nrm = -nrm                                                      # seen from inside
    idx = idx.reshape(-1, 3)[:, [0, 2, 1]].ravel()
    common.write_glb(os.path.join(OUT, "meshes", "ph-membrane.glb"), pos, nrm, uv, idx, "membrane")
    s.proc("membrane", {"kind": "mesh", "asset": "meshes/ph-membrane.glb"},
           material={"program": g.name, "baseColor": [0, 0, 0], "roughness": 0.9, "metallic": 0.0, "unlit": True},
           # the same displacement as the blades: along its (inward) normal, negative = outward
           deformers=[{"kind": "field", "field": "disp", "amount": -DISP, "space": "world", "alongNormal": True}])
    return g


def chip_program():
    """A cap chip lights like a blade (a hot core down its long axis, a shoulder, dark edges, as bright as the columns);
    its body is the palette's tone by the membrane's displacement (it has no phase of its own: the network's azimuth is
    singular at a pole), and its brightness follows the same zones and the kick's ring."""
    g = common.Prog("chip")
    cw, ch = CHIP[0], CHIP[1]
    g.op(None, "field", 0, field="disp")
    g.op(None, "remap", 0, a=0, k=[-2.5, 6.0, 0.0, 1.0], f=1.0)     # r0 = the swell's zone (0..1)
    g.op("tone", "ramp", 1, a=0, k=C("trough", TONE) + [1], k2=C("mid", TONE) + [1], k3=C("crest", TONE) + [1])
    g.op(None, "input", 2, inp="localPosition")
    g.op(None, "gradient", 3, a=2, k=[1, 0, 0, 0.5], f=1.0 / cw)
    g.op(None, "gradient", 4, a=2, k=[-1, 0, 0, 0.5], f=1.0 / cw)
    g.op(None, "multiply", 3, a=3, b=4)                             # u (1 - u)
    g.op(None, "gradient", 4, a=2, k=[0, 1, 0, 0.5], f=1.0 / ch)
    g.op(None, "gradient", 5, a=2, k=[0, -1, 0, 0.5], f=1.0 / ch)
    g.op(None, "multiply", 4, a=4, b=5)                             # v (1 - v)
    g.op(None, "smoothstep", 5, a=3, k=[0.0, 0.05, 0, 0])
    g.op(None, "multiply", 1, a=1, b=5)                             # the body darkens at its edges
    g.op(None, "remap", 3, a=3, k=[0.0, 0.25, 0.0, 1.0], f=1.0)     # 4u(1-u)
    g.op(None, "smoothstep", 4, a=4, k=[0.0, 0.05, 0, 0])           # gone at the ends
    g.op(None, "multiply", 3, a=3, b=4)
    g.op(None, "power", 5, a=3, f=14.0)                             # core
    g.op(None, "power", 6, a=3, f=2.5)
    g.op(None, "remap", 6, a=6, k=[0.0, 1.0, 0.0, 0.12])            # shoulder
    g.op(None, "add", 5, a=5, b=6)
    g.op(None, "remap", 6, a=0, k=[0.0, 1.0, 0.45, 2.2])            # intensity: the zones
    g.op(None, "field", 7, field="kickPush")
    g.op(None, "remap", 7, a=7, k=[0.3, 2.4, 0.0, 2.0], f=1.0)
    g.op(None, "add", 6, a=6, b=7)
    g.op(None, "multiply", 5, a=5, b=6)
    g.op("core", "constant", 7, k=C("core", CORE) + [1])
    g.op(None, "multiply", 5, a=5, b=7)
    g.base, g.emit = 1, 5
    return g


def chips(s):
    g = chip_program()
    s.program(g.doc(base=g.base, emission=g.emit))
    bind(s, "trough", op_path(g, "tone", "constant"), TONE)
    bind(s, "mid", op_path(g, "tone", "constant2"), TONE)
    bind(s, "crest", op_path(g, "tone", "constant3"), TONE)
    bind(s, "core", op_path(g, "core"), CORE)
    pts = chip_points()
    s.chip_count = len(pts)
    s.proc("chips", {"kind": "box", "size": list(CHIP)},
           distribution={"kind": "points", "points": pts},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": 0.45, "metallic": 0.55},
           extra={"effectors": [
               {"field": "disp", "op": "positionOffset", "blend": "add", "strength": DISP, "axis": [0, 0, -1],
                "local": True},
               # the chips tilt as the swell passes under them (about their own long axis)
               {"field": "disp", "op": "rotation", "blend": "add", "strength": 0.16, "axis": [0, 1, 0],
                "local": True}]})
    return g


def light(s):
    env = C("environment")
    s.environment = {
        "intensity": 0.18, "background": env, "fogColor": env, "volumeDensity": 0.0,
        "fogSky": 0.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0, "shadowRange": 160.0,
        "ambientOcclusion": False, "contactShadows": False,
        "depthPrepass": True,   # ADR-1406: the membrane and the blades' backs behind the faces
        "sky": {"enabled": True, "zenithColor": env, "horizonColor": env, "groundColor": env, "haze": 1.0,
                "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
        # the flash: vertical great circles in direction space. A blade swinging about its hinge sweeps its reflection
        # across them (Ned Kahn's mechanism), so light runs over the turning blades
        "bands": {"phase": 0.0, "rotation": 0.0, "gain": 1.0,
                  "softbox": {"intensity": 0.0, "azimuth": 0.6, "elevation": 0.7, "falloff": 2.6, "skyFill": 0.0},
                  "strips": [{"axis": [math.cos(k * math.pi / 4), 0.0, math.sin(k * math.pi / 4)], "offset": 0.0,
                              "width": 0.1, "intensity": 2.0, "segments": 0, "warmth": 0.5, "rate": 0.0}
                             for k in range(4)]},
    }
    for k in ("env/sky/zenithColor", "env/sky/horizonColor", "env/sky/groundColor", "scene/fogColor"):
        bind(s, "environment", k)
    # the faces' light: a soft source at the sphere's heart, so a blade swinging away from the centre falls dark
    s.light("heart", "point", position=[0.0, 10.0, 0.0], color=C("glow"), intensity=26000.0, range=0.0,
            castsShadow=False, contactShadow=False, diffuseOnly=True)
    bind(s, "glow", "lights/heart/color")


# ================================================================================================ the music
# The music writes into the network (onsets are phase kicks, held sound is coupling, a build is the lag), into the
# membrane (the bass history rolls through it, the held sound swells it, kicks push rings through it) and into the
# light (bass: the cores; hats: the streaks). Every audio route's depth is Audio Influence (CC 4, half at rest).
MIDI_CC = [
    (1, "coupling", "COUPLING: pulls the sphere toward unison"),
    (2, "complexity", "COMPLEXITY: widens the spread of natural frequencies (more islands, more seams)"),
    (3, "motion", "MOTION: the network's speed"),
    (4, "influence", "AUDIO INFLUENCE: how hard the music drives everything (half at rest)"),
    (5, "lag", "LAG: the Sakaguchi phase lag, rings toward spirals"),
    (6, "light", "LIGHT: the bands' luminous cores and the reflected strips"),
    (7, "swell", "SWELL: how far the membrane moves (the bass rings, the broad swell)"),
    (8, "impact", "IMPACT: how hard a kick pushes the membrane and the network"),
    (9, "palette", "PALETTE: choose one of the six (a direct blend to it)"),
    (10, "paletteLock", "PALETTE LOCK: hold the chosen palette (no automatic changes)"),
    (11, "shimmer", "SHIMMER: the streaks of light up the blades"),
]
MIDI_NOTES = [(36, "pace")]


def music(s, net, g):
    G = "grid/net/"
    for name, at, src, speed, width, decay in (("kickFront", KICK_AT, "low", 60.0, 14.0, 0.9),
                                               ("snareFront", SNARE_AT, "mid", 90.0, 8.0, 1.6)):
        s.field(name, {"kind": "onset", "onsetSource": src, "audioSpeed": speed, "onsetWidth": width,
                       "onsetDecay": decay, "point": list(on_strip(*at)), "waveGeometry": "spherical"})
    s.field("kicks", {"kind": "compound", "combine": "add", "children": ["kickFront", "snareFront"]})
    net["injectField"], net["injectRate"] = "kicks", 2.0
    # MIDI note 36 holds a pacemaker: a patch tuned faster sends target waves while the note is held
    s.field("pace", {"kind": "radial", "radius": 24.0, "strength": 0.0},
            position=on_strip(FRONT_THETA - 0.25, math.radians(16.0)))
    s.field("tune", {"kind": "compound", "combine": "add", "children": ["tilt", "pace"]})
    net["frequencyField"] = "tune"
    A = common.audio
    s.route(A("response.sustain", G + "coupling", 3.0, attackMs=3000, decayMs=6000),
            A("response.bass", G + "frequency", 0.25, attackMs=300, decayMs=1500),
            A("response.hat", G + "noise", 0.12, attackMs=40, decayMs=400),
            A("response.intensity", G + "phaseLag", 0.35, attackMs=6000, decayMs=12000),
            A("response.level", "scene/bands/rotation", 0.15, integrate=True),
            # the light: the bass warms the cores slowly, the hats run the streaks
            A("response.bass", op_path(g, "core"), 1.0, op="multiply", gain=0.5, offset=0.75, attackMs=200,
              decayMs=900),
            A("response.hat", op_path(g, "glint"), 1.0, op="multiply", gain=3.0, offset=0.04, attackMs=8,
              decayMs=260),
            # the held sound swells the membrane
            A("response.sustain", "field/breath/strength", 3.0, attackMs=2500, decayMs=5000))
    common.instrument(s, MIDI_CC, MIDI_NOTES)
    CC = common.CC
    s.route(R("control.coupling", G + "coupling", 12.0, **CC),
            R("control.complexity", G + "spread", 0.2, **CC),
            R("control.motion", G + "speed", 1.5, **CC),
            R("control.lag", G + "phaseLag", 1.0, **CC),
            R("control.light", "scene/bands/gain", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.light", op_path(g, "core"), 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.swell", "field/swell/strength", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.swell", "field/breath/strength", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.impact", "field/kickPush/strength", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.impact", G + "injectRate", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.shimmer", op_path(g, "glint"), 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.palette", "palette/select", float(len(PALETTES) - 1), **CC),
            R("control.paletteLock", "palette/lock", 1.0, **CC),
            R("control.pace", "field/pace/strength", 0.35, attackMs=80, decayMs=2500))


# ================================================================================================ camera
# The performance camera: well inside (a third of the radius off the centre, a little low), looking across at the far
# side a few degrees up, so the frame holds the belt of columns curving away to both sides; it drifts a few metres
# over 90 s and never looks along an axis toward a pole.
CAM = ((8.0, -18.0, 52.0), (70.0, 22.0, -140.0))
CAMERAS = {  # review framings
    "perf": ("8,-18,52", "70,22,-140"),
    "up": ("8,-18,52", "-20,150,-60"),
    "zenith": ("8,-18,52", "8.5,150,52.5"),
    "nadir": ("8,-18,52", "8.5,-150,52.5"),
    "side": ("8,-18,52", "150,0,40"),
    "back": ("8,-18,52", "0,10,150"),
}


def camera(s):
    s.camera = {"mode": 1, "position": list(CAM[0]), "target": list(CAM[1]), "fov": 60.0, "orbitSpeed": 0.0}
    s.drift_camera(CAM[0], CAM[1], period=90.0, amp=(5.0, 2.0, 4.0), tamp=(6.0, 3.0, 0.0), steps=8)
    s.params_({"camera/lens/focalLength": 20.0, "post/bloom/intensity": 0.26, "post/bloom/threshold": 1.0,
               "post/bloom/radius": 0.8, "post/output/vignette": 0.2, "post/grade/contrast": 1.06})


# ================================================================================================ build
def build(autonomous=False):
    s = common.Scene(SID, TITLE, {"category": "experiment", "experiment": "A coupled oscillators",
                                  "thesis": "An immense membrane of luminous vertical blades round the inside of a "
                                            "sphere: about eighteen thousand blades, each swung by its own oscillator, falling "
                                            "into step and breaking apart round the viewer, the music travelling "
                                            "through the surface as rings, swells and streaks of light."})
    s.binds = []
    net = network(s)
    motion_fields(s)
    g = blades(s)
    chips(s)
    membrane(s)
    light(s)
    camera(s)
    if autonomous:
        s.response = dict(common.RESPONSE)
    else:
        music(s, net, g)
    s.palette = palette_block(s)
    return s


def register(s):
    """The Examples > Live > Generative entry (live: the project carries sonic.live and no audio file)."""
    cc = common.describe_ccs(MIDI_CC, "note 36 holds a pacemaker")
    text = ("Play live audio (any mix) and, optionally, a MIDI controller: " + cc + ". " + s.design["thesis"] +
            " The bass history rolls out across the membrane as rings, the held sound swells it and pulls the blades "
            "toward unison, kicks push a ring through it and launch a phase front round the sphere, the snare a "
            "quicker one, hats run streaks up the blades; six palettes take turns at phrase boundaries (or on a big "
            "change in the music when there is no tempo). See docs/prototypes/phase-wall/ and "
            "docs/prototypes/generative-pass/.")
    common.index_entry(s.title, "experiments/%s/%s.json" % (SID, SID), text, live=True, category="Generative")


def write_variant(pp, out, wav=None, cam=None, sets=(), tracks=(), palette=None):
    """A review project. `palette` (an index) selects and locks one state."""
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
    pp, sp = common.write_example(s, OUT)
    nf = sum(1 for n in s.nodes if n["kind"] == "field")
    assert nf <= 16, "%d GPU fields (16 at most)" % nf
    print(pp, "fields=%d blades=%d chips=%d grid=%s ops=%d" % (nf, s.blade_count, s.chip_count,
                                                               s.grids[0]["resolution"], len(s.blade_program.ops)))
    if not a.variant and not a.autonomous:
        register(s)
    if a.variant:
        print(write_variant(pp, a.out, a.wav, a.camera, a.set, a.track, a.palette))


if __name__ == "__main__":
    main()
