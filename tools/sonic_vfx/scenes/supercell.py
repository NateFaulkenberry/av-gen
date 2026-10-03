"""SUPERCELL (awe, dread). A storm over a farm at golden hour: a vast rotating supercell lowers its wall cloud over the
plains while the setting sun, slipping under its edge, lights the wheat and a farmstead gold against the storm.

Art direction (ART-RESTART-PLAN.md #13): low in a wheat field. The sky is split: on the right a vast supercell with a
striated rotating updraft and a dark wall cloud reaches down toward the plains; on the left, under its edge, the low
sun throws gold across the field. A farmstead (a red barn, a white house, a windmill, a few trees) stands on the left
third, glowing in that light against the storm's blue-black. A line of power poles leads from the right foreground
toward the farm. Rain hangs under the far side of the storm. The wheat moves in waves.

Built from: a plains terrain with a wheat scatter (wind-driven), primitives for the farm and the poles, the tornado
medium dressed as a mesocyclone (huge radii, a lowered wall cloud, striations, no touchdown) lit and self-shadowed by
the low sun, dark alpha particles for the rain curtain.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "supercell"
TITLE = "Supercell"

STORM = "#1e2733"
SLATE = "#3a4b5c"
GOLD = "#f3b04b"
WHEAT = "#d8b46a"
BARN = "#9b2b1e"

CAM = (0.0, 1.2, 0.0)
LENS = 21.0
FARM = (-110.0, 0.0, -230.0)           # the farmstead on the left third
STORMC = (700.0, 0.0, -2200.0)         # the mesocyclone's centre on the ground
SUN_EL, SUN_AZ = 3.5, -78.0            # low in the west: degrees up, degrees right of straight ahead (-Z)

DESIGN = {
    "category": "plains and weather",
    "thesis": "A supercell over a farm at golden hour: the storm lowers its wall cloud while the sun under its edge "
              "lights the wheat and the farm gold.",
    "composition": {
        "background": "the supercell filling the right two thirds of the sky; gold light low on the left",
        "midground": "the farmstead on the left third; power poles running to it; rain under the storm",
        "foreground": "wheat, close, gold and moving",
        "focal": "the wall cloud over the plains, with the lit farm against it",
        "secondary": ["the farm", "the poles", "rain"],
        "atmosphere": "the storm's own medium; a warm haze toward the sun",
        "camera": "21 mm in the wheat at 1.2 m, a slight up tilt",
    },
    "palette": {"dominant": STORM, "secondary": SLATE, "accent": GOLD, "highlight": "#ffe2a8"},
    "motion": {"very_slow": ["the storm's turning"], "medium": ["wheat in the wind", "the windmill"], "fast": [],
               "extremely_fast": []},
    "vocabulary": [],
    "tier": "heavy: one large placed medium (self-shadowed), a dense wheat scatter",
}


def sun_dir():
    el, az = math.radians(SUN_EL), math.radians(SUN_AZ)
    return [round(-math.sin(az) * math.cos(el), 5), round(-math.sin(el), 5), round(math.cos(az) * math.cos(el), 5)]


def farm(s, y):
    """A barn, a house, a silo, a windmill and a windbreak, at FARM, standing on ground height y."""
    fx, fz = FARM[0], FARM[2]
    red = {"baseColor": hexrgb(BARN), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.85,
           "metallic": 0.0}
    white = {"baseColor": hexrgb("#d9d4c7"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.8,
             "metallic": 0.0}
    roof = {"baseColor": hexrgb("#3b3a38"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.7,
            "metallic": 0.2}
    steel = {"baseColor": hexrgb("#9aa0a3"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.4,
             "metallic": 0.8}

    def box(name, size, pos, rot=(0, 0, 0), mat=red):
        s.proc(name, {"kind": "box", "size": list(size), "subdivisions": 1}, material=mat,
               transform={"position": [pos[0], pos[1], pos[2]], "rotation": list(rot), "scale": [1, 1, 1]})
    # the barn: walls and a gambrel-ish roof of two slabs a side
    box("barn", (14.0, 8.0, 24.0), (fx, y + 4.0, fz))
    box("barnroofa", (8.6, 0.4, 25.0), (fx - 3.4, y + 9.8, fz), (0, 0, 32.0), roof)
    box("barnroofb", (8.6, 0.4, 25.0), (fx + 3.4, y + 9.8, fz), (0, 0, -32.0), roof)
    # the house
    box("house", (9.0, 6.0, 11.0), (fx + 26.0, y + 3.0, fz + 14.0), (0, 18.0, 0), white)
    box("houseroofa", (6.0, 0.3, 12.0), (fx + 24.2, y + 7.2, fz + 14.6), (0, 18.0, 38.0), roof)
    box("houseroofb", (6.0, 0.3, 12.0), (fx + 27.8, y + 7.2, fz + 13.4), (0, 18.0, -38.0), roof)
    # the silo
    s.proc("silo", {"kind": "cylinder", "radius": 3.6, "height": 20.0, "radialSegments": 32}, material=steel,
           transform={"position": [fx - 14.0, y + 10.0, fz - 8.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("silocap", {"kind": "sphere", "radius": 3.7, "segments": 32, "rings": 16}, material=steel,
           transform={"position": [fx - 14.0, y + 20.0, fz - 8.0], "rotation": [0, 0, 0], "scale": [1, 0.45, 1]})
    # the windmill: a lattice tower (a thin cylinder) and a ring of blades
    s.proc("milltower", {"kind": "cylinder", "radius": 0.35, "height": 18.0, "radialSegments": 6}, material=steel,
           transform={"position": [fx + 44.0, y + 9.0, fz - 6.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("millblades", {"kind": "box", "size": [0.6, 2.6, 0.08], "subdivisions": 1},
           distribution={"kind": "radial", "count": 14, "radius": 1.5, "plane": "xy", "orientation": "outward",
                         "center": [fx + 44.0, y + 18.4, fz - 5.6]}, material=steel)
    # a windbreak of trees behind the house
    for k in range(5):
        s.gltf("windbreak%d" % k, "tree", variant=1 + k % 5, position=[fx + 12.0 + k * 9.0, y, fz - 22.0 - (k % 2) * 6],
               rotation=[0, k * 70.0, 0], scale=1.6 + 0.2 * (k % 3), tint=[0.45, 0.5, 0.35])


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    sd = sun_dir()
    s.environment = {
        "intensity": 0.35, "skyIntensity": 1.0, "background": hexrgb("#2a2f36"), "fogColor": hexrgb("#7a6a58"),
        "volumeDensity": 0.0, "volumeMaxDistance": 0.0, "volumeSteps": 24, "volumeShadowSteps": 6,
        "volumeShadowStrength": 1.5, "volumeAnisotropy": 0.35, "volumeLocalLights": 0.0,
        "fogSky": 1.0, "fogSkyDistance": 4500.0,
        "sky": {"enabled": True, "background": True, "zenithColor": hexrgb("#262d36"),
                "horizonColor": hexrgb("#c99a62"), "groundColor": hexrgb("#2a261f"), "haze": 0.12,
                "sunIntensity": 10.0, "sunColor": hexrgb("#ffc070"), "sunSize": 0.012, "sunGlow": 0.35,
                "intensity": 1.0, "sunDirection": [-sd[0], -sd[1], -sd[2]], "useKeyLight": False},
        "shadowCascades": 3, "shadowRange": 900.0,
    }

    # ---- the plains: gentle undulation, wheat everywhere near
    world = {"seed": 41, "size": [9000.0, 9000.0], "baseHeight": 0.0,
             "layers": [{"frequency": 0.0012, "amplitude": 10.0, "warp": 300.0},
                        {"frequency": 0.008, "amplitude": 1.5}, {"frequency": 0.05, "amplitude": 0.2}],
             "features": []}
    s.program({"name": "fieldGround", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.01, "seed": 3},
        {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 1.2, "seed": 4},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 3, "constant": [0.5, 0.5, 0.5, 0.5]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
        {"kind": "ramp", "dst": 4, "srcA": 1, "constant": hexrgb("#6b5a36") + [1.0],
         "constant2": hexrgb("#9c8550") + [1.0], "constant3": hexrgb("#bca468") + [1.0]}],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    s.terrain("plains", world=world,
              terrain={"chunkSize": 120.0, "resolution": 32, "lodLevels": 4, "lodDistance": 200.0,
                       "viewDistance": 4500.0},
              material={"program": "fieldGround", "baseColor": hexrgb(WHEAT), "roughness": 0.9, "metallic": 0.0})
    # the field round the camera: the same world, smaller and 3 cm higher, carrying the wheat (a scatter layer's
    # instance cap thins it over its WHOLE map, so dense near vegetation needs a small map of its own)
    near = dict(world, size=[240.0, 240.0])
    s.terrain("field", world=near, position=(0.0, 0.03, 0.0),
              terrain={"chunkSize": 40.0, "resolution": 24, "lodLevels": 2, "viewDistance": 300.0},
              material={"program": "fieldGround", "baseColor": hexrgb(WHEAT), "roughness": 0.9, "metallic": 0.0},
              scatter=[{"name": "wheat", "asset": kit.asset("wispy"), "densities": {"marsh": 9.0, "meadow": 9.0,
                                                                                  "forest": 9.0, "scree": 9.0,
                                                                                  "rim": 9.0},
                        "minScale": 0.8, "maxScale": 1.2, "height": 0.95, "randomYaw": 1.0, "clustering": 0.15,
                        "clusterScale": 8.0, "seed": 9, "meshBudget": 300, "maxInstances": 400000,
                        "tint": hexrgb("#e6c27a"), "viewDistance": 120.0, "castsShadow": False,
                        "motion": {"stiffness": 1.2, "mass": 0.06, "windSensitivity": 1.4, "gustResponse": 1.2}}])
    hs = kit.terrain_heights(world, [(CAM[0], CAM[2]), (FARM[0], FARM[2])])
    cam = (CAM[0], hs[0] + CAM[1], CAM[2])
    farm(s, hs[1])

    # ---- the power line: poles from the right foreground to the farm
    a, b = (14.0, -18.0), (FARM[0] + 40.0, FARM[2] + 30.0)
    n = 9
    wood = {"baseColor": hexrgb("#2f2820"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.9,
            "metallic": 0.0}
    pts = [(a[0] + (b[0] - a[0]) * i / (n - 1), a[1] + (b[1] - a[1]) * i / (n - 1)) for i in range(n)]
    ph = kit.terrain_heights(world, pts)
    yaw = math.degrees(math.atan2(b[0] - a[0], b[1] - a[1]))
    for i, ((x, z), y) in enumerate(zip(pts, ph)):
        s.proc("pole%d" % i, {"kind": "cylinder", "radius": 0.16, "height": 11.0, "radialSegments": 8}, material=wood,
               transform={"position": [x, y + 5.5, z], "rotation": [((i * 7) % 5 - 2) * 0.5, yaw, 0], "scale": [1, 1, 1]})
        s.proc("arm%d" % i, {"kind": "box", "size": [2.8, 0.15, 0.15], "subdivisions": 1}, material=wood,
               transform={"position": [x, y + 10.4, z], "rotation": [0, yaw + 90.0, 0], "scale": [1, 1, 1]})

    # ---- the storm: the tornado medium dressed as a mesocyclone, no touchdown, its wall cloud lowered
    s.effect("storm", "tornado", ("world",), parameters={
        "base": [STORMC[0], 0.0, STORMC[2]], "height": 1500.0, "radiusBottom": 260.0, "radiusMid": 520.0,
        "radiusTop": 1250.0, "taper": 1.4, "shellWidth": 0.5, "shellGain": 1.0, "coreRadius": 0.7,
        "coreDensity": 0.8, "edgeSoft": 0.5, "wallCloudGain": 1.6, "cloudWidth": 3.2, "cloudHeight": 0.4,
        "cloudDensity": 1.6, "touchdown": 0.3, "footSoft": 0.4, "skirtWidth": 1.0, "skirtHeight": 0.02,
        "skirtDensity": 0.0, "stripeCount": 6.0, "stripePitch": 1.4, "stripeDepth": 0.5, "stripeHarmonic": 0.5,
        "cloudAmount": 0.85, "macroAmp": 1.0, "mesoAmp": 0.8, "microAmp": 0.4, "detailContrast": 1.6,
        "detailScale": 1.6, "climbRate": 0.02, "erosion": 1.0, "edgeWidth": 0.8, "circulation": 400.0,
        "rotationBottom": 0.3, "rotationTop": 0.15, "wobbleAmount": 0.0, "lean": [-300.0, 0.0],
        "density": 0.006, "emission": 0.0, "scattering": 1.2,
        "colorThin": hexrgb("#8f98a0"), "colorThick": hexrgb("#1a2028")})

    # ---- rain hanging under the far side of the storm: dark curtains
    s.particles("rain", capacity=20000, seed=17, shape="box", position=[STORMC[0] + 700.0, 600.0, STORMC[2] + 300.0],
                extent=[700.0, 600.0, 300.0], direction=[0.1, -1.0, 0.0], spawnRate=4000.0, lifetimeMin=4.0,
                lifetimeMax=5.0, spread=0.02, speedMin=180.0, speedMax=220.0, gravity=[0, 0, 0], drag=0.0,
                sizeStart=1.4, sizeEnd=1.4, colorStart=hexrgb("#3a434c") + [0.12], colorEnd=hexrgb("#3a434c") + [0.06],
                emissive=0.0, blend="alpha", velocityStretch=1.0, stretchMax=40.0)

    # ---- the low sun from the west, under the storm's edge
    s.light("sun", "directional", direction=sd, color=hexrgb("#ffb35c"), intensity=7.0, castsShadow=True,
            contactShadow=False, softness=1.0, volumetric=1.0)

    # ---- camera: in the wheat, the storm right, the farm on the left third
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 1, "post/bloom/intensity": 0.25,
               "post/bloom/threshold": 1.4, "post/output/vignette": 0.35, "post/output/grain": 0.025,
               "post/grade/contrast": 1.1})
    tgt = kit.aim(cam, [FARM[0], hs[1] + 6.0, FARM[2]], 0.30, 0.62, LENS)
    s.camera = {"mode": 1, "position": list(cam), "target": tgt, "fov": 60.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=cam, target=tgt, period=90.0, amp=(0.2, 0.04, 0.2), tamp=(0.4, 0.15, 0.0))
    return s
