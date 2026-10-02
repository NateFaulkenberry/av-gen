"""FERROFLUID CROWN (abstract). A black magnetic liquid listens: each note pulls a spike from its surface, and harmony
shapes the crown.

Composition (SCENE-CATALOG.md #5): a macro view of a shallow lacquered dish in studio darkness, after Sachiko
Kodama's ferrofluid sculptures (*Protrude, Flow*, 2001; the *Morpho Towers*). The liquid's form is read only through
reflections: a long warm softbox on the left, a thin cool strip behind on the right. Colour exists only as reflected
light. The central spiral tower is the subject, on the right third; twelve spikes round it rise and melt with the
notes; a field of small spikes breathes with the bass.
"""
import math

from .. import kit, signals
from ..kit import (R, M, hexrgb, scale3, sd_union, sd_smooth, sd_diff, sd_inter, sd_move, sd_rot, sd_cyl, sd_cone,
                   sd_box, sd_rbox, sd_torus, sd_repeat, sd_polar, sd_twist, sd_noise, sd_wave, sd_voronoi, SLOW, MEDIUM, FAST, HIT,
                   SNAP)

ID = "ferrofluid-crown"
TITLE = "Ferrofluid Crown"

BLACK = "#020203"
GRAPHITE = "#3a3c40"
WARM = "#ffae5a"
COOL = "#7fb6ff"

DESIGN = {
    "category": "abstract",
    "thesis": "A black magnetic liquid listens: each note pulls a spike from its surface, and harmony shapes the crown.",
    "composition": {
        "background": "studio darkness",
        "midground": "a shallow lacquered dish of ferrofluid, a field of small spikes over its surface",
        "foreground": "the dish's near rim, soft with depth of field",
        "focal": "the central spiral tower and the tallest spike, on the right third",
        "secondary": ["the ring of twelve note spikes", "droplets thrown off on hits"],
        "atmosphere": "none: clean air",
        "post": "macro depth of field, a gentle bloom on speculars, a vignette, a near-monochrome grade",
        "camera": "macro, 15 degrees above the dish, a slow 60 s breathing drift",
    },
    "palette": {"dominant": BLACK, "secondary": GRAPHITE, "accent": WARM, "highlight": "#ffffff",
                "background_value": "black",
                "saturation": "none in the material; a warm key and a cool rim exist only as reflections"},
    "motion": {
        "very_slow": ["the camera's drift", "the tower turning"],
        "medium": ["the surface's swell and the spike field (bass)"],
        "fast": ["note spikes rising and melting"],
        "extremely_fast": ["surface shimmer (hat)", "the ripple ring (kick)", "droplets (snare)"],
    },
    "vocabulary": [
        ["melodic", "notes.noteOn + notes.lastPitch", "a spike rises from the liquid at its pitch's place round the "
         "crown (low notes on the left, high on the right)"],
        ["sustained", "notes.held", "a held note's spike stays up; released, it melts back"],
        ["chord", "notes.polyphony", "the crown forms: the spike field rises round the tower"],
        ["bass", "response.bass", "the magnetic field: the surface heaves, the small spikes lengthen together"],
        ["kick", "response.kick", "a ripple ring crosses the dish"],
        ["snare", "response.snare", "spike tips throw black droplets"],
        ["hat", "response.hat", "fine shimmer on the surface"],
        ["tension", "notes.tension", "dissonance makes the surface restless (noise)"],
        ["brightness", "sonic.brightness", "the warm key cools toward white"],
    ],
    "tier": "medium: two compiled SDF objects, no fog",
}

N_SPIKES = 12
RING_R = 1.18
TOWER = (0.0, 0.0, 0.0)


# Rings of small spikes round the tower, graded (tall inside, short outside) as a magnet's field grades them:
# (radius, count, cone radius, cone height). The note ring sits between the second and third.
FIELD_RINGS = [(0.52, 13, 0.07, 0.34), (0.8, 20, 0.065, 0.27), (1.55, 34, 0.055, 0.2), (1.82, 41, 0.05, 0.14)]


def fluid():
    """The liquid: a pool, concentric rings of small spikes graded by the field (never a grid), twelve note spikes
    round the tower, and the tower itself."""
    pool = sd_move((0.0, -0.06, 0.0), sd_cyl(2.15, 0.12))
    rings = []
    for j, (r, n, cr, ch) in enumerate(FIELD_RINGS):
        cone = sd_move((r, 0.0, 0.0), sd_cone(cr, ch, name="ringCone%d" % j))
        rings.append(sd_move((0.0, ch * 0.5 - 0.02, 0.0), sd_rot((0, 9.0 * j, 0), sd_polar(n, cone)),
                             name="ringLift%d" % j))
    field = sd_union(*rings)
    # the twelve note spikes: each hides below the surface (y -0.62) until its note pulls it up
    spikes = []
    for k in range(N_SPIKES):
        a = math.radians(200.0 - k * (220.0 / (N_SPIKES - 1)))
        spikes.append(sd_move((RING_R * math.cos(a), -0.62, RING_R * math.sin(a)),
                              sd_cone(0.12, 1.0), name="spike%d" % k))
    ring = sd_union(sd_union(*spikes[:6]), sd_union(*spikes[6:]))
    # the central tower: a cone with a spiral of fins round its lower two thirds, the liquid climbing it
    fins = sd_move((0.0, -0.18, 0.0), sd_twist(2.4, sd_polar(6, sd_move((0.13, 0.0, 0.0),
                                                                        sd_box((0.05, 0.34, 0.016))))))
    tower = sd_move((0.0, 0.5, 0.0), sd_smooth(0.05, sd_cone(0.26, 1.0), fins), name="tower")
    body = sd_smooth(0.08, pool, field, ring, tower)
    body = sd_wave(0.0, 2.2, body, axis=(1.0, 0.0, 0.3), speed=1.1, name="swell")
    return sd_noise(0.0, 9.0, body, speed=1.8, seed=4, name="shimmer")


def dish():
    outer = sd_rbox((2.55, 0.16, 2.55), 0.12)
    return sd_diff(sd_inter(sd_move((0.0, -0.05, 0.0), sd_cyl(2.55, 0.32)), sd_move((0.0, 0.0, 0.0), outer)),
                   sd_move((0.0, 0.12, 0.0), sd_cyl(2.22, 0.4)))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.0, "background": hexrgb("#000000"), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        # the studio as an environment: almost black, with a warm glow high on one side for the glossy reflections
        "sky": {"enabled": True, "zenithColor": scale3(hexrgb("#2a2622"), 0.12),
                "horizonColor": scale3(hexrgb("#141516"), 0.06), "groundColor": [0.0, 0.0, 0.0], "haze": 0.0,
                "sunColor": hexrgb(WARM), "sunIntensity": 2.5, "sunSize": 0.35, "sunGlow": 0.5, "intensity": 1.0,
                "background": False, "useKeyLight": False, "sunDirection": [-0.7, 0.45, 0.35]},
    }
    s.composition = {"focalPoints": [{"name": "tower", "position": [0.0, 0.7, 0.0], "radius": 0.9, "weight": 1.0}]}
    s.sdf("fluid", fluid(), (-2.3, -0.3, -2.3), (2.3, 1.4, 2.3),
          surfaces=[{"color": scale3(hexrgb("#050506"), 1.0)}],
          material={"baseColor": hexrgb("#030304"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                    "roughness": 0.07, "metallic": 0.35},
          look={"aoStrength": 0.35, "aoDistance": 0.25}, max_steps=160, epsilon=0.0006, step_scale=0.7,
          max_distance=20.0)
    s.sdf("dish", dish(), (-2.7, -0.45, -2.7), (2.7, 0.3, 2.7),
          material={"baseColor": hexrgb("#0a0a0b"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                    "roughness": 0.22, "metallic": 0.0},
          look={"aoStrength": 0.5, "aoDistance": 0.2}, max_steps=96, epsilon=0.0008, max_distance=20.0)
    # ---- light: a long warm softbox high on the left, a thin cool strip behind on the right, a dim top fill
    s.light("softbox", "rect", position=[-3.4, 2.6, 1.6], direction=[0.72, -0.52, -0.38], up=[0, 1, 0],
            color=hexrgb(WARM), intensity=55.0, width=3.6, height=1.0, castsShadow=False)
    s.light("strip", "rect", position=[2.6, 1.6, -3.2], direction=[-0.5, -0.28, 0.82], up=[0, 1, 0],
            color=hexrgb(COOL), intensity=40.0, width=0.25, height=3.2, castsShadow=False)
    s.light("top", "rect", position=[0.0, 4.0, 0.0], direction=[0.0, -1.0, 0.0], up=[0, 0, 1],
            color=hexrgb("#d8d4cc"), intensity=6.0, width=2.0, height=2.0, castsShadow=False)

    # ---- droplets (snare): black beads flung from the spike tips
    s.particles("droplets", capacity=1500, seed=5, shape="disc", position=[0.0, 0.55, 0.0], extent=[1.25, 0.0, 1.25],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.5, lifetimeMax=0.9, spread=0.35, speedMin=1.2,
                speedMax=2.6, gravity=[0, -9.8, 0], drag=0.1, sizeStart=0.022, sizeEnd=0.012,
                colorStart=hexrgb("#0b0b0c") + [1.0], colorEnd=hexrgb("#0b0b0c") + [1.0], emissive=0.0,
                blend="alpha", collision="kill", collisionHeight=0.0)

    # ---- the kick's ripple ring across the dish (a refracting membrane wave)
    s.effect("ripple", "ripple", ("world",), trigger={"source": "signal", "name": signals.S("kick"), "threshold": 0.25},
             parameters={"amplitude": 0.22, "radius": 2.2, "wavelength": 0.32, "speed": 2.6, "plane": 1,
                         "crestColor": hexrgb("#d8e4ff"), "crestEmission": 0.15, "chroma": 0.06, "radialDecay": 1.2,
                         "ageDecay": 2.4, "duration": 1.1, "maxConcurrent": 3, "offsetX": 0.0, "offsetY": 0.05,
                         "offsetZ": 0.0})

    # ---- camera: macro, low over the rim, the tower on the right third
    s.camera["fov"] = 30.0
    s.drift_camera(centre=(-1.5, 1.7, 6.8), target=(-0.75, 0.42, 0.0), period=60.0, amp=(0.35, 0.08, 0.2),
                   tamp=(0.05, 0.03, 0.0))

    # ---- the instrument ---------------------------------------------------------------------------------------------
    centres = [0.32 + 0.032 * k for k in range(N_SPIKES)]
    s.places("sp", "lastPitch", centres, 0.034, event="noteOn")
    s.map(M("anyHeld", [("active", 1.0)], "mean"))
    s.map3(*[M("spHeld%d" % k, [("visual.sp%d" % k, 1.0), ("visual.anyHeld", 1.0)], "min") for k in range(N_SPIKES)])
    s.map(M("crown", [("polyphony", 1.0), ("chord", 0.6)], "max"))
    s.map(M("unrest", [("tension", 1.0), ("roughness", 0.6)], "max"))

    # melodic: a spike rises at the note's place (fast, with a little overshoot) and melts when the note ends
    for k in range(N_SPIKES):
        s.route(R("visual.spHeld%d" % k, "sdf/fluid/node/spike%d/translation" % k, 1.05, comp=1, attackMs=40,
                  decayMs=700, springHz=3.2, springDamping=0.45))
    # chord: the crown forms (the spike field rises round the tower)
    for j, (r, n, cr, ch) in enumerate(FIELD_RINGS):
        s.route(R("visual.crown", "sdf/fluid/node/ringLift%d/translation" % j, 0.06 + 0.04 * (3 - j), comp=1,
                  attackMs=300 + 120 * j, decayMs=1500))
    # bass: the field strength (the small spikes lengthen together) and a heave
    for j, (r, n, cr, ch) in enumerate(FIELD_RINGS):
        s.route(R("bass", "sdf/fluid/node/ringCone%d/height" % j, ch * 0.9, attackMs=60 + 30 * j, decayMs=500))
    s.route(R("bass", "sdf/fluid/node/swell/amount", 0.03, attackMs=80, decayMs=700))
    # hat: shimmer
    s.route(R("hat", "sdf/fluid/node/shimmer/amount", 0.008, attackMs=0, decayMs=90))
    # snare: droplets
    s.route(R("snare", "particles/droplets/burst", 90.0, attackMs=0, decayMs=30))
    # tension: a restless surface
    s.route(R("visual.unrest", "sdf/fluid/node/shimmer/amount", 0.006, **MEDIUM))
    # timbre: a bright sound cools the key toward white
    s.route(R("brightness", "lights/softbox/color", -0.15, comp=0, **MEDIUM),
            R("brightness", "lights/softbox/color", 0.25, comp=2, **MEDIUM))
    s.route(R("kick", "post/lens/chromaticAberration", 0.03, attackMs=0, decayMs=120),
            R("visual.crown", "post/bloom/intensity", 0.08, **SLOW))

    s.params_({
        "post/bloom/intensity": 0.22, "post/bloom/threshold": 1.1, "post/output/vignette": 0.6,
        "post/output/grain": 0.012, "post/grade/contrast": 1.15, "post/grade/saturation": 0.85,
        "post/lens/chromaticAberration": 0.01,
        "camera/lens/focalLength": 58.0, "camera/exposure/compensation": 0.4,
        "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/aperture": 4.0,
        "camera/focus/mode": 0, "camera/lens/focusDistance": 6.9,
    })
    return s
