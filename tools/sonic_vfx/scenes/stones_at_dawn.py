"""STONES AT DAWN (mystery, the ancient). A ring of standing stones on a heather moor at sunrise: the low sun burns
the edge of the tallest stone through the fog lying in the hollows.

Art direction (ART-RESTART-PLAN.md #2): a long-lens view across the moor. The ring stands on a low rise in the
midground; the tallest stone leans a little on the right third with the sun just behind its shoulder, so its edges
burn and its face is cool and dark. The stones overlap in depth, the far ones paler in the fog. Out-of-focus heather
and a lichen boulder frame the lower left. Behind the ring the land falls away in layers of hills, each paler and
pinker. The only light is the sun.

Built from: a terrain moor with hill ridges for the layers, scanned boulders stretched upright for the stones, scanned
shrubs tinted to heather, the height fog pooling in the hollows and scattering forward round the sun.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "stones-at-dawn"
TITLE = "Stones at Dawn"

FOG = "#c9b8b5"
HEATHER = "#6e5466"
LICHEN = "#6f6d62"
SUN = "#ffd7a8"
SHADOW = "#3e4758"

CAM = (-1.0, 1.3, 36.0)
LENS = 85.0
RING = (0.0, 0.0, -12.0)            # the ring's centre on the rise
RING_R = 11.0
TALL = (5.2, 0.0, -18.5)            # the tallest stone, the hero: on the right third, the sun behind it
SUN_EL, SUN_AZ = 6.0, 7.5          # the sun: degrees above the horizon, degrees right of straight ahead (-Z)

DESIGN = {
    "category": "moor",
    "thesis": "A ring of standing stones on a heather moor at sunrise, the sun burning the tallest stone's edge "
              "through the fog.",
    "composition": {
        "background": "layers of hills, paler and pinker with distance; the dawn sky",
        "midground": "the ring of stones on its rise, overlapping, the far ones paler",
        "foreground": "heather and a lichen boulder, out of focus",
        "focal": "the tallest stone with the sun at its shoulder",
        "secondary": ["the other stones", "the fog in the hollows", "the long shadows"],
        "atmosphere": "fog pooling in the hollows, glowing round the sun",
        "camera": "85 mm, 1.2 m, across the moor and slightly up the rise",
    },
    "palette": {"dominant": FOG, "secondary": HEATHER, "accent": SUN, "highlight": "#fff3de"},
    "motion": {"very_slow": ["fog drift", "the camera's drift"], "medium": ["heather in the wind"], "fast": [],
               "extremely_fast": []},
    "vocabulary": [],
    "tier": "light: a terrain, scanned stones, scattered shrubs (budgeted), height fog",
}


def sun_dir():
    """The direction the sunlight TRAVELS: from the sun (ahead, a little right, low) toward the camera."""
    el, az = math.radians(SUN_EL), math.radians(SUN_AZ)
    return [round(-math.sin(az) * math.cos(el), 5), round(-math.sin(el), 5), round(math.cos(az) * math.cos(el), 5)]


def standing_stone(h, w, d, seed):
    """A standing stone: a rough slab tapering a little to an uneven top, weathered (two scales of noise)."""
    slab = kit.sd_move((0, h * 0.5, 0), kit.sd_rbox([w, h * 0.5, d], 0.12))
    top = kit.sd_move((0.35 * w, h + 0.2, 0), kit.sd_rot((0, 0, 24.0), kit.sd_box([w * 1.6, 0.6, d * 2.0])))
    side = kit.sd_move((-1.25 * w, h * 0.72, 0), kit.sd_rot((0, 0, -12.0), kit.sd_box([w * 0.4, h, d * 2.0])))
    stone = kit.sd_diff(slab, top, side)
    stone = kit.sd_noise(0.16, 0.55, stone, seed=seed)
    return kit.sd_noise(0.025, 3.5, stone, seed=seed + 50)


def lichen_program():
    """Weathered grey stone with crusts of pale lichen in patches and dark in the hollows."""
    return {"name": "lichenStone", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.9, "seed": 7},
        {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 6.0, "seed": 8},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 3, "constant": [0.5, 0.5, 0.5, 0.5]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
        {"kind": "ramp", "dst": 4, "srcA": 1, "constant": hexrgb("#3d3c37") + [1.0],
         "constant2": hexrgb("#77756b") + [1.0], "constant3": hexrgb("#a19e90") + [1.0]},
        {"kind": "noise", "dst": 5, "srcA": 0, "value": 4.5, "seed": 9},
        {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.6, 0.72, 0.0, 0.0]},
        {"kind": "constant", "dst": 6, "constant": hexrgb("#9a9a7c") + [1.0]},
        {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 6, "srcC": 5},
        {"kind": "input", "dst": 7, "input": "cavity"},
        {"kind": "constant", "dst": 6, "constant": hexrgb("#1f1e1b") + [1.0]},
        {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 6, "srcC": 7}],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1}


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    sd = sun_dir()
    s.environment = {
        "intensity": 0.22, "skyIntensity": 1.0, "background": hexrgb("#d6c2bd"), "fogColor": hexrgb("#d3bfb8"),
        "volumeDensity": 0.02, "volumeMaxDistance": 160.0, "volumeSteps": 16, "volumeAnisotropy": 0.72,
        "volumeScattering": 1.2, "volumeAbsorption": 0.15, "volumeLocalLights": 0.0,
        "fogHeight": 0.0, "fogHeightFalloff": 1.4, "fogHeightCurve": 0.5, "fogPooling": 0.9, "fogSky": 1.0,
        "fogSkyDistance": 2200.0,
        "sky": {"enabled": True, "background": True, "zenithColor": hexrgb("#8f9fbe"),
                "horizonColor": hexrgb("#f2c7a6"), "groundColor": hexrgb("#4a4446"), "haze": 0.35,
                "sunIntensity": 14.0, "sunColor": hexrgb(SUN), "sunSize": 0.012, "sunGlow": 0.12, "intensity": 1.0,
                "sunDirection": [-sd[0], -sd[1], -sd[2]], "useKeyLight": False},
        "shadowCascades": 2,
    }

    # ---- the moor: a low rise for the ring, hollows round it, ridges of hills behind
    world = {
        "seed": 7, "size": [3000.0, 3000.0], "baseHeight": 0.0,
        "layers": [{"frequency": 0.004, "amplitude": 6.0, "warp": 60.0}, {"frequency": 0.02, "amplitude": 1.6},
                   {"frequency": 0.09, "amplitude": 0.25}],
        "features": [
            {"name": "rise", "kind": "ridge", "path": [[RING[0], 0.0, RING[2]]], "width": 70.0, "amplitude": 5.5,
             "falloff": 1.2, "roughness": 0.3, "smoothing": 2},
            {"name": "camrise", "kind": "ridge", "path": [[CAM[0] - 30.0, 0.0, CAM[2] + 6.0],
                                                          [CAM[0] + 30.0, 0.0, CAM[2] + 2.0]],
             "width": 34.0, "amplitude": 3.2, "falloff": 1.2, "roughness": 0.4, "smoothing": 2},
            {"name": "hollow", "kind": "valley", "path": [[-80.0, 0.0, 14.0], [0.0, 0.0, 12.0], [90.0, 0.0, 8.0]],
             "width": 26.0, "amplitude": 2.5, "falloff": 1.0, "roughness": 0.3, "smoothing": 2},
            {"name": "ringtop", "kind": "flat", "path": [[RING[0], 0.0, RING[2]]], "width": 30.0, "flatten": 0.5,
             "falloff": 1.0, "smoothing": 2},
            {"name": "hills1", "kind": "ridge", "path": [[-900.0, 0.0, -380.0], [-200.0, 0.0, -320.0],
                                                         [600.0, 0.0, -420.0]],
             "width": 260.0, "amplitude": 28.0, "falloff": 1.3, "roughness": 0.6, "smoothing": 3},
            {"name": "hills2", "kind": "ridge", "path": [[-1200.0, 0.0, -820.0], [100.0, 0.0, -760.0],
                                                         [1100.0, 0.0, -900.0]],
             "width": 420.0, "amplitude": 70.0, "falloff": 1.2, "roughness": 0.6, "smoothing": 3},
            {"name": "hills3", "kind": "ridge", "path": [[-1300.0, 0.0, -1350.0], [300.0, 0.0, -1300.0],
                                                         [1300.0, 0.0, -1400.0]],
             "width": 520.0, "amplitude": 120.0, "falloff": 1.1, "roughness": 0.5, "smoothing": 3},
        ]}
    s.program(lichen_program())
    s.program({"name": "moorGround", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.08, "seed": 3},
        {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 1.7, "seed": 4},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 3, "constant": [0.5, 0.5, 0.5, 0.5]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
        {"kind": "ramp", "dst": 4, "srcA": 1, "constant": hexrgb("#2b2522") + [1.0],
         "constant2": hexrgb("#4f4036") + [1.0], "constant3": hexrgb("#6d6447") + [1.0]}],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    s.terrain("moor", world=world,
              terrain={"chunkSize": 75.0, "resolution": 40, "lodLevels": 4, "lodDistance": 120.0,
                       "viewDistance": 1600.0},
              material={"program": "moorGround", "baseColor": hexrgb("#4d4238"), "roughness": 0.95,
                        "metallic": 0.0},
              scatter=[
                  {"name": "heather", "asset": kit.asset("shrub_c"), "densities": {"marsh": 2.5, "meadow": 2.5,
                                                                                   "forest": 2.5, "scree": 1.5,
                                                                                   "rim": 1.5},
                   "minScale": 0.6, "maxScale": 1.3, "height": 0.45, "randomYaw": 1.0, "clustering": 0.6,
                   "clusterScale": 6.0, "seed": 3, "meshBudget": 1200, "maxInstances": 200000,
                   "tint": hexrgb("#7a5a6c"), "viewDistance": 140.0, "castsShadow": False, "alignToGround": 0.6,
                   "motion": {"stiffness": 2.0, "mass": 0.2, "windSensitivity": 0.6}},
                  {"name": "grass", "asset": kit.asset("grass_tufts"), "densities": {"marsh": 1.5, "meadow": 1.5,
                                                                                     "forest": 1.5, "scree": 1.0,
                                                                                     "rim": 1.0},
                   "minScale": 0.7, "maxScale": 1.4, "height": 0.35, "randomYaw": 1.0, "clustering": 0.4,
                   "clusterScale": 4.0, "seed": 4, "meshBudget": 800, "maxInstances": 120000,
                   "tint": hexrgb("#a69a72"), "viewDistance": 110.0, "castsShadow": False, "alignToGround": 0.6,
                   "motion": {"stiffness": 1.0, "mass": 0.05, "windSensitivity": 1.0}},
              ])

    # ---- the ring: eleven stones on the rise, the tallest leaning on the right third
    stones = []
    for k in range(11):
        a = 2.0 * math.pi * k / 11.0 + 0.3
        x, z = RING[0] + RING_R * math.sin(a), RING[2] + RING_R * math.cos(a)
        hgt = 1.0 + 0.9 * ((k * 7) % 5) / 4.0
        stones.append((x, z, hgt, math.degrees(a) + 90.0, 0.0))
    stones.append((TALL[0], TALL[2], 3.4, 70.0, -8.0))       # the hero, taller and leaning
    hs = kit.terrain_heights(world, [(x, z) for x, z, _, _, _ in stones] + [(CAM[0], CAM[2])])
    for k, ((x, z, hgt, yaw, lean), y) in enumerate(zip(stones, hs)):
        h = hgt * 1.8
        w = 0.55 + 0.25 * ((k * 5) % 3) / 2.0 + (0.25 if k == len(stones) - 1 else 0.0)
        s.sdf("stone%d" % k, standing_stone(h, w, 0.32 + 0.06 * (k % 3), seed=k + 1),
              bounds_min=[-w - 0.6, -0.6, -1.0], bounds_max=[w + 0.6, h + 0.6, 1.0], mesh=72, shadows=True,
              material={"baseColor": hexrgb("#8a877c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                        "roughness": 0.9, "metallic": 0.0, "program": "lichenStone"},
              position=(x, y - 0.35, z), rotation=(lean, yaw, ((k * 3) % 5 - 2) * 1.5))
    cam = (CAM[0], hs[-1] + CAM[1], CAM[2])

    # ---- the foreground: a lichen boulder at the lower left, close (soft in the depth of field)
    fh = kit.terrain_heights(world, [(cam[0] - 2.6, cam[2] - 9.0)])
    s.gltf("nearstone", "moss_rocks_b", position=[cam[0] - 2.6, fh[0] - 0.3, cam[2] - 9.0], rotation=[0, 60, 0],
           scale=0.9)

    # ---- the sun: low, behind the ring, a warm back light throwing long shadows toward us
    s.light("sun", "directional", direction=sd, color=hexrgb(SUN), intensity=9.0, castsShadow=True,
            contactShadow=False, softness=1.2, volumetric=1.0)

    # ---- camera: long lens across the moor, the tall stone on the right third
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 1, "post/bloom/intensity": 0.3,
               "post/bloom/threshold": 1.2, "post/output/vignette": 0.25, "post/output/grain": 0.02,
               "post/dof/enabled": True, "post/dof/focusDistance": 52.0, "post/dof/focusRange": 26.0,
               "post/dof/maxRadius": 6.0})
    tall_y = hs[len(stones) - 1]
    tgt = kit.aim(cam, [TALL[0], tall_y + 2.4, TALL[2]], 0.66, 0.48, LENS)
    s.camera = {"mode": 1, "position": list(cam), "target": tgt, "fov": 20.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=cam, target=tgt, period=100.0, amp=(0.5, 0.05, 0.3), tamp=(0.3, 0.08, 0.0))
    return s
