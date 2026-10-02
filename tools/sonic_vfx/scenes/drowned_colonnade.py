"""THE DROWNED COLONNADE (melancholy, mystery). Sunken ruins: a double row of broken marble columns runs into blue water
toward a gate of three stones, in slanting sun shafts, with caustic nets moving on the sand.

Art direction (ART-RESTART-PLAN.md #7): underwater, a few metres above a sandy floor, looking along the colonnade as it
recedes into blue. Some columns stand to full height, some are snapped, one lies across the way. At the end of the
row, on the left third, the gate (two pillars and a lintel) stands half buried; a fallen colossal head lies in the sand
before it. Sun shafts from the unseen surface slant down from the upper right; caustics move on the sand and the
marble. Sea grass at the column bases, a few fish. Beyond 40 m the water swallows everything into one blue. Nothing
glows.

Underwater is built from the air's own pieces: the sea is the volumetric medium (absorbing, blue, dense) with no sky,
the surface light a shadow-casting directional light, the shafts lightBeams, the caustics a material program on the
sand (a moving cell network, voronoiEdge through time).
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "drowned-colonnade"
TITLE = "The Drowned Colonnade"

DEEP = "#0b2f4d"
TEAL = "#2a7f8f"
SAND = "#c9b48a"
MARBLE = "#d8d2c4"
GRASS = "#3f6b3a"

CAM = (1.6, 3.4, 6.0)
LENS = 24.0
ROW_X = 4.2                       # the rows stand at x = -ROW_X and +ROW_X
SPACING = 5.4                     # metres between columns along the row
COLS = 6                          # columns per row
GATE = (-0.6, 0.0, -36.0)         # the gate's centre on the sand
HEAD = (-3.2, 0.0, -31.0)         # the fallen head, before the gate
SUN_DIR = (-0.55, -0.80, -0.24)   # the light from the surface: from the upper right, a little behind the camera

DESIGN = {
    "category": "underwater ruins",
    "thesis": "Sunken ruins: broken marble columns recede into blue water toward a gate, in slanting sun shafts and "
              "moving caustics.",
    "composition": {
        "background": "the water itself, blue to black; the gate's silhouette",
        "midground": "the colonnade receding, the fallen column, the head in the sand",
        "foreground": "the nearest column on the right, sea grass, the rippled sand",
        "focal": "the gate at the end of the row, in the light",
        "secondary": ["the fallen head", "the shafts", "caustics on the sand"],
        "atmosphere": "dense blue water that swallows the distance",
        "camera": "24 mm, 3.4 m above the sand, a gentle downward look along the row",
    },
    "palette": {"dominant": DEEP, "secondary": TEAL, "accent": SAND, "highlight": MARBLE},
    "motion": {"very_slow": ["the camera's drift"], "medium": ["caustics", "the shafts' sway"], "fast": [],
               "extremely_fast": []},
    "vocabulary": [],
    "tier": "medium: SDF columns meshed once, a terrain floor, a dense absorbing medium (16 steps), beams",
}


def column(height, broken=0.0, seed=1):
    """A fluted Doric column standing on y = 0: a square plinth, a fluted shaft, a cushion and abacus capital. `broken`
    > 0 snaps the shaft at that height with a ragged noisy cut (no capital)."""
    r = 0.62
    plinth = kit.sd_move((0, 0.18, 0), kit.sd_rbox([0.86, 0.18, 0.86], 0.03))
    torus = kit.sd_move((0, 0.42, 0), kit.sd_torus(r + 0.06, 0.09))
    shaft = kit.sd_move((0, height * 0.5, 0), kit.sd_cyl(r, height))
    flutes = kit.sd_polar(20, kit.sd_move((r + 0.035, height * 0.5, 0), kit.sd_cyl(0.075, height + 0.2)))
    shaft = kit.sd_diff(shaft, flutes)
    parts = [plinth, torus]
    if broken > 0.0:
        cut = kit.sd_noise(0.35, 1.3, kit.sd_move((0, broken + 30.0, 0), kit.sd_box([3.0, 30.0, 3.0])), seed=seed)
        parts.append(kit.sd_diff(shaft, cut))
    else:
        echinus = kit.sd_move((0, height + 0.12, 0), kit.sd_cyl(r + 0.16, 0.24))
        abacus = kit.sd_move((0, height + 0.38, 0), kit.sd_box([0.95, 0.14, 0.95]))
        parts += [shaft, echinus, abacus]
    body = kit.sd_union(*parts)
    return kit.sd_noise(0.035, 2.4, body, seed=seed + 7)      # weathering


def gate():
    """The trilithon at the end of the row: two pillars and a lintel, a little out of true."""
    left = kit.sd_move((-2.6, 4.4, 0), kit.sd_rbox([0.9, 4.4, 0.8], 0.05))
    right = kit.sd_move((2.6, 4.2, 0), kit.sd_rot((0, 0, 2.5), kit.sd_rbox([0.9, 4.2, 0.8], 0.05)))
    lintel = kit.sd_move((0.0, 9.3, 0), kit.sd_rot((0, 0, -3.0), kit.sd_rbox([3.9, 0.7, 0.95], 0.06)))
    return kit.sd_noise(0.06, 1.6, kit.sd_union(left, right, lintel), seed=21)


def caustic_program(name, base_hex, strength, scale=1.6, speed=0.45, warp=0.45):
    """Sand (or marble) under moving caustics. The caustic net is a cell network (voronoiEdge) whose domain is warped by
    two noise fields (so the filaments curve instead of running straight like tile joints) and whose middle coordinate
    is time (so the cells change shape, as sunlight through moving waves does); thin bright edges, patchy in strength,
    masked to surfaces that face up. (Multiply and add take registers only: constants go through a `constant` op.
    Eight registers.)"""
    return {
        "name": name,
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            # the warp: (w n1, 0, w n2)
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.9, "seed": 11},
            {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.9, "seed": 12},
            {"kind": "constant", "dst": 3, "constant": [float(warp), 0.0, 0.0, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
            {"kind": "constant", "dst": 3, "constant": [0.0, 0.0, float(warp), 0.0]},
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
            # (x, 0, z) + warp + (0, speed t, 0)
            {"kind": "constant", "dst": 2, "constant": [1.0, 0.0, 1.0, 0.0]},
            {"kind": "multiply", "dst": 2, "srcA": 0, "srcB": 2},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 1},
            {"kind": "input", "dst": 1, "input": "time"},
            {"kind": "constant", "dst": 3, "constant": [0.0, float(speed), 0.0, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 1},
            {"kind": "voronoiEdge", "dst": 3, "srcA": 2, "value": float(scale), "seed": 3},
            {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.07, 0.0, 0.0, 0.0]},
            {"kind": "swizzle", "dst": 3, "srcA": 3, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "power", "dst": 3, "srcA": 3, "value": 1.6},
            # patchy strength: a slow large noise drifting through time (r2 = (x', st, z') scaled down)
            {"kind": "noise", "dst": 4, "srcA": 2, "value": 0.09, "seed": 7},
            {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.3, 0.75, 0.0, 0.0]},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},
            # up-facing only
            {"kind": "input", "dst": 4, "input": "normal"},
            {"kind": "swizzle", "dst": 4, "srcA": 4, "constant": [1.0, 1.0, 1.0, 1.0]},
            {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.3, 0.9, 0.0, 0.0]},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},
            {"kind": "constant", "dst": 4, "constant": hexrgb("#e9fbff") + [1.0]},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},
            # the ground's own colour, mottled
            {"kind": "noise", "dst": 5, "srcA": 0, "value": 0.35, "seed": 5},
            {"kind": "ramp", "dst": 5, "srcA": 5, "constant": hexrgb("#9c8b68") + [1.0],
             "constant2": hexrgb(base_hex) + [1.0], "constant3": hexrgb("#e0d3b2") + [1.0]},
        ],
        "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": float(strength),
        "opacity": -1,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.18, "skyIntensity": 0.0, "background": hexrgb("#021220"), "fogColor": hexrgb("#0a5a86"),
        "volumeDensity": 0.03, "volumeMaxDistance": 70.0, "volumeSteps": 16, "volumeAnisotropy": 0.55,
        "volumeScattering": 0.8, "volumeAbsorption": 1.0, "volumeLocalLights": 1.0, "fogSky": 1.0,
        "fogSkyDistance": 60.0,
        "sky": {"enabled": True, "background": True, "zenithColor": hexrgb("#2a8fb8"),
                "horizonColor": hexrgb("#073049"), "groundColor": hexrgb("#020a12"), "haze": 0.6,
                "sunIntensity": 0.0, "sunColor": [1, 1, 1], "sunSize": 0.01, "sunGlow": 0.05, "intensity": 1.0,
                "useKeyLight": False},
        "shadowCascades": 2, "shadowRange": 70.0,
    }
    s.program(caustic_program("dcSand", SAND, 1.6))

    # ---- the floor: soft sand dunes, the colonnade's way slightly raised
    s.terrain("seabed", world={
        "seed": 5, "size": [160.0, 160.0], "baseHeight": 0.0,
        "layers": [{"frequency": 0.03, "amplitude": 1.1, "warp": 6.0}, {"frequency": 0.12, "amplitude": 0.25}],
        "features": [{"name": "way", "kind": "flat", "path": [[0.0, 0.1, 8.0], [0.0, 0.1, -50.0]], "width": 14.0,
                      "flatten": 0.6, "falloff": 1.0, "roughness": 0.2, "smoothing": 2}]},
        terrain={"chunkSize": 40.0, "resolution": 40, "viewDistance": 120.0},
        material={"program": "dcSand", "baseColor": hexrgb(SAND), "roughness": 0.95, "metallic": 0.0})

    marble = {"baseColor": hexrgb(MARBLE), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.7,
              "metallic": 0.0}
    # ---- the colonnade: two rows, some whole, some snapped (each its own meshed SDF: static)
    plan = {(-1, 0): 0.0, (-1, 1): 5.2, (-1, 2): 0.0, (-1, 3): 2.4, (-1, 4): 0.0, (-1, 5): 3.6,
            (1, 0): 4.1, (1, 1): 0.0, (1, 2): 0.0, (1, 3): 6.3, (1, 4): 1.4, (1, 5): 0.0}
    k = 0
    for (side, i), broken in plan.items():
        x, z = side * ROW_X, -SPACING * i
        tilt = ((k * 37) % 7 - 3) * 0.5
        s.sdf("col%d" % k, column(7.4, broken, seed=k + 1), bounds_min=[-1.2, -0.2, -1.2],
              bounds_max=[1.2, 8.4, 1.2], mesh=96, shadows=True, material=marble, position=(x, -0.25, z),
              rotation=(tilt, k * 23.0, tilt * 0.6))
        k += 1
    # a fallen column across the way
    s.sdf("fallen", column(7.4, 0.0, seed=40), bounds_min=[-1.2, -0.2, -1.2], bounds_max=[1.2, 8.4, 1.2], mesh=96,
          shadows=True, material=marble, position=(2.6, 0.55, -18.5), rotation=(0.0, 18.0, 86.0))
    # the gate at the end
    s.sdf("gate", gate(), bounds_min=[-4.6, -0.2, -1.4], bounds_max=[4.6, 10.4, 1.4], mesh=128, shadows=True,
          material=marble, position=(GATE[0], -0.4, GATE[2]), rotation=(0.0, 8.0, 0.0))
    # the fallen head, a colossus' face half in the sand
    s.mesh("head", "kenney/city/nature-kit/statue_head.glb", scale=6.5,
           material={"baseColor": hexrgb("#cfc6b3"), "roughness": 0.75, "metallic": 0.0, "emissiveColor": [0, 0, 0],
                     "emissiveIntensity": 0.0},
           transform={"position": [HEAD[0], 1.4, HEAD[2]], "rotation": [-12.0, 55.0, 80.0], "scale": [1, 1, 1]})

    # ---- the light from the surface, its shafts
    s.light("sun", "directional", direction=list(SUN_DIR), color=hexrgb("#bfe8ff"), intensity=8.0,
            castsShadow=True, contactShadow=False, softness=2.0, volumetric=0.6)
    for k, (x, z, w) in enumerate([(-3.0, -7.0, 1.3), (1.8, -14.5, 1.9), (-2.2, -23.0, 1.6), (0.2, -31.5, 2.6)]):
        top = (x - SUN_DIR[0] / -SUN_DIR[1] * 26.0, 26.0, z - SUN_DIR[2] / -SUN_DIR[1] * 26.0)
        s.beam("shaft%d" % k, top, toward=(x, 0.0, z), color=hexrgb("#cdefff"), intensity=10.0, length=None,
               angle=1.5, aperture=w, dust=0.7, dustScale=0.8, falloff=0.3, endFade=0.4, depthFade=1.5)

    # ---- camera: low along the row, the gate on the left third
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 3, "post/bloom/intensity": 0.2,
               "post/bloom/threshold": 1.2, "post/output/vignette": 0.4, "post/output/grain": 0.02})
    tgt = kit.aim(CAM, [GATE[0], 3.2, GATE[2]], 0.40, 0.46, LENS)
    s.camera = {"mode": 1, "position": list(CAM), "target": tgt, "fov": 50.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=CAM, target=tgt, period=70.0, amp=(0.5, 0.25, 0.6), tamp=(0.4, 0.2, 0.0))
    return s
