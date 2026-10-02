"""THE LAVA FIELD (energy, danger). A fissure eruption at night: a fountaining vent feeds a river of lava that winds
across a black basalt plain toward the camera, under a plume of steam lit orange from below.

Art direction (ART-RESTART-PLAN.md #15): low on a black basalt shelf at night. A river of lava winds from the right
middle distance toward the left foreground, its crust broken into black plates with incandescent seams. Behind it a
fissure fountains lava and lights the base of a towering plume whose underside glows orange. Rough glassy black rock
fills the foreground and catches the orange on its edges. Distant cones stand on the horizon. Sparks rise; steam
drifts across.

The light IS the lava: the river is an emissive ribbon (cells with glowing seams, drifting) with real point lights
along it, so the basalt round it is lit by it; the fountain is particles and one strong light; the plume is the placed
fog medium, lit from below by those lights.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "lava-field"
TITLE = "The Lava Field"

BLACK = "#0a0807"
LAVA = "#ff6a1a"
HOT = "#ffd27a"
EMBER = "#b8240f"
ASH = "#4d4844"

CAM = (-27.0, 3.2, 20.0)
LENS = 24.0
# the river's course: from the vent (far right) to the left foreground
RIVER = [(68.0, -150.0), (52.0, -112.0), (30.0, -84.0), (24.0, -58.0), (8.0, -38.0), (-2.0, -22.0), (-12.0, -8.0),
         (-24.0, 4.0), (-40.0, 12.0)]
VENT = (74.0, 0.0, -158.0)

DESIGN = {
    "category": "volcanic",
    "thesis": "A fissure eruption at night: a fountaining vent feeds a river of lava across a black plain, under a "
              "plume lit from below.",
    "composition": {
        "background": "distant cones; the plume rising into the night, its underside orange",
        "midground": "the fountaining vent; the river's far bends",
        "foreground": "glassy black rock catching the orange; the river's near bend",
        "focal": "the vent's fountain where the river begins",
        "secondary": ["the river as the leading line", "the lit plume", "sparks"],
        "atmosphere": "steam drifting low over the river; the plume",
        "camera": "24 mm, 1 m above the rock, the river leading in from the lower left",
    },
    "palette": {"dominant": BLACK, "secondary": ASH, "accent": LAVA, "highlight": HOT},
    "motion": {"very_slow": ["the plume"], "medium": ["the crust's drift"], "fast": ["the fountain"],
               "extremely_fast": []},
    "vocabulary": [],
    "tier": "medium: a terrain, an emissive ribbon, 8 point lights, one placed medium, one particle fountain",
}


def lava_program(name, strength, vent=(74.0, -158.0), cam=(-27.0, 20.0)):
    """Lava under a broken crust. Thin incandescent cracks (a cell network, domain-warped so the cracks wander, drifting
    with the flow) and patches where the crust is thin, both hotter toward the vent (a gradient along the flow), so
    the river cools from a white-orange source to a dark crust webbed with red downstream. The crust itself is
    near-black."""
    dx, dz = vent[0] - cam[0], vent[1] - cam[1]
    n = math.hypot(dx, dz)
    ux, uz = dx / n, dz / n
    lo = ux * cam[0] + uz * cam[1]
    f = 0.85 / n                       # 0.15 at the camera's end, 1.0 at the vent
    return {
        "name": name,
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.6, "seed": 11},
            {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.6, "seed": 12},
            {"kind": "constant", "dst": 3, "constant": [0.8, 0.0, 0.0, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
            {"kind": "constant", "dst": 3, "constant": [0.0, 0.0, 0.8, 0.0]},
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "constant", "dst": 2, "constant": [1.0, 0.0, 1.0, 0.0]},
            {"kind": "multiply", "dst": 2, "srcA": 0, "srcB": 2},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 1},
            {"kind": "input", "dst": 1, "input": "time"},
            {"kind": "constant", "dst": 3, "constant": [-0.22 * ux, 0.04, -0.22 * uz, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 1},
            {"kind": "voronoiEdge", "dst": 3, "srcA": 2, "value": 0.85, "seed": 9},
            {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.075, 0.0, 0.0, 0.0]},
            {"kind": "swizzle", "dst": 3, "srcA": 3, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "noise", "dst": 4, "srcA": 2, "value": 0.11, "seed": 4},
            {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.5, 0.82, 0.0, 0.0]},
            {"kind": "gradient", "dst": 5, "srcA": 0, "constant": [ux, 0.0, uz, round(0.15 - lo * f, 5)],
             "value": round(f, 6)},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
            {"kind": "multiply", "dst": 6, "srcA": 3, "srcB": 5},
            {"kind": "add", "dst": 6, "srcA": 6, "srcB": 4},
            {"kind": "ramp", "dst": 7, "srcA": 6, "constant": [0.0, 0.0, 0.0, 1.0],
             "constant2": hexrgb("#e2400c") + [1.0], "constant3": hexrgb("#ffe0a0") + [1.0]},
            {"kind": "constant", "dst": 6, "constant": hexrgb("#110c0a") + [1.0]},
        ],
        "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": float(strength),
        "opacity": -1,
    }


def basalt_program(name):
    """Black basalt: near-black with a faint rust and grey mottling."""
    return {
        "name": name,
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.12, "seed": 2},
            {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 3.0, "seed": 3},
            {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "constant", "dst": 3, "constant": [0.5, 0.5, 0.5, 0.5]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
            {"kind": "ramp", "dst": 4, "srcA": 1, "constant": hexrgb("#050404") + [1.0],
             "constant2": hexrgb("#14110f") + [1.0], "constant3": hexrgb("#2c2520") + [1.0]},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.05, "skyIntensity": 1.0, "background": hexrgb("#020308"), "fogColor": hexrgb("#120c0b"),
        "volumeDensity": 0.006, "fogHeight": 0.0, "fogHeightFalloff": 0.6, "fogHeightCurve": 0.6,
        "volumeMaxDistance": 260.0, "volumeSteps": 16, "volumeAnisotropy": 0.2,
        "volumeScattering": 1.0, "volumeAbsorption": 0.6, "volumeLocalLights": 1.0, "fogSky": 0.6,
        "fogSkyDistance": 500.0,
        "sky": {"enabled": True, "background": True, "zenithColor": hexrgb("#020308"),
                "horizonColor": hexrgb("#120806"), "groundColor": hexrgb("#030303"), "haze": 0.1,
                "sunIntensity": 0.0, "sunColor": [1, 1, 1], "sunSize": 0.01, "sunGlow": 0.05, "intensity": 1.0,
                "useKeyLight": False},
        "shadowCascades": 2,
    }
    s.program(lava_program("lfLava", 14.0, vent=(VENT[0], VENT[2]), cam=(CAM[0], CAM[2])))
    s.program(basalt_program("lfBasalt"))

    # ---- the plain: rough flows, the river's channel cut into them, cones far off
    channel = [[x, -1.2, z] for x, z in RIVER]
    world = {
        "seed": 23, "size": [900.0, 900.0], "baseHeight": 0.0,
        "layers": [{"frequency": 0.008, "amplitude": 4.0, "warp": 30.0},
                   {"frequency": 0.04, "amplitude": 1.4, "ridged": 0.8, "warp": 6.0},
                   {"frequency": 0.18, "amplitude": 0.35, "ridged": 0.6}],
        "features": [
            {"name": "channel", "kind": "river", "path": channel, "width": 9.0, "amplitude": 1.6, "falloff": 0.9,
             "flatten": 0.9, "roughness": 0.2, "water": False, "smoothing": 3},
            {"name": "cone1", "kind": "ridge", "path": [[-160.0, 0.0, -380.0]], "width": 260.0, "amplitude": 95.0,
             "falloff": 1.6, "roughness": 0.5, "smoothing": 2},
            {"name": "cone2", "kind": "ridge", "path": [[150.0, 0.0, -420.0]], "width": 200.0, "amplitude": 70.0,
             "falloff": 1.8, "roughness": 0.5, "smoothing": 2},
            {"name": "cone3", "kind": "ridge", "path": [[20.0, 0.0, -430.0]], "width": 140.0, "amplitude": 40.0,
             "falloff": 1.8, "roughness": 0.5, "smoothing": 2},
        ]}
    s.terrain("plain", world=world, terrain={"chunkSize": 60.0, "resolution": 48, "lodLevels": 3,
                                              "viewDistance": 700.0},
              material={"program": "lfBasalt", "baseColor": hexrgb("#14110f"), "roughness": 0.75, "metallic": 0.0})
    rocks = [(-31.0, 15.0, 2.4, 30.0), (-22.0, 13.5, 1.6, 110.0), (-17.0, 17.5, 2.8, 200.0)]
    probe = [(CAM[0], CAM[2]), (VENT[0], VENT[2])] + [(x, z) for x, z in RIVER] + [(x, z) for x, z, _, _ in rocks]
    h = kit.terrain_heights(world, probe)
    cam = (CAM[0], h[0] + CAM[1], CAM[2])
    vent = (VENT[0], h[1], VENT[2])
    bed = h[2:2 + len(RIVER)]
    rock_h = h[2 + len(RIVER):]

    # ---- the river: a tube along the channel's bed, flattened into a ribbon (the scale flattens the path's heights
    #      too, so they are written pre-divided), emissive
    flat = 0.08
    pts = [{"position": [x, (b + 0.35) / flat, z], "scale": 1.0, "roll": 0.0} for (x, z), b in zip(RIVER, bed)]
    s.proc("river", {"kind": "tube", "tubeRadius": 4.6, "tubeTaper": 1.0, "tubeSides": 12, "tubeSegments": 240,
                     "tubeTwist": 0.0, "tubeCaps": False,
                     "curve": {"kind": "catmullRom", "generator": "points", "points": pts, "samplesPerSegment": 12}},
           material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.6,
                     "metallic": 0.0, "program": "lfLava"},
           transform={"position": [0.0, 0.0, 0.0], "rotation": [0, 0, 0], "scale": [1.0, flat, 1.0]})

    # ---- light along the river (it lights the basalt), and the vent's
    for k, ((x, z), b) in enumerate(zip(RIVER[1:], bed[1:])):
        s.light("lava%d" % k, "point", position=[x, b + 5.0, z], color=hexrgb(LAVA), intensity=500.0, range=28.0,
                radius=4.0, castsShadow=False, volumetric=0.02)
    s.light("vent", "point", position=[vent[0], vent[1] + 8.0, vent[2]], color=hexrgb("#ff8a3a"), intensity=6000.0,
            range=140.0, radius=6.0, castsShadow=False, volumetric=0.15)

    # ---- the fountain: incandescent spatter thrown up from the vent
    s.particles("fountain", capacity=6000, seed=5, shape="disc", position=[vent[0], vent[1] + 0.5, vent[2]],
                extent=[6.0, 0.0, 0.0], direction=[0.0, 1.0, 0.0], spawnRate=900.0, lifetimeMin=1.6,
                lifetimeMax=2.6, spread=0.16, speedMin=14.0, speedMax=24.0, gravity=[0, -9.8, 0], drag=0.1,
                sizeStart=0.55, sizeEnd=0.25, colorStart=hexrgb(HOT) + [1.0], colorEnd=hexrgb(EMBER) + [0.8],
                emissive=14.0, blend="additive", velocityStretch=0.6, stretchMax=3.0, turbulence=0.2)

    # ---- the plume: the placed medium above the vent, lit from below by the lava's lights
    s.fogbank("plume", (vent[0] - 10.0, vent[1] + 90.0, vent[2] - 10.0), shape="Ellipsoid", density=0.6, radius=55.0,
              height=110.0, color=hexrgb("#8a7a72"), deepColor=hexrgb("#2a2220"), edgeSoftness=0.6,
              heightFalloff=0.4)

    # ---- the night sky
    s.effect("stars", "stars", ("world",), parameters={"brightness": 1.2, "density": 0.03, "twinkle": 0.2,
                                                       "twinkleRate": 2.0, "band": 0.6, "bandTilt": 0.5})

    # ---- the foreground: glassy black rock
    for k, ((x, z, sc, yaw), rh) in enumerate(zip(rocks, rock_h)):
        s.gltf("rock%d" % k, "boulder", position=[x, rh - 0.2, z], rotation=[0, yaw, 0], scale=sc,
               tint=[0.18, 0.16, 0.15], roughness=0.6)

    # ---- camera: low, the river leading in from the lower left to the vent on the right third
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 3, "post/bloom/intensity": 0.45,
               "post/bloom/threshold": 1.0, "post/output/vignette": 0.45, "post/output/grain": 0.03})
    tgt = kit.aim(cam, [vent[0], vent[1] + 12.0, vent[2]], 0.70, 0.42, LENS)
    s.camera = {"mode": 1, "position": list(cam), "target": tgt, "fov": 50.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=cam, target=tgt, period=80.0, amp=(0.4, 0.06, 0.3), tamp=(0.8, 0.3, 0.0))
    return s
