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
import json, math, os

HERE = os.path.dirname(os.path.abspath(__file__))


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
E = direction(-6.0, 6.0)                    # toward the Earth: ahead, over the basin
EXIT = [0.0, 0.0, 1500.0]
CURVATURE = 150000.0                        # the ground's fall (ADR-1887): a gentle sun gradient across the route
# 3.2 degrees across, 9 km out (a node's position is capped at 10 km and its scale at 100: make_earth.py's sphere is 100 m)
EARTH_DISTANCE, EARTH_RADIUS = 9000.0, 250.0
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
# M1: the portal crater P sits inside the dark basin near its east rim, lit by a dome beside it; the portal at its floor
PORTAL = [300.0, 0.0, 650.0]
BASIN = (0.0, 0.0, 2200.0)
# the domes (approved look: 02-crater-domes-in-the-dark): crater, dome scale (x 100 m), the dome's light
DOMES = [((-320.0, -480.0, 520.0), (2.0, 1.7), 60000.0),
         ((-760.0, -160.0, 700.0), (2.8, 2.2), 80000.0),
         ((260.0, -860.0, 600.0), (2.4, 2.0), 70000.0)]
# the dome craters' floor heights, measured on the generated ground (a node's y is absolute)
DOME_FLOOR = {(-320.0, -480.0): -134.5, (-760.0, -160.0): -131.6, (260.0, -860.0): -108.5, (540.0, 520.0): -91.8}
GOLD = [0.85, 0.75, 0.45]                 # the lunar scan's cooler gold
# Gate C 2: the canyon-side exit is in a canyon wall, as portal 1 is (owner). Site B of the candidates
# (portals-c2/00-exit-candidates): the east wall, 35 m up a 56-degree face over the river, facing down-canyon; 200 m of
# open water straight ahead, the far walls in the last light. The portal turns the floor (axis down) onto that wall:
# a camera dropping into the floor with its up pointing ahead (-z) leaves the wall level, its up the world's.
DUSK_OUT = [-0.426, 0.0, -0.905]          # out of the wall (the wall's downhill normal)
DUSK_EXIT = [166.5, 46.0, 3427.3]       # 10 m above the wall's face where it meets the plane (35.9 m)


def canyon(d, lat):
    """A point d metres out of the wall and lat to the right of that line (x, z)."""
    return [166.5 - 0.426 * d + 0.905 * lat, 3427.3 - 0.905 * d - 0.426 * lat]


# after the exit (crossing 67.70 s, bar 35): out over the water, swinging left so the camp on the terrace to the right
# passes 50-80 m off, then down the canyon and climbing toward the set-back wall that M2 opens (heights above the floor)
DUSK_FLIGHT = [[*canyon(d, lat)[:1], h, canyon(d, lat)[1], t] for d, lat, h, t in
               ((30, -5, 18, 69.4), (58, 10, 9, 71.6), (72, 23, 6, 73.6), (95, 18, 8, 75.6), (135, 5, 15, 77.4), (190, -10, 22, 79.0),
               (260, 20, 28, 81.0), (350, 60, 38, 83.6), (430, 110, 55, 85.8))]
EGLOW = [-c for c in direction(-6, 50)]
CAMP = canyon(75, 45)                     # the terrace right of the exit, about 5 m over the water
DUSK_AIMS = [[CAMP[0], 4.2, CAMP[1], 69.4, 75.0]]
bounce = unit([S[0], 0.3, S[2]])
for (cx, cz, _), _, _ in DOMES:
    DOME_FLOOR.setdefault((cx, cz), 0.0)
earth = [EXIT[0] + E[0] * EARTH_DISTANCE, 40.0 + E[1] * EARTH_DISTANCE, EXIT[2] + E[2] * EARTH_DISTANCE]
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
        [{"name": f"dome{k}", "type": "point", "position": [c[0], DOME_FLOOR[(c[0], c[1])] + 60.0 * sc[1], c[1]],
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
                   "heroes": [{"x": 40, "z": 1320, "diameter": 140, "fresh": 1.0},
                              {"x": BASIN[0], "z": BASIN[1], "diameter": BASIN[2], "fresh": 0.12},
                              {"x": PORTAL[0], "z": PORTAL[2], "diameter": 380, "fresh": 0.75}] +
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
               "rotation": [0, 200, 23], "scale": [EARTH_RADIUS / EARTH_FILE_RADIUS] * 3, "visible": True}] +
             [{"name": f"dome{k}", "kind": "gltf", "asset": "../../assets/strange-attractor/proofs/dome.glb",
               "position": [c[0], DOME_FLOOR[(c[0], c[1])] - 12.0, c[1]], "rotation": [0, 37 * k, 0],
               "scale": [sc[0], sc[1], sc[0]], "visible": True} for k, (c, sc, _) in enumerate(DOMES)],
}
out = os.path.join(HERE, 'strange-attractor-moon.scene.json')
json.dump(scene, open(out, 'w'), indent=2)
print(out)
