"""EMBER FOREST (organic, after the fire). The morning after the fire: the bass is the wind breathing on the embers,
the snare a flare-up in the black trunks, the melody one ember dancing through the smoke.

Composition (SCENE-CATALOG.md #13): black trunks stand in three receding planes, separated by smoke that the low
fire-glow behind the last plane lights into shafts. The ground is ash with seams of live coal. The subject is a single
ember with a long trail, dancing at the height of the melody between the near trunks; the camera tracks sideways so
the planes slide past each other. Colour is soot, smoke and ember: nothing in the frame is saturated except fire.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "ember-forest"
TITLE = "Ember Forest"

SOOT = "#070505"
SMOKE = "#3a3230"
ASH = "#d8d0c8"
EMBER = "#ff6a00"
HOT = "#ffe6a0"

DANCE = (1.6, 1.4, -5.0)     # the ember's rest position, between the near trunks

DESIGN = {
    "category": "organic",
    "thesis": "The morning after the fire: the bass is the wind breathing on the embers, the snare a flare-up in the "
              "black trunks, the melody one ember dancing through the smoke.",
    "composition": {
        "background": "the fire's glow low behind the last plane of trunks",
        "midground": "two more planes of trunks, separated by lit smoke and shafts",
        "foreground": "near trunks; ash ground with seams of live coal",
        "focal": "the dancing ember and its trail, between the near trunks",
        "secondary": ["the coal seams breathing with the wind", "flare-ups in the trunks", "sparks"],
        "atmosphere": "low smoke lit from behind, with shafts between the trunks",
        "post": "bloom on the fire, a warm-on-black grade, grain",
        "camera": "a slow sideways track, so the trunk planes slide in parallax",
    },
    "palette": {"dominant": SOOT, "secondary": SMOKE, "accent": ASH, "highlight": EMBER,
                "background_value": "near black over a warm band",
                "saturation": "only the fire is saturated"},
    "motion": {
        "very_slow": ["the camera's track", "the smoke's drift", "the horizon glow (sustain)"],
        "medium": ["the coal seams' breathing (bass)"],
        "fast": ["the dancing ember (melody)"],
        "extremely_fast": ["flare-ups (snare)", "sparks (hat)", "the crack and throw of embers (kick)"],
    },
    "vocabulary": [
        ["melodic", "notes.lastPitch", "the dancing ember's height: the melody's contour drawn in fire"],
        ["note", "response.note", "the ember flares and its trail brightens"],
        ["bass", "response.bass", "the wind breathes on the embers: the coal seams glow and the smoke stirs"],
        ["snare", "response.snare", "a flare-up: fire runs up the charred trunks"],
        ["kick", "response.kick", "a trunk cracks and throws embers"],
        ["hat", "response.hat", "sparks"],
        ["sustained", "response.sustain", "the fire's glow behind the trees builds"],
        ["velocity", "notes.lastVelocity", "the ember's brightness"],
        ["silence", "(no input)", "a still, smoking forest; the coals glow faintly; the last sparks rise"],
    ],
    "tier": "medium: a smoke march (32 steps live) with the trunks' shadows; three instanced trunk planes",
}


def ground_program():
    """Ash with seams of live coal: a thin contour of fBm, glowing; OP_SEAM's constant is the seams' emission."""
    return {
        "name": "efAsh",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.22, "seed": 6},
            {"kind": "remap", "dst": 1, "srcA": 1, "value": 0, "constant": [0.5, 1.0, 0.0, 1.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 1},
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.0, 0.0012, 0.0, 0.0]},
            {"kind": "remap", "dst": 1, "srcA": 1, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},   # 1 on a seam
            {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.9, "seed": 8},                         # flicker grain
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "constant", "dst": 3, "constant": hexrgb(EMBER, 1.0) + [0.0]},                # OP_SEAM
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 1},
            {"kind": "microDetail", "dst": 4, "srcA": 0, "value": 2.0, "seed": 9},
            {"kind": "ramp", "dst": 5, "srcA": 4, "constant": hexrgb("#0a0807") + [1.0],
             "constant2": hexrgb("#1c1816") + [1.0], "constant3": hexrgb("#3a3430") + [1.0]},
        ],
        "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_SEAM = 9


def bark_program():
    """Charred bark: black, with glowing cracks low on the trunk (OP_FLARE's constant is their emission, raised by a
    flare-up); the glow fades with height."""
    return {
        "name": "efBark",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "constant", "dst": 6, "constant": [3.0, 0.35, 3.0, 0.0]},                     # stretched vertically
            {"kind": "multiply", "dst": 6, "srcA": 0, "srcB": 6},
            {"kind": "noise", "dst": 1, "srcA": 6, "value": 1.0, "seed": 12},
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.6, 0.68, 0.0, 0.0]},       # cracks
            {"kind": "swizzle", "dst": 2, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},           # height
            {"kind": "remap", "dst": 2, "srcA": 2, "value": 1, "constant": [0.0, 4.0, 1.0, 0.0]},  # low on the trunk
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "constant", "dst": 3, "constant": hexrgb(EMBER, 0.25) + [0.0]},               # OP_FLARE
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 1},
            {"kind": "constant", "dst": 4, "constant": hexrgb("#0b0908") + [1.0]},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_FLARE = 9


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.1, "release": 1.4}
    s.environment = {
        "intensity": 0.15, "background": hexrgb("#0a0504"), "fogColor": hexrgb("#2a1c16"),
        "volumeDensity": 0.012, "volumeMaxDistance": 60.0, "volumeAnisotropy": 0.6, "volumeNoise": 0.5,
        "volumeNoiseScale": 0.08, "volumeNoiseSpeed": 0.04, "fogHeight": 2.5, "fogHeightFalloff": 0.35,
        "fogHeightAmount": 1.0, "volumeShadowStrength": 0.8, "volumeShadowSteps": 4, "volumeLocalLights": 1.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#030202"), "horizonColor": hexrgb("#4a1806"),
                "groundColor": hexrgb("#050303"), "haze": 0.06, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "ember", "position": [DANCE[0], DANCE[1] + 0.8, DANCE[2]], "radius": 1.2,
                         "weight": 1.0}],
        "layers": [
            {"name": "near", "start": 0.0, "end": 16.0, "contrast": 1.0, "saturation": 1.0},
            {"name": "mid", "start": 16.0, "end": 50.0, "contrast": 0.8, "saturation": 0.8},
            {"name": "far", "start": 50.0, "end": 400.0, "contrast": 0.6, "saturation": 0.7},
        ],
    }
    s.program(ground_program())
    s.program(bark_program())

    s.proc("ground", {"kind": "box", "size": [400.0, 0.4, 400.0], "subdivisions": 2},
           material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0, "roughness": 0.95,
                     "metallic": 0.0, "program": "efAsh"},
           transform={"position": [0.0, -0.2, -120.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- three planes of trunks (instanced cylinders with a noise lean); near ones thick, far ones thin
    planes = [("near", -9.0, 7, 1, 7.0, 3.0, 0.42, 16.0, 3),
              ("mid", -30.0, 11, 2, 5.5, 6.0, 0.3, 18.0, 5),
              ("far", -70.0, 18, 3, 4.5, 12.0, 0.22, 20.0, 7)]
    for name, z, nx, nz, sx, sz, r, h, seed in planes:
        s.proc("trunks_" + name, {"kind": "cylinder", "radius": r, "height": h, "segments": 10},
               distribution={"kind": "grid", "gridCount": [nx, 1, nz], "gridSpacing": [sx, 1.0, sz]},
               material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0,
                         "roughness": 0.9, "metallic": 0.0, "program": "efBark"},
               variation={"seed": seed, "position": [sx * 0.42, 0.0, sz * 0.4], "rotation": [0.05, 3.1, 0.05],
                          "scale": [0.35, 0.25, 0.35]},
               deformers=[{"kind": "noise", "amount": 0.25, "scale": 0.12, "speed": 0.0, "seed": seed,
                           "axisMask": [1.0, 0.0, 1.0]}],
               transform={"position": [0.0, h * 0.5, z], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- light: the fire's glow low behind the last plane (it lights the smoke into shafts); a dim cold fill
    s.light("fire", "directional", direction=[0.12, -0.08, 0.99], color=hexrgb("#ff7a2a"), intensity=1.4,
            castsShadow=True, contactShadow=False, shadowStrength=1.0, softness=0.4, volumetric=1.0)
    s.light("fill", "directional", direction=[-0.3, -1.0, -0.2], color=hexrgb("#3a4050"), intensity=0.03,
            castsShadow=False)
    s.light("emberLight", "point", position=list(DANCE), color=hexrgb(EMBER), intensity=6.0, range=6.0, radius=0.3,
            castsShadow=False, volumetric=0.6)

    # ---- the dancing ember: three particles orbiting a hand that rises with the melody, long trails
    s.particles("ember", capacity=60, seed=5, shape="sphere", position=list(DANCE), extent=[0.05, 0.05, 0.05],
                direction=[0, 1, 0], spawnRate=1.2, lifetimeMin=4.0, lifetimeMax=6.0, spread=1.0, speedMin=0.3,
                speedMax=0.6, gravity=[0, 0.05, 0], drag=0.3, turbulence=0.25, turbulenceScale=0.5,
                attractorPosition=list(DANCE), attractorStrength=2.2, attractorRadius=2.0, orbit=2.6,
                sizeStart=0.03, sizeEnd=0.012, colorStart=hexrgb(HOT) + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=26.0, blend="additive", trailEnabled=True, trailLength=32, trailStride=2, trailWidth=0.6,
                trailTaper=0.0, trailFade=0.0, trailTint=hexrgb("#ff5a10"))
    # sparks (hat) and thrown embers (kick): from the ground seams round the near trunks
    s.particles("sparks", capacity=4000, seed=9, shape="box", position=[0.0, 0.1, -9.0], extent=[14.0, 0.1, 3.0],
                direction=[0.1, 1, 0], spawnRate=8.0, lifetimeMin=0.6, lifetimeMax=2.2, spread=0.5, speedMin=0.6,
                speedMax=2.4, gravity=[0.15, 0.4, 0], drag=0.4, turbulence=0.6, turbulenceScale=0.7,
                sizeStart=0.012, sizeEnd=0.0, colorStart=hexrgb(HOT) + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=18.0, blend="additive", velocityStretch=0.6, stretchMax=0.08)
    s.particles("thrown", capacity=3000, seed=11, shape="sphere", position=[-3.5, 0.6, -9.0],
                extent=[0.4, 0.4, 0.4], direction=[0.4, 1, 0.2], spawnRate=0.0, lifetimeMin=1.0, lifetimeMax=2.5,
                spread=0.6, speedMin=2.0, speedMax=5.0, gravity=[0, -3.0, 0], drag=0.2, turbulence=0.3,
                sizeStart=0.02, sizeEnd=0.004, colorStart=hexrgb(HOT) + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=22.0, blend="additive", trailEnabled=True, trailLength=10, trailStride=1, trailWidth=0.5,
                trailTaper=0.0, trailFade=0.0)

    # ---- the ember's walk round the near trunks (keyed), its height the melody (routed)
    keys = []
    for i in range(13):
        u = i / 12.0
        a = 2.0 * math.pi * u
        keys.append({"time": round(24.0 * u, 4),
                     "value": [round(DANCE[0] + 1.6 * math.sin(a), 4), DANCE[1],
                               round(DANCE[2] + 0.8 * math.sin(2 * a), 4)], "interp": "smooth"})
    for t in ("particles/ember/position", "particles/ember/attractorPosition", "lights/emberLight/position"):
        s.track(t, keys, loop=24.0)

    # ---- camera: a slow sideways track
    s.camera["fov"] = 45.0
    s.track("camera/position", [{"time": 0.0, "value": [-4.0, 1.7, 6.0], "interp": "smooth"},
                                {"time": 45.0, "value": [4.0, 1.75, 6.0], "interp": "smooth"},
                                {"time": 90.0, "value": [-4.0, 1.7, 6.0], "interp": "smooth"}], loop=90.0)
    s.track("camera/target", [{"time": 0.0, "value": [-2.0, 2.6, -20.0], "interp": "smooth"},
                              {"time": 45.0, "value": [4.5, 2.6, -20.0], "interp": "smooth"},
                              {"time": 90.0, "value": [-2.0, 2.6, -20.0], "interp": "smooth"}], loop=90.0)
    s.camera = {"mode": 1, "position": [-4.0, 1.7, 6.0], "target": [-2.0, 2.6, -20.0], "fov": 45.0,
                "orbitSpeed": 0.0}

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # melody: the ember's height (a spring); each note flares it
    for t in ("particles/ember/position", "particles/ember/attractorPosition", "lights/emberLight/position"):
        s.route(R("lastPitch", t, 4.0, comp=1, offset=-0.3, springHz=1.3, springDamping=0.55))
    s.route(R("noteEnv", "particles/ember/emissive", 30.0, depth="lastVelocity", attackMs=0, decayMs=400),
            R("noteEnv", "lights/emberLight/intensity", 30.0, attackMs=0, decayMs=400),
            R("noteOn", "particles/ember/burst", 1.0, threshold="binary", thresholdLevel=0.01))
    # bass: the wind breathes on the coals and stirs the smoke
    for c, w in enumerate(hexrgb(EMBER, 1.0)):
        if w > 1e-4:
            s.route(R("bass", "material/efAsh/op/%d/constant/constant" % OP_SEAM, 5.0 * w, comp=c, attackMs=300,
                      decayMs=1400))
    s.route(R("bass", "scene/volumeDensity", 0.02, attackMs=400, decayMs=1500),
            R("bass", "particles/sparks/spawnRate", 120.0, attackMs=200, decayMs=900))
    # snare: a flare-up runs up the trunks
    for c, w in enumerate(hexrgb(EMBER, 1.0)):
        if w > 1e-4:
            s.route(R("snare", "material/efBark/op/%d/constant/constant" % OP_FLARE, 9.0 * w, comp=c, attackMs=0,
                      decayMs=450))
    # kick: a crack and a throw of embers; hat: sparks; sustain: the fire's glow builds
    s.route(R("kick", "particles/thrown/burst", 70.0, attackMs=0, decayMs=40),
            R("hat", "particles/sparks/burst", 40.0, attackMs=0, decayMs=40),
            R("sustain", "lights/fire/intensity", 2.6, **SLOW),
            R("sustain", "env/sky/intensity", 1.2, **SLOW),
            R("kick", "post/lens/chromaticAberration", 0.008, attackMs=0, decayMs=120))

    s.params_({
        "scene/volumeSteps": 32, "scene/volumeJitter": 0.6,
        "post/bloom/intensity": 0.32, "post/bloom/threshold": 1.0, "post/output/vignette": 0.45,
        "post/output/grain": 0.03, "post/grade/contrast": 1.08, "post/grade/temperature": 0.04,
        "post/lens/chromaticAberration": 0.002, "post/tonemap/operator": 3, "camera/lens/focalLength": 32.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("ember", centre=[DANCE[0], DANCE[1] + 0.8, DANCE[2]], radius=2.0)
    s.region("ground", box=[0.0, 0.7, 1.0, 1.0])
    s.region("glow", box=[0.0, 0.3, 1.0, 0.62])
    return s
