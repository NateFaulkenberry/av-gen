"""NORTHERN FJORD (wonder, stillness). An Arctic fjord at night under the aurora: a steep snow peak doubled in black
still water, three red fishing cabins on stilts with one window lit.

Art direction (ART-RESTART-PLAN.md #5): a wide night view across the still fjord. A steep snow-streaked peak rises
left of centre; the aurora's green curtain arcs over it into the right of the sky, rays hanging down. The fjord mirrors
it all. On the right third, on a rocky point, three red cabins stand on stilts over the water, one window lit warm.
Snowy boulders fill the foreground shore. The far shore is a dark band of mountains with snow on their shoulders.
The moonless sky is deep blue-black with stars.

Built from: a terrain fjord (seaLevel) with ridged mountains as features and a snow program (white where the ground
faces up and is high, rock on the steep faces), primitives for the cabins, the aurora and stars effects, a clear
mirror sea.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "northern-fjord"
TITLE = "Northern Fjord"

NIGHT = "#070b16"
DEEP = "#10213a"
AURORA = "#4cf08c"
VIOLET = "#8a5cf0"
SNOW = "#cfd8e3"
CABIN = "#8e1f17"
WINDOW = "#ffb35a"

CAM = (0.0, 1.6, 30.0)
LENS = 24.0
PEAK = (-260.0, 0.0, -1300.0)
CABINS = (95.0, 0.0, -150.0)

DESIGN = {
    "category": "fjord",
    "thesis": "An Arctic fjord at night under the aurora: a snow peak doubled in black water, red cabins with one "
              "window lit.",
    "composition": {
        "background": "the aurora arching over the peak; stars; the far shore's mountains",
        "midground": "the peak and its mirror; the cabins on the right third",
        "foreground": "snowy boulders and dark rocks on the shore",
        "focal": "the peak under the aurora",
        "secondary": ["the cabins and the lit window", "the mirror"],
        "atmosphere": "clear, still, cold",
        "camera": "24 mm on the shore at 1.6 m",
    },
    "palette": {"dominant": NIGHT, "secondary": DEEP, "accent": AURORA, "highlight": WINDOW},
    "motion": {"very_slow": ["the aurora's drift"], "medium": [], "fast": [], "extremely_fast": []},
    "vocabulary": [],
    "tier": "medium: a large terrain, the aurora (sky), a few primitives",
}


def snow_program():
    """Snow on the mountains: white where the ground faces up (and higher up, more of it), dark rock on the steep
    faces, a little noise in the line between."""
    return {"name": "fjordSnow", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "input", "dst": 1, "input": "normal"},
        {"kind": "swizzle", "dst": 1, "srcA": 1, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.03, "seed": 4},
        {"kind": "constant", "dst": 3, "constant": [0.35, 0.35, 0.35, 0.35]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "gradient", "dst": 4, "srcA": 0, "constant": [0.0, 1.0, 0.0, 0.0], "value": 0.004},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 4},
        {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.78, 0.98, 0.0, 0.0]},
        {"kind": "noise", "dst": 5, "srcA": 0, "value": 0.2, "seed": 5},
        {"kind": "ramp", "dst": 6, "srcA": 5, "constant": hexrgb("#0e1013") + [1.0],
         "constant2": hexrgb("#24272c") + [1.0], "constant3": hexrgb("#3a3d42") + [1.0]},
        {"kind": "constant", "dst": 7, "constant": hexrgb(SNOW) + [1.0]},
        {"kind": "mixBy", "dst": 6, "srcA": 6, "srcB": 7, "srcC": 1}],
        "baseColor": 6, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1}


def cabins(s, y):
    """Three red rorbu cabins on stilts over the water on a rocky point, one window lit."""
    red = {"baseColor": hexrgb(CABIN), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.8,
           "metallic": 0.0}
    roof = {"baseColor": hexrgb("#1a1a1c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.6,
            "metallic": 0.0}
    wood = {"baseColor": hexrgb("#2a221c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.9,
            "metallic": 0.0}
    for k, (dx, dz, yaw, w, d) in enumerate([(0.0, 0.0, 10.0, 7.0, 5.0), (10.0, -5.0, 14.0, 6.0, 5.0),
                                             (19.0, -12.0, 6.0, 7.5, 5.5)]):
        x, z = CABINS[0] + dx, CABINS[2] + dz
        h = 4.2
        s.proc("cabin%d" % k, {"kind": "box", "size": [w, h, d], "subdivisions": 1}, material=red,
               transform={"position": [x, y + 1.6 + h * 0.5, z], "rotation": [0, yaw, 0], "scale": [1, 1, 1]})
        for side, ang, off in (("a", 38.0, 1.0), ("b", -38.0, -1.0)):
            s.proc("cabin%droof%s" % (k, side), {"kind": "box", "size": [w + 0.6, 0.25, d * 0.66], "subdivisions": 1},
                   material=roof,
                   transform={"position": [x - math.sin(math.radians(yaw)) * 0.0, y + 1.6 + h + 1.0,
                                           z + off * d * 0.27], "rotation": [ang, yaw, 0], "scale": [1, 1, 1]})
        s.proc("cabin%dstilts" % k, {"kind": "cylinder", "radius": 0.14, "height": 4.0, "radialSegments": 6},
               distribution={"kind": "grid", "gridCount": [3, 1, 2], "gridSpacing": [w * 0.45, 1.0, d * 0.8]},
               material=wood,
               transform={"position": [x, y - 0.4, z], "rotation": [0, yaw, 0], "scale": [1, 1, 1]})
    s.proc("window", {"kind": "box", "size": [1.0, 0.9, 0.1], "subdivisions": 1},
           material={"baseColor": [0, 0, 0], "emissiveColor": hexrgb(WINDOW), "emissiveIntensity": 7.0,
                     "roughness": 0.5, "metallic": 0.0},
           transform={"position": [CABINS[0] + 10.6, y + 3.6, CABINS[2] - 2.4], "rotation": [0, 14.0, 0],
                      "scale": [1, 1, 1]})
    s.light("window", "point", position=[CABINS[0] + 10.6, y + 3.6, CABINS[2] - 1.4], color=hexrgb(WINDOW),
            intensity=300.0, range=18.0, radius=0.4, castsShadow=False, volumetric=0.2)


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.25, "skyIntensity": 1.0, "background": hexrgb(NIGHT), "fogColor": hexrgb("#0c1626"),
        "volumeDensity": 0.0, "fogSky": 0.8, "fogSkyDistance": 6000.0,
        "sky": {"enabled": True, "background": True, "zenithColor": [0.004, 0.007, 0.02],
                "horizonColor": [0.02, 0.05, 0.08], "groundColor": hexrgb("#05070a"), "haze": 0.12,
                "sunIntensity": 0.0, "sunColor": [1, 1, 1], "sunSize": 0.01, "sunGlow": 0.05, "intensity": 1.0,
                "useKeyLight": False},
        "shadowCascades": 2,
    }
    s.program(snow_program())

    # ---- the fjord: water to the far shore; the peak left of centre; mountains along both banks; the point
    world = {"seed": 61, "size": [8000.0, 8000.0], "baseHeight": -40.0, "seaLevel": 0.0,
             "layers": [{"frequency": 0.0015, "amplitude": 30.0, "ridged": 0.6, "warp": 200.0},
                        {"frequency": 0.006, "amplitude": 12.0, "ridged": 0.8}],
             "features": [
                 {"name": "peak", "kind": "ridge", "path": [[PEAK[0], 0.0, PEAK[2]]], "width": 900.0,
                  "amplitude": 620.0, "falloff": 0.9, "roughness": 2.5, "smoothing": 1},
                 {"name": "westwall", "kind": "ridge", "path": [[-900.0, 0.0, 200.0], [-700.0, 0.0, -600.0],
                                                                [-900.0, 0.0, -2000.0]],
                  "width": 700.0, "amplitude": 380.0, "falloff": 1.0, "roughness": 2.0, "smoothing": 2},
                 {"name": "farshore", "kind": "ridge", "path": [[-2000.0, 0.0, -2600.0], [400.0, 0.0, -2500.0],
                                                                [2600.0, 0.0, -2800.0]],
                  "width": 900.0, "amplitude": 420.0, "falloff": 1.1, "roughness": 2.0, "smoothing": 2},
                 {"name": "eastwall", "kind": "ridge", "path": [[1200.0, 0.0, 300.0], [1000.0, 0.0, -900.0],
                                                                [1400.0, 0.0, -2000.0]],
                  "width": 800.0, "amplitude": 300.0, "falloff": 1.1, "roughness": 2.0, "smoothing": 2},
                 {"name": "point", "kind": "ridge", "path": [[CABINS[0] + 30.0, 0.0, CABINS[2] - 10.0],
                                                             [CABINS[0] + 200.0, 0.0, CABINS[2] + 40.0]],
                  "width": 60.0, "amplitude": 44.0, "falloff": 1.2, "roughness": 1.0, "smoothing": 2},
                 {"name": "shore", "kind": "ridge", "path": [[-200.0, 0.0, CAM[2] + 25.0], [200.0, 0.0, CAM[2] + 20.0]],
                  "width": 60.0, "amplitude": 42.0, "falloff": 1.0, "roughness": 1.2, "smoothing": 2},
             ]}
    s.terrain("fjord", world=world,
              terrain={"chunkSize": 200.0, "resolution": 40, "lodLevels": 4, "lodDistance": 300.0,
                       "viewDistance": 7000.0,
                       "water": {"enabled": True, "shallow": 2.0, "shallowColor": hexrgb("#0b141c"),
                                 "deepColor": hexrgb("#020408"), "clarity": 0.5, "maxOpacity": 0.98,
                                 "edgeFade": 0.4, "roughness": 0.01, "fresnel": 0.7, "reflection": 3.0,
                                 "reflectionTint": [1.0, 1.0, 1.0], "specular": 1.5, "ripple": 0.03,
                                 "rippleScale": 0.4, "rippleSpeed": 0.1, "chop": 0.0, "foam": 0.0}},
              material={"program": "fjordSnow", "baseColor": hexrgb(SNOW), "roughness": 0.85, "metallic": 0.0})
    hs = kit.terrain_heights(world, [(CAM[0], CAM[2]), (CABINS[0], CABINS[2])])
    cam = (CAM[0], max(hs[0], 0.0) + CAM[1], CAM[2])
    cabins(s, 0.0)

    # ---- the sky: the aurora and the stars
    s.effect("aurora", "aurora", ("world",), parameters={
        "lowColor": hexrgb(AURORA), "midColor": hexrgb("#2bd98a"), "topColor": hexrgb(VIOLET), "intensity": 3.0,
        "curtainHeight": 2400.0, "curtains": 3, "flowSpeed": 0.05, "audioSensitivity": 0.0, "spectrumShape": 0.0})
    s.effect("stars", "stars", ("world",), parameters={"brightness": 1.6, "density": 0.04, "twinkle": 0.25,
                                                       "twinkleRate": 1.5, "band": 0.8, "bandTilt": 0.6})

    # ---- a faint cold fill so the snow reads (the aurora's green on the snow, by colour)
    s.light("auroralight", "directional", direction=[0.25, -0.55, 0.8], color=hexrgb("#5fe0a8"), intensity=0.25,
            castsShadow=False)

    # ---- camera: on the shore, the peak left of centre, the cabins on the right third
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 3, "post/bloom/intensity": 0.3,
               "post/bloom/threshold": 1.0, "post/output/vignette": 0.3, "post/output/grain": 0.03})
    tgt = kit.aim(cam, [PEAK[0], 260.0, PEAK[2]], 0.40, 0.42, LENS)
    s.camera = {"mode": 1, "position": list(cam), "target": tgt, "fov": 60.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=cam, target=tgt, period=100.0, amp=(0.3, 0.04, 0.2), tamp=(1.0, 0.4, 0.0))
    return s
