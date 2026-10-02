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
# The disk: fourteen thin flat bands from the innermost stable orbit (3 m) to 15 m, touching edge to edge, each turned
# at its own Keplerian rate (a twist with speed only; ADR-1021 made a full turn safe), so the gas shears as it
# turns. One program draws them all: the light lives in streaks that run ALONG the orbit (noise sampled in polar
# coordinates: slow round the circle, fast across the radius), hot white-gold inside, ember and deep red outside.
R_IN, R_OUT, N_BANDS = 3.0, 15.0, 14
SPIN_IN = 46.0  # degrees per second at the inner edge; r^-1.5 outward


def bands():
    step = (R_OUT - R_IN) / N_BANDS
    out = []
    for i in range(N_BANDS):
        r = R_IN + step * (i + 0.5)
        out.append((r, step * 0.5, SPIN_IN * (R_IN + step * 0.5) ** 1.5 / r ** 1.5))
    return out


def disk_program(r_max=R_OUT, angular=1.7, radial=11.0):
    """Polar streaks. The angle has no op, so the unit direction (x/r, z/r) stands for it: noise of
    (A x/r, B r/Rmax, A z/r) varies slowly round the circle and fast across the radius."""
    hot, mid, cool = hexrgb(WHITE_HOT, 1.6), hexrgb(EMBER, 0.9), hexrgb(DEEP_RED, 0.35)
    return {
        "name": "ehDisk",
        "ops": [
            {"kind": "input", "dst": 0, "input": "localPosition"},
            {"kind": "multiply", "dst": 1, "srcA": 0, "srcB": 0},
            {"kind": "gradient", "dst": 2, "srcA": 1, "value": 1.0 / (r_max * r_max), "constant": [1.0, 0.0, 1.0, 0.0]},
            {"kind": "power", "dst": 2, "srcA": 2, "value": 0.5},                       # r / Rmax
            {"kind": "power", "dst": 3, "srcA": 2, "value": -1.0},                      # Rmax / r
            {"kind": "multiply", "dst": 3, "srcA": 0, "srcB": 3},                       # Rmax (x, y, z) / r
            {"kind": "constant", "dst": 4, "constant": [angular / r_max, 0.0, angular / r_max, 0.0]},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},                       # A (x/r, 0, z/r)
            {"kind": "constant", "dst": 4, "constant": [0.0, radial, 0.0, 0.0]},
            {"kind": "multiply", "dst": 4, "srcA": 2, "srcB": 4},                       # (0, B r/Rmax, 0)
            {"kind": "add", "dst": 3, "srcA": 3, "srcB": 4},                            # the polar point
            {"kind": "noise", "dst": 5, "srcA": 3, "value": 1.0, "seed": 7},
            {"kind": "noise", "dst": 6, "srcA": 3, "value": 2.9, "seed": 13},
            {"kind": "mix", "dst": 5, "srcA": 5, "srcB": 6, "value": 0.38},
            {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.3, 0.78, 0.0, 0.0]},
            {"kind": "remap", "dst": 5, "srcA": 5, "value": 1, "constant": [0.0, 1.0, 0.12, 1.3]},  # streaks
            {"kind": "remap", "dst": 6, "srcA": 2, "value": 1, "constant": [R_IN / r_max, 1.0, 0.0, 1.0]},  # t
            {"kind": "ramp", "dst": 7, "srcA": 6, "constant": hot + [1.0], "constant2": mid + [1.0],
             "constant3": cool + [1.0]},
            {"kind": "remap", "dst": 6, "srcA": 6, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},  # 1 - t
            {"kind": "power", "dst": 6, "srcA": 6, "value": 1.8},
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 6},
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 5},
            {"kind": "input", "dst": 0, "input": "materialEmission"},
            {"kind": "swizzle", "dst": 0, "srcA": 0, "constant": [0.0, 0.0, 0.0, 0.0]},  # the routed gain
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 0},
            {"kind": "constant", "dst": 1, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 1.0, "opacity": -1,
    }


def nebula_program():
    """A dim matte-painted nebula far behind the hole, for the lens to bend: warped fBm through an indigo-violet-rose
    ramp, cut by dark dust lanes. Kept at a low value so the disk owns the frame."""
    return {
        "name": "ehNebula",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.006, "seed": 21},
            {"kind": "constant", "dst": 2, "constant": [90.0, 140.0, 60.0, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "add", "dst": 1, "srcA": 0, "srcB": 1},                          # a warped point
            {"kind": "noise", "dst": 3, "srcA": 1, "value": 0.0045, "seed": 5},       # the gas
            {"kind": "noise", "dst": 4, "srcA": 1, "value": 0.016, "seed": 9},        # the dust lanes
            {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.38, 0.82, 0.0, 0.0]},
            {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.42, 0.62, 0.0, 0.0]},
            {"kind": "remap", "dst": 4, "srcA": 4, "value": 1, "constant": [0.0, 1.0, 1.0, 0.15]},
            {"kind": "multiply", "dst": 5, "srcA": 3, "srcB": 4},
            {"kind": "ramp", "dst": 6, "srcA": 5, "constant": hexrgb("#05040c") + [1.0],
             "constant2": hexrgb("#2a1a4a", 0.9) + [1.0], "constant3": hexrgb("#a0607a", 0.8) + [1.0]},
            {"kind": "input", "dst": 7, "input": "materialEmission"},
            {"kind": "swizzle", "dst": 7, "srcA": 7, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
            {"kind": "constant", "dst": 0, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": -1,
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
    s.program(disk_program())
    s.program(nebula_program())

    # ---- the disk
    for i, (r, w, spin) in enumerate(bands()):
        s.proc("band%d" % i,
               {"kind": "torus", "majorRadius": round(r, 4), "minorRadius": round(w * 1.02, 4),
                "majorSegments": 256, "minorSegments": 8},
               material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": 6.0,
                         "roughness": 1.0, "metallic": 0.0, "program": "ehDisk"},
               deformers=[{"kind": "twist", "amount": 0.0, "speed": round(spin * 3.14159265 / 180.0, 5),
                           "axis": [0, 1, 0]},
                          {"kind": "noise", "amount": 0.0, "scale": 0.35, "speed": 0.6, "seed": 40 + i,
                           "axisMask": [0, 1, 0], "space": "world"}],
               transform={"position": [0, 0.0015 * (i % 3), 0], "rotation": [0, 0, 0], "scale": [1, 0.035, 1]})

    # ---- the far sky: a matte-painted nebula plane far behind the hole (the lens bends it)
    s.proc("nebula", {"kind": "box", "size": [1000.0, 700.0, 2.0], "subdivisions": 1},
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": 0.1,
                     "roughness": 1.0, "metallic": 0.0, "program": "ehNebula"},
           transform={"position": [-60.0, 40.0, -520.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the hole: a black-hole lens with a thin photon ring
    s.effect("bh", "gravLens", ("world",), parameters={
        "einsteinRadius": 6.0, "mode": 1, "horizonScale": 0.42, "chroma": 0.003,
        "ringColor": hexrgb("#ffe2b8"), "photonRing": 4.0, "ringWidth": 0.045, "falloffRadius": 3.2,
        "feather": 0.55, "offsetX": BH[0], "offsetY": BH[1], "offsetZ": BH[2]})
    # ---- the sky
    s.effect("stars", "stars", ("world",), parameters={
        "brightness": 6.0, "density": 0.08, "magnitudeSlope": 5.5, "colorSpread": 0.55, "twinkle": 0.1,
        "twinkleRate": 3.0, "horizonFade": 0.05, "band": 1.8, "bandTilt": 0.45, "daylight": 0.0})
    # ---- jets: particle streams leaving the poles, faint until a kick
    for jid, sign in (("jetUp", 1.0), ("jetDown", -1.0)):
        s.particles(jid, capacity=6000, seed=61 if sign > 0 else 67, shape="sphere", position=[0, 0.6 * sign, 0],
                    extent=[0.1, 0.1, 0.1], direction=[0, sign, 0], spawnRate=0.0, lifetimeMin=1.2,
                    lifetimeMax=2.4, spread=0.01, speedMin=16.0, speedMax=24.0, gravity=[0, 0, 0], drag=0.05,
                    turbulence=0.05, turbulenceScale=0.2, sizeStart=0.02, sizeEnd=0.05, sizeVariance=0.4,
                    colorStart=hexrgb("#cfe2ff") + [0.5], colorEnd=hexrgb(JET_BLUE) + [0.0], emissive=0.35,
                    blend="additive", velocityStretch=2.5, stretchMax=2.5)

    # ---- the gas: matter spiralling in, heating as it falls. Two populations: the outer disk (born ember-red,
    # dying gold) and the inner (gold to white-hot). Additive and streaked along its orbit, so the disk is light.
    s.particles("gasInner", capacity=40000, seed=19, shape="disc", position=[0, 0, 0], extent=[7.0, 0.0, 7.0],
                direction=[0, 1, 0], spawnRate=9000.0, lifetimeMin=1.6, lifetimeMax=3.4, spread=1.0,
                speedMin=0.0, speedMax=0.02, gravity=[0, 0, 0], drag=0.5, turbulence=0.02,
                turbulenceScale=0.5, turbulenceSpeed=0.5, attractorPosition=[0, 0, 0], attractorStrength=0.55,
                attractorRadius=8.0, orbit=4.5, sizeStart=0.02, sizeEnd=0.014, sizeVariance=0.5, sizeSkew=2.0,
                colorStart=hexrgb(GOLD) + [0.0], colorEnd=hexrgb(WHITE_HOT) + [0.6], emissive=1.2,
                blend="additive", velocityStretch=0.5, stretchMax=0.35)
    # the approaching side is brighter (Doppler beaming: the disk turns towards the camera on the right)
    s.particles("beaming", capacity=16000, seed=23, shape="disc", position=[5.2, 0.02, 1.0], extent=[3.2, 0.0, 4.2],
                direction=[0, 1, 0], spawnRate=5000.0, lifetimeMin=0.8, lifetimeMax=1.8, spread=1.0,
                speedMin=0.0, speedMax=0.02, gravity=[0, 0, 0], drag=0.5, turbulence=0.02,
                attractorPosition=[0, 0, 0], attractorStrength=0.5, attractorRadius=12.0, orbit=4.0,
                sizeStart=0.022, sizeEnd=0.014, sizeVariance=0.5, sizeSkew=2.0,
                colorStart=hexrgb(WHITE_HOT) + [0.0], colorEnd=hexrgb(GOLD) + [0.5], emissive=1.6,
                blend="additive", velocityStretch=0.4, stretchMax=0.3)
    # ---- note-stars: born where the note says, spiralling in with a trail
    s.particles("stars", capacity=3000, seed=29, shape="sphere", position=[13.0, 0.0, 0.0], extent=[0.25, 0.25, 0.25],
                direction=[0, 0, -1], spawnRate=0.0, lifetimeMin=2.5, lifetimeMax=4.0, spread=0.15,
                speedMin=2.2, speedMax=3.0, gravity=[0, 0, 0], drag=0.15, turbulence=0.05,
                attractorPosition=[0, 0, 0], attractorStrength=1.6, attractorRadius=18.0, orbit=3.8,
                sizeStart=0.11, sizeEnd=0.03, colorStart=hexrgb("#dfe9ff") + [1.0],
                colorEnd=hexrgb(WHITE_HOT) + [0.0], emissive=22.0, blend="additive",
                trailEnabled=True, trailLength=24, trailStride=2, trailWidth=0.8, trailTaper=0.05, trailFade=0.0)
    # ---- the flare (snare): a burst from the hot spot on the approaching side
    s.particles("flare", capacity=4000, seed=41, shape="sphere", position=[4.6, 0.05, 1.2], extent=[0.35, 0.05, 0.35],
                direction=[0, 0, 1], spawnRate=0.0, lifetimeMin=0.35, lifetimeMax=0.9, spread=0.18,
                speedMin=4.0, speedMax=9.0, gravity=[0, 0, 0], drag=0.6, turbulence=0.2, turbulenceScale=0.8,
                attractorPosition=[0, 0, 0], attractorStrength=1.2, attractorRadius=12.0, orbit=6.0,
                sizeStart=0.06, sizeEnd=0.0, colorStart=hexrgb(WHITE_HOT) + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=10.0, blend="additive", velocityStretch=1.2, stretchMax=0.8)
    # ---- sparks (hat): glints scattered over the disk, a few frames each
    s.particles("sparks", capacity=2000, seed=53, shape="disc", position=[0, 0.05, 0], extent=[11.0, 0.0, 11.0],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.08, lifetimeMax=0.22, spread=1.0,
                speedMin=0.0, speedMax=0.2, gravity=[0, 0, 0], drag=0.0, turbulence=0.0,
                sizeStart=0.05, sizeEnd=0.0, colorStart=hexrgb("#ffffff") + [1.0], colorEnd=hexrgb(GOLD) + [0.0],
                emissive=16.0, blend="additive")

    # ---- debris for scale: dark fragments, the near one out of focus, rimmed by the disk
    rock = {"baseColor": hexrgb("#1a1714"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
            "roughness": 0.85, "metallic": 0.0}
    for name, pos, rad, rot, seed in (("rockNear", (-9.5, -0.9, 17.8), 1.5, (20, 35, 10), 5),
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
    s.arc_camera(BH, radius=34.0, height=2.6, period=160.0, centre_deg=-8.0, sweep_deg=32.0, steps=16,
                 side=6.0, lift=1.4, height_bob=0.35)

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
    # heat: sustained sound brightens the disk and thickens its gas (the whole disk, slowly)
    for i in range(N_BANDS):
        s.route(R("visual.heat", "procedural/band%d/material/emissive" % i, 5.0, **SLOW))
    s.route(R("visual.heat", "particles/gasInner/spawnRate", 2200.0, **SLOW),
            R("visual.heat", "particles/gasInner/emissive", 3.0, **SLOW),
            R("visual.heat", "particles/beaming/emissive", 3.0, **SLOW))
    # timbre: a bright sound turns the hot inner edge from gold-white to blue-white (the ramp's hot stop)
    hot_stop = "material/ehDisk/op/18/ramp/constant"
    s.route(R("brightness", hot_stop, -0.35, comp=0, **MEDIUM),
            R("brightness", hot_stop, 0.15, comp=1, **MEDIUM),
            R("brightness", hot_stop, 0.9, comp=2, **MEDIUM))
    # roughness: the disk's streaks break into clumps and the gas boils
    s.route(R("visual.grit", "material/ehDisk/op/12/noise/value", 1.6, **FAST),
            R("visual.grit", "particles/gasInner/turbulence", 1.0, **FAST))
    for i in range(N_BANDS):
        s.route(R("visual.grit", "procedural/band%d/deform/2/amount" % i, 0.12, **FAST))
    # the playing: busy notes churn the disk -- finer, more numerous streaks across the radius
    s.route(R("visual.churn", "material/ehDisk/op/9/constant/constant", 22.0, comp=1, **MEDIUM),
            R("visual.churn", "particles/gasInner/orbit", 2.0, **MEDIUM))
    # melodic: the star's birthplace follows the latest note with no lag (it must be there when the burst fires),
    # and a chord spreads the birthplace into a cluster
    s.route(R("visual.orbit", "particles/stars/position", -9.0, comp=0, attackMs=0, decayMs=0),
            R("polyphony", "particles/stars/extent", 2.5, comp=0, **FAST),
            R("polyphony", "particles/stars/extent", 2.5, comp=2, **FAST),
            R("noteOn", "particles/stars/burst", 9.0, depth="lastVelocity", attackMs=0, decayMs=140))
    # kick: the photon ring flashes, the jets pulse
    s.route(R("kick", "fx/bh/photonRing", 22.0, **HIT),
            R("kick", "particles/jetUp/burst", 70.0, attackMs=0, decayMs=90),
            R("kick", "particles/jetDown/burst", 70.0, attackMs=0, decayMs=90),
            R("kick", "particles/jetUp/emissive", 1.2, attackMs=0, decayMs=500),
            R("kick", "particles/jetDown/emissive", 1.2, attackMs=0, decayMs=500),
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
        "post/lens/chromaticAberration": 0.02, "post/output/vignette": 0.55, "post/output/grain": 0.022,
        "post/grade/contrast": 1.12, "post/grade/saturation": 1.05,
        "camera/lens/focalLength": 38.0, "camera/exposure/compensation": -0.4,
        "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/aperture": 2.8,
        "camera/focus/mode": 0, "camera/lens/focusDistance": 34.0,
    })
    # ---- the evaluator's screen regions, projected through the camera at t = 0, and the performer's baseline
    s.region("horizon", centre=list(BH), radius=4.5)
    s.region_ring("disk", list(BH), 15.0)
    s.region_ring("lensed", list(BH), 8.0, axis="z")
    s.region("background", box=[0.0, 0.0, 1.0, 0.2])
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.55, "attack": 1.2, "release": 1.5}
    return s
