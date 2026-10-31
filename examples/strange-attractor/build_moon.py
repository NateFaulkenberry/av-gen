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


S = direction(110.0, 24.0)                  # toward the sun
E = direction(-4.0, 3.0)                    # toward the Earth: ahead, above the hero crater
EXIT = [0.0, 0.0, 1500.0]
EARTH_DISTANCE, EARTH_RADIUS = 40000.0, 1120.0   # 3.2 degrees across (life: 1.9; enlarged for the composition, Moon polish)
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
# Gate C: the portal on P's floor, a little toward the sunlit side; the canyon at dusk beyond it
PORTAL = [-45.0, 0.0, 835.0]
GOLD = [0.85, 0.75, 0.45]                 # the lunar scan's cooler gold
# Gate C 2: the canyon-side exit is in a canyon wall, as portal 1 is (owner). Site B of the candidates
# (portals-c2/00-exit-candidates): the east wall, 35 m up a 56-degree face over the river, facing down-canyon; 200 m of
# open water straight ahead, the far walls in the last light. The portal turns the floor (axis down) onto that wall:
# a camera dropping into the floor with its up pointing ahead (-z) leaves the wall level, its up the world's.
DUSK_OUT = [-0.426, 0.0, -0.905]          # out of the wall (the wall's downhill normal)
DUSK_EXIT = [166.5, 43.0, 3427.3]       # 7 m above the wall's face where it meets the plane (35.9 m)
# after the exit: out over the water, low, drifting right with the river, a slow rise (heights above the canyon floor)
DUSK_FLIGHT = [[166.5 - 0.426 * 45, 26, 3427.3 - 0.905 * 45, 55.3],
               [166.5 - 0.426 * 105 + 0.905 * 5, 20, 3427.3 - 0.905 * 105 - 0.426 * 5, 57.3],
               [166.5 - 0.426 * 165 + 0.905 * 10, 28, 3427.3 - 0.905 * 165 - 0.426 * 10, 59.5],
               [166.5 - 0.426 * 200 + 0.905 * 12, 36, 3427.3 - 0.905 * 200 - 0.426 * 12, 61.0]]
bounce = unit([S[0], 0.3, S[2]])
earth = [EXIT[0] + E[0] * EARTH_DISTANCE, 40.0 + E[1] * EARTH_DISTANCE, EXIT[2] + E[2] * EARTH_DISTANCE]
scene = {
    "format": "avgen-scene", "version": 1, "name": "the-strange-attractor-moon",
    "_note": "The Strange Attractor's Moon (dimensional portals, Gate B; ADR-1882), written by build_moon.py; the destination of the canyon's portal (ADR-1879), whose 'portal' block drives its camera.",
    "camera": {"mode": 1, "position": [0, 60, 1500], "target": [0, 40, 0], "fov": 44.0, "orbitSpeed": 0.0, "far": 150000.0},
    "lights": [
        {"name": "sun", "type": "directional", "role": "key", "direction": [-c for c in S], "color": [1.0, 0.97, 0.93],
         "intensity": 5.0, "castsShadow": True, "shadowStrength": 1.0, "softness": 0.15},
        {"name": "regolith-bounce", "type": "directional", "role": "fill", "direction": bounce, "color": [1.0, 0.96, 0.9],
         "intensity": 0.18, "castsShadow": False, "shadowStrength": 0.0}],
    "environment": {"intensity": 0.0, "skyIntensity": 0.0, "fallbackAmbient": False, "background": [0.0, 0.0, 0.0], "fogColor": [0, 0, 0],
                    "volumeDensity": 0.0, "skybox": False, "sky": {"enabled": False}, "shadowRange": 2500.0,
                    "shadowCascades": 4, "ambientOcclusion": False, "shadowCasterReach": 6000.0, "contactShadows": True, "shadowSplitLambda": 0.92},
    "post": {"bloomIntensity": 0.04, "tonemap": 0},
    "materialPrograms": [{"name": "regolith", "ops": ops, "baseColor": 6, "metallic": 0, "roughness": 1}],
    "lunarWorld": {"seed": 3, "extent": 4000, "spacing": 2.5, "skirtExtent": 32000, "skirtSpacing": 32, "craters": 3.0,
                   "rocks": 0.02, "relief": 60, "program": "regolith",
                   # the route (Moon polish): a fresh crater close beside the glide (B), the rim of an old basin as the
                   # ridge that reveals the hero (C), and the hero crater on the basin's floor (P, Gate C's portal)
                   "heroes": [{"x": 55, "z": 1325, "diameter": 160, "fresh": 1.0},
                              {"x": -20, "z": 785, "diameter": 900, "fresh": 0.15},
                              {"x": -15, "z": 845, "diameter": 400, "fresh": 0.7},
                              {"x": 650, "z": -400, "diameter": 420, "fresh": 0.8}]},
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
    "portal": {"world": "strange-attractor-dusk.scene.json", "exit": DUSK_EXIT, "exitAxis": DUSK_OUT,
               "entryUp": [0.0, 0.0, -1.0], "exitUp": [0.0, 1.0, 0.0], "flight": DUSK_FLIGHT},
    "nodes": [{"name": "earth", "kind": "gltf", "asset": "../../assets/space/earth.glb", "position": earth,
               "rotation": [0, 200, 23], "scale": [EARTH_RADIUS / EARTH_FILE_RADIUS] * 3, "visible": True}],
}
out = os.path.join(HERE, 'strange-attractor-moon.scene.json')
json.dump(scene, open(out, 'w'), indent=2)
print(out)
