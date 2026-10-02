"""EVENT HORIZON (cosmic). A black hole drinks a disk of light: every note is a star that falls into it, and the bass
is the hole's own mass bending the sky.

Composition (SCENE-CATALOG.md #1): the shadow and its photon ring on the right third, the disk sweeping across the
frame on a shallow diagonal with its far side lensed over the shadow, a dark basalt fragment out of focus in the lower
left for scale, the galaxy's band behind. Light: the disk is the only real light; it rims the debris from inside.
"""
from .. import kit
from ..kit import R, M, hexrgb, scale3, mix, FAST, HIT, SNAP, MEDIUM, SLOW

ID = "event-horizon"
TITLE = "Event Horizon"

# palette (sRGB hex; kit.hexrgb converts to linear)
VOID = "#04030a"
EMBER = "#ff7a2a"
DEEP_RED = "#7a1808"
GOLD = "#ffc77a"
WHITE_HOT = "#fff3dc"
JET_BLUE = "#6aa8ff"

DESIGN = {
    "category": "cosmic",
    "thesis": "A black hole drinks a disk of light: every note is a star that falls into it, and the bass is the "
              "hole's own mass bending the sky.",
    "composition": {
        "background": "a desaturated star field and the galaxy's dust band, lensed into arcs round the shadow",
        "midground": "the accretion disk on a shallow diagonal, hot white-gold at its inner edge, ember-red outside; "
                     "its far side is bent up over the shadow",
        "foreground": "a basalt fragment drifting out of focus in the lower left; fine dust",
        "focal": "the shadow and its photon ring, on the right third",
        "secondary": ["relativistic jets, faint, above and below", "note-stars spiralling in", "the hot spot"],
        "atmosphere": "none (vacuum); glowing gas and dust only",
        "post": "bloom and warm halation on the inner disk, a whisper of anamorphic streak, edge chromatic "
                "aberration, grain, a heavy vignette",
        "camera": "a 40-degree arc swept back and forth over 160 s, 9 degrees above the disk plane",
    },
    "palette": {"dominant": VOID, "secondary": EMBER, "accent": JET_BLUE, "highlight": WHITE_HOT,
                "background_value": "very dark (L about 0.03)",
                "saturation": "highest in the inner disk; stars and dust desaturated"},
    "motion": {
        "very_slow": ["the camera's arc", "the lensed star field drifting"],
        "medium": ["the disk's rotation (inner bands faster: Keplerian)", "the gas spiralling inward"],
        "fast": ["note-stars spiralling in over 2-4 s", "the hot spot's flare"],
        "extremely_fast": ["the photon ring flash (kick)", "sparks in the disk (hat)", "the lens chroma tick"],
    },
    "vocabulary": [
        ["sustained", "response.sustain", "the disk heats: every band brighter, the gas denser and whiter"],
        ["melodic", "notes.noteOn + notes.lastPitch", "a star is born at the orbit set by its pitch (low notes far "
         "and slow, high notes close and fast) and spirals in, trailing light"],
        ["velocity", "notes.lastVelocity", "how many stars and how bright"],
        ["polyphony", "notes.polyphony", "a chord releases a cluster: the birthplace spreads"],
        ["bass", "response.bass", "the hole's mass: the Einstein radius and the shadow swell, the sky bends more"],
        ["kick", "response.kick", "the photon ring flashes and the jets pulse"],
        ["snare", "response.snare", "a flare erupts from the hot spot on the approaching side"],
        ["hat", "response.hat", "sparks across the disk"],
        ["density", "notes.density", "busy playing churns the gas (turbulence)"],
        ["tension", "notes.tension", "dissonance splits the lens's colours"],
        ["brightness", "sonic.brightness", "a bright timbre turns the inner edge from gold to blue-white"],
    ],
    "tier": "live: no fog march; one DF lens, two light beams, four particle systems",
}

BH = (0.0, 0.0, 0.0)
# The disk's inner edge: three thin opaque bands of white-hot gas at the innermost stable orbit (radius, half-width,
# emission, colour mix 0 = white-hot .. 1 = deep red). The rest of the disk is gas (particles): additive, streaked
# along its orbit by velocity stretch, so it reads as luminous matter rather than a plate.
BANDS = [(3.1, 0.16, 16.0, 0.0), (3.55, 0.2, 11.0, 0.1), (4.15, 0.26, 7.0, 0.22)]


def band_colour(t):
    """White-hot -> gold -> ember -> deep red as t goes 0 -> 1."""
    stops = [hexrgb(WHITE_HOT), hexrgb(GOLD), hexrgb(EMBER), hexrgb(DEEP_RED)]
    x = min(max(t, 0.0), 1.0) * (len(stops) - 1)
    i = min(int(x), len(stops) - 2)
    return mix(stops[i], stops[i + 1], x - i)


def gas_program():
    """The disk's own light lives in clumps along each band: two octaves of noise on the band's object-space point
    (which travels with the band as it spins), so the gas reads as streaming clumps, not a painted ring."""
    return {
        "name": "ehGas",
        "ops": [
            {"kind": "input", "dst": 0, "input": "localPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.55, "seed": 3},
            {"kind": "noise", "dst": 2, "srcA": 0, "value": 2.1, "seed": 11},
            {"kind": "mix", "dst": 3, "srcA": 1, "srcB": 2, "value": 0.35},
            {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.28, 0.78, 0.0, 0.0]},
            {"kind": "remap", "dst": 3, "srcA": 3, "value": 1, "constant": [0.0, 1.0, 0.18, 1.35]},
            {"kind": "input", "dst": 4, "input": "materialEmission"},
            {"kind": "multiply", "dst": 5, "srcA": 4, "srcB": 3},
            {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 5, "emissionIntensity": 1.0, "opacity": -1,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.0, "background": hexrgb(VOID), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": scale3(hexrgb("#070512"), 0.5), "horizonColor": scale3(hexrgb("#0b0716"), 0.6),
                "groundColor": scale3(hexrgb("#05040a"), 0.5), "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.composition = {"focalPoints": [{"name": "horizon", "position": list(BH), "radius": 6.0, "weight": 1.0}]}
    s.program(gas_program())

    # ---- the inner edge: three thin bands at the innermost stable orbit, spun by a twist with no height term
    # (ADR-1021 made a full turn safe)
    for i, (r, w, e, t) in enumerate(BANDS):
        spin = 62.0 * (BANDS[0][0] / r) ** 1.5  # degrees per second
        s.proc("band%d" % i,
               {"kind": "torus", "majorRadius": r, "minorRadius": w, "majorSegments": 192, "minorSegments": 10},
               material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": band_colour(t), "emissiveIntensity": e,
                         "roughness": 1.0, "metallic": 0.0, "program": "ehGas"},
               deformers=[{"kind": "twist", "amount": 0.0, "speed": round(spin * 3.14159 / 180.0, 4),
                           "axis": [0, 1, 0]},
                          {"kind": "noise", "amount": 0.0, "scale": 0.35, "speed": 0.6, "seed": 40 + i,
                           "axisMask": [1, 1, 1], "space": "world"}],
               transform={"position": [0, 0.004 * i, 0], "rotation": [0, 37.0 * i, 0], "scale": [1, 0.09, 1]})

    # ---- the hole: a black-hole lens with a thin photon ring
    s.effect("bh", "gravLens", ("world",), parameters={
        "einsteinRadius": 6.0, "mode": 1, "horizonScale": 0.42, "chroma": 0.008,
        "ringColor": hexrgb("#ffe2b8"), "photonRing": 4.0, "ringWidth": 0.045, "falloffRadius": 3.2,
        "feather": 0.55, "offsetX": BH[0], "offsetY": BH[1], "offsetZ": BH[2]})
    # ---- the sky
    s.effect("stars", "stars", ("world",), parameters={
        "brightness": 4.0, "density": 0.07, "magnitudeSlope": 6.5, "colorSpread": 0.55, "twinkle": 0.1,
        "twinkleRate": 3.0, "horizonFade": 0.05, "band": 1.3, "bandTilt": 0.55, "daylight": 0.0})
    # ---- jets: particle streams leaving the poles, faint until a kick
    for jid, sign in (("jetUp", 1.0), ("jetDown", -1.0)):
        s.particles(jid, capacity=6000, seed=61 if sign > 0 else 67, shape="sphere", position=[0, 0.6 * sign, 0],
                    extent=[0.12, 0.12, 0.12], direction=[0, sign, 0], spawnRate=28.0, lifetimeMin=1.2,
                    lifetimeMax=2.4, spread=0.012, speedMin=16.0, speedMax=24.0, gravity=[0, 0, 0], drag=0.05,
                    turbulence=0.08, turbulenceScale=0.2, sizeStart=0.035, sizeEnd=0.07, sizeVariance=0.4,
                    colorStart=hexrgb("#cfe2ff") + [0.7], colorEnd=hexrgb(JET_BLUE) + [0.0], emissive=1.4,
                    blend="additive", velocityStretch=0.6, stretchMax=0.8)

    # ---- the gas: matter spiralling in, heating as it falls. Two populations: the outer disk (born ember-red,
    # dying gold) and the inner (gold to white-hot). Additive and streaked along its orbit, so the disk is light.
    s.particles("gasOuter", capacity=60000, seed=17, shape="disc", position=[0, 0, 0], extent=[15.0, 0.0, 15.0],
                direction=[0, 1, 0], spawnRate=7500.0, lifetimeMin=5.0, lifetimeMax=8.0, spread=1.0,
                speedMin=0.0, speedMax=0.02, gravity=[0, 0, 0], drag=0.42, turbulence=0.03,
                turbulenceScale=0.3, turbulenceSpeed=0.35, attractorPosition=[0, 0, 0], attractorStrength=0.3,
                attractorRadius=17.0, orbit=2.4, sizeStart=0.026, sizeEnd=0.016, sizeVariance=0.6, sizeSkew=2.4,
                colorStart=hexrgb(DEEP_RED) + [0.0], colorEnd=hexrgb(EMBER) + [0.85], emissive=2.6,
                blend="additive", velocityStretch=2.0, stretchMax=1.4)
    s.particles("gasInner", capacity=40000, seed=19, shape="disc", position=[0, 0, 0], extent=[7.0, 0.0, 7.0],
                direction=[0, 1, 0], spawnRate=9000.0, lifetimeMin=1.6, lifetimeMax=3.4, spread=1.0,
                speedMin=0.0, speedMax=0.02, gravity=[0, 0, 0], drag=0.5, turbulence=0.02,
                turbulenceScale=0.5, turbulenceSpeed=0.5, attractorPosition=[0, 0, 0], attractorStrength=0.55,
                attractorRadius=8.0, orbit=4.5, sizeStart=0.02, sizeEnd=0.014, sizeVariance=0.5, sizeSkew=2.0,
                colorStart=hexrgb(GOLD) + [0.0], colorEnd=hexrgb(WHITE_HOT) + [0.9], emissive=6.0,
                blend="additive", velocityStretch=2.2, stretchMax=1.2)
    # the approaching side is brighter (Doppler beaming: the disk turns towards the camera on the right)
    s.particles("beaming", capacity=16000, seed=23, shape="disc", position=[5.2, 0.02, 1.0], extent=[3.2, 0.0, 4.2],
                direction=[0, 1, 0], spawnRate=5000.0, lifetimeMin=0.8, lifetimeMax=1.8, spread=1.0,
                speedMin=0.0, speedMax=0.02, gravity=[0, 0, 0], drag=0.5, turbulence=0.02,
                attractorPosition=[0, 0, 0], attractorStrength=0.5, attractorRadius=12.0, orbit=4.0,
                sizeStart=0.022, sizeEnd=0.014, sizeVariance=0.5, sizeSkew=2.0,
                colorStart=hexrgb(WHITE_HOT) + [0.0], colorEnd=hexrgb(GOLD) + [0.8], emissive=5.0,
                blend="additive", velocityStretch=2.2, stretchMax=1.2)
    # ---- note-stars: born where the note says, spiralling in with a trail
    s.particles("stars", capacity=3000, seed=29, shape="sphere", position=[13.0, 0.0, 0.0], extent=[0.25, 0.25, 0.25],
                direction=[0, 0, -1], spawnRate=0.0, lifetimeMin=2.5, lifetimeMax=4.0, spread=0.15,
                speedMin=2.2, speedMax=3.0, gravity=[0, 0, 0], drag=0.15, turbulence=0.05,
                attractorPosition=[0, 0, 0], attractorStrength=1.6, attractorRadius=18.0, orbit=3.8,
                sizeStart=0.11, sizeEnd=0.03, colorStart=hexrgb("#dfe9ff") + [1.0],
                colorEnd=hexrgb(WHITE_HOT) + [0.0], emissive=22.0, blend="additive",
                trailEnabled=True, trailLength=24, trailStride=2, trailWidth=0.8, trailTaper=0.05, trailFade=0.0)
    # ---- the flare (snare): a burst from the hot spot on the approaching side
    s.particles("flare", capacity=4000, seed=41, shape="sphere", position=[4.4, 0.1, 1.8], extent=[0.4, 0.2, 0.4],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.5, lifetimeMax=1.2, spread=0.55,
                speedMin=2.0, speedMax=6.0, gravity=[0, 0, 0], drag=1.6, turbulence=0.6, turbulenceScale=0.8,
                attractorPosition=[0, 0, 0], attractorStrength=0.0, attractorRadius=8.0, orbit=0.0,
                sizeStart=0.07, sizeEnd=0.0, colorStart=hexrgb(WHITE_HOT) + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=14.0, blend="additive", velocityStretch=1.0, stretchMax=0.5)
    # ---- sparks (hat): glints scattered over the disk, a few frames each
    s.particles("sparks", capacity=2000, seed=53, shape="disc", position=[0, 0.05, 0], extent=[11.0, 0.0, 11.0],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.08, lifetimeMax=0.22, spread=1.0,
                speedMin=0.0, speedMax=0.2, gravity=[0, 0, 0], drag=0.0, turbulence=0.0,
                sizeStart=0.05, sizeEnd=0.0, colorStart=hexrgb("#ffffff") + [1.0], colorEnd=hexrgb(GOLD) + [0.0],
                emissive=16.0, blend="additive")

    # ---- debris for scale: dark fragments, the near one out of focus, rimmed by the disk
    rock = {"baseColor": hexrgb("#1a1714"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
            "roughness": 0.85, "metallic": 0.0}
    for name, pos, rad, rot, seed in (("rockNear", (-19.0, -1.2, 19.0), 2.6, (20, 35, 10), 5),
                                      ("rockFar", (17.0, 4.5, -9.0), 0.9, (-10, 80, 30), 9),
                                      ("rockMid", (9.5, -2.4, 9.0), 0.45, (40, 10, 70), 13)):
        s.proc(name, {"kind": "sphere", "radius": rad, "segments": 48, "rings": 24}, material=rock,
               deformers=[{"kind": "noise", "amount": rad * 0.35, "scale": 0.9 / rad, "speed": 0.0, "seed": seed,
                           "axisMask": [1, 1, 1]},
                          {"kind": "noise", "amount": rad * 0.08, "scale": 4.0 / rad, "speed": 0.0,
                           "seed": seed + 1, "axisMask": [1, 1, 1]}],
               transform={"position": list(pos), "rotation": list(rot), "scale": [1.0, 0.72, 0.9]})

    # ---- light: the inner disk is the light source; a faint cold fill keeps the debris' shadow side from black
    s.light("disk", "point", position=[0, 0.6, 0], color=hexrgb(GOLD), intensity=1600.0, range=60.0,
            radius=3.0, castsShadow=False, volumetric=0.0)
    s.light("fill", "directional", direction=[0.3, -0.25, -1.0], color=hexrgb("#7f95c9"), intensity=0.08,
            castsShadow=False)

    # ---- camera: an arc, 9 degrees above the plane, the hole on the right third
    s.camera["fov"] = 32.0
    s.arc_camera(BH, radius=40.0, height=4.6, period=160.0, centre_deg=-8.0, sweep_deg=36.0, steps=16,
                 side=6.8, lift=0.9, height_bob=0.5)

    # ---- the instrument -------------------------------------------------------------------------------------------
    # pitch -> orbit: low notes are born far out (slow), high notes close in (fast). lastPitch 0.30 (about D3) maps to
    # the outer edge, 0.72 (about C6) to the inner.
    s.map(M("orbit", [("lastPitch", 1.0)], "mean", -0.30 / 0.42, 1.0 / 0.42))
    s.map(M("heat", [("sustain", 1.0), ("energy", 0.6)], "max"))
    s.map(M("churn", [("density", 1.0), ("rhythm", 0.6)], "max", 0.0, 1.4))
    s.map(M("grit", [("roughness", 1.0), ("energy", 0.4), ("inharmonicity", 2.0, True)], "product", -0.3, 6.5))

    # mass: the bass swells the lens (medium attack, slow release: heavy things move slowly)
    s.route(R("bass", "fx/bh/einsteinRadius", 1.8, attackMs=90, decayMs=900),
            R("bass", "fx/bh/horizonScale", 0.03, attackMs=90, decayMs=900))
    # heat: sustained sound brightens every band and densifies the gas
    for i, (r, w, e, t) in enumerate(BANDS):
        s.route(R("visual.heat", "procedural/band%d/material/emissive" % i, e * 0.9, **SLOW))
    s.route(R("visual.heat", "particles/gasOuter/spawnRate", 2400.0, **SLOW),
            R("visual.heat", "particles/gasOuter/emissive", 2.5, **SLOW),
            R("visual.heat", "particles/gasInner/spawnRate", 2200.0, **SLOW),
            R("visual.heat", "particles/gasInner/emissive", 6.0, **SLOW),
            R("visual.heat", "particles/beaming/emissive", 5.0, **SLOW))
    # timbre: a bright sound turns the inner edge blue-white
    s.route(R("brightness", "procedural/band0/material/emissiveColor", -0.25, comp=0, **MEDIUM),
            R("brightness", "procedural/band0/material/emissiveColor", 0.1, comp=1, **MEDIUM),
            R("brightness", "procedural/band0/material/emissiveColor", 0.55, comp=2, **MEDIUM),
            R("brightness", "procedural/band1/material/emissiveColor", 0.35, comp=2, **MEDIUM))
    # roughness: the bands break up and the gas boils
    for i in range(len(BANDS)):
        s.route(R("visual.grit", "procedural/band%d/deform/2/amount" % i, 0.35, **FAST))
    s.route(R("visual.grit", "particles/gasOuter/turbulence", 1.5, **FAST),
            R("visual.grit", "particles/gasInner/turbulence", 1.0, **FAST))
    # the playing: busy notes churn the disk
    s.route(R("visual.churn", "particles/gasOuter/turbulence", 0.9, **MEDIUM),
            R("visual.churn", "particles/gasOuter/orbit", 1.4, **MEDIUM),
            R("visual.churn", "particles/gasInner/orbit", 2.0, **MEDIUM))
    # melodic: the star's birthplace follows the latest note with no lag (it must be there when the burst fires),
    # and a chord spreads the birthplace into a cluster
    s.route(R("visual.orbit", "particles/stars/position", -9.0, comp=0, attackMs=0, decayMs=0),
            R("polyphony", "particles/stars/extent", 2.5, comp=0, **FAST),
            R("polyphony", "particles/stars/extent", 2.5, comp=2, **FAST),
            R("noteOn", "particles/stars/burst", 9.0, depth="lastVelocity", attackMs=0, decayMs=140))
    # kick: the photon ring flashes, the jets pulse
    s.route(R("kick", "fx/bh/photonRing", 22.0, **HIT),
            R("kick", "particles/jetUp/burst", 140.0, attackMs=0, decayMs=60),
            R("kick", "particles/jetDown/burst", 140.0, attackMs=0, decayMs=60),
            R("kick", "particles/jetUp/emissive", 5.0, attackMs=0, decayMs=500),
            R("kick", "particles/jetDown/emissive", 5.0, attackMs=0, decayMs=500),
            R("kick", "post/lens/chromaticAberration", 0.12, attackMs=0, decayMs=160))
    # snare: a flare from the hot spot
    s.route(R("snare", "particles/flare/burst", 260.0, attackMs=0, decayMs=40),
            R("snare", "post/bloom/intensity", 0.22, attackMs=0, decayMs=260))
    # hat: sparks
    s.route(R("hat", "particles/sparks/burst", 45.0, attackMs=0, decayMs=30))
    # tension: dissonance splits the lens's colours
    s.route(R("tension", "fx/bh/chroma", 0.03, **MEDIUM))

    s.params_({
        "post/bloom/intensity": 0.45, "post/bloom/threshold": 0.9, "post/bloom/emissionWeight": 0.8,
        "post/halation/enabled": True, "post/halation/intensity": 0.25, "post/halation/warmth": 0.6,
        "post/anamorphic/enabled": True, "post/anamorphic/intensity": 0.08, "post/anamorphic/stretch": 0.7,
        "post/lens/chromaticAberration": 0.035, "post/output/vignette": 0.55, "post/output/grain": 0.022,
        "post/grade/contrast": 1.12, "post/grade/saturation": 1.05,
        "camera/lens/focalLength": 38.0, "camera/exposure/compensation": -0.4,
        "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/aperture": 2.8,
        "camera/focus/mode": 0, "camera/lens/focusDistance": 40.0,
    })
    return s
