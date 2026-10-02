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
        ["kick", "response.kick", "a whip-crack: the silk shudders and shreds fly from the hand"],
        ["snare", "response.snare", "gold shreds fly; the rim light flashes"],
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
        "volumeDensity": 0.0035, "volumeMaxDistance": 24.0, "volumeAnisotropy": 0.55, "volumeNoise": 0.15,
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
           material={"baseColor": hexrgb("#171213"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.22, "metallic": 0.0},
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
            intensity=2400.0, range=22.0, innerCone=9.0, outerCone=13.0, castsShadow=False, volumetric=1.0)
    # the curtain's wash: a spot from above the stage front, grazing the velvet so its folds stand out
    s.light("wash", "spot", position=[0.0, 10.5, -2.5], direction=[0.0, -0.72, -0.69], color=hexrgb("#ffd0c0"),
            intensity=150.0, range=18.0, innerCone=34.0, outerCone=56.0, castsShadow=False, volumetric=0.0)
    # (its source above the frame: placed in view, it showed through the haze as a red dot over the curtain)
    s.light("rim", "spot", position=[0.0, 12.0, -6.8], direction=[0.0, -0.85, 0.53], color=hexrgb("#ff3040"),
            intensity=60.0, range=16.0, innerCone=30.0, outerCone=55.0, castsShadow=False, volumetric=0.0)

    # ---- the silk: a brush stroke in the air. While a note sounds, the hand lays silk continuously -- particles born
    # where the hand is and left there, overlapping into one unbroken band (a trail is capped at 32 points, too
    # short and too frame-rate-dependent for a ribbon; a laid stroke is neither). The stroke sags and fades over
    # five seconds, like silk settling; a chord lays a second, gold stroke beside the red one.
    # The silk is a BAND, not a tube: the hand is a short vertical bar (the emitter's box), so the laid particles --
    # many, tiny, translucent -- weave a ribbon a hand's breadth wide whose face turns to the light as the stroke
    # turns, fibres showing at its edges.
    silk = dict(shape="box", position=list(LEAD), direction=[0, 1, 0], spawnRate=0.0, spread=1.0, speedMin=0.0,
                speedMax=0.015, gravity=[0, -0.05, 0], drag=1.5, turbulence=0.015, turbulenceScale=0.4,
                blend="additive", sizeVariance=0.5, sizeSkew=1.6)
    # (the box is as deep as the hand travels in a frame, so one frame's fibres meet the next: no comb)
    s.particles("silk", capacity=16000, seed=7, lifetimeMin=4.5, lifetimeMax=5.5, extent=[0.05, 0.09, 0.05],
                sizeStart=0.028, sizeEnd=0.016, colorStart=hexrgb("#ff4a3a") + [0.42],
                colorEnd=hexrgb("#5a0410") + [0.0], emissive=1.4, **silk)
    s.particles("gold", capacity=9000, seed=13, lifetimeMin=3.5, lifetimeMax=4.5, extent=[0.04, 0.05, 0.04],
                sizeStart=0.02, sizeEnd=0.012, colorStart=hexrgb(GOLD) + [0.5], colorEnd=hexrgb("#a86a1a") + [0.0],
                emissive=1.8, **silk)
    # the whip-crack: a flick of silk shreds thrown from the hand (kick red, snare gold), stopped by the air
    for name, colour, seed in (("shreds", "#ff3a3a", 19), ("goldShreds", GOLD, 23)):
        s.particles(name, capacity=1500, seed=seed, shape="sphere", position=list(LEAD), extent=[0.05, 0.05, 0.05],
                    direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.8, lifetimeMax=1.4, spread=1.0, speedMin=2.0,
                    speedMax=4.5, gravity=[0, -1.2, 0], drag=2.6, sizeStart=0.045, sizeEnd=0.0,
                    colorStart=hexrgb(colour) + [0.9], colorEnd=hexrgb(colour) + [0.0], emissive=1.4,
                    blend="alpha", velocityStretch=1.0, stretchMax=0.25)
    s.particles("glitter", capacity=1500, seed=17, shape="sphere", position=list(LEAD), extent=[0.6, 0.6, 0.6],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.25, lifetimeMax=0.7, spread=1.0, speedMin=0.05,
                speedMax=0.3, gravity=[0, -0.4, 0], drag=0.6, sizeStart=0.012, sizeEnd=0.0,
                colorStart=hexrgb("#fff2c8") + [1.0], colorEnd=hexrgb(GOLD) + [0.0], emissive=8.0,
                blend="additive")

    # the leader walks a figure of eight across the stage (9 s), the emitters and attractors ride it
    keys = []
    for i in range(17):
        u = i / 16.0
        a = 2.0 * math.pi * u
        keys.append({"time": round(9.0 * u, 4),
                     "value": [round(LEAD[0] + 3.4 * math.sin(a), 4), LEAD[1],
                               round(LEAD[2] + 1.6 * math.sin(2.0 * a), 4)], "interp": "smooth"})
    for sysname in ("silk", "gold", "glitter", "shreds", "goldShreds"):
        s.track("particles/%s/position" % sysname, keys, loop=9.0)
    # the ribbon turns as it flies: the band's height (the hand's bar) narrows to its edge and opens to its face,
    # so the laid stroke twists along its length; the gold turns against the red
    for sysname, hmax, hmin, phase in (("silk", 0.12, 0.018, 0.0), ("gold", 0.07, 0.012, 0.55)):
        tk = []
        for i in range(9):
            u = i / 8.0
            h = hmin + (hmax - hmin) * (0.5 + 0.5 * math.cos(2.0 * math.pi * (u + phase)))
            tk.append({"time": round(1.6 * u, 4), "value": round(h, 4), "interp": "smooth"})
        s.track("particles/%s/extent" % sysname, tk, loop=1.6, comp=1)

    # ---- camera: a slow orbit at stage height, the hand off centre
    s.camera["fov"] = 45.0
    s.arc_camera((0.0, 1.5, 0.0), radius=7.0, height=1.7, period=70.0, centre_deg=0.0, sweep_deg=50.0, steps=16,
                 side=1.2, lift=0.6, height_bob=0.15)

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # melody: the hand's height (low notes at the floor, high notes overhead), on a slow spring, so the stroke curves
    # from note to note instead of stepping
    for sysname, dy in (("silk", 0.0), ("gold", 0.18), ("glitter", 0.0), ("shreds", 0.0), ("goldShreds", 0.0)):
        s.route(R("lastPitch", "particles/%s/position" % sysname, 5.0, comp=1, springHz=0.7, springDamping=0.85,
                  offset=-0.3 + dy / 5.0))
    # a sounding note lays silk (legato: one unbroken stroke; staccato: short strokes); each note-on a small knot
    s.route(R("active", "particles/silk/spawnRate", 2600.0, attackMs=20, decayMs=60),
            R("noteOn", "particles/silk/burst", 12.0, threshold="binary", thresholdLevel=0.01),
            R("lastVelocity", "particles/silk/size", 0.6, **FAST),
            R("noteEnv", "particles/silk/emissive", 2.2, attackMs=0, decayMs=260))
    # (no note on the spot or the bloom: a note brightening the whole stage is a visualizer's pump; the note is the
    #  silk's own flare)
    # chord: a second, gold stroke beside the red
    s.route(R("polyphony", "particles/gold/spawnRate", 1900.0, attackMs=20, decayMs=80))
    # bass: the dance sways; kick: the whip-crack -- the laid silk shudders all along its length
    for sysname in ("silk", "gold"):
        s.route(R("bass", "particles/%s/position" % sysname, 0.5, comp=0, attackMs=50, decayMs=900),
                R("kick", "particles/%s/turbulence" % sysname, 2.4, attackMs=0, decayMs=180))
    s.route(R("kick", "particles/shreds/burst", 70.0, attackMs=0, decayMs=40),
            R("snare", "particles/goldShreds/burst", 50.0, attackMs=0, decayMs=40))
    # hat: glitter; sustain: the spot breathes
    s.route(R("hat", "particles/glitter/burst", 60.0, attackMs=0, decayMs=40),
            R("sustain", "lights/spot/intensity", 500.0, **SLOW),
            R("snare", "lights/rim/intensity", 260.0, attackMs=0, decayMs=240),
            R("kick", "post/lens/chromaticAberration", 0.008, attackMs=0, decayMs=120))

    s.params_({
        "scene/volumeSteps": 32, "scene/volumeJitter": 0.5,
        "post/bloom/intensity": 0.3, "post/bloom/threshold": 1.0, "post/output/vignette": 0.45,
        "post/output/grain": 0.02, "post/grade/temperature": 0.05, "post/grade/contrast": 1.06,
        "post/lens/chromaticAberration": 0.002, "post/tonemap/operator": 3,
        "camera/lens/focalLength": 28.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("dance", centre=[LEAD[0], LEAD[1] + 1.2, LEAD[2]], radius=3.2)
    s.region("curtain", box=[0.0, 0.0, 1.0, 0.45])
    s.region("floor", box=[0.0, 0.75, 1.0, 1.0])
    return s
