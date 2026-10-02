"""SALT FLAT MIRAGE (atmospheric, lonely). On an endless salt flat at dusk the sky is the instrument: chords paint it,
melodies fall through it as meteors, the bass makes the horizon shimmer, and a black monolith answers the drums.

Composition (SCENE-CATALOG.md #8): an ultra-wide, almost still frame. The horizon sits on the lower third and the sky
owns the rest. The monolith stands on the left third with the setting sun peeking past its right edge, so it is a
silhouette against the brightest part of the sky and its shadow runs at the camera: a dark wedge from the bottom of
the frame to its base, beside the sun's glow broken across the puddles. A thin range of violet mountains melts into
the haze. Colour moves only along designed dusk states (the project palette, ADR-1043, blended in OKLab): gold, rose,
violet hour, indigo.

Why a monolith: the scene's thesis is loneliness, so it has exactly one made object, and that object is the only thing
on the ground that answers the music (its outline flares on the snare; the kick sends a ring of light out from its
base). Everything else that moves is weather and sky.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "salt-flat-mirage"
TITLE = "Salt Flat Mirage"

LAVENDER = "#8a86b3"
SALT = "#e7e1d8"
ROSE = "#e9a49a"
MOON = "#fff6ea"

CAM = (2.5, 1.55, 6.0)
MONO = (-11.0, -30.0)            # the monolith's footprint centre (x, z)
MONO_SIZE = (4.4, 9.9, 1.1)      # 4 : 9 : 1, the film's proportions
MONO_YAW = 12.0                  # turned so its right side catches the light as a sliver
RIM_W = 0.035                    # the outline's width, metres


def _dir(yaw_deg, el_deg):
    """A unit vector toward bearing `yaw` (0 = -Z, positive = to the right) and elevation `el`."""
    y, e = math.radians(yaw_deg), math.radians(el_deg)
    return [round(math.sin(y) * math.cos(e), 5), round(math.sin(e), 5), round(-math.cos(y) * math.cos(e), 5)]


MONO_YAW_FROM_CAM = math.degrees(math.atan2(MONO[0] - CAM[0], -(MONO[1] - CAM[2])))
SUN_YAW = MONO_YAW_FROM_CAM + 3.1    # peeking past the monolith's right edge (its half-width is ~3.2 degrees)
SUN_EL = 2.0                         # the disc, sitting on the range
LIGHT_EL = 4.5                       # the light that casts the shadow: a little higher, so the shadow is a wedge
                                     # that ends in frame rather than a band that never ends

DESIGN = {
    "category": "atmospheric",
    "thesis": "On an endless salt flat at dusk the sky is the instrument: chords paint it, melodies fall through it "
              "as meteors, the bass makes the horizon shimmer, and a black monolith answers the drums.",
    "composition": {
        "background": "a dusk sky (two thirds of the frame) over a thin violet range",
        "midground": "the horizon dissolving into haze and heat shimmer",
        "foreground": "salt crust polygons and puddles to the horizon; the monolith's shadow, a dark wedge from the "
                      "frame's foot; the sun's glow broken across the puddles",
        "focal": "the monolith on the left third, a silhouette with the setting sun just behind its right edge",
        "secondary": ["meteors", "the ring of light the kick sends across the salt", "the path of light on the "
                      "puddles"],
        "atmosphere": "horizon haze; blowing salt grains",
        "post": "halation round the sun, fine grain, a cool dusk grade with a warm horizon",
        "camera": "ultra-wide (18 mm), low, nearly still: a 90 s drift of a few centimetres",
    },
    "palette": {"dominant": "#5a5070", "secondary": "#c48a8a", "accent": "#0a0a0c", "highlight": "#ffd8a0",
                "background_value": "mid (a dusk sky)",
                "saturation": "the horizon band is the only saturated region; the salt takes the sky's colour"},
    "motion": {
        "very_slow": ["the sky's colour (the palette)", "the camera"],
        "medium": ["the shimmer", "the haze", "blowing salt"],
        "fast": ["meteors"],
        "extremely_fast": ["the monolith's outline (snare)", "the ring of light (kick)"],
    },
    "vocabulary": [
        ["sustained / chords", "notes.tension", "the chord paints the sky: a consonant chord warms the dusk to "
         "gold, dissonance darkens it through violet to indigo; a bright sound brightens it, held sound lifts the sun"],
        ["melodic", "response.note", "a meteor falls at the pitch's place across the sky (low notes "
         "low and left, high notes high and right), all from one radiant"],
        ["velocity", "notes.lastVelocity", "the meteor's brightness"],
        ["bass", "response.bass", "heat shimmer along the horizon; the haze thickens"],
        ["kick", "response.kick", "a ring of light runs out across the salt from the monolith's base"],
        ["snare", "response.snare", "the monolith's outline flares white"],
        ["hat", "response.hat", "salt crystals glint across the near crust; grains blow low across the flat"],
        ["silence", "(no input)", "a still rose dusk: the sky settles, nothing moves but the drift"],
    ],
    "tier": "light: one shadowed sun, a thin uniform haze (no march detail), one DF shimmer column",
}


def salt_program():
    """The salt crust after rain: dry crust with a fine grain, a few sharp-edged puddles, and -- far out -- a sheet of
    standing water that mirrors the sky, so the range floats on a band of light: the mirage. Puddles are darker and
    nearly mirrors, so the sun's glow lies on them as a broken path of light toward the camera."""
    return {
        "name": "sfSalt",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "microDetail", "dst": 1, "srcA": 0, "value": 3.0, "seed": 3},           # crust grain
            {"kind": "remap", "dst": 1, "srcA": 1, "value": 1, "constant": [0.3, 0.7, 0.88, 1.08]},
            {"kind": "noise", "dst": 3, "srcA": 0, "value": 0.03, "seed": 7},               # puddles
            {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.6, 0.606, 0.0, 0.0]},
            {"kind": "input", "dst": 2, "input": "cameraDistance"},                         # the far sheet
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [90.0, 140.0, 0.0, 0.0]},
            {"kind": "add", "dst": 3, "srcA": 3, "srcB": 2},
            {"kind": "remap", "dst": 3, "srcA": 3, "value": 1, "constant": [0.0, 1.0, 0.0, 1.0]},  # wet 0..1
            {"kind": "constant", "dst": 4, "constant": hexrgb(SALT, 0.72) + [1.0]},         # dry crust
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 1},
            {"kind": "constant", "dst": 5, "constant": hexrgb("#6a6478", 0.3) + [1.0]},     # wet salt
            {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 5, "srcC": 3},
            {"kind": "remap", "dst": 6, "srcA": 3, "value": 1, "constant": [0.0, 1.0, 0.88, 0.04]},  # wet: a mirror
        ],
        "baseColor": 4, "metallic": -1, "roughness": 6, "emission": -1, "emissionIntensity": 1.0, "opacity": -1,
    }


# 1-based index of the outline colour's constant op (routed by the snare)
OP_RIM = 9


def monolith_program():
    """Polished black, with its outline lit: the faces' distance to the box's edges, from the local position."""
    hw, hh, hd = (v * 0.5 for v in MONO_SIZE)
    ops = [
        {"kind": "input", "dst": 0, "input": "localPosition"},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 0},
        {"kind": "power", "dst": 0, "srcA": 0, "value": 0.5},                                      # |p|
        {"kind": "constant", "dst": 1, "constant": [-hw, -hh, -hd, 0.0]},
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},                                           # 0 at a face
        {"kind": "smoothstep", "dst": 0, "srcA": 0, "constant": [-RIM_W, 0.0, 0.0, 0.0]},       # 1 near an edge
        {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},              # the y edges
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},                                           # x + y
        {"kind": "constant", "dst": 2, "constant": hexrgb("#ffd9b0", 0.25) + [0.0]},              # OP_RIM
        {"kind": "swizzle", "dst": 0, "srcA": 0, "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "remap", "dst": 0, "srcA": 0, "value": 1, "constant": [0.0, 1.0, 0.0, 1.0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 0},
        {"kind": "constant", "dst": 3, "constant": hexrgb("#07070a") + [1.0]},
    ]
    assert ops[OP_RIM - 1]["kind"] == "constant" and ops[OP_RIM - 1]["dst"] == 2
    return {"name": "sfMonolith", "ops": ops, "baseColor": 3, "metallic": -1, "roughness": -1, "emission": 2,
            "emissionIntensity": 1.0, "opacity": -1}


def palette():
    def st(name, zen, hor, sun, fog, rim):
        return {"name": name, "colors": {"zenith": hexrgb(zen), "horizon": hexrgb(hor), "sun": hexrgb(sun),
                                         "fog": hexrgb(fog), "light": hexrgb(rim)}}
    return {
        "states": [
            st("gold", "#2c4a86", "#ffad66", "#ffcf8a", "#e0a07a", "#ffcf96"),
            st("rose", "#1a2462", "#f69580", "#ffa070", "#c8848a", "#ffb08a"),
            st("violet", "#120f3c", "#b45f9c", "#ff8462", "#8c5e8e", "#ff9a86"),
            st("indigo", "#05081c", "#34346e", "#a888ff", "#30305e", "#b49aff"),
        ],
        "bindings": [
            {"role": "zenith", "target": "env/sky/zenithColor"},
            {"role": "horizon", "target": "env/sky/horizonColor"},
            {"role": "sun", "target": "env/sky/sunColor"},
            {"role": "light", "target": "lights/sun/color"},
            {"role": "fog", "target": "scene/fogColor"},
        ],
        "position": 1.0, "saturation": 1.15, "value": 1.0,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.45, "sustain": 0.6, "attack": 1.3, "release": 1.6}
    # The air: a thin uniform haze. The march covers only the first 60 m (so looking into the sun does not fill the
    # monolith with in-scattered light); beyond it the surfaces fade in closed form toward the sky's own radiance
    # (fogSky), so the range melts into the horizon behind it.
    s.environment = {
        "intensity": 1.0, "background": hexrgb("#2b3a78"), "fogColor": hexrgb("#d0928e"),
        "volumeDensity": 0.00028, "volumeMaxDistance": 60.0, "volumeAnisotropy": 0.2, "volumeShadowStrength": 0.0,
        "volumeNoise": 0.0, "fogSky": 1.0, "fogSkyDistance": 2500.0, "skyIntensity": 1.0, "shadowRange": 120.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#2b3a78"), "horizonColor": hexrgb("#f2a08a"),
                "groundColor": hexrgb("#2a2632"), "haze": 0.17, "sunColor": hexrgb("#ffa070"), "sunIntensity": 30.0,
                "sunSize": 0.012, "sunGlow": 0.12, "intensity": 1.0, "background": True, "useKeyLight": False,
                "sunDirection": _dir(SUN_YAW, SUN_EL)},
    }
    s.palette = palette()
    hw, hh, hd = MONO_SIZE
    s.composition = {
        "focalPoints": [{"name": "monolith", "position": [MONO[0], hh * 0.55, MONO[1]], "radius": 5.0,
                         "weight": 1.0}],
        "layers": [
            {"name": "crust", "start": 0.0, "end": 60.0, "contrast": 1.0, "saturation": 1.0},
            {"name": "flat", "start": 60.0, "end": 900.0, "contrast": 0.85, "saturation": 0.8},
            {"name": "range", "start": 900.0, "end": 9000.0, "contrast": 0.6, "saturation": 0.65},
        ],
    }
    s.program(salt_program())
    s.program(monolith_program())
    s.effect("stars", "stars", ("world",), parameters={"brightness": 0.5, "density": 0.04, "magnitudeSlope": 7.0,
                                                        "colorSpread": 0.4, "twinkle": 0.25, "twinkleRate": 2.0,
                                                        "horizonFade": 0.5, "band": 0.0, "daylight": 0.8})

    # ---- the flat, the monolith, the range
    # (a procedural's size is at most 1000 m: the flat is scaled)
    s.proc("flat", {"kind": "box", "size": [1000.0, 0.4, 1000.0], "subdivisions": 2},
           material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.8,
                     "metallic": 0.0, "program": "sfSalt"},
           transform={"position": [0.0, -0.2, -2000.0], "rotation": [0, 0, 0], "scale": [9.0, 1.0, 9.0]})
    s.proc("monolith", {"kind": "box", "size": [hw, hh, hd], "subdivisions": 1},
           material={"baseColor": hexrgb("#07070a"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0,
                     "roughness": 0.38, "metallic": 0.0, "program": "sfMonolith"},
           transform={"position": [MONO[0], hh * 0.5, MONO[1]], "rotation": [0, MONO_YAW, 0], "scale": [1, 1, 1]})
    s.entity("monolith")
    # the range: eight 1000 m spans whose ridgeline is one world-space noise, so they join without a seam
    for k in range(8):
        s.proc("range%d" % k, {"kind": "box", "size": [1000.0, 150.0, 80.0], "subdivisions": 64},
               material={"baseColor": hexrgb("#4a3f6a"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                         "roughness": 1.0, "metallic": 0.0},
               deformers=[{"kind": "noise", "amount": 80.0, "scale": 0.0019, "speed": 0.0, "seed": 9,
                           "axisMask": [0, 1, 0], "space": "world"},
                          {"kind": "noise", "amount": 20.0, "scale": 0.011, "speed": 0.0, "seed": 10,
                           "axisMask": [0, 1, 0], "space": "world"}],
               transform={"position": [-2900.0 + 1000.0 * k, 30.0, -3300.0], "rotation": [0, 0, 0],
                          "scale": [1, 1, 1]})
    # ---- light: the low sun, behind the monolith (its shadow runs at the camera)
    toward = _dir(SUN_YAW, LIGHT_EL)
    s.light("sun", "directional", direction=[-v for v in toward], color=hexrgb("#ffb08a"), intensity=9.0,
            castsShadow=True, contactShadow=False, shadowStrength=1.0, softness=0.25)
    # (contactShadow off: at a 4.5 degree sun the screen-space contact march dithers the whole lit flat)
    s.light("foot", "point", position=[MONO[0] + 0.6, 0.35, MONO[1] + 1.2], color=hexrgb("#ffe6cc"),
            intensity=0.0, range=26.0, radius=0.6, castsShadow=False, volumetric=0.0)

    # ---- meteors: one per note, falling at the pitch's place across the sky, all from one radiant (upper right)
    s.particles("meteors", capacity=600, seed=21, shape="sphere", position=[-1000.0, 400.0, -1200.0],
                extent=[0.0, 0.0, 0.0], direction=[-0.78, -0.6, 0.08], spawnRate=0.0, lifetimeMin=0.8,
                lifetimeMax=1.2, spread=0.035, speedMin=200.0, speedMax=260.0, gravity=[0, 0, 0], drag=0.0,
                sizeStart=11.0, sizeEnd=3.0, colorStart=hexrgb("#f2fff4") + [1.0],
                colorEnd=hexrgb("#ffd2a8") + [0.0], emissive=24.0, blend="additive", trailEnabled=True,
                trailLength=30, trailStride=1, trailWidth=0.7, trailTaper=0.0, trailFade=0.0,
                trailTint=hexrgb("#ffb27a"))
    # ...and its ignition: a flare the instant it enters (the impact frame), gone in a fifth of a second
    s.particles("ignition", capacity=60, seed=22, shape="sphere", position=[-1000.0, 400.0, -1200.0],
                extent=[0.0, 0.0, 0.0], direction=[-0.78, -0.6, 0.08], spawnRate=0.0, lifetimeMin=0.12,
                lifetimeMax=0.2, spread=0.0, speedMin=200.0, speedMax=200.0, gravity=[0, 0, 0], drag=0.0,
                sizeStart=22.0, sizeEnd=5.0, colorStart=hexrgb("#f6fff8") + [1.0],
                colorEnd=hexrgb("#ffd2a8") + [0.0], emissive=14.0, blend="additive")
    # ---- blowing salt (hat): grains streaming low across the flat
    s.particles("salt", capacity=3000, seed=23, shape="box", position=[-2.0, 0.12, -10.0], extent=[16.0, 0.1, 9.0],
                direction=[1, 0.03, 0.25], spawnRate=0.0, lifetimeMin=0.22, lifetimeMax=0.55, spread=0.1,
                speedMin=9.0, speedMax=13.0, gravity=[0, -0.2, 0], drag=0.2, turbulence=0.3, turbulenceScale=0.6,
                sizeStart=0.009, sizeEnd=0.004, colorStart=hexrgb("#fff8ee") + [0.7],
                colorEnd=hexrgb("#fff8ee") + [0.0], emissive=0.7, blend="additive", velocityStretch=1.0,
                stretchMax=0.35)

    # ---- glints: salt crystals catching the low sun for an instant (hats), across the near crust
    s.particles("glints", capacity=4000, seed=27, shape="box", position=[0.0, 0.04, -14.0], extent=[22.0, 0.02, 14.0],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.06, lifetimeMax=0.18, spread=1.0, speedMin=0.0,
                speedMax=0.01, gravity=[0, 0, 0], drag=0.0, sizeStart=0.05, sizeEnd=0.0,
                colorStart=hexrgb("#fff6e8") + [1.0], colorEnd=hexrgb("#ffd8b0") + [0.0], emissive=26.0,
                blend="additive")

    # ---- effects: the horizon's heat shimmer (bass), the ring of light (kick)
    # (its strength is metres at the column, projected from the middle of the air a ray crosses: a column
    #  kilometres wide moves the horizon by a fraction of a pixel; one 400 m round the view moves it by several)
    s.effect("shimmer", "heatShimmer", ("world",), parameters={
        "strength": 0.3, "radius": 400.0, "height": 30.0, "scale": 4.0, "riseSpeed": 1.5, "chroma": 0.0,
        "churn": 0.7, "edgeSoftness": 0.5, "heightFalloff": 1.4, "thicknessRef": 300.0, "fadeDistance": 0.0,
        "offsetX": CAM[0], "offsetY": 0.0, "offsetZ": CAM[2] - 380.0})
    s.effect("ring", "groundPulse", ("world",),
             trigger={"source": "signal", "name": signals.S("kick"), "threshold": 0.25},
             parameters={"source": {"kind": "world", "position": [MONO[0], 0.0, MONO[1]]},
                         "propagation": {"kind": "radial", "speed": 60.0, "range": 220.0, "frontWidth": 3.5,
                                         "trailLength": 18.0, "falloff": 1.6, "ringCount": 1.0,
                                         "verticalExtent": 1.0},
                         "appearance": {"color": hexrgb("#ffe2c8"), "intensity": 0.6, "edgeColor": hexrgb("#fff4e8"),
                                        "edgeIntensity": 1.6, "width": 1.0, "rainbow": False},
                         "response": {"ground": 1.0, "foliage": 0.0, "surface": 1.0, "emissive": 0.0},
                         "sparkle": {"enabled": False}},
             timing={"fadeIn": 0.0, "fadeOut": 0.5})

    # ---- camera: ultra-wide, low, still; the horizon on the lower third, the monolith on the left third
    s.camera["fov"] = 62.0
    s.drift_camera(centre=CAM, target=(2.5, 15.6, -60.0), period=90.0, amp=(0.12, 0.02, 0.08),
                   tamp=(0.05, 0.02, 0.0))

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # the sky: the palette rests at rose (1). A consonant chord warms it toward gold (0); dissonance darkens it
    # through violet (2) to indigo (3). A bright sound brightens the whole sky; held sound lifts the sun.
    s.map(M("dissonance", [("tension", 1.0)], "mean", 0.0, 2.0))
    s.map(M("consonance", [("polyphony", 1.0), ("tension", 1.0, True)], "min"))
    s.map(M("glow", [("sustain", 1.0), ("held", 0.5)], "max"))
    s.route(R("visual.dissonance", "palette/position", 2.0, attackMs=1200, decayMs=4000),
            R("visual.consonance", "palette/position", -1.0, attackMs=1200, decayMs=4000),
            R("brightness", "palette/value", 0.25, attackMs=400, decayMs=1600),
            R("visual.glow", "env/sky/sunIntensity", 8.0, **SLOW),
            R("visual.glow", "env/sky/sunGlow", 0.12, **SLOW))
    # melodic: the meteor's birthplace follows the latest note across the sky; velocity sets its brightness
    s.map(M("skyX", [("lastPitch", 1.0)], "mean", -0.3 / 0.4, 1.0 / 0.4))
    for sysname in ("meteors", "ignition"):
        s.route(R("visual.skyX", "particles/%s/position" % sysname, 2000.0, comp=0, attackMs=0, decayMs=0),
                R("visual.skyX", "particles/%s/position" % sysname, 260.0, comp=1, attackMs=0, decayMs=0),
                # exactly one per note-on: a burst is a whole count each frame, so binarise the event
                R("noteOn", "particles/%s/burst" % sysname, 1.0, threshold="binary", thresholdLevel=0.01),
                R("lastVelocity", "particles/%s/emissive" % sysname, 12.0, **FAST))
    # bass: the horizon shimmers and the haze thickens
    s.route(R("bass", "fx/shimmer/strength", 1.6, attackMs=40, decayMs=1200),
            R("bass", "scene/volumeDensity", 0.00018, attackMs=50, decayMs=1800))
    # hat: salt grains blowing
    s.route(R("hat", "particles/glints/burst", 160.0, attackMs=0, decayMs=30),
            R("hat", "particles/salt/burst", 50.0, attackMs=0, decayMs=40),
            R("hatRate", "particles/salt/spawnRate", 240.0, **MEDIUM))
    # snare: the monolith's outline flares (the kick's ring fires on its own trigger)
    for c in range(3):
        s.route(R("snare", "material/sfMonolith/op/%d/constant/constant" % OP_RIM, 9.0, comp=c, attackMs=0,
                  decayMs=260))
    # kick: the impact at the monolith's foot is instant (a flash of light on the salt); the ring then runs out
    s.route(R("kick", "lights/foot/intensity", 900.0, attackMs=0, decayMs=220),
            R("kick", "post/lens/chromaticAberration", 0.012, attackMs=0, decayMs=120),
            R("visual.glow", "post/bloom/intensity", 0.05, **SLOW))

    s.params_({
        "post/bloom/intensity": 0.07, "post/bloom/threshold": 1.2, "post/halation/enabled": True,
        "post/halation/intensity": 0.06, "post/halation/warmth": 0.7,
        "post/output/vignette": 0.3, "post/output/grain": 0.018, "post/grade/contrast": 1.05,
        "post/lens/chromaticAberration": 0.003,
        "camera/lens/focalLength": 18.0, "camera/exposure/compensation": 0.0,
        # PBR Neutral keeps the dusk's authored hues (AgX greys a deep blue zenith)
        "post/tonemap/operator": 3, "post/tonemap/chroma-retention": 0.3,
    })
    # ---- the evaluator's screen regions, projected through the camera at t = 0
    hw2, hh2 = MONO_SIZE[0] * 0.5, MONO_SIZE[1]
    s.region_points("monolith", [[MONO[0] - hw2, 0.0, MONO[1]], [MONO[0] + hw2, 0.0, MONO[1]],
                                 [MONO[0] - hw2, hh2, MONO[1]], [MONO[0] + hw2, hh2, MONO[1]]], pad=0.01)
    s.region("sky", box=[0.12, 0.04, 0.95, 0.5])
    s.region("ground", box=[0.0, 0.7, 1.0, 1.0])
    s.region("horizon", box=[0.0, 0.6, 1.0, 0.7])
    return s
