"""THE BREATHING DEEP (organic). A colossal fungal organism hangs from the roof of a flooded cavern and breathes with
the bass; melodies climb its hanging threads as light.

Composition (SCENE-CATALOG.md #2): seen from the pool's edge, looking up. The organism -- an inverted cap like a
chandelier, its gill ring glowing from underneath -- on the upper right third; eleven threads hang from its rim into
the haze over a black pool; the cavern dissolves into teal-black behind; a wet rock lip with small glowing caps in the
lower left, soft with depth of field. The light belongs to the living thing: the gill ring is the only warm light.
"""
from .. import kit
from ..kit import (R, M, hexrgb, scale3, mix, lathe, strand, place_bumps, sd_box, sd_rbox, sd_diff, sd_noise,
                   sd_union, sd_move, sd_cone, sd_repeat, sd_smooth, SLOW, MEDIUM, FAST, HIT, SNAP)

ID = "breathing-deep"
TITLE = "The Breathing Deep"

TEAL_BLACK = "#071a1c"
UMBER = "#241a12"
BIO = "#46f0c0"
AMBER = "#ffb85c"
GOLD_DEEP = "#e08a2c"

DESIGN = {
    "category": "organic",
    "thesis": "A colossal fungal organism hangs from the roof of a flooded cavern and breathes with the bass; "
              "melodies climb its hanging threads as light.",
    "composition": {
        "background": "the cavern's far wall dissolving into blue-black haze; faint lichen colonies",
        "midground": "the organism: an inverted cap like a chandelier, its gill ring glowing underneath, eleven "
                     "hyphae threads falling to a black pool",
        "foreground": "a wet rock lip in the lower left with small glowing caps, soft with depth of field",
        "focal": "the underside of the gill ring, warm, on the upper right third",
        "secondary": ["light pulses climbing the threads", "the stalk into the ceiling", "the glowing caps on the lip"],
        "atmosphere": "humid haze lit by the gills; spores falling through it",
        "post": "soft bloom, warm halation on the gills, depth of field, vignette, teal shadows and warm highlights",
        "camera": "low at the pool's edge looking up; a slow 90 s drift",
    },
    "palette": {"dominant": TEAL_BLACK, "secondary": UMBER, "accent": BIO, "highlight": AMBER,
                "background_value": "very dark teal",
                "saturation": "the warm gill light is the only warm, saturated area; the threads carry the cool accent"},
    "motion": {
        "very_slow": ["the camera's drift", "haze"],
        "medium": ["the organism's breath (bass)", "threads swaying", "spores drifting down"],
        "fast": ["light pulses climbing the threads (melody)", "the gill ring's flash"],
        "extremely_fast": ["spore glints (hat)", "the kick's contraction"],
    },
    "vocabulary": [
        ["sustained", "response.sustain", "the gill ring blooms, the air under the organism warms, spores thicken"],
        ["melodic", "notes.noteOn + notes.lastPitch", "a pulse of light lights the thread its pitch picks (the "
         "threads hang left to right, low to high) and climbs it"],
        ["velocity", "notes.noteOn strength", "how bright the thread's pulse is"],
        ["chord", "notes.polyphony", "a chord shimmers through the whole curtain of threads"],
        ["bass", "response.bass", "the organism breathes: the cap swells, the threads sway"],
        ["kick", "response.kick", "a contraction: the cap tightens and springs back; a puff of spores"],
        ["snare", "response.snare", "the gill ring flashes and sheds a burst of spores"],
        ["hat", "response.hat", "spore glints in the haze"],
        ["brightness", "sonic.brightness", "the spores turn from amber to cyan"],
        ["roughness", "sonic.roughness", "the gills ripple and the threads kink"],
    ],
    "tier": "medium: the fog march (24 steps live) and one compiled SDF cavern",
}

ORG = (3.0, 10.5, -7.0)      # the cap's rim centre (the organism's origin)
RIM_R = 4.7
N_THREADS = 11


def thread_anchor(k):
    """Threads hang from the rim's camera-facing half, left (low notes) to right (high notes)."""
    import math
    a = math.radians(168.0 - k * (138.0 / (N_THREADS - 1)))
    return (ORG[0] + RIM_R * math.cos(a), ORG[1] - 0.25, ORG[2] + RIM_R * math.sin(a))


def lichen_program():
    """Wet stone, with colonies of cyan lichen only where a slow noise allows: clustered, never scattered."""
    return {
        "name": "bdLichen",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.11, "seed": 3},            # where colonies may live
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.72, 0.8, 0.0, 0.0]},
            {"kind": "voronoi", "dst": 2, "srcA": 0, "value": 2.6, "seed": 5},           # the colony's cells
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.32, 0.12, 0.0, 0.0]},
            {"kind": "multiply", "dst": 3, "srcA": 1, "srcB": 2},
            {"kind": "input", "dst": 4, "input": "materialEmission"},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 3},
            {"kind": "noise", "dst": 5, "srcA": 0, "value": 0.7, "seed": 9},             # wet stone tone
            {"kind": "ramp", "dst": 6, "srcA": 5, "constant": hexrgb("#050807") + [1.0],
             "constant2": hexrgb("#0e1312") + [1.0], "constant3": hexrgb("#1a1610") + [1.0]},
        ],
        "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 1.0, "opacity": -1,
    }


def flesh_program():
    """The organism's hood: dark living tissue with faint veins of the cool accent, brighter toward the rim."""
    return {
        "name": "bdFlesh",
        "ops": [
            {"kind": "input", "dst": 0, "input": "localPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 1.4, "seed": 21},
            {"kind": "remap", "dst": 2, "srcA": 1, "value": 0, "constant": [0.5, 1.0, 0.0, 1.0]},
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 2},
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.0, 0.004, 0.0, 0.0]},
            {"kind": "remap", "dst": 2, "srcA": 2, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},  # thin veins
            {"kind": "input", "dst": 3, "input": "materialEmission"},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 2},
            {"kind": "ramp", "dst": 4, "srcA": 1, "constant": hexrgb("#120d0a") + [1.0],
             "constant2": hexrgb("#24170f") + [1.0], "constant3": hexrgb("#3a2414") + [1.0]},
        ],
        "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0, "opacity": -1,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.0, "background": hexrgb("#020606"), "fogColor": scale3(hexrgb("#0c2a2c"), 0.35),
        "volumeDensity": 0.0032, "volumeMaxDistance": 70.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": scale3(hexrgb("#0a1c1e"), 0.25),
                "horizonColor": scale3(hexrgb("#0d2224"), 0.25), "groundColor": scale3(hexrgb("#080d0c"), 0.2),
                "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "gills", "position": [ORG[0], ORG[1] - 0.3, ORG[2]], "radius": 5.0, "weight": 1.0}],
        "layers": [
            {"name": "near", "start": 0.0, "end": 10.0, "contrast": 1.0, "saturation": 1.0},
            {"name": "organism", "start": 10.0, "end": 28.0, "contrast": 1.05, "saturation": 1.05},
            {"name": "cavern", "start": 28.0, "end": 200.0, "contrast": 0.78, "saturation": 0.7},
        ],
    }
    s.program(lichen_program())
    s.program(flesh_program())

    # ---- the cavern: rock everywhere outside a soft, noise-carved void (the camera stands inside it)
    void = sd_move((2.0, 8.0, -6.0), sd_rbox((25.0, 15.0, 21.0), 11.0))
    rock = sd_diff(sd_move((0.0, 9.0, -6.0), sd_box((60.0, 26.0, 52.0))), void)
    stalactites = sd_move((2.0, 21.5, -8.0), sd_repeat((4.6, 0.0, 4.2),
                                                       sd_move((0.0, -1.6, 0.0), sd_cone(0.7, 4.2)), count=3))
    cavern = sd_noise(0.35, 0.95, sd_noise(1.6, 0.16, sd_smooth(1.4, rock, stalactites), seed=7), seed=11)
    s.sdf("cavern", cavern, (-60, -4, -60), (60, 36, 48),
          material={"baseColor": [1, 1, 1], "emissiveColor": hexrgb(BIO), "emissiveIntensity": 0.3,
                    "roughness": 0.32, "metallic": 0.0, "program": "bdLichen"},
          look={"aoStrength": 0.45, "aoDistance": 1.6}, max_steps=110, epsilon=0.002, step_scale=0.75,
          max_distance=90.0)

    # ---- the pool: black, still, a faint sheen of the cavern's air
    s.proc("pool", {"kind": "box", "size": [140.0, 0.2, 140.0], "subdivisions": 1},
           material={"baseColor": hexrgb("#020404"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.06, "metallic": 0.0},
           transform={"position": [0.0, -0.1, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the organism: hood, gill plates, the glowing hymenium behind them, the stalk into the ceiling
    hood = lathe([(0.0, 5.2), (0.2, 5.3), (0.55, 5.05), (1.0, 4.55), (1.55, 3.7), (2.05, 2.55), (2.45, 1.45),
                  (2.75, 0.55), (2.85, 0.1)], sides=64)
    s.proc("cap", hood, position=ORG,
           material={"baseColor": [0.03, 0.02, 0.015], "emissiveColor": hexrgb(BIO), "emissiveIntensity": 0.14,
                     "roughness": 0.55, "metallic": 0.0, "program": "bdFlesh"},
           deformers=[{"kind": "noise", "amount": 0.16, "scale": 0.55, "speed": 0.05, "seed": 3,
                       "axisMask": [1, 1, 1]}])
    s.proc("gills", {"kind": "box", "size": [0.035, 0.6, 4.2], "subdivisions": 2},
           distribution={"kind": "radial", "count": 84, "radius": 2.75, "orientation": "outward"},
           transform={"position": [0.0, -0.32, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]}, position=ORG,
           material={"baseColor": hexrgb("#1e120a"), "emissiveColor": hexrgb(GOLD_DEEP), "emissiveIntensity": 0.6,
                     "roughness": 0.7, "metallic": 0.0},
           variation={"seed": 4, "scale": [0.0, 0.25, 0.08], "rotation": [0.0, 0.02, 0.0]},
           material_variation={"emissiveRandom": 0.35},
           deformers=[{"kind": "noise", "amount": 0.0, "scale": 1.3, "speed": 2.2, "seed": 61}])
    s.proc("hymenium", {"kind": "cylinder", "radius": 5.0, "height": 0.04, "radialSegments": 96, "caps": True},
           position=ORG, transform={"position": [0.0, 0.0, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb(AMBER), "emissiveIntensity": 2.2,
                     "roughness": 1.0, "metallic": 0.0})
    s.proc("stalk", strand((ORG[0], ORG[1] + 2.6, ORG[2]), (ORG[0] + 0.8, 25.0, ORG[2] - 1.2), 0.85, taper=2.2,
                           seed=31, noise=0.5, noise_scale=0.4, sides=16, segments=20),
           material={"baseColor": [0.03, 0.02, 0.015], "emissiveColor": hexrgb(BIO), "emissiveIntensity": 0.25,
                     "roughness": 0.6, "metallic": 0.0, "program": "bdFlesh"})

    # ---- the threads: eleven hyphae, each its own entity carrying a travelling pulse of light
    for k in range(N_THREADS):
        a = thread_anchor(k)
        length = 7.4 + 1.6 * ((k * 37) % 5) / 4.0
        end = (a[0] + 0.35 * ((k % 3) - 1), a[1] - length, a[2] + 0.4 * ((k % 2) - 0.5))
        name = "thread%d" % k
        s.proc(name, strand(a, end, 0.032, taper=0.3, seed=100 + k, noise=0.28, noise_scale=0.7, sides=8,
                            segments=40),
               material={"baseColor": [0.01, 0.02, 0.02], "emissiveColor": hexrgb(BIO), "emissiveIntensity": 0.07,
                         "roughness": 0.5, "metallic": 0.0},
               deformers=[{"kind": "sine", "amount": 0.12, "frequency": 0.35, "speed": 0.55 + 0.04 * k,
                           "phase": 0.6 * k, "axis": [0, 1, 0], "displacementAxis": [1, 0, 0.4], "space": "world"},
                          {"kind": "noise", "amount": 0.0, "scale": 1.8, "speed": 3.0, "seed": 70 + k}])
        s.entity(name, tags=["thread"])
        s.effect("pulse%d" % k, "pulse", ("entity", name), parameters={
            "mode": 1, "waveform": 4, "rate": -0.55, "peak": 2.0, "depth": 0.95, "phase": 0.07 * k,
            "sharpness": 2.0, "axis": 0, "bandWidth": 0.16})

    # ---- the cap breathes (bass) and its tissue lives (a slow bioluminescent drift)
    s.entity("cap", tags=["organism"])
    s.effect("capBreath", "breathing", ("entity", "cap"), parameters={
        "amplitude": 0.02, "rate": 0.12, "asymmetry": 0.3, "regionCenter": 0.5, "regionWidth": 1.2, "phase": 0.0})

    # ---- the rock lip in the foreground, and its small glowing caps
    s.proc("lip", {"kind": "sphere", "radius": 2.6, "segments": 48, "rings": 24}, position=(-8.4, -0.5, 5.2),
           material={"baseColor": hexrgb("#0b0d0c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.4, "metallic": 0.0},
           deformers=[{"kind": "noise", "amount": 0.9, "scale": 0.45, "speed": 0.0, "seed": 15, "axisMask": [1, 1, 1]},
                      {"kind": "noise", "amount": 0.18, "scale": 2.2, "speed": 0.0, "seed": 16, "axisMask": [1, 1, 1]}],
           transform={"position": [0, 0, 0], "rotation": [0, 30, 0], "scale": [1.5, 0.7, 1.1]})
    small_cap = lathe([(0.0, 0.2), (0.05, 0.21), (0.12, 0.17), (0.18, 0.1), (0.21, 0.02)], sides=20)
    small_stem = {"kind": "tube", "tubeRadius": 0.03, "tubeTaper": 0.6, "tubeSides": 6, "tubeSegments": 6,
                  "tubeTwist": 0.0, "tubeCaps": True,
                  "curve": {"kind": "catmullRom", "generator": "line", "count": 3, "start": [0, -0.32, 0],
                            "end": [0, 0.0, 0], "samplesPerSegment": 4}}
    # a colony, not a scatter: one larger cap and a tail of smaller ones (heavy-tailed sizes)
    for name, src, emi in (("lipCaps", small_cap, 0.55), ("lipStems", small_stem, 0.12)):
        s.proc(name, src, position=(-7.6, 1.15, 4.6),
               distribution={"kind": "spiral", "count": 12, "radius": 0.3, "radiusGrowth": 0.28, "turns": 1.25,
                             "spiralHeight": 0.0},
               variation={"seed": 12, "scale": [0.55, 0.55, 0.55], "position": [0.12, 0.05, 0.12]},
               material={"baseColor": [0.02, 0.02, 0.02], "emissiveColor": hexrgb(BIO), "emissiveIntensity": emi,
                         "roughness": 0.5, "metallic": 0.0},
               material_variation={"emissiveRandom": 0.6})

    # ---- particles: spores falling from the gills; a burst on a snare; glints on hats
    s.particles("spores", capacity=6000, seed=7, shape="disc", position=[ORG[0], ORG[1] - 0.7, ORG[2]],
                extent=[4.6, 0.0, 4.6], direction=[0, -1, 0], spawnRate=55.0, lifetimeMin=6.0, lifetimeMax=11.0,
                spread=0.35, speedMin=0.05, speedMax=0.25, gravity=[0, -0.04, 0], drag=0.6, turbulence=0.35,
                turbulenceScale=0.35, turbulenceSpeed=0.25, sizeStart=0.022, sizeEnd=0.012, sizeVariance=0.5,
                sizeSkew=2.2, colorStart=hexrgb(AMBER) + [1.0], colorEnd=hexrgb(GOLD_DEEP) + [0.0], emissive=4.0,
                blend="additive", softness=0.2)
    s.particles("sporeBurst", capacity=6000, seed=9, shape="disc", position=[ORG[0], ORG[1] - 0.6, ORG[2]],
                extent=[4.4, 0.0, 4.4], direction=[0, -1, 0], spawnRate=0.0, lifetimeMin=1.5, lifetimeMax=3.5,
                spread=0.6, speedMin=0.6, speedMax=1.8, gravity=[0, -0.1, 0], drag=1.4, turbulence=0.6,
                turbulenceScale=0.5, sizeStart=0.03, sizeEnd=0.0, colorStart=hexrgb("#ffe3a8") + [1.0],
                colorEnd=hexrgb(AMBER) + [0.0], emissive=7.0, blend="additive")
    s.particles("glints", capacity=1500, seed=13, shape="box", position=[ORG[0] - 1.0, 5.5, ORG[2] + 2.0],
                extent=[7.0, 4.5, 6.0], direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.06, lifetimeMax=0.18,
                spread=1.0, speedMin=0.0, speedMax=0.05, gravity=[0, 0, 0], drag=0.0, turbulence=0.0,
                sizeStart=0.03, sizeEnd=0.0, colorStart=hexrgb("#fff2d6") + [1.0], colorEnd=hexrgb(AMBER) + [0.0],
                emissive=12.0, blend="additive")

    # ---- light: the gill ring is the light; a cold rim from deep in the cavern separates the threads
    s.light("hymenium", "point", position=[ORG[0], ORG[1] - 1.1, ORG[2]], color=hexrgb(AMBER), intensity=160.0,
            range=15.0, radius=3.0, castsShadow=False, volumetric=0.6)
    s.light("deep", "point", position=[ORG[0] + 3.0, 6.0, ORG[2] - 12.0], color=hexrgb("#58d8c8"), intensity=90.0,
            range=34.0, radius=2.0, castsShadow=False, volumetric=0.5)
    s.light("fill", "directional", direction=[0.25, -1.0, -0.35], color=hexrgb("#3c6e70"), intensity=0.05,
            castsShadow=False)

    # ---- camera: low at the pool's edge, looking up into the organism
    s.camera["fov"] = 50.0
    s.drift_camera(centre=(-6.0, 1.8, 11.0), target=(-2.6, 5.2, -6.0), period=90.0, amp=(0.9, 0.25, 0.7),
                   tamp=(0.25, 0.15, 0.0))

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # the threads, low to high (pitch 0.30 ~ D3 .. 0.66 ~ A5 across the eleven)
    centres = [0.30 + 0.036 * k for k in range(N_THREADS)]
    s.places("th", "lastPitch", centres, 0.036, event="noteOn")
    s.map(M("heat", [("sustain", 1.0), ("held", 0.5)], "max"))
    s.map(M("accent", [("density", 1.0, True)], "mean"))
    s.map(M("grit", [("roughness", 1.0), ("energy", 0.4), ("inharmonicity", 2.0, True)], "product", -0.3, 6.5))
    s.map2(M("chordShimmer", [("noteOn", 1.0), ("polyphony", 1.0)], "min"))

    # sustained: the gill ring blooms, the light under the organism warms, spores thicken
    s.route(R("visual.heat", "procedural/hymenium/material/emissive", 6.0, **SLOW),
            R("visual.heat", "lights/hymenium/intensity", 900.0, **SLOW),
            R("visual.heat", "procedural/gills/material/emissive", 1.6, **SLOW),
            R("visual.heat", "particles/spores/spawnRate", 160.0, **SLOW),
            R("visual.heat", "scene/fogColor", 0.02, comp=0, **SLOW),
            R("visual.heat", "scene/fogColor", 0.008, comp=1, **SLOW))
    # melodic: each note lights its own thread; the pulse climbing it carries the light up
    for k in range(N_THREADS):
        s.route(R("visual.thHit%d" % k, "fx/pulse%d/peak" % k, 16.0, depth="visual.accentPlus", attackMs=0,
                  decayMs=1400),
                R("visual.thHit%d" % k, "procedural/thread%d/material/emissive" % k, 1.6, attackMs=0, decayMs=900))
        # a chord shimmers through every thread at once, faintly
        s.route(R("visual.chordShimmer", "fx/pulse%d/peak" % k, 3.0, attackMs=0, decayMs=1100))
        # roughness kinks the threads
        s.route(R("visual.grit", "procedural/thread%d/deform/2/amount" % k, 0.12, **FAST))
    s.map2(M("accentPlus", [("visual.accent", 0.6)], "mean", 0.4))  # dense playing: smaller flashes (0.4 .. 1.0)
    # bass: the organism breathes
    s.route(R("bass", "fx/capBreath/amplitude", 0.22, attackMs=120, decayMs=900),
            R("bass", "procedural/gills/transform/scale", 0.05, comp=1, attackMs=120, decayMs=900))
    for k in range(N_THREADS):
        s.route(R("bass", "procedural/thread%d/deform/1/amount" % k, 0.35, attackMs=200, decayMs=1200))
    # kick: a contraction, on a spring (overshoot and settle), and a puff of spores
    for node in ("cap", "gills", "hymenium"):
        s.route(R("kick", "procedural/%s/transform/scale" % node, 1.0, op="multiply", gain=-0.07, offset=1.0,
                  attackMs=0, decayMs=90, springHz=2.6, springDamping=0.32))
    s.route(R("kick", "particles/sporeBurst/burst", 90.0, attackMs=0, decayMs=40))
    # snare: the gill ring flashes and sheds spores
    s.route(R("snare", "procedural/gills/material/emissive", 9.0, attackMs=0, decayMs=220),
            R("snare", "procedural/hymenium/material/emissive", 6.0, attackMs=0, decayMs=260),
            R("snare", "particles/sporeBurst/burst", 420.0, attackMs=0, decayMs=40))
    # hat: glints in the haze
    s.route(R("hat", "particles/glints/burst", 26.0, attackMs=0, decayMs=30))
    # timbre: a bright sound turns the spores from amber toward cyan; a rough one ripples the gills
    s.route(R("brightness", "particles/spores/colorStart", -0.55, comp=0, **MEDIUM),
            R("brightness", "particles/spores/colorStart", 0.25, comp=1, **MEDIUM),
            R("brightness", "particles/spores/colorStart", 0.55, comp=2, **MEDIUM),
            R("visual.grit", "procedural/gills/deform/1/amount", 0.18, **FAST))
    # no generic post routes: bloom and chromatic aberration answer only to these
    s.route(R("visual.heat", "post/bloom/intensity", 0.15, **SLOW),
            R("snare", "post/lens/chromaticAberration", 0.05, attackMs=0, decayMs=180))

    s.params_({
        "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.85, "post/bloom/emissionWeight": 0.8,
        "post/halation/enabled": True, "post/halation/intensity": 0.3, "post/halation/warmth": 0.75,
        "post/lens/chromaticAberration": 0.015, "post/output/vignette": 0.55, "post/output/grain": 0.016,
        "post/grade/contrast": 1.1, "post/grade/saturation": 1.08,
        "post/grade/lift": [0.0, 0.004, 0.005], "post/grade/gain": [1.04, 1.0, 0.96],
        "camera/lens/focalLength": 26.0, "camera/exposure/compensation": 0.1,
        "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/aperture": 2.4,
        "camera/focus/mode": 0, "camera/lens/focusDistance": 19.0,
        "scene/volumeAnisotropy": 0.45, "scene/volumeScattering": 1.0,
    })
    # ---- the evaluator's screen regions, projected through the camera at t = 0, and the performer's baseline
    s.region("organism", centre=list(ORG), radius=3.5)
    s.region_points("threads", [[ORG[0] - 3.2, ORG[1], ORG[2]], [ORG[0] + 3.2, ORG[1], ORG[2]],
                                [ORG[0] - 3.2, 2.0, ORG[2]], [ORG[0] + 3.2, 2.0, ORG[2]]])
    s.region("background", box=[0.0, 0.0, 0.4, 1.0])
    s.response = {"sensitivity": 0.5, "transient": 0.4, "sustain": 0.6, "attack": 1.4, "release": 1.8}
    return s
