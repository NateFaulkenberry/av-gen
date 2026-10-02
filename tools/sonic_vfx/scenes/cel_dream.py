"""3. CEL-SHADED DREAM WORLD: CANDY ARCHIPELAGO (04-brief-abstract-direction.md, direction 3; ABSTRACT-PLAN.md section 3).

A floating island in a candy sky: lollipop trees, cone trees, one giant flower, a stone ring half-sunk in the lawn,
three round creatures who look at you and sing the notes, a waterfall falling off the edge into the void, smaller
islands drifting behind. Deliberately low-poly toy forms, two flat shade bands, violet graphic shadows, plum outlines.

Grammar: every object is a few primitives with low segment counts (a sphere of 12 by 8 is a creature, a cone of 7 sides
is a tree); colour is flat (cel lighting, ADR-1071: two lit tones over a lavender shadow tone, a candy rim); every edge
is inked (the screen-space outline, ADR-1072). Nothing is textured.

The creatures are parented rigs: a body node with its eyes, pupils, feet and sprout as children, so a route on the
body's node scale squashes the whole creature (eyes included) and one on its position makes it hop.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "cel-dream"
TITLE = "Cel-Shaded Dream World"

SKY_TOP = "#ff8fc0"
SKY_HORIZON = "#ffd6b4"
VOID = "#c4a6ff"
LAWN = "#8ff0c0"
ROCK = "#b9a3ff"
ROCK_DARK = "#8f78e8"
PINK = "#ff7eb6"
BUTTER = "#ffe27a"
CORAL = "#ff8a7a"
LILAC = "#c7a6ff"
CREAM = "#fff3df"
MINT_DARK = "#3fc49a"
CLOUD = "#fff6fa"
PLUM = "#3d1d4f"
SHADOW = (0.62, 0.5, 0.95)

DESIGN = {
    "category": "cel-shaded",
    "thesis": "Candy Archipelago: a toy island floating in a candy sky, inked and cel-shaded, where three round "
              "creatures sing the notes, hop on the kick and the giant flower blooms when the sound is held.",
    "composition": {
        "background": "a peach-to-pink sky with flat pastel clouds; small islands drifting behind",
        "midground": "the island: a mint lawn on a lilac rock, lollipop and cone trees, the giant flower, the ring",
        "foreground": "three round creatures in a row on the lawn's front edge, looking at the camera",
        "focal": "the creatures, with the giant flower above them",
        "secondary": ["the giant flower", "the waterfall", "the far islands"],
        "atmosphere": "(none: flat colour)",
        "post": "plum outlines, a soft bloom on the rims",
        "camera": "a slow arc round the diorama from slightly above",
    },
    "palette": {"dominant": SKY_HORIZON, "secondary": LAWN, "accent": CORAL, "highlight": BUTTER,
                "background_value": "light", "saturation": "pastel pushed to candy; plum ink"},
    "motion": {
        "very_slow": ["the camera's arc", "the clouds' drift"],
        "medium": ["the flower's bloom", "the island's bob"],
        "fast": ["the creatures singing"],
        "extremely_fast": ["the creatures' hop on the kick"],
    },
    "vocabulary": [
        ["kick", "response.kick", "the creatures hop"],
        ["bass", "response.bass", "the creatures squash and stretch; the island bobs"],
        ["snare", "response.snare", "the flower's petals flick; a confetti puff"],
        ["hat", "response.hat", "sparkles round the flower"],
        ["note", "notes.lastPitch", "the creature at the pitch's place sings (low left, high right)"],
        ["sustained", "response.sustain", "the giant flower blooms"],
        ["brightness", "sonic.brightness.slow", "the time of day: noon pastel to lavender dusk"],
        ["silence", "(no input)", "the island floats; the creatures breathe"],
    ],
    "tier": "light: about 45 low-poly procedural nodes with cel lighting, the outline pass, one particle system",
}


def toon(hexc, bands=2, rim=0.0, spec=0.0, ambient=0.62, softness=0.015, terminator=0.05, emissive=0.0):
    return {"baseColor": hexrgb(hexc), "emissiveColor": hexrgb(hexc), "emissiveIntensity": float(emissive),
            "roughness": 0.6, "metallic": 0.0,
            "toon": {"bands": bands, "softness": softness, "terminator": terminator,
                     "shadowColor": list(SHADOW), "ambient": ambient, "rimWidth": rim,
                     "rimColor": hexrgb("#fff0fa"), "rimIntensity": 0.9 if rim else 0.0,
                     "specular": spec, "specularSize": 0.12}}


def sphere(r, seg=12, rings=8):
    return {"kind": "sphere", "radius": float(r), "segments": int(seg), "rings": int(rings)}


def cyl(r, h, seg=8):
    return {"kind": "cylinder", "radius": float(r), "height": float(h), "radialSegments": int(seg), "caps": True}


def at(x, y, z):
    return {"position": [float(x), float(y), float(z)], "rotation": [0, 0, 0], "scale": [1, 1, 1]}


def island(s, name, centre, size, lawn=LAWN, rock=ROCK):
    """A floating island: a faceted lawn disc on an inverted faceted cone of rock."""
    cx, cy, cz = centre
    s.proc(name + "Lawn", cyl(9.0 * size, 1.1 * size, 14), material=toon(lawn, rim=0.0),
           transform=at(cx, cy - 0.55 * size, cz))
    prof = [(-10.0, 0.4), (-7.2, 2.6), (-4.2, 5.4), (-2.0, 7.6), (-1.0, 8.6)]
    s.proc(name + "Rock", kit.lathe([(h * size, r * size) for h, r in prof], sides=9, samples=4),
           material=toon(rock, ambient=0.55), transform=at(cx, cy, cz))


def creature(s, name, pos, body_col, facing=0.0, scale=1.0):
    """A round creature: the body node, and its eyes, pupils, feet and sprout as children (node-parented)."""
    s.proc(name, sphere(0.85, 14, 10), material=toon(body_col, rim=0.25, spec=0.6),
           position=pos, rotation=(0.0, facing, 0.0), scale=(scale, scale * 1.12, scale))
    for side in (-1.0, 1.0):
        sx = 0.32 * side
        s.proc("%sEye%s" % (name, "L" if side < 0 else "R"), sphere(0.24, 12, 8), material=toon(CREAM, ambient=0.8),
               position=(sx, 0.28, 0.66), parent=name)
        s.proc("%sPupil%s" % (name, "L" if side < 0 else "R"), sphere(0.12, 10, 6),
               material=toon("#2d1640", bands=1, ambient=0.9, spec=1.0), position=(sx, 0.3, 0.86), parent=name)
        s.proc("%sFoot%s" % (name, "L" if side < 0 else "R"), sphere(0.24, 10, 6),
               material=toon(body_col, ambient=0.55), position=(0.38 * side, -0.78, 0.18), scale=(1.0, 0.5, 1.3),
               parent=name)
    s.proc(name + "Mouth", sphere(0.17, 12, 6), material=toon("#4a1a3a", bands=1, ambient=0.9),
           position=(0.0, 0.02, 0.8), scale=(1.0, 0.08, 0.45), parent=name)
    s.proc(name + "Sprout", cyl(0.05, 0.5, 6), material=toon(MINT_DARK), position=(0.0, 0.98, 0.0), parent=name)
    s.proc(name + "Bud", sphere(0.16, 10, 6), material=toon(PINK, rim=0.3), position=(0.0, 1.28, 0.0), parent=name)


CREATURES = ("blob1", "blob2", "blob3")
EXTRA = ("blob4", "blob5")
RAINBOW = ["#ff9aa8", "#ffc38a", "#fff09a", "#a8f0b8", "#9ad4ff", "#c8a8ff"]


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 3)."""
    N = "nodes/%s/"
    # ---- KICK: the creatures hop, a ripple left to right, landing on a spring; velocity sets the height
    for k, name in enumerate(CREATURES + EXTRA):
        s.route(R("kick", N % name + "position", 0.75, comp=1, attackMs=0, decayMs=240, delayMs=70 * k,
                  springHz=2.4, springDamping=0.45))
    # ---- BASS: squash and stretch (wider and lower on the bass); the camera bobs
    for name in CREATURES + EXTRA:
        s.route(R("bass", N % name + "scale", -0.16, comp=1, attackMs=20, decayMs=260),
                R("bass", N % name + "scale", 0.1, comp=0, attackMs=20, decayMs=260),
                R("bass", N % name + "scale", 0.1, comp=2, attackMs=20, decayMs=260))
    s.route(R("bass", "camera/position", 0.35, comp=1, attackMs=60, decayMs=500))
    # ---- SNARE: the flower's petals flick open; a confetti puff from its heart
    s.route(R("snare", "procedural/petals/transform/scale", 0.22, attackMs=0, decayMs=260, springHz=3.0,
              springDamping=0.4),
            R("snare", "particles/confetti/burst", 140.0, threshold="binary", thresholdLevel=0.05))
    # ---- HATS: sparkles round the flower
    s.route(R("hat", "particles/sparkle/burst", 18.0, threshold="binary", thresholdLevel=0.05),
            R("audio.treble", "particles/sparkle/spawnRate", 60.0, attackMs=30, decayMs=300))
    # ---- MIDS: the clouds sway; the creatures turn their heads
    for k in range(4):
        s.route(R("audio.mid", "procedural/cloud%d/transform/position" % k, 2.4 * (1 if k % 2 else -1), comp=0,
                  attackMs=300, decayMs=1200))
    for k, name in enumerate(CREATURES):
        s.route(R("audio.mid", N % name + "rotation", (-22.0, 10.0, 26.0)[k], comp=1, attackMs=200, decayMs=800))
    # ---- CENTROID: the time of day (a bright timbre is a blue-pink noon, a dark one a lavender dusk)
    s.route(R("brightnessSlow", "env/sky/zenithColor", -0.35, comp=0, **SLOW),
            R("brightnessSlow", "env/sky/zenithColor", 0.15, comp=1, **SLOW),
            R("brightnessSlow", "env/sky/zenithColor", 0.3, comp=2, **SLOW),
            R("brightnessSlow", "lights/sun/intensity", 1.0, **SLOW))
    # ---- SUSTAIN: the giant flower blooms (its petals spread)
    s.route(R("sustain", "procedural/petals/distribution/radius", 0.6, **SLOW),
            R("sustain", "procedural/heart/transform/scale", 0.25, **SLOW))
    # ---- TEMPO: everyone bobs on the beat
    for name in CREATURES + EXTRA:
        s.route(R("beat", N % name + "position", 0.1, comp=1, attackMs=0, decayMs=200))
    # ---- INTENSITY: STRUCTURE -- two more creatures join as the piece builds
    for name in EXTRA:
        s.route(R("intensity", N % name + "scale", 1.0, threshold="binary", thresholdLevel=0.5, attackMs=300,
                  decayMs=900, springHz=1.6, springDamping=0.5))
    # ---- MIDI: the creature at the pitch's place sings (low left, high right): its mouth opens and it lifts;
    # STRUCTURE: as many lawn flowers bloom as there are notes in the chord; a held note raises the rainbow
    for k, name in enumerate(CREATURES):
        hit = "visual.singHit%d" % k
        s.route(R(hit, N % (name + "Mouth") + "scale", 0.9, comp=1, attackMs=0, decayMs=420),
                R(hit, N % name + "position", 0.35, comp=1, depth="lastVelocity", attackMs=0, decayMs=380))
    for node in ("tulipStems", "tulips"):
        s.route(R("polyphony", "procedural/%s/distribution/count" % node, 16.0, attackMs=0, decayMs=900))
    for k in range(len(RAINBOW)):
        s.route(R("held", "procedural/rainbow%d/transform/scale" % k, 1.0, delayMs=60 * k, **MEDIUM))
    # ---- MOD WHEEL: day to night
    wheel = s.modwheel()
    s.route(R(wheel, "lights/sun/intensity", -2.4, attackMs=80, decayMs=80),
            R(wheel, "env/sky/zenithColor", 1.0, op="multiply", gain=-0.75, offset=1.0, attackMs=80, decayMs=80),
            R(wheel, "env/sky/horizonColor", 1.0, op="multiply", gain=-0.6, offset=1.0, attackMs=80, decayMs=80))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.0,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    s.environment = {
        "intensity": 0.6, "background": hexrgb(SKY_HORIZON), "fogColor": hexrgb(SKY_HORIZON), "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(SKY_TOP), "horizonColor": hexrgb(SKY_HORIZON),
                "groundColor": hexrgb(VOID), "haze": 0.45, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.light("sun", "directional", direction=[0.55, -0.75, -0.38], color=hexrgb("#fff3e6"), intensity=3.2,
            castsShadow=True, contactShadow=False)

    # ---- the island and its far siblings
    island(s, "isle", (0.0, 0.0, 0.0), 1.0)
    island(s, "farA", (27.0, 7.0, -34.0), 0.42, lawn="#a9f5cf", rock="#cbb8ff")
    island(s, "farB", (-31.0, 11.0, -52.0), 0.3, lawn="#b8f7d8", rock="#d4c4ff")
    island(s, "farC", (40.0, -6.0, -70.0), 0.25, lawn="#b8f7d8", rock="#d4c4ff")

    # ---- the giant flower (left of centre, behind the creatures): a curved stem, eight petals, a butter heart
    stem = {"kind": "tube", "tubeRadius": 0.22, "tubeTaper": 0.7, "tubeSides": 7, "tubeSegments": 20,
            "tubeTwist": 0.0, "tubeCaps": True,
            "curve": {"kind": "catmullRom", "generator": "points", "samplesPerSegment": 8,
                      "points": [{"position": [-3.6, 0.0, -1.6]}, {"position": [-3.9, 2.8, -1.7]},
                                 {"position": [-3.3, 5.6, -1.4]}, {"position": [-2.7, 7.4, -0.9]}]}}
    s.proc("stem", stem, material=toon(MINT_DARK))
    head = (-2.7, 7.6, -0.8)
    s.proc("petals", sphere(1.0, 12, 8),
           distribution={"kind": "radial", "count": 8, "radius": 1.55, "plane": "xy", "orientation": "outward"},
           material=toon(CREAM, rim=0.2, ambient=0.7),
           transform={"position": list(head), "rotation": [-28.0, 18.0, 0.0], "scale": [1, 1, 1]},
           extra={"sourceTransform": {"scale": [0.62, 0.22, 1.2]}})
    s.proc("heart", sphere(0.95, 14, 8), material=toon(BUTTER, rim=0.3, spec=0.4),
           transform={"position": [head[0], head[1], head[2] + 0.15], "rotation": [-28.0 + 90.0, 18.0, 0.0],
                      "scale": [1.0, 0.45, 1.0]})
    for k, (dx, dz, ang) in enumerate(((0.35, 0.2, 40.0), (-0.25, -0.15, -35.0))):
        s.proc("leaf%d" % k, sphere(0.9, 10, 6), material=toon(MINT_DARK),
               transform={"position": [-3.8 + dx, 2.4 + 1.2 * k, -1.7 + dz], "rotation": [0.0, 30.0 + 120 * k, ang],
                          "scale": [1.2, 0.18, 0.5]})

    # ---- trees: two lollipops (back left), three cones (right)
    for k, (x, z, h, col) in enumerate(((-6.0, -4.0, 3.6, PINK), (-1.0, -5.6, 2.8, BUTTER))):
        s.proc("stick%d" % k, cyl(0.13, h, 8), material=toon(CREAM), transform=at(x, h * 0.5, z))
        s.proc("pop%d" % k, sphere(1.25, 12, 8), material=toon(col, rim=0.25, spec=0.5), transform=at(x, h + 0.9, z))
    for k, (x, z, h) in enumerate(((4.6, -3.2, 4.4), (6.4, -0.6, 3.2), (3.2, -5.6, 3.6))):
        cone = kit.lathe([(0.0, 1.35), (h * 0.55, 0.85), (h, 0.0)], sides=7, samples=3)
        s.proc("cone%d" % k, cone, material=toon("#4fd6a8", rim=0.15), transform=at(x, 0.35, z))
        s.proc("trunk%d" % k, cyl(0.16, 0.7, 6), material=toon("#b07a5a"), transform=at(x, 0.2, z))

    # ---- the stone ring half-sunk in the lawn: a doorway that leads nowhere
    s.proc("ring", {"kind": "torus", "majorRadius": 1.7, "minorRadius": 0.32, "majorSegments": 12,
                    "minorSegments": 6},
           material=toon("#ffb4a8", rim=0.15), transform={"position": [5.2, 0.6, 2.6], "rotation": [90.0, -35.0, 0.0],
                                                           "scale": [1, 1, 1]})

    # ---- the creatures: three in a row on the lawn's front edge, turned toward the camera
    for name, x, z, col, face in (("blob1", -2.4, 4.6, LILAC, 28.0), ("blob2", 0.2, 5.3, BUTTER, 24.0),
                                  ("blob3", 2.8, 4.8, CORAL, 18.0)):
        creature(s, name, (x, 0.95, z), col, facing=face)

    # ---- clouds: rows of flat puffs
    for k, (x, y, z, n) in enumerate(((-14.0, 9.0, -22.0, 5), (16.0, 13.0, -30.0, 4), (-26.0, 4.0, -12.0, 3),
                                      (9.0, 19.0, -48.0, 4))):
        s.proc("cloud%d" % k, sphere(1.6, 12, 7),
               distribution={"kind": "linear", "count": n, "start": [x - n * 0.9, y, z], "end": [x + n * 0.9, y, z]},
               variation={"seed": 7 + k, "randomScale": [0.35, 0.3, 0.2], "randomPosition": [0.3, 0.45, 0.4]},
               material=toon(CLOUD, ambient=0.85, bands=1, rim=0.2),
               extra={"sourceTransform": {"scale": [1.0, 0.62, 0.8]}})

    # ---- the waterfall: pale drops falling off the island's front-left edge into the void
    s.particles("falls", capacity=6000, seed=5, shape="box", position=[-6.4, -0.2, 5.2], extent=[0.55, 0.05, 0.25],
                direction=[-0.3, -0.2, 0.25], spawnRate=1400.0, lifetimeMin=2.2, lifetimeMax=3.0, spread=0.06,
                speedMin=0.6, speedMax=1.0, gravity=[0, -6.0, 0], drag=0.4, turbulence=0.15, turbulenceScale=0.6,
                sizeStart=0.09, sizeEnd=0.05, sizeVariance=0.4, colorStart=hexrgb("#e8f8ff") + [0.95],
                colorEnd=hexrgb("#b9e6ff") + [0.0], emissive=1.0, blend="alpha", velocityStretch=1.2,
                stretchMax=0.6)

    # ---- extra creatures that join as a piece builds (scale 0 at rest), small lawn flowers (one per chord voice),
    # a rainbow behind the island for held notes (scale 0 at rest)
    for name, x, z, col, face in (("blob4", -0.9, 2.2, PINK, 26.0), ("blob5", 4.2, 2.6, "#9ff0d0", 12.0)):
        creature(s, name, (x, 0.95, z), col, facing=face, scale=0.001)
    tulip_dist = {"kind": "radial", "count": 3, "radius": 7.2, "plane": "xz", "orientation": "outward",
                  "startAngle": 0.6, "endAngle": 2.9}
    s.proc("tulipStems", cyl(0.05, 0.7, 6), distribution=tulip_dist, material=toon(MINT_DARK),
           transform=at(0.0, 0.35, 0.0))
    s.proc("tulips", sphere(0.26, 10, 6), distribution=tulip_dist, material=toon(PINK, rim=0.25),
           transform=at(0.0, 0.85, 0.0), material_variation={"hueGradient": 0.6, "perceptualHue": True})
    for k, col in enumerate(RAINBOW):
        s.proc("rainbow%d" % k, {"kind": "torus", "majorRadius": 9.0 - 0.42 * k, "minorRadius": 0.2,
                                 "majorSegments": 64, "minorSegments": 6},
               material=toon(col, bands=1, ambient=0.9, emissive=0.25),
               transform={"position": [0.0, -0.4, -3.5], "rotation": [90.0, 0.0, 0.0], "scale": [0.001, 0.001, 0.001]})

    # ---- confetti (the snare) and sparkles (the hats) from the flower
    s.particles("confetti", capacity=2000, seed=19, shape="sphere", position=list(head), extent=[0.4, 0.4, 0.4],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=1.6, lifetimeMax=2.6, spread=0.9, speedMin=2.0,
                speedMax=4.0, gravity=[0, -3.0, 0], drag=1.2, turbulence=0.6, turbulenceScale=0.8,
                sizeStart=0.11, sizeEnd=0.08, colorStart=hexrgb(PINK) + [1.0], colorEnd=hexrgb(BUTTER) + [0.0],
                colorCurve=[{"t": 0.0, "color": hexrgb(PINK)}, {"t": 0.33, "color": hexrgb(BUTTER)},
                            {"t": 0.66, "color": hexrgb("#8ff0c0")}, {"t": 1.0, "color": hexrgb(LILAC)}],
                emissive=1.0, blend="alpha", shape2d="leaf", tumbleRate=6.0, leafAspect=0.7)
    s.particles("sparkle", capacity=1200, seed=27, shape="sphere", position=list(head), extent=[2.2, 1.6, 1.4],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.3, lifetimeMax=0.7, spread=1.0, speedMin=0.0,
                speedMax=0.2, gravity=[0, 0, 0], drag=1.0, sizeStart=0.09, sizeEnd=0.0,
                colorStart=hexrgb("#ffffff") + [1.0], colorEnd=hexrgb(BUTTER) + [0.0], emissive=5.0,
                blend="additive")
    s.places("sing", "lastPitch", [0.36, 0.5, 0.64], 0.07, event="noteOn")
    instrument(s)

    # ---- camera: a slow arc round the diorama from slightly above; the creatures and flower left of centre
    focal = 32.0
    focus = [0.0, 2.2, 1.0]
    s.params_({"camera/lens/focalLength": focal, "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.4,
               "post/output/vignette": 0.18, "post/output/grain": 0.0, "post/tonemap/operator": 4,
               "post/outline/amount": 1.0, "post/outline/color": hexrgb(PLUM), "post/outline/intensity": 1.0,
               "post/outline/width": 2.2, "post/outline/depthThreshold": 0.06,
               "post/outline/normalThreshold": 0.4, "post/outline/objectEdges": 1.0})
    s.arc_camera(focus, radius=24.0, height=8.5, period=90.0, centre_deg=24.0, sweep_deg=34.0, side=1.2, lift=0.6)
    s.region("creatures", box=[0.3, 0.45, 0.7, 0.8])
    s.region("flower", box=[0.25, 0.1, 0.5, 0.45])
    return s
