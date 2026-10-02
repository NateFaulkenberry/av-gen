"""THE WISP MARSH (unease, quiet dread). A bog at night: a crooked dead tree on a hummock in a black pool, pale wisps
hovering low over the water, dead trunks fading into fog.

Art direction (ART-RESTART-PLAN.md #10): at water level. Black still water fills the lower half and mirrors the fog. A
broken boardwalk of grey planks leads in from the bottom left and ends, half sunk, short of a hummock where the dead
tree stands on the right third, branches reaching. Reeds crowd the near water in silhouette. Dead trunks stand in the
fog behind, each fainter. A hazed moon behind the fog gives a cold grey-green light. Three or four small pale lights
hover low over the water between the trunks: not bright, just there.

The mirror is built (the water reflects only the sky): the hero tree and the nearest trunks have twins turned over
below the waterline, seen through a clear, dark surface.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "wisp-marsh"
TITLE = "The Wisp Marsh"

FOG = "#6f7d72"
WATER = "#070a09"
WOOD = "#3b3a33"
MOON = "#c9d4cc"
WISP = "#bff7ff"

CAM = (-1.5, 0.55, 8.0)
LENS = 24.0
TREE = (9.0, 0.0, -14.0)          # the hero dead tree on its hummock
MOON_DIR = (0.15, -0.35, 0.92)    # the moonlight travels toward the camera: the moon is behind the tree, low

DESIGN = {
    "category": "marsh",
    "thesis": "A bog at night: a crooked dead tree on a hummock in a black pool, pale wisps over the water, trunks in "
              "the fog.",
    "composition": {
        "background": "dead trunks fading into fog; the hazed moon",
        "midground": "the hummock and its crooked tree; the wisps over the water",
        "foreground": "reeds in silhouette; the broken boardwalk leading in",
        "focal": "the dead tree on the right third",
        "secondary": ["the wisps", "the boardwalk's end", "the mirror"],
        "atmosphere": "fog lying on the water, thinning upward",
        "camera": "24 mm, half a metre above the water",
    },
    "palette": {"dominant": FOG, "secondary": WATER, "accent": WISP, "highlight": MOON},
    "motion": {"very_slow": ["fog drift", "camera drift"], "medium": ["reeds"], "fast": [], "extremely_fast": []},
    "vocabulary": [],
    "tier": "light: a terrain, stylized trees in silhouette, a height fog, a few point lights",
}


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.18, "skyIntensity": 1.0, "background": hexrgb("#1a201c"), "fogColor": hexrgb("#4f5c54"),
        "volumeDensity": 0.03, "volumeMaxDistance": 120.0, "volumeSteps": 16, "volumeAnisotropy": 0.6,
        "volumeScattering": 1.0, "volumeAbsorption": 0.3, "volumeLocalLights": 1.0,
        "fogHeight": 1.5, "fogHeightFalloff": 0.35, "fogSky": 1.0, "fogSkyDistance": 140.0,
        "sky": {"enabled": True, "background": True, "zenithColor": hexrgb("#0e1311"),
                "horizonColor": hexrgb("#56645b"), "groundColor": hexrgb("#0b0e0c"), "haze": 0.35,
                "sunIntensity": 2.0, "sunColor": hexrgb(MOON), "sunSize": 0.035, "sunGlow": 0.25, "intensity": 1.0,
                "sunDirection": [-MOON_DIR[0], -MOON_DIR[1], -MOON_DIR[2]], "useKeyLight": False},
        "shadowCascades": 2,
    }

    # ---- the bog: hummocks and pools at the water's level, the hero's hummock
    s.terrain("bog", world={
        "seed": 31, "size": [300.0, 300.0], "baseHeight": -0.18, "seaLevel": 0.0,
        "layers": [{"frequency": 0.045, "amplitude": 0.55, "warp": 8.0}, {"frequency": 0.18, "amplitude": 0.16}],
        "features": [
            {"name": "hummock", "kind": "ridge", "path": [[TREE[0], 0.0, TREE[2]]], "width": 9.0, "amplitude": 0.9,
             "falloff": 1.2, "roughness": 0.5, "smoothing": 2},
            {"name": "pool", "kind": "flat", "path": [[2.0, -0.6, -4.0]], "width": 26.0, "flatten": 0.8,
             "falloff": 1.0, "roughness": 0.1, "smoothing": 2},
        ]},
        terrain={"chunkSize": 40.0, "resolution": 40, "viewDistance": 200.0,
                 "water": {"enabled": True, "shallow": 1.2, "shallowColor": hexrgb("#141a16"),
                           "deepColor": hexrgb(WATER), "clarity": 0.8, "maxOpacity": 0.96, "edgeFade": 0.3,
                           "roughness": 0.03, "fresnel": 0.6, "reflection": 1.6, "reflectionTint": hexrgb("#9aa89f"),
                           "specular": 1.0, "ripple": 0.05, "rippleScale": 0.5, "rippleSpeed": 0.2, "chop": 0.0,
                           "foam": 0.0, "refraction": 0.1, "glow": 0.0, "sparkle": 0.0}},
        material={"baseColor": hexrgb("#262a22"), "roughness": 0.95, "metallic": 0.0})

    # ---- the hero: a crooked dead tree on the hummock, in silhouette against the moon's fog
    bark = [0.32, 0.31, 0.28]
    s.gltf("deadtree", "dead_tree", variant=3, position=[TREE[0], 0.2, TREE[2]], rotation=[0, 200, 0], scale=0.85,
           tint=bark)
    # trunks in the fog behind, each fainter
    for k, (x, z, v, sc, yaw) in enumerate([(-14.0, -30.0, 1, 0.9, 40.0), (-4.0, -46.0, 2, 1.0, 120.0),
                                            (20.0, -52.0, 4, 1.1, 300.0), (34.0, -34.0, 5, 0.8, 80.0),
                                            (-28.0, -64.0, 3, 1.2, 10.0), (6.0, -78.0, 1, 1.1, 250.0)]):
        s.gltf("trunk%d" % k, "dead_tree", variant=v, position=[x, -0.1, z], rotation=[0, yaw, 0], scale=sc,
               tint=bark)

    # ---- the boardwalk: grey planks on posts from the bottom left, its end tilted into the water
    wood = {"baseColor": hexrgb("#4a4840"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.85,
            "metallic": 0.0}
    start, end = (CAM[0] - 1.6, CAM[2] - 1.0), (TREE[0] - 6.0, TREE[2] + 5.0)
    dx, dz = end[0] - start[0], end[1] - start[1]
    L = math.hypot(dx, dz)
    heading = math.degrees(math.atan2(dx, dz))
    n = int(L // 0.55)
    for i in range(n):
        u = i / max(1, n - 1)
        if 0.62 < u < 0.7:
            continue                                   # a gap where planks are missing
        sink = max(0.0, (u - 0.78) / 0.22)
        x, z = start[0] + dx * u, start[1] + dz * u
        s.proc("plank%d" % i, {"kind": "box", "size": [1.3, 0.05, 0.42], "subdivisions": 1}, material=wood,
               transform={"position": [x, 0.32 - 0.45 * sink, z],
                          "rotation": [6.0 * sink + ((i * 7) % 5 - 2) * 0.8, heading + ((i * 13) % 7 - 3) * 1.5,
                                       ((i * 11) % 5 - 2) * 1.2 + 9.0 * sink], "scale": [1, 1, 1]})
    # (posts as one instanced distribution along each side)
    for side, off in (("l", -0.62), ("r", 0.62)):
        ox, oz = off * dz / L, -off * dx / L
        s.proc("posts" + side, {"kind": "cylinder", "radius": 0.07, "height": 1.6, "radialSegments": 8},
               distribution={"kind": "linear", "count": int(L // 2.0), "start": [start[0] + ox, -0.4, start[1] + oz],
                             "end": [end[0] + ox, -0.5, end[1] + oz]}, material=wood)

    # ---- reeds in the near water and along the hummocks
    s.mesh("reeds", "wispy", distribution={"kind": "grid", "gridCount": [10, 1, 6], "gridSpacing": [1.6, 1.0, 1.4]},
           ops=[{"kind": "scatter", "range": [0.8, 0.0, 0.7], "seed": 41},
                {"kind": "filterProbability", "probability": 0.55, "seed": 42}],
           variation={"seed": 41, "rotation": [0.08, 3.14159, 0.08], "uniformScale": 0.35},
           scale=0.9, material={"baseColor": hexrgb("#2f3524"), "roughness": 0.9, "metallic": 0.0,
                                "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0},
           transform={"position": [CAM[0] + 6.5, -0.05, CAM[2] - 6.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the wisps: small pale lights low over the water
    for k, (x, y, z) in enumerate([(4.0, 0.9, -8.0), (-6.0, 1.3, -20.0), (14.0, 1.1, -26.0), (-1.0, 0.8, -34.0)]):
        s.particles("wisp%d" % k, capacity=200, seed=10 + k, shape="sphere", position=[x, y, z], extent=[0.12, 0, 0],
                    direction=[0, 1, 0], spawnRate=40.0, lifetimeMin=0.8, lifetimeMax=1.4, spread=1.0,
                    speedMin=0.02, speedMax=0.08, gravity=[0, 0.02, 0], drag=0.5, sizeStart=0.09, sizeEnd=0.0,
                    colorStart=hexrgb(WISP) + [0.9], colorEnd=hexrgb(WISP) + [0.0], emissive=6.0,
                    blend="additive", turbulence=0.3)
        s.light("wispl%d" % k, "point", position=[x, y, z], color=hexrgb(WISP), intensity=6.0, range=5.0,
                radius=0.2, castsShadow=False, volumetric=1.0)

    # ---- the moon: low behind the tree, a cold back light
    s.light("moon", "directional", direction=list(MOON_DIR), color=hexrgb(MOON), intensity=0.5, castsShadow=True,
            contactShadow=False, volumetric=1.0)

    # ---- camera: half a metre over the water
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 3, "post/bloom/intensity": 0.3,
               "post/bloom/threshold": 1.0, "post/output/vignette": 0.45, "post/output/grain": 0.035,
               "post/grade/saturation": 0.7})
    tgt = kit.aim(CAM, [TREE[0], 3.5, TREE[2]], 0.68, 0.40, LENS)
    s.camera = {"mode": 1, "position": list(CAM), "target": tgt, "fov": 50.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=CAM, target=tgt, period=90.0, amp=(0.3, 0.04, 0.2), tamp=(0.3, 0.1, 0.0))
    return s
