"""6. PARTICLE / VFX WORLD: GALAXY ENGINE (04-brief-abstract-direction.md, direction 6; ABSTRACT-PLAN.md section 6).

Particles are the forms: a vast spiral of luminous points seen at three quarters, its arms rivers of light flowing into
a white-hot core, a halo of dust curtains round it, a stream arcing over the camera; feedback turns the points to silk.

Grammar: almost no geometry. Every large form is made of points: an ARM is a spiral spline that emits particles
along itself (three segments per arm, so the colour runs gold at the core through magenta to blue at the rim), drifting
inward along the arm; the galaxy turns because the splines themselves turn (`spline/<arm>/startAngle`), so it keeps its
shape while its points flow. The core and the halo are emitters of their own.

Structural instruments: the arms' `turns` (how tightly they wind), the number of arms lit, the spawn rates.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "particle-world"
TITLE = "Particle / VFX World"

GROUND = "#05020c"
BLUE = "#2b6bff"
CYAN = "#5fd6ff"
MAGENTA = "#ff3fb4"
GOLD = "#ffc35a"
WHITE = "#fff6e0"

ROT_PERIOD = 80.0      # seconds for the galaxy to turn once
# arm segments: (suffix, inner radius, outer radius, turns, colour at birth, colour at death, jitter, rate)
SEGMENTS = [
    ("in", 0.7, 3.4, 0.42, WHITE, GOLD, 0.22, 2600.0),
    ("mid", 3.4, 7.2, 0.40, GOLD, MAGENTA, 0.42, 2400.0),
    ("out", 7.2, 12.5, 0.42, MAGENTA, BLUE, 0.75, 2200.0),
]

DESIGN = {
    "category": "particles",
    "thesis": "Galaxy Engine: a spiral of light made only of points. Its arms are rivers flowing into a white-hot "
              "core; the bass pulls it in and lets it bloom, the kick explodes it and it reforms, chords grow arms.",
    "composition": {
        "background": "violet-black space with faint feedback smears",
        "midground": "the halo of dust curtains",
        "foreground": "a stream of particles arcing over the camera",
        "focal": "the white-hot core, a third of the way in from the right",
        "secondary": ["the two arms", "the halo", "the stream"],
        "atmosphere": "(none: light only)",
        "post": "bloom, feedback silk, a chromatic fringe",
        "camera": "a slow orbit at 25 degrees",
    },
    "palette": {"dominant": GROUND, "secondary": BLUE, "accent": MAGENTA, "highlight": GOLD,
                "background_value": "black", "saturation": "iridescent: gold core, magenta arms, blue rim"},
    "motion": {
        "very_slow": ["the galaxy's turn", "the camera's orbit"],
        "medium": ["the arms' flow inward", "the breath of the pull"],
        "fast": ["comets on notes"],
        "extremely_fast": ["the kick's explosion"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the pull: the galaxy contracts and blooms"],
        ["kick", "response.kick", "an explosion from the core; the arms reform"],
        ["snare", "response.snare", "a flare runs along an arm"],
        ["hat", "response.hat", "glitter"],
        ["mids", "audio.mid", "the turn speeds up; turbulence"],
        ["chord", "notes.polyphony", "more arms light"],
        ["note", "notes.lastPitch", "a comet launches from the rim at the pitch's angle"],
        ["silence", "(no input)", "the galaxy turns slowly"],
    ],
    "tier": "light: about 60 k particles in nine systems, feedback",
}


def arm_splines(s, arm, phase):
    """One arm as three spiral segments, continuous in angle."""
    angle = phase
    names = []
    for suffix, r0, r1, turns, *_ in SEGMENTS:
        name = "arm%d_%s" % (arm, suffix)
        s.nodes.append({"name": name, "kind": "spline", "spline": {
            "kind": "catmullRom", "generator": "spiral", "count": 24, "radius": r0, "radiusGrowth": r1 - r0,
            "turns": turns, "startAngle": angle, "center": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0],
            "samplesPerSegment": 12}})
        names.append((name, angle))
        angle += 2.0 * math.pi * turns
    return names


EXTRA_ARMS = [(math.pi * 0.5, 0.3), (math.pi * 1.5, 0.45), (math.pi * 0.25, 0.6), (math.pi * 1.25, 0.72)]


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 6)."""
    arms = ["arm%d_%s" % (a, seg[0]) for a in (0, 1) for seg in SEGMENTS]
    # ---- BASS: the pull -- the arms contract toward the core (their splines shrink) and bloom out again
    for name in arms:
        s.route(R("bass", "spline/%s/radius" % name, 1.0, op="multiply", gain=-0.16, offset=1.0, attackMs=40,
                  decayMs=500),
                R("bass", "spline/%s/radiusGrowth" % name, 1.0, op="multiply", gain=-0.16, offset=1.0, attackMs=40,
                  decayMs=500))
    s.route(R("bass", "particles/core/emissive", 6.0, attackMs=20, decayMs=300),
            R("bass", "procedural/heart/material/emissive", 20.0, attackMs=20, decayMs=300))
    # ---- KICK: an explosion from the core, a shock ring on the screen; the arms reform after it
    s.route(R("kick", "particles/blast/burst", 1800.0, threshold="binary", thresholdLevel=0.05),
            R("kick", "post/shock/amount", 40.0, attackMs=0, decayMs=380),
            R("kick", "post/shock/radius", -0.9, attackMs=0, decayMs=0, envelope="linearfall",
              envelopeFallPerSecond=2.2))     # the ring starts small and expands to its resting 1.0 as the hit falls
    # ---- SNARE: a flare runs along one arm
    s.route(R("snare", "particles/p_arm0_mid/burst", 900.0, threshold="binary", thresholdLevel=0.05),
            R("snare", "particles/p_arm0_out/burst", 900.0, threshold="binary", thresholdLevel=0.05),
            R("snare", "post/split/amount", 6.0, attackMs=0, decayMs=160))
    # ---- HIGHS: glitter through the disc
    s.route(R("hat", "particles/glitter/burst", 90.0, threshold="binary", thresholdLevel=0.05),
            R("audio.treble", "particles/glitter/spawnRate", 900.0, attackMs=20, decayMs=250))
    # ---- MIDS: the galaxy turns faster (integrated, on top of its own turn); FLUX: turbulence frays the arms
    for name in arms + ["xarm%d" % k for k in range(len(EXTRA_ARMS))]:
        s.route(R("audio.mid", "spline/%s/startAngle" % name, 0.9, integrate=True, attackMs=150, decayMs=900))
    for name in arms:
        s.route(R("flux", "particles/p_%s/turbulence" % name, 1.4, attackMs=40, decayMs=400))
    # ---- CENTROID: a bright sound pushes the arms' rim toward cyan and the points grow
    for name in arms:
        s.route(R("brightness", "particles/p_%s/size" % name, 0.5, **SLOW))
    s.route(R("brightness", "post/grade/hueShift", -0.06, **SLOW))
    # ---- TEMPO: the core flashes on the beat
    s.route(R("beat", "particles/core/emissive", 4.0, attackMs=0, decayMs=160))
    # ---- INTENSITY: STRUCTURE -- more points, and the halo thickens, as the piece builds
    for name in arms:
        s.route(R("intensity", "particles/p_%s/spawnRate" % name, 1.0, op="multiply", gain=1.2, offset=1.0,
                  **VERY_SLOW))
    s.route(R("intensity", "particles/halo/spawnRate", 2000.0, **VERY_SLOW))
    # ---- MIDI: each note launches a comet from the rim at its pitch's angle (velocity its brightness);
    # STRUCTURE: a chord lights more arms (3 notes a third arm ... 6 notes all six); held notes thicken the halo
    s.route(R("lastPitch", "spline/cometArc/startAngle", 2.0 * math.pi),
            R("note", "particles/comet/burst", 260.0, threshold="binary", thresholdLevel=0.05),
            R("noteEnv", "particles/comet/emissive", 6.0, depth="lastVelocity", attackMs=0, decayMs=500))
    for k, (_, level) in enumerate(EXTRA_ARMS):
        s.route(R("polyphony", "particles/xp%d/spawnRate" % k, 3200.0, threshold="binary", thresholdLevel=level,
                  attackMs=200, decayMs=1200))
    s.route(R("held", "particles/halo/emissive", 2.0, **MEDIUM))
    # ---- MOD WHEEL: the galaxy tilts (every spline's axis leans)
    wheel = s.modwheel()
    for name in arms + ["xarm%d" % k for k in range(len(EXTRA_ARMS))]:
        s.route(R(wheel, "spline/%s/axis" % name, 0.7, comp=0, attackMs=80, decayMs=80))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.6, "sustain": 0.5, "attack": 1.0, "release": 1.0,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    s.environment = {
        "intensity": 0.0, "background": hexrgb(GROUND), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#07031a"), "horizonColor": hexrgb("#0c0520"),
                "groundColor": hexrgb("#030108"), "haze": 0.8, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }

    # ---- the arms: two, opposite; each three segments emitting along themselves, flowing inward
    for arm, phase in ((0, 0.0), (1, math.pi)):
        for k, ((name, ang0), (suffix, r0, r1, turns, c0, c1, jitter, rate)) in enumerate(
                zip(arm_splines(s, arm, phase), SEGMENTS)):
            s.particles("p_" + name, capacity=9000, seed=11 + arm * 7 + k * 3, shape="spline", spline=name,
                        position=[0, 0, 0], extent=[jitter, jitter, jitter], direction=[0.0, 0.12, -1.0],
                        spawnRate=rate, lifetimeMin=2.4, lifetimeMax=3.6, spread=0.12, speedMin=0.25,
                        speedMax=0.6, gravity=[0, 0, 0], drag=0.15, turbulence=0.18, turbulenceScale=0.35,
                        turbulenceSpeed=0.2, attractorPosition=[0, 0, 0], attractorStrength=0.0,
                        attractorRadius=14.0, orbit=0.0, sizeStart=0.05 if suffix == "in" else 0.045,
                        sizeEnd=0.02, sizeVariance=0.5, sizeSkew=2.0, colorStart=hexrgb(c0) + [1.0],
                        colorEnd=hexrgb(c1) + [0.0], emissive=4.0, blend="additive", trailEnabled=True,
                        trailLength=10, trailStride=2, trailWidth=0.8, trailTaper=0.0, trailFade=0.0)
            # the galaxy turns: every segment's start angle advances a full turn per ROT_PERIOD
            s.track("spline/%s/startAngle" % name, [
                {"time": 0.0, "value": ang0, "interp": "linear"},
                {"time": ROT_PERIOD, "value": ang0 + 2.0 * math.pi, "interp": "linear"}], loop=ROT_PERIOD)

    # ---- the core: a dense sphere of gold-white points, and a white-hot heart
    s.particles("core", capacity=7000, seed=3, shape="sphere", position=[0.0, 0.0, 0.0], extent=[0.7, 0.35, 0.7],
                direction=[0, 1, 0], spawnRate=2600.0, lifetimeMin=1.5, lifetimeMax=2.6, spread=1.0, speedMin=0.05,
                speedMax=0.25, gravity=[0, 0, 0], drag=0.6, turbulence=0.3, turbulenceScale=1.2,
                attractorPosition=[0, 0, 0], attractorStrength=0.6, attractorRadius=3.0, orbit=0.9,
                sizeStart=0.06, sizeEnd=0.0, sizeVariance=0.5, colorStart=hexrgb(WHITE) + [1.0],
                colorEnd=hexrgb(GOLD) + [0.0], emissive=5.0, blend="additive")
    s.proc("heart", {"kind": "sphere", "radius": 0.22, "segments": 24, "rings": 12},
           material={"baseColor": [0, 0, 0], "emissiveColor": hexrgb(WHITE), "emissiveIntensity": 14.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True})

    # ---- the halo: faint dust curtains, a wide disc of large soft points that orbits
    s.particles("halo", capacity=9000, seed=21, shape="disc", position=[0.0, 0.0, 0.0], extent=[15.0, 0.6, 15.0],
                direction=[0, 1, 0], spawnRate=1100.0, lifetimeMin=5.0, lifetimeMax=8.0, spread=1.0, speedMin=0.0,
                speedMax=0.1, gravity=[0, 0, 0], drag=0.2, turbulence=0.25, turbulenceScale=0.15,
                turbulenceSpeed=0.1, attractorPosition=[0, 0, 0], attractorStrength=0.02, attractorRadius=20.0,
                orbit=0.35, sizeStart=0.16, sizeEnd=0.26, sizeVariance=0.6, colorStart=hexrgb("#3b2a9a") + [0.0],
                colorEnd=hexrgb(CYAN) + [0.0],
                colorCurve=[{"t": 0.0, "color": hexrgb("#4a2ab8")}, {"t": 0.5, "color": hexrgb("#2b6bff")},
                            {"t": 1.0, "color": hexrgb("#ff3fb4")}],
                opacityCurve=[{"t": 0.0, "value": 0.0}, {"t": 0.3, "value": 0.1}, {"t": 0.7, "value": 0.1},
                              {"t": 1.0, "value": 0.0}], emissive=1.2, blend="additive")

    # ---- the stream: points flung from the rim, arcing over toward the camera's side and falling back
    s.particles("stream", capacity=5000, seed=41, shape="sphere", position=[9.5, 0.0, -6.0], extent=[0.3, 0.3, 0.3],
                direction=[-0.35, 0.75, 0.55], spawnRate=650.0, lifetimeMin=3.0, lifetimeMax=4.0, spread=0.05,
                speedMin=7.0, speedMax=8.0, gravity=[0, 0, 0], drag=0.05, turbulence=0.2, turbulenceScale=0.3,
                attractorPosition=[0, 0, 0], attractorStrength=2.4, attractorRadius=40.0, orbit=0.8,
                sizeStart=0.04, sizeEnd=0.02, colorStart=hexrgb(CYAN) + [1.0], colorEnd=hexrgb(MAGENTA) + [0.0],
                emissive=3.0, blend="additive", velocityStretch=1.0, stretchMax=0.4)

    # ---- glitter: tiny fast sparks through the disc (the hats raise it)
    s.particles("glitter", capacity=3000, seed=51, shape="disc", position=[0.0, 0.0, 0.0], extent=[12.0, 0.4, 12.0],
                direction=[0, 1, 0], spawnRate=120.0, lifetimeMin=0.3, lifetimeMax=0.8, spread=1.0, speedMin=0.0,
                speedMax=0.3, gravity=[0, 0, 0], drag=1.0, sizeStart=0.025, sizeEnd=0.0,
                colorStart=hexrgb("#ffffff") + [1.0], colorEnd=hexrgb(CYAN) + [0.0], emissive=8.0, blend="additive")

    # ---- the explosion (the kick): a burst from the core outward, slowed by drag so the arms reform round it
    s.particles("blast", capacity=8000, seed=61, shape="sphere", position=[0.0, 0.0, 0.0], extent=[0.4, 0.2, 0.4],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.9, lifetimeMax=1.8, spread=1.0, speedMin=6.0,
                speedMax=14.0, gravity=[0, 0, 0], drag=2.2, turbulence=0.4, turbulenceScale=0.4,
                sizeStart=0.06, sizeEnd=0.0, colorStart=hexrgb(WHITE) + [1.0], colorEnd=hexrgb(MAGENTA) + [0.0],
                emissive=5.0, blend="additive", velocityStretch=1.5, stretchMax=0.6)
    # ---- comets (notes): a short arc on the rim whose angle the pitch sets; points fall inward from it
    s.nodes.append({"name": "cometArc", "kind": "spline", "spline": {
        "kind": "catmullRom", "generator": "spiral", "count": 6, "radius": 13.5, "radiusGrowth": 0.0, "turns": 0.03,
        "startAngle": 0.0, "center": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0], "samplesPerSegment": 8}})
    s.particles("comet", capacity=4000, seed=67, shape="spline", spline="cometArc", position=[0, 0, 0],
                extent=[0.3, 0.3, 0.3], direction=[-1.0, 0.05, 0.0], spawnRate=0.0, lifetimeMin=1.4,
                lifetimeMax=2.2, spread=0.06, speedMin=5.0, speedMax=9.0, gravity=[0, 0, 0], drag=0.4,
                attractorPosition=[0, 0, 0], attractorStrength=1.5, attractorRadius=20.0, orbit=1.2,
                sizeStart=0.07, sizeEnd=0.0, colorStart=hexrgb("#e8fbff") + [1.0], colorEnd=hexrgb(CYAN) + [0.0],
                emissive=4.0, blend="additive", trailEnabled=True, trailLength=16, trailStride=1, trailWidth=0.8,
                trailFade=0.0)
    # ---- the extra arms (chords): single magenta-violet spirals between the two arms, dark at rest
    for k, (phase, _) in enumerate(EXTRA_ARMS):
        name = "xarm%d" % k
        s.nodes.append({"name": name, "kind": "spline", "spline": {
            "kind": "catmullRom", "generator": "spiral", "count": 32, "radius": 1.2, "radiusGrowth": 10.5,
            "turns": 1.2, "startAngle": phase, "center": [0.0, 0.0, 0.0], "axis": [0.0, 1.0, 0.0],
            "samplesPerSegment": 12}})
        s.particles("xp%d" % k, capacity=7000, seed=71 + k, shape="spline", spline=name, position=[0, 0, 0],
                    extent=[0.45, 0.45, 0.45], direction=[0.0, 0.12, -1.0], spawnRate=0.0, lifetimeMin=2.0,
                    lifetimeMax=3.0, spread=0.12, speedMin=0.25, speedMax=0.6, gravity=[0, 0, 0], drag=0.15,
                    turbulence=0.2, turbulenceScale=0.35, sizeStart=0.045, sizeEnd=0.02, sizeVariance=0.5,
                    colorStart=hexrgb("#ff6ad5") + [1.0], colorEnd=hexrgb("#6a3cff") + [0.0], emissive=4.0,
                    blend="additive", trailEnabled=True, trailLength=10, trailStride=2, trailWidth=0.8)
        s.track("spline/%s/startAngle" % name, [
            {"time": 0.0, "value": phase, "interp": "linear"},
            {"time": ROT_PERIOD, "value": phase + 2.0 * math.pi, "interp": "linear"}], loop=ROT_PERIOD)
    instrument(s)

    # ---- camera: a slow orbit at about 25 degrees; the core right of centre
    s.params_({"camera/lens/focalLength": 30.0, "post/bloom/intensity": 0.65, "post/bloom/threshold": 0.55,
               "post/bloom/emissionWeight": 1.0, "post/output/vignette": 0.5, "post/output/grain": 0.015,
               "post/tonemap/operator": 3, "post/lens/chromaticAberration": 0.006,
               "temporal/feedback/enabled": True, "temporal/feedback/frames": 10, "temporal/feedback/amount": 0.9,
               "temporal/feedback/decay": 0.72, "temporal/feedback/zoom": 1.0, "temporal/feedback/rotate": 0.0,
               "temporal/feedback/hue": 0.0})
    s.arc_camera([0.0, 0.0, 0.0], radius=22.0, height=9.5, period=120.0, centre_deg=20.0, sweep_deg=60.0,
                 side=-2.8, lift=-1.2)
    cu, cv, _ = s.project([0.0, 0.0, 0.0])
    s.params_({"post/shock/radius": 1.0, "post/shock/width": 0.06, "post/shock/chroma": 0.6,
               "post/shock/centerX": round(cu, 3), "post/shock/centerY": round(cv, 3)})
    s.region("core", centre=[0.0, 0.0, 0.0], radius=1.5)
    s.region_ring("disc", [0.0, 0.0, 0.0], 12.0)
    return s
