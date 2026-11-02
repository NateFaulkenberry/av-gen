"""Writes strange-attractor-ocean.scene.json (the deep ocean, Art Pass 2, M3; M3b: seabed life, fish, five subs, and the
bay portal home to the canyon at dawn -- the owner cut the UFO interior).

    python3 examples/strange-attractor/build_ocean.py

The canyon's riverbed portal (night) opens beneath the ocean. The camera comes out into deep, blue-green water over a
seabed of rock and sediment, the continental shelf's wall rising to the left into the gloom. Ahead, faint at first
through the water, an installation is built into the shelf's foot: lit windows, beacons, a launch bay glowing from
inside. A few submersible discs move in and out of the bay. The camera turns toward the bay and, as it closes, a
portal opens in the bay's energy membrane onto the interior of a UFO.

The seabed is the lunar generator (ADR-1882) with its Mars terms (ADR-1896): the shelf is a giant mesa, the rest a
rolling, rock-strewn floor. The water is the environment's fog, dense and blue-green, over a deep blue background; the
light comes from above, cold and dim. Marine snow drifts through it (particles).

Ocean coordinates: the arrival heads -z; the shelf's wall runs along x = -100; the base stands at its foot.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from canyon_scale import WORLD_SCALE as WS, R, PREFIX, site, pace  # noqa: E402
from sample_ground import heights  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
A = "../../assets/strange-attractor/build/"

WATER = [0.012, 0.075, 0.095]       # the fog: the water's own colour
DEEP = [0.004, 0.022, 0.035]        # the background beyond the fog
LIGHT_DOWN = [0.12, -1.0, 0.18]

# the shelf: a giant mesa whose east wall (x about -100) faces the camera's route
SHELF = dict(x=-800.0, z=-600.0, rx=700.0, rz=1600.0, angle=0.0, height=240.0, terraces=4)
MESAS = [SHELF,
         dict(x=420.0, z=-300.0, rx=60.0, rz=45.0, angle=30.0, height=40.0, terraces=2),   # rock outcrops
         dict(x=260.0, z=-1250.0, rx=90.0, rz=60.0, angle=-20.0, height=55.0, terraces=2),
         dict(x=520.0, z=-800.0, rx=70.0, rz=70.0, angle=10.0, height=35.0, terraces=2)]

# the base at the shelf's foot, its front (+z in the glb) turned to face +x (yaw 90), measured ground below it
BASE_XZ = (-25.0, -880.0)
BASE_GROUND = 50.0                  # the apron at the shelf's foot (measured 44-60 m under the hull)
# the seabed under the bay approach, measured: (x, z) -> metres
GROUND = {(365.0, -785.0): -3.0, (215.0, -870.0): 6.4, (125.0, -880.0): 30.0, (65.0, -880.0): 44.0, (40.0, -880.0): 48.0,
          (300.0, -700.0): 0.0}
BAY_LOCAL = (0.0, 32.0, 90.0)       # the membrane's centre in the glb (y up, front +z)


def base_world(local):
    """A point in the base glb's frame (yaw 90: local +z -> world +x, local +x -> world -z) into the world."""
    lx, ly, lz = local
    return [BASE_XZ[0] + lz, BASE_GROUND + ly, BASE_XZ[1] - lx]


BAY = base_world(BAY_LOCAL)

# the arrival: out into open water, heading -z (the exit's y is over the seabed). M3b: much closer to the base, and the
# flight leaves at the glide's own speed (3.2 m/s), gathers pace smoothly to a cruise and slows into the launch bay
# (owner: the water portal felt like a step). Path in absolute heights, turned into heights over the sampled seabed.
EXIT = [300.0, 22.0, -430.0]
EXIT_AXIS = [0.0, 0.0, -1.0]
OCEAN_PATH = [(300.0, None, -437.0), (298.0, 22.5, -458.0), (293.0, 25.0, -500.0), (282.0, 30.0, -565.0),
              (262.0, 38.0, -645.0), (222.0, 52.0, -735.0), (165.0, 66.0, -812.0), (112.0, 77.0, -862.0),
              (84.0, 81.0, -878.0), (65.0, 82.0, -880.0), (45.0, 82.0, -880.0)]
T_EXIT, T_BAY = 121.2, 147.0


# the ocean flight (x, height over the seabed, z, t): crossing 5 at 121.2 s (bar 62), crossing 6 at 147.0 s (bar 75)
def flight(bay_rel_y=None):
    try:
        ground = heights(PREFIX + "strange-attractor-ocean.scene.json", [(x, z) for x, _, z in OCEAN_PATH])
    except Exception:
        ground = [0.0] * len(OCEAN_PATH)
    pts = []
    for (x, y, z), g in zip(OCEAN_PATH, ground):
        pts.append((x, (g + EXIT[1] if y is None else y), z))
    # the first knot is 2.2 s out at the glide speed; the bay knot is the crossing (147.0), one more knot beyond it
    knots, _ = pace(pts[:-1], T_EXIT + 2.2, T_BAY, 3.4, 9.0, ramp_in=0.3, ramp_out=0.3)
    knots.append([pts[-1][0], pts[-1][1], pts[-1][2], T_BAY + 2.2])
    return [[x, y - g, z, t] for (x, y, z, t), g in zip(knots, ground)]


# the submersibles: three discs (the UFO model, scaled), their paths keyed in the project's timeline (build_journey)
SUBS = [("sub-1", 0.026), ("sub-2", 0.021), ("sub-3", 0.017), ("sub-4", 0.019), ("sub-5", 0.023)]  # 11-17 m discs


def sub_tracks():
    """[(node, [(t, [x, y, z])], [(t, [pitch, yaw, roll])])]: absolute positions. M3c (owner: much faster -- they whiz in
    and out of the base and head up toward the surface): launches out of the bay, a hard climb, gone up into the
    water's gloom in three seconds; returns diving down out of it and into the bay just as fast. Parked inside the
    base (behind the membrane) when not flying."""
    bx, by, bz = BAY
    inside = [bx - 40.0, by, bz]

    def launch(t0, side):
        pos = [(t0 - 0.6, inside), (t0, [bx + 12.0, by + 1.0, bz]), (t0 + 0.7, [bx + 75.0, by + 14.0, bz + side * 10.0]),
               (t0 + 1.5, [bx + 150.0, by + 85.0, bz + side * 45.0]), (t0 + 2.4, [bx + 215.0, by + 280.0, bz + side * 110.0]),
               (t0 + 3.4, [bx + 250.0, by + 650.0, bz + side * 170.0])]
        rot = [(t0 - 0.6, [0.0, 90.0, 0.0]), (t0 + 0.4, [0.0, 90.0, 0.0]), (t0 + 1.4, [0.0, 90.0 - side * 15.0, 32.0]),
               (t0 + 3.4, [0.0, 90.0 - side * 20.0, 55.0])]
        return pos, rot

    def dive(t1, side):
        pos = [(t1 - 3.2, [bx + 280.0, by + 650.0, bz + side * 180.0]), (t1 - 2.2, [bx + 220.0, by + 260.0, bz + side * 110.0]),
               (t1 - 1.3, [bx + 120.0, by + 60.0, bz + side * 35.0]), (t1 - 0.5, [bx + 30.0, by + 4.0, bz + side * 4.0]),
               (t1, inside)]
        rot = [(t1 - 3.2, [0.0, -90.0 + side * 15.0, -45.0]), (t1 - 1.3, [0.0, -90.0 + side * 8.0, -20.0]),
               (t1 - 0.4, [0.0, -90.0, 0.0])]
        return pos, rot

    def park(entries, t_end=150.0):
        """Hold the first and last key from the start and to the end (seek-safe outside the sorties)."""
        pos, rot = entries
        return [(118.0, pos[0][1])] + pos + [(t_end, pos[-1][1])], [(118.0, rot[0][1])] + rot + [(t_end, rot[-1][1])]

    tracks = []
    for name, kind, t, side in (("sub-1", launch, 126.6, 1.0), ("sub-2", dive, 131.2, -1.0), ("sub-3", launch, 135.4, -1.0),
                                ("sub-4", dive, 140.2, 1.0), ("sub-5", launch, 143.4, 1.0)):
        pos, rot = park(kind(t, side))
        tracks.append((name, pos, rot))
    return tracks


def write_ocean(portal_flight):
    bay_y = BAY[1]
    scene = {
        "format": "avgen-scene", "version": 1, "name": "the-strange-attractor-ocean",
        "_note": "The Strange Attractor's deep ocean (Art Pass 2, M3), written by build_ocean.py; reached by the canyon's "
                 "riverbed portal (ADR-1895), left by the launch bay's portal into the UFO.",
        "camera": {"mode": 1, "position": [250, 60, 300], "target": [0, 30, -900], "fov": 44.0, "orbitSpeed": 0.0, "far": 6000.0},
        "lights": [
            {"name": "surface-light", "type": "directional", "role": "key", "direction": LIGHT_DOWN, "color": [0.55, 0.85, 1.0],
             "intensity": 1.1, "castsShadow": True, "shadowStrength": 0.8, "softness": 0.6},
            {"name": "water-fill", "type": "directional", "role": "fill", "direction": [-0.3, 0.2, 0.5], "color": [0.2, 0.55, 0.65],
             "intensity": 0.25, "castsShadow": False, "shadowStrength": 0.0},
            # the bay's light into the water, the base's floods
            {"name": "bay-light", "type": "point", "position": base_world((0.0, 32.0, 100.0)), "color": [1.0, 0.85, 0.65],
             "intensity": 2600.0, "range": 220.0, "castsShadow": False},
            {"name": "flood-l", "type": "point", "position": base_world((-80.0, 50.0, 110.0)), "color": [0.6, 0.85, 1.0],
             "intensity": 3000.0, "range": 160.0, "castsShadow": False},
            {"name": "flood-r", "type": "point", "position": base_world((80.0, 50.0, 110.0)), "color": [0.6, 0.85, 1.0],
             "intensity": 3000.0, "range": 160.0, "castsShadow": False},
        ] + [{"name": f"{name}-light", "type": "point", "position": [0.0, -500.0, 0.0], "color": [0.55, 0.9, 1.0],
              "intensity": 900.0, "range": 60.0, "castsShadow": False} for name, _ in SUBS],
        "environment": {"intensity": 0.18, "background": DEEP, "fogColor": WATER, "volumeDensity": 0.0024, "skybox": False,
                        "sky": {"enabled": False}, "fallbackAmbient": False, "shadowRange": 900.0, "shadowCascades": 3,
                        "ambientOcclusion": False, "shadowCasterReach": 1500.0, "contactShadows": False},
        "post": {"bloomIntensity": 0.06, "tonemap": 0},
        "materialPrograms": [{"name": "seabed", "ops": [
            {"kind": "input", "dst": 0, "input": "uv"},
            {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [0, 0, 0, 0]},
            {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.07, 0.2, 0.6, 1.3], "value": 1.0},
            {"kind": "input", "dst": 2, "input": "worldPosition"},
            {"kind": "microDetail", "dst": 3, "srcA": 2, "value": 0.6, "seed": 7},
            {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, 0.82, 1.15]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
            {"kind": "noise", "dst": 4, "srcA": 2, "value": 0.004, "seed": 71},
            {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0.3, 0.7, 0, 1], "value": 1.0},
            {"kind": "constant", "dst": 5, "constant": [0.16, 0.19, 0.17, 1.0]},     # silt
            {"kind": "constant", "dst": 6, "constant": [0.09, 0.1, 0.1, 1.0]},       # dark rock
            {"kind": "mixBy", "dst": 5, "srcA": 5, "srcB": 6, "srcC": 4},
            {"kind": "input", "dst": 7, "input": "normal"},
            {"kind": "swizzle", "dst": 7, "srcA": 7, "constant": [1, 1, 1, 1]},
            {"kind": "smoothstep", "dst": 7, "srcA": 7, "constant": [0.85, 0.45, 0, 0]},
            {"kind": "constant", "dst": 6, "constant": [0.07, 0.075, 0.08, 1.0]},    # bare cliff
            {"kind": "mixBy", "dst": 5, "srcA": 5, "srcB": 6, "srcC": 7},
            {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 1},
            {"kind": "constant", "dst": 0, "constant": [0, 0, 0, 0]},
            {"kind": "constant", "dst": 1, "constant": [0.9, 0.9, 0.9, 0.9]},
        ], "baseColor": 5, "metallic": 0, "roughness": 1}],
        "lunarWorld": {"seed": 23, "extent": 4000, "spacing": 2.5, "skirtExtent": 24000, "skirtSpacing": 48, "craters": 0.0,
                       "rocks": 0.07, "relief": 55, "program": "seabed", "curvature": 6371.0e3,
                       "heroes": [], "mesas": MESAS, "yardangs": 2.5, "wind": 70.0},
        # the bay portal: the membrane across the launch bay tears open (the corruption's focus y is over the seabed)
        "corruption": {"targets": ["bay-membrane"], "focus": [BAY[0], bay_y - GROUND[(65.0, -880.0)], BAY[2]], "axis": [-1.0, 0.0, 0.0],
                       "amount": 1.0, "radius": 45.0, "inner": 0.2, "fieldNear": 25.0, "fieldFar": 260.0, "openNear": 20.0,
                       "openFar": 130.0, "seam": 0.3, "width": 22.0, "height": 11.0, "depth": 8.0, "block": 2.5,
                       "share": 0.25, "slip": 1.5, "rate": 0.6, "strip": 2.5, "stripSlip": 1.0, "slide": 0.0, "pull": 4.0,
                       "lift": 0.8, "fold": 0.6, "lag": 0.7, "glyph": 6.0, "glyphSize": 1.0, "glow": 4.0, "echoes": 1,
                       "echoOffset": 0.6, "glowColor": [0.55, 0.9, 1.0], "light": 1500.0},
        # M3b (owner: cut the UFO interior): the bay's portal leads home -- out of the canyon wall where the journey
        # began (portal 1, reversed), at dawn
        "portal": {"world": "@root", "exit": HOME_EXIT, "exitAxis": HOME_AXIS, "entryUp": [0.0, 1.0, 0.0],
                   "exitUp": [0.0, 1.0, 0.0], "flight": HOME_FLIGHT},
        "nodes": [
            {"name": "ocean-base", "kind": "gltf", "asset": A + "ocean-base.glb", "position": [BASE_XZ[0], BASE_GROUND, BASE_XZ[1]],
             "rotation": [0, 90, 0], "scale": [1, 1, 1], "visible": True},
            {"name": "bay-membrane", "kind": "gltf", "asset": A + "bay-membrane.glb",
             "position": [BASE_XZ[0], BASE_GROUND, BASE_XZ[1]], "rotation": [0, 90, 0], "scale": [1, 1, 1], "visible": True},
        ] + [{"name": name, "kind": "gltf", "asset": A + "ufo-craft.glb",
              "position": [0.0, -500.0, 0.0], "rotation": [0, 0, 0], "scale": [s, s, s], "visible": True} for name, s in SUBS] + [
            # marine snow: slow, sparse flecks through the water along the route
            {"name": "marine-snow", "kind": "particles", "visible": True, "particles": {
                "enabled": True, "capacity": 26000, "seed": 77, "shape": "box", "position": [120.0, 60.0, -380.0],
                "extent": [360.0, 90.0, 720.0], "direction": [0, -1, 0], "spawnRate": 900.0, "lifetimeMin": 25.0,
                "lifetimeMax": 40.0, "spread": 1.0, "speedMin": 0.02, "speedMax": 0.15, "gravity": [0.05, -0.04, 0.02],
                "drag": 0.2, "turbulence": 0.2, "turbulenceScale": 0.05, "sizeStart": 0.09, "sizeEnd": 0.09,
                "sizeVariance": 0.6, "colorStart": [0.7, 0.85, 0.85, 0.35], "colorEnd": [0.7, 0.85, 0.85, 0.35],
                "emissive": 0.3, "blend": "additive"}},
        ] + life_nodes() + fish_nodes(),
    }
    path = os.path.join(HERE, PREFIX + "strange-attractor-ocean.scene.json")
    json.dump(scene, open(path, "w"), indent=2)
    return path


# --- home (M3b): out of portal 1's wall in the canyon, the way the journey went in, now at dawn: out over the river,
# turning up it past the camp, low along the water, then rising to a high, slowing view as the music ends (210.5 s).
CANYON_SCENE = PREFIX + "strange-attractor-flythrough.scene.json"
P1_AXIS = (0.795, 0.0, -0.606)                     # into portal 1's wall (the canyon's portal axis)
HOME_AXIS = [-P1_AXIS[0], 0.0, -P1_AXIS[2]]       # out of the wall, toward the river
# (d, lat) in the current canyon's metres (site()), absolute heights there (scaled with the world)
HOME_PATH = [(35.5, 23.0, 8.0), (31.0, 12.0, 7.4), (23.0, 0.0, 6.6), (12.0, -14.0, 6.2), (-2.0, -32.0, 6.6),
             (-20.0, -50.0, 8.5), (-42.0, -62.0, 13.0), (-66.0, -68.0, 22.0), (-90.0, -76.0, 38.0),
             (-110.0, -86.0, 60.0), (-126.0, -95.0, 84.0), (-138.0, -102.0, 100.0)]
T_HOME, T_END = 147.0, 210.5


def _home():
    xz = [site(d, l) for d, l, _ in HOME_PATH]
    try:
        ground = heights(CANYON_SCENE, [tuple(p) for p in xz])
    except Exception:          # the POC's canyon is not written yet (the builder runs twice)
        ground = [0.0] * len(xz)
    pts = [(p[0], y * R, p[1]) for p, (_, _, y) in zip(xz, HOME_PATH)]
    exit_ = [pts[0][0], pts[0][1] - ground[0], pts[0][2]]
    # out of the wall at the bay's speed, easing to a slow, held climb as the music ends
    knots, _ = pace(pts[1:], T_HOME + 1.4, T_END, 7.0, 0.6, ramp_in=0.2, ramp_out=0.35)
    return exit_, [[x, y - g, z, t] for (x, y, z, t), g in zip(knots, ground[1:])]


HOME_EXIT, HOME_FLIGHT = _home()


def life_nodes():
    """Rock, kelp, sponges, sea fans and grass along the route, set on the sampled seabed."""
    items = []
    spots = [(312.0, -472.0), (284.0, -488.0), (304.0, -540.0), (268.0, -572.0), (292.0, -612.0), (246.0, -652.0),
             (278.0, -690.0), (228.0, -704.0), (202.0, -770.0), (178.0, -802.0), (140.0, -832.0), (150.0, -872.0),
             (100.0, -838.0)]
    kinds = ["kelp-a", "kelp-b", "sponge", "fan-coral", "seagrass", "basalt-a", "basalt-b", "kelp-a", "sponge", "fan-coral",
             "kelp-b", "seagrass", "basalt-c"]
    pts = []
    for k, (x, z) in enumerate(spots):
        for j in range(5):   # a cluster at each spot
            a = (k * 7 + j * 2.4) % 6.283
            r = 2.0 + 4.0 * ((k * 13 + j * 7) % 10) / 10.0
            pts.append((x + r * math.cos(a), z + r * math.sin(a), kinds[(k + j) % len(kinds)], 0.7 + 0.12 * ((k + j) % 5), a * 57.3))
    try:
        gr = heights(PREFIX + "strange-attractor-ocean.scene.json", [(p[0], p[1]) for p in pts])
    except Exception:
        gr = [0.0] * len(pts)
    for i, ((x, z, kind, sc, yaw), g) in enumerate(zip(pts, gr)):
        items.append({"name": f"life-{i}", "kind": "gltf", "asset": A + f"{kind}.glb", "position": [x, g - 0.15, z],
                      "rotation": [0, yaw, 0], "scale": [sc * (2.5 if kind.startswith(("rock", "basalt")) else 1.0)] * 3,
                      "visible": True})
    return items


def fish_nodes():
    """Fish: schools (particles circling a point, streaked along their motion) and a few singles."""
    out = []
    for k, (c, n, r) in enumerate((((291.0, 24.0, -522.0), 260, 9.0), ((270.0, 40.0, -660.0), 340, 12.0),
                                   ((196.0, 62.0, -790.0), 200, 8.0))):
        out.append({"name": f"fish-school-{k}", "kind": "particles", "visible": True, "particles": {
            "enabled": True, "capacity": n, "seed": 600 + k, "shape": "sphere", "position": list(c), "extent": [r, r * 0.3, r],
            "direction": [1, 0, 0], "spawnRate": n / 30.0, "burst": float(n), "lifetimeMin": 28.0, "lifetimeMax": 32.0,
            "spread": 0.4, "speedMin": 2.0, "speedMax": 3.5, "gravity": [0, 0, 0], "drag": 0.05, "turbulence": 0.4,
            "turbulenceScale": 0.08, "attractorPosition": list(c), "attractorStrength": 2.2, "attractorRadius": r * 3.0,
            "orbit": 2.6, "sizeStart": 0.22, "sizeEnd": 0.22, "sizeVariance": 0.3,
            "colorStart": [0.55, 0.68, 0.72, 0.9], "colorEnd": [0.55, 0.68, 0.72, 0.9], "emissive": 0.0,
            "blend": "alpha", "velocityStretch": 0.9, "stretchMax": 0.5}})
    out.append({"name": "fish-singles", "kind": "particles", "visible": True, "particles": {
        "enabled": True, "capacity": 40, "seed": 700, "shape": "box", "position": [260.0, 40.0, -640.0],
        "extent": [70.0, 20.0, 220.0], "direction": [0, 0, 1], "spawnRate": 1.2, "burst": 30.0, "lifetimeMin": 30.0,
        "lifetimeMax": 34.0, "spread": 1.0, "speedMin": 0.6, "speedMax": 1.4, "gravity": [0, 0, 0], "drag": 0.0,
        "turbulence": 0.3, "turbulenceScale": 0.02, "sizeStart": 0.45, "sizeEnd": 0.45, "sizeVariance": 0.4,
        "colorStart": [0.35, 0.45, 0.5, 0.95], "colorEnd": [0.35, 0.45, 0.5, 0.95], "emissive": 0.0, "blend": "alpha",
        "velocityStretch": 1.4, "stretchMax": 1.2}})
    return out


def write():
    return write_ocean(None), flight()


if __name__ == "__main__":
    print(write()[0])
