"""EMBER FOREST (organic, after the fire). The night after the burn: a dead pine forest still smouldering under a cold
sky while the fire front crawls along the far ridge. The bass is the wind breathing on the coals, the kick a snag
cracking, the snare a tree torching, and every note lifts an ember from the ground where its pitch says.

Art pass 2026-10 (docs/prototypes/sonic-garden/ART-PASS-2026-10-ember-forest.md). The first pass was one colour (orange
fog), identical branchless cylinders, and glowing contour squiggles on the ground that read as neon worms. The
direction now is wildfire-aftermath night photography:
- TWO TEMPERATURES: a cold blue night (moonlit smoke, sky, ash) against hot points of fire. Nothing is uniformly
  orange; the warm light comes only from things that are burning;
- REAL DEAD TREES: Quaternius dead pines with charred bark, cracks that still glow low on some trunks, scanned logs
  and stumps smouldering from inside;
- COAL BEDS, not lines: the ash floor carries scattered beds of live coal (Voronoi cells, crazed with glowing cracks),
  the way a burned slope at night is pocked with fire like a city seen from the air;
- THE FRONT on the far ridge: a broken line of flame on the crest, backlighting the smoke and throwing the trunks'
  shadows toward the camera;
- EMBERS on the wind, ash falling.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "ember-forest"
TITLE = "Ember Forest"

SOOT = "#060507"
SMOKE = "#2a3040"     # moonlit smoke: cold
NIGHT = "#0a1020"
ASH = "#b8bcc4"
EMBER = "#ff5a00"
COAL = "#ffa040"   # (a redder, dimmer emission went pink under the cold fog and the grade)
HOT = "#ffd27a"
MOON = "#8fa6d8"

DESIGN = {
    "category": "organic",
    "thesis": "The night after the burn: a dead forest still smouldering under a cold sky while the fire front crawls "
              "along the far ridge. The bass is the wind breathing on the coals, the snare a tree torching, and every "
              "note lifts an ember from the ground where its pitch says.",
    "composition": {
        "background": "the far ridge black against a cold night sky, the fire front a broken line of flame on its crest, "
                      "its glow in the smoke above",
        "midground": "receding planes of dead pines in cold smoke, rimmed by the fire behind them, their shadows "
                     "falling toward the camera",
        "foreground": "charred snags close to the lens; a smouldering log and stumps; ash floor pocked with coal beds",
        "focal": "the coal beds and the glowing log in the near ground, against the ridge fire",
        "secondary": ["embers rising on the wind", "ash falling", "glowing cracks low in the trunks"],
        "atmosphere": "low cold smoke, lit warm only near the fire",
        "post": "bloom on the fire, a cold-and-hot grade, grain",
        "camera": "a slow sideways track at head height (90 s there and back), so the tree planes slide in parallax",
    },
    "palette": {"dominant": SOOT, "secondary": SMOKE, "accent": ASH, "highlight": EMBER,
                "background_value": "dark, cold (night blue)",
                "saturation": "only what burns is warm; everything else is cold and grey"},
    "motion": {
        "very_slow": ["the camera's track", "the smoke's drift", "the ridge fire's growth (sustain)"],
        "medium": ["the coal beds breathing (bass)", "embers drifting on the wind", "ash falling"],
        "fast": ["embers lifted by notes", "a tree torching (snare)"],
        "extremely_fast": ["the crack and throw of sparks (kick)", "glints (hat)"],
    },
    "vocabulary": [
        ["melodic", "response.note", "each note lifts an ember from the ground at the pitch's place (low notes left, "
         "high notes right); it rises on the heat with a trail"],
        ["velocity", "notes.lastVelocity", "how many embers and how bright"],
        ["bass", "response.bass", "the wind breathes on the coals: the beds and the log glow up, embers stream "
         "off downwind, the smoke stirs"],
        ["kick", "signal.kick", "a snag cracks: a throw of sparks from the base of the near snag and its cracks "
         "flash"],
        ["snare", "response.snare", "a tree torches: flame bursts up a midground trunk and lights the smoke"],
        ["hat", "response.hat", "glints off the coal"],
        ["sustained", "response.sustain", "the fire front on the ridge grows and its glow climbs the sky"],
        ["silence", "(no input)", "a still, smoking forest under a cold sky; the coals breathe faintly; ash falls"],
    ],
    "tier": "medium: a smoke march (16 steps) with the fire's shadows; instanced dead pines; one coal-bed program",
}


def ground_program():
    """Ash with BEDS of live coal: one Voronoi cell in five (hash band) is a bed, brightest at its centre and crazed
    by a finer Voronoi's edges (glowing cracks between cooling plates). OP_COAL's constant is the coal's emission (the
    bass breathes on it). The ash itself is grey-black micro detail, paler where it is cold."""
    return {
        "name": "efAsh",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "constant", "dst": 1, "constant": [1.0, 0.0, 1.0, 0.0]},
            {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 1},                                  # flat (x, 0, z)
            {"kind": "voronoiEdge", "dst": 2, "srcA": 0, "value": 0.3, "seed": 4},                 # beds, ~3 m
            {"kind": "swizzle", "dst": 3, "srcA": 2, "constant": [2.0, 2.0, 2.0, 2.0]},            # the cell's hash
            {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.8, 0.84, 0.0, 0.0]},       # 1 in a bed
            {"kind": "swizzle", "dst": 4, "srcA": 2, "constant": [1.0, 1.0, 1.0, 1.0]},            # F1
            {"kind": "remap", "dst": 4, "srcA": 4, "value": 1, "constant": [0.0, 0.5, 1.0, 0.0]},  # hot centre
            {"kind": "power", "dst": 4, "srcA": 4, "value": 1.5},                                  # soft edges
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},
            {"kind": "voronoiEdge", "dst": 5, "srcA": 0, "value": 7.0, "seed": 9},                 # coal chunks
            {"kind": "swizzle", "dst": 6, "srcA": 5, "constant": [2.0, 2.0, 2.0, 2.0]},            # chunk's hash
            {"kind": "power", "dst": 6, "srcA": 6, "value": 3.0},                                  # few chunks hot
            {"kind": "swizzle", "dst": 5, "srcA": 5, "constant": [0.0, 0.0, 0.0, 0.0]},            # F2 - F1
            {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.0, 0.06, 0.0, 0.0]},        # dark gaps
            {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
            {"kind": "noise", "dst": 6, "srcA": 0, "value": 0.7, "seed": 8},                       # ragged beds
            {"kind": "smoothstep", "dst": 6, "srcA": 6, "constant": [0.38, 0.62, 0.0, 0.0]},
            {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 5},                                  # the coal mask
            {"kind": "constant", "dst": 7, "constant": hexrgb(COAL, 4.0) + [0.0]},                 # OP_COAL
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 3},
            {"kind": "microDetail", "dst": 4, "srcA": 0, "value": 2.0, "seed": 9},
            {"kind": "ramp", "dst": 4, "srcA": 4, "constant": hexrgb("#0a0a0c") + [1.0],
             "constant2": hexrgb("#26272c") + [1.0], "constant3": hexrgb("#5a5d66") + [1.0]},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_COAL = 21
assert ground_program()["ops"][OP_COAL - 1]["kind"] == "constant"


def bark_program():
    """Charred bark: black, with cracks that still glow low on the trunk -- but only on trunks a low-frequency noise
    of the world position picks (most of the forest is dead and cold). OP_FLARE's constant is their emission (raised
    by the kick and the bass); the glow fades with height."""
    return {
        "name": "efBark",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "constant", "dst": 6, "constant": [7.0, 0.9, 7.0, 0.0]},                      # stretched vertically
            {"kind": "multiply", "dst": 6, "srcA": 0, "srcB": 6},
            {"kind": "noise", "dst": 1, "srcA": 6, "value": 1.0, "seed": 12},
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.66, 0.7, 0.0, 0.0]},       # cracks
            {"kind": "swizzle", "dst": 2, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},           # height
            {"kind": "remap", "dst": 2, "srcA": 2, "value": 1, "constant": [0.0, 2.2, 1.0, 0.0]},  # low on the trunk
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "noise", "dst": 5, "srcA": 0, "value": 0.08, "seed": 31},                     # which trees burn
            {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.58, 0.66, 0.0, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 5},
            {"kind": "constant", "dst": 3, "constant": hexrgb(COAL, 0.6) + [0.0]},                 # OP_FLARE
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 1},
            {"kind": "constant", "dst": 4, "constant": hexrgb("#0d0c0c") + [1.0]},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_FLARE = 12


def front_program():
    """The fire front: a line broken into burning runs and dark gaps (a noise along the crest), each run flickering
    (a faster noise in x and time) and hottest at its base. Emission x the material's (routed) emission."""
    return {
        "name": "efFront",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.035, "seed": 41},                    # runs and gaps
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.42, 0.56, 0.0, 0.0]},
            {"kind": "input", "dst": 2, "input": "time"},
            {"kind": "constant", "dst": 3, "constant": [0.0, 1.3, 0.0, 0.0]},
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "add", "dst": 2, "srcA": 0, "srcB": 2},
            {"kind": "noise", "dst": 2, "srcA": 2, "value": 0.6, "seed": 43},                      # flicker
            {"kind": "remap", "dst": 2, "srcA": 2, "value": 1, "constant": [0.2, 0.8, 0.35, 1.4]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "input", "dst": 4, "input": "materialEmission"},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 1},
            {"kind": "constant", "dst": 5, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 1.0, "opacity": -1,
    }
assert bark_program()["ops"][OP_FLARE - 1]["kind"] == "constant"


def ember_wood_program():
    """A log or stump burning from inside: charred plates (Voronoi cells) with deep glowing cracks between them, the
    cracks hottest where a slow noise says. OP_WOOD's constant is the glow (the bass breathes on it)."""
    return {
        "name": "efWood",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "voronoiEdge", "dst": 1, "srcA": 0, "value": 3.2, "seed": 17},
            {"kind": "swizzle", "dst": 1, "srcA": 1, "constant": [0.0, 0.0, 0.0, 0.0]},            # F2 - F1 only
            {"kind": "remap", "dst": 1, "srcA": 1, "value": 1, "constant": [0.0, 0.09, 1.0, 0.0]},  # cracks
            {"kind": "power", "dst": 1, "srcA": 1, "value": 2.0},
            {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.7, "seed": 19},
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.5, 0.68, 0.0, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "constant", "dst": 3, "constant": hexrgb(COAL, 2.6) + [0.0]},                 # OP_WOOD
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 1},
            {"kind": "constant", "dst": 4, "constant": hexrgb("#0e0c0b") + [1.0]},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_WOOD = 9
assert ember_wood_program()["ops"][OP_WOOD - 1]["kind"] == "constant"

CHAR = {"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0, "roughness": 0.9,
        "metallic": 0.0, "program": "efBark"}
WOOD = dict(CHAR, program="efWood")
CHAR_PLAIN = {"baseColor": hexrgb("#0d0c0c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.9,
              "metallic": 0.0}

RIDGE_Z, RIDGE_Y = -170.0, 14.0
SNAG = (-2.6, 0.0, -3.5)       # the near snag the kick cracks
TORCH = (6.5, 0.0, -24.0)      # the midground tree the snare torches
NOTE_Z = -7.0                  # the line along which notes lift embers


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.1, "release": 1.4}
    s.environment = {
        "intensity": 0.06, "background": hexrgb(NIGHT), "fogColor": hexrgb(SMOKE),
        # thin cold smoke, thickest low: the near snags stay black silhouettes; the far planes fade into it
        "volumeDensity": 0.007, "volumeMaxDistance": 95.0, "volumeAnisotropy": 0.1, "volumeNoise": 0.55,
        "volumeNoiseScale": 0.05, "volumeNoiseSpeed": 0.03, "fogHeight": 6.0, "fogHeightFalloff": 0.18,
        "fogHeightAmount": 1.0, "volumeShadowStrength": 0.85, "volumeShadowSteps": 4, "volumeLocalLights": 1.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#02040c"), "horizonColor": hexrgb("#2a1a1c"),
                "groundColor": hexrgb("#030304"), "haze": 0.12, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "coals", "position": [0.5, 0.2, -6.0], "radius": 3.0, "weight": 1.0},
                        {"name": "front", "position": [0.0, RIDGE_Y, RIDGE_Z], "radius": 30.0, "weight": 0.6}],
        "layers": [
            {"name": "near", "start": 0.0, "end": 14.0, "contrast": 1.0, "saturation": 1.0},
            {"name": "mid", "start": 14.0, "end": 60.0, "contrast": 0.8, "saturation": 0.8},
            {"name": "far", "start": 60.0, "end": 400.0, "contrast": 0.6, "saturation": 0.7},
        ],
    }
    s.program(ground_program())
    s.program(bark_program())
    s.program(ember_wood_program())
    s.program(front_program())
    s.effect("stars", "stars", ("world",), parameters={
        "brightness": 2.5, "density": 0.03, "magnitudeSlope": 5.0, "colorSpread": 0.4, "twinkle": 0.3,
        "twinkleRate": 2.0, "horizonFade": 0.25, "band": 0.0, "daylight": 0.0})

    # ---- ground: ash with coal beds
    s.proc("ground", {"kind": "box", "size": [400.0, 0.4, 400.0], "subdivisions": 2},
           material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0, "roughness": 0.95,
                     "metallic": 0.0, "program": "efAsh"},
           transform={"position": [0.0, -0.2, -120.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    # ---- the far ridge: a black hill mass whose crest the fire front follows (both take the same world-space noise,
    # so the flame sits on the crest)
    crest = {"kind": "noise", "amount": 9.0, "scale": 0.012, "speed": 0.0, "seed": 21, "axisMask": [0.0, 1.0, 0.0],
             "space": "world"}
    s.proc("ridge", {"kind": "box", "size": [900.0, 2.0 * RIDGE_Y, 30.0], "subdivisions": 64},
           material={"baseColor": hexrgb("#050506"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 1.0, "metallic": 0.0},
           deformers=[crest], transform={"position": [0.0, 0.0, RIDGE_Z - 15.0], "rotation": [0, 0, 0],
                                         "scale": [1, 1, 1]})
    s.proc("front", {"kind": "box", "size": [900.0, 2.4, 2.0], "subdivisions": 64},
           material={"baseColor": [0, 0, 0], "emissiveColor": hexrgb("#ff7a20"), "emissiveIntensity": 9.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "efFront"},
           deformers=[crest, {"kind": "noise", "amount": 1.2, "scale": 0.4, "speed": 0.9, "seed": 23,
                              "axisMask": [0.0, 1.0, 0.0], "space": "world"}],
           transform={"position": [0.0, RIDGE_Y + 0.4, RIDGE_Z + 0.5], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the dead forest: three planes of Quaternius dead pines, charred (one draw per variant and plane)
    planes = [  # (name, z, count x, count z, spacing x, spacing z, variants, seed)
        ("mid", -22.0, 5, 1, 11.0, 9.0, (2, 4), 5),
        ("far", -48.0, 6, 1, 14.0, 14.0, (3, 5), 7),
        ("deep", -78.0, 6, 1, 18.0, 18.0, (1, 3), 9),
    ]
    for name, z, nx, nz, sx, sz, variants, seed in planes:
        for k, v in enumerate(variants):
            # (plain char, no program: the bark program's two fBm taps over every branch pixel of eighty trees cost
            # 15 ms at 1080p; at this distance, in smoke, the glow of a crack is a pixel)
            # (the far planes unlit: in the smoke they are silhouettes, and the lit shading of their branches was
            # most of their cost)
            s.mesh("pines_%s%d" % (name, k), "dead_tree", variant=v,
                   material=CHAR_PLAIN if name == "mid" else dict(CHAR_PLAIN, unlit=True),
                   distribution={"kind": "grid", "gridCount": [nx, 1, nz], "gridSpacing": [sx, 1.0, sz]},
                   variation={"seed": seed + 11 * k, "position": [sx * 0.45, 0.0, sz * 0.45],
                              "rotation": [0.04, 3.14, 0.04], "scale": [0.15, 0.25, 0.15]},
                   transform={"position": [sx * 0.5 * k, 0.0, z - sz * 0.5 * k], "rotation": [0, 0, 0],
                              "scale": [1, 1, 1]})
    # the near snags, close to the lens (single placements: their silhouettes frame the shot)
    for name, v, pos, yaw, sc in (("snag", 1, SNAG, 30.0, 1.0), ("snagR", 3, (5.5, 0.0, -6.0), 200.0, 1.15),
                                  ("snagL", 2, (-9.0, 0.0, -9.5), 110.0, 1.1), ("torch", 4, TORCH, 60.0, 1.0)):
        s.mesh(name, "dead_tree", variant=v, material=CHAR,
               transform={"position": list(pos), "rotation": [0, yaw, 0], "scale": [sc, sc, sc]})
    # smouldering wood on the floor: a fallen log and stumps burning from inside
    for name, key, pos, rot, sc in (("log", "log_big", (0.8, 0.25, -6.2), (0, 18, 0), 1.6),
                                    ("log2", "log", (-5.5, 0.1, -12.0), (0, -40, 0), 1.8),
                                    ("stump", "stump", (3.2, 0.1, -4.2), (0, 70, 0), 1.3),
                                    ("stump2", "stump_b", (-1.6, 0.1, -14.0), (0, 10, 0), 1.6),
                                    ("stump3", "stump", (8.0, 0.1, -16.0), (0, 140, 0), 1.5)):
        s.mesh(name, key, material=WOOD, budget=12000,   # (the 1k scans are 40-100k triangles each)
               transform={"position": list(pos), "rotation": list(rot), "scale": [sc, sc, sc]})

    # ---- light: the fire front behind everything (warm, low, casting the trunks' shadows toward the camera, lighting
    # the smoke), a cold moon from above; the coals light only themselves (emission and bloom)
    s.light("fire", "directional", direction=[0.1, -0.06, 0.99], color=hexrgb("#ff7a2a"), intensity=0.26,
            castsShadow=True, contactShadow=False, shadowStrength=1.0, softness=0.4, volumetric=0.0)
    s.light("moon", "directional", direction=[0.45, -0.8, 0.35], color=hexrgb(MOON), intensity=0.45,
            castsShadow=False, volumetric=1.0)
    s.light("torchLight", "point", position=[TORCH[0], 4.0, TORCH[2]], color=hexrgb("#ff8a3a"), intensity=0.0,
            range=26.0, radius=2.0, castsShadow=False, volumetric=0.0)

    # ---- particles
    # embers on the wind: rising off the coal beds, drifting downwind (+x), cooling
    s.particles("embers", capacity=3000, seed=9, shape="box", position=[0.0, 0.2, -14.0], extent=[22.0, 0.1, 12.0],
                direction=[0.2, 1, 0], spawnRate=60.0, lifetimeMin=2.5, lifetimeMax=6.0, spread=0.4, speedMin=0.4,
                speedMax=1.4, gravity=[0.35, 0.3, 0], drag=0.3, turbulence=0.7, turbulenceScale=0.5,
                sizeStart=0.018, sizeEnd=0.004, sizeVariance=0.6, colorStart=hexrgb(HOT) + [1.0],
                colorEnd=hexrgb(EMBER) + [0.0], emissive=14.0, blend="additive", velocityStretch=0.4,
                stretchMax=0.06)
    # ash falling: grey flakes, lit by the scene (alpha blended, cold)
    s.particles("ash", capacity=2500, seed=13, shape="box", position=[0.0, 9.0, -14.0], extent=[24.0, 4.0, 14.0],
                direction=[0, -1, 0], spawnRate=70.0, lifetimeMin=8.0, lifetimeMax=14.0, spread=0.6, speedMin=0.1,
                speedMax=0.3, gravity=[0.08, -0.12, 0], drag=0.8, turbulence=0.35, turbulenceScale=0.4,
                sizeStart=0.02, sizeEnd=0.016, sizeVariance=0.5, colorStart=hexrgb("#9aa0aa") + [0.7],
                colorEnd=hexrgb("#6a6e78") + [0.0], emissive=0.0, blend="alpha")
    # sparks thrown when the near snag cracks (kick)
    s.particles("crack", capacity=3000, seed=11, shape="sphere", position=[SNAG[0], 0.8, SNAG[2]],
                extent=[0.4, 0.5, 0.4], direction=[0.3, 1, 0.3], spawnRate=0.0, lifetimeMin=0.6, lifetimeMax=1.6,
                spread=0.6, speedMin=1.2, speedMax=3.5, gravity=[0.2, -2.0, 0], drag=0.35, turbulence=0.4,
                sizeStart=0.016, sizeEnd=0.003, colorStart=hexrgb("#ffc070") + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=14.0, blend="additive", trailEnabled=True, trailLength=6, trailStride=1, trailWidth=0.4,
                trailTaper=0.0, trailFade=0.0)
    # the torching tree (snare): flame climbing the trunk, then a column of sparks
    s.particles("torch", capacity=4000, seed=15, shape="box", position=[TORCH[0], 3.0, TORCH[2]],
                extent=[0.6, 3.0, 0.6], direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.35, lifetimeMax=0.8,
                spread=0.15, speedMin=3.0, speedMax=7.0, gravity=[0.3, 3.0, 0], drag=0.4, turbulence=0.6,
                turbulenceScale=0.4, sizeStart=1.1, sizeEnd=0.2, sizeVariance=0.4,
                colorStart=hexrgb("#ffc870") + [0.35], colorEnd=hexrgb("#ff4a00") + [0.0], emissive=5.0,
                blend="additive", velocityStretch=0.25, stretchMax=0.8)
    # glints off the coal (hat)
    s.particles("glints", capacity=1500, seed=19, shape="box", position=[0.5, 0.05, -9.0], extent=[12.0, 0.0, 6.0],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.06, lifetimeMax=0.2, spread=1.0, speedMin=0.0,
                speedMax=0.3, gravity=[0, 0, 0], drag=0.0, sizeStart=0.03, sizeEnd=0.0,
                colorStart=hexrgb("#fff2d0") + [1.0], colorEnd=hexrgb(HOT) + [0.0], emissive=18.0, blend="additive")
    # note embers: lifted from the ground at the pitch's place, rising on the heat with a trail
    s.particles("notes", capacity=300, seed=5, shape="sphere", position=[-7.0, 0.3, NOTE_Z], extent=[0.3, 0.1, 0.6],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=2.0, lifetimeMax=3.2, spread=0.5, speedMin=0.6,
                speedMax=1.4, gravity=[0.3, 0.5, 0], drag=0.4, turbulence=0.9, turbulenceScale=0.5,
                sizeStart=0.045, sizeEnd=0.012, colorStart=hexrgb("#ffe0a0") + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=12.0, blend="additive", trailEnabled=True, trailLength=16, trailStride=2, trailWidth=0.35,
                trailTaper=0.1, trailFade=0.0, trailTint=hexrgb("#ff8a3a"))

    # ---- camera: a slow sideways track at head height, looking slightly up so the ridge clears the near ground
    s.camera["fov"] = 45.0
    s.track("camera/position", [{"time": 0.0, "value": [-3.5, 1.6, 6.0], "interp": "smooth"},
                                {"time": 45.0, "value": [3.5, 1.7, 6.0], "interp": "smooth"},
                                {"time": 90.0, "value": [-3.5, 1.6, 6.0], "interp": "smooth"}], loop=90.0)
    s.track("camera/target", [{"time": 0.0, "value": [-1.5, 3.2, -24.0], "interp": "smooth"},
                              {"time": 45.0, "value": [4.0, 3.2, -24.0], "interp": "smooth"},
                              {"time": 90.0, "value": [-1.5, 3.2, -24.0], "interp": "smooth"}], loop=90.0)
    s.camera = {"mode": 1, "position": [-3.5, 1.6, 6.0], "target": [-1.5, 3.2, -24.0], "fov": 45.0,
                "orbitSpeed": 0.0}

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # melody: an ember lifted where the pitch says (lastPitch 0.30 -> x -9, 0.72 -> x +9), velocity-scaled
    s.map(M("place", [("lastPitch", 1.0)], "mean", -0.30 / 0.42, 1.0 / 0.42))
    s.route(R("visual.place", "particles/notes/position", 18.0, comp=0, attackMs=0, decayMs=0),
            R("noteOn", "particles/notes/burst", 2.0, depth="lastVelocity", attackMs=0, decayMs=120))
    # bass: the wind breathes on the coals (beds, log, stumps), embers stream off downwind, the smoke stirs
    for c, w in enumerate(hexrgb(COAL, 4.0)):
        if w > 1e-4:
            s.route(R("bass", "material/efAsh/op/%d/constant/constant" % OP_COAL, 3.0 * w, comp=c, attackMs=60,
                      decayMs=1400))
    for c, w in enumerate(hexrgb(COAL, 2.6)):
        if w > 1e-4:
            s.route(R("bass", "material/efWood/op/%d/constant/constant" % OP_WOOD, 1.6 * w, comp=c, attackMs=60,
                      decayMs=1400))
    s.route(R("bass", "particles/embers/spawnRate", 260.0, attackMs=60, decayMs=1200),
            R("bass", "particles/embers/gravity", 2.2, comp=0, attackMs=60, decayMs=1400),
            R("bass", "scene/volumeNoise", 0.3, attackMs=60, decayMs=1500))
    # kick: the near snag cracks -- sparks thrown, its cracks (and every glowing crack) flash
    s.route(R("kick", "particles/crack/burst", 30.0, attackMs=0, decayMs=40))
    for c, w in enumerate(hexrgb(COAL, 0.6)):
        if w > 1e-4:
            s.route(R("kick", "material/efBark/op/%d/constant/constant" % OP_FLARE, 5.0 * w, comp=c, attackMs=0,
                      decayMs=350))
    # snare: a tree torches -- flame up the midground trunk and its light in the smoke
    s.route(R("snare", "particles/torch/burst", 90.0, attackMs=0, decayMs=40),
            R("snare", "lights/torchLight/intensity", 900.0, attackMs=0, decayMs=600))
    # hat: glints off the coal
    s.route(R("hat", "particles/glints/burst", 30.0, attackMs=0, decayMs=30))
    # sustained: the fire front grows and its glow climbs the sky and the smoke
    s.route(R("sustain", "procedural/front/material/emissive", 9.0, **SLOW),
            R("sustain", "lights/fire/intensity", 0.9, **SLOW),
            # (live, a routed sky relights at most twice a second, ADR-1070: a slow route suits that schedule)
            R("sustain", "env/sky/intensity", 0.8, **SLOW),
            R("kick", "post/lens/chromaticAberration", 0.006, attackMs=0, decayMs=120))

    s.params_({
        "scene/volumeSteps": 10, "scene/volumeJitter": 0.6,
        "post/bloom/intensity": 0.38, "post/bloom/threshold": 1.0, "post/output/vignette": 0.5,
        "post/output/grain": 0.03, "post/grade/contrast": 1.1, "post/grade/temperature": 0.0,
        "post/grade/saturation": 1.0, "post/lens/chromaticAberration": 0.002, "post/tonemap/operator": 3,
        "camera/lens/focalLength": 30.0, "camera/exposure/compensation": 0.2,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("coals", centre=[0.5, 0.2, -6.0], radius=3.0)
    s.region("ground", box=[0.0, 0.68, 1.0, 1.0])
    s.region("front", box=[0.0, 0.3, 1.0, 0.55])
    return s
