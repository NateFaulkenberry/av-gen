"""TESLA CHOIR (energy). Twelve Tesla towers stand in a ring round a humming core, one for each pitch class; every
chord is drawn in lightning, so harmony has a shape.

Composition (SCENE-CATALOG.md #3): a low wide view into a dark turbine hall. The core's top-load sits on the right
third; the towers recede round it; arcs leave the core for the towers of the notes that sound. The towers stand on
the circle of fifths, so a consonant chord draws a compact shape (a triad is a narrow triangle) and a cluster a
jagged fan. Sodium windows high on the far wall are the only warm light.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, place_bumps, sd_move, sd_diff, sd_cyl, sd_box, sd_polar, sd_union, \
    SLOW, MEDIUM, FAST, HIT, SNAP

ID = "tesla-choir"
TITLE = "Tesla Choir"

STEEL = "#0a0e14"
COPPER = "#6a3b22"
SODIUM = "#ff9a3c"
ARC = "#d6c8ff"

DESIGN = {
    "category": "energy",
    "thesis": "Twelve Tesla towers stand in a ring round a humming core, one for each pitch class; every chord is "
              "drawn in lightning, so harmony has a shape.",
    "composition": {
        "background": "a vast dark turbine hall: ribbed walls, sodium windows high up, ozone haze",
        "midground": "twelve copper coil towers on a circle (the circle of fifths) round the core",
        "foreground": "cables snaking across wet concrete toward the camera",
        "focal": "the core's top-load and the arcs leaving it, on the right third",
        "secondary": ["the tower crowns", "sparks", "the floor's sheen under each flash"],
        "atmosphere": "haze, so arcs and flashes light the air",
        "post": "bloom, a chromatic tick on discharges, grain, hard contrast",
        "camera": "low and wide; a 40-degree dolly round the ring over 110 s",
    },
    "palette": {"dominant": STEEL, "secondary": COPPER, "accent": SODIUM, "highlight": ARC,
                "background_value": "near black",
                "saturation": "the arcs are near-white; colour lives only in the copper and the sodium windows"},
    "motion": {
        "very_slow": ["the dolly", "the haze"],
        "medium": ["the core's hum (bass)", "the coils' glow"],
        "fast": ["arcs per note (writhing while held)"],
        "extremely_fast": ["the kick's discharge", "the snare's crackle", "sparks (hat)"],
    },
    "vocabulary": [
        ["melodic / chords", "response.note", "each held note arcs from the core to its tower; a "
         "triad draws a triangle of lightning, a cluster a jagged fan"],
        ["velocity", "notes.lastVelocity", "arc brightness and its light"],
        ["sustained", "notes.active", "a held note is a steady writhing arc; a staccato note one crack"],
        ["bass", "response.bass", "the core charges: its top-load glows and the hall hums brighter"],
        ["kick", "response.kick", "a discharge: bolts from the core to the ring, the hall flashes"],
        ["snare", "response.snare", "crackle crawls over the core"],
        ["hat", "response.hat", "sparks spit from the core's crown"],
        ["tension", "notes.tension", "dissonant chords make the arcs jagged and flickering"],
        ["timbre", "sonic.roughness", "arc jaggedness; arc colour from violet to blue-white"],
    ],
    "tier": "medium: haze march (24 steps live), twelve ribbon arcs, one SDF hall",
}

CORE_TOP = (0.0, 6.7, 0.0)
RING_R = 10.0
TOWER_TOP_Y = 4.25
FIFTHS = [0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5]   # tower i hosts pitch class FIFTHS[i]


def tower_top(i):
    a = math.radians(90.0 + 30.0 * i)
    return (RING_R * math.cos(a), TOWER_TOP_Y + 0.35, RING_R * math.sin(a))


def helix_coil(radius, height, turns, wire, sides=6):
    return {"kind": "tube", "tubeRadius": float(wire), "tubeTaper": 1.0, "tubeSides": int(sides),
            "tubeSegments": min(512, int(turns * 14)), "tubeTwist": 0.0, "tubeCaps": True,
            "curve": {"kind": "catmullRom", "generator": "helix", "radius": float(radius), "height": float(height),
                      "turns": float(turns), "count": int(turns * 12), "start": [0, 0, 0],
                      "samplesPerSegment": 6}}


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.0, "background": hexrgb("#010203"), "fogColor": scale3(hexrgb("#141a24"), 0.4),
        "volumeDensity": 0.0045, "volumeMaxDistance": 70.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": scale3(hexrgb("#0b0f16"), 0.2),
                "horizonColor": scale3(hexrgb("#121822"), 0.25), "groundColor": scale3(hexrgb("#07090c"), 0.2),
                "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "core", "position": list(CORE_TOP), "radius": 3.0, "weight": 1.0}],
        "layers": [
            {"name": "floor", "start": 0.0, "end": 12.0, "contrast": 1.0, "saturation": 1.0},
            {"name": "ring", "start": 12.0, "end": 34.0, "contrast": 1.08, "saturation": 1.0},
            {"name": "hall", "start": 34.0, "end": 200.0, "contrast": 0.8, "saturation": 0.75},
        ],
    }

    # ---- the hall: a ribbed drum of concrete, open to darkness above
    wall = sd_diff(sd_cyl(30.0, 34.0), sd_cyl(28.6, 36.0))
    ribs = sd_polar(28, sd_move((28.2, 0.0, 0.0), sd_box((0.9, 17.0, 0.7))))
    hall = sd_move((0.0, 17.0, 0.0), sd_union(wall, ribs))
    s.sdf("hall", hall, (-32, -1, -32), (32, 36, 32),
          material={"baseColor": hexrgb("#16181b"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                    "roughness": 0.85, "metallic": 0.0},
          look={"aoStrength": 0.5, "aoDistance": 1.2}, max_steps=96, epsilon=0.002, max_distance=80.0)
    # sodium windows: tall slits high in the wall, a few of them dark (a hall, not a pattern)
    s.proc("windows", {"kind": "box", "size": [2.0, 5.5, 0.4], "subdivisions": 1},
           distribution={"kind": "radial", "count": 14, "radius": 28.3, "orientation": "inward", "startAngle": 0.11},
           transform={"position": [0.0, 15.0, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb(SODIUM), "emissiveIntensity": 3.2,
                     "roughness": 1.0, "metallic": 0.0},
           material_variation={"emissiveRandom": 0.85}, variation={"seed": 3, "scale": [0.0, 0.25, 0.0]})
    # the floor: wet concrete, a faint sheen
    s.proc("floor", {"kind": "box", "size": [64.0, 0.2, 64.0], "subdivisions": 1},
           material={"baseColor": hexrgb("#08090a"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.55, "metallic": 0.0},
           transform={"position": [0.0, -0.1, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the core: plinth, coil, top-load
    concrete = {"baseColor": hexrgb("#2a2b2c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                "roughness": 0.8, "metallic": 0.0}
    copper = {"baseColor": hexrgb("#7a4528"), "emissiveColor": hexrgb("#ff8a50"), "emissiveIntensity": 0.0,
              "roughness": 0.35, "metallic": 1.0}
    load = {"baseColor": hexrgb("#9aa2aa"), "emissiveColor": hexrgb(ARC), "emissiveIntensity": 0.0,
            "roughness": 0.25, "metallic": 1.0}
    s.proc("corePlinth", {"kind": "cylinder", "radius": 1.5, "height": 1.0, "radialSegments": 48, "caps": True,
                          "bevel": 0.06}, material=concrete,
           transform={"position": [0.0, 0.5, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("coreCoil", helix_coil(0.85, 4.6, 34, 0.035), material=copper,
           transform={"position": [0.0, 1.05, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("coreColumn", {"kind": "cylinder", "radius": 0.78, "height": 4.7, "radialSegments": 40, "caps": True},
           material={"baseColor": hexrgb("#0e0f10"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.6, "metallic": 0.0},
           transform={"position": [0.0, 3.35, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("coreLoad", {"kind": "torus", "majorRadius": 1.5, "minorRadius": 0.48, "majorSegments": 96,
                        "minorSegments": 24}, material=load,
           transform={"position": list(CORE_TOP), "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the ring of towers (one node per part, radially)
    ring = {"kind": "radial", "count": 12, "radius": RING_R, "orientation": "outward", "startAngle": math.pi / 2}
    s.proc("towerPlinths", {"kind": "cylinder", "radius": 0.8, "height": 0.6, "radialSegments": 32, "caps": True,
                            "bevel": 0.04}, distribution=ring, material=concrete,
           transform={"position": [0.0, 0.3, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("towerCoils", helix_coil(0.42, 3.1, 24, 0.022), distribution=ring, material=copper,
           transform={"position": [0.0, 0.62, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("towerColumns", {"kind": "cylinder", "radius": 0.36, "height": 3.15, "radialSegments": 24, "caps": True},
           distribution=ring, material={"baseColor": hexrgb("#0e0f10"), "emissiveColor": [0, 0, 0],
                                        "emissiveIntensity": 0.0, "roughness": 0.6, "metallic": 0.0},
           transform={"position": [0.0, 2.2, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("towerLoads", {"kind": "torus", "majorRadius": 0.72, "minorRadius": 0.22, "majorSegments": 48,
                          "minorSegments": 16}, distribution=ring, material=load,
           transform={"position": [0.0, TOWER_TOP_Y, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    # cables from the core to each tower, lying on the floor
    s.proc("cables", {"kind": "tube", "tubeRadius": 0.06, "tubeTaper": 1.0, "tubeSides": 8, "tubeSegments": 40,
                      "tubeTwist": 0.0, "tubeCaps": True,
                      "curve": {"kind": "catmullRom", "generator": "noise", "count": 7, "start": [0, 0.06, 1.6],
                                "end": [0, 0.06, 9.2], "noiseAmount": 0.45, "noiseScale": 0.5, "seed": 3,
                                "samplesPerSegment": 6}},
           distribution={"kind": "radial", "count": 12, "radius": 0.0, "orientation": "outward",
                         "startAngle": math.pi / 2},
           material={"baseColor": hexrgb("#08090a"), "emissiveColor": hexrgb(ARC), "emissiveIntensity": 0.0,
                     "roughness": 0.4, "metallic": 0.0})

    # ---- the arcs: one per tower, from the core's crown to the tower's crown, dark until a note sounds
    for i in range(12):
        s.effect("arc%d" % i, "arc", ("world",), parameters={
            "strands": 2, "rate": 9.0, "jaggedness": 0.14, "sag": -0.12, "branchProbability": 0.07, "depth": 6,
            "reach": 10.0, "color": hexrgb(ARC), "intensity": 0.0, "coreWidth": 0.028, "glowIntensity": 0.0,
            "glowWidth": 0.32, "flicker": 0.45, "light": 0.0, "lightRange": 20.0, "seed": 11 + 7 * i,
            "source": {"kind": "world", "position": [CORE_TOP[0], CORE_TOP[1] + 0.2, CORE_TOP[2]]},
            "target": {"kind": "world", "position": list(tower_top(i))}})
    # ---- the kick's discharge from the core
    s.effect("discharge", "discharge", ("world",), trigger={"source": "signal", "name": signals.S("kick"),
                                                             "threshold": 0.25},
             parameters={"bolts": 3, "radius": 4.2, "downward": 0.9, "boltDuration": 0.16, "jaggedness": 0.3,
                         "branchProbability": 0.3, "color": hexrgb(ARC), "intensity": 30.0,
                         "flashIntensity": 900.0, "coreWidth": 0.035, "glowIntensity": 1.2, "glowWidth": 0.4,
                         "flashRange": 26.0, "sparkCount": 16, "sparkSpeed": 10.0, "sparkLife": 0.6,
                         "sparkIntensity": 12.0, "seed": 5,
                         "source": {"kind": "world", "position": list(CORE_TOP)}})
    # ---- the snare's crackle over the core
    s.entity("coreLoad", tags=["core"])
    s.effect("crackle", "electricField", ("entity", "coreLoad"), parameters={
        "crackle": 0.0, "crackleScale": 6.0, "crackleSpeed": 3.0, "coverage": 0.5, "arcCount": 3, "arcRate": 9.0,
        "hopDistance": 0.6, "color": hexrgb(ARC), "intensity": 0.0, "coreWidth": 0.02, "glowWidth": 0.25,
        "glowIntensity": 0.0, "seed": 3})

    # ---- sparks from the core's crown (hat)
    s.particles("sparks", capacity=3000, seed=31, shape="sphere", position=list(CORE_TOP), extent=[1.4, 0.3, 1.4],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.4, lifetimeMax=1.1, spread=0.9, speedMin=2.0,
                speedMax=5.0, gravity=[0, -9.0, 0], drag=0.4, turbulence=0.0, sizeStart=0.025, sizeEnd=0.0,
                colorStart=hexrgb("#f4eeff") + [1.0], colorEnd=hexrgb("#ffb070") + [0.0], emissive=14.0,
                blend="additive", velocityStretch=1.2, stretchMax=0.35, collision="bounce", collisionHeight=0.0,
                collisionRestitution=0.25)
    s.particles("dust", capacity=3000, seed=37, shape="box", position=[0.0, 6.0, 0.0], extent=[14.0, 6.0, 14.0],
                direction=[0, 1, 0], spawnRate=120.0, lifetimeMin=8.0, lifetimeMax=14.0, spread=1.0,
                speedMin=0.0, speedMax=0.05, gravity=[0, 0.01, 0], drag=0.5, turbulence=0.15,
                turbulenceScale=0.3, sizeStart=0.012, sizeEnd=0.012, colorStart=hexrgb("#8090a0") + [0.5],
                colorEnd=hexrgb("#8090a0") + [0.0], emissive=0.35, blend="additive")

    # ---- light: the core's glow (bass), sodium from the windows, a cold flash from the arcs themselves
    s.light("coreGlow", "point", position=list(CORE_TOP), color=hexrgb("#b8a8ff"), intensity=20.0, range=22.0,
            radius=1.0, castsShadow=False, volumetric=0.7)
    for k, (x, z) in enumerate(((-17.0, -21.0), (19.0, -19.0), (-24.0, 6.0))):
        s.light("sodium%d" % k, "point", position=[x, 14.0, z], color=hexrgb(SODIUM), intensity=140.0,
                range=24.0, radius=2.0, castsShadow=False, volumetric=0.55)
    s.light("fill", "directional", direction=[0.3, -1.0, 0.2], color=hexrgb("#5a6a80"), intensity=0.03,
            castsShadow=False)

    # ---- camera: low and wide, the core on the right third
    s.camera["fov"] = 55.0
    s.arc_camera((0.0, 3.0, 0.0), radius=23.0, height=7.6, period=110.0, centre_deg=28.0, sweep_deg=40.0,
                 steps=16, side=4.6, lift=0.2, height_bob=0.2)

    # ---- the instrument ---------------------------------------------------------------------------------------------
    s.map(M("anyHeld", [("active", 1.0)], "mean"))
    s.map(M("jag", [("tension", 0.6), ("roughness", 0.8)], "max"))
    if signals.ENGINE == "response":
        # the engineer's pitch-class lanes: tower i answers pitch class FIFTHS[i] for as long as it is held
        for i in range(12):
            s.map(M("arc%d" % i, [("notes.class.%d" % FIFTHS[i], 1.0)], "mean"))
    else:
        # fallback before the lanes land: towers by register, round the ring (one arc for the latest note)
        centres = [0.30 + 0.033 * i for i in range(12)]
        s.places("tw", "lastPitch", centres, 0.033)
        for i in range(12):
            s.map3(M("arc%d" % i, [("visual.tw%d" % i, 1.0), ("visual.anyHeld", 1.0)], "min"))
    for i in range(12):
        src = "visual.arc%d" % i
        s.route(R(src, "fx/arc%d/intensity" % i, 42.0, attackMs=8, decayMs=140),
                R(src, "fx/arc%d/glowIntensity" % i, 1.3, attackMs=8, decayMs=160),
                R(src, "fx/arc%d/light" % i, 320.0, attackMs=8, decayMs=140),
                R("visual.jag", "fx/arc%d/jaggedness" % i, 0.25, **MEDIUM),
                R("visual.jag", "fx/arc%d/flicker" % i, 0.4, **MEDIUM))
    # bass: the core charges
    s.route(R("bass", "procedural/coreLoad/material/emissive", 3.0, attackMs=50, decayMs=700),
            R("bass", "lights/coreGlow/intensity", 150.0, attackMs=50, decayMs=700),
            R("bass", "procedural/coreCoil/material/emissive", 1.6, attackMs=50, decayMs=700),
            R("bass", "procedural/cables/material/emissive", 0.4, attackMs=50, decayMs=900))
    # snare: crackle over the core
    s.route(R("snare", "fx/crackle/intensity", 60.0, attackMs=0, decayMs=240),
            R("snare", "fx/crackle/crackle", 14.0, attackMs=0, decayMs=240),
            R("snare", "fx/crackle/glowIntensity", 3.0, attackMs=0, decayMs=240))
    # hat: sparks from the crown
    s.route(R("hat", "particles/sparks/burst", 34.0, attackMs=0, decayMs=30))
    # kick: the discharge fires on its own trigger; the hall answers with a cold flash in the haze and a lens tick
    s.route(R("kick", "lights/fill/intensity", 0.6, attackMs=0, decayMs=180),
            R("kick", "post/lens/chromaticAberration", 0.1, attackMs=0, decayMs=150))
    # timbre: bright sounds whiten the arcs
    for i in range(12):
        s.route(R("brightness", "fx/arc%d/color" % i, -0.3, comp=0, **MEDIUM))
    s.route(R("visual.anyHeld", "post/bloom/intensity", 0.12, **FAST))

    s.params_({
        "post/bloom/intensity": 0.42, "post/bloom/threshold": 0.9, "post/bloom/emissionWeight": 0.8,
        "post/halation/enabled": True, "post/halation/intensity": 0.12, "post/halation/warmth": 0.4,
        "post/lens/chromaticAberration": 0.02, "post/output/vignette": 0.5, "post/output/grain": 0.03,
        "post/grade/contrast": 1.18, "post/grade/saturation": 0.95,
        "camera/lens/focalLength": 30.0, "camera/exposure/compensation": 0.0,
        "scene/volumeAnisotropy": 0.35,
    })
    # ---- the evaluator's screen regions, projected through the camera at t = 0, and the performer's baseline
    s.region("core", centre=list(CORE_TOP), radius=2.5)
    s.region_ring("towers", [0.0, TOWER_TOP_Y, 0.0], RING_R)
    s.region("background", box=[0.0, 0.0, 1.0, 0.22])
    s.response = {"sensitivity": 0.5, "transient": 0.65, "sustain": 0.45, "attack": 0.7, "release": 0.8}
    return s
