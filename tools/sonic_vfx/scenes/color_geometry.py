"""4. INFINITE COLOR GEOMETRY: CHROMATIC CORRIDOR (04-brief-abstract-direction.md, direction 4; ABSTRACT-PLAN.md section 4).

Space itself is the subject: a procession of monumental portal frames that never ends, each a step further round the
colour wheel, in coloured haze, toward a blinding light. Pure planes of saturated colour, no texture, no lines at rest.

Grammar: one module (a polygonal portal frame and a floating slab) repeated forever along one axis; COLOUR LIVES IN THE
SPACE, not on the objects: the hue is a function of the distance from the viewer (warm near, cool far), so the frames
travel through the colour as they come. The corridor twists about its axis, anchored at the viewer.

Construction (one compiled SDF, corridor along local +Y, turned so +Y runs into the screen):
  twist(about Y, anchored at the viewer) . translate(the travel: a sawtooth of one spacing) . repeat(Y, spacing)
    . polarRepeat(n) of a slab = the portal's polygon
The viewer is still; the corridor flows toward it (a seamless loop of one spacing). The spacing and the travel are
scaled by the same bass factor, so the corridor breathes like an accordion round the viewer without a jump.
A material program colours each fragment by its depth (`localPosition.y`) through an OKLCH hue rotation of vermilion,
with a per-face shade from the normal; the phase is routable (the music recolours the space).
"""
import math

from .. import kit
from ..kit import (R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW, sd_union, sd_move, sd_rot, sd_box,
                   sd_rbox, sd_repeat, sd_polar, sd_twist)

ID = "color-geometry"
TITLE = "Infinite Color Geometry"

VERMILION = "#ff3b1f"
MAGENTA = "#ff2d8a"
VIOLET = "#8a2be2"
COBALT = "#1f4fff"
TEAL = "#00c2b8"
LIGHT = "#fff3d4"
LUMEN = "#fff0e2"      # the luminous haze the far frames dissolve into (Turrell's light, not a dark void)

SPACING = 9.0          # metres between portals
APOTHEM = 4.2          # the portal opening's inradius
FRAME = 7.0            # the frame's width (radial): monumental, a wall of colour with a hole in it
DEPTH = 0.8            # the frame's thickness along the corridor (half)
SIDES = 4              # the portal's polygon order at rest
HUE_PERIOD = 90.0      # metres for a full turn of the colour wheel
TRAVEL_PERIOD = 2.6    # seconds for the corridor to advance one spacing (3.5 m/s)
END_Z = 112.0          # where the corridor's march stops and the light stands (down the axis a ray runs this far)
MAX_STEPS = 72

DESIGN = {
    "category": "colour field",
    "thesis": "Chromatic Corridor: monumental portals of pure colour flow toward you forever through coloured haze, "
              "each a step further round the colour wheel, toward a blinding light. The music breathes the corridor's "
              "spacing, winds its twist and recolours the space itself.",
    "composition": {
        "background": "the light at the end, white-gold, blooming through the last frames",
        "midground": "the procession of portals receding and twisting, vermilion to violet to cobalt to teal",
        "foreground": "the nearest portal's edge sweeping past the frame",
        "focal": "the vanishing point, slightly off-centre",
        "secondary": ["the floating slabs", "the haze between the frames"],
        "atmosphere": "a thin luminous haze; the light at the end",
        "post": "bloom; a radial zoom on the kick",
        "camera": "a long lens (85 mm) down the corridor's axis from just inside it: the frames stacked into a spiral "
                  "of the whole colour wheel round the light; the corridor flows toward it",
    },
    "palette": {"dominant": MAGENTA, "secondary": VIOLET, "accent": TEAL, "highlight": LIGHT,
                "background_value": "mid (colour)", "saturation": "full saturation, flat; colour by depth"},
    "motion": {
        "very_slow": ["the colour's drift", "the twist's wind"],
        "medium": ["the flow toward the viewer", "the accordion breath"],
        "fast": ["colour jumps on the snare"],
        "extremely_fast": ["the kick's zoom punch"],
    },
    "vocabulary": [
        ["bass", "response.bass", "STRUCTURE: the corridor's spacing breathes like an accordion round the viewer"],
        ["kick", "response.kick", "a zoom punch toward the light; the light flares"],
        ["snare", "response.snare", "the whole colour sequence jumps a step round the wheel and stays there"],
        ["mids", "audio.mid", "the corridor winds its twist"],
        ["highs", "audio.treble", "a fine light traces every edge"],
        ["brightness", "sonic.brightness.slow", "the colour range cools as the sound brightens"],
        ["flux", "response.flux", "the corridor wrings tighter"],
        ["beat", "beat.pulse", "the corridor surges forward on each beat"],
        ["intensity", "response.intensity", "the light at the end grows; the bloom opens"],
        ["note", "notes.lastPitch", "each note recolours the space (the pitch sets the hue)"],
        ["chord", "notes.polyphony", "STRUCTURE: the portals' polygon order: a triangle, square, hexagon..."],
        ["mod wheel", "control.modwheel", "the frames thicken"],
        ["silence", "(no input)", "the corridor flows slowly toward the light"],
    ],
    "tier": "medium: one compiled SDF corridor (72 steps to 112 m), analytic haze, an emissive light, bloom",
}


def corridor():
    # each slab reaches the outer polygon's corners: polarRepeat folds space into one wedge per side, so the
    # neighbouring slabs meet in a clean mitre on the wedge boundary
    half_edge = (APOTHEM + FRAME) * math.tan(math.pi / SIDES)
    slab = sd_move((APOTHEM + FRAME * 0.5, 0.0, 0.0), sd_box((FRAME * 0.5, DEPTH, half_edge), name="slab"))
    portal = sd_polar(SIDES, slab, name="polygon")
    rep = sd_repeat((0.0, SPACING, 0.0), portal, count=0, name="accordion")
    travel = sd_move((0.0, 0.0, 0.0), rep, name="travel")
    return sd_twist(0.026, travel, name="twist")


def colour_program():
    return {
        "name": "cgSpace",
        "ops": [
            {"kind": "input", "dst": 0, "input": "localPosition"},
            {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},     # depth along the axis
            {"kind": "constant", "dst": 2, "constant": [-1.0 / HUE_PERIOD] * 4},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},                              # hue turns
            {"kind": "constant", "dst": 3, "constant": hexrgb(VERMILION) + [1.0]},
            {"kind": "hueShift", "dst": 4, "srcA": 3, "srcB": 1, "value": 0.0},              # op 6: the phase
            {"kind": "saturate", "dst": 4, "srcA": 4, "value": 1.6},                          # op 7: full chroma
            {"kind": "input", "dst": 5, "input": "normal"},
            {"kind": "gradient", "dst": 6, "srcA": 5, "constant": [0.35, 0.8, -0.45, 0.62], "value": 0.42},
            {"kind": "multiply", "dst": 7, "srcA": 4, "srcB": 6},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 0.95, "opacity": -1,
    }


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 4)."""
    N = "sdf/corridor/node/%s/"
    HUE = "material/cgSpace/op/6/hueShift/value"
    # ---- BASS: the accordion -- the spacing and the travel are scaled by the same factor (1 + 0.28 bass), so the
    # corridor breathes round the viewer without a jump; the haze thickens a little
    breath = dict(op="multiply", gain=0.28, offset=1.0, attackMs=40, decayMs=420)
    s.route(R("bass", N % "accordion" + "size", 1.0, comp=1, **breath),
            R("bass", N % "travel" + "translation", 1.0, comp=1, **breath),
            R("bass", "scene/volumeDensity", 0.008, attackMs=60, decayMs=600))
    # ---- KICK: a zoom punch toward the light; the light flares
    s.route(R("kick", "post/radial/amount", 0.1, attackMs=0, decayMs=200),
            R("kick", "camera/lens/focalLength", 10.0, attackMs=0, decayMs=240),
            R("kick", "lights/end/intensity", 30000.0, attackMs=0, decayMs=260),
            R("kick", "procedural/endLight/material/emissive", 50.0, attackMs=0, decayMs=260))
    # ---- SNARE: the whole colour sequence jumps a step round the wheel (an integrated hit: it stays where it lands)
    s.route(R("snare", HUE, 1.1, attackMs=0, decayMs=180, integrate=True))
    # ---- MIDS: the corridor winds and unwinds its twist
    s.route(R("audio.mid", N % "twist" + "amount", 0.03, attackMs=120, decayMs=900))
    # ---- HIGHS: a fine light traces every edge
    s.route(R("audio.treble", "sdf/corridor/look/edge/intensity", 2.0, attackMs=20, decayMs=200),
            R("hat", "sdf/corridor/look/edge/intensity", 1.2, attackMs=0, decayMs=90))
    # ---- CENTROID: the colour range cools as the sound brightens
    s.route(R("brightnessSlow", HUE, 0.3, **SLOW))
    # ---- FLUX: the corridor wrings tighter (more twist on change)
    s.route(R("flux", N % "twist" + "amount", 0.02, attackMs=60, decayMs=500))
    # ---- TEMPO: the corridor surges a little on each beat
    s.route(R("beat", N % "travel" + "translation", -1.4, comp=1, attackMs=0, decayMs=260))
    # ---- INTENSITY: the light at the end grows; the bloom opens
    s.route(R("intensity", "lights/end/intensity", 22000.0, **VERY_SLOW),
            R("intensity", "procedural/endLight/material/emissive", 30.0, **VERY_SLOW),
            R("intensity", "post/bloom/intensity", 0.3, **VERY_SLOW))
    # ---- MIDI: each note recolours the space (the pitch sets the hue's phase); a chord sets the portal's polygon
    # order (STRUCTURE: 3 notes a triangle, 4 a square, 6 a hexagon; a single note keeps the square); velocity flares
    # the light; held notes glow the haze
    s.route(R("lastPitch", HUE, 0.9, springHz=1.5, springDamping=0.8),
            R("polyphony", N % "polygon" + "count", -1.0, threshold="binary", thresholdLevel=0.3),
            R("polyphony", N % "polygon" + "count", 1.0, gain=8.0, offset=-3.0, clampEnabled=True, clampMin=0.0,
              clampMax=5.0),
            R("noteEnv", "lights/end/intensity", 26000.0, depth="lastVelocity", attackMs=0, decayMs=400),
            R("noteEnv", "procedural/endLight/material/emissive", 40.0, depth="lastVelocity", attackMs=0,
              decayMs=400),
            R("held", "scene/volumeDensity", 0.01, **MEDIUM))
    # ---- MOD WHEEL: the frames thicken from slender to monumental
    s.route(R(s.modwheel(), N % "slab" + "size", 1.6, comp=0, attackMs=60, decayMs=60))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.0,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    s.environment = {
        "intensity": 0.0, "background": hexrgb(LUMEN), "fogColor": hexrgb(LUMEN), "volumeDensity": 0.005,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(LUMEN), "horizonColor": hexrgb(LUMEN),
                "groundColor": hexrgb(LUMEN), "haze": 0.5, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.program(colour_program())
    # the corridor: local +Y into the screen (rotation -90 about X), starting just behind the viewer
    rmax = (APOTHEM + FRAME) / math.cos(math.pi / 3.0) + 1.0     # a triangle's corners (the chords reach 3 sides)
    s.sdf("corridor", corridor(), (-rmax, -16.0, -rmax), (rmax, 135.0, rmax),
          surfaces=[{"color": [1.0, 1.0, 1.0], "emission": [1.0, 1.0, 1.0], "edge": [1.0, 1.0, 1.0]}],
          look={"edgeIntensity": 0.0, "edgePixels": 1.4, "edgeThreshold": 0.03, "edgeSoftness": 0.25,
                "edgeColor": hexrgb("#fff1d8")},
          material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.85,
                    "metallic": 0.0, "program": "cgSpace"},
          position=(0.0, 0.0, 0.0), rotation=(-90.0, 0.0, 0.0), max_steps=MAX_STEPS, epsilon=0.0008, step_scale=0.85,
          max_distance=END_Z)
    # THE LIGHT AT THE END: geometry, blinding (the sky's own sun cannot sit at the end of a horizontal corridor, and a
    # point light only lights what it reaches). Down the long lens it is the white square every frame turns round.
    s.proc("endLight", {"kind": "sphere", "radius": 8.0}, position=(0.0, 0.0, -END_Z + 2.0),
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb(LIGHT), "emissiveIntensity": 40.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True})
    # the light at the end, and a magenta fill behind the viewer: they colour the haze
    s.light("end", "point", position=[0.0, 0.0, -118.0], color=hexrgb(LIGHT), intensity=9000.0, range=260.0,
            volumetric=0.7, castsShadow=False)
    s.light("near", "point", position=[0.0, 1.0, 14.0], color=hexrgb(MAGENTA), intensity=300.0, range=60.0,
            volumetric=0.4, castsShadow=False)
    s.light("fill", "directional", direction=[-0.3, -0.6, -0.7], color=hexrgb("#ffd0e8"), intensity=0.3,
            castsShadow=False)

    # the flow: the corridor advances one spacing per TRAVEL_PERIOD, a seamless sawtooth
    s.track("sdf/corridor/node/travel/translation", [
        {"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
        {"time": TRAVEL_PERIOD, "value": [0.0, -SPACING, 0.0], "interp": "linear"}], loop=TRAVEL_PERIOD)

    # A LONG LENS down the axis: the frames 20 to 110 m away stack into one spiral of the whole colour wheel round the
    # light (the first look, a 24 mm lens at the mouth, was one pink frame with a hole in it). Tonemap 4 (clamp) keeps
    # the colour flat and pure.
    s.params_({"camera/lens/focalLength": 85.0, "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.9,
               "post/bloom/emissionWeight": 0.8, "post/output/vignette": 0.35, "post/output/grain": 0.012,
               "post/tonemap/operator": 4, "scene/volumeSteps": 24})
    cam = [0.8, -0.5, 4.0]
    tgt = [-1.5, 0.8, -60.0]       # the vanishing point just off the axis, up and to the left of centre
    s.camera = {"mode": 1, "position": cam, "target": tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(cam, tgt, period=40.0, amp=(0.7, 0.45, 0.0), tamp=(2.0, 1.4, 0.0))
    instrument(s)
    s.region("vanishing", box=[0.4, 0.35, 0.6, 0.6])
    s.region("near", box=[0.0, 0.0, 1.0, 1.0])
    return s
