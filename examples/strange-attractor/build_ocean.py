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
from canyon_scale import WORLD_SCALE as WS, R, site, pace, L_UNIT  # noqa: E402
from sample_ground import heights  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
A = "../../assets/strange-attractor/build/"

WATER = [0.012, 0.075, 0.095]       # the fog: the water's own colour
DEEP = [0.004, 0.022, 0.035]        # the background beyond the fog
WATER_FAR = [0.006, 0.05, 0.075]    # M3g: the colour distance fades to (red gone, a deeper blue-green)
LIGHT_DOWN = [0.12, -1.0, 0.18]

# the shelf: a giant mesa whose east wall (x about -100) faces the camera's route
SHELF = dict(x=-800.0, z=-600.0, rx=700.0, rz=1600.0, angle=0.0, height=240.0, terraces=4)
MESAS = [SHELF,
         dict(x=420.0, z=-300.0, rx=60.0, rz=45.0, angle=30.0, height=40.0, terraces=2),   # rock outcrops
         dict(x=260.0, z=-1250.0, rx=90.0, rz=60.0, angle=-20.0, height=55.0, terraces=2),
         dict(x=520.0, z=-800.0, rx=70.0, rz=70.0, angle=10.0, height=35.0, terraces=2)]

# M3d (owner: no box-like "basins"; the seafloor on the canyon's procedural pipeline, ADR-1902): the layout is carried
# onto the generated reef, whose wall at about x = 690 faces +x over 1.1 km of open floor (found by a search of the
# height field); every place below is authored as before and offset by (OX, OZ); heights are absolute
OX, OZ = 809.0, 677.0
PLAIN = 32.7                        # the floor's level in front of the wall (sampled)
# the base at the reef wall's foot, its front (+z in the glb) turned to face +x (yaw 90), on a pad at the floor's level
BASE_XZ = (-25.0 + OX, -880.0 + OZ)
BASE_GROUND = PLAIN
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
# M3d: on the generated floor the approach runs along the open ground in front of the reef wall, heading -x
EXIT = [BASE_XZ[0] + 1050.0, PLAIN + 30.0, BASE_XZ[1] + 60.0]   # absolute (over the open floor)
EXIT_AXIS = [-1.0, 0.0, 0.0]
_bx, _bz = BASE_XZ
# (M3d, owner: the camera rocked at the start -- measured: the M3c path's heights over a dipping seabed nodded the
# camera +-7 degrees in pitch): one smooth line now, level at first, its lateral offset easing in as a cosine (no
# kink), its height rising 3 m over the whole run
def _ocean_path():
    pts = [(_bx + 1050.0 - 7.0, None, _bz + 60.0)]
    for d in (1020.0, 960.0, 860.0, 730.0, 580.0, 420.0, 260.0, 150.0):
        u = (1043.0 - d) / (1043.0 - 150.0)
        pts.append((_bx + d, PLAIN + 30.0 + 2.0 * u, _bz + 60.0 * math.cos(0.5 * math.pi * u)))
    return pts + [(BAY[0], BAY[1], BAY[2]), (BAY[0] - 20.0, BAY[1], BAY[2])]


OCEAN_PATH = _ocean_path()
T_EXIT, T_BAY = 121.2 + 1.0 / 60.0, 147.0 + 1.0 / 60.0   # half a frame off the 30 fps grid (ADR-1903)


# the ocean flight (x, height over the seabed, z, t): crossing 5 at 121.2 s (bar 62), crossing 6 at 147.0 s (bar 75)
def flight(bay_rel_y=None):
    try:
        ground = heights("strange-attractor-ocean.scene.json", [(x, z) for x, _, z in OCEAN_PATH])
    except Exception:
        ground = [0.0] * len(OCEAN_PATH)
    pts = []
    for (x, y, z), g in zip(OCEAN_PATH, ground):
        pts.append((x, (EXIT[1] if y is None else y), z))
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

    # M3d (owner: out and up, not straight up -- 00-owner-refs/ocean-ufo-launch-path-arrow.png): a launch leaves the
    # bay along a shallow climbing line, mostly outward across the floor and rising (about 12-20 degrees), fast, and
    # is gone into the gloom in four seconds; a return mirrors it, coming in on a shallow descent
    def launch(t0, side):
        pos = [(t0 - 0.6, inside), (t0, [bx + 12.0, by + 0.5, bz]), (t0 + 0.8, [bx + 85.0, by + 8.0, bz + side * 12.0]),
               (t0 + 1.7, [bx + 190.0, by + 30.0, bz + side * 40.0]), (t0 + 2.7, [bx + 320.0, by + 70.0, bz + side * 80.0]),
               (t0 + 4.0, [bx + 480.0, by + 135.0, bz + side * 130.0])]
        rot = [(t0 - 0.6, [0.0, 90.0, 0.0]), (t0 + 0.6, [0.0, 90.0 - side * 6.0, 6.0]),
               (t0 + 2.0, [0.0, 90.0 - side * 14.0, 14.0]), (t0 + 4.0, [0.0, 90.0 - side * 16.0, 17.0])]
        return pos, rot

    def dive(t1, side):
        pos = [(t1 - 4.0, [bx + 470.0, by + 130.0, bz + side * 140.0]), (t1 - 2.8, [bx + 310.0, by + 68.0, bz + side * 85.0]),
               (t1 - 1.6, [bx + 170.0, by + 26.0, bz + side * 38.0]), (t1 - 0.6, [bx + 50.0, by + 4.0, bz + side * 8.0]),
               (t1, inside)]
        rot = [(t1 - 4.0, [0.0, -90.0 + side * 16.0, -15.0]), (t1 - 1.6, [0.0, -90.0 + side * 8.0, -8.0]),
               (t1 - 0.4, [0.0, -90.0, 0.0])]
        return pos, rot

    def park(entries, t_end=150.0):
        """Hold the first and last key from the start and to the end (seek-safe outside the sorties)."""
        pos, rot = entries
        return [(118.0, pos[0][1])] + pos + [(t_end, pos[-1][1])], [(118.0, rot[0][1])] + rot + [(t_end, rot[-1][1])]

    tracks = []
    # M3d (owner: no launches while the bay's portal opens -- its code gathers from about 140 s): all done by 140.4
    for name, kind, t, side in (("sub-1", launch, 124.4, 1.0), ("sub-2", dive, 128.2, -1.0), ("sub-3", launch, 130.6, -1.0),
                                ("sub-4", dive, 134.6, 1.0), ("sub-5", launch, 136.4, 1.0)):
        pos, rot = park(kind(t, side))
        tracks.append((name, pos, rot))
    return tracks


def reef_program():
    """M3e (owner: better texturing; the black streaks): the reef's own program, replacing M3d's cooled copy of the
    canyon's (whose strata bands, laid by world height, ran down the steep reef faces as dark streaks). Photoscanned rock
    and sand (Poly Haven, CC0; never committed), triplanar in world space with the anti-tiling (ADR-1905); sand settles
    on the flats in drifts; algae greens the tops and coral pinks patches; the deep's blue-green; caustics drift across
    the up-facing rock."""
    T = "../../assets/strange-attractor/polyhaven/textures/"
    hexed = [1.0, 0.6, 5.0, 0.7]
    return {"name": "reefRock", "baseColor": 0, "metallic": -1, "roughness": -1, "emission": -1, "opacity": -1,
            "normal": 7, "occlusion": -1,
            "textures": [{"name": "rock", "path": T + "rock_boulder_cracked/rock_boulder_cracked_diff_2k.jpg"},
                         {"name": "sand", "path": T + "coast_sand_rocks_02/coast_sand_rocks_02_diff_2k.jpg"},
                         {"name": "rockN", "path": T + "rock_boulder_cracked/rock_boulder_cracked_nor_gl_2k.jpg",
                          "colorSpace": "linear"}],
            "ops": [
                {"kind": "textureTriplanar", "dst": 0, "texture": "rock", "value": 0.1, "constant2": [6.0, 0, 0, 0],
                 "constant3": hexed},
                {"kind": "textureTriplanar", "dst": 1, "texture": "sand", "value": 0.25, "constant3": [1.0, 0.5, 6.0, 0.6]},
                {"kind": "constant", "dst": 2, "constant": [0.55, 0.6, 0.6, 1.0]},     # the sand darker: deep water
                {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
                # M3g: the rock lifted toward the sand, so the floor is not dark blotches on pale sand
                {"kind": "constant", "dst": 2, "constant": [1.45, 1.45, 1.45, 1.0]},
                {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 2},
                # sand on the flats, in drifts (a broad noise thins it)
                {"kind": "input", "dst": 2, "input": "normal"},
                {"kind": "swizzle", "dst": 2, "srcA": 2, "constant": [1, 1, 1, 1]},
                {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.62, 0.86, 0, 0]},
                {"kind": "input", "dst": 3, "input": "worldPosition"},
                {"kind": "noise", "dst": 3, "srcA": 3, "value": 0.04, "seed": 3},
                {"kind": "smoothstep", "dst": 4, "srcA": 3, "constant": [0.3, 0.7, 0, 0]},      # M3g: softer drifts
                {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 4},
                {"kind": "mixBy", "dst": 0, "srcA": 0, "srcB": 1, "srcC": 2},
                # algae on the broad patches
                {"kind": "smoothstep", "dst": 4, "srcA": 3, "constant": [0.52, 0.7, 0, 0]},
                {"kind": "constant", "dst": 5, "constant": [0.6, 0.92, 0.5, 1.0]},
                {"kind": "constant", "dst": 6, "constant": [1.0, 1.0, 1.0, 1.0]},
                {"kind": "mixBy", "dst": 5, "srcA": 6, "srcB": 5, "srcC": 4},
                {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 5},
                # coral tint in smaller patches
                {"kind": "input", "dst": 3, "input": "worldPosition"},
                {"kind": "noise", "dst": 3, "srcA": 3, "value": 0.12, "seed": 11},
                {"kind": "smoothstep", "dst": 4, "srcA": 3, "constant": [0.6, 0.74, 0, 0]},
                {"kind": "constant", "dst": 5, "constant": [1.2, 0.66, 0.6, 1.0]},
                {"kind": "mixBy", "dst": 5, "srcA": 6, "srcB": 5, "srcC": 4},
                {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 5},
                # the deep's colour
                {"kind": "constant", "dst": 5, "constant": [0.72, 0.9, 0.95, 1.0]},
                {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 5},
                # caustics: a cell pattern drifting with time, brightest at the cells' centres, on up-facing surfaces
                {"kind": "input", "dst": 3, "input": "time"},
                # M3f (owner §2b: the ocean "wobble" -- the camera is smooth; the whole floor's caustics drifting at
                # 0.7 m/s read as the frame swimming): a slow drift and half the brightness
                {"kind": "constant", "dst": 5, "constant": [0.12, 0.0, 0.08, 0.0]},
                {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 5},
                {"kind": "input", "dst": 5, "input": "worldPosition"},
                {"kind": "add", "dst": 5, "srcA": 5, "srcB": 3},
                {"kind": "voronoi", "dst": 5, "srcA": 5, "value": 0.2, "seed": 5},
                {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.5, 0.0, 0, 0]},     # M3g: soft-edged
                {"kind": "input", "dst": 4, "input": "normal"},
                {"kind": "swizzle", "dst": 4, "srcA": 4, "constant": [1, 1, 1, 1]},
                {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.3, 0.9, 0, 0]},
                {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 4},
                {"kind": "constant", "dst": 4, "constant": [0.18, 0.24, 0.26, 0.0]},
                {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 4},
                {"kind": "add", "dst": 5, "srcA": 5, "srcB": 6},
                {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 5},
                {"kind": "textureTriplanarNormal", "dst": 7, "texture": "rockN", "value": 0.1, "constant2": [6.0, 0, 0, 0],
                 "constant3": [1.0, 0.0, 0.0, 0.0]},
            ]}


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
            # M3g: the fill from above (the bright water overhead), not from below; the floor's underside stays dark
            {"name": "water-fill", "type": "directional", "role": "fill", "direction": [-0.3, -0.85, 0.4], "color": [0.2, 0.55, 0.65],
             "intensity": 0.25, "castsShadow": False, "shadowStrength": 0.0},
            # the bay's light into the water, the base's floods
            # M3f (owner §2b, the white-out before the sea exit): ROOT CAUSE -- this 2600 cd lamp stands 10 m inside the
            # bay, right behind the opening the camera flies through, and it scattered in the ocean's fog at full
            # strength: within a few metres of it the in-scatter (falling as 1/d^2) filled the frame white for the last
            # 0.4 s (with the fog off there is no white-out; the corruption's light and glow and the bloom made no
            # difference). Its scattering is off (6% still left a glow and a disc for 3 frames); the floods' cut to 25%.
            # Both still light the bay, the hull and the water round them
            {"name": "bay-light", "type": "point", "position": base_world((0.0, 32.0, 100.0)), "color": [1.0, 0.85, 0.65],
             "intensity": 2600.0, "range": 220.0, "castsShadow": False, "volumetric": 0.0},
            {"name": "flood-l", "type": "point", "position": base_world((-80.0, 50.0, 110.0)), "color": [0.6, 0.85, 1.0],
             "intensity": 3000.0, "range": 160.0, "castsShadow": False, "volumetric": 0.25},
            {"name": "flood-r", "type": "point", "position": base_world((80.0, 50.0, 110.0)), "color": [0.6, 0.85, 1.0],
             "intensity": 3000.0, "range": 160.0, "castsShadow": False, "volumetric": 0.25},
        ] + [{"name": f"{name}-light", "type": "point", "position": [0.0, -500.0, 0.0], "color": [0.55, 0.9, 1.0],
              "intensity": 900.0, "range": 60.0, "castsShadow": False} for name, _ in SUBS],
        # M3g (owner §2c: real water, not CG; 53-pass2-m3g-plan.md): the distance fog loses red first, then green
        # (ADR-1908, Jerlov-like weights), toward a deeper blue-green; the march only covers the near water, where its
        # forward-scattering haze and the local lights' cones live; grounding by AO and contact shadows; AgX
        "environment": {"intensity": 0.14, "background": DEEP, "fogColor": WATER_FAR, "volumeDensity": 0.0024, "skybox": False,
                        "sky": {"enabled": False}, "fallbackAmbient": False, "shadowRange": 900.0, "shadowCascades": 3,
                        "ambientOcclusion": True, "shadowCasterReach": 1500.0, "contactShadows": True,
                        "fogSpectral": [2.6, 1.0, 0.62], "volumeMaxDistance": 45.0, "volumeAnisotropy": 0.75,
                        "volumeLocalLights": 1.0},
        "post": {"bloomIntensity": 0.06, "tonemap": 1},
        "materialPrograms": [reef_program(), {"name": "seabed", "ops": [
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
        # M3d (owner; ADR-1902): the seafloor on the canyon's pipeline, its Mars style turned to a reef -- eroded
        # outcrops and ledges (no terraces to speak of), sediment drifts between them, the floor falling away
        # M3e (owner: rounded, eroded rock, not crumpled spikes): softer relief (roughness 1 -> 0.4, ridges 1 -> 0.3),
        # less warp and terracing, and a finer mesh where the silhouettes were jagged (error 0.003 -> 0.0015)
        "attractorWorld": {"seed": 9, "resolution": 4096, "extent": 16000, "worldScale": 0.35, "camera": "scene",
                           "terrainScale": 1.0, "roughness": 0.4, "ridgeStrength": 0.3, "domainWarp": 0.5, "terraces": 0.05,
                           "erosion": 1.0, "strataFold": 0.3, "branchingDensity": 0.0, "meshSpacing": 1.0,
                           "meshError": 0.0015, "meshMinError": 0.1, "terrainProgram": "reefRock", "sunLux": 2.5,
                           "sunShadowBias": 1.6, "look": {"sunAzimuth": 20.0, "sunElevation": 70.0, "sunWarmth": 0.0},
                           "mars": {"mesaCover": 0.42, "yardangs": 0.0, "wind": 70.0, "dunes": 3.0},
                           "pads": [{"x": BASE_XZ[0], "z": BASE_XZ[1], "radius": 170.0, "level": PLAIN}]},
        # the bay portal: the membrane across the launch bay tears open (the corruption's focus y is over the seabed)
        # M3e (owner: not confined to the bay): the base, its rock and the seafloor are targets too, the field reaches
        # well past the bay's frame, and the opening is wider and taller than the bay, so the tear breaks out over the hull.
        # M3e (owner: sooner): the code reaches out from 770 m (about 130 s; M3d 260 m, 139 s) and the tear starts at 360 m
        # (about 137.5 s, after the last sortie at 136.4 s; M3d 130 m, 141.6 s)
        "corruption": {"targets": ["bay-membrane", "bay-strips", "ocean-base", "attractor.terrain"], "focus": [BAY[0], bay_y, BAY[2]],
                       "axis": [-1.0, 0.0, 0.0],
                       "amount": 1.0, "radius": 150.0, "inner": 0.2, "fieldNear": 25.0, "fieldFar": 770.0, "openNear": 20.0,
                       "openFar": 360.0, "seam": 0.3, "width": 36.0, "height": 21.0, "depth": 8.0, "block": 2.5,
                       "share": 0.25, "slip": 1.5, "rate": 0.6, "strip": 2.5, "stripSlip": 1.0, "slide": 0.0, "pull": 4.0,
                       "lift": 0.8, "fold": 0.6, "lag": 0.7, "glyph": 6.0, "glyphSize": 1.0, "glow": 4.0, "echoes": 1,
                       "echoOffset": 0.6, "glowColor": [0.55, 0.9, 1.0], "light": 500.0},
        # M3b (owner: cut the UFO interior): the bay's portal leads home -- out of the canyon wall where the journey
        # began (portal 1, reversed), at dawn
        # M3d (owner: the Moon portal is the reference look): the mesh wire on the portal's own surface -- its
        # scan, its rim, and the shards that break off the lips and fly into the opening (the pyramid had none)
        "meshWire": {"targets": ["bay-membrane", "bay-strips", "ocean-base", "attractor.terrain"], "color": [0.45, 0.85, 1.0], "intensity": 0.0, "width": 1.0, "creaseLow": 2.0,
                     "creaseHigh": 12.0, "reveal": 1.0, "fadeNear": 60.0, "fadeFar": 900.0, "minPixels": 14.0, "wave": 0.0,
                     "waveRadius": 300.0, "waveWidth": 40.0, "waveGain": 1.6, "waveAhead": 200.0, "clipBelow": -1000.0,
                     "fromFocus": 1.0, "pulse": 1.2, "pulseSpacing": 60.0, "pulseSpeed": 40.0, "pulseWidth": 6.0,
                     "focusGain": 0.5, "rimGain": 6.0, "frontColor": [0.75, 0.95, 1.0], "shards": 0.3, "shardRate": 0.35},
        "portal": {"world": "@root", "exit": HOME_EXIT, "exitAxis": HOME_AXIS, "entryUp": [0.0, 1.0, 0.0],
                   "exitUp": [0.0, 1.0, 0.0], "flight": HOME_FLIGHT},
        "nodes": [
            {"name": "ocean-base", "kind": "gltf", "asset": A + "ocean-base.glb", "position": [BASE_XZ[0], BASE_GROUND, BASE_XZ[1]],
             "rotation": [0, 90, 0], "scale": [1, 1, 1], "visible": True},
            {"name": "bay-membrane", "kind": "gltf", "asset": A + "bay-membrane.glb",
             "position": [BASE_XZ[0], BASE_GROUND, BASE_XZ[1]], "rotation": [0, 90, 0], "scale": [1, 1, 1], "visible": True},
            # M3e: the bay frame's light strips, apart, so their light dies as the corruption takes the base (the timeline)
            {"name": "bay-strips", "kind": "gltf", "asset": A + "bay-strips.glb",
             "position": [BASE_XZ[0], BASE_GROUND, BASE_XZ[1]], "rotation": [0, 90, 0], "scale": [1, 1, 1], "visible": True},
        ] + [{"name": name, "kind": "gltf", "asset": A + "ufo-craft.glb",
              "position": [0.0, -500.0, 0.0], "rotation": [0, 0, 0], "scale": [s, s, s], "visible": True} for name, s in SUBS] + [
            # marine snow: slow, sparse flecks through the water along the route
            {"name": "marine-snow", "kind": "particles", "visible": True, "particles": {
                "enabled": True, "capacity": 26000, "seed": 77, "shape": "box", "position": [BASE_XZ[0] + 560.0, PLAIN + 55.0, BASE_XZ[1]],
                "extent": [620.0, 80.0, 380.0], "direction": [0, -1, 0], "spawnRate": 900.0, "lifetimeMin": 25.0,
                "lifetimeMax": 40.0, "spread": 1.0, "speedMin": 0.02, "speedMax": 0.15, "gravity": [0.05, -0.04, 0.02],
                "drag": 0.2, "turbulence": 0.2, "turbulenceScale": 0.05, "sizeStart": 0.09, "sizeEnd": 0.09,
                "sizeVariance": 0.6, "colorStart": [0.7, 0.85, 0.85, 0.35], "colorEnd": [0.7, 0.85, 0.85, 0.35],
                "emissive": 0.3, "blend": "additive"}},
        ] + life_nodes() + fish_nodes() + creature_nodes(),
    }
    path = os.path.join(HERE, "strange-attractor-ocean.scene.json")
    json.dump(scene, open(path, "w"), indent=2)
    return path


# --- home (M3b): out of portal 1's wall in the canyon, the way the journey went in, now at dawn: out over the river,
# turning up it past the camp, low along the water, then rising to a high, slowing view as the music ends (210.5 s).
CANYON_SCENE = "strange-attractor-flythrough.scene.json"
# M3d (owner: exit higher in the canyon, no clipping through rock or shrubs): out of the outer cliff behind the camp,
# 86 m up its face (the cliff the night came home through), high over the benches, down to the river and up it,
# rising to the high view as the music ends. (d, lat) in the current canyon's metres, absolute heights there.
HOME_SITE = (46.0, 92.0)
HOME_AXIS = [-L_UNIT[0], 0.0, -L_UNIT[1]]          # out of the cliff, toward the river
HOME_PATH = [(46.0, 92.0, 86.0), (42.0, 82.0, 84.0), (36.0, 60.0, 72.0), (27.0, 32.0, 52.0), (15.0, 6.0, 36.0),
             (-2.0, -26.0, 30.0), (-26.0, -52.0, 32.0), (-56.0, -66.0, 40.0), (-86.0, -77.0, 56.0),
             (-110.0, -88.0, 76.0), (-128.0, -97.0, 92.0), (-138.0, -102.0, 100.0)]
T_HOME, T_END = 147.0, 210.5


def _home():
    xz = [site(d, l) for d, l, _ in HOME_PATH]
    ground = heights(CANYON_SCENE, [tuple(p) for p in xz])
    pts = [(p[0], y * R, p[1]) for p, (_, _, y) in zip(xz, HOME_PATH)]
    exit_ = [pts[0][0], pts[0][1], pts[0][2]]          # the root's exit height is absolute
    clear = min(pt[1] - g for pt, g in zip(pts[1:], ground[1:]))
    print(f"home flight: lowest knot {clear:.1f} m over the ground")
    # out of the wall at the bay's speed, easing to a slow, held climb as the music ends
    knots, _ = pace(pts[1:], T_HOME + 1.4, T_END, 7.0, 0.6, ramp_in=0.2, ramp_out=0.35)
    return exit_, [[x, y - g, z, t] for (x, y, z, t), g in zip(knots, ground[1:])]


HOME_EXIT, HOME_FLIGHT = _home()


def life_nodes():
    """Rock, kelp, sponges, sea fans and grass along the route, set on the sampled seabed."""
    items = []
    bx, bz = BASE_XZ
    spots = [(bx + d, bz + o) for d, o in ((980.0, 30.0), (940.0, 95.0), (900.0, -25.0), (860.0, 110.0), (800.0, 70.0),
                                          (760.0, -40.0), (700.0, 120.0), (640.0, -55.0), (600.0, 80.0), (540.0, -30.0),
                                          (480.0, 100.0), (420.0, -60.0), (360.0, 60.0), (300.0, -45.0), (240.0, 70.0),
                                          (190.0, -50.0), (150.0, 45.0), (900.0, 40.0))]
    kinds = ["kelp-a", "kelp-b", "sponge", "fan-coral", "seagrass", "basalt-a", "basalt-b", "kelp-a", "sponge", "fan-coral",
             "kelp-b", "seagrass", "basalt-c"]
    pts = []
    for k, (x, z) in enumerate(spots):
        for j in range(9):   # a cluster at each spot (M3d: denser)
            a = (k * 7 + j * 2.4) % 6.283
            r = 2.0 + 4.0 * ((k * 13 + j * 7) % 10) / 10.0
            pts.append((x + r * math.cos(a), z + r * math.sin(a), kinds[(k + j) % len(kinds)], 0.7 + 0.12 * ((k + j) % 5), a * 57.3))
    try:
        gr = heights("strange-attractor-ocean.scene.json", [(p[0], p[1]) for p in pts])
    except Exception:
        gr = [0.0] * len(pts)
    for i, ((x, z, kind, sc, yaw), g) in enumerate(zip(pts, gr)):
        items.append({"name": f"life-{i}", "kind": "gltf", "asset": A + f"{kind}.glb", "position": [x, g - 0.15, z],
                      "rotation": [0, yaw, 0], "scale": [sc * (2.5 if kind.startswith(("rock", "basalt")) else 1.0)] * 3,
                      "visible": True})
    return items


# M3e: the bay frame's strips fail as the corruption reaches them -- a few stutters, then dark (seconds, emissiveBoost)
STRIPS_DIE = [(133.6, 1.0), (134.1, 0.25), (134.3, 0.9), (134.9, 0.1), (135.1, 0.55), (135.8, 0.0)]


def creature_tracks():
    """M3d (owner: wildlife, animated, not sliding statues): rigged procedural animals (pass2/build_sealife.py) with
    their swim loops, cruising along straight lines across the route (keyed in the timeline: deterministic, seek-safe).
    [(node, asset, scale, [(t, [x, y, z])], yaw)]. Their front is -z in the glb (yaw + 180 from the heading)."""
    bx, bz = BASE_XZ
    out = []
    import random
    rng = random.Random(1903)
    # single fish: 12, crossing the route ahead of the camera at 1.5-3 m/s, at 4-30 m over the floor
    for k in range(12):
        d0 = rng.uniform(200.0, 980.0)
        side = 1.0 if k % 2 == 0 else -1.0
        z0, z1 = bz + side * rng.uniform(60.0, 140.0), bz - side * rng.uniform(40.0, 140.0)
        x0 = bx + d0
        x1 = x0 + rng.uniform(-120.0, 60.0)
        y = PLAIN + rng.uniform(-8.0, 22.0)
        t0, t1 = 121.0, 148.0
        yaw = math.degrees(math.atan2(x1 - x0, z1 - z0)) + 180.0
        out.append((f"cruiser-{k}", "fish", rng.uniform(0.8, 1.6), [(t0, [x0, y, z0]), (t1, [x1, y + rng.uniform(-3, 3), z1])], yaw))
    # rays: 3, gliding low over the floor
    for k in range(3):
        x0 = bx + 800.0 - 260.0 * k
        out.append((f"ray-{k}", "ray", 1.6 + 0.4 * k, [(121.0, [x0, PLAIN - 9.0 + 2.0 * k, bz + 90.0 - 70.0 * k]),
                                                      (148.0, [x0 - 70.0, PLAIN - 8.0 + 2.0 * k, bz - 20.0 - 70.0 * k])],
                    math.degrees(math.atan2(-70.0, -110.0)) + 180.0))
    # jellyfish: 10, drifting up slowly near the route, glowing softly
    for k in range(10):
        x0 = bx + rng.uniform(150.0, 1000.0)
        z0 = bz + rng.uniform(-50.0, 90.0)
        y0 = PLAIN + rng.uniform(18.0, 42.0)
        out.append((f"jelly-{k}", "jelly", rng.uniform(1.0, 2.2), [(121.0, [x0, y0, z0]), (148.0, [x0 + 4.0, y0 + 6.0, z0 - 3.0])],
                    rng.uniform(0, 360)))
    return out


def creature_nodes():
    return [{"name": n, "kind": "gltf", "asset": A + f"{a}.glb", "position": pts[0][1], "rotation": [0, yaw, 0],
             "scale": [sc] * 3, "visible": True, "animation": {"state": "swim", "cullDistance": 900.0, "speed": 1.0}}
            for n, a, sc, pts, yaw in creature_tracks()]


def fish_nodes():
    """Fish: schools (particles circling a point, streaked along their motion) and a few singles."""
    out = []
    bx, bz = BASE_XZ
    for k, (c, n, r) in enumerate((((bx + 880.0, PLAIN + 26.0, bz + 75.0), 260, 9.0),
                                   ((bx + 600.0, PLAIN + 30.0, bz - 45.0), 340, 12.0),
                                   ((bx + 320.0, PLAIN + 36.0, bz + 55.0), 200, 8.0))):
        out.append({"name": f"fish-school-{k}", "kind": "particles", "visible": True, "particles": {
            "enabled": True, "capacity": n, "seed": 600 + k, "shape": "sphere", "position": list(c), "extent": [r, r * 0.3, r],
            "direction": [1, 0, 0], "spawnRate": n / 30.0, "burst": float(n), "lifetimeMin": 28.0, "lifetimeMax": 32.0,
            "spread": 0.4, "speedMin": 2.0, "speedMax": 3.5, "gravity": [0, 0, 0], "drag": 0.05, "turbulence": 0.4,
            "turbulenceScale": 0.08, "attractorPosition": list(c), "attractorStrength": 2.2, "attractorRadius": r * 3.0,
            "orbit": 2.6, "sizeStart": 0.22, "sizeEnd": 0.22, "sizeVariance": 0.3,
            "colorStart": [0.55, 0.68, 0.72, 0.9], "colorEnd": [0.55, 0.68, 0.72, 0.9], "emissive": 0.0,
            "blend": "alpha", "velocityStretch": 0.9, "stretchMax": 0.5}})
    out.append({"name": "fish-singles", "kind": "particles", "visible": True, "particles": {
        "enabled": True, "capacity": 40, "seed": 700, "shape": "box", "position": [BASE_XZ[0] + 600.0, PLAIN + 38.0, BASE_XZ[1]],
        "extent": [420.0, 20.0, 160.0], "direction": [1, 0, 0], "spawnRate": 1.2, "burst": 30.0, "lifetimeMin": 30.0,
        "lifetimeMax": 34.0, "spread": 1.0, "speedMin": 0.6, "speedMax": 1.4, "gravity": [0, 0, 0], "drag": 0.0,
        "turbulence": 0.3, "turbulenceScale": 0.02, "sizeStart": 0.45, "sizeEnd": 0.45, "sizeVariance": 0.4,
        "colorStart": [0.35, 0.45, 0.5, 0.95], "colorEnd": [0.35, 0.45, 0.5, 0.95], "emissive": 0.0, "blend": "alpha",
        "velocityStretch": 1.4, "stretchMax": 1.2}})
    return out


def write():
    return write_ocean(None), flight()


if __name__ == "__main__":
    print(write()[0])
