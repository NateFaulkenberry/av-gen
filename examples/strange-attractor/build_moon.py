"""Writes strange-attractor-moon.scene.json: the Strange Attractor's Moon (dimensional portals, Gate B; ADR-1882), the
destination of the canyon's portal (ADR-1879).

    python3 examples/strange-attractor/build_moon.py

The sun is low (9 degrees) from the right of the arrival heading (-z), a little behind, so shadows run long across the
frame and the Earth -- ahead and to the left, about 7 degrees up, 1.9 degrees across -- shows a gibbous face. The
regolith is a lunar-Lambert material program (McEwen 1991: Lommel-Seeliger blended with Lambert by a phase-dependent
weight) with a narrow opposition surge; its sun direction is baked into its ops, so it is written here with the light.
The fill is the regolith's own bounce: weak, warm grey, from the sunlit ground back toward the sun. The scene has no
environment light (a black sky lights nothing). The Earth is NASA's Blue Marble (make_earth.py; assets/space, gitignored).
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


S = direction(95.0, 9.0)                    # toward the sun
E = direction(-22.0, 5.0)                  # toward the Earth
EXIT = [0.0, 0.0, 1500.0]
EARTH_DISTANCE, EARTH_RADIUS = 6000.0, 100.0   # 1.9 degrees at true size; nearer than life because a body much past ~11 km is cut back to a small cap (renderer far-depth limit, not investigated)
EARTH_SCALE_FIX = 1.0  # the earlier "fix" (x10, x25) was chasing that far cut, not a scale bug

ops = [
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
    {"kind": "remap", "dst": 5, "srcA": 4, "constant": [0, 1, 0.15, 1.0]},             # L(phase)
    {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 5},
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
    {"kind": "constant", "dst": 7, "constant": [1.0, 0.96, 0.9, 1.0]},                 # a faintly warm grey
    {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
    {"kind": "constant", "dst": 0, "constant": [0, 0, 0, 0]},
    {"kind": "constant", "dst": 1, "constant": [0.97, 0.97, 0.97, 0.97]},
]
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
                    "shadowCascades": 4, "ambientOcclusion": False, "shadowCasterReach": 6000.0, "contactShadows": False},
    "post": {"bloomIntensity": 0.04, "tonemap": 0},
    "materialPrograms": [{"name": "regolith", "ops": ops, "baseColor": 6, "metallic": 0, "roughness": 1}],
    "lunarWorld": {"seed": 3, "extent": 4000, "spacing": 2.5, "skirtExtent": 32000, "skirtSpacing": 32, "craters": 3.0,
                   "rocks": 0.01, "relief": 60, "program": "regolith",
                   "heroes": [{"x": -150, "z": 500, "diameter": 950, "fresh": 0.95},
                              {"x": 650, "z": -400, "diameter": 420, "fresh": 0.8}]},
    "nodes": [{"name": "earth", "kind": "gltf", "asset": "../../assets/space/earth.glb", "position": earth,
               "rotation": [0, 200, 23], "scale": [EARTH_RADIUS * EARTH_SCALE_FIX] * 3, "visible": True}],
}
out = os.path.join(HERE, 'strange-attractor-moon.scene.json')
json.dump(scene, open(out, 'w'), indent=2)
print(out)
