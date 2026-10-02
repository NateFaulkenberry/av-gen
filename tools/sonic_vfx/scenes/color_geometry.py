"""4. INFINITE COLOR GEOMETRY: CHROMATIC CORRIDOR (04-brief-abstract-direction.md, direction 4; ABSTRACT-PLAN.md section 4).

Space itself is the subject: a procession of monumental portal frames that never ends, each a step further round the
colour wheel, in coloured haze, toward a blinding light. Pure planes of saturated colour, no texture, no lines at rest.

Grammar: one module (a polygonal portal frame and a floating slab) repeated forever along one axis; COLOUR LIVES IN THE
SPACE, not on the objects: the hue is a function of the distance from the viewer (warm near, cool far), so the frames
travel through the colour as they come. The corridor twists about its axis, anchored at the viewer.

Construction (one compiled SDF, corridor along local +Y, turned so +Y runs into the screen):
  twist(about Y, anchored at the viewer) . translate(the travel: a sawtooth of one spacing) . repeat(Y, spacing)
    . union( polarRepeat(n) of a slab = the portal's polygon,  a tilted card = the floating slab )
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
HAZE = "#3a0f3f"

SPACING = 9.0          # metres between portals
APOTHEM = 4.2          # the portal opening's inradius
FRAME = 1.1            # the frame's width (radial)
DEPTH = 0.55           # the frame's thickness along the corridor (half)
SIDES = 4              # the portal's polygon order at rest
HUE_PERIOD = 150.0     # metres for a full turn of the colour wheel
TRAVEL_PERIOD = 2.6    # seconds for the corridor to advance one spacing (3.5 m/s)

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
        "atmosphere": "coloured haze lit by the light at the end",
        "post": "bloom; a radial zoom on the kick",
        "camera": "still, slightly off-axis; the corridor flows toward it",
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
        ["bass", "response.bass", "the corridor's spacing breathes like an accordion"],
        ["kick", "response.kick", "a zoom punch toward the light; the frames flash"],
        ["snare", "response.snare", "the whole colour sequence jumps a step round the wheel"],
        ["mids", "audio.mid", "the corridor winds its twist"],
        ["highs", "audio.treble", "fine light traces the frames' edges"],
        ["chord", "notes.polyphony", "the portal's polygon order (square, hexagon, octagon)"],
        ["note", "notes.lastPitch", "each note recolours the space"],
        ["silence", "(no input)", "the corridor flows slowly; the colours drift"],
    ],
    "tier": "medium: one compiled SDF corridor, haze march, bloom",
}


def corridor():
    half_edge = APOTHEM * math.tan(math.pi / SIDES) + FRAME * 0.5
    slab = sd_move((APOTHEM + FRAME * 0.5, 0.0, 0.0), sd_box((FRAME * 0.5, DEPTH, half_edge), name="slab"))
    portal = sd_polar(SIDES, slab, name="polygon")
    # a floating card in each bay, tilted, off-axis: the slabs that cross the space
    card = sd_move((1.9, SPACING * 0.5, -1.4), sd_rot((24.0, 38.0, 12.0), sd_box((1.25, 0.035, 0.7)), name="card"),
                   m=0)
    cell = sd_union(portal, card)
    rep = sd_repeat((0.0, SPACING, 0.0), cell, count=0, name="accordion")
    travel = sd_move((0.0, 0.0, 0.0), rep, name="travel")
    return sd_twist(0.012, travel, name="twist")


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
            {"kind": "input", "dst": 5, "input": "normal"},
            {"kind": "gradient", "dst": 6, "srcA": 5, "constant": [0.35, 0.8, -0.45, 0.62], "value": 0.42},
            {"kind": "multiply", "dst": 7, "srcA": 4, "srcB": 6},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 0.55, "opacity": -1,
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
            R("kick", "camera/lens/focalLength", 3.0, attackMs=0, decayMs=240),
            R("kick", "lights/end/intensity", 30000.0, attackMs=0, decayMs=260))
    # ---- SNARE: the whole colour sequence jumps a step round the wheel (an integrated hit: it stays where it lands)
    s.route(R("snare", HUE, 1.1, attackMs=0, decayMs=180, integrate=True))
    # ---- MIDS: the corridor winds and unwinds its twist
    s.route(R("audio.mid", N % "twist" + "amount", 0.03, attackMs=120, decayMs=900))
    # ---- HIGHS: a fine light traces every edge
    s.route(R("audio.treble", "sdf/corridor/look/edge/intensity", 2.0, attackMs=20, decayMs=200),
            R("hat", "sdf/corridor/look/edge/intensity", 1.2, attackMs=0, decayMs=90))
    # ---- CENTROID: the colour range cools as the sound brightens
    s.route(R("brightnessSlow", HUE, 0.3, **SLOW))
    # ---- FLUX: the floating cards tumble
    s.route(R("flux", N % "card" + "rotation", 50.0, comp=0, attackMs=60, decayMs=500),
            R("flux", N % "card" + "rotation", -35.0, comp=2, attackMs=60, decayMs=500))
    # ---- TEMPO: the corridor surges a little on each beat
    s.route(R("beat", N % "travel" + "translation", -1.4, comp=1, attackMs=0, decayMs=260))
    # ---- INTENSITY: the light at the end grows; the bloom opens
    s.route(R("intensity", "lights/end/intensity", 22000.0, **VERY_SLOW),
            R("intensity", "post/bloom/intensity", 0.3, **VERY_SLOW))
    # ---- MIDI: each note recolours the space (the pitch sets the hue's phase); a chord sets the portal's polygon
    # order (STRUCTURE: 3 notes a triangle, 4 a square, 6 a hexagon; a single note keeps the square); velocity flares
    # the light; held notes glow the haze
    s.route(R("lastPitch", HUE, 0.9, springHz=1.5, springDamping=0.8),
            R("polyphony", N % "polygon" + "count", -1.0, threshold="binary", thresholdLevel=0.3),
            R("polyphony", N % "polygon" + "count", 1.0, gain=8.0, offset=-3.0, clampEnabled=True, clampMin=0.0,
              clampMax=5.0),
            R("noteEnv", "lights/end/intensity", 26000.0, depth="lastVelocity", attackMs=0, decayMs=400),
            R("held", "scene/volumeDensity", 0.01, **MEDIUM))
    # ---- MOD WHEEL: the frames thicken from slender to monumental
    s.route(R(s.modwheel(), N % "slab" + "size", 1.6, comp=0, attackMs=60, decayMs=60))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.0}
    s.environment = {
        "intensity": 0.0, "background": hexrgb(HAZE), "fogColor": hexrgb("#ff6a9a"), "volumeDensity": 0.022,
        "volumeMaxDistance": 110.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#2a0a3a"), "horizonColor": hexrgb("#7a1f6a"),
                "groundColor": hexrgb("#12052a"), "haze": 0.5, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.program(colour_program())
    # the corridor: local +Y into the screen (rotation -90 about X), starting just behind the viewer
    rmax = APOTHEM + FRAME + 2.5
    s.sdf("corridor", corridor(), (-rmax, -4.0, -rmax), (rmax, 135.0, rmax),
          surfaces=[{"color": [1.0, 1.0, 1.0], "emission": [1.0, 1.0, 1.0], "edge": [1.0, 1.0, 1.0]}],
          look={"edgeIntensity": 0.0, "edgePixels": 1.4, "edgeThreshold": 0.03, "edgeSoftness": 0.25,
                "edgeColor": hexrgb("#fff1d8")},
          material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.85,
                    "metallic": 0.0, "program": "cgSpace"},
          position=(0.0, 0.0, 0.0), rotation=(-90.0, 0.0, 0.0), max_steps=96, epsilon=0.0008, step_scale=0.85,
          max_distance=130.0)
    # the light at the end, and a magenta fill behind the viewer: they colour the haze
    s.light("end", "point", position=[0.0, 0.0, -118.0], color=hexrgb(LIGHT), intensity=26000.0, range=260.0,
            volumetric=1.0, castsShadow=False)
    s.light("near", "point", position=[0.0, 1.0, 14.0], color=hexrgb(MAGENTA), intensity=900.0, range=60.0,
            volumetric=0.6, castsShadow=False)
    s.light("fill", "directional", direction=[-0.3, -0.6, -0.7], color=hexrgb("#ffd0e8"), intensity=0.6,
            castsShadow=False)

    # the flow: the corridor advances one spacing per TRAVEL_PERIOD, a seamless sawtooth
    s.track("sdf/corridor/node/travel/translation", [
        {"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
        {"time": TRAVEL_PERIOD, "value": [0.0, -SPACING, 0.0], "interp": "linear"}], loop=TRAVEL_PERIOD)

    s.params_({"camera/lens/focalLength": 24.0, "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.9,
               "post/bloom/emissionWeight": 0.8, "post/output/vignette": 0.35, "post/output/grain": 0.012,
               "post/tonemap/operator": 3, "scene/volumeSteps": 24})
    cam = [0.55, -0.35, 4.0]
    s.camera = {"mode": 1, "position": cam, "target": [-0.6, 0.25, -60.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(cam, [-0.6, 0.25, -60.0], period=40.0, amp=(0.7, 0.45, 0.0), tamp=(2.0, 1.4, 0.0))
    instrument(s)
    s.region("vanishing", box=[0.4, 0.35, 0.6, 0.6])
    s.region("near", box=[0.0, 0.0, 1.0, 1.0])
    return s
