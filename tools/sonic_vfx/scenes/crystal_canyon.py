"""CRYSTAL CANYON (wonder, heat). An impossible geode in a slot canyon at noon: a colossal cluster of pale cyan crystal
prisms bursts from the sandstone wall into the sun.

Art direction (ART-RESTART-PLAN.md #4): a low-angle view up a narrow sandstone canyon at midday. Terracotta walls rise
on both sides and frame a strip of white-hot sky. On the right wall, high up, enormous translucent crystals erupt from
the rock, pale cyan to violet, each prism the height of a house, throwing hard glints. Smaller crystals stud the dry
riverbed of pebbles and sand, which leads away from the camera. A lone walker on the riverbed gives the scale. Hard
light, black shadows, warm bounce in the shadow side. Almost no particles.

Built from: scanned red sandstone cliffs stacked and stretched into the walls, SDF hexagonal prisms (three boxes'
intersection, with a pointed tip) meshed once for the crystals, a terrain riverbed, a hard sun and a warm bounce.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "crystal-canyon"
TITLE = "Crystal Canyon"

TERRACOTTA = "#b4532a"
RUST = "#7a3418"
SAND = "#d9a66b"
SKY = "#f4efe6"
CRYSTAL = "#a6e3f0"
VIOLET = "#8f7fe0"

CAM = (0.0, 1.0, 14.0)
LENS = 20.0
SPIRES = (4.5, 0.0, -66.0)         # the crystal spires' root in the riverbed at the bend
SUN_DIR = (0.42, -0.86, 0.29)      # high sun from the upper left and a little behind the cluster

DESIGN = {
    "category": "desert canyon",
    "thesis": "An impossible geode in a slot canyon at noon: colossal cyan crystals burst from the sandstone into the "
              "sun.",
    "composition": {
        "background": "the canyon's far bend; a strip of white sky",
        "midground": "the crystal cluster high on the right wall; the riverbed",
        "foreground": "pebbles, a small crystal, the near wall's shadow",
        "focal": "the great crystal cluster in the sun",
        "secondary": ["the walker on the riverbed", "small crystals in the bed", "the light line on the walls"],
        "atmosphere": "almost none: dry, hard light",
        "camera": "20 mm, 1 m, tilted up the canyon",
    },
    "palette": {"dominant": TERRACOTTA, "secondary": SAND, "accent": CRYSTAL, "highlight": SKY},
    "motion": {"very_slow": ["the camera's drift"], "medium": [], "fast": [], "extremely_fast": []},
    "vocabulary": [],
    "tier": "medium: scanned walls (budgeted), meshed SDF crystals, a terrain",
}


def prism(r, h, tip, seed=1):
    """A hexagonal crystal prism standing on y = 0 with a pointed tip, in 8 SDF nodes: polarRepeat(6) folds space about
    Y into one 60-degree sector whose centre line is +X, so ONE box gives all six faces (|x| < r is the hexagon,
    apothem r) and ONE tilted box cuts all six facets of the tip."""
    body = kit.sd_move((0, (h + tip) * 0.5, 0), kit.sd_polar(6, kit.sd_box([r, (h + tip) * 0.5, 2.0 * r])))
    L = math.hypot(r, tip)
    phi = -math.degrees(math.atan2(tip, r))     # the cut's up axis is the tip facet's outward normal
    H = 2.0 * tip + r
    nx, ny = tip / L, r / L                                   # the cut line's upward normal
    c = (r * 0.5 + nx * H, h + tip * 0.5 + ny * H, 0.0)
    cut = kit.sd_polar(6, kit.sd_move(c, kit.sd_rot((0, 0, phi), kit.sd_box([2.0 * L, H, 3.0 * r]))))
    return kit.sd_diff(body, cut)


def cluster(spec):
    """Prisms radiating from one root: spec = [(radius, height, tilt_x, tilt_z, yaw), ...]."""
    parts = []
    for k, (r, h, tx, tz, yaw) in enumerate(spec):
        parts.append(kit.sd_rot((tx, yaw, tz), prism(r, h, r * 1.2, seed=k)))
    return kit.sd_union(*parts)


def burst(n, seed, size=1.0, spread=55.0):
    """A geode's burst of crystals: n prisms from one root, directions inside a cone round +Y (the cluster's own up),
    sizes from a steep power law (a few giants, many small), deterministic."""
    import random
    rnd = random.Random(seed)
    spec = []
    for k in range(n):
        u = rnd.random()
        h = size * (3.0 + 15.0 * u ** 2.2)
        r = h * rnd.uniform(0.07, 0.11)
        tx = rnd.uniform(-spread, spread)
        tz = rnd.uniform(-spread, spread)
        spec.append((r, h, tx, tz, rnd.uniform(0.0, 360.0)))
    return spec


def crystal_program(name):
    """Crystal: a dark, saturated body (glass is dark where it is thick: violet at the root, deep teal along the shaft,
    from |p|^2 about the cluster's origin) with bright edges where the view grazes it (fresnel as emission: light
    carried through the glass to its rims) and a faint inner glow. Glossy, so the sun throws hard glints."""
    return {"name": name, "ops": [
        {"kind": "input", "dst": 1, "input": "localPosition"},
        {"kind": "multiply", "dst": 2, "srcA": 1, "srcB": 1},
        {"kind": "gradient", "dst": 2, "srcA": 2, "constant": [1.0, 1.0, 1.0, 0.0], "value": 0.0045},
        {"kind": "noise", "dst": 3, "srcA": 1, "value": 0.35, "seed": 3},
        {"kind": "constant", "dst": 4, "constant": [0.25, 0.25, 0.25, 0.25]},
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},
        {"kind": "add", "dst": 2, "srcA": 2, "srcB": 3},
        {"kind": "ramp", "dst": 3, "srcA": 2, "constant": hexrgb("#4b3a8f") + [1.0], "constant2": hexrgb(VIOLET) + [1.0],
         "constant3": hexrgb(CRYSTAL) + [1.0]},
        {"kind": "constant", "dst": 5, "constant": [0.32, 0.36, 0.4, 1.0]},
        {"kind": "multiply", "dst": 6, "srcA": 3, "srcB": 5},
        {"kind": "fresnel", "dst": 0, "value": 2.2},
        {"kind": "multiply", "dst": 4, "srcA": 3, "srcB": 0},
        {"kind": "constant", "dst": 5, "constant": [0.12, 0.12, 0.12, 1.0]},
        {"kind": "multiply", "dst": 7, "srcA": 3, "srcB": 5},
        {"kind": "add", "dst": 4, "srcA": 4, "srcB": 7}],
        "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 2.6, "opacity": -1}


def geode():
    """The rock the crystals burst from: a lumpy dark mass half sunk in the wall."""
    rock = kit.sd_sphere(5.5)
    rock = kit.sd_diff(rock, kit.sd_move((0, 3.8, 0), kit.sd_sphere(4.6)))
    rock = kit.sd_noise(1.1, 0.35, rock, seed=4)
    return kit.sd_noise(0.3, 1.4, rock, seed=5)


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.8, "skyIntensity": 1.0, "background": hexrgb(SKY), "fogColor": hexrgb("#e8c9a8"),
        "volumeDensity": 0.0, "fogSky": 0.6, "fogSkyDistance": 600.0,
        "sky": {"enabled": True, "background": True, "zenithColor": hexrgb("#9cc2e8"),
                "horizonColor": hexrgb("#f7efe2"), "groundColor": hexrgb("#c9733e"), "haze": 0.45,
                "sunIntensity": 6.0, "sunColor": hexrgb("#fff4e2"), "sunSize": 0.01, "sunGlow": 0.06,
                "intensity": 1.0, "sunDirection": [-SUN_DIR[0], -SUN_DIR[1], -SUN_DIR[2]], "useKeyLight": False},
        "shadowCascades": 2,
    }
    s.program(crystal_program("crystal"))
    s.program({"name": "sandBed", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.12, "seed": 3},
        {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 2.5, "seed": 4},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 3, "constant": [0.5, 0.5, 0.5, 0.5]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
        {"kind": "ramp", "dst": 4, "srcA": 1, "constant": hexrgb("#a66a3e") + [1.0],
         "constant2": hexrgb(SAND) + [1.0], "constant3": hexrgb("#ecc996") + [1.0]}],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})

    # ---- the riverbed: a narrow dry bed between the walls, rising gently away
    world = {"seed": 13, "size": [400.0, 400.0], "baseHeight": 0.0,
             "layers": [{"frequency": 0.03, "amplitude": 0.6, "warp": 8.0}, {"frequency": 0.15, "amplitude": 0.12}],
             "features": [{"name": "westrim", "kind": "ridge", "path": [[-48.0, 0.0, 40.0], [-46.0, 0.0, -140.0]],
                           "width": 44.0, "amplitude": 22.0, "falloff": 1.6, "roughness": 0.5, "smoothing": 2},
                          {"name": "eastrim", "kind": "ridge", "path": [[50.0, 0.0, 40.0], [47.0, 0.0, -140.0]],
                           "width": 44.0, "amplitude": 22.0, "falloff": 1.6, "roughness": 0.5, "smoothing": 2},
                          {"name": "bed", "kind": "valley", "path": [[0.0, 0.0, 30.0], [-2.0, 0.0, -20.0],
                                                                   [4.0, 0.0, -70.0], [-6.0, 0.0, -130.0]],
                           "width": 18.0, "amplitude": 1.0, "falloff": 1.0, "flatten": 0.6, "roughness": 0.2,
                           "smoothing": 3}]}
    s.terrain("bed", world=world, terrain={"chunkSize": 40.0, "resolution": 32, "viewDistance": 300.0},
              material={"program": "sandBed", "baseColor": hexrgb(SAND), "roughness": 0.95, "metallic": 0.0},
              scatter=[{"name": "pebbles", "asset": kit.asset("pebble"), "densities": {"marsh": 1.5, "meadow": 1.5,
                                                                                     "forest": 1.5, "scree": 1.5,
                                                                                     "rim": 1.5},
                        "minScale": 0.5, "maxScale": 1.4, "height": 0.07, "randomYaw": 1.0, "clustering": 0.7,
                        "clusterScale": 3.0, "seed": 5, "meshBudget": 400, "maxInstances": 20000,
                        "viewDistance": 60.0, "castsShadow": True}])

    # ---- the walls: red sandstone scans in two overlapping ranks per side, stretched tall, the canyon bending away
    rows = []
    for side, base_x in ((-1, -10.0), (1, 11.0)):
        for k, z in enumerate((16.0, 2.0, -12.0, -26.0, -40.0, -54.0, -68.0, -82.0)):
            bend = 3.0 * math.sin(z * 0.035)
            jitter = ((k * 7 + (side + 1) * 3) % 5 - 2) * 3.0
            rows.append((base_x + bend + side * ((k * 13) % 3) * 0.8, z, (90.0 if side < 0 else -90.0) + jitter,
                         2.9 + 0.5 * ((k * 5) % 3) / 2.0, side))
    for k, (x, z, yaw, sy, side) in enumerate(rows):
        s.gltf("wall%d" % k, "red_cliff", position=[x, -1.5, z], rotation=[0.0, yaw, side * 3.0],
               scale=[1.0, sy, 1.7], tint=[1.0, 0.93, 0.86])

    # ---- the hero: colossal crystal spires grown up out of the riverbed at the canyon's bend, leaning, with smaller
    #      bursts round their feet and in the walls' cracks (an SDF holds 96 nodes; a prism is 9)
    xtal = {"baseColor": hexrgb(CRYSTAL), "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.06,
            "metallic": 0.0, "program": "crystal"}
    gy = kit.terrain_heights(world, [(SPIRES[0], SPIRES[2])])[0]
    spires = [(3.2, 44.0, -4.0, -6.0, 10.0), (2.4, 31.0, 9.0, 4.0, 40.0), (2.0, 24.0, -14.0, 12.0, 75.0),
              (1.5, 17.0, 18.0, -14.0, 130.0), (1.2, 12.0, -6.0, 26.0, 200.0), (1.0, 9.0, 22.0, 20.0, 250.0),
              (0.8, 6.0, -25.0, -18.0, 300.0), (0.7, 5.0, 12.0, -30.0, 330.0), (0.6, 4.0, -30.0, 8.0, 20.0)]
    s.sdf("spires", cluster(spires), bounds_min=[-26.0, -2.0, -26.0], bounds_max=[26.0, 52.0, 26.0], mesh=128,
          shadows=True, material=xtal, position=(SPIRES[0], gy - 1.0, SPIRES[2]), rotation=(0.0, 0.0, -4.0))
    # smaller spire groups on ledges of both walls, leaning out over the bed
    for k, (pos, rot, size) in enumerate((((-8.6, 6.5, -24.0), (0.0, 0.0, -28.0), 0.32),
                                          ((9.4, 4.0, -10.0), (0.0, 30.0, 32.0), 0.24),
                                          ((-8.0, 2.0, -46.0), (0.0, 0.0, -20.0), 0.4))):
        grp = [(r * size / 0.32, h * size / 0.32, tx, tz, yaw) for (r, h, tx, tz, yaw) in
               [(0.9, 9.0, -5.0, 4.0, 0.0), (0.7, 6.5, 14.0, -8.0, 60.0), (0.6, 5.0, -16.0, 12.0, 140.0),
                (0.45, 3.5, 20.0, 18.0, 220.0), (0.4, 3.0, -22.0, -16.0, 300.0)]]
        s.sdf("ledge%d" % k, cluster(grp), bounds_min=[-8.0, -1.0, -8.0], bounds_max=[8.0, 12.0, 8.0], mesh=80,
              shadows=True, material=xtal, position=pos, rotation=rot)
    hs = kit.terrain_heights(world, [(-3.0, -6.0), (4.0, -20.0), (-1.5, -40.0), (CAM[0], CAM[2]), (1.2, -16.0)])
    for k, ((x, z), y) in enumerate(zip([(-3.0, -6.0), (4.0, -20.0), (-1.5, -40.0)], hs[:3])):
        small = [(0.25, 1.6, -15.0, 10.0, 0.0), (0.18, 1.1, 20.0, -25.0, 80.0), (0.15, 0.8, 5.0, 40.0, 160.0)]
        s.sdf("shard%d" % k, cluster(small), bounds_min=[-2.0, -0.5, -2.0], bounds_max=[2.0, 2.4, 2.0], mesh=80,
              shadows=True, material={"baseColor": hexrgb(CRYSTAL), "emissiveColor": [1, 1, 1],
                                      "emissiveIntensity": 1.0, "roughness": 0.08, "metallic": 0.0,
                                      "program": "crystal"},
              position=(x, y - 0.2, z), rotation=(0.0, k * 50.0, 0.0))
    s.figure("walker", [1.2, hs[4], -16.0], facing=170.0)
    cam = (CAM[0], hs[3] + CAM[1], CAM[2])

    # ---- light: the hard noon sun, a warm bounce off the sandstone
    s.light("sun", "directional", direction=list(SUN_DIR), color=hexrgb("#fff4e2"), intensity=12.0,
            castsShadow=True, contactShadow=True, softness=0.6)
    s.light("bounce", "directional", direction=[-0.3, 0.6, -0.2], color=hexrgb("#ff9a5c"), intensity=1.2,
            castsShadow=False)

    # ---- camera: low, tilted up the canyon, the cluster upper right
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 1, "post/bloom/intensity": 0.2,
               "post/bloom/threshold": 1.5, "post/output/vignette": 0.25, "post/output/grain": 0.015})
    tgt = kit.aim(cam, [SPIRES[0], 18.0, SPIRES[2]], 0.6, 0.36, LENS)
    s.camera = {"mode": 1, "position": list(cam), "target": tgt, "fov": 60.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=cam, target=tgt, period=90.0, amp=(0.3, 0.05, 0.4), tamp=(0.4, 0.2, 0.0))
    return s
