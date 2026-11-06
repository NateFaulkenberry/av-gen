"""Writes strange-attractor-moon.scene.json: the Strange Attractor's Moon (dimensional portals, Gates B and C; ADR-1882,
1883), the destination of the canyon's portal (ADR-1879) and the source of the crater-floor portal to the dusk canyon.

    python3 examples/strange-attractor/build_moon.py

The sun is 24 degrees up from the right-behind of the arrival heading (-z), so shadows run forward-left and the crater
floors on the route are lit; the Earth -- ahead and to the upper left, 3.2 degrees across -- shows a gibbous face. The
regolith is a lunar-Lambert material program (McEwen 1991: Lommel-Seeliger blended with Lambert by a phase-dependent
weight) with a narrow opposition surge, plus micro-relief shaded along the sun (grit, lumps and craterlets); its sun
direction is baked into its ops, so it is written here with the light. The fill is the regolith's own bounce: weak,
warm grey. The scene has no environment light (a black sky lights nothing). The Earth is NASA's Blue Marble
(make_earth.py; assets/space, gitignored). At Gate C the hero crater's floor carries the portal to the dusk canyon.
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from canyon_scale import GAZE, WORLD_SCALE as WS, R, canyon_frame, site, knot, pace, O_UNIT, L_UNIT, line_points  # noqa: E402  (ADR-1893)


def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def direction(az_deg, el_deg):
    """Toward a point at azimuth az (degrees, 0 = -z, the arrival heading; positive to the right) and elevation el."""
    az, el = math.radians(az_deg), math.radians(el_deg)
    return [math.cos(el) * math.sin(az), math.sin(el), -math.cos(el) * math.cos(az)]


# Art Pass 2, M1: the sun is just over the horizon behind the arrival (2.2 degrees), so the plain at the exit is lit
# with long shadows and the great basin ahead lies in its rim's shadow -- the night side, reached by flying into it
S = direction(175.0, 2.2)                   # toward the sun
E = direction(-4.0, 10.0)                   # toward the Earth: ahead, over the basin; M3b: higher (a bigger disc clears
                                            # the horizon) and nearly dead ahead, the first thing out of the portal
EXIT = [0.0, 0.0, 1500.0]
# M1b: a tighter fall (ADR-1887) than M1's 150 km. The plain at the exit tilts toward the low sun behind and is lit;
# a few kilometres ahead the ground has turned past the terminator into night, below the horizon from low down; the
# domes stand out there and come over the horizon only as the camera rises
CURVATURE = 25000.0
# 3.2 degrees across, 9 km out (a node's position is capped at 10 km and its scale at 100: make_earth.py's sphere is 100 m)
EARTH_DISTANCE, EARTH_RADIUS = 9000.0, 950.0  # M3b: 12 degrees across (owner: bigger, brighter, noticed first)
RAVE_GROUPS = ("m", "c", "g", "a", "v")
EARTH_FILE_RADIUS = 100.0  # make_earth.py's sphere (a node's scale is capped at 100: the Gate B 'far cut' was that cap)

SH = unit([S[0], 0.0, S[2]])   # the sun's direction along the ground


def toward_sun(f, e):
    """The op constant that samples a noise of frequency f at e metres toward the sun (p * f + k)."""
    return [SH[0] * e * f, 0.0, SH[2] * e * f, 0.0]


G_MICRO = 1.4     # how hard the micro-relief shades (a derivative over a few cm, divided by mu0: grazing sun = gritty)
micro = [
    # Moon polish: micro-relief the mesh cannot carry, shaded by its slope along the sun (a directional derivative:
    # the noise a few cm toward the sun minus here, over mu0). Grit at 33 cm, lumps at 2 m, and craterlets: Worley
    # bowls on a 2.5 m lattice (3-D cells, so their sections come in every size), faded out by 90 m.
    {"kind": "input", "dst": 7, "input": "worldPosition"},
    {"kind": "microDetail", "dst": 6, "srcA": 7, "value": 3.0, "seed": 11},
    {"kind": "microDetail", "dst": 5, "srcA": 7, "value": 3.0, "seed": 11, "constant": toward_sun(3.0, 0.06)},
    {"kind": "remap", "dst": 6, "srcA": 6, "constant": [0, 1, 0, -1]},
    {"kind": "add", "dst": 5, "srcA": 5, "srcB": 6},
    {"kind": "microDetail", "dst": 6, "srcA": 7, "value": 0.5, "seed": 12},
    {"kind": "microDetail", "dst": 4, "srcA": 7, "value": 0.5, "seed": 12, "constant": toward_sun(0.5, 0.3)},
    {"kind": "remap", "dst": 6, "srcA": 6, "constant": [0, 1, 0, -1]},
    {"kind": "add", "dst": 4, "srcA": 4, "srcB": 6},
    {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0, 1, 0, 0.4]},
    {"kind": "add", "dst": 5, "srcA": 5, "srcB": 4},
    {"kind": "voronoi", "dst": 6, "srcA": 7, "value": 0.4, "seed": 13},
    {"kind": "smoothstep", "dst": 6, "srcA": 6, "constant": [0.0, 0.3, 0, 0]},
    {"kind": "voronoi", "dst": 4, "srcA": 7, "value": 0.4, "seed": 13, "constant": toward_sun(0.4, 0.12)},
    {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.0, 0.3, 0, 0]},
    {"kind": "remap", "dst": 6, "srcA": 6, "constant": [0, 1, 0, -1]},
    {"kind": "add", "dst": 4, "srcA": 4, "srcB": 6},
    {"kind": "input", "dst": 6, "input": "cameraDistance"},
    {"kind": "remap", "dst": 6, "srcA": 6, "constant": [30, 90, 0.5, 0], "value": 1.0},
    {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 6},
    {"kind": "add", "dst": 5, "srcA": 5, "srcB": 4},                                   # r5 = D
]
ops = micro + [
    {"kind": "input", "dst": 0, "input": "normal"},
    {"kind": "gradient", "dst": 1, "srcA": 0, "constant": S + [0], "value": 1.0},      # mu0
    {"kind": "fresnel", "dst": 2, "value": 1.0},                                       # 1 - mu
    {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0, 1, 1, 0]},                  # mu
    {"kind": "add", "dst": 3, "srcA": 1, "srcB": 2},
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, 0.03, 1.03]},
    {"kind": "power", "dst": 3, "srcA": 3, "value": -1.0},                             # 1 / (mu0 + mu)
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, -1, 1]},                 # 2 / (mu0 + mu) - 1
    {"kind": "input", "dst": 4, "input": "viewDirection"},
    {"kind": "gradient", "dst": 4, "srcA": 4, "constant": S + [0], "value": 1.0},      # cos(phase), 0 past 90 degrees
    {"kind": "remap", "dst": 2, "srcA": 4, "constant": [0, 1, 0.15, 1.0]},             # L(phase)
    {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 2},
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, 1, 2]},                  # 1 + L (2/(mu0+mu) - 1)
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 4, 0, 4], "value": 1.0},
    {"kind": "power", "dst": 4, "srcA": 4, "value": 150.0},
    {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0, 1, 1, 1.8]},                # opposition surge
    {"kind": "input", "dst": 6, "input": "uv"},
    {"kind": "swizzle", "dst": 6, "srcA": 6, "constant": [0, 0, 0, 0]},                # albedo (uv.x)
    {"kind": "input", "dst": 7, "input": "worldPosition"},
    {"kind": "microDetail", "dst": 7, "srcA": 7, "value": 1.3, "seed": 5},
    {"kind": "remap", "dst": 7, "srcA": 7, "constant": [0, 1, 0.8, 1.2]},
    {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
    {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 3},
    {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 4},
    {"kind": "remap", "dst": 7, "srcA": 1, "constant": [0.22, 1, 0.22, 1], "value": 1.0},
    {"kind": "power", "dst": 7, "srcA": 7, "value": -1.0},                             # 1 / mu0
    {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 5},
    {"kind": "remap", "dst": 7, "srcA": 7, "constant": [0, 1, 1, 1 - G_MICRO]},        # 1 - g D / mu0
    {"kind": "remap", "dst": 7, "srcA": 7, "constant": [0.55, 1.5, 0.55, 1.5], "value": 1.0},
    {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
    {"kind": "constant", "dst": 7, "constant": [1.0, 0.96, 0.9, 1.0]},                 # a faintly warm grey
    {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
    {"kind": "constant", "dst": 0, "constant": [0, 0, 0, 0]},
    {"kind": "constant", "dst": 1, "constant": [0.97, 0.97, 0.97, 0.97]},
]
# M1b: a short Moon (owner). The camera pops out onto the sunlit plain with the Earth ahead, rises over the near rim
# of the great basin -- the domes glimpsed far across its night -- and dives into the portal crater P on the plain,
# just short of the rim
PORTAL = [0.0, 0.0, 1270.0]
BASIN = (0.0, 0.0, 2200.0)
# the domes (approved look: 02-crater-domes-in-the-dark): crater, dome scale (x 100 m), the dome's light
# M1b: in the great basin's night, as at M1 -- below the near rim's line from low down, revealed whole once the camera
# rises over the rim
DOMES = [((-320.0, -480.0, 520.0), (2.0, 1.5), 60000.0),
         ((-760.0, -160.0, 700.0), (2.8, 1.5), 80000.0),
         ((260.0, -860.0, 600.0), (2.4, 1.5), 70000.0)]
# the dome craters' floor heights, measured on the generated ground (a node's y is absolute)
DOME_FLOOR = {(-320.0, -480.0): -140.0, (-760.0, -160.0): -141.5, (260.0, -860.0): -122.0}  # measured (M1b ground)
DOME_SINK = {(-320.0, -480.0): 12.0, (-760.0, -160.0): 12.0, (260.0, -860.0): 12.0}
GOLD = [0.85, 0.75, 0.45]                 # the lunar scan's cooler gold
# Gate C 2: the canyon-side exit is in a canyon wall, as portal 1 is (owner). Site B of the candidates
# (portals-c2/00-exit-candidates): the east wall, 35 m up a 56-degree face over the river, facing down-canyon; 200 m of
# open water straight ahead, the far walls in the last light. The portal turns the floor (axis down) onto that wall:
# a camera dropping into the floor with its up pointing ahead (-z) leaves the wall level, its up the world's.
# M3e (owner §7): the arrival moves down the river to the FAR bank, opposite the camp -- out of the far wall's face
# (d 40, lat -53.5: its foot at lat -48, 2.7 m; its top at lat -64, 26.7 m; the face 12 m up here), facing across the
# river and a little up-canyon, toward the camp
_out = (-0.33 * O_UNIT[0] + 0.94 * L_UNIT[0], -0.33 * O_UNIT[1] + 0.94 * L_UNIT[1])
_on = math.hypot(*_out)
DUSK_OUT = [_out[0] / _on, 0.0, _out[1] / _on]
DUSK_EXIT = [site(40.0, -53.5)[0], 12.0, site(40.0, -53.5)[1]]   # absolute (the root's exit y is absolute)


# M1b: the camp is on the low bank right of the exit, 16 m out and 16 m over (real metres; it was the M1 bank's
# neighbour before the world was scaled down). Crossing 2 at 47.87 s (bar 25). Out of the wall and down over the camp:
# a slow arc a few metres off the fire, the eye on the Sasquatches, then away down the canyon (heights above the floor;
# the next crossing, M2's, is planned on bar 36 = 69.69 s)
# M3b: placements are sites on the terrain (authored at the current canyon's scale, following the terrain at any
# scale) or, near the camp, real metres from it (canyon_scale.site, near, knot)
CAMP = site(16.0, 16.0)
CAMP_GROUND = 4.4 * WS                    # measured on the bank (generator 4.4 m at d 40, lat 40)
# M3e (owner §7): one smooth, continuous path -- out of the far wall, across the river, BY the camp on its river side
# (8-10 m from the fire, low; no circling, no lingering), then up over the benches to the outer cliff's portal. The
# last two knots are the crossing's, as before (95 m absolute at the face). Times from a smooth speed profile.
# M3g (54- §1): it leaves the exit at the dive's own speed (30 of the 46 m/s it arrives with), easing down, so the line
# does not overshoot its first knot and swing (the arrival spiked 83 degrees between the look and the travel)
# M3g (54- §1, the 50.2 s spike: a sharp corner low over the water at (19, 5), where the look led the turn by 28
# degrees): a rounder arc through the same pass, a little higher
_DUSK_PATH = [(36, -44, 9.0), (29, -27, 6.5), (23, -11, 5.0), (17, 1, 4.0), (11, 9, 3.6), (5, 16, 5.0), (0, 26, 12.0),
              (-4, 42, 30.0), (-6, 58, 52.0), (-6, 72, 50.0), (-7, 82, 34.0)]
_dk = [knot(d, lat, h, 0.0) for d, lat, h in _DUSK_PATH]
# Pass 3 (61- §3, the A-to-B rule; owner OK 2026-11-05): one gentle curve from the exit to the outer cliff's portal,
# bowed 30 m to the river's side so the gathering passes about 15 m off (it ran within 3 m on the straight chord),
# low past the camp, then up over the benches early and high so the mound below the cliff stays under the frame
# (it filled the frame at 52-62 s), at an even pace; the last two knots are the crossing's, as before
_A, _B = (_dk[0][0], _dk[0][2]), (_dk[-1][0], _dk[-1][2])
_mx, _mz = (_A[0] + _B[0]) / 2.0, (_A[1] + _B[1]) / 2.0
_L = math.hypot(_B[0] - _A[0], _B[1] - _A[1])
_C = (_mx - (_B[1] - _A[1]) / _L * 30.0, _mz + (_B[0] - _A[0]) / _L * 30.0)
# (owner 2026-11-06: no tilt out of the portal: level at the exit's 12 m, a long shallow settle toward the camp)
# heights as a smooth line in absolute terms (heights over the ground passed the benches' steps into the look's pitch)
_dusk_xz = line_points(_A, _B, [(0.0, 0.0), (1.0, 0.0)], n=14, control=_C)
from sample_ground import heights as _heights  # noqa: E402
_dg = _heights("strange-attractor-flythrough.scene.json", [(p[0], p[2]) for p in _dusk_xz])
# the highest ground under the pass, sampled finely (the bank's lip stands between the knots)
_fine = line_points(_A, _B, [(0.0, 0.0), (1.0, 0.0)], n=70, control=_C)
_gpass = max(_heights("strange-attractor-flythrough.scene.json", [(p[0], p[2]) for p in _fine[10:32]]))
_ya = line_points((0.0, 0.0), (1.0, 0.0), [(0.0, 12.0), (0.35, _gpass + 4.5), (1.0, _dg[-1] + 34.0)], n=14)
_dusk_abs = [(p[0], max(y[1], g + 3.0), p[2]) for p, y, g in zip(_dusk_xz, _ya, _dg)]
DUSK_FLIGHT = [[k[0], k[1] - g, k[2], k[3]] for k, g in zip(pace([list(p) for p in _dusk_abs], 48.35, 68.0, 24.0, 9.0, ramp_in=0.08, ramp_out=0.1)[0], _dg)] + \
    [knot(-8, 89, 21.2, 69.2), knot(-8, 96, -39.2, 70.4)]
EGLOW = [-c for c in direction(-6, 50)]
CLIFF_PORTAL = site(-8.0, 91.8)
bounce = unit([S[0], 0.3, S[2]])
for (cx, cz, _), _, _ in DOMES:
    DOME_FLOOR.setdefault((cx, cz), 0.0)
earth = [EXIT[0] + E[0] * EARTH_DISTANCE, 40.0 + E[1] * EARTH_DISTANCE, EXIT[2] + E[2] * EARTH_DISTANCE]
ECHO_SIDE = unit([-E[2], 0.0, E[0]])        # sideways across the line of sight (the echoes' offset)
# the halo's plane faces back along the line of sight: its +y (the glb's face) turned onto -E (x first, then y)
# M3e: a card set d Earth radii behind the planet is seen smaller (farther); scaled back so its 1.0 R is the limb as seen
def AURA_K(d):
    return EARTH_RADIUS / 10.0 * (EARTH_DISTANCE + d * EARTH_RADIUS) / EARTH_DISTANCE


HALO_ROT = [math.degrees(math.acos(-E[1])), math.degrees(math.atan2(-E[0], -E[2])), 0.0]
scene = {
    "format": "avgen-scene", "version": 1, "name": "the-strange-attractor-moon",
    "_note": "The Strange Attractor's Moon (dimensional portals, Gate B; ADR-1882), written by build_moon.py; the destination of the canyon's portal (ADR-1879), whose 'portal' block drives its camera.",
    "camera": {"mode": 1, "position": [0, 60, 1500], "target": [0, 40, 0], "fov": 44.0, "orbitSpeed": 0.0, "far": 150000.0},
    "lights": [
        {"name": "sun", "type": "directional", "role": "key", "direction": [-c for c in S], "color": [1.0, 0.97, 0.93],
         "intensity": 5.0, "castsShadow": True, "shadowStrength": 1.0, "softness": 0.15},
        {"name": "regolith-bounce", "type": "directional", "role": "fill", "direction": bounce, "color": [1.0, 0.96, 0.9],
         "intensity": 0.08, "castsShadow": False, "shadowStrength": 0.0},
        # earthshine: the near side's night is lit faintly blue-white by the Earth (02-proofs/03)
        {"name": "earthshine", "type": "directional", "role": "fill", "direction": [-c for c in E], "color": [0.62, 0.72, 1.0],
         "intensity": 0.6, "castsShadow": False, "shadowStrength": 0.0},
        # M1: the Earth's glow as a high, weak fill, so the basin floor's relief reads in its night (a grazing
        # earthshine alone leaves a flat floor black)
        {"name": "earthglow", "type": "directional", "role": "fill", "direction": EGLOW, "color": [0.62, 0.72, 1.0],
         "intensity": 0.09, "castsShadow": False, "shadowStrength": 0.0}] +
        # M3b/M3c: the rave's spill -- three coloured lights at each dome's foot, just outside its glass, flashed on the
        # beat with the windows (routed in the project; a low base glow with audio off)
        [{"name": f"rave{k}-{nm}", "type": "point",
          "position": [c[0] + dx * sc[0] * 112.0, DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])] + 14.0 + 6.0 * sc[1], c[1] + dz * sc[0] * 112.0],
          "color": col, "intensity": 2500.0, "range": 420.0, "castsShadow": False, "volumetric": 0.0}
         for k, (c, sc, _) in enumerate(DOMES)
         for nm, col, dx, dz in (("m", [1.0, 0.15, 0.8], 1.0, 0.0), ("c", [0.15, 0.85, 1.0], -0.5, 0.86), ("g", [0.3, 1.0, 0.25], -0.5, -0.86))] +
        [{"name": f"dome{k}", "type": "point", "position": [c[0], DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])] + 60.0 * sc[1], c[1]],
          "color": [1.0, 0.74, 0.45], "intensity": I, "range": 900.0, "castsShadow": False}
         for k, (c, sc, I) in enumerate(DOMES)],
    "environment": {"intensity": 0.0, "skyIntensity": 0.0, "fallbackAmbient": False, "background": [0.0, 0.0, 0.0], "fogColor": [0, 0, 0],
                    "volumeDensity": 0.0, "skybox": False, "sky": {"enabled": False}, "shadowRange": 2500.0,
                    "shadowCascades": 4, "ambientOcclusion": False, "shadowCasterReach": 6000.0, "contactShadows": False, "shadowSplitLambda": 0.92},
    "post": {"bloomIntensity": 0.04, "tonemap": 1},
    "materialPrograms": [{"name": "regolith", "ops": ops, "baseColor": 6, "metallic": 0, "roughness": 1}],
    "lunarWorld": {"seed": 3, "extent": 4000, "spacing": 2.5, "skirtExtent": 32000, "skirtSpacing": 32, "craters": 3.0,
                   "rocks": 0.02, "relief": 60, "program": "regolith", "curvature": CURVATURE,
                   # M1: a fresh crater beside the first glide (B); the great old basin whose near rim casts the night
                   # over it; the portal crater P inside it; the dome craters
                   "heroes": [{"x": BASIN[0], "z": BASIN[1], "diameter": BASIN[2], "fresh": 0.12},
                              {"x": PORTAL[0], "z": PORTAL[2], "diameter": 260, "fresh": 0.75,
                               "clear": 125.0}] +   # owner 2026-11-06 (ADR-1912): no blob where the portal opens
                             [{"x": c[0], "z": c[1], "diameter": c[2], "fresh": 0.45} for c, _, _ in DOMES]},
    # Gate C (ADR-1883): the crater-floor portal. The floor of P cracks and opens under the diving camera onto the
    # canyon at dusk, far below; the same corruption as the canyon's wall (focus y is a height above the ground here),
    # turned to face down, in a cooler gold, with no strata slide (uv.x is the regolith's albedo).
    "corruption": {"targets": ["lunar.surface"], "focus": PORTAL, "axis": [0.0, -1.0, 0.0], "amount": 1.0,
                   "radius": 60.0, "inner": 0.25, "fieldNear": 50.0, "fieldFar": 450.0, "openNear": 30.0, "openFar": 150.0,
                   "seam": 0.3, "width": 46.0, "height": 40.0, "depth": 10.0, "block": 5.0, "share": 0.18, "slip": 2.5,
                   "rate": 0.5, "strip": 4.0, "stripSlip": 1.5, "slide": 0.0, "pull": 6.0, "lift": 1.2, "fold": 0.8,
                   "lag": 0.7, "glyph": 5.0, "glyphSize": 1.8, "glow": 4.0, "echoes": 0, "echoOffset": 1.0,
                   "glowColor": GOLD, "light": 1800.0},
    "meshWire": {"targets": ["lunar.surface"], "color": [0.75, 0.62, 0.34], "intensity": 0.0, "width": 1.0, "creaseLow": 2.0,
                 "creaseHigh": 12.0, "reveal": 1.0, "fadeNear": 60.0, "fadeFar": 900.0, "minPixels": 14.0, "wave": 0.0,
                 "waveRadius": 300.0, "waveWidth": 40.0, "waveGain": 1.6, "waveAhead": 200.0, "clipBelow": -1000.0,
                 "fromFocus": 1.0, "pulse": 1.2, "pulseSpacing": 60.0, "pulseSpeed": 40.0, "pulseWidth": 6.0,
                 "focusGain": 0.5, "rimGain": 6.0, "frontColor": [0.95, 0.88, 0.62], "shards": 0.25, "shardRate": 0.35},
    # ADR-1885: back to the root -- the canyon, at dusk (its look from crossing 1)
    "portal": {**GAZE, "climbLook": 0.35,   # 65- §1: no pitch swing out of the arrival; the climb to the outer cliff is not looked at in full
               "world": "@root", "exit": DUSK_EXIT, "exitAxis": DUSK_OUT,
               "entryUp": [1.0, 0.0, 0.0], "exitUp": [0.0, 1.0, 0.0], "flight": DUSK_FLIGHT},
    "nodes": [{"name": "earth", "kind": "gltf", "asset": "../../assets/space/earth.glb", "position": earth,
               "rotation": [0, 200, 23], "scale": [EARTH_RADIUS / EARTH_FILE_RADIUS] * 3, "visible": True,
               "emissiveBoost": 2.0}] +
             # M3b: the datamosh -- two copies a step out of register, red and cyan, flashed on the beat (their
             # opacity routed in the project; 0 between beats and with audio off)
             [{"name": f"earth-echo-{c}", "kind": "gltf", "asset": "../../assets/space/earth.glb",
               "position": [earth[i] + off * ECHO_SIDE[i] for i in range(3)], "rotation": [0, 200 + rot, 23],
               "scale": [EARTH_RADIUS * 1.015 / EARTH_FILE_RADIUS] * 3, "visible": True, "tint": tint,
               "emissiveBoost": 3.0} for c, off, rot, tint in (("r", 70.0, 4.0, [1.0, 0.15, 0.3]), ("c", -70.0, -4.0, [0.2, 0.9, 1.0]))] +
             # M3e (owner §6: the ring of white pixel blocks replaced): a glowing aura (pass2/build_earth_aura.py) set
             # just behind the planet, facing the arrival -- soft, hugging the limb, falling off smoothly -- its
             # emissive pulsed on the beat (routed, as the M3c halo's was); and its glitch and datamosh, each layer at 0
             # between beats and with audio off (the project's routes): an RGB split (two tinted copies a step either
             # side), a torn copy (scanline tears and displaced blocks, on the highs) and mosh trails (two larger,
             # fainter copies smeared sideways, on the low onsets, decaying slowly)
             [{"name": "earth-aura", "kind": "gltf", "asset": "../../assets/strange-attractor/build/earth-aura.glb",
               "position": [earth[i] + E[i] * EARTH_RADIUS * 1.2 for i in range(3)], "rotation": HALO_ROT,
               "scale": [AURA_K(1.2)] * 3, "visible": True, "emissiveBoost": 1.3}] +
             [{"name": f"earth-aura-{c}", "kind": "gltf", "asset": "../../assets/strange-attractor/build/earth-aura.glb",
               "position": [earth[i] + E[i] * EARTH_RADIUS * 1.22 + off * ECHO_SIDE[i] for i in range(3)],
               "rotation": HALO_ROT, "scale": [AURA_K(1.22) * 1.01] * 3, "visible": True, "tint": tint,
               "emissiveBoost": 1.4, "opacity": 0.0}
              for c, off, tint in (("r", 55.0, [1.0, 0.2, 0.6]), ("c", -55.0, [0.2, 0.9, 1.0]))] +
             [{"name": "earth-aura-tear", "kind": "gltf", "asset": "../../assets/strange-attractor/build/earth-aura-tear.glb",
               "position": [earth[i] + E[i] * EARTH_RADIUS * 1.18 for i in range(3)], "rotation": HALO_ROT,
               "scale": [AURA_K(1.18)] * 3, "visible": True, "emissiveBoost": 1.6, "opacity": 0.0}] +
             [{"name": f"earth-aura-trail-{k}", "kind": "gltf", "asset": "../../assets/strange-attractor/build/earth-aura.glb",
               "position": [earth[i] + E[i] * EARTH_RADIUS * (1.24 + 0.02 * k) + off * ECHO_SIDE[i] for i in range(3)],
               "rotation": HALO_ROT, "scale": [AURA_K(1.24 + 0.02 * k) * sc] * 3, "visible": True, "emissiveBoost": 0.9,
               "opacity": 0.0} for k, off, sc in ((1, 120.0, 1.06), (2, 260.0, 1.13))] +
             # M3c (owner: no beams into the sky): the rave is inside the domes -- their glass lit in five colours in
             # bands that wind round them, each band on its own beat source (routed), a steady glow with audio off
             [{"name": f"dome{k}-rave-{g}", "kind": "gltf", "asset": f"../../assets/strange-attractor/build/dome-rave-{g}.glb",
               "position": [c[0], DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])], c[1]], "rotation": [0, 37 * k, 0],
               "scale": [sc[0], sc[1], sc[0]], "visible": True, "emissiveBoost": 0.35}
              for k, (c, sc, _) in enumerate(DOMES) for g in RAVE_GROUPS] +
             [{"name": f"dome{k}", "kind": "gltf", "asset": "../../assets/strange-attractor/proofs/dome.glb",
               "position": [c[0], DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])], c[1]], "rotation": [0, 37 * k, 0],
               "scale": [sc[0], sc[1], sc[0]], "visible": True} for k, (c, sc, _) in enumerate(DOMES)],
}
out = os.path.join(HERE, 'strange-attractor-moon.scene.json')
json.dump(scene, open(out, 'w'), indent=2)
print(out)
