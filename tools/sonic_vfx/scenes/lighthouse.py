"""THE LIGHTHOUSE (isolation, melancholy). Blue hour on a calm sea: from a high cliff top, a white lighthouse on a
dark rock island across the bay begins to sweep its beam through the sea mist; the last orange band of sunset lies on
the horizon behind it.

Art direction (ART-RESTART-PLAN.md #12): from the cliff top, looking down and across the calm bay to the island with
its lighthouse and keeper's cottage. The beam is just starting to turn, a pale cone in the low mist on the water. The
sunset's last band glows on the horizon behind the island; everything else is cool blue. In the foreground the cliff
edge with grass and a weathered fence post. Dark sea stacks stand off the coast on the left. The calm sea holds the
band of sunset light.

Built from: a terrain sea (seaLevel) with the island and the cliff as ridges, scanned coastal cliffs dressing the
island and the stacks, a primitive tower (a tapered tube, a lantern room, a gallery, a cap), a spot light turning on a
timeline track with a lightBeam riding it, a low fog bank on the water.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "lighthouse"
TITLE = "The Lighthouse"

BLUE = "#2b3f63"
BLUE_LIGHT = "#4a6a96"
BAND = "#f29b5a"
WHITE = "#e9ecef"
ROCK = "#1d2026"

CAM = (-40.0, 1.7, 76.0)           # on the cliff top (y is the eye's height above the ground)
LENS = 40.0
ISLAND = (60.0, 0.0, -230.0)
TOWER_H = 26.0

DESIGN = {
    "category": "sea cliffs",
    "thesis": "Blue hour on a calm sea: a white lighthouse on a rock island begins to sweep its beam through the mist.",
    "composition": {
        "background": "the last orange band of sunset on the horizon; the sky deepening to blue",
        "midground": "the island, the lighthouse and its cottage; the beam in the mist; sea stacks on the left",
        "foreground": "the cliff edge with grass and a fence post",
        "focal": "the lighthouse's lamp",
        "secondary": ["the beam", "the cottage's lit window", "the stacks"],
        "atmosphere": "a low mist lying on the water",
        "camera": "50 mm from the cliff top, 40 m above the sea",
    },
    "palette": {"dominant": BLUE, "secondary": BLUE_LIGHT, "accent": BAND, "highlight": WHITE},
    "motion": {"very_slow": ["the beam's turn", "the swell"], "medium": ["grass"], "fast": [], "extremely_fast": []},
    "vocabulary": [],
    "tier": "medium: a terrain sea, scanned rocks (budgeted), one shadowless spot and its beam, a fog bank",
}


def tower(s, base_y):
    """The lighthouse: a tapered white tower (a tube along a vertical line, tapering), a black lantern room with its
    lamp, a gallery ring and a cap; the keeper's cottage beside it."""
    x, z = ISLAND[0], ISLAND[2]
    white = {"baseColor": hexrgb("#e6e6e0"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
             "roughness": 0.7, "metallic": 0.0}
    black = {"baseColor": hexrgb("#16181a"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
             "roughness": 0.5, "metallic": 0.6}
    s.proc("tower", {"kind": "tube", "tubeRadius": 3.0, "tubeTaper": 0.68, "tubeSides": 40, "tubeSegments": 8,
                     "tubeTwist": 0.0, "tubeCaps": True,
                     "curve": {"kind": "polyline", "generator": "points",
                               "points": [{"position": [x, base_y, z], "scale": 1.0, "roll": 0.0},
                                          {"position": [x, base_y + TOWER_H, z], "scale": 1.0, "roll": 0.0}],
                               "samplesPerSegment": 4}}, material=white)
    top = base_y + TOWER_H
    s.proc("gallery", {"kind": "cylinder", "radius": 2.9, "height": 0.4, "radialSegments": 40}, material=black,
           transform={"position": [x, top + 0.2, z], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("lantern", {"kind": "cylinder", "radius": 1.6, "height": 2.6, "radialSegments": 24},
           material={"baseColor": [0, 0, 0], "emissiveColor": hexrgb("#ffe4b0"), "emissiveIntensity": 8.0,
                     "roughness": 0.2, "metallic": 0.0},
           transform={"position": [x, top + 1.7, z], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("cap", {"kind": "sphere", "radius": 1.8, "segments": 24, "rings": 12}, material=black,
           transform={"position": [x, top + 3.0, z], "rotation": [0, 0, 0], "scale": [1, 0.6, 1]})
    # the keeper's cottage, a window lit
    cx, cz = x - 12.0, z + 6.0
    s.proc("cottage", {"kind": "box", "size": [9.0, 4.5, 6.0], "subdivisions": 1}, material=white,
           transform={"position": [cx, base_y + 2.25, cz], "rotation": [0, 20, 0], "scale": [1, 1, 1]})
    for side, ang in (("a", 35.0), ("b", -35.0)):
        s.proc("cottageroof" + side, {"kind": "box", "size": [9.4, 0.3, 3.9], "subdivisions": 1}, material=black,
               transform={"position": [cx, base_y + 5.4, cz + (1.5 if side == "a" else -1.5)],
                          "rotation": [ang, 20, 0], "scale": [1, 1, 1]})
    s.proc("window", {"kind": "box", "size": [1.2, 1.0, 0.1], "subdivisions": 1},
           material={"baseColor": [0, 0, 0], "emissiveColor": hexrgb("#ffb35a"), "emissiveIntensity": 6.0,
                     "roughness": 0.5, "metallic": 0.0},
           transform={"position": [cx + 1.0, base_y + 2.2, cz + 3.05], "rotation": [0, 20, 0], "scale": [1, 1, 1]})
    return top + 1.7


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.5, "skyIntensity": 1.0, "background": hexrgb("#1c2a45"), "fogColor": hexrgb("#3c5478"),
        "volumeDensity": 0.0, "fogSky": 1.0, "fogSkyDistance": 2400.0, "volumeLocalLights": 1.0,
        "sky": {"enabled": True, "background": True, "zenithColor": [0.02, 0.05, 0.2],
                "horizonColor": [0.8, 0.3, 0.1], "groundColor": hexrgb("#141b28"), "haze": 0.03,
                "sunIntensity": 0.0, "sunColor": hexrgb(BAND), "sunSize": 0.01, "sunGlow": 0.2, "intensity": 1.0,
                "sunDirection": [0.25, -0.03, -0.97], "useKeyLight": False},
        "shadowCascades": 2,
    }

    # ---- the sea, the island, the cliff the camera stands on, the stacks
    world = {"seed": 51, "size": [3000.0, 3000.0], "baseHeight": -22.0, "seaLevel": 0.0,
             "layers": [{"frequency": 0.004, "amplitude": 4.0}, {"frequency": 0.03, "amplitude": 1.0}],
             "features": [
                 {"name": "island", "kind": "ridge", "path": [[ISLAND[0], 0.0, ISLAND[2]]], "width": 70.0,
                  "amplitude": 30.0, "falloff": 1.6, "roughness": 1.2, "smoothing": 2},
                 {"name": "islandtop", "kind": "flat", "path": [[ISLAND[0] - 4.0, 7.5, ISLAND[2]]], "width": 30.0,
                  "flatten": 0.9, "falloff": 1.0, "smoothing": 2},
                 {"name": "cliff", "kind": "ridge", "path": [[-400.0, 0.0, 120.0], [-60.0, 0.0, 70.0],
                                                             [200.0, 0.0, 110.0]],
                  "width": 80.0, "amplitude": 60.0, "falloff": 0.7, "roughness": 1.0, "smoothing": 2},
                 {"name": "clifftop", "kind": "flat", "path": [[-60.0, 32.0, 85.0], [40.0, 32.0, 95.0]],
                  "width": 60.0, "flatten": 0.8, "falloff": 1.0, "smoothing": 2},

             ]}
    s.program({"name": "coastRock", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.08, "seed": 3},
        {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 1.5, "seed": 4},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 3, "constant": [0.5, 0.5, 0.5, 0.5]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
        {"kind": "ramp", "dst": 4, "srcA": 1, "constant": hexrgb("#16181c") + [1.0],
         "constant2": hexrgb("#2b2e33") + [1.0], "constant3": hexrgb("#45484a") + [1.0]},
        # grass on the flat tops: where the surface faces up
        {"kind": "input", "dst": 5, "input": "normal"},
        {"kind": "swizzle", "dst": 5, "srcA": 5, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.8, 0.93, 0.0, 0.0]},
        {"kind": "constant", "dst": 6, "constant": hexrgb("#33402c") + [1.0]},
        {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 6, "srcC": 5}],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    s.terrain("coast", world=world,
              terrain={"chunkSize": 100.0, "resolution": 40, "lodLevels": 4, "lodDistance": 200.0,
                       "viewDistance": 2600.0,
                       "water": {"enabled": True, "shallow": 6.0, "shallowColor": hexrgb("#1f3a4a"),
                                 "deepColor": hexrgb("#0a1420"), "clarity": 0.6, "maxOpacity": 0.97,
                                 "edgeFade": 0.6, "roughness": 0.06, "fresnel": 0.5, "reflection": 2.2,
                                 "reflectionTint": hexrgb("#9fb2cf"), "specular": 1.2, "ripple": 0.06,
                                 "rippleScale": 0.3, "rippleSpeed": 0.15, "chop": 0.0, "foam": 0.8,
                                 "foamWidth": 0.6, "foamColor": hexrgb("#b9c6d6"), "swell": 0.4}},
              material={"program": "coastRock", "baseColor": hexrgb(ROCK), "roughness": 0.9, "metallic": 0.0})
    hs = kit.terrain_heights(world, [(ISLAND[0], ISLAND[2]), (CAM[0], CAM[2]), (CAM[0] + 4.0, CAM[2] - 12.0)])
    lamp_y = tower(s, hs[0] - 0.3)
    cam = (CAM[0], hs[1] + CAM[1], CAM[2])

    # ---- dress the island and the stacks with scanned cliff faces
    for k, (x, z, yaw, sc) in enumerate([(ISLAND[0] + 16.0, ISLAND[2] + 12.0, 200.0, 0.6),
                                         (ISLAND[0] - 22.0, ISLAND[2] + 14.0, 160.0, 0.5)]):
        s.gltf("islandcliff%d" % k, "cliff_long", position=[x, -2.0, z], rotation=[0, yaw, 0],
               scale=[sc, 0.9, sc], tint=[0.55, 0.57, 0.62])

    # ---- sea stacks off the coast on the left: scanned rock faces stood up tall
    for k, (x, z, sc, yaw) in enumerate([(-120.0, -110.0, (2.4, 5.2, 2.0), 30.0), (-88.0, -160.0, (1.8, 3.6, 1.6), 140.0),
                                         (-150.0, -190.0, (1.4, 2.6, 1.4), 260.0)]):
        s.gltf("stack%d" % k, "mountainside", position=[x, -4.0, z], rotation=[0, yaw, 0], scale=list(sc),
               tint=[0.42, 0.44, 0.5])

    # ---- the foreground: the cliff edge, rocks and grass at the lower left, a fence post
    for k, (dx, dz, sc, yaw) in enumerate([(-7.5, -13.0, 1.5, 20.0), (-1.5, -15.0, 1.1, 120.0)]):
        eh = kit.terrain_heights(world, [(CAM[0] + dx, CAM[2] + dz)])[0]
        s.gltf("edge%d" % k, "moss_rocks", position=[CAM[0] + dx, eh - 0.4, CAM[2] + dz], rotation=[0, yaw, 0],
               scale=sc, tint=[0.6, 0.62, 0.66])
    s.proc("fencepost", {"kind": "cylinder", "radius": 0.07, "height": 1.3, "radialSegments": 8},
           material={"baseColor": hexrgb("#4a4139"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.9, "metallic": 0.0},
           transform={"position": [CAM[0] + 4.0, hs[2] + 0.6, CAM[2] - 12.0], "rotation": [4, 0, -6],
                      "scale": [1, 1, 1]})

    # ---- the lamp's beam: a spot light that turns (its direction keyed round a circle), the beam riding it
    lx, lz = ISLAND[0], ISLAND[2]
    keys = []
    for i in range(13):
        a = 2.0 * math.pi * i / 12.0
        keys.append({"time": round(24.0 * i / 12.0, 4), "value": [round(math.sin(a), 5), -0.06, round(math.cos(a), 5)],
                     "interp": "linear"})
    s.light("lamp", "spot", position=[lx, lamp_y, lz], direction=[0.0, -0.06, 1.0], color=hexrgb("#ffe7bd"),
            intensity=2.0e5, range=900.0, innerCone=1.5, outerCone=4.0, castsShadow=False, volumetric=1.0)
    s.track("lights/lamp/direction", keys, loop=24.0)
    s.effect("beam", "lightBeam", ("light", "lamp"), parameters={"color": hexrgb("#ffe7bd"), "intensity": 0.9,
                                                                  "length": 700.0, "angle": 4.0, "aperture": 1.2,
                                                                  "dust": 0.4, "dustScale": 0.05, "falloff": 1.2,
                                                                  "endFade": 0.6, "castLight": False})
    s.light("lampglow", "point", position=[lx, lamp_y, lz], color=hexrgb("#ffe0a8"), intensity=6000.0, range=60.0,
            radius=1.5, castsShadow=False, volumetric=0.8)
    s.light("window", "point", position=[ISLAND[0] - 11.0, hs[0] + 2.0, ISLAND[2] + 10.0], color=hexrgb("#ffb35a"),
            intensity=200.0, range=14.0, radius=0.5, castsShadow=False, volumetric=0.4)

    # ---- the mist on the water
    s.fogbank("mist", (ISLAND[0] - 60.0, 0.0, ISLAND[2] + 120.0), shape="Bank", density=0.25, radius=700.0,
              height=12.0, color=hexrgb("#6f86a8"), groundHug=0.8, heightFalloff=1.2, edgeSoftness=0.8)

    # ---- camera: the cliff top, the lighthouse on the right third, the horizon high
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 3, "post/bloom/intensity": 0.3,
               "post/bloom/threshold": 1.1, "post/output/vignette": 0.35, "post/output/grain": 0.025})
    tgt = kit.aim(cam, [ISLAND[0], hs[0] + 10.0, ISLAND[2]], 0.68, 0.42, LENS)
    s.camera = {"mode": 1, "position": list(cam), "target": tgt, "fov": 30.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=cam, target=tgt, period=100.0, amp=(0.4, 0.05, 0.3), tamp=(0.5, 0.2, 0.0))
    return s
