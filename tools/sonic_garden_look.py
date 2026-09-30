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
  3. Qualities (medium tier): mass, radiance, grain, edge, shimmer, breath, energy, tension, swarm.
  4. Musical context (notes.* only): sustain, figure (an arpeggio's patterned motion), stack (dense chords), lift
     (pitch). And the MIDI note-on itself, whose visual gesture is chosen by the family through a route's depth:
     a swell in the organic world, a ring in the crystalline one, a heave in the heavy one; in the light one the
     audio's own transients strike instead.
Everything here is an artistic choice (brief §27), and every weight is also a live parameter.
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
    # families: products are ANDs -- warm AND smooth AND not bright AND not inharmonic is organic; bright-ish AND
    # inharmonic AND clean is crystalline (the bell, and the morph's bright and resonant stages); chaotic is a mean
    # of roughness (doubled), sharpness and the absence of smoothness, lifted off the floor by bias/gain.
    M("organic", [("sonic.warmth.slow", 1.0, False), ("sonic.smoothness.slow", 1.0, False),
                  ("sonic.brightness.slow", 1.5, True), ("sonic.inharmonicity.slow", 1.0, True)],
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
]
# Listed after "garden", so it reads this frame's families (a source reads an earlier source's outputs in the same
# frame, but its own only a frame late).
WORLD_MAPPINGS = [
    M("tectonic", [("visual.chaotic", 1.0, False), ("visual.mass", 1.0, False)], "product"),
    M("impact", [("visual.chaotic", 1.0, False), ("visual.mass", 1.0, True)], "product"),
]


def xf(pos=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
    return {"position": list(pos), "rotation": list(rot), "scale": list(scale)}


def mat(base, emissive=(0, 0, 0), ei=0.0, rough=0.6, metal=0.0):
    return {"baseColor": list(base), "emissiveColor": list(emissive), "emissiveIntensity": ei, "roughness": rough,
            "metallic": metal}


def proc(name, source, dist, dxf, material, **kw):
    p = {"source": source, "distribution": dist, "distributionTransform": dxf, "material": material}
    for k, v in kw.items():
        if v is not None:
            p[k] = v
    return {"name": name, "kind": "procedural", "procedural": p}


def cap_profile(n=11, power=0.55, height=0.3):
    """A mushroom cap as a lathe: a tube on a straight curve whose per-point scale draws cos(u)^power."""
    pts = []
    for i in range(n):
        u = i / (n - 1)
        s = max(0.02, math.cos(u * math.pi / 2) ** power)
        pts.append({"position": [0.0, round(height * u, 4), 0.0], "scale": round(s, 4), "roll": 0.0})
    return pts


# ------------------------------------------------------------------------------------------------ the scene
nodes = []


# The ground: a broad, gently undulating plain. Dark, rough enough to catch the rim and the emissive spill, so the
# forms stand on something; the undulation gives the rim light a surface to rake across. Its second deformer is a
# slow swell that only the heavy (tectonic) world raises.
nodes.append(proc("ground",
                  {"kind": "box", "size": [150, 0.6, 150], "subdivisions": 64},
                  {"kind": "single"}, xf((0, -0.3, 0)),
                  mat((0.016, 0.015, 0.016), rough=0.7),
                  deformers=[{"kind": "noise", "amount": 0.45, "scale": 0.05, "speed": 0.0, "seed": 17,
                              "axisMask": [0, 1, 0]},
                             {"kind": "sine", "amount": 0.0, "frequency": 0.32, "speed": 0.9,
                              "axis": [1, 0, 0.4], "displacementAxis": [0, 1, 0], "space": "world"}]))

# The horizon: a far ring of dark masses, silhouettes against the sky gradient. Shared by every world; the sky
# behind them is what changes.
nodes.append(proc("horizon",
                  {"kind": "box", "size": [26, 9, 14], "subdivisions": 2, "bevel": 3.0, "bevelSegments": 2},
                  {"kind": "radial", "count": 30, "radius": 115, "orientation": "outward"},
                  xf((0, -2.5, 0)),
                  mat((0.01, 0.01, 0.012), rough=0.92),
                  variation={"seed": 41, "scale": [0.6, 0.8, 0.5], "rotation": [0.12, 1.2, 0.12],
                             "position": [10, 0, 10]}))

# ---- the hero: two superimposed forms, each held by its family, so a sound between families shows both.
nodes.append(proc("hero",
                  {"kind": "sphere", "radius": 1.25, "segments": 160, "rings": 120},
                  {"kind": "single"}, xf((0, 2.6, 0)),
                  mat((0.02, 0.02, 0.02), (0.0, 0.0, 0.0), 0.0, rough=0.42),
                  deformers=[{"kind": "noise", "amount": 0.0, "scale": 1.4, "speed": 0.5, "seed": 11},
                             {"kind": "sine", "amount": 0.02, "frequency": 1.6, "speed": 0.55, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0.3, 1]},
                             {"kind": "twist", "amount": 0.0, "speed": 0.0, "axis": [0, 1, 0],
                              "center": [0, 0, 0]},
                             {"kind": "displacement", "amount": 0.0, "scale": 4.0, "speed": 0.25, "seed": 3}]))

# The crystalline hero: a compound of chamfered cubes (boxes are the one faceted primitive), each level turned
# against the last -- a cut gem around the core. It turns slowly and exactly.
nodes.append(proc("facets",
                  {"kind": "box", "size": [3.1, 3.1, 3.1], "subdivisions": 1, "bevel": 0.45, "bevelSegments": 1},
                  {"kind": "single"}, xf((0, 2.6, 0)),
                  mat((0.1, 0.13, 0.18), (0.55, 0.85, 1.0), 0.03, rough=0.1, metal=0.35),
                  hierarchy={"recursionDepth": 2, "scalePerLevel": 0.98, "offsetPerLevel": [0, 0, 0],
                             "rotationPerLevel": [35.26, 45.0, 0.0], "colorPerLevel": False}))

# ---- organic: stalks, caps on stems, petals
nodes.append(proc("stalks",
                  {"kind": "tube", "tubeRadius": 0.075, "tubeTaper": 0.1, "tubeSides": 9, "tubeSegments": 28,
                   "tubeTwist": 0.4, "tubeCaps": True,
                   "curve": {"kind": "catmullRom", "generator": "noise", "count": 8, "start": [0, 0, 0],
                             "end": [0.0, 3.3, 0.9], "noiseAmount": 0.3, "noiseScale": 0.9, "seed": 5,
                             "samplesPerSegment": 8}},
                  {"kind": "radial", "count": 18, "radius": 3.5, "orientation": "outward"},
                  xf((0, 0, 0)),
                  mat((0.035, 0.022, 0.018), (1.0, 0.5, 0.22), 0.05, rough=0.55),
                  variation={"seed": 5, "rotation": [0.2, 0.5, 0.2], "scale": [0.25, 0.4, 0.25],
                             "position": [0.7, 0, 0.7]},
                  materialVariation={"valueRandom": 0.2, "emissiveRandom": 0.7},
                  deformers=[{"kind": "sine", "amount": 0.06, "frequency": 0.9, "speed": 0.5, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0, 0.6]}]))

cap = {"kind": "tube", "tubeRadius": 0.6, "tubeTaper": 1.0, "tubeSides": 28, "tubeSegments": 12, "tubeTwist": 0.0,
       "tubeCaps": True, "curve": {"kind": "catmullRom", "generator": "points", "points": cap_profile(),
                                   "samplesPerSegment": 6}}
CAPS = {"kind": "radial", "count": 9, "radius": 5.9, "orientation": "outward", "startAngle": 0.2}
CAPVAR = {"seed": 9, "scale": [0.35, 0.35, 0.35], "position": [0.8, 0.0, 0.8], "rotation": [0.0, 1.0, 0.0]}
nodes.append(proc("caps", cap, CAPS, xf((0, 0.62, 0)),
                  mat((0.16, 0.06, 0.08), (1.0, 0.32, 0.45), 0.1, rough=0.5),
                  variation=CAPVAR, materialVariation={"emissiveRandom": 0.5},
                  deformers=[{"kind": "sine", "amount": 0.03, "frequency": 2.0, "speed": 0.7, "axis": [0, 1, 0],
                              "displacementAxis": [1, 0, 1]}]))
nodes.append(proc("capstems",
                  {"kind": "tube", "tubeRadius": 0.07, "tubeTaper": 0.7, "tubeSides": 8, "tubeSegments": 10,
                   "tubeTwist": 0.0, "tubeCaps": True,
                   "curve": {"kind": "catmullRom", "generator": "line", "count": 4, "start": [0, 0, 0],
                             "end": [0.0, 0.66, 0.0], "samplesPerSegment": 6}},
                  CAPS, xf((0, 0, 0)),
                  mat((0.1, 0.07, 0.06), (1.0, 0.45, 0.3), 0.1, rough=0.7), variation=CAPVAR))

# Petals cradling the hero: flattened lobes, tipped up and out, that sway with the breath.
nodes.append(proc("petals",
                  {"kind": "sphere", "radius": 1.0, "segments": 40, "rings": 24},
                  {"kind": "radial", "count": 7, "radius": 1.5, "orientation": "outward"},
                  xf((0, 0.75, 0)),
                  mat((0.2, 0.07, 0.09), (1.0, 0.34, 0.42), 0.05, rough=0.55),
                  sourceTransform=xf((0.0, 0.0, 0.55), (-38, 0, 0), (0.55, 0.05, 1.25)),
                  variation={"seed": 13, "rotation": [0.05, 0.12, 0.05], "scale": [0.12, 0.1, 0.12]},
                  deformers=[{"kind": "sine", "amount": 0.05, "frequency": 1.2, "speed": 0.45, "axis": [0, 0, 1],
                              "displacementAxis": [0, 1, 0]}]))

# ---- crystalline: two ordered rings of chamfered prisms (the chamfer is what makes them catch light as facets),
# and three precise orbital rings that precess around the hero.
# A cube stood on its vertex and stretched upright is a rhombohedral crystal: pointed at both ends, six flat
# faces, and a chamfer on every edge to catch a line of light.
GEM = {"kind": "box", "size": [0.8, 0.8, 0.8], "subdivisions": 1, "bevel": 0.05, "bevelSegments": 1}
ON_VERTEX = xf(rot=(45.0, 0.0, 35.264))
nodes.append(proc("prisms", GEM,
                  {"kind": "radial", "count": 12, "radius": 7.6, "orientation": "outward"},
                  xf((0, 1.9, 0), scale=(1.0, 3.2, 1.0)),
                  mat((0.05, 0.065, 0.1), (0.35, 0.7, 1.0), 0.03, rough=0.42, metal=0.1),
                  sourceTransform=ON_VERTEX,
                  variation={"seed": 21, "scale": [0.2, 0.4, 0.2], "rotation": [0.0, 0.6, 0.0]},
                  materialVariation={"hueGradient": 0.04, "valueRandom": 0.2}))
nodes.append(proc("spires", GEM,
                  {"kind": "radial", "count": 12, "radius": 4.7, "orientation": "outward", "startAngle": 0.2618},
                  xf((0, 1.1, 0), scale=(0.6, 1.9, 0.6)),
                  mat((0.05, 0.065, 0.1), (0.35, 0.7, 1.0), 0.03, rough=0.42, metal=0.1),
                  sourceTransform=ON_VERTEX,
                  variation={"seed": 22, "scale": [0.2, 0.4, 0.2], "rotation": [0.15, 0.6, 0.15]},
                  materialVariation={"hueGradient": 0.05, "valueRandom": 0.2}))
nodes.append(proc("halos",
                  {"kind": "torus", "majorRadius": 2.7, "minorRadius": 0.02, "majorSegments": 192,
                   "minorSegments": 8},
                  {"kind": "radial", "count": 3, "radius": 0.001, "orientation": "none"},
                  xf((0, 2.6, 0)),
                  mat((0.3, 0.35, 0.45), (0.7, 0.9, 1.0), 0.6, rough=0.1, metal=0.6),
                  variation={"seed": 77, "rotation": [1.1, 3.1, 1.1], "scale": [0.3, 0.0, 0.3]}))

# ---- chaotic: blades in a vortex around the hero (impact), slabs breaking up out of the ground (weight)
nodes.append(proc("shards",
                  {"kind": "box", "size": [0.05, 0.75, 0.24], "subdivisions": 1},
                  {"kind": "spiral", "count": 110, "radius": 1.9, "radiusGrowth": 1.4, "turns": 5,
                   "spiralHeight": 3.8, "orientation": "tangent"},
                  xf((0, 0.8, 0)),
                  mat((0.03, 0.03, 0.035), (0.0, 0.0, 0.0), 0.0, rough=0.15, metal=0.8),
                  variation={"seed": 33, "position": [0.7, 0.5, 0.7], "rotation": [3.1, 3.1, 3.1],
                             "scale": [0.5, 0.7, 0.5]},
                  materialVariation={"emissiveRandom": 0.85, "valueRandom": 0.3},
                  deformers=[{"kind": "noise", "amount": 0.0, "scale": 0.9, "speed": 1.5, "seed": 7}]))
nodes.append(proc("slabs",
                  {"kind": "box", "size": [1.8, 4.6, 0.7], "subdivisions": 8, "bevel": 0.06, "bevelSegments": 1},
                  {"kind": "radial", "count": 9, "radius": 7.8, "orientation": "outward", "startAngle": 0.35},
                  xf((0, -2.4, 0)),
                  mat((0.02, 0.018, 0.03), (0.45, 0.15, 1.0), 0.0, rough=0.55, metal=0.1),
                  variation={"seed": 51, "rotation": [0.4, 0.7, 0.4], "scale": [0.35, 0.45, 0.35],
                             "position": [1.4, 0.4, 1.4]},
                  materialVariation={"emissiveRandom": 0.7, "valueRandom": 0.3},
                  deformers=[{"kind": "displacement", "amount": 0.14, "scale": 1.6, "speed": 0.0, "seed": 5}]))


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
                       turbulence=0.35, sizeStart=0.045, sizeEnd=0.015, colorStart=[1.0, 0.62, 0.3, 1],
                       colorEnd=[1.0, 0.3, 0.4, 0], emissive=1.3))
nodes.append(particles("glints", seed=777, position=[0, 2.8, 0], extent=[8.5, 3.0, 8.5], lifetimeMin=0.2,
                       lifetimeMax=0.7, speedMin=0.0, speedMax=0.04, gravity=[0, 0, 0], drag=2.0, turbulence=0.0,
                       sizeStart=0.03, sizeEnd=0.0, colorStart=[0.85, 0.95, 1.0, 1], colorEnd=[0.6, 0.8, 1.0, 0],
                       emissive=6.0))
nodes.append(particles("sparks", seed=909, shape="sphere", position=[0, 2.6, 0], extent=[1.4, 1.4, 1.4],
                       lifetimeMin=0.25, lifetimeMax=0.8, direction=[0, 0.2, 0], spread=1.0, speedMin=4.0,
                       speedMax=11.0, gravity=[0, -7.0, 0], drag=1.0, turbulence=0.2, sizeStart=0.05,
                       sizeEnd=0.0, colorStart=[0.8, 1.0, 0.3, 1], colorEnd=[0.45, 0.9, 0.1, 0], emissive=7.0,
                       velocityStretch=0.06, stretchMax=0.6))
nodes.append(particles("dust", seed=31, position=[0, 0.9, 0], extent=[13, 0.9, 13], lifetimeMin=4, lifetimeMax=8,
                       speedMin=0.02, speedMax=0.1, gravity=[0, -0.04, 0], drag=0.8, turbulence=0.5,
                       turbulenceScale=0.2, sizeStart=0.045, sizeEnd=0.03, colorStart=[0.5, 0.3, 1.0, 1],
                       colorEnd=[0.2, 0.08, 0.5, 0], emissive=1.4))

scene = {
    "format": "avgen-scene", "version": 1, "name": "sonic-garden",
    "camera": {"mode": 1, "position": [-10.5, 1.7, 15.5], "target": [0.0, 2.3, 0.0], "fov": 38.0, "orbitSpeed": 0.0},
    "lightRig": "../lightrigs/sonic-garden.rig.json",
    # The rig is sized to the subject; without a focal point the subject would be the whole 150 m ground.
    "composition": {"focalPoints": [{"name": "hero", "position": [0.0, 2.6, 0.0], "radius": 4.0}]},
    "lights": [{"id": "heart", "name": "heart", "type": "point", "position": [0.0, 2.6, 0.0], "color": [1, 1, 1],
                "intensity": 0.0, "range": 9.0, "radius": 0.4, "castsShadow": False, "volumetric": 0.4}],
    "environment": {
        "intensity": 0.0, "background": [0.004, 0.004, 0.006], "fogColor": [0.0, 0.0, 0.0],
        "volumeDensity": 0.0008, "volumeMaxDistance": 90.0, "skyIntensity": 5.0,
        "sky": {"enabled": True, "zenithColor": [0.0, 0.0, 0.0], "horizonColor": [0.0, 0.0, 0.0],
                "groundColor": [0.003, 0.003, 0.004], "haze": 0.35, "sunColor": [1.0, 0.8, 0.6],
                "sunIntensity": 0.0, "sunSize": 0.3, "sunGlow": 0.6, "intensity": 1.0, "background": True,
                "useKeyLight": False}},
    "nodes": nodes,
}

with open(SCENE, "w") as f:
    json.dump(scene, f, indent=1)
    f.write("\n")
print("wrote", SCENE, len(nodes), "nodes")


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
MED = {"attackMs": 150, "decayMs": 500}

# ---- the world: sky, fog, light, grade. Four palettes blended by the family weights (tectonic and impact are the
# two faces of the chaotic family, split by mass).
palette("env/sky/zenithColor", {"organic": (0.016, 0.006, 0.022), "crystalline": (0.002, 0.005, 0.016),
                                 "tectonic": (0.004, 0.001, 0.012), "impact": (0.0, 0.0, 0.0)}, **SLOW)
palette("env/sky/horizonColor", {"organic": (0.15, 0.055, 0.03), "crystalline": (0.025, 0.055, 0.1),
                                  "tectonic": (0.035, 0.01, 0.07), "impact": (0.01, 0.011, 0.012)}, **SLOW)
palette("scene/fogColor", {"organic": (0.06, 0.026, 0.02), "crystalline": (0.008, 0.016, 0.03),
                            "tectonic": (0.02, 0.008, 0.045), "impact": (0.005, 0.005, 0.006)}, **SLOW)
scalar("scene/volumeDensity", {"organic": 0.0015, "crystalline": 0.0002, "tectonic": 0.0026, "impact": 0.0004},
       **SLOW)
scalar("env/sky/haze", {"organic": 0.5, "crystalline": -0.2, "tectonic": 0.3, "impact": -0.25}, **SLOW)
palette(RIG + "/ambientColor", {"organic": (0.35, 0.18, 0.2), "crystalline": (0.12, 0.2, 0.4),
                                 "tectonic": (0.2, 0.08, 0.4), "impact": (0.16, 0.17, 0.15)}, **SLOW)
scalar(RIG + "/key/temperature", {"organic": -3900, "crystalline": 4500, "tectonic": 2500, "impact": 500}, **SLOW)
scalar(RIG + "/rim/temperature", {"organic": -3800, "crystalline": 6000, "tectonic": 5000, "impact": 1000}, **SLOW)
scalar(RIG + "/under/temperature", {"organic": -1800, "tectonic": 8000}, **SLOW)
scalar(RIG + "/under/intensity", {"organic": 0.35, "tectonic": 0.6}, **SLOW)
scalar(RIG + "/rim/intensity", {"crystalline": 0.7, "impact": 0.4, "tectonic": -0.3}, **SLOW)
scalar(RIG + "/key/intensity", {"tectonic": -0.35, "impact": -0.4}, **SLOW)
scalar(RIG + "/key/elevation", {"tectonic": -22, "impact": 30}, **SLOW)
scalar("post/grade/temperature", {"organic": 0.35, "crystalline": -0.35, "tectonic": -0.15}, **SLOW)
scalar("post/grade/tint", {"organic": 0.12, "tectonic": 0.25, "impact": -0.1}, **SLOW)
scalar("post/grade/saturation", {"organic": 0.1, "crystalline": -0.05, "tectonic": 0.3, "impact": -0.45}, **SLOW)
scalar("post/grade/contrast", {"crystalline": 0.12, "tectonic": 0.1, "impact": 0.3}, **SLOW)
scalar("post/bloom/intensity", {"organic": 0.25, "crystalline": 0.35, "impact": 0.2}, **SLOW)
scalar("post/halation/intensity", {"organic": 0.9}, **SLOW)
scalar("temporal/echo/strength", {"organic": 0.55, "crystalline": 0.25}, **SLOW)
scalar("temporal/echo/decay", {"organic": 0.86, "crystalline": 0.55}, **SLOW)

# ---- the hero: its body swells with weight and yields to the facets as the sound turns crystalline
R("visual.crystalline", "procedural/hero/source/scale", 1.0, op="multiply", gain=-0.55, offset=1.0, **SLOW)
R("visual.mass", "procedural/hero/source/scale", 1.0, op="multiply", gain=0.7, offset=1.0, **SLOW)
R("visual.impact", "procedural/hero/source/scale", 1.0, op="multiply", gain=-0.4, offset=1.0, **SLOW)
palette("procedural/hero/material/emissiveColor", {"organic": (1.0, 0.4, 0.2), "crystalline": (0.55, 0.8, 1.0),
                                                     "tectonic": (0.5, 0.12, 1.0), "impact": (1.0, 1.0, 0.92)},
        **SLOW)
scalar("procedural/hero/material/roughness", {"crystalline": -0.34, "tectonic": 0.35, "impact": 0.1}, **SLOW)
scalar("procedural/hero/material/emissive", {"organic": 0.22, "crystalline": 0.04}, **SLOW)
scalar("procedural/hero/material/metallic", {"crystalline": 0.5, "impact": 0.3}, **SLOW)
palette("procedural/hero/material/baseColor", {"organic": (0.26, 0.09, 0.06), "crystalline": (0.16, 0.22, 0.3),
                                                 "tectonic": (0.012, 0.008, 0.03), "impact": (0.02, 0.02, 0.02)},
        **SLOW)
R("visual.radiance", "procedural/hero/material/emissive", 0.3, depth="visual.organic", **MED)
R("visual.grain", "procedural/hero/deform/1/amount", 0.5, attackMs=80, decayMs=400)
R("visual.tectonic", "procedural/hero/deform/1/amount", 0.25, **SLOW)
R("visual.breath", "procedural/hero/deform/2/amount", 0.09, depth="visual.organic", attackMs=300, decayMs=900)
R("visual.figure", "procedural/hero/deform/3/amount", 0.5, attackMs=200, decayMs=800)
R("visual.edge", "procedural/hero/deform/4/amount", 0.3, attackMs=30, decayMs=250)
R("visual.tension", "procedural/hero/deform/4/amount", 0.25, attackMs=100, decayMs=500)
# lift: the core follows the melody's contour (the typical melodic range, MIDI ~53-83, spread over 1.6 m)
for n in ("procedural/hero/transform/position", "procedural/facets/transform/position",
          "procedural/halos/transform/position", "lights/heart/position"):
    R("visual.lift", n, 1.6, comp=1, gain=2.86, offset=-1.0, clampEnabled=True, clampMin=0.0, clampMax=1.0,
      attackMs=160, decayMs=320)

R("visual.crystalline", "procedural/facets/source/scale", 1.0, op="multiply", curveAmount=1.3, curve="power",
  **SLOW)
R("visual.radiance", "procedural/facets/material/emissive", 0.15, **MED)
# Spins are rotation routes on the clock (a twist deformer's rebuilt normals flip past 90 degrees of turn), kept
# inside the parameter's +-360 over a 31.5 s file.
R("time.seconds", "procedural/facets/transform/rotation", 9.0, comp=1, offset=-16.0)
R("time.seconds", "procedural/halos/transform/rotation", 10.0, comp=1, offset=-16.0)
R("time.seconds", "procedural/shards/transform/rotation", -10.0, comp=1, offset=-16.0)

# ---- organic
for n in ("stalks", "caps", "capstems", "petals"):
    R("visual.organic", "procedural/%s/source/scale" % n, 1.0, op="multiply", curve="power", curveAmount=1.4, **SLOW)
R("visual.sustain", "procedural/caps/source/scale", 1.0, op="multiply", gain=0.45, offset=0.75, **SLOW)
R("visual.breath", "procedural/stalks/deform/1/amount", 0.18, attackMs=400, decayMs=1200)
R("visual.breath", "procedural/petals/deform/1/amount", 0.3, attackMs=500, decayMs=1500)
R("visual.radiance", "procedural/stalks/material/emissive", 0.15, **MED)
R("visual.radiance", "procedural/caps/material/emissive", 0.25, **MED)
R("visual.swarm", "particles/spores/spawnRate", 70.0, depth="visual.organic", attackMs=300, decayMs=900)

# ---- crystalline
for n in ("prisms", "spires", "halos"):
    R("visual.crystalline", "procedural/%s/source/scale" % n, 1.0, op="multiply", curve="power", curveAmount=1.3,
      **SLOW)
R("visual.radiance", "procedural/prisms/material/emissive", 0.05, **MED)
R("visual.radiance", "procedural/spires/material/emissive", 0.08, **MED)
R("visual.shimmer", "particles/glints/spawnRate", 500.0, depth="visual.crystalline", attackMs=100, decayMs=400)

# ---- chaotic
R("visual.chaotic", "procedural/shards/source/scale", 1.0, op="multiply", curve="power", curveAmount=1.2, **SLOW)
R("visual.mass", "procedural/shards/source/scale", 1.0, op="multiply", gain=-0.5, offset=1.0, **SLOW)
R("visual.grain", "procedural/shards/deform/1/amount", 0.6, attackMs=50, decayMs=250)
palette("procedural/shards/material/emissiveColor", {"tectonic": (0.5, 0.15, 1.0), "impact": (1.0, 1.0, 0.9),
                                                       "crystalline": (0.55, 0.85, 1.0), "organic": (1.0, 0.5, 0.3)},
        **SLOW)
R("visual.tectonic", "procedural/slabs/source/scale", 1.0, op="multiply", **SLOW)
R("visual.tectonic", "procedural/slabs/transform/position", 3.6, comp=1, **SLOW)
R("visual.energy", "procedural/slabs/transform/position", 0.35, comp=1, depth="visual.tectonic", attackMs=350,
  decayMs=1200)
R("visual.grain", "procedural/slabs/material/emissive", 0.12, depth="visual.tectonic", attackMs=60, decayMs=400)
R("visual.energy", "procedural/ground/deform/2/amount", 0.25, depth="visual.tectonic", attackMs=500, decayMs=1500)
R("visual.tectonic", "particles/dust/spawnRate", 160.0, **SLOW)
R("visual.grain", "particles/sparks/spawnRate", 80.0, depth="visual.impact", attackMs=50, decayMs=300)

# ---- the same MIDI note, different gestures: the family the sound belongs to decides what a note-on looks like.
# organic: a slow swell of light; crystalline: a precise ring; tectonic: a heave of the mass (below); impact: the
# audio's transients strike (the next block).
R("notes.noteOn", "procedural/hero/material/emissive", 0.6, depth="visual.organic", attackMs=0, decayMs=1400)
R("notes.noteOn", "procedural/stalks/material/emissive", 0.5, depth="visual.organic", attackMs=0, decayMs=1600)
R("notes.noteOn", "procedural/caps/material/emissive", 0.8, depth="visual.organic", attackMs=0, decayMs=1800)
R("notes.noteOn", "particles/spores/burst", 6.0, depth="visual.organic", attackMs=0, decayMs=120)
R("notes.noteOn", "procedural/prisms/material/emissive", 0.35, depth="visual.crystalline", attackMs=0, decayMs=90)
R("notes.noteOn", "procedural/spires/material/emissive", 0.6, depth="visual.crystalline", attackMs=0, decayMs=90)
R("notes.noteOn", "procedural/halos/material/emissive", 5.0, depth="visual.crystalline", attackMs=0, decayMs=320)
R("notes.noteOn", "procedural/facets/material/emissive", 0.4, depth="visual.crystalline", attackMs=0, decayMs=150)
R("notes.noteOn", "particles/glints/burst", 25.0, depth="visual.crystalline", attackMs=0, decayMs=60)
R("notes.noteOn", "procedural/shards/material/emissive", 0.25, depth="visual.tectonic", attackMs=0, decayMs=200)
# weight: a note does not light the heavy world's hero, it swells it -- a slow heave of the mass itself
R("notes.noteOn", "procedural/hero/source/scale", 1.0, op="multiply", gain=0.07, offset=1.0, depth="visual.tectonic",
  attackMs=120, decayMs=700)

# ---- the audio's own impacts: what the transient sounded like, not that a note began.
R("sonic.transient", "procedural/shards/transform/scale", 0.35, depth="visual.impact", threshold="gate",
  thresholdLevel=0.6, attackMs=0, decayMs=380)
R("sonic.transient", "procedural/shards/material/emissive", 0.45, depth="visual.impact", attackMs=0, decayMs=60)
R("sonic.transient", "particles/sparks/burst", 90.0, depth="visual.impact", attackMs=0, decayMs=45)
R("sonic.transient", "procedural/hero/material/emissive", 0.8, depth="visual.impact", attackMs=0, decayMs=70)

# ---- musical context, in every world: MIDI says what happened (these read notes.* only)
# sustained notes: the world grows and opens, and the trails lengthen
R("visual.sustain", "procedural/stalks/source/scale", 1.0, op="multiply", comp=1, gain=0.55, offset=0.72,
  attackMs=900, decayMs=1500)
R("visual.sustain", "procedural/prisms/source/scale", 1.0, op="multiply", comp=1, gain=0.45, offset=0.78,
  attackMs=900, decayMs=1500)
R("visual.sustain", "procedural/spires/source/scale", 1.0, op="multiply", comp=1, gain=0.45, offset=0.78,
  attackMs=900, decayMs=1500)
R("visual.sustain", "particles/spores/lifetime", 1.0, op="multiply", gain=1.0, offset=0.7, **SLOW)
R("visual.sustain", "temporal/echo/strength", 0.12, attackMs=600, decayMs=1200)
# an arpeggio: patterned motion -- the motes circle the core, the stalks ripple, the rings tilt
R("visual.figure", "particles/spores/orbit", 2.6, attackMs=400, decayMs=1200)
R("visual.figure", "particles/spores/spawnRate", 50.0, depth="visual.organic", attackMs=300, decayMs=900)
R("visual.figure", "procedural/stalks/deform/1/frequency", 2.2, attackMs=400, decayMs=1000)
R("visual.figure", "procedural/stalks/deform/1/amount", 0.07, attackMs=300, decayMs=900)
R("visual.figure", "procedural/petals/deform/1/amount", 0.12, attackMs=300, decayMs=900)
R("visual.figure", "procedural/halos/transform/rotation", 28.0, comp=0, attackMs=500, decayMs=1200)
R("visual.figure", "particles/glints/spawnRate", 300.0, depth="visual.crystalline", attackMs=200, decayMs=600)
R("visual.figure", "particles/sparks/spawnRate", 60.0, depth="visual.impact", attackMs=100, decayMs=400)
# dense chords: mass and layering -- the core swells, the petals and rings open wide, every chord heaves the light
R("visual.stack", "procedural/hero/source/scale", 1.0, op="multiply", gain=0.25, offset=1.0, attackMs=300,
  decayMs=900)
R("visual.stack", "procedural/facets/source/scale", 1.0, op="multiply", gain=0.25, offset=1.0, attackMs=300,
  decayMs=900)
R("visual.stack", "procedural/petals/source/scale", 1.0, op="multiply", gain=0.55, offset=0.8, attackMs=400,
  decayMs=1000)
R("visual.stack", "procedural/halos/source/scale", 1.0, op="multiply", gain=0.4, offset=1.0, attackMs=300,
  decayMs=900)
R("visual.stack", "procedural/caps/source/scale", 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=400,
  decayMs=1000)
R("notes.noteOn", "lights/heart/intensity", 6.0, depth="visual.stack", attackMs=0, decayMs=650)
R("notes.noteOn", "particles/spores/burst", 30.0, depth="visual.stack", attackMs=0, decayMs=90)
R("notes.noteOn", "particles/glints/burst", 40.0, depth="visual.stack", attackMs=0, decayMs=70)

# Each particle system belongs to a family: gestures anywhere may spawn it, but only its own world lets it shine
# (additive particles with no emission are invisible), so a chord's burst of motes is warm in the garden and absent
# from the heavy world.
for n, fam in (("spores", "organic"), ("glints", "crystalline"), ("dust", "tectonic"), ("sparks", "impact")):
    R("visual." + fam, "particles/%s/emissive" % n, 1.0, op="multiply", **SLOW)

# ---- the heart: a point light inside the hero, so its glow reaches the ground and the forms around it
palette("lights/heart/color", {"organic": (1.0, 0.55, 0.3), "crystalline": (0.6, 0.85, 1.0),
                               "tectonic": (0.55, 0.2, 1.0), "impact": (0.9, 1.0, 0.75)}, **SLOW)
# Kept local on purpose: a creative-critic pass on the first render found the whole frame brightening with the
# onsets (98% of regions, 12% of mean luma), which reads as exposure flicker, not as the world responding.
R("visual.radiance", "lights/heart/intensity", 4.0, **MED)
R("notes.noteOn", "lights/heart/intensity", 4.0, depth="visual.organic", attackMs=0, decayMs=1400)
R("notes.noteOn", "lights/heart/intensity", 7.0, depth="visual.crystalline", attackMs=0, decayMs=240)
R("notes.noteOn", "lights/heart/intensity", 5.0, depth="visual.tectonic", attackMs=0, decayMs=300)
R("sonic.transient", "lights/heart/intensity", 20.0, depth="visual.impact", attackMs=0, decayMs=90)

# ---- the camera: weight lowers it and tips it up at the hero
R("visual.mass", "camera/position", -1.0, comp=1, **SLOW)
R("visual.mass", "camera/target", 0.35, comp=1, **SLOW)

with open(MASTER) as f:
    master = json.load(f)

world = {"kind": "interpret", "name": "world", "settings": {"mappings": WORLD_MAPPINGS, "groups": {}}}
garden = {"kind": "interpret", "name": "garden", "settings": {"mappings": GARDEN_MAPPINGS,
                                                              "groups": {"family": {"sharpness": 3.0}}}}
master["sonic"] = {"notes": "notes/phrase.mid", "character": CHARACTER}
master["sources"] = [garden, world]
master["routes"] = routes
master["parameters"] = {
    "post/bloom/enabled": True, "post/bloom/intensity": 0.45, "post/bloom/threshold": 1.0,
    "post/bloom/emissionWeight": 0.6, "post/tonemap/chroma-retention": 0.5,
    "post/output/vignette": 0.45, "post/output/grain": 0.01,
    "post/halation/enabled": True, "post/halation/intensity": 0.0,
    "temporal/echo/enabled": True, "temporal/echo/strength": 0.0, "temporal/echo/decay": 0.0,
    "temporal/echo/frames": 8.0,
    "camera/exposure/mode": 0, "camera/exposure/compensation": -0.3,
    "env/sky/enabled": True, "env/sky/background": True, "env/sky/intensity": 0.18, "lightrig/SonicGarden/keyIntensity": 2.6,
    "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/useExplicitFov": False,
    "scene/volumeJitter": 0.4, "scene/volumeSteps": 48,
    "camera/lens/focalLength": 35.0, "camera/lens/aperture": 2.2, "camera/focus/mode": 2,
}
# The camera: one composed move, authored over the 21.5 s phrase (sonic_garden_variants.py stretches it to each
# file's length): a low wide establishing view from the front left, a slow arc right and in through the arpeggio
# and the melody, closest on the stabs, and a lift and ease back on the final chord.
cam = [(0.0, (-10.5, 1.7, 15.5), (0.0, 2.3, 0.0)),
       (5.0, (-7.5, 2.1, 13.2), (0.0, 2.4, 0.0)),
       (10.0, (-2.5, 2.7, 12.9), (0.3, 2.5, 0.0)),
       (15.0, (4.6, 2.3, 11.6), (0.2, 2.6, 0.0)),
       (18.75, (8.2, 2.9, 11.4), (0.0, 2.6, 0.0)),
       (21.5, (10.0, 3.5, 12.8), (0.0, 2.7, 0.0))]
master["timeline"] = {"enabled": True, "cues": [], "tracks": [
    {"target": "camera/position", "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
     "loopLength": 0.0, "keys": [{"time": t, "value": list(p), "interp": "smooth"} for t, p, _ in cam]},
    {"target": "camera/target", "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
     "loopLength": 0.0, "keys": [{"time": t, "value": list(q), "interp": "smooth"} for t, _, q in cam]}]}
with open(MASTER, "w") as f:
    json.dump(master, f, indent=1)
    f.write("\n")
print("wrote", MASTER, len(routes), "routes")
