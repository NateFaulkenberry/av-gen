#!/usr/bin/env python3
"""The Sonic Garden's look (docs/prototypes/sonic-garden, brief §13-19, §26-27, §34-37): one readable file that writes
the scene, and the master project's character tuning, interpreter mappings, routes, camera and look parameters.

    python3 tools/sonic_garden_look.py && python3 tools/sonic_garden_variants.py

It writes examples/sonic-garden/sonic-garden.scene.json and the `sonic`, `sources`, `routes`, `timeline` and
`parameters` of examples/sonic-garden/sonic-garden.json (the rest of the master is kept). Edit here, not in those
files; the variants are regenerated from the master as before.

The mapping language, in four layers (ART-NOTES.md in the review folder has the table and the reasons):
  1. Families (group "family", slow tier, competing): organic, crystalline, chaotic, and silence (no sound, no
     world). They are the world's identity: which structures exist, the palette, the air, the light, the materials.
  2. A second source ("world") splits chaotic by weight into its two faces: tectonic (heavy: a loud, full, pitched
     sound) and impact (light: noise and transients). With them there are four palettes, all continuous blends.
  3. Qualities (medium tier): mass, radiance, grain, edge, shimmer, breath, energy, tension, swarm, and (pass 2)
     glow -- the sound's brightness alone, the continuous "filter" channel that opens the light and the detail.
  4. Musical context (notes.* only): sustain, figure (an arpeggio's patterned motion), stack (dense chords), lift
     (pitch). And the MIDI note-on itself, whose visual gesture is chosen by the family through a route's depth:
     a swell in the organic world, a ring in the crystalline one, a heave in the heavy one; in the light one the
     audio's own transients strike instead. Pass 2 also places the gesture by pitch: low notes answer low in the
     world, high notes high (the `lo`/`hi` family splits below).
Everything here is an artistic choice (brief §27), and every weight is also a live parameter.

Art pass 2 (2026-09-30) keeps that language and rebuilds what it is expressed through: surfaces that carry their
own structure (six material programs, each shaping the material's OWN emission, so every route still decides colour
and strength), designed hero forms instead of a ball, a sky with a sun placed behind the subject, per-world
skylines, a longer lens and one slow push-in, and fog that only marches in the two worlds that need air.
"""
import json
import math
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = os.path.join(REPO, "examples", "sonic-garden", "sonic-garden.scene.json")
MASTER = os.path.join(REPO, "examples", "sonic-garden", "sonic-garden.json")

# ------------------------------------------------------------------------------------------------ the character
# One retune of the default Sonic Character (src/sonic/character.cpp): roughness also reads the spectrum's spread.
# Distortion's intermodulation spreads energy across the spectrum; without this the distorted bass read "dense and
# bright" rather than rough (0.41 against the percussion's 0.75), and the morph's distorted stage barely moved it.
CHARACTER = {"dimensions": {"roughness": {"terms": [
    {"feature": "dissonance", "lo": 0.01, "hi": 0.12, "weight": 1.5},
    {"feature": "flatness", "lo": 0.001, "hi": 0.3, "log": True, "weight": 1.0},
    {"feature": "inharmonicity", "lo": 0.15, "hi": 0.7, "weight": 1.0},
    {"feature": "bandwidth", "lo": 1500, "hi": 6000, "log": True, "weight": 1.0}]}}}


# ------------------------------------------------------------------------------------------------ the interpreter
def M(name, inputs, combine="mean", bias=0.0, gain=1.0, curve=1.0, group=None):
    o = {"name": name, "combine": combine,
         "inputs": [dict({"signal": s, "weight": w}, **({"invert": True} if inv else {})) for s, w, inv in inputs]}
    if group:
        o["group"] = group
    if bias:
        o["bias"] = bias
    if gain != 1.0:
        o["gain"] = gain
    if curve != 1.0:
        o["curve"] = curve
    return o


GARDEN_MAPPINGS = [
    # families: products are ANDs -- warm AND smooth AND not bright AND not inharmonic AND steady is organic (a
    # decaying FM bell chord is warm and soft too; only its moving spectrum tells it apart); bright-ish AND
    # inharmonic AND clean is crystalline (the bell, and the morph's bright and resonant stages); chaotic is a mean
    # of roughness (doubled), sharpness and the absence of smoothness, lifted off the floor by bias/gain.
    M("organic", [("sonic.warmth.slow", 1.0, False), ("sonic.smoothness.slow", 1.0, False),
                  ("sonic.brightness.slow", 1.5, True), ("sonic.inharmonicity.slow", 1.0, True),
                  ("sonic.stability.slow", 0.5, False)],
      "product", group="family"),
    M("crystalline", [("sonic.brightness.slow", 0.5, False), ("sonic.inharmonicity.slow", 0.5, False),
                      ("sonic.roughness.slow", 2.0, True)], "product", group="family"),
    M("chaotic", [("sonic.roughness.slow", 2.0, False), ("sonic.sharpness.slow", 1.0, False),
                  ("sonic.smoothness.slow", 1.0, True)], "mean", -0.2, 1.6, group="family"),
    # silence owns the world when there is no sound (slow energy under 0.2), so a world grows in from nothing
    # rather than from whichever family an all-zero character happens to favour (on frame 0 that was chaotic, via
    # "not smooth", and every world opened with shards shrinking away).
    M("silence", [("sonic.energy.slow", 1.0, True)], "mean", -4.0, 5.0, 2.0, group="family"),  # bias is +-4
    # qualities
    M("mass", [("sonic.energy.slow", 1.0, False), ("sonic.density.slow", 3.0, False),
               ("sonic.harmonicity.slow", 3.0, False)], "product", -0.3, 5.0, 1.5),  # loud AND full AND pitched
    M("radiance", [("sonic.brightness", 1.0, False), ("sonic.energy", 1.0, False)]),
    # glow: the sound's brightness alone (medium tier) -- the continuous channel a filter sweep moves while the
    # MIDI stays put. It opens the light (surfaces, the ground's veins, the heart) and the detail (the hero's
    # fine displacement), inside whatever world the family has chosen.
    M("glow", [("sonic.brightness", 1.0, False)], "mean", -0.1, 1.4),
    M("grain", [("sonic.roughness", 1.0, False), ("sonic.energy", 0.5, False)], "product"),
    M("edge", [("sonic.sharpness", 1.0, False), ("sonic.inharmonicity", 0.5, False)], bias=-0.2, gain=1.4),
    M("shimmer", [("sonic.inharmonicity", 1.0, False), ("sonic.brightness", 0.5, False),
                  ("sonic.roughness", 1.0, True)], "product"),
    M("breath", [("sonic.smoothness.slow", 1.0, False), ("notes.duration", 1.0, False),
                 ("notes.legato", 1.0, False)]),
    M("energy", [("sonic.energy", 1.0, False)]),
    # musical context (MIDI only)
    M("sustain", [("notes.legato", 1.0, False), ("notes.duration", 1.0, False), ("notes.rhythm", 0.5, True)]),
    M("figure", [("notes.rhythm", 1.0, False), ("notes.regularity", 0.5, False), ("notes.chord", 1.0, True)],
      "product"),
    M("stack", [("notes.chord", 1.0, False), ("notes.polyphony", 1.0, False)]),
    M("lift", [("notes.pitch", 1.0, False)]),
    M("swarm", [("notes.density", 1.0, False), ("sonic.density", 0.5, False)]),
    M("tension", [("notes.tension", 1.0, False)]),
    # register: where in the world a note answers. `notes.pitch` is the centre of the sounding notes (the
    # phrase's 5-95% span is 0.38-0.62, a chord sits near 0.41), and a mapping is x * gain + bias, so high runs
    # 0 -> 1 over pitch 0.38 -> 0.54 and low 1 -> 0 over 0.34 -> 0.50: a chord is mostly low, an arpeggio's
    # upper notes are high.
    M("high", [("notes.pitch", 1.0, False)], "mean", -2.375, 6.25),
    M("low", [("notes.pitch", 1.0, True)], "mean", -3.125, 6.25),
]
# Listed after "garden", so it reads this frame's families (a source reads an earlier source's outputs in the same
# frame, but its own only a frame late).
WORLD_MAPPINGS = [
    M("tectonic", [("visual.chaotic", 1.0, False), ("visual.mass", 1.0, False)], "product"),
    M("impact", [("visual.chaotic", 1.0, False), ("visual.mass", 1.0, True)], "product"),
    # a family's gesture, split by register: the melody climbs the world
    M("organicLo", [("visual.organic", 1.0, False), ("visual.low", 1.0, False)], "product"),
    M("organicHi", [("visual.organic", 1.0, False), ("visual.high", 1.0, False)], "product"),
    M("crystalLo", [("visual.crystalline", 1.0, False), ("visual.low", 1.0, False)], "product"),
    M("crystalHi", [("visual.crystalline", 1.0, False), ("visual.high", 1.0, False)], "product"),
]


def xf(pos=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
    return {"position": list(pos), "rotation": list(rot), "scale": list(scale)}


def mat(base, emissive=(0, 0, 0), ei=0.0, rough=0.6, metal=0.0, program=None):
    m = {"baseColor": list(base), "emissiveColor": list(emissive), "emissiveIntensity": ei, "roughness": rough,
         "metallic": metal}
    if program:
        m["program"] = program
    return m


def proc(name, source, dist, dxf, material, **kw):
    p = {"source": source, "distribution": dist, "distributionTransform": dxf, "material": material}
    for k, v in kw.items():
        if v is not None:
            p[k] = v
    return {"name": name, "kind": "procedural", "procedural": p}


def lathe(profile, n=13):
    """A lathe profile as a tube's points: per-point scale draws the radius, u runs 0..1 up the axis."""
    pts = []
    for i in range(n):
        u = i / (n - 1)
        y, s = profile(u)
        pts.append({"position": [0.0, round(y, 4), 0.0], "scale": round(max(0.02, s), 4), "roll": 0.0})
    return pts


def cap_profile(n=11, power=0.55, height=0.3):
    """A mushroom cap as a lathe: a tube on a straight curve whose per-point scale draws cos(u)^power."""
    return lathe(lambda u: (height * u, math.cos(u * math.pi / 2) ** power), n)


# ------------------------------------------------------------------------------------------------ the surfaces
# Six material programs (docs/procedural-materials.md). Each one shapes the material's OWN emission through the
# `materialEmission` input (ADR-904) rather than asserting a colour, so every existing route into a node's
# `material/emissive` and `material/emissiveColor` still decides the colour and the strength; the program only
# decides WHERE on the surface the light lives (rims, veins, fissures, tips, fracture lines). Base colour and
# roughness stay the material's own (-1) wherever a route drives them.
class Prog:
    def __init__(self, name):
        self.name, self.ops = name, []

    def op(self, kind, dst, a=None, b=None, v=None, k=None, k2=None, k3=None, seed=None, inp=None):
        o = {"kind": kind, "dst": dst}
        if a is not None:
            o["srcA"] = a
        if b is not None:
            o["srcB"] = b
        if v is not None:
            o["value"] = v
        for key, val in (("constant", k), ("constant2", k2), ("constant3", k3)):
            if val is not None:
                o[key] = [float(x) for x in (list(val) + [0.0] * 4)[:4]]
        if seed is not None:
            o["seed"] = seed
        if inp is not None:
            o["input"] = inp
        self.ops.append(o)
        return dst

    # helpers (ops are component-wise; scalars are broadcast)
    def scale(self, dst, a, s):  # dst = a * s, unclamped
        return self.op("remap", dst, a, k=(0, 1, 0, s), v=0)

    def ridge(self, dst, a, width):  # 1 on the 0.5 contour of a noise, falling to 0 over `width` (in (2n-1)^2)
        self.op("remap", dst, a, k=(0.5, 1.0, 0.0, 1.0), v=0)
        self.op("multiply", dst, dst, dst)
        self.op("smoothstep", dst, dst, k=(0.0, width))
        return self.op("remap", dst, dst, k=(0, 1, 1, 0), v=1)

    def doc(self, **outs):
        d = {"name": self.name, "ops": self.ops, "baseColor": -1, "metallic": -1, "roughness": -1, "emission": -1,
             "emissionIntensity": 1.0, "opacity": -1}
        d.update(outs)
        return d


programs = []

# The ground: dark loam/stone at three scales (a macro tone, and two nets of fine veins), whose veins carry the
# material's emission -- mycelium in the warm world, seams of light in the heavy one, faint ice lines in the glass
# one, nothing in the void. The veins fade with distance so they never alias into a shimmer on the far plain.
g = Prog("sgGround")
g.op("input", 0, inp="worldPosition")
g.op("noise", 1, 0, v=0.11, seed=2)
g.op("noise", 2, 0, v=0.42, seed=3)
g.ridge(3, 2, 0.0009)
g.op("noise", 2, 0, v=1.35, seed=8)
g.ridge(2, 2, 0.0015)
g.scale(2, 2, 0.35)
g.op("add", 3, 3, 2)
g.op("smoothstep", 2, 1, k=(0.42, 0.68))  # the veins gather in patches, not a net over the whole plain
g.op("multiply", 3, 3, 2)
g.op("input", 4, inp="cameraDistance")
g.op("remap", 4, 4, k=(10, 75, 1, 0), v=1)
g.op("multiply", 3, 3, 4)
g.op("input", 7, inp="materialEmission")
g.op("multiply", 7, 7, 3)
g.op("ramp", 5, 1, k=(0.010, 0.009, 0.010, 1), k2=(0.022, 0.019, 0.018, 1), k3=(0.042, 0.036, 0.032, 1))
programs.append(g.doc(baseColor=5, emission=7))

# The core (the hero of the glass, heavy and light worlds): a dark body lit from inside through thin fissures that
# move with it (object space: the contours of two noises), with a faint rim -- a cracked mass in the heavy world,
# a white knot whose cracks flash in the light one; the routes pick the colour and the strength.
c = Prog("sgCore")
c.op("input", 0, inp="objectPosition")
c.op("noise", 1, 0, v=1.3, seed=13)
c.ridge(1, 1, 0.00045)
c.op("noise", 2, 0, v=2.9, seed=17)
c.ridge(2, 2, 0.0006)
c.scale(2, 2, 0.35)
c.op("add", 1, 1, 2)
c.scale(1, 1, 2.2)
c.op("fresnel", 3, v=5.0)
c.scale(3, 3, 0.15)
c.op("constant", 4, k=(0.02, 0.02, 0.02, 0.02))
c.op("add", 4, 4, 3)
c.op("add", 4, 4, 1)
c.op("input", 7, inp="materialEmission")
c.op("multiply", 7, 7, 4)
programs.append(c.doc(emission=7))

# Living tissue (every organic form): the light of a translucent body -- strongest at the grazing rim, faint
# along a net of veins, and gathered into the tips of anything tall (a tendril's last metre glows; a cap, a petal
# or a bud is too short to have a tip, so it keeps only its rim and veins).
f = Prog("sgFlesh")
f.op("fresnel", 0, v=2.2)
f.scale(0, 0, 1.15)
f.op("input", 1, inp="objectPosition")
f.op("noise", 2, 1, v=3.2, seed=21)
f.ridge(2, 2, 0.0012)
f.scale(2, 2, 0.55)
f.op("gradient", 3, 1, k=(0, 0.26, 0, 0), v=1)
f.op("power", 3, 3, v=4.0)
f.scale(3, 3, 3.2)
f.op("constant", 4, k=(0.07, 0.07, 0.07, 0.07))
f.op("add", 4, 4, 0)
f.op("add", 4, 4, 2)
f.op("add", 4, 4, 3)
f.op("input", 7, inp="materialEmission")
f.op("multiply", 7, 7, 4)
programs.append(f.doc(emission=7))

# Glass (every crystal, and the gem): a dark, polished body whose light is all at the edges of the view (a Fresnel
# rim) and in a few hairline fractures -- so a crystal reads as a volume of dark glass holding light at its edges,
# not as a lit card (the first try also lit an internal cloud, and the crystals went milky).
q = Prog("sgGlass")
q.op("fresnel", 0, v=4.0)
q.scale(0, 0, 1.7)
q.op("input", 1, inp="objectPosition")
q.op("noise", 2, 1, v=1.8, seed=31)
q.ridge(2, 2, 0.0007)
q.scale(2, 2, 0.4)
q.op("constant", 4, k=(0.01, 0.01, 0.01, 0.01))
q.op("add", 4, 4, 0)
q.op("add", 4, 4, 2)
q.op("input", 7, inp="materialEmission")
q.op("multiply", 7, 7, 4)
programs.append(q.doc(emission=7))

# Obsidian (the blades): black and polished; a strike lights their edges white-hot (the rim), not their faces.
o = Prog("sgObsidian")
o.op("fresnel", 0, v=4.0)
o.scale(0, 0, 2.6)
o.op("constant", 1, k=(0.06, 0.06, 0.06, 0.06))
o.op("add", 1, 1, 0)
o.op("input", 7, inp="materialEmission")
o.op("multiply", 7, 7, 1)
programs.append(o.doc(emission=7))

# Basalt (the monoliths and the heavy skyline): near-black stone at two scales whose light lives only in its
# seams -- the contour lines of two noises, which read as cracks (a Voronoi band read as leopard spots).
b = Prog("sgBasalt")
b.op("input", 0, inp="objectPosition")
b.op("noise", 1, 0, v=0.9, seed=41)
b.ridge(1, 1, 0.0007)
b.op("noise", 2, 0, v=2.1, seed=43)
b.ridge(2, 2, 0.0012)
b.scale(2, 2, 0.45)
b.op("add", 1, 1, 2)
b.op("remap", 1, 1, k=(0, 1, 0.0, 1.8), v=0)
b.op("input", 7, inp="materialEmission")
b.op("multiply", 7, 7, 1)
b.op("input", 3, inp="worldPosition")
b.op("triplanar", 4, 3, v=0.8)
b.op("ramp", 5, 4, k=(0.008, 0.007, 0.010, 1), k2=(0.020, 0.018, 0.022, 1), k3=(0.036, 0.031, 0.040, 1))
programs.append(b.doc(baseColor=5, emission=7))

# ------------------------------------------------------------------------------------------------ the scene
nodes = []
# The organic world breathes once every 10 s (4 bars at the phrase's 96 BPM): the core, the petals, the tendrils,
# the caps and (at twice the rate) the buds all sway on this one clock, with their own spatial frequencies, so
# the garden moves as one living thing instead of a dozen unrelated oscillators.
BREATH = 2.0 * math.pi / 10.0

# The ground: a broad, gently undulating plain in the ground program. Its roughness is the world's (routes): a
# soft loam in the warm world, polished black glass in the observatory, dry ash in the void. Its second deformer
# is a slow swell that only the heavy (tectonic) world raises.
nodes.append(proc("ground",
                  {"kind": "box", "size": [150, 0.6, 150], "subdivisions": 64},
                  {"kind": "single"}, xf((0, -0.3, 0)),
                  mat((0.02, 0.018, 0.018), (1.0, 1.0, 1.0), 0.0, rough=0.7, program="sgGround"),
                  deformers=[{"kind": "noise", "amount": 0.45, "scale": 0.05, "speed": 0.0, "seed": 17,
                              "axisMask": [0, 1, 0]},
                             {"kind": "sine", "amount": 0.0, "frequency": 0.32, "speed": 0.9,
                              "axis": [1, 0, 0.4], "displacementAxis": [0, 1, 0], "space": "world"}]))

# The far horizon: a ring of low dark masses, the one skyline every world shares.
nodes.append(proc("horizon",
                  {"kind": "box", "size": [26, 7, 14], "subdivisions": 2, "bevel": 3.0, "bevelSegments": 2},
                  {"kind": "radial", "count": 30, "radius": 118, "orientation": "outward"},
                  xf((0, -2.5, 0)),
                  mat((0.008, 0.008, 0.01), rough=0.95),
                  variation={"seed": 41, "scale": [0.6, 0.8, 0.5], "rotation": [0.12, 1.2, 0.12],
                             "position": [10, 0, 10]}))

# ---- each world's own skyline, far away and faded by the air: giant fungi (organic), glass spires
# (crystalline), a broken basalt ridge (tectonic). The void (impact) has none: its world ends at the light.
FAR_CAP = {"kind": "tube", "tubeRadius": 0.6, "tubeTaper": 1.0, "tubeSides": 24, "tubeSegments": 12,
           "tubeTwist": 0.0, "tubeCaps": True,
           "curve": {"kind": "catmullRom", "generator": "points", "points": cap_profile(power=0.45),
                     "samplesPerSegment": 6}}
# (A distribution transform's scale scales the ring as well as the forms, so each far ring's radius is its
# distance divided by the form's scale: 62 m / 14.)
FAR_RING = {"kind": "radial", "count": 11, "radius": 62 / 14.0, "orientation": "outward", "startAngle": 0.3}
FAR_VAR = {"seed": 61, "scale": [0.45, 0.6, 0.45], "position": [0.6, 0, 0.6], "rotation": [0.0, 1.0, 0.0]}
nodes.append(proc("farcaps", FAR_CAP, FAR_RING, xf((0, 9.0, 0), scale=(14, 14, 14)),
                  mat((0.03, 0.012, 0.018), (1.0, 0.42, 0.3), 0.0, rough=0.8, program="sgFlesh"),
                  variation=FAR_VAR))
nodes.append(proc("farstems",
                  {"kind": "tube", "tubeRadius": 0.07, "tubeTaper": 0.6, "tubeSides": 10, "tubeSegments": 10,
                   "tubeTwist": 0.0, "tubeCaps": True,
                   "curve": {"kind": "catmullRom", "generator": "noise", "count": 5, "start": [0, 0, 0],
                             "end": [0.0, 0.66, 0.08], "noiseAmount": 0.03, "noiseScale": 1.0, "seed": 3,
                             "samplesPerSegment": 6}},
                  FAR_RING, xf((0, -0.2, 0), scale=(14, 14, 14)),
                  mat((0.02, 0.01, 0.012), (1.0, 0.45, 0.3), 0.0, rough=0.85), variation=FAR_VAR))
GEM = {"kind": "box", "size": [0.8, 0.8, 0.8], "subdivisions": 1, "bevel": 0.05, "bevelSegments": 1}
ON_VERTEX = xf(rot=(45.0, 0.0, 35.264))
nodes.append(proc("farspires", GEM,
                  {"kind": "radial", "count": 13, "radius": 70 / 5.0, "orientation": "outward", "startAngle": 0.1},
                  xf((0, 5.0, 0), scale=(5.0, 22.0, 5.0)),
                  mat((0.006, 0.01, 0.02), (0.5, 0.8, 1.0), 0.0, rough=0.08, metal=0.2, program="sgGlass"),
                  sourceTransform=ON_VERTEX,
                  variation={"seed": 71, "scale": [0.3, 0.55, 0.3], "rotation": [0.12, 0.8, 0.12],
                             "position": [2.4, 0, 2.4]}))
nodes.append(proc("ridge",
                  {"kind": "box", "size": [9, 26, 5], "subdivisions": 6, "bevel": 0.4, "bevelSegments": 1},
                  {"kind": "radial", "count": 17, "radius": 58, "orientation": "outward", "startAngle": 0.2},
                  xf((0, -16.0, 0)),
                  mat((0.012, 0.01, 0.015), (0.5, 0.18, 1.0), 0.0, rough=0.8, program="sgBasalt"),
                  variation={"seed": 81, "scale": [0.5, 0.5, 0.4], "rotation": [0.35, 0.9, 0.35],
                             "position": [8, 3, 8]},
                  deformers=[{"kind": "displacement", "amount": 0.9, "scale": 0.25, "speed": 0.0, "seed": 9}]))

# ---- the hero: the core, shared by every world. Its size, colour and surface are each world's: a small seed of
# light cradled by petals (organic), a spark inside a cut gem (crystalline), a massive fissured body (tectonic), a
# hard white knot inside the blades (impact).
nodes.append(proc("hero",
                  {"kind": "sphere", "radius": 1.25, "segments": 160, "rings": 120},
                  {"kind": "single"}, xf((0, 2.6, 0)),
                  mat((0.02, 0.02, 0.02), (0.0, 0.0, 0.0), 0.0, rough=0.42, program="sgCore"),
                  deformers=[{"kind": "noise", "amount": 0.0, "scale": 1.4, "speed": 0.5, "seed": 11},
                             {"kind": "sine", "amount": 0.02, "frequency": 1.6, "speed": BREATH, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0.3, 1]},
                             {"kind": "twist", "amount": 0.0, "speed": 0.0, "axis": [0, 1, 0],
                              "center": [0, 0, 0]},
                             {"kind": "displacement", "amount": 0.0, "scale": 4.0, "speed": 0.25, "seed": 3}]))

# The organic hero: a small seed of light cradled by the lotus, in living tissue.
nodes.append(proc("seed",
                  {"kind": "sphere", "radius": 0.85, "segments": 96, "rings": 72},
                  {"kind": "single"}, xf((0, 1.8, 0)),
                  mat((0.3, 0.12, 0.05), (1.0, 0.7, 0.38), 0.0, rough=0.45, program="sgFlesh"),
                  deformers=[{"kind": "noise", "amount": 0.05, "scale": 2.2, "speed": 0.3, "seed": 19},
                             {"kind": "sine", "amount": 0.025, "frequency": 2.4, "speed": BREATH, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0.2, 1]}]))

# The crystalline hero: a compound of chamfered cubes (boxes are the one faceted primitive), each level turned
# against the last -- a cut gem around the core, in dark glass. It turns slowly and exactly.
nodes.append(proc("facets",
                  {"kind": "box", "size": [3.1, 3.1, 3.1], "subdivisions": 1, "bevel": 0.45, "bevelSegments": 1},
                  {"kind": "single"}, xf((0, 2.6, 0)),
                  mat((0.012, 0.02, 0.035), (0.55, 0.85, 1.0), 0.0, rough=0.14, metal=0.2, program="sgGlass"),
                  hierarchy={"recursionDepth": 2, "scalePerLevel": 0.98, "offsetPerLevel": [0, 0, 0],
                             "rotationPerLevel": [35.26, 45.0, 0.0], "colorPerLevel": False}))

# ---- organic: a lotus of cupped petals round the seed, tendrils with glowing tips, mushrooms, buds.
# A petal is a thin ellipsoid lying flat, curled up along its length and cupped across its width by two bends
# (the deformers act after the source transform, so the curl is the petal's own, not a tilt).
def petals(name, count, ring, length, width, curl, cup, y, start=0.0, seed=13):
    return proc(name,
                {"kind": "sphere", "radius": 1.0, "segments": 48, "rings": 28},
                {"kind": "radial", "count": count, "radius": ring, "orientation": "outward", "startAngle": start},
                xf((0, y, 0)),
                mat((0.16, 0.035, 0.06), (1.0, 0.42, 0.34), 0.0, rough=0.55, program="sgFlesh"),
                sourceTransform=xf((0.0, 0.0, length * 0.92), scale=(width, 0.045, length)),
                variation={"seed": seed, "rotation": [0.05, 0.1, 0.05], "scale": [0.1, 0.1, 0.12]},
                deformers=[{"kind": "bend", "amount": curl, "axis": [0, 0, 1], "displacementAxis": [0, 1, 0],
                            "center": [0, 0, 0]},
                           {"kind": "bend", "amount": cup, "axis": [1, 0, 0], "displacementAxis": [0, 1, 0],
                            "center": [0, 0, 0]},
                           {"kind": "sine", "amount": 0.04, "frequency": 1.2, "speed": BREATH, "axis": [0, 0, 1],
                            "displacementAxis": [0, 1, 0]}])


nodes.append(petals("petals", 7, 0.33, 1.35, 0.5, 0.41, 1.55, 0.95, 0.0, 13))
nodes.append(petals("outerpetals", 11, 0.68, 1.85, 0.62, 0.27, 1.28, 0.85, 0.29, 14))

nodes.append(proc("stalks",
                  {"kind": "tube", "tubeRadius": 0.055, "tubeTaper": 0.12, "tubeSides": 9, "tubeSegments": 30,
                   "tubeTwist": 0.4, "tubeCaps": True,
                   "curve": {"kind": "catmullRom", "generator": "noise", "count": 8, "start": [0, 0, 0],
                             "end": [0.0, 3.8, 1.0], "noiseAmount": 0.32, "noiseScale": 0.9, "seed": 5,
                             "samplesPerSegment": 8}},
                  {"kind": "radial", "count": 22, "radius": 3.3, "orientation": "outward"},
                  xf((0, 0, 0)),
                  mat((0.03, 0.014, 0.012), (1.0, 0.62, 0.3), 0.0, rough=0.6, program="sgFlesh"),
                  variation={"seed": 5, "rotation": [0.2, 0.5, 0.2], "scale": [0.25, 0.45, 0.25],
                             "position": [0.9, 0, 0.9]},
                  materialVariation={"valueRandom": 0.2, "emissiveRandom": 0.6},
                  deformers=[{"kind": "sine", "amount": 0.06, "frequency": 0.9, "speed": BREATH, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0, 0.6]}]))

cap = {"kind": "tube", "tubeRadius": 0.6, "tubeTaper": 1.0, "tubeSides": 28, "tubeSegments": 12, "tubeTwist": 0.0,
       "tubeCaps": True, "curve": {"kind": "catmullRom", "generator": "points", "points": cap_profile(),
                                   "samplesPerSegment": 6}}
CAPS = {"kind": "radial", "count": 9, "radius": 5.9, "orientation": "outward", "startAngle": 0.2}
CAPVAR = {"seed": 9, "scale": [0.45, 0.45, 0.45], "position": [0.9, 0.0, 0.9], "rotation": [0.0, 1.0, 0.0]}
nodes.append(proc("caps", cap, CAPS, xf((0, 0.62, 0)),
                  mat((0.09, 0.03, 0.04), (1.0, 0.36, 0.42), 0.0, rough=0.6, program="sgFlesh"),
                  variation=CAPVAR, materialVariation={"emissiveRandom": 0.5},
                  deformers=[{"kind": "sine", "amount": 0.03, "frequency": 2.0, "speed": BREATH, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0, 1]}]))
# Gills: a flatter copy of the cap profile hung upside down under each cap (the same distribution and seed, so they
# stay aligned), glowing, and a little wider than the cap, so from above they show as a luminous lip.
gill = {"kind": "tube", "tubeRadius": 0.64, "tubeTaper": 1.0, "tubeSides": 28, "tubeSegments": 10, "tubeTwist": 0.0,
        "tubeCaps": True, "curve": {"kind": "catmullRom", "generator": "points",
                                    "points": cap_profile(height=0.16), "samplesPerSegment": 6}}
nodes.append(proc("gills", gill, CAPS, xf((0, 0.625, 0)),
                  mat((0.08, 0.04, 0.05), (1.0, 0.45, 0.28), 0.9, rough=0.6),
                  sourceTransform=xf(rot=(180, 0, 0)), variation=CAPVAR, materialVariation={"emissiveRandom": 0.4}))
nodes.append(proc("capstems",
                  {"kind": "tube", "tubeRadius": 0.07, "tubeTaper": 0.7, "tubeSides": 8, "tubeSegments": 10,
                   "tubeTwist": 0.0, "tubeCaps": True,
                   "curve": {"kind": "catmullRom", "generator": "line", "count": 4, "start": [0, 0, 0],
                             "end": [0.0, 0.66, 0.0], "samplesPerSegment": 6}},
                  CAPS, xf((0, 0, 0)),
                  mat((0.1, 0.07, 0.06), (1.0, 0.5, 0.3), 0.0, rough=0.7, program="sgFlesh"), variation=CAPVAR))

# Buds: glowing bulbs that only dense chords raise (organic x stack) -- the harmony blossoms. "Dense: many
# overlapping forms, layered geometry, crowded composition" (brief §14).
nodes.append(proc("buds",
                  {"kind": "sphere", "radius": 0.13, "segments": 20, "rings": 14},
                  {"kind": "spiral", "count": 44, "radius": 2.3, "radiusGrowth": 3.2, "turns": 4, "spiralHeight": 0.0,
                   "orientation": "outward"},
                  xf((0, 0.35, 0)),
                  mat((0.2, 0.08, 0.06), (1.0, 0.55, 0.25), 1.3, rough=0.5, program="sgFlesh"),
                  variation={"seed": 23, "position": [0.5, 0.4, 0.5], "scale": [0.45, 0.45, 0.45]},
                  materialVariation={"emissiveRandom": 0.6, "hueGradient": 0.06},
                  deformers=[{"kind": "sine", "amount": 0.02, "frequency": 3.0, "speed": 2 * BREATH, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0.5, 1]}]))

# ---- crystalline: two ordered rings of dark-glass rhombohedra, a cluster the chords grow, and an armillary of
# thin platinum rings round the gem (an observatory's instrument: harmonic order made literal).
nodes.append(proc("prisms", GEM,
                  {"kind": "radial", "count": 12, "radius": 7.6, "orientation": "outward"},
                  xf((0, 1.9, 0), scale=(1.0, 3.2, 1.0)),
                  mat((0.008, 0.014, 0.028), (0.4, 0.75, 1.0), 0.0, rough=0.05, metal=0.15, program="sgGlass"),
                  sourceTransform=ON_VERTEX,
                  variation={"seed": 21, "scale": [0.2, 0.4, 0.2], "rotation": [0.0, 0.6, 0.0]},
                  materialVariation={"hueGradient": 0.04, "valueRandom": 0.2}))
nodes.append(proc("spires", GEM,
                  {"kind": "radial", "count": 12, "radius": 4.7, "orientation": "outward", "startAngle": 0.2618},
                  xf((0, 1.1, 0), scale=(0.6, 1.9, 0.6)),
                  mat((0.008, 0.014, 0.028), (0.4, 0.75, 1.0), 0.0, rough=0.05, metal=0.15, program="sgGlass"),
                  sourceTransform=ON_VERTEX,
                  variation={"seed": 22, "scale": [0.2, 0.4, 0.2], "rotation": [0.15, 0.6, 0.15]},
                  materialVariation={"hueGradient": 0.05, "valueRandom": 0.2}))
# A cluster of small gems between the rings that only dense chords raise (crystalline x stack).
nodes.append(proc("cluster", GEM,
                  {"kind": "spiral", "count": 36, "radius": 5.2, "radiusGrowth": 2.2, "turns": 3, "spiralHeight": 0.0,
                   "orientation": "outward"},
                  xf((0, 0.55, 0), scale=(0.45, 1.3, 0.45)),
                  mat((0.008, 0.014, 0.028), (0.4, 0.75, 1.0), 0.0, rough=0.05, metal=0.15, program="sgGlass"),
                  sourceTransform=ON_VERTEX,
                  variation={"seed": 29, "scale": [0.3, 0.5, 0.3], "rotation": [0.3, 3.1, 0.3],
                             "position": [0.4, 0.0, 0.4]},
                  materialVariation={"hueGradient": 0.05, "valueRandom": 0.25}))
RING = {"kind": "torus", "majorRadius": 2.7, "minorRadius": 0.016, "majorSegments": 256, "minorSegments": 8}
PLATINUM = mat((0.55, 0.58, 0.62), (0.7, 0.88, 1.0), 0.0, rough=0.22, metal=1.0)
# the equator and the ecliptic (two rings crossing at 23 degrees), and three meridians
nodes.append(proc("halos", RING, {"kind": "radial", "count": 2, "radius": 0.001, "orientation": "outward"},
                  xf((0, 2.6, 0)), PLATINUM, sourceTransform=xf(rot=(23.4, 0, 0))))
nodes.append(proc("meridians", dict(RING, majorRadius=2.45),
                  {"kind": "radial", "count": 3, "radius": 0.001, "orientation": "outward"},
                  xf((0, 2.6, 0)), PLATINUM, sourceTransform=xf(rot=(90, 0, 0))))

# ---- chaotic: obsidian blades in a vortex (impact), basalt monoliths rising out of the ground (tectonic), and
# the strike's shock ring: a thin line of light that races out along the ground from each hit and dies.
nodes.append(proc("shards",
                  {"kind": "box", "size": [0.05, 0.75, 0.24], "subdivisions": 1},
                  {"kind": "spiral", "count": 140, "radius": 1.9, "radiusGrowth": 1.4, "turns": 5,
                   "spiralHeight": 3.8, "orientation": "tangent"},
                  xf((0, 0.8, 0)),
                  mat((0.006, 0.006, 0.007), (0.0, 0.0, 0.0), 0.0, rough=0.12, metal=0.15, program="sgObsidian"),
                  variation={"seed": 33, "position": [0.3, 0.3, 0.3], "rotation": [0.35, 0.5, 0.35],
                             "scale": [0.4, 0.6, 0.4]},
                  materialVariation={"emissiveRandom": 0.85, "valueRandom": 0.3},
                  deformers=[{"kind": "noise", "amount": 0.0, "scale": 0.9, "speed": 1.5, "seed": 7}]))
nodes.append(proc("slabs",
                  {"kind": "box", "size": [1.8, 4.6, 0.7], "subdivisions": 8, "bevel": 0.06, "bevelSegments": 1},
                  {"kind": "radial", "count": 9, "radius": 7.8, "orientation": "outward", "startAngle": 0.35},
                  xf((0, -2.4, 0)),
                  mat((0.02, 0.018, 0.03), (0.5, 0.16, 1.0), 0.0, rough=0.6, metal=0.05, program="sgBasalt"),
                  variation={"seed": 51, "rotation": [0.4, 0.7, 0.4], "scale": [0.35, 0.45, 0.35],
                             "position": [1.4, 0.4, 1.4]},
                  materialVariation={"emissiveRandom": 0.7, "valueRandom": 0.3},
                  deformers=[{"kind": "displacement", "amount": 0.14, "scale": 1.6, "speed": 0.0, "seed": 5}]))
nodes.append(proc("shock",
                  {"kind": "torus", "majorRadius": 1.0, "minorRadius": 0.006, "majorSegments": 256,
                   "minorSegments": 6},
                  {"kind": "single"}, xf((0, 0.06, 0), scale=(7.0, 7.0, 7.0)),
                  mat((0.0, 0.0, 0.0), (1.0, 1.0, 0.92), 0.0, rough=0.5)))


# ---- particles
def particles(name, **kw):
    base = {"enabled": True, "capacity": 4096, "seed": 4242, "shape": "box", "position": [0, 2.6, 0],
            "extent": [9, 3, 9], "spawnRate": 0.0, "burst": 0, "lifetimeMin": 3.0, "lifetimeMax": 7.0,
            "direction": [0, 1, 0], "spread": 1.0, "speedMin": 0.05, "speedMax": 0.3, "gravity": [0, 0.05, 0],
            "drag": 0.4, "turbulence": 0.4, "turbulenceScale": 0.4, "turbulenceSpeed": 0.3, "sizeStart": 0.022,
            "sizeEnd": 0.008, "colorStart": [1, 1, 1, 1], "colorEnd": [1, 1, 1, 0], "emissive": 1.2,
            "blend": "additive", "softness": 1.0}
    base.update(kw)
    return {"name": name, "kind": "particles", "particles": base}


nodes.append(particles("spores", seed=4242, position=[0, 1.4, 0], extent=[8, 1.4, 8], lifetimeMin=5,
                       lifetimeMax=9, speedMin=0.05, speedMax=0.22, gravity=[0, 0.1, 0], drag=0.5,
                       turbulence=0.35, sizeStart=0.04, sizeEnd=0.012, colorStart=[1.0, 0.7, 0.36, 1],
                       colorEnd=[1.0, 0.36, 0.42, 0], emissive=1.3))
nodes.append(particles("glints", seed=777, position=[0, 2.8, 0], extent=[8.5, 3.0, 8.5], lifetimeMin=0.2,
                       lifetimeMax=0.7, speedMin=0.0, speedMax=0.04, gravity=[0, 0, 0], drag=2.0, turbulence=0.0,
                       sizeStart=0.026, sizeEnd=0.0, colorStart=[0.85, 0.95, 1.0, 1], colorEnd=[0.6, 0.8, 1.0, 0],
                       emissive=6.0))
nodes.append(particles("sparks", seed=909, shape="sphere", position=[0, 2.6, 0], extent=[1.4, 1.4, 1.4],
                       lifetimeMin=0.25, lifetimeMax=0.8, direction=[0, 0.2, 0], spread=1.0, speedMin=4.0,
                       speedMax=11.0, gravity=[0, -7.0, 0], drag=1.0, turbulence=0.2, sizeStart=0.04,
                       sizeEnd=0.0, colorStart=[0.8, 1.0, 0.3, 1], colorEnd=[0.45, 0.9, 0.1, 0], emissive=7.0,
                       velocityStretch=0.06, stretchMax=0.6))
nodes.append(particles("dust", seed=31, position=[0, 0.9, 0], extent=[13, 0.9, 13], lifetimeMin=4, lifetimeMax=8,
                       speedMin=0.02, speedMax=0.1, gravity=[0, -0.04, 0], drag=0.8, turbulence=0.5,
                       turbulenceScale=0.2, sizeStart=0.04, sizeEnd=0.028, colorStart=[0.5, 0.3, 1.0, 1],
                       colorEnd=[0.2, 0.08, 0.5, 0], emissive=1.4))

_az, _el = math.radians(145.0), math.radians(7.0)
SUN = [round(math.sin(_az) * math.cos(_el), 4), round(math.sin(_el), 4), round(math.cos(_az) * math.cos(_el), 4)]
scene = {
    "format": "avgen-scene", "version": 1, "name": "sonic-garden",
    "camera": {"mode": 1, "position": [-6.8, 1.45, 19.5], "target": [1.3, 2.2, 0.0], "fov": 30.0, "orbitSpeed": 0.0},
    "lightRig": "../lightrigs/sonic-garden.rig.json",
    # The rig is sized to the subject; without a focal point the subject would be the whole 150 m ground.
    "composition": {"focalPoints": [{"name": "hero", "position": [0.0, 2.2, 0.0], "radius": 4.0}]},
    "materialPrograms": programs,
    "lights": [
        # the heart: a light inside the hero, so its glow reaches the ground and the forms round it
        {"id": "heart", "name": "heart", "type": "point", "position": [0.0, 2.6, 0.0], "color": [1, 1, 1],
         "intensity": 0.0, "range": 9.0, "radius": 0.4, "castsShadow": False, "volumetric": 0.4},
        # the note: where the melody is. It rides the pitch (low notes answer low, high notes high) at the
        # garden's edge, so a note lights the part of the world at its own height.
        {"id": "note", "name": "note", "type": "point", "position": [1.6, 1.0, 1.4], "color": [1, 1, 1],
         "intensity": 0.0, "range": 5.5, "radius": 0.2, "castsShadow": False, "volumetric": 0.0},
        # the void's one light: a hard top light over the strike field (impact only)
        {"id": "top", "name": "top", "type": "spot", "position": [0.0, 15.0, 1.5], "direction": [0.0, -1.0, -0.1],
         "color": [1, 1, 1], "intensity": 30.0, "range": 40.0, "innerCone": 12.0, "outerCone": 24.0,
         "castsShadow": True, "volumetric": 0.0},
    ],
    "environment": {
        "intensity": 0.0, "background": [0.004, 0.004, 0.006], "fogColor": [0.0, 0.0, 0.0],
        "volumeDensity": 0.0008, "volumeMaxDistance": 0.0, "skyIntensity": 5.0,
        "sky": {"enabled": True, "zenithColor": [0.0, 0.0, 0.0], "horizonColor": [0.0, 0.0, 0.0],
                "groundColor": [0.003, 0.003, 0.004], "haze": 0.35, "sunColor": [1.0, 0.8, 0.6],
                "sunIntensity": 0.0, "sunSize": 0.035, "sunGlow": 0.12, "intensity": 1.0, "background": True,
                # The sun is placed, not taken from the key: the key's direction is a blend of the families'
                # and moves a little every frame with the sound, and a sun that moves past 0.25 mrad rebuilds
                # the sky's lighting cube (ADR-1022) -- about 6 ms of CPU a frame when it followed the key. It
                # sits where the warm world's key is (azimuth 145, 7 degrees up), the one world that shows it.
                "useKeyLight": False, "sunDirection": SUN}},
    "nodes": nodes,
}

with open(SCENE, "w") as fh:
    json.dump(scene, fh, indent=1)
    fh.write("\n")
print("wrote", SCENE, len(nodes), "nodes,", len(programs), "programs")


# ------------------------------------------------------------------------------------------------ the project
RIG = "lightrig/SonicGarden"
routes = []


def R(src, target, amount, op="add", comp=None, depth=None, depth_inv=False, **chain):
    r = {"source": src, "target": target, "amount": amount, "op": op, "polarity": "unipolar"}
    if comp is not None:
        r["component"] = comp
    if depth:
        r["depthSource"] = depth
        if depth_inv:
            r["depthMin"], r["depthMax"] = 1.0, 0.0
    if chain:
        r["chain"] = chain
    routes.append(r)


def palette(target, colors, **chain):
    """A colour blended from the four world families: one add route per family and channel."""
    for fam, rgb in colors.items():
        for c, v in enumerate(rgb):
            if abs(v) > 1e-6:
                R("visual." + fam, target, v, comp=c, **chain)


def scalar(target, values, **chain):
    for fam, v in values.items():
        if abs(v) > 1e-9:
            R("visual." + fam, target, v, **chain)


SLOW = {"attackMs": 800, "decayMs": 1600}
# a family's forms appear from a weight of 0.2 and are full by 1 (x 1.25 - 0.25, clamped): a world does not carry
# the other worlds' forms as small debris at the 0.05-0.15 weights the families leave each other. (A scalar()
# route multiplies its own amount in, so GROW scales the family's value the same way.)
GROW = dict(SLOW, gain=1.25, offset=-0.25, clampEnabled=True, clampMin=0.0, clampMax=1.0)
MED = {"attackMs": 150, "decayMs": 500}
FAMS = ("organic", "crystalline", "tectonic", "impact")

# ---- the world: sky, sun, fog, light, grade. Four palettes blended by the family weights (tectonic and impact are
# the two faces of the chaotic family, split by mass).
#   organic   dusk: a plum zenith over an ember horizon, a low gold sun BEHIND the subject (a backlight through the
#             haze, which the petals and tendrils catch as rims), and a cool violet fill in the shadows -- warm
#             light against cool shade, the relationship that makes warmth read.
#   crystalline a clear cold night: black-blue zenith, a teal horizon line, a cold high key and a strong rim, clean
#             air, a glossy floor.
#   tectonic  pressure: black-violet air heavy with dust, a low violet glow on the horizon, light from below.
#   impact    the void: black, one hard top light, nothing on the horizon.
palette("env/sky/zenithColor", {"organic": (0.010, 0.004, 0.018), "crystalline": (0.0015, 0.003, 0.011),
                                 "tectonic": (0.004, 0.001, 0.010), "impact": (0.0, 0.0, 0.0)}, **SLOW)
palette("env/sky/horizonColor", {"organic": (0.10, 0.034, 0.016), "crystalline": (0.012, 0.045, 0.075),
                                  "tectonic": (0.03, 0.008, 0.07), "impact": (0.004, 0.004, 0.005)}, **SLOW)
palette("env/sky/sunColor", {"organic": (1.0, 0.44, 0.17)}, **SLOW)
scalar("env/sky/sunIntensity", {"organic": 2.2}, **GROW)
# sunSize and sunGlow are radians (the disc's angular radius and the aureole's width): the warm world's sun is a
# deep orange disc about 3.5 degrees in radius, low behind the giant fungi (a dusk sun looks large and low); the
# other worlds show no sun
scalar("env/sky/sunSize", {"organic": 0.03}, **GROW)
scalar("env/sky/sunGlow", {"organic": 0.14}, **GROW)
scalar("env/sky/haze", {"organic": 0.1, "crystalline": -0.22, "tectonic": -0.05, "impact": -0.25}, **SLOW)
palette("scene/fogColor", {"organic": (0.05, 0.02, 0.016), "crystalline": (0.006, 0.014, 0.028),
                            "tectonic": (0.022, 0.008, 0.05), "impact": (0.003, 0.003, 0.004)}, **SLOW)
scalar("scene/volumeDensity", {"organic": 0.0004, "crystalline": 0.0002, "tectonic": 0.0018, "impact": 0.0003},
       **SLOW)
# The fog only MARCHES (lit air, shafts, the heart's glow in the haze) in the two worlds that are about air: the
# warm hollow's haze and the heavy world's dust. Past the reach the surface pass integrates the same fog in closed
# form (ADR-705), so the observatory and the void keep their distance fog and pay nothing for the march (it is
# 21 ms of a 1080p frame at the default tier). Each world's reach is exactly zero below a weight of 0.2.
for fam in ("organic", "tectonic"):
    R("visual." + fam, "scene/volumeMaxDistance", 90.0, gain=1.25, offset=-0.25, clampEnabled=True, clampMin=0.0,
      clampMax=1.0, **SLOW)

palette(RIG + "/ambientColor", {"organic": (0.16, 0.14, 0.34), "crystalline": (0.1, 0.18, 0.38),
                                 "tectonic": (0.2, 0.08, 0.42), "impact": (0.12, 0.12, 0.12)}, **SLOW)
# key: where the light comes from is part of the world. Azimuth is from world +Z (the camera's side) and the key
# is fixed in the world (not to the camera), so the sun it places in the sky stays put as the camera moves.
# The camera looks along azimuth ~158-162 over the push-in, and screen-right is toward smaller azimuths, so the
# organic sun at 145 sits low, right of the hero, in frame for the whole shot; the heavy world's key rakes in from
# behind the left; the observatory's cold moon is high on the camera's side, out of frame.
scalar(RIG + "/key/azimuth", {"organic": 145.0, "crystalline": 55.0, "tectonic": -150.0, "impact": 90.0}, **SLOW)
scalar(RIG + "/key/elevation", {"organic": 7.0, "crystalline": 38.0, "tectonic": 10.0, "impact": 60.0}, **SLOW)
scalar(RIG + "/key/temperature", {"organic": -4200, "crystalline": 3000, "tectonic": 2500, "impact": 0}, **SLOW)
scalar(RIG + "/key/intensity", {"organic": -0.35, "crystalline": 0.25, "tectonic": -0.55, "impact": -0.9}, **SLOW)
scalar(RIG + "/rim/azimuth", {"organic": -150.0, "crystalline": -150.0, "tectonic": 150.0, "impact": 180.0}, **SLOW)
scalar(RIG + "/rim/temperature", {"organic": -3500, "crystalline": 5000, "tectonic": 4500, "impact": 1500}, **SLOW)
scalar(RIG + "/rim/intensity", {"organic": 0.4, "crystalline": 1.0, "tectonic": 0.2, "impact": -0.6}, **SLOW)
scalar(RIG + "/under/temperature", {"organic": -2000, "tectonic": 8000}, **SLOW)
scalar(RIG + "/under/intensity", {"organic": 0.25, "tectonic": 0.9}, **SLOW)
scalar(RIG + "/fill/intensity", {"organic": 0.25, "crystalline": 0.1}, **SLOW)
scalar(RIG + "/fill/temperature", {"organic": 4500, "crystalline": 3000}, **SLOW)
# grade: split toning in the grade's own terms -- the shadows (lift) toward each world's cool or deep counterpoint,
# the highlights (gain) toward its light
palette("post/grade/lift", {"organic": (0.0, -0.002, 0.006), "crystalline": (-0.003, 0.0, 0.006),
                            "tectonic": (0.004, -0.003, 0.008), "impact": (0.0, 0.0, 0.0)}, **SLOW)
palette("post/grade/gain", {"organic": (0.06, 0.0, -0.06), "crystalline": (-0.04, 0.0, 0.05),
                            "tectonic": (0.04, -0.04, 0.08), "impact": (0.0, 0.0, 0.0)}, **SLOW)
scalar("post/grade/temperature", {"organic": 0.2, "crystalline": -0.3, "tectonic": -0.15}, **SLOW)
scalar("post/grade/tint", {"organic": 0.08, "tectonic": 0.22, "impact": -0.05}, **SLOW)
scalar("post/grade/saturation", {"organic": 0.12, "crystalline": -0.05, "tectonic": 0.25, "impact": -0.5}, **SLOW)
scalar("post/grade/contrast", {"organic": 0.05, "crystalline": 0.14, "tectonic": 0.12, "impact": 0.3}, **SLOW)
scalar("post/bloom/intensity", {"organic": 0.3, "crystalline": 0.35, "tectonic": 0.1, "impact": 0.2}, **SLOW)
scalar("post/halation/intensity", {"organic": 0.9}, **SLOW)
scalar("temporal/echo/strength", {"organic": 0.5, "crystalline": 0.25}, **SLOW)
scalar("temporal/echo/decay", {"organic": 0.86, "crystalline": 0.55}, **SLOW)

# ---- the ground: its roughness and the light in its veins are the world's
scalar("procedural/ground/material/roughness", {"organic": 0.3, "crystalline": -0.42, "tectonic": 0.2,
                                                "impact": 0.25}, **SLOW)
palette("procedural/ground/material/emissiveColor", {"organic": (1.0, 0.42, 0.24), "crystalline": (0.4, 0.75, 1.0),
                                                      "tectonic": (0.5, 0.15, 1.0)}, **SLOW)
scalar("procedural/ground/material/emissive", {"organic": 0.07, "crystalline": 0.03, "tectonic": 0.12}, **SLOW)
# the filter channel opens the veins (inside any world that lights them)
R("visual.glow", "procedural/ground/material/emissive", 0.15, depth="visual.organic", **MED)
R("visual.glow", "procedural/ground/material/emissive", 0.1, depth="visual.tectonic", **MED)
R("visual.energy", "procedural/ground/material/emissive", 0.15, depth="visual.tectonic", attackMs=350,
  decayMs=1200)

# ---- the skylines: each world's far silhouette
for n in ("farcaps", "farstems"):
    R("visual.organic", "procedural/%s/source/scale" % n, 1.0, op="multiply", **GROW)
R("visual.organic", "procedural/farcaps/material/emissive", 0.5, **SLOW)
R("visual.crystalline", "procedural/farspires/source/scale", 1.0, op="multiply", **GROW)
R("visual.crystalline", "procedural/farspires/material/emissive", 0.25, **SLOW)
R("visual.tectonic", "procedural/ridge/source/scale", 1.0, op="multiply", **SLOW)
R("visual.tectonic", "procedural/ridge/material/emissive", 0.4, **SLOW)

# ---- the hero: a small seed in the lotus (organic), a spark in the gem (crystalline), a massive body (tectonic),
# a hard knot (impact)
# (The warm world hides the core quickly and gives it back slowly, so the seed never shares its first seconds with
# it. A chain's envelope follows its OUTPUT after the gain and offset, so a hide -- whose output falls as the
# family rises -- is timed by the decay, and the reveal by the attack. Silence hides the core too; the other worlds
# grow it back in over a fraction of a second as their sound begins.)
R("visual.organic", "procedural/hero/source/scale", 1.0, op="multiply", gain=-1.25, offset=1.0, clampEnabled=True,
  clampMin=0.0, clampMax=1.0, attackMs=1600, decayMs=200)
R("visual.silence", "procedural/hero/source/scale", 1.0, op="multiply", gain=-1.0, offset=1.0, clampEnabled=True,
  clampMin=0.0, clampMax=1.0, attackMs=300, decayMs=2600)
R("visual.crystalline", "procedural/hero/source/scale", 1.0, op="multiply", gain=-0.55, offset=1.0, **SLOW)
R("visual.mass", "procedural/hero/source/scale", 1.0, op="multiply", gain=0.7, offset=1.0, **SLOW)
R("visual.impact", "procedural/hero/source/scale", 1.0, op="multiply", gain=-0.45, offset=1.0, **SLOW)
palette("procedural/hero/material/emissiveColor", {"crystalline": (0.6, 0.85, 1.0),
                                                     "tectonic": (0.45, 0.16, 1.0), "impact": (1.0, 1.0, 0.94)},
        **SLOW)
scalar("procedural/hero/material/roughness", {"crystalline": -0.34, "tectonic": 0.4, "impact": 0.1}, **SLOW)
scalar("procedural/hero/material/emissive", {"crystalline": 0.06, "tectonic": 0.3}, **SLOW)
scalar("procedural/hero/material/metallic", {"crystalline": 0.5, "impact": 0.2}, **SLOW)
palette("procedural/hero/material/baseColor", {"crystalline": (0.16, 0.22, 0.3),
                                                 "tectonic": (0.01, 0.007, 0.02), "impact": (0.06, 0.06, 0.06)},
        **SLOW)
R("visual.energy", "procedural/hero/material/emissive", 0.7, depth="visual.tectonic", attackMs=300, decayMs=1000)
# the seed: its own light, the sound's radiance, and the breath
R("visual.organic", "procedural/seed/material/emissive", 0.9, **SLOW)
R("visual.radiance", "procedural/seed/material/emissive", 0.5, depth="visual.organic", **MED)
R("visual.glow", "procedural/seed/material/emissive", 0.6, depth="visual.organic", **MED)
R("visual.breath", "procedural/seed/deform/2/amount", 0.05, attackMs=300, decayMs=900)
R("visual.grain", "procedural/hero/deform/1/amount", 0.25, attackMs=80, decayMs=400)
R("visual.tectonic", "procedural/hero/deform/1/amount", 0.14, **SLOW)
R("visual.figure", "procedural/hero/deform/3/amount", 0.5, attackMs=200, decayMs=800)
R("visual.edge", "procedural/hero/deform/4/amount", 0.3, attackMs=30, decayMs=250)
R("visual.tension", "procedural/hero/deform/4/amount", 0.25, attackMs=100, decayMs=500)
# the filter channel adds fine detail: a brighter sound roughens the body's skin at a finer scale
R("visual.glow", "procedural/hero/deform/4/scale", 5.0, attackMs=200, decayMs=700)
# lift: the core follows the melody's contour (the typical melodic range, MIDI ~53-83, over 1.1 m)
for n in ("procedural/hero/transform/position", "procedural/seed/transform/position",
          "procedural/facets/transform/position",
          "procedural/halos/transform/position", "procedural/meridians/transform/position", "lights/heart/position"):
    R("visual.lift", n, 1.1, comp=1, gain=2.86, offset=-1.0, clampEnabled=True, clampMin=0.0, clampMax=1.0,
      attackMs=220, decayMs=450)
R("visual.organic", "lights/heart/position", -0.8, comp=1, **SLOW)

R("visual.crystalline", "procedural/facets/source/scale", 1.0, op="multiply", **GROW)
R("visual.radiance", "procedural/facets/material/emissive", 0.25, **MED)
# Spins are rotation routes on the clock (kept from pass 1; ADR-1021 has since made twist speed safe), inside the
# parameter's +-360 over a 31.5 s file.
# The observatory turns at the bell's own ratio: the gem at 10 degrees a second, the ecliptic rings 1.4 times
# faster and the meridians 1.4 times slower the other way (the test bell is FM at 1:1.4).
R("time.seconds", "procedural/facets/transform/rotation", 10.0, comp=1, offset=-16.0)
R("time.seconds", "procedural/halos/transform/rotation", 14.0, comp=1, offset=-16.0)
R("time.seconds", "procedural/meridians/transform/rotation", -10.0 / 1.4, comp=1, offset=-16.0)
R("time.seconds", "procedural/shards/transform/rotation", -10.0, comp=1, offset=-16.0)

# ---- organic
for n in ("stalks", "caps", "gills", "capstems", "petals", "outerpetals", "buds", "seed"):
    R("visual.organic", "procedural/%s/source/scale" % n, 1.0, op="multiply", **GROW)
for n in ("caps", "gills"):
    R("visual.sustain", "procedural/%s/source/scale" % n, 1.0, op="multiply", gain=0.45, offset=0.75, **SLOW)
# every organic surface glows a little by itself, more as the sound opens (the filter channel)
for n, base_e, glow_e in (("stalks", 0.45, 0.5), ("caps", 0.3, 0.35), ("capstems", 0.15, 0.15),
                          ("petals", 0.4, 0.35), ("outerpetals", 0.3, 0.3)):
    R("visual.organic", "procedural/%s/material/emissive" % n, base_e, **SLOW)
    R("visual.glow", "procedural/%s/material/emissive" % n, glow_e, depth="visual.organic", **MED)
R("visual.radiance", "procedural/gills/material/emissive", 0.4, **MED)
R("visual.breath", "procedural/stalks/deform/1/amount", 0.18, attackMs=400, decayMs=1200)
R("visual.breath", "procedural/petals/deform/3/amount", 0.06, attackMs=500, decayMs=1500)
R("visual.breath", "procedural/outerpetals/deform/3/amount", 0.08, attackMs=500, decayMs=1500)
R("visual.swarm", "particles/spores/spawnRate", 70.0, depth="visual.organic", attackMs=300, decayMs=900)

# ---- crystalline
for n in ("prisms", "spires", "halos", "meridians", "cluster"):
    R("visual.crystalline", "procedural/%s/source/scale" % n, 1.0, op="multiply", **GROW)
for n, base_e, glow_e in (("prisms", 0.3, 0.25), ("spires", 0.35, 0.3), ("cluster", 0.4, 0.25), ("facets", 0.35, 0.3)):
    R("visual.crystalline", "procedural/%s/material/emissive" % n, base_e, **SLOW)
    R("visual.glow", "procedural/%s/material/emissive" % n, glow_e, depth="visual.crystalline", **MED)
for n in ("halos", "meridians"):
    R("visual.radiance", "procedural/%s/material/emissive" % n, 0.4, depth="visual.crystalline", **MED)
R("visual.shimmer", "particles/glints/spawnRate", 500.0, depth="visual.crystalline", attackMs=100, decayMs=400)

# ---- chaotic
R("visual.chaotic", "procedural/shards/source/scale", 1.0, op="multiply", **GROW)
R("visual.mass", "procedural/shards/source/scale", 1.0, op="multiply", gain=-0.5, offset=1.0, **SLOW)
R("visual.grain", "procedural/shards/deform/1/amount", 0.6, attackMs=50, decayMs=250)
palette("procedural/shards/material/emissiveColor", {"tectonic": (0.5, 0.15, 1.0), "impact": (1.0, 1.0, 0.9),
                                                       "crystalline": (0.55, 0.85, 1.0), "organic": (1.0, 0.5, 0.3)},
        **SLOW)
R("visual.impact", "procedural/shards/material/emissive", 0.25, **SLOW)
R("visual.tectonic", "procedural/slabs/source/scale", 1.0, op="multiply", **SLOW)
R("visual.tectonic", "procedural/slabs/transform/position", 3.6, comp=1, **SLOW)
R("visual.tectonic", "procedural/slabs/material/emissive", 0.3, **SLOW)
R("visual.energy", "procedural/slabs/transform/position", 0.35, comp=1, depth="visual.tectonic", attackMs=350,
  decayMs=1200)
R("visual.grain", "procedural/slabs/material/emissive", 0.35, depth="visual.tectonic", attackMs=60, decayMs=400)
R("visual.energy", "procedural/ground/deform/2/amount", 0.25, depth="visual.tectonic", attackMs=500, decayMs=1500)
R("visual.tectonic", "particles/dust/spawnRate", 160.0, **SLOW)
R("visual.grain", "particles/sparks/spawnRate", 80.0, depth="visual.impact", attackMs=50, decayMs=300)
R("visual.impact", "procedural/shock/source/scale", 1.0, op="multiply", **SLOW)

# ---- the same MIDI note, different gestures: the family the sound belongs to decides what a note-on looks like,
# and the note's register decides WHERE it answers.
#   organic: a slow swell of light -- low notes in the mushrooms' gills and the lotus, high ones in the tendrils'
#            tips; a puff of spores.
#   crystalline: a precise ring -- low notes ring the inner spires and the gem, high ones the tall outer prisms and
#            the armillary; glints.
#   tectonic: a heave of the mass (below); impact: the audio's transients strike (the next block).
R("notes.noteOn", "procedural/seed/material/emissive", 0.8, depth="visual.organic", attackMs=0, decayMs=1400)
R("notes.noteOn", "procedural/stalks/material/emissive", 1.6, depth="visual.organicHi", attackMs=0, decayMs=1600)
R("notes.noteOn", "procedural/gills/material/emissive", 1.4, depth="visual.organicLo", attackMs=0, decayMs=1800)
R("notes.noteOn", "procedural/caps/material/emissive", 0.8, depth="visual.organicLo", attackMs=0, decayMs=1800)
R("notes.noteOn", "procedural/petals/material/emissive", 0.7, depth="visual.organic", attackMs=0, decayMs=1500)
R("notes.noteOn", "particles/spores/burst", 6.0, depth="visual.organic", attackMs=0, decayMs=120)
R("notes.noteOn", "procedural/prisms/material/emissive", 1.4, depth="visual.crystalHi", attackMs=0, decayMs=90)
R("notes.noteOn", "procedural/spires/material/emissive", 1.6, depth="visual.crystalLo", attackMs=0, decayMs=90)
R("notes.noteOn", "procedural/cluster/material/emissive", 1.2, depth="visual.crystalline", attackMs=0, decayMs=90)
R("notes.noteOn", "procedural/halos/material/emissive", 5.0, depth="visual.crystalHi", attackMs=0, decayMs=320)
R("notes.noteOn", "procedural/meridians/material/emissive", 5.0, depth="visual.crystalLo", attackMs=0, decayMs=320)
R("notes.noteOn", "procedural/facets/material/emissive", 1.2, depth="visual.crystalline", attackMs=0, decayMs=150)
R("notes.noteOn", "particles/glints/burst", 25.0, depth="visual.crystalline", attackMs=0, decayMs=60)
R("notes.noteOn", "procedural/shards/material/emissive", 0.25, depth="visual.tectonic", attackMs=0, decayMs=200)
# weight: a note does not light the heavy world's hero, it swells it -- a slow heave of the mass itself -- and
# opens its fissures a little
R("notes.noteOn", "procedural/hero/source/scale", 1.0, op="multiply", gain=0.07, offset=1.0, depth="visual.tectonic",
  attackMs=120, decayMs=700)
R("notes.noteOn", "procedural/hero/material/emissive", 0.5, depth="visual.tectonic", attackMs=100, decayMs=900)
# the note light rides the pitch at the garden's edge (0.4 m for the lowest notes, 4.4 m for the highest) and
# takes the world's colour; it answers each note in the world's own tempo
R("notes.pitch", "lights/note/position", 4.0, comp=1, gain=2.86, offset=-1.0, clampEnabled=True, clampMin=0.0,
  clampMax=1.0, attackMs=60, decayMs=60)
palette("lights/note/color", {"organic": (1.0, 0.62, 0.32), "crystalline": (0.6, 0.85, 1.0),
                              "tectonic": (0.55, 0.2, 1.0), "impact": (1.0, 1.0, 0.9)}, **SLOW)
R("notes.noteOn", "lights/note/intensity", 5.0, depth="visual.organic", attackMs=0, decayMs=1200)
R("notes.noteOn", "lights/note/intensity", 8.0, depth="visual.crystalline", attackMs=0, decayMs=200)

# ---- the audio's own impacts: what the transient sounded like, not that a note began.
R("sonic.transient", "procedural/shards/transform/scale", 0.35, depth="visual.impact", threshold="gate",
  thresholdLevel=0.6, attackMs=0, decayMs=380)
R("sonic.transient", "procedural/shards/material/emissive", 1.6, depth="visual.impact", attackMs=0, decayMs=70)
R("sonic.transient", "particles/sparks/burst", 90.0, depth="visual.impact", attackMs=0, decayMs=45)
R("sonic.transient", "procedural/hero/material/emissive", 1.0, depth="visual.impact", attackMs=0, decayMs=70)
# the shock ring: at the hit it is small and white-hot; as the envelope decays it races out along the ground and
# fades (scale x (1 - 0.95 env), light x env)
R("sonic.transient", "procedural/shock/transform/scale", 1.0, op="multiply", gain=-0.95, offset=1.0,
  depth="visual.impact", threshold="gate", thresholdLevel=0.45, attackMs=0, decayMs=420)
R("sonic.transient", "procedural/shock/material/emissive", 6.0, depth="visual.impact", threshold="gate",
  thresholdLevel=0.45, attackMs=0, decayMs=420)
# The top light is authored at 30 cd so its parameter reaches 600 (a light's range is 20x its authored value); it
# exists only in the void (x impact), sits at a stage level there, and every strike slams it up and lets it fall.
R("visual.impact", "lights/top/intensity", 1.0, op="multiply", **SLOW)
R("visual.impact", "lights/top/intensity", 300.0, **SLOW)
R("sonic.transient", "lights/top/intensity", 270.0, depth="visual.impact", attackMs=0, decayMs=140)

# ---- musical context, in every world: MIDI says what happened (these read notes.* only)
# sustained notes: the world grows and opens, and the trails lengthen
R("visual.sustain", "procedural/stalks/source/scale", 1.0, op="multiply", comp=1, gain=0.5, offset=0.75,
  attackMs=900, decayMs=1500)
R("visual.sustain", "procedural/prisms/source/scale", 1.0, op="multiply", comp=1, gain=0.45, offset=0.78,
  attackMs=900, decayMs=1500)
R("visual.sustain", "procedural/spires/source/scale", 1.0, op="multiply", comp=1, gain=0.45, offset=0.78,
  attackMs=900, decayMs=1500)
R("visual.sustain", "particles/spores/lifetime", 1.0, op="multiply", gain=1.0, offset=0.7, **SLOW)
R("visual.sustain", "temporal/echo/strength", 0.12, attackMs=600, decayMs=1200)
# the lotus opens on held notes and closes round its seed on quick ones
R("visual.sustain", "procedural/outerpetals/deform/1/amount", -0.15, attackMs=900, decayMs=1500)
R("visual.sustain", "procedural/petals/deform/1/amount", -0.17, attackMs=900, decayMs=1500)
# an arpeggio: patterned motion -- the motes circle the core, the tendrils ripple, the armillary tilts
R("visual.figure", "particles/spores/orbit", 2.6, attackMs=400, decayMs=1200)
R("visual.figure", "particles/spores/spawnRate", 50.0, depth="visual.organic", attackMs=300, decayMs=900)
R("visual.figure", "procedural/stalks/deform/1/frequency", 2.2, attackMs=400, decayMs=1000)
R("visual.figure", "procedural/stalks/deform/1/amount", 0.07, attackMs=300, decayMs=900)
R("visual.figure", "procedural/halos/transform/rotation", 28.0, comp=0, attackMs=500, decayMs=1200)
R("visual.figure", "procedural/meridians/transform/rotation", -18.0, comp=2, attackMs=500, decayMs=1200)
R("visual.figure", "particles/glints/spawnRate", 300.0, depth="visual.crystalline", attackMs=200, decayMs=600)
R("visual.figure", "particles/sparks/spawnRate", 60.0, depth="visual.impact", attackMs=100, decayMs=400)
# dense chords: mass and layering -- the core swells, the petals and rings open wide, every chord heaves the light
R("visual.stack", "procedural/hero/source/scale", 1.0, op="multiply", gain=0.25, offset=1.0, attackMs=300,
  decayMs=900)
R("visual.stack", "procedural/facets/source/scale", 1.0, op="multiply", gain=0.25, offset=1.0, attackMs=300,
  decayMs=900)
R("visual.stack", "procedural/outerpetals/source/scale", 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=400,
  decayMs=1000)
R("visual.stack", "procedural/outerpetals/deform/1/amount", -0.13, attackMs=400, decayMs=1000)
for n in ("halos", "meridians"):
    R("visual.stack", "procedural/%s/source/scale" % n, 1.0, op="multiply", gain=0.35, offset=1.0, attackMs=300,
      decayMs=900)
for n in ("caps", "gills"):
    R("visual.stack", "procedural/%s/source/scale" % n, 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=400,
      decayMs=1000)
# the chord layers rise and fall with the density of the harmony
R("visual.stack", "procedural/buds/source/scale", 1.0, op="multiply", attackMs=500, decayMs=1500)
R("visual.stack", "procedural/cluster/source/scale", 1.0, op="multiply", attackMs=500, decayMs=1500)
R("notes.noteOn", "lights/heart/intensity", 6.0, depth="visual.stack", attackMs=0, decayMs=650)
R("notes.noteOn", "particles/spores/burst", 30.0, depth="visual.stack", attackMs=0, decayMs=90)
R("notes.noteOn", "particles/glints/burst", 40.0, depth="visual.stack", attackMs=0, decayMs=70)

# Each particle system belongs to a family: gestures anywhere may spawn it, but only its own world lets it shine
# (additive particles with no emission are invisible), so a chord's burst of motes is warm in the garden and absent
# from the heavy world.
for n, fam in (("spores", "organic"), ("glints", "crystalline"), ("dust", "tectonic"), ("sparks", "impact")):
    R("visual." + fam, "particles/%s/emissive" % n, 1.0, op="multiply", **SLOW)

# ---- the heart: a point light inside the hero, so its glow reaches the ground and the forms around it
palette("lights/heart/color", {"organic": (1.0, 0.6, 0.3), "crystalline": (0.6, 0.85, 1.0),
                               "tectonic": (0.55, 0.2, 1.0), "impact": (0.9, 1.0, 0.75)}, **SLOW)
# Kept local on purpose: a creative-critic pass on the first render found the whole frame brightening with the
# onsets (98% of regions, 12% of mean luma), which reads as exposure flicker, not as the world responding.
R("visual.radiance", "lights/heart/intensity", 2.5, **MED)
R("visual.organic", "lights/heart/intensity", 0.8, **SLOW)
R("notes.noteOn", "lights/heart/intensity", 2.5, depth="visual.organic", attackMs=0, decayMs=1400)
R("notes.noteOn", "lights/heart/intensity", 7.0, depth="visual.crystalline", attackMs=0, decayMs=240)
R("notes.noteOn", "lights/heart/intensity", 5.0, depth="visual.tectonic", attackMs=0, decayMs=300)
R("sonic.transient", "lights/heart/intensity", 20.0, depth="visual.impact", attackMs=0, decayMs=90)

# ---- the camera: weight lowers it and tips it up at the hero
R("visual.mass", "camera/position", -1.0, comp=1, **SLOW)
R("visual.mass", "camera/target", 0.45, comp=1, **SLOW)

with open(MASTER) as fh:
    master = json.load(fh)

world = {"kind": "interpret", "name": "world", "settings": {"mappings": WORLD_MAPPINGS, "groups": {}}}
garden = {"kind": "interpret", "name": "garden", "settings": {"mappings": GARDEN_MAPPINGS,
                                                              "groups": {"family": {"sharpness": 3.0}}}}
master["sonic"] = {"notes": "notes/phrase.mid", "character": CHARACTER}
master["sources"] = [garden, world]
master["routes"] = routes
master["parameters"] = {
    "post/bloom/enabled": True, "post/bloom/intensity": 0.4, "post/bloom/threshold": 1.0,
    "post/bloom/emissionWeight": 0.7, "post/tonemap/chroma-retention": 0.6,
    "post/output/vignette": 0.5, "post/output/grain": 0.012,
    "post/halation/enabled": True, "post/halation/intensity": 0.0,
    "temporal/echo/enabled": True, "temporal/echo/strength": 0.0, "temporal/echo/decay": 0.0,
    "temporal/echo/frames": 8.0,
    "camera/exposure/mode": 0, "camera/exposure/compensation": -0.45,
    "env/sky/enabled": True, "env/sky/background": True, "env/sky/intensity": 0.18,
    "lightrig/SonicGarden/keyIntensity": 2.6,
    "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/useExplicitFov": False,
    "scene/volumeJitter": 0.4, "scene/volumeSteps": 48,
    "camera/lens/focalLength": 50.0, "camera/lens/aperture": 2.0, "camera/focus/mode": 2,
}
# The camera: one slow push-in, authored over the 21.5 s phrase (sonic_garden_variants.py stretches it to each
# file's length): from a low, wide view off the hero's left shoulder, in and slightly up, with the hero held left of
# centre and the world opening to the right. A 50 mm lens compresses the layers (foreground forms, the hero, the
# skyline) instead of spreading them like a wide lens; the camera moves about 7 m in 21 s.
cam = [(0.0, (-6.8, 1.45, 19.5), (1.3, 2.2, 0.0)),
       (10.75, (-5.3, 1.6, 16.6), (1.0, 2.25, 0.0)),
       (21.5, (-3.6, 1.85, 13.4), (0.7, 2.3, 0.0))]
master["timeline"] = {"enabled": True, "cues": [], "tracks": [
    {"target": "camera/position", "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
     "loopLength": 0.0, "keys": [{"time": t, "value": list(p), "interp": "smooth"} for t, p, _ in cam]},
    {"target": "camera/target", "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
     "loopLength": 0.0, "keys": [{"time": t, "value": list(q), "interp": "smooth"} for t, _, q in cam]}]}
with open(MASTER, "w") as fh:
    json.dump(master, fh, indent=1)
    fh.write("\n")
print("wrote", MASTER, len(routes), "routes")
