"""SILK THEATRE (abstract). Silk ribbons draw the melody in the air of an empty theatre: pitch is height, legato is
one unbroken stroke, and the kick cracks the ribbons like whips.

Composition (SCENE-CATALOG.md #11): a black stage under a single top spot whose cone stands in the haze, a crimson
velvet curtain falling into darkness behind, a polished floor. The subject is a leader -- an invisible dancer whose
hand the particles orbit -- that walks a figure of eight across the stage; its height is the melody. The particles'
trails are the ribbons, so a phrase leaves a stroke in the air, a staccato line a scatter of short flicks, and a chord
a sheaf of parallel gold ribbons. Nothing else moves.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "silk-theatre"
TITLE = "Silk Theatre"

VELVET = "#1a0306"
SCARLET = "#d01a2a"
GOLD = "#ffbf5a"

LEAD = (0.0, 1.2, 0.0)     # the leader's rest position (the figure of eight is keyed round it)

DESIGN = {
    "category": "abstract",
    "thesis": "Silk ribbons draw the melody in the air of an empty theatre: pitch is height, legato is one unbroken "
              "stroke, and the kick cracks the ribbons like whips.",
    "composition": {
        "background": "a crimson velvet curtain falling into darkness",
        "midground": "the spot's cone of light standing in the haze",
        "foreground": "a black polished stage floor holding the spot's pool",
        "focal": "the ribbons' newest stroke, at the leader's hand",
        "secondary": ["the gold sheaf of a chord", "glitter on the hats", "the spot's pool"],
        "atmosphere": "stage haze, so the cone is a solid of light",
        "post": "bloom, a warm grade, grain",
        "camera": "a slow orbit at stage height",
    },
    "palette": {"dominant": VELVET, "secondary": SCARLET, "accent": GOLD, "highlight": "#fff6ea",
                "background_value": "near black",
                "saturation": "scarlet silk and gold on black; the curtain is a darker red"},
    "motion": {
        "very_slow": ["the camera's orbit", "the leader's figure of eight"],
        "medium": ["the whole dance's sway (bass)", "the spot's breathing (sustain)"],
        "fast": ["the ribbons' strokes (notes)"],
        "extremely_fast": ["the whip-crack (kick)", "glitter (hat)"],
    },
    "vocabulary": [
        ["melodic", "notes.lastPitch", "the leader's height: the ribbon draws the melody's contour in the air"],
        ["legato", "notes.held", "a held line is one unbroken stroke (the leader keeps spinning silk)"],
        ["note", "response.note", "each note-on throws a flick of silk"],
        ["velocity", "notes.lastVelocity", "how wide the ribbon is"],
        ["chord", "notes.polyphony", "a sheaf of parallel gold ribbons"],
        ["bass", "response.bass", "the whole dance sways"],
        ["kick", "response.kick", "a whip-crack: the ribbons snap tight round the hand"],
        ["hat", "response.hat", "glitter sparks along the ribbons"],
        ["sustained", "response.sustain", "the spot breathes brighter"],
        ["silence", "(no input)", "the empty stage: the cone of light, the curtain, the last ribbons falling"],
    ],
    "tier": "light: two ribbon systems, a short haze march under one spot",
}


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.3}
    s.environment = {
        "intensity": 0.0, "background": hexrgb("#020001"), "fogColor": hexrgb("#120406"),
        "volumeDensity": 0.03, "volumeMaxDistance": 24.0, "volumeAnisotropy": 0.45, "volumeNoise": 0.25,
        "volumeNoiseScale": 0.35, "volumeNoiseSpeed": 0.05, "volumeLocalLights": 1.0, "skyIntensity": 0.0,
        "sky": {"enabled": False, "background": False},
    }
    s.composition = {
        "focalPoints": [{"name": "hand", "position": [LEAD[0], LEAD[1] + 0.6, LEAD[2]], "radius": 1.0,
                         "weight": 1.0}],
        "layers": [
            {"name": "stage", "start": 0.0, "end": 12.0, "contrast": 1.0, "saturation": 1.0},
            {"name": "curtain", "start": 12.0, "end": 40.0, "contrast": 0.85, "saturation": 0.9},
        ],
    }

    # ---- the stage: a polished black floor, the curtain's folds behind
    s.proc("floor", {"kind": "box", "size": [26.0, 0.2, 18.0], "subdivisions": 1},
           material={"baseColor": hexrgb("#050404"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.12, "metallic": 0.0},
           transform={"position": [0.0, -0.1, -1.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("curtain", {"kind": "box", "size": [22.0, 11.0, 0.05], "subdivisions": 64},
           material={"baseColor": hexrgb("#5a0610"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.92, "metallic": 0.0},
           deformers=[{"kind": "sine", "amount": 0.2, "axis": [1, 0, 0], "displacementAxis": [0, 0, 1],
                       "frequency": 3.4, "phase": 0.0, "speed": 0.0},
                      {"kind": "sine", "amount": 0.07, "axis": [1, 0, 0], "displacementAxis": [0, 0, 1],
                       "frequency": 9.1, "phase": 1.3, "speed": 0.0},
                      {"kind": "noise", "amount": 0.06, "scale": 0.4, "speed": 0.05, "seed": 4,
                       "axisMask": [0.0, 0.0, 1.0]}],
           transform={"position": [0.0, 5.5, -7.5], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.entity("curtain")

    # ---- light: one top spot whose cone stands in the haze; a faint warm bounce from the floor's pool
    s.light("spot", "spot", position=[0.0, 9.5, 0.6], direction=[0.0, -1.0, -0.06], color=hexrgb("#ffe7c4"),
            intensity=900.0, range=22.0, innerCone=14.0, outerCone=24.0, castsShadow=False, volumetric=1.0)
    s.light("bounce", "point", position=[0.0, 0.3, 0.5], color=hexrgb("#ff6a40"), intensity=6.0, range=9.0,
            radius=2.0, castsShadow=False, volumetric=0.0)
    s.light("rim", "spot", position=[0.0, 7.5, -6.8], direction=[0.0, -0.4, 1.0], color=hexrgb("#ff3040"),
            intensity=60.0, range=16.0, innerCone=30.0, outerCone=55.0, castsShadow=False, volumetric=0.15)

    # ---- the silk: particles orbit the leader's hand; their trails are the ribbons
    silk = dict(shape="sphere", position=list(LEAD), extent=[0.04, 0.04, 0.04], direction=[0, 1, 0],
                spawnRate=0.0, spread=1.0, gravity=[0, -0.15, 0], drag=0.35, turbulence=0.05, turbulenceScale=0.6,
                attractorPosition=list(LEAD), attractorStrength=2.6, attractorRadius=2.4, orbit=3.2,
                blend="alpha", trailEnabled=True, trailStride=2, trailTaper=0.0, trailFade=0.0)
    s.particles("silk", capacity=900, seed=7, lifetimeMin=4.0, lifetimeMax=6.0, speedMin=0.5, speedMax=0.9,
                sizeStart=0.05, sizeEnd=0.035, colorStart=hexrgb(SCARLET) + [0.95],
                colorEnd=hexrgb("#7a0610") + [0.0], emissive=1.6, trailLength=32, trailWidth=1.0,
                trailTint=hexrgb("#ff5a4a"), **silk)
    s.particles("gold", capacity=600, seed=13, lifetimeMin=3.0, lifetimeMax=4.5, speedMin=0.7, speedMax=1.1,
                sizeStart=0.03, sizeEnd=0.02, colorStart=hexrgb(GOLD) + [0.9],
                colorEnd=hexrgb("#a86a1a") + [0.0], emissive=2.2, trailLength=28, trailWidth=1.0,
                trailTint=hexrgb("#ffdf9a"), **silk)
    s.particles("glitter", capacity=1500, seed=17, shape="sphere", position=list(LEAD), extent=[0.6, 0.6, 0.6],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.25, lifetimeMax=0.7, spread=1.0, speedMin=0.05,
                speedMax=0.3, gravity=[0, -0.4, 0], drag=0.6, sizeStart=0.012, sizeEnd=0.0,
                colorStart=hexrgb("#fff2c8") + [1.0], colorEnd=hexrgb(GOLD) + [0.0], emissive=8.0,
                blend="additive")

    # the leader walks a figure of eight across the stage (16 s), the emitters and attractors ride it
    keys = []
    for i in range(17):
        u = i / 16.0
        a = 2.0 * math.pi * u
        keys.append({"time": round(16.0 * u, 4),
                     "value": [round(LEAD[0] + 2.8 * math.sin(a), 4), LEAD[1],
                               round(LEAD[2] + 1.2 * math.sin(2.0 * a), 4)], "interp": "smooth"})
    for sysname in ("silk", "gold", "glitter"):
        s.track("particles/%s/position" % sysname, keys, loop=16.0)
        if sysname != "glitter":
            s.track("particles/%s/attractorPosition" % sysname, keys, loop=16.0)

    # ---- camera: a slow orbit at stage height, the hand off centre
    s.camera["fov"] = 45.0
    s.arc_camera((0.0, 1.5, 0.0), radius=9.0, height=1.7, period=70.0, centre_deg=0.0, sweep_deg=50.0, steps=16,
                 side=1.2, lift=0.6, height_bob=0.15)

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # melody: the leader's height (low notes at the floor, high notes overhead); a spring, so it swings to each note
    for sysname in ("silk", "gold", "glitter"):
        s.route(R("lastPitch", "particles/%s/position" % sysname, 5.0, comp=1, springHz=1.6, springDamping=0.6,
                  offset=-0.3))
        if sysname != "glitter":
            s.route(R("lastPitch", "particles/%s/attractorPosition" % sysname, 5.0, comp=1, springHz=1.6,
                      springDamping=0.6, offset=-0.3))
    # each note throws a flick; a held line keeps spinning silk (one unbroken stroke)
    s.route(R("noteOn", "particles/silk/burst", 4.0, threshold="binary", thresholdLevel=0.01),
            R("held", "particles/silk/spawnRate", 40.0, **MEDIUM),
            R("lastVelocity", "particles/silk/trailWidth", 1.4, **FAST))
    # chord: a sheaf of gold ribbons
    s.map(M("chordOn", [("noteOn", 1.0), ("polyphony", 1.0)], "min"))
    s.route(R("visual.chordOn", "particles/gold/burst", 30.0, attackMs=0, decayMs=60),
            R("polyphony", "particles/gold/spawnRate", 25.0, **MEDIUM))
    # bass: the dance sways; kick: the whip-crack (the ribbons snap tight round the hand)
    for sysname in ("silk", "gold"):
        s.route(R("bass", "particles/%s/attractorPosition" % sysname, 0.6, comp=0, attackMs=300, decayMs=900),
                R("kick", "particles/%s/attractorStrength" % sysname, 9.0, attackMs=0, decayMs=180),
                R("kick", "particles/%s/orbit" % sysname, 6.0, attackMs=0, decayMs=220))
    # hat: glitter; sustain: the spot breathes
    s.route(R("hat", "particles/glitter/burst", 60.0, attackMs=0, decayMs=40),
            R("sustain", "lights/spot/intensity", 500.0, **SLOW),
            R("snare", "lights/rim/intensity", 260.0, attackMs=0, decayMs=240),
            R("kick", "post/lens/chromaticAberration", 0.008, attackMs=0, decayMs=120),
            R("noteEnv", "post/bloom/intensity", 0.08, attackMs=0, decayMs=300))

    s.params_({
        "scene/volumeSteps": 32, "scene/volumeJitter": 0.5,
        "post/bloom/intensity": 0.3, "post/bloom/threshold": 1.0, "post/output/vignette": 0.45,
        "post/output/grain": 0.02, "post/grade/temperature": 0.05, "post/grade/contrast": 1.06,
        "post/lens/chromaticAberration": 0.002, "post/tonemap/operator": 3,
        "camera/lens/focalLength": 35.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("dance", centre=[LEAD[0], LEAD[1] + 1.2, LEAD[2]], radius=3.2)
    s.region("curtain", box=[0.0, 0.0, 1.0, 0.45])
    s.region("floor", box=[0.0, 0.75, 1.0, 1.0])
    return s
