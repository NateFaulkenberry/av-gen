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
from canyon_scale import WORLD_SCALE as WS, R, PREFIX, canyon_frame, site, knot  # noqa: E402  (ADR-1893; M3b POC)
from portal_rim import RIM_PARTICLES_MOON  # noqa: E402  (ADR-1898)


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
E = direction(-4.0, 13.0)                   # toward the Earth: ahead, over the basin; M3b: higher (a bigger disc clears
                                            # the horizon) and nearly dead ahead, the first thing out of the portal
EXIT = [0.0, 0.0, 1500.0]
# M1b: a tighter fall (ADR-1887) than M1's 150 km. The plain at the exit tilts toward the low sun behind and is lit;
# a few kilometres ahead the ground has turned past the terminator into night, below the horizon from low down; the
# domes stand out there and come over the horizon only as the camera rises
CURVATURE = 25000.0
# 3.2 degrees across, 9 km out (a node's position is capped at 10 km and its scale at 100: make_earth.py's sphere is 100 m)
EARTH_DISTANCE, EARTH_RADIUS = 9000.0, 950.0  # M3b: 12 degrees across (owner: bigger, brighter, noticed first)
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
DUSK_OUT = [-0.426, 0.0, -0.905]          # out of the wall (the wall's downhill normal)
DUSK_EXIT = [*canyon_frame(0, 0)[:1], 46.0 * WS, canyon_frame(0, 0)[1]]  # the Gate C 2 site, scaled (ADR-1893)


# M1b: the camp is on the low bank right of the exit, 16 m out and 16 m over (real metres; it was the M1 bank's
# neighbour before the world was scaled down). Crossing 2 at 47.87 s (bar 25). Out of the wall and down over the camp:
# a slow arc a few metres off the fire, the eye on the Sasquatches, then away down the canyon (heights above the floor;
# the next crossing, M2's, is planned on bar 36 = 69.69 s)
# M3b: placements are sites on the terrain (authored at the current canyon's scale, following the terrain at any
# scale) or, near the camp, real metres from it (canyon_scale.site, near, knot)
CAMP = site(16.0, 16.0)
CAMP_GROUND = 4.4 * WS                    # measured on the bank (generator 4.4 m at d 40, lat 40)
DUSK_FLIGHT = [knot(d, lat, h, t) for d, lat, h, t in
               ((6, 2, 11, 49.4), (10, 6, 5, 51.2), (12, 9, 2.1, 53.0), (13.5, 11, 1.8, 55.4), (17.5, 10.5, 1.9, 57.8),
                # M2: away from the fire, up and across the benches to the sheer outer cliff behind the camp, where
                # the canyon's second portal opens (its face 80-122 m between lat 91 and 93: the crossing at lat 91.8,
                # 69.69 s, bar 36). Heights over the ground; the last two hold 95 m absolute (73.8 and 134.2 below).
                # up first, almost straight up from the bank, then across at the portal's height to the face
                (17.5, 11.5, 9.0, 59.7), (19, 24, 15.0, 61.0), (19, 31, 36.0, 62.3), (14, 44, 43.0, 63.6), (8, 58, 65.0, 65.2), (2, 70, 56.0, 66.8),
                (-4, 80, 38.0, 68.0), (-8, 89, 21.2, 69.2), (-8, 96, -39.2, 70.4))]
EGLOW = [-c for c in direction(-6, 50)]
CLIFF_PORTAL = site(-8.0, 91.8)
DUSK_AIMS = [[CAMP[0], CAMP_GROUND + 1.5, CAMP[1], 49.0, 59.5],
             [CLIFF_PORTAL[0], 95.0 * R, CLIFF_PORTAL[1], 58.9, 69.3]]   # M2: the eye on the cliff as the tear opens
bounce = unit([S[0], 0.3, S[2]])
for (cx, cz, _), _, _ in DOMES:
    DOME_FLOOR.setdefault((cx, cz), 0.0)
earth = [EXIT[0] + E[0] * EARTH_DISTANCE, 40.0 + E[1] * EARTH_DISTANCE, EXIT[2] + E[2] * EARTH_DISTANCE]
ECHO_SIDE = unit([-E[2], 0.0, E[0]])        # sideways across the line of sight (the echoes' offset)
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
        # M3b: the rave -- strobes in three colours in each dome, flashed on the beat (routed in the project; a low
        # base glow with audio off)
        [{"name": f"rave{k}-{nm}", "type": "point",
          "position": [c[0] + dx * sc[0] * 30.0, DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])] + 45.0 * sc[1], c[1] + dz * sc[0] * 30.0],
          "color": col, "intensity": 9000.0, "range": 1100.0, "castsShadow": False, "volumetric": 0.0}
         for k, (c, sc, _) in enumerate(DOMES)
         for nm, col, dx, dz in (("m", [1.0, 0.15, 0.8], 1.0, 0.0), ("c", [0.15, 0.85, 1.0], -0.5, 0.86), ("g", [0.3, 1.0, 0.25], -0.5, -0.86))] +
        [{"name": f"dome{k}", "type": "point", "position": [c[0], DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])] + 60.0 * sc[1], c[1]],
          "color": [1.0, 0.74, 0.45], "intensity": I, "range": 900.0, "castsShadow": False}
         for k, (c, sc, I) in enumerate(DOMES)],
    "environment": {"intensity": 0.0, "skyIntensity": 0.0, "fallbackAmbient": False, "background": [0.0, 0.0, 0.0], "fogColor": [0, 0, 0],
                    "volumeDensity": 0.0, "skybox": False, "sky": {"enabled": False}, "shadowRange": 2500.0,
                    "shadowCascades": 4, "ambientOcclusion": False, "shadowCasterReach": 6000.0, "contactShadows": False, "shadowSplitLambda": 0.92},
    "post": {"bloomIntensity": 0.04, "tonemap": 0},
    "materialPrograms": [{"name": "regolith", "ops": ops, "baseColor": 6, "metallic": 0, "roughness": 1}],
    "lunarWorld": {"seed": 3, "extent": 4000, "spacing": 2.5, "skirtExtent": 32000, "skirtSpacing": 32, "craters": 3.0,
                   "rocks": 0.02, "relief": 60, "program": "regolith", "curvature": CURVATURE,
                   # M1: a fresh crater beside the first glide (B); the great old basin whose near rim casts the night
                   # over it; the portal crater P inside it; the dome craters
                   "heroes": [{"x": BASIN[0], "z": BASIN[1], "diameter": BASIN[2], "fresh": 0.12},
                              {"x": PORTAL[0], "z": PORTAL[2], "diameter": 260, "fresh": 0.75}] +
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
    "portal": {"world": "@root", "exit": DUSK_EXIT, "exitAxis": DUSK_OUT,
               "entryUp": [1.0, 0.0, 0.0], "exitUp": [0.0, 1.0, 0.0], "flight": DUSK_FLIGHT, "aims": DUSK_AIMS},
    "nodes": [{"name": "earth", "kind": "gltf", "asset": "../../assets/space/earth.glb", "position": earth,
               "rotation": [0, 200, 23], "scale": [EARTH_RADIUS / EARTH_FILE_RADIUS] * 3, "visible": True,
               "emissiveBoost": 2.0}] +
             # M3b: the datamosh -- two copies a step out of register, red and cyan, flashed on the beat (their
             # opacity routed in the project; 0 between beats and with audio off)
             [{"name": f"earth-echo-{c}", "kind": "gltf", "asset": "../../assets/space/earth.glb",
               "position": [earth[i] + off * ECHO_SIDE[i] for i in range(3)], "rotation": [0, 200 + rot, 23],
               "scale": [EARTH_RADIUS * 1.015 / EARTH_FILE_RADIUS] * 3, "visible": True, "tint": tint,
               "emissiveBoost": 3.0} for c, off, rot, tint in (("r", 70.0, 4.0, [1.0, 0.15, 0.3]), ("c", -70.0, -4.0, [0.2, 0.9, 1.0]))] +
             # the glow: a soft additive halo round the Earth, pulsed on the beat (its emissive routed)
             [{"name": "earth-glow", "kind": "particles", "visible": True, "particles": {
                 "enabled": True, "capacity": 24, "seed": 5, "shape": "sphere", "position": earth, "extent": [5.0, 5.0, 5.0],
                 "direction": [0, 1, 0], "spawnRate": 1.2, "burst": 0.0, "lifetimeMin": 14.0, "lifetimeMax": 16.0,
                 "spread": 1.0, "speedMin": 0.0, "speedMax": 0.0, "gravity": [0, 0, 0], "drag": 0.0,
                 "sizeStart": EARTH_RADIUS * 1.55, "sizeEnd": EARTH_RADIUS * 1.55, "sizeVariance": 0.04,
                 "colorStart": [0.3, 0.6, 1.0, 0.09], "colorEnd": [0.3, 0.6, 1.0, 0.09], "emissive": 3.0,
                 "blend": "additive"}}] +
             # the rave in the domes: a laser fan in each, turning (the timeline), flashing (routed)
             [{"name": f"laser{k}", "kind": "gltf", "asset": "../../assets/strange-attractor/build/laser-fan.glb",
               "position": [c[0], DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])] + 35.0 * sc[1], c[1]],
               "rotation": [0, 40 * k, 0], "scale": [7.0 * sc[0]] * 3, "visible": True, "emissiveBoost": 0.1}
              for k, (c, sc, _) in enumerate(DOMES)] +
             [{"name": "crater-rim", "kind": "particles", "visible": True, "portalRim": True, "particles": RIM_PARTICLES_MOON}] +
             [{"name": f"dome{k}", "kind": "gltf", "asset": "../../assets/strange-attractor/proofs/dome.glb",
               "position": [c[0], DOME_FLOOR[(c[0], c[1])] - DOME_SINK[(c[0], c[1])], c[1]], "rotation": [0, 37 * k, 0],
               "scale": [sc[0], sc[1], sc[0]], "visible": True} for k, (c, sc, _) in enumerate(DOMES)],
}
out = os.path.join(HERE, PREFIX + 'strange-attractor-moon.scene.json')
json.dump(scene, open(out, 'w'), indent=2)
print(out)
