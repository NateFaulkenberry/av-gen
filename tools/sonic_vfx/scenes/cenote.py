"""THE CENOTE (serenity, reverence). A flooded sinkhole at noon: one column of sun falls through a round opening in the
cave roof onto a small island of rock, ferns and young trees in turquoise water.

Art direction (ART-RESTART-PLAN.md #1): wide view from a wet limestone ledge inside the cavern. The vault is dark; the
opening in its upper left lets in one slanting column of noon sun that lands on the island on the right third. Roots
hang from the opening's rim through the shaft and catch its light. The pool is clear turquoise in the shaft, deep
teal-black at the walls, pale sand under the island. A plank walkway with a rope rail runs from the foreground ledge
toward the island: human scale and the leading line. The far wall is lost in blue haze. Almost nothing glows: the
drama is one light.

How the light is built (no shadowed god rays in the engine): the sun is a shadow-casting directional light, so the
vault really blocks it and the island really gets the spot; the visible column is a lightBeam effect laid along the
same direction; a warm point light at the spot is the island's bounce, a teal one under the water the pool's glow on
the walls.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT

ID = "cenote"
TITLE = "The Cenote"

TURQUOISE = "#2fb5b0"
DEEP_TEAL = "#0b3a40"
LIMESTONE = "#8a8170"
SUN = "#fff1cf"
FERN = "#4f7a3a"

POOL = (0.0, 0.0, -20.0)        # the pool's centre; water at y = 0
ISLAND = (7.0, 0.0, -28.0)      # the islet behind the platform
PLATFORM = (4.6, 0.0, -24.5)    # the round stone platform the walkway ends on, where the sun lands
OPENING = (-11.0, 23.3, -38.0)  # the roof opening's centre, on the vault's inner surface
CAM = (0.6, 1.55, -2.5)
LENS = 20.0
WALL_R = 24.0                   # the wall ring's radius round the pool
DOME_C = (0.0, 2.0, -20.0)      # the vault: a sphere's inside, 30 m radius, meeting the walls at about 20 m
DOME_R = 30.0

DESIGN = {
    "category": "cave and water",
    "thesis": "A flooded sinkhole at noon: one column of sun through the roof onto an island of ferns in turquoise "
              "water.",
    "composition": {
        "background": "the far cave wall lost in blue haze; the bright sky through the roof opening",
        "midground": "the turquoise pool, the island in the sun, the column of light, the hanging roots",
        "foreground": "the wet limestone ledge, the walkway's first planks and rail",
        "focal": "the island where the light lands",
        "secondary": ["the roof opening", "hanging roots in the shaft", "the walkway"],
        "atmosphere": "a faint haze that the shaft is drawn in",
        "camera": "22 mm from the ledge, 1.5 m above the water, tilted up; a slow drift",
    },
    "palette": {"dominant": DEEP_TEAL, "secondary": LIMESTONE, "accent": TURQUOISE, "highlight": SUN},
    "motion": {"very_slow": ["the camera's drift"], "medium": ["the water's surface"], "fast": [], "extremely_fast": []},
    "vocabulary": [],
    "tier": "medium: scanned walls (budgeted), one SDF vault meshed once, a terrain pool, one beam",
}


def _toward(src, dst):
    d = [b - a for a, b in zip(src, dst)]
    n = math.sqrt(sum(c * c for c in d))
    return [round(c / n, 5) for c in d], n


def vault():
    """The cave roof: the inside of a rock dome over the pool, reaching out past the walls so no sun leaks round it,
    with the opening cut through it as a funnel (narrow below, wide above, so the sky shows through a thick roof);
    rough with two scales of noise kept under the shell's thickness (no holes). Static, so meshed once."""
    outer = kit.sd_move(DOME_C, kit.sd_sphere(DOME_R + 4.5))
    inner = kit.sd_move(DOME_C, kit.sd_sphere(DOME_R))
    below = kit.sd_move((DOME_C[0], 12.0 - 30.0, DOME_C[2]), kit.sd_box([80.0, 30.0, 80.0]))
    hole = kit.sd_union(kit.sd_move(OPENING, kit.sd_cyl(3.4, 30.0)),
                        kit.sd_move((OPENING[0], OPENING[1] + 13.0, OPENING[2]), kit.sd_cyl(6.5, 20.0)))
    roof = kit.sd_diff(outer, inner, below, hole)
    roof = kit.sd_noise(0.9, 0.16, roof, seed=3)
    return kit.sd_noise(0.3, 0.7, roof, seed=5)


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    land = (PLATFORM[0], 0.5, PLATFORM[2])
    sun_dir, shaft_len = _toward(OPENING, land)

    s.environment = {
        "intensity": 0.06, "skyIntensity": 1.0, "background": hexrgb("#9cc6e6"), "fogColor": hexrgb("#0e2c31"),
        "volumeDensity": 0.005, "volumeMaxDistance": 80.0, "volumeSteps": 16, "volumeAnisotropy": 0.55,
        "volumeScattering": 1.0, "volumeAbsorption": 0.4, "volumeLocalLights": 1.0, "fogSky": 0.0,
        "sky": {"enabled": True, "background": True, "zenithColor": hexrgb("#5d9fd8"),
                "horizonColor": hexrgb("#cfe5f2"), "groundColor": hexrgb("#2a2722"), "haze": 0.3,
                "sunIntensity": 0.0, "sunColor": hexrgb(SUN), "sunSize": 0.01, "sunGlow": 0.05, "intensity": 1.0,
                "useKeyLight": False},
        "shadowCascades": 1, "shadowRange": 90.0,
    }

    # ---- the pool: a terrain whose floor lies under sea level, the island a round ridge rising through it, the
    #      camera's ledge another
    s.terrain("pool", world={
        "seed": 11, "size": [120.0, 120.0], "baseHeight": -9.0, "seaLevel": 0.0,
        "layers": [{"frequency": 0.05, "amplitude": 1.2, "warp": 4.0}, {"frequency": 0.21, "amplitude": 0.3}],
        "features": [
            {"name": "island", "kind": "ridge", "path": [[ISLAND[0], 0.0, ISLAND[2]]], "width": 12.0,
             "amplitude": 10.4, "falloff": 1.4, "roughness": 0.6, "smoothing": 2},
        ]},
        terrain={"chunkSize": 30.0, "resolution": 48, "viewDistance": 140.0, "skirtDepth": 1.0,
                 "water": {"enabled": True, "shallow": 4.0, "shallowColor": hexrgb("#2bb8ae"),
                           "deepColor": hexrgb("#021619"), "clarity": 2.2, "maxOpacity": 0.9, "edgeFade": 0.6,
                           "roughness": 0.05, "fresnel": 0.5, "reflection": 0.25, "reflectionTint": hexrgb("#406a70"),
                           "specular": 1.5, "ripple": 0.12, "rippleScale": 0.6, "rippleSpeed": 0.3, "chop": 0.0,
                           "foam": 0.0, "refraction": 0.3, "glow": 0.0, "sparkle": 0.4,
                           "sparkleColor": hexrgb("#fff6dc")}},
        material={"baseColor": hexrgb("#d8cdb2"), "roughness": 0.9, "metallic": 0.0})

    # ---- the walls: scanned limestone cliffs in a ring facing the pool, stretched tall
    s.mesh("walls", "cliff_long", distribution={"kind": "radial", "count": 8, "radius": WALL_R + 1.5, "plane": "xz",
                                                  "center": [POOL[0], -1.2, POOL[2]], "orientation": "inward"},
           scale=[0.95, 2.45, 1.0], budget=160000)

    # ---- the vault with its opening
    s.sdf("vault", vault(), bounds_min=[-38.0, 10.0, -58.0], bounds_max=[38.0, 40.0, 18.0], mesh=176, shadows=True,
          material={"baseColor": hexrgb("#4a443b"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                    "roughness": 0.9, "metallic": 0.0})

    # ---- the walkway: planks on posts from the ledge toward the island, a rope rail on its right
    a, b = (CAM[0] - 3.2, 0.42, CAM[2] - 1.0), (PLATFORM[0] - 2.0, 0.42, PLATFORM[2] + 1.7)
    dx, dz = b[0] - a[0], b[2] - a[2]
    length = math.hypot(dx, dz)
    heading = math.degrees(math.atan2(dx, dz))
    wood = {"baseColor": hexrgb("#6b5236"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.75,
            "metallic": 0.0}
    mid = [(a[0] + b[0]) / 2, a[1], (a[2] + b[2]) / 2]
    s.proc("deck", {"kind": "box", "size": [1.2, 0.08, length], "subdivisions": 1}, material=wood,
           transform={"position": mid, "rotation": [0.0, heading, 0.0], "scale": [1, 1, 1]})
    nposts = int(length // 2.2) + 1
    for side, off in (("l", -0.58), ("r", 0.58)):
        ox, oz = off * dz / length, -off * dx / length
        s.proc("posts" + side, {"kind": "cylinder", "radius": 0.06, "height": 2.4, "radialSegments": 8},
               distribution={"kind": "linear", "count": nposts, "start": [a[0] + ox, -0.4, a[2] + oz],
                             "end": [b[0] + ox, -0.4, b[2] + oz]}, material=wood)
    rail = []
    for i in range(nposts):
        u = i / max(1, nposts - 1)
        ox, oz = 0.58 * dz / length, -0.58 * dx / length
        rail.append({"position": [a[0] + dx * u + ox, 1.25 - 0.08 * math.sin(math.pi * (u * (nposts - 1) % 1.0)),
                                  a[2] + dz * u + oz], "scale": 1.0, "roll": 0.0})
    s.proc("rope", {"kind": "tube", "tubeRadius": 0.025, "tubeTaper": 1.0, "tubeSides": 6, "tubeSegments": 120,
                    "tubeTwist": 0.0, "tubeCaps": False,
                    "curve": {"kind": "catmullRom", "generator": "points", "points": rail, "samplesPerSegment": 6}},
           material={"baseColor": hexrgb("#8a7a5c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.9, "metallic": 0.0})

    # ---- the foreground: a wet rock shelf at the lower left, close to the lens
    s.gltf("nearrock", "rock_face", position=[CAM[0] + 2.4, -1.6, CAM[2] - 3.0], rotation=[0, 200, 0], scale=1.2)

    # ---- the platform in the light, and the islet behind it: mossed rocks, ferns, young trees in a line away from us
    stone = {"baseColor": hexrgb("#b9ad94"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.8,
             "metallic": 0.0}
    s.proc("platform", {"kind": "cylinder", "radius": 2.6, "height": 1.2, "radialSegments": 48, "bevel": 0.12,
                        "bevelSegments": 3}, material=stone,
           transform={"position": [PLATFORM[0], -0.12, PLATFORM[2]], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.gltf("islandrocks", "moss_rocks", position=[ISLAND[0] + 0.5, -0.2, ISLAND[2] - 1.0], rotation=[0, 30, 0],
           scale=1.3)
    s.gltf("ferns", "ferns", position=[ISLAND[0] - 0.5, 0.8, ISLAND[2]], rotation=[0, 70, 0], scale=1.8)
    # a person standing in the light: the scale of the place, and someone to feel it
    s.figure("visitor", [PLATFORM[0] + 0.3, 0.48, PLATFORM[2] - 0.2], facing=-150.0)
    # the row of four trees laid along the view, so they stack into one clump on the islet's far side
    view = math.degrees(math.atan2(ISLAND[0] - CAM[0], ISLAND[2] - CAM[2]))
    s.gltf("trees", "pachira", position=[ISLAND[0] + 0.5, 0.5, ISLAND[2] - 1.5], rotation=[0, view - 90.0, 0],
           scale=2.8)

    # ---- light: the sun through the opening, its column, the island's bounce, the pool's glow
    s.light("sun", "directional", direction=sun_dir, color=hexrgb(SUN), intensity=9.0, castsShadow=True,
            contactShadow=False, softness=1.5, volumetric=0.0)
    s.light("bounce", "point", position=[PLATFORM[0], 2.5, PLATFORM[2] + 0.5], color=hexrgb("#ffd9a0"), intensity=140.0,
            range=26.0, radius=1.5, castsShadow=False, volumetric=0.3)
    s.light("poolglow", "rect", position=[POOL[0], 0.05, POOL[2] - 4.0], direction=[0.0, 1.0, 0.0], up=[0.0, 0.0, -1.0],
            color=hexrgb("#3fd6c4"), intensity=0.45, width=34.0, height=30.0, castsShadow=False, volumetric=0.0)
    s.beam("shaft", OPENING, toward=land, color=hexrgb(SUN), intensity=1.2, length=shaft_len + 3.0,
           angle=2.0, aperture=2.9, dust=0.55, dustScale=0.5, falloff=0.15, endFade=0.05, depthFade=1.5)

    # ---- camera: the ledge, the island on the right third, the opening up left
    s.params_({"camera/lens/focalLength": LENS, "post/tonemap/operator": 3, "post/bloom/intensity": 0.25,
               "post/bloom/threshold": 1.2, "post/output/vignette": 0.3, "post/output/grain": 0.015})
    tgt = kit.aim(CAM, [PLATFORM[0], 1.0, PLATFORM[2]], 0.64, 0.68, LENS)
    s.camera = {"mode": 1, "position": list(CAM), "target": tgt, "fov": 50.0, "orbitSpeed": 0.0}
    s.drift_camera(centre=CAM, target=tgt, period=90.0, amp=(0.25, 0.05, 0.15), tamp=(0.3, 0.12, 0.0))
    return s
