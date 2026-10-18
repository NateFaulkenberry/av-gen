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

SID, TITLE = "phase-wall", "Phase Wall"
OUT = os.path.join(REPO, "examples", "experiments", SID)

# ================================================================================================ the hall
L, W, H = 128.0, 48.0, 30.0       # length (z), width (x), height
BAY = 16.0                        # bay piers on the long walls
PIER = 3.2                        # corner piers (square)
BASE = 0.6                        # the blades start this high (a dark plinth below)
PX, PY = 0.75, 1.6                # blade pitch: across, up
BLADE = (0.745, 1.585, 0.04)      # blade size: width, height, thickness (closed, the screen has no see-through gaps)
CELL = 1.2                        # network cell (m): an island spans 8-15 blades
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


# ================================================================================================ palette (sRGB refs)
# Roles (02-plan.md §6). Stage 1 carries one state, Bronze Tide (the R&D A3 look); Stage 5 adds the rest.
P = {
    "environment": "#071214",   # haze, ceiling, the air in the darks
    "back": "#0f3a42",          # the backplane: the colour between and behind the blades
    "trough": "#06100f",        # a blade's face turned from the light (sin theta < 0)
    "metal": "#c08a52",         # a blade's face at its crest (sin theta > 0)
    "key": "#ffd9b0",           # the key light
    "front": "#4ff0dc",         # strain: fronts and defects
    "peak": "#ffb35c",          # the hottest defect cores
    "trim": "#ffcf9a",          # pier strips
    "stone": "#0a0f10",         # piers, plinth
    "floor": "#14282b",         # the floor: dark lacquered metal, so what it reflects takes its tint
}
TUNE = {"rough": 0.38, "key": 1.2, "wash": 1.8, "back": 0.55, "emit": 1.0, "bands": 2.5, "metallic": 0.6}


def C(key, s=1.0):
    return hexrgb(P[key], s)


# ================================================================================================ the network
def network(s):
    """The strip: PERIM x H, wrapped, islands regime; the coupling breathes on the timeline (the R&D's 96 s arc)."""
    g = {"name": "net", "mode": "oscillator", "wrap": "wrap",
         "resolution": [int(round(PERIM / CELL)), 1, int(round(H / CELL))],
         "boundsMin": [-PERIM / 2, -40.0, -H / 2], "boundsMax": [PERIM / 2, 40.0, H / 2],
         "frequency": 0.18, "simRate": 60.0, "maxSubSteps": 4, "seed": 3, "persistence": 0.6, "links": 0,
         "linkRange": 48.0, "checkpointInterval": 5.0, "coupling": 4.0, "phaseLag": 0.3, "couplingRadius": 3,
         "spread": 0.15, "seedAmount": 1.0, "noise": 0.05}
    s.grid(**g)
    # a periodic frequency tilt (one sine round the hall, no seam at the wrap): unison still rolls as a slow wave
    s.field("tilt", {"kind": "wave", "waveGeometry": "planar", "waveShape": "sine", "axis": [1.0, 0.0, 0.0],
                     "point": [0.0, 0.0, 0.0], "wavelength": PERIM, "waveSpeed": 0.0, "waveWidth": 0.0,
                     "amplitude": 1.0, "strength": 0.02})
    g["frequencyField"] = "tilt"
    # the breath (96 s): disorder -> islands (K ~4.5, held 16-34 s) -> unison (K 12, 44-56 s) -> islands -> disorder.
    # On the 1.2 m lattice K 10 already locks the room, so the islands band is held on purpose
    keys = [(0, 1.5), (16, 4.0), (34, 5.5), (44, 12.0), (56, 12.0), (66, 5.5), (80, 4.0), (96, 1.5)]
    s.track("grid/net/coupling", [{"time": float(t), "value": [k], "interp": "smooth"} for t, k in keys], loop=96.0)
    return g


# ================================================================================================ the blades
ACT = (0.5, 1.9)      # activity -> front glow (the coarse lattice's strain is lower than the R&D's)
ACT_PEAK = (1.6, 3.2)  # activity -> the defect core


def blade_program(wall):
    """One wall's blades. The face's tone is the phase (trough -> metal across sin theta, the full range); the
    reflection bands and the key add the moving light; strain glows `front`, its cores `peak`."""
    g = common.Prog("blade_" + wall)
    g.op(None, "field", 0, field="ph_" + wall)                      # r0 = sin theta
    g.op(None, "remap", 0, a=0, k=[-1.0, 1.0, 0.0, 1.0], f=1.0)    # -> 0..1
    g.op(None, "smoothstep", 0, a=0, k=[0.15, 0.85, 0, 0])
    g.op("trough", "constant", 1, k=C("trough") + [1])
    g.op("metal", "constant", 2, k=C("metal") + [1])
    g.op(None, "mixBy", 3, a=1, b=2, c=0)                           # r3 = base
    g.op(None, "input", 4, inp="instanceEmissive")                  # r4 = activity, one value per blade (effector)
    g.op(None, "smoothstep", 5, a=4, k=[ACT[0], ACT[1], 0, 0])
    g.op("front", "constant", 6, k=C("front", 1.3) + [1])
    g.op(None, "multiply", 5, a=5, b=6)                             # r5 = front glow
    g.op(None, "smoothstep", 4, a=4, k=[ACT_PEAK[0], ACT_PEAK[1], 0, 0])
    g.op("peak", "constant", 6, k=C("peak", 2.5) + [1])
    g.op(None, "multiply", 4, a=4, b=6)
    g.op(None, "add", 5, a=5, b=4)                                  # r5 = emission
    g.op("rough", "constant", 6, k=[TUNE["rough"]] * 3 + [1])
    g.op(None, "roughnessFilter", 6, a=6, f=1.0)
    g.base, g.emit, g.rough = 3, 5, 6
    return g


def walls(s):
    for name, st, d, n, u0 in WALLS:
        pos, rot = reader_frame(st, d, u0)
        for ch, fn in ((1, "ph_"), (3, "act_")):   # sin theta; activity (strain + kicks)
            s.field(fn + name, {"kind": "grid", "reference": "net", "channel": ch, "strength": 1.0,
                                "rotation": rot}, position=pos)
        g = blade_program(name)
        s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
        s.blade_programs.append(g)
        inset = PIER / 2 + 0.2
        nx = int((n - 2 * inset) // PX)
        ny = int((H - BASE - 0.4) // PY)
        cx, cz = st[0] + d[0] * n / 2, st[1] + d[2] * n / 2
        nrm = (-d[2], 0.0, d[0])
        yaw = yaw_of(d)
        s.proc("blades_" + name, {"kind": "box", "size": list(BLADE)},
               distribution={"kind": "grid", "gridCount": [nx, ny, 1], "gridSpacing": [PX, PY, 1.0]},
               position=(cx, BASE + ny * PY / 2, cz), rotation=(0.0, yaw, 0.0),
               material={"program": g.name, "baseColor": [1, 1, 1], "roughness": TUNE["rough"], "metallic": TUNE["metallic"]},
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
               material={"baseColor": C("stone"), "roughness": 0.9, "metallic": 0.0,
                         "emissiveColor": C("back", TUNE["back"]), "emissiveIntensity": 1.0})


# ================================================================================================ the room
def bounce_program():
    """The floor: dark polished stone that picks up the walls' colour -- a spill of `back` within ~7 m of each wall
    (four axis gradients of the world position, summed, squared) -- since the engine has no planar reflection."""
    g = common.Prog("floorStone")
    g.op(None, "input", 0, inp="worldPosition")
    reach = 7.0
    for i, (ax, half) in enumerate(((( -1, 0, 0), W / 2), ((1, 0, 0), W / 2), ((0, 0, -1), L / 2), ((0, 0, 1), L / 2))):
        g.op(None, "gradient", 1 if i == 0 else 2, a=0, k=list(ax) + [1.0 - half / reach], f=1.0 / reach)
        if i:
            g.op(None, "add", 1, a=1, b=2)
    g.op(None, "power", 1, a=1, f=2.0)
    g.op("spill", "constant", 2, k=C("back", 0.22) + [1])
    g.op(None, "multiply", 1, a=1, b=2)
    g.op("stone", "constant", 3, k=C("floor") + [1])
    g.base, g.emit = 3, 1
    return g


def room(s):
    nb = int(round(L / BAY))
    stone = {"baseColor": C("stone"), "roughness": 0.55, "metallic": 0.0}
    for i, (cx, cz) in enumerate(((-W / 2, L / 2), (-W / 2, -L / 2), (W / 2, -L / 2), (W / 2, L / 2))):
        s.proc("pier%d" % i, {"kind": "box", "size": [PIER, H + 2.0, PIER]}, position=(cx, H / 2, cz), material=stone)
    for side, x in (("w", -W / 2), ("e", W / 2)):
        inward = 1.0 if side == "w" else -1.0
        s.proc("bays_" + side, {"kind": "box", "size": [1.6, H + 2.0, 1.4]},
               distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1.0, 1.0, L / nb]},
               position=(x + inward * 0.5, H / 2, 0.0), material=stone)
        s.proc("strips_" + side, {"kind": "box", "size": [0.1, H - 4.0, 0.2]},
               distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1.0, 1.0, L / nb]},
               position=(x + inward * 1.32, H / 2, 0.0),
               material={"baseColor": [0, 0, 0], "roughness": 0.5, "metallic": 0.0,
                         "emissiveColor": C("trim"), "emissiveIntensity": 1.6})
    # plinth under the blades
    for name, st, d, n, u0 in WALLS:
        cx, cz = st[0] + d[0] * n / 2, st[1] + d[2] * n / 2
        nrm = (-d[2], 0.0, d[0])
        s.proc("plinth_" + name, {"kind": "box", "size": [n, BASE, 0.8]},
               position=(cx + nrm[0] * 0.1, BASE / 2, cz + nrm[2] * 0.1), rotation=(0.0, yaw_of(d), 0.0),
               material=stone)
    # the ceiling: deep coffers, mostly in shadow; a faint wash of the haze
    dark = {"baseColor": C("environment", 0.8), "roughness": 0.8, "metallic": 0.0}
    s.proc("beams_x", {"kind": "box", "size": [W + 4, 2.4, 0.8]},
           distribution={"kind": "grid", "gridCount": [1, 1, nb * 2 + 1], "gridSpacing": [1, 1, L / (nb * 2)]},
           position=(0.0, H - 0.2, 0.0), material=dark)
    s.proc("beams_z", {"kind": "box", "size": [0.8, 2.4, L + 4]},
           distribution={"kind": "grid", "gridCount": [5, 1, 1], "gridSpacing": [W / 4, 1, 1]},
           position=(0.0, H - 0.2, 0.0), material=dark)
    s.proc("ceiling", {"kind": "box", "size": [W + 4, 1.0, L + 4]}, position=(0.0, H + 1.5, 0.0),
           material={"baseColor": C("environment", 0.5), "roughness": 0.9, "metallic": 0.0,
                     "emissiveColor": C("environment", 0.6), "emissiveIntensity": 1.0})
    # the floor and its brass inlay (leading lines down the hall, joints across it)
    fp = bounce_program()
    s.program(fp.doc(base=fp.base, emission=fp.emit))
    s.floor_program = fp
    s.proc("floor", {"kind": "box", "size": [W + 8, 1.0, L + 8]}, position=(0.0, -0.5, 0.0),
           material={"program": fp.name, "baseColor": C("floor"), "roughness": 0.22, "metallic": 1.0})
    brass = {"baseColor": C("metal", 0.5), "roughness": 0.35, "metallic": 1.0}
    s.proc("inlay_z", {"kind": "box", "size": [0.12, 0.02, L]},
           distribution={"kind": "grid", "gridCount": [5, 1, 1], "gridSpacing": [W / 6, 1, 1]},
           position=(0.0, 0.005, 0.0), material=brass)
    s.proc("inlay_x", {"kind": "box", "size": [W, 0.02, 0.12]},
           distribution={"kind": "grid", "gridCount": [1, 1, nb - 1], "gridSpacing": [1, 1, L / nb]},
           position=(0.0, 0.005, 0.0), material=brass)


def light(s):
    env = C("environment")
    s.environment = {
        "intensity": 0.05, "background": env, "fogColor": env, "volumeDensity": 0.0,
        "fogSky": 0.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0, "shadowRange": 160.0,
        "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": env, "horizonColor": env, "groundColor": env, "haze": 1.0,
                "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
        # the flash: vertical great circles (the opposite piers' strips, in direction space). A blade turning
        # about y sweeps its reflection across them; 0.12 wide on 0.38-rough metal passes the strobe gate
        "bands": {"phase": 0.0, "rotation": 0.0, "gain": 1.0,
                  "softbox": {"intensity": 0.0, "azimuth": 0.6, "elevation": 0.7, "falloff": 2.6, "skyFill": 0.0},
                  "strips": [{"axis": [math.cos(k * math.pi / 4), 0.0, math.sin(k * math.pi / 4)], "offset": 0.0,
                              "width": 0.12, "intensity": TUNE["bands"], "segments": 0, "warmth": 0.7 - 0.15 * k,
                              "rate": 0.0} for k in range(4)]},
    }
    # the key: high, raking the floor and the piers through an implied clerestory
    s.light("key", "directional", direction=[0.45, -0.8, -0.4], color=C("key"), intensity=TUNE["key"],
            castsShadow=True, contactShadow=False, softness=0.35)
    # four washes, one per wall, from high in front of it (Rozin's lamps above the tiles): a wall is lit only by its
    # own wash (the opposite wall faces away; the side walls take it at grazing), so a blade's value reads its angle
    for name, st, d, n, u0 in WALLS:
        nrm = (-d[2], 0.0, d[0])
        el = 0.35
        s.light("wash_" + name, "directional", direction=[-nrm[0] * math.cos(el), -math.sin(el), -nrm[2] * math.cos(el)],
                color=C("key"), intensity=TUNE["wash"], castsShadow=False, contactShadow=False)


# ================================================================================================ camera
CAMERAS = {  # review framings: oblique and off-axis (the walls are the hero; never a dead-centre corridor)
    "nave": ("-15,6,42", "17,13,-30"),
    "east": ("-12,9,-2", "24,14,-26"),
    "corner": ("13,5,-38", "-24,19,-64"),
    "end": ("9,8,-22", "-8,14,-64"),
    "up": ("2,3,8", "22,24,-22"),
}


def camera(s):
    p, t = CAMERAS["nave"]
    s.camera = {"mode": 1, "position": [float(v) for v in p.split(",")],
                "target": [float(v) for v in t.split(",")], "fov": 40.0, "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": 24.0, "post/bloom/intensity": 0.28, "post/output/vignette": 0.3,
               "post/grade/contrast": 1.08, "post/look/atmospheric": 0.12, "post/look/atmosphericDistance": 140.0,
               "post/look/atmosphericTint": C("environment", 2.0)})


# ================================================================================================ build
def build(autonomous=True):
    s = common.Scene(SID, TITLE, {"category": "experiment", "experiment": "A coupled oscillators",
                                  "thesis": "A hall whose four walls are one kinetic installation: eight thousand "
                                            "metal blades, each swung by its own oscillator, falling into step and "
                                            "breaking apart round the room."})
    s.blade_programs = []
    network(s)
    walls(s)
    room(s)
    light(s)
    camera(s)
    s.response = dict(common.RESPONSE)
    return s


def write_variant(pp, out, wav=None, cam=None, sets=(), tracks=()):
    if cam and cam in CAMERAS:
        cam = "%s:%s" % CAMERAS[cam]
    params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in sets}
    return common.variant(pp, out, wav, cam, params, list(tracks))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--wav")
    ap.add_argument("--camera")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--track", action="append", default=[])
    a = ap.parse_args()
    s = build()
    pp, sp = s.write(OUT)
    nf = sum(1 for n in s.nodes if n["kind"] == "field")
    blades = sum(n["procedural"]["distribution"]["gridCount"][0] * n["procedural"]["distribution"]["gridCount"][1]
                 for n in s.nodes if n["name"].startswith("blades_"))
    assert nf <= 16, "%d GPU fields (16 at most)" % nf
    print(pp, "fields=%d blades=%d grid=%s" % (nf, blades, s.grids[0]["resolution"]))
    if a.variant:
        print(write_variant(pp, a.out, a.wav, a.camera, a.set, a.track))


if __name__ == "__main__":
    main()
