"""Writes strange-attractor-ocean.scene.json (the deep ocean, Art Pass 2, M3) and strange-attractor-ufo.scene.json (the
UFO interior's first form, the bay portal's destination; M4 builds the interior).

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
GROUND = {(365.0, -785.0): -3.0, (215.0, -870.0): 6.4, (125.0, -880.0): 30.0, (65.0, -880.0): 44.0, (40.0, -880.0): 48.0}
BAY_LOCAL = (0.0, 32.0, 90.0)       # the membrane's centre in the glb (y up, front +z)


def base_world(local):
    """A point in the base glb's frame (yaw 90: local +z -> world +x, local +x -> world -z) into the world."""
    lx, ly, lz = local
    return [BASE_XZ[0] + lz, BASE_GROUND + ly, BASE_XZ[1] - lx]


BAY = base_world(BAY_LOCAL)

# the arrival: out into open water, heading -z (the exit's y is over the seabed)
EXIT = [250.0, 60.0, 320.0]
EXIT_AXIS = [0.0, 0.0, -1.0]

# the ocean flight (x, height over the seabed, z, t): crossing 5 at 121.2 s (bar 62), crossing 6 at 147.0 s (bar 75)
def flight(bay_rel_y):
    by = BAY[1]
    return [
        [245.0, 55.0, 210.0, 123.2],
        [215.0, 46.0, -120.0, 127.8],    # the shelf's wall to the left, the water's depth ahead
        [175.0, 40.0, -430.0, 132.4],    # the base's lights, faint ahead and to the left
        [150.0, 36.0, -690.0, 136.6],
        # turning in toward the launch bay at its height (heights over the measured seabed)
        [365.0, by - GROUND[(365.0, -785.0)] + 6.0, -785.0, 140.6],
        [215.0, by - GROUND[(215.0, -870.0)], -870.0, 144.0],
        [125.0, by - GROUND[(125.0, -880.0)], -880.0, 145.9],
        [65.0, by - GROUND[(65.0, -880.0)], -880.0, 147.0],      # the crossing, in the membrane
        [40.0, by - GROUND[(40.0, -880.0)], -880.0, 147.8],
    ]


# the submersibles: three discs (the UFO model, scaled), their paths keyed in the project's timeline (build_journey)
SUBS = [("sub-1", 0.026), ("sub-2", 0.021), ("sub-3", 0.017)]   # 17, 14 and 11 m discs (the model is 666 units across)


def sub_tracks():
    """[(node, [(t, [x, y, z])], [(t, yaw)])]: absolute positions (the seabed under the routes is about 0..10 m)."""
    bx, by, bz = BAY
    return [
        # sub-1 leaves the bay, turns out over the seabed and away to the north
        ("sub-1", [(118.0, [bx - 30.0, by, bz]), (126.0, [bx - 30.0, by, bz]), (131.0, [bx + 60.0, by + 2.0, bz]),
                   (137.0, [bx + 180.0, by + 18.0, bz + 160.0]), (146.0, [bx + 330.0, by + 40.0, bz + 520.0])],
         [(126.0, 90.0), (131.0, 90.0), (137.0, 40.0), (146.0, 20.0)]),
        # sub-2 comes in from the south and enters the bay, ahead of the camera
        ("sub-2", [(118.0, [bx + 420.0, by + 30.0, bz - 380.0]), (130.0, [bx + 230.0, by + 12.0, bz - 120.0]),
                   (138.0, [bx + 90.0, by + 2.0, bz - 10.0]), (142.5, [bx + 10.0, by, bz]), (147.0, [bx - 45.0, by, bz])],
         [(118.0, -40.0), (130.0, -60.0), (138.0, -85.0), (147.0, -90.0)]),
        # sub-3 holds station high by the towers, drifting
        ("sub-3", [(118.0, [bx - 60.0, by + 95.0, bz + 70.0]), (147.0, [bx - 40.0, by + 102.0, bz + 30.0])],
         [(118.0, 10.0), (147.0, 35.0)]),
    ]


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
                       "rocks": 0.02, "relief": 55, "program": "seabed", "curvature": 6371.0e3,
                       "heroes": [], "mesas": MESAS, "yardangs": 2.5, "wind": 70.0},
        # the bay portal: the membrane across the launch bay tears open (the corruption's focus y is over the seabed)
        "corruption": {"targets": ["bay-membrane"], "focus": [BAY[0], bay_y - GROUND[(65.0, -880.0)], BAY[2]], "axis": [-1.0, 0.0, 0.0],
                       "amount": 1.0, "radius": 45.0, "inner": 0.2, "fieldNear": 25.0, "fieldFar": 260.0, "openNear": 20.0,
                       "openFar": 130.0, "seam": 0.3, "width": 22.0, "height": 11.0, "depth": 8.0, "block": 2.5,
                       "share": 0.25, "slip": 1.5, "rate": 0.6, "strip": 2.5, "stripSlip": 1.0, "slide": 0.0, "pull": 4.0,
                       "lift": 0.8, "fold": 0.6, "lag": 0.7, "glyph": 6.0, "glyphSize": 1.0, "glow": 4.0, "echoes": 1,
                       "echoOffset": 0.6, "glowColor": [0.55, 0.9, 1.0], "light": 1500.0},
        "portal": {"world": "strange-attractor-ufo.scene.json", "exit": UFO_EXIT, "exitYaw": 0.0, "exitAxis": [0.0, 0.0, -1.0],
                   "entryUp": [0.0, 1.0, 0.0], "exitUp": [0.0, 1.0, 0.0], "flight": UFO_FLIGHT},
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
        ],
    }
    path = os.path.join(HERE, "strange-attractor-ocean.scene.json")
    json.dump(scene, open(path, "w"), indent=2)
    return path


# --- the UFO interior's first form (M4 builds the room): the proof hall, lit cold, the camera arriving through the bay
UFO_EXIT = [0.0, 3.2, 14.0]
UFO_FLIGHT = [[0.0, 3.0, 6.0, 149.0], [0.0, 2.8, 0.0, 151.5], [0.0, 2.8, -4.0, 153.5]]


def write_ufo():
    scene = {
        "format": "avgen-scene", "version": 1, "name": "the-strange-attractor-ufo",
        "_note": "The UFO interior's first form (Art Pass 2, M3: the bay portal's destination; M4 builds the interior).",
        "camera": {"mode": 1, "position": [0, 3, 14], "target": [0, 2, 0], "fov": 44.0, "orbitSpeed": 0.0},
        "lights": [
            {"name": "ceiling", "type": "point", "position": [0.0, 7.0, 0.0], "color": [0.8, 0.9, 1.0], "intensity": 140.0,
             "range": 40.0, "castsShadow": True},
            {"name": "rim", "type": "directional", "role": "fill", "direction": [0.3, -0.5, -1.0], "color": [0.4, 0.55, 0.8],
             "intensity": 0.3, "castsShadow": False},
        ],
        "environment": {"intensity": 0.08, "background": [0.01, 0.012, 0.02], "skybox": False, "sky": {"enabled": False},
                        "fallbackAmbient": False},
        "post": {"bloomIntensity": 0.05, "tonemap": 0},
        "nodes": [{"name": "interior", "kind": "gltf", "asset": "../../assets/strange-attractor/proofs/interior.glb",
                   "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1], "visible": True}],
    }
    path = os.path.join(HERE, "strange-attractor-ufo.scene.json")
    json.dump(scene, open(path, "w"), indent=2)
    return path


def write():
    write_ufo()
    bay_rel_y = BAY[1] - BASE_GROUND   # the bay's centre over the seabed (the pad levels the floor there)
    return write_ocean(None), flight(bay_rel_y)


if __name__ == "__main__":
    print(write()[0])
