#!/usr/bin/env python3
"""PHONOTAXIS: the flagship LIVE scene's source of truth.

Phonotaxis is an organism's movement toward sound. Here the viewer is the organism: a continuous flight
through an endless night world that hears. There is no centrepiece and nothing the camera orbits; the
world is the spectacle, and it has no edge.

Writes phonotaxis.scene.json (the world) and the projects beside it. Edit this file, not the JSON: the JSON is
generated so the colour rule, the band split, the flight and the world's layers stay consistent.

    python3 examples/phonotaxis/build.py

Layers, near to far (every one but the floor is a GPU generator, ADR-1117: it exists only around the camera,
so 100 km of flight costs what 0 m does):
  floor      the organism's trail (an agents grid, ADR-1120, wrapping, sampled twice at different rotations so it
             does not tile visibly), on a 2 km plane that travels with the camera
  reeds      a forest that grows only where the organism walked; each reed hears its own band, delayed by its
             distance from the traveller (near is the present, far is the past)
  spires     groves of crystal blades the flight weaves through (a corridor keeps the flight line clear)
  fins       colossi 60-180 m tall on the horizon; the music climbs each fin's height (history up the height)
  drifters   lanterns floating 30-100 m up, glinting with the air band: the sky is never empty
  spores     sparks around the traveller
Kick fronts and the performer's strike spread outward from the traveller like sonar.

Design notes: docs/research/gpu-world-productionization.md, "Phase 4: PHONOTAXIS".
"""
from __future__ import annotations

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ---------------------------------------------------------------- the one colour rule: colour is frequency
EMBER = [1.00, 0.36, 0.10]   # lows  (species 0)
TEAL = [0.10, 0.85, 0.78]    # mids  (species 1)
VIOLET = [0.62, 0.32, 1.00]  # highs (species 2)
NIGHT = {"lo": EMBER, "mid": TEAL, "hi": VIOLET}
# The reborn world keeps the rule in one molten hue family: ember, gold, white.
REBIRTH = {"lo": [1.0, 0.28, 0.04], "mid": [1.0, 0.66, 0.2], "hi": [0.88, 0.9, 1.0]}

FOSSIL = [0.05, 0.03, 0.10]  # the old roads' faint violet
TRACK = "../../assets/audio/feline-footwear.wav"  # the owner's test track for this scene (gitignored asset)
TILE = 179.2          # half extent of the organism's plane (1024 cells of 0.35 m); it wraps, so it is a tile
MAX_SPEED = 50.0      # m/s at macros/speed = 1
ECHO_SPEED = 6.0      # m/s the present spreads outward from the traveller over the reeds and spires
RING_SPEED = 26.0     # m/s a kick front races outward from the traveller
COLUMN_SPEED = 12.0   # m/s the music climbs a fin


def field(name, **f):
    f.setdefault("falloff", {"kind": "none"})
    return {"name": name, "kind": "field", "position": [0, 0, 0], "field": f}


def op(kind, dst, **kw):
    o = {"kind": kind, "dst": dst}
    o.update(kw)
    return o


def species_colour_ops(src, dst, scratch):
    """dst.rgb = src.x * EMBER + src.y * TEAL + src.z * VIOLET (src a vector register)."""
    a, b = scratch
    return [
        op("swizzle", a, srcA=src, constant=[0, 0, 0, 0]),
        op("constant", b, constant=EMBER + [0]),
        op("multiply", dst, srcA=a, srcB=b),
        op("swizzle", a, srcA=src, constant=[1, 1, 1, 1]),
        op("constant", b, constant=TEAL + [0]),
        op("multiply", a, srcA=a, srcB=b),
        op("add", dst, srcA=dst, srcB=a),
        op("swizzle", a, srcA=src, constant=[2, 2, 2, 2]),
        op("constant", b, constant=VIOLET + [0]),
        op("multiply", a, srcA=a, srcB=b),
        op("add", dst, srcA=dst, srcB=a),
    ]


def band_ramp_ops(dst):
    """dst.rgb = the colour of the element's own band: its random lane w is what picks its band in an Element-band
    spectrum field (ADR-1116), so lows ember, mids teal, highs violet."""
    return [
        op("input", dst, input="instanceRandom"),
        op("swizzle", dst, srcA=dst, constant=[3, 3, 3, 3]),
        op("ramp", dst, srcA=dst, constant=EMBER + [1.0], constant2=TEAL + [1.0], constant3=VIOLET + [1.0]),
    ]


def programs():
    # The floor: the organism's trail, compressed, coloured by species, white-hot where lanes are densest, lifted
    # where a kick front or the strike passes.
    bed = [
        # the trail read twice, the second rotated 37 degrees and scaled 1.37, so the wrapping tile never repeats
        # visibly. Two Field ops, not one compound: a compound field is typed scalar on the GPU, so it would hand
        # the program the trail's length and lose the species (the colour).
        op("field", 0, field="trailA"),
        op("field", 7, field="trailB"),
        op("add", 0, srcA=0, srcB=7),
        op("remap", 0, srcA=0, value=1, constant=[0.01, 2.0, 0.0, 1.0]),
        op("power", 0, srcA=0, value=0.85),
        *species_colour_ops(0, 1, (2, 3)),
        op("gradient", 2, srcA=0, value=0.4, constant=[1, 1, 1, 0]),
        op("power", 2, srcA=2, value=4.0),
        op("constant", 3, constant=[0.25, 0.24, 0.22, 0]),
        op("multiply", 2, srcA=2, srcB=3),
        op("add", 1, srcA=1, srcB=2),
        # fossil roads: a faint polygon web of old channels under everything, so ground the organism has not
        # reached (the first seconds, a starved Collapse, the silence after the song) is dark but never a void;
        # the kick fronts light it with the rest
        op("input", 2, input="worldPosition"),
        op("voronoiEdge", 2, srcA=2, value=0.13, constant=[0, 0, 0, 0]),
        op("swizzle", 3, srcA=2, constant=[1, 1, 1, 1]),  # F1: each old cell dished, darker at its middle
        op("swizzle", 2, srcA=2, constant=[0, 0, 0, 0]),  # F2 - F1: the channel itself
        op("remap", 2, srcA=2, value=1, constant=[0.2, 0.0, 0.0, 1.0]),
        op("power", 2, srcA=2, value=3.0),
        op("remap", 3, srcA=3, value=1, constant=[0.0, 1.0, 0.08, 0.2]),
        op("add", 2, srcA=2, srcB=3),
        op("constant", 3, constant=FOSSIL + [0]),
        op("multiply", 2, srcA=2, srcB=3),
        op("add", 1, srcA=1, srcB=2),
        op("field", 4, field="kickRing"),
        op("field", 5, field="strike"),
        op("add", 4, srcA=4, srcB=5),
        op("remap", 4, srcA=4, value=0, constant=[0.0, 1.0, 1.0, 5.5]),
        op("multiply", 1, srcA=1, srcB=4),
        op("constant", 6, constant=[0.035, 0.03, 0.045, 1]),  # dark ground the lantern can still find
    ]
    # Reeds and spires: each wears its band's colour; brightness from its instance emission (its band at its
    # moment, the fronts), brighter toward the tip.
    choir = [
        *band_ramp_ops(1),
        op("input", 4, input="localPosition"),
        op("swizzle", 4, srcA=4, constant=[1, 1, 1, 1]),
        op("power", 4, srcA=4, value=2.2),
        op("remap", 4, srcA=4, value=1, constant=[0.0, 1.0, 0.12, 1.0]),  # a stalk is never a black silhouette
        op("input", 5, input="instanceEmissive"),
        op("swizzle", 5, srcA=5, constant=[0, 0, 0, 0]),
        op("multiply", 4, srcA=4, srcB=5),
        op("multiply", 1, srcA=1, srcB=4),
        op("constant", 6, constant=[0.01, 0.01, 0.012, 1]),
    ]
    # Fins: dark glass on which the music climbs -- the loudest part of the spectrum's range, heard at a delay that
    # grows with height, lights horizontal bands that rise up the fin and fade (history up the height).
    fin = [
        op("field", 0, field="columns"),
        op("remap", 0, srcA=0, value=1, constant=[0.42, 0.92, 0.0, 1.0]),
        op("power", 0, srcA=0, value=2.0),
        *band_ramp_ops(1),
        op("multiply", 1, srcA=1, srcB=0),
        op("input", 2, input="instanceEmissive"),  # the kick fronts reach it too
        op("swizzle", 2, srcA=2, constant=[0, 0, 0, 0]),
        op("multiply", 1, srcA=1, srcB=2),
        op("constant", 3, constant=[3.0, 3.0, 3.0, 0]),
        op("multiply", 1, srcA=1, srcB=3),
        # a faint cold rim, so a colossus between its bands is a shape against the haze, not a hole in it
        op("fresnel", 4, value=3.0),
        op("constant", 5, constant=[0.05, 0.045, 0.1, 0]),
        op("multiply", 4, srcA=4, srcB=5),
        op("add", 1, srcA=1, srcB=4),
        op("constant", 6, constant=[0.012, 0.012, 0.016, 1]),
    ]
    # Drifters: soft lanterns in their band's colour, glinting with the air band.
    lantern = [
        *band_ramp_ops(1),
        op("input", 5, input="instanceEmissive"),
        op("swizzle", 5, srcA=5, constant=[0, 0, 0, 0]),
        op("multiply", 1, srcA=1, srcB=5),
        op("constant", 6, constant=[0.02, 0.02, 0.025, 1]),
    ]

    def mk(n, ops):
        return {"name": n, "ops": ops, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 1,
                "emissionIntensity": 1.0, "opacity": -1}
    return [mk("bed", bed), mk("canopy", bed), mk("choir", choir), mk("fin", fin), mk("lantern", lantern)]


def fields():
    return [  # 16: the GPU field table's limit (kMaxGpuFields)
        # what the organism eats: each species its band (fieldElement = (species + 0.5) / 3)
        field("bands", kind="spectrum", audioBand="element", bandLow=0.0, bandHigh=1.0, strength=1.0),
        field("hunger", kind="compound", children=["bands", "kickRing", "strike", "appetite"], combine="add",
              strength=1.0),
        # a little hunger with no sound at all, so silence thins the organism's roads but never erases them
        field("appetite", kind="constant", strength=0.25),
        field("wander", kind="curlNoise", frequency=0.02, speed=0.06, strength=1.0),
        # the organism's memory, read twice at different rotations and scales so the wrapping tile never repeats
        # visibly, and once more for the reeds' growth
        field("trailA", kind="grid", reference="organism", strength=1.0),
        {**field("trailB", kind="grid", reference="organism", strength=1.0),
         "rotation": [0, 37, 0], "scale": [1.37, 1.0, 1.37]},
        field("trailN", kind="grid", reference="organism", strength=0.8),
        field("trailCap", kind="constant", strength=1.0),
        field("growth", kind="compound", children=["trailN", "trailCap"], combine="min", strength=1.0),
        # around the traveller (these fields ride with the camera): near is the present, far is the past
        field("echo", kind="spectrum", audioBand="element", bandLow=0.0, bandHigh=1.0, audioSpeed=ECHO_SPEED,
              waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
        field("kickRing", kind="onset", onsetSource="low", onsetDecay=0.8, onsetWidth=7.0, audioSpeed=RING_SPEED,
              waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
        # the performer's strike (MIDI pad 36): one shock front from the traveller to the horizon
        field("strike", kind="wave", waveGeometry="radial", waveShape="pulse", axis=[0, 1, 0], amplitude=1.0,
              wavelength=4000.0, waveSpeed=40.0, waveWidth=10.0, waveOrigin=0.0, strength=3.0,
              trigger={"source": "signal", "name": "control.strike", "threshold": 0.3}),
        field("air", kind="spectrum", audioBand="range", bandLow=0.55, bandHigh=1.0, audioSpeed=10.0,
              waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
        # history up the height of every fin
        field("columns", kind="spectrum", audioBand="range", bandLow=0.0, bandHigh=1.0, audioSpeed=COLUMN_SPEED,
              waveGeometry="planar", axis=[0, 1, 0], strength=1.0),
        # the flight line is kept clear: spires inside 7 m and fins inside 80 m of the traveller's line shrink away
        field("corridor", kind="box", size=[7.0, 400.0, 900.0], softness=5.0, invert=True, strength=1.0),
        field("corridorWide", kind="box", size=[80.0, 600.0, 1600.0], softness=30.0, invert=True, strength=1.0),
    ]


def organism():
    return {
        "name": "organism", "enabled": True, "mode": "agents", "wrap": "wrap",
        "resolution": [1024, 1, 1024], "boundsMin": [-TILE, -1, -TILE], "boundsMax": [TILE, 1, TILE],
        "velocityField": "wander", "advect": 0.15, "diffusion": 0.12, "dissipation": 3.5,
        "simRate": 30.0, "maxSubSteps": 4, "seed": 2026, "agentCount": 420000, "species": 3,
        "sensorAngle": 0.5, "sensorDistance": 5.0, "turnAngle": 0.5, "stepSize": 2.0,
        "depositAmount": 0.042, "repel": 0.7, "depositField": "hunger", "checkpointInterval": 10.0,
    }


def bed():
    """The floor: a 2 km plane that travels with the camera (nodes/bed/position), so it never ends."""
    return {"name": "bed", "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [1000.0, 0.02, 1000.0], "subdivisions": 1},
        "sourceTransform": {"position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [2, 1, 2]},
        "distribution": {"kind": "single"},
        "lod": {"cull": False, "count": 1},
        "material": {"baseColor": [0.004, 0.004, 0.006], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.85, "metallic": 0.0, "program": "bed"}}}


CANOPY_Y = 85.0
ROOF_GAIN = 1.4  # the roof is seen through more haze than the floor  # the roof: the organism's trail again, overhead, so the sky is never a void


def canopy():
    """The roof: a second 2 km sheet of the same living trail, 85 m up, travelling with the camera. The flight is
    between two living strata; looking up is never looking into nothing."""
    return {"name": "canopy", "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [1000.0, 0.02, 1000.0], "subdivisions": 1},
        "sourceTransform": {"position": [0, CANOPY_Y, 0], "rotation": [0, 0, 0], "scale": [2, 1, 2]},
        "distribution": {"kind": "single"},
        "lod": {"cull": False, "count": 1},
        "material": {"baseColor": [0.004, 0.004, 0.006], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.85, "metallic": 0.0, "program": "canopy"}}}


def gen(cell, view, **kw):
    g = {"cellSize": cell, "viewDistance": view, "presence": 1.0, "jitter": 1.0, "sizeMin": 1.0, "sizeMax": 1.0,
         "tilt": 0.0, "bounded": False, "regionMin": [-50, -50], "regionMax": [50, 50], "regionRadius": 0.0,
         "groundHeight": 0.0, "groundAmplitude": 0.0, "groundFrequency": 0.01, "groundSeed": 7}
    g.update(kw)
    return {"kind": "generator", "generator": g}


def reeds():
    return {"name": "choir", "kind": "procedural", "procedural": {
        "source": {"kind": "cylinder", "radius": 0.05, "height": 1.0, "radialSegments": 3, "caps": False},
        "sourceTransform": {"position": [0, 0.5, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
        "variation": {"seed": 11},
        "distribution": gen(0.55, 60.0, sizeMin=0.7, sizeMax=1.6, tilt=0.1),
        "effectors": [
            {"field": "growth", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            {"field": "echo", "op": "scale", "blend": "add", "strength": 2.2, "scaleAxis": [0, 1, 0]},
            {"field": "echo", "op": "emission", "blend": "add", "strength": 5.0},
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 8.0},
            {"field": "strike", "op": "emission", "blend": "add", "strength": 8.0},
            {"field": "kickRing", "op": "positionOffset", "blend": "add", "strength": 0.6, "axis": [0, 1, 0]},
        ],
        # a reed too small to see is culled, not drawn as a speck (faint trails left a lattice of glowing dots)
        "lod": {"cull": True, "maxDistance": 64.0, "minScreenRadius": 1.5, "count": 1},
        "material": {"baseColor": [0.01, 0.01, 0.012], "emissiveColor": [1, 1, 1], "emissiveIntensity": 0.6,
                     "roughness": 0.4, "metallic": 0.0, "program": "choir"}}}


def curve(points, samples=6):
    return {"kind": "catmullRom", "generator": "points", "samplesPerSegment": samples,
            "points": [{"position": list(p), "scale": 1.0, "roll": 0.0} for p in points]}


def spires():
    """Sea whips: tall, tapered, curving stalks, all leaning in one current (a unit-tall curve, scaled ~14 m)."""
    return {"name": "spires", "kind": "procedural", "procedural": {
        "source": {"kind": "tube", "tubeRadius": 0.024, "tubeTaper": 0.9, "tubeSides": 6, "tubeSegments": 16,
                   "tubeTwist": 0.0, "tubeCaps": False,
                   "curve": curve([(0, 0, 0), (0.015, 0.35, 0), (0.07, 0.68, 0), (0.17, 0.92, 0), (0.24, 1.0, 0)])},
        "sourceTransform": {"position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [14, 14, 14]},
        "variation": {"seed": 77},
        "distribution": gen(6.5, 280.0, presence=0.36, clusterSize=7.0, clusterContrast=0.85, sizeMin=0.45,
                            sizeMax=2.0, tilt=0.12, groundSeed=3),
        "effectors": [
            {"field": "corridor", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            {"field": "echo", "op": "emission", "blend": "add", "strength": 4.0},
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 10.0},
            {"field": "strike", "op": "emission", "blend": "add", "strength": 14.0},
        ],
        "lod": {"cull": True, "maxDistance": 280.0, "count": 1},
        "material": {"baseColor": [0.012, 0.012, 0.016], "emissiveColor": [1, 1, 1], "emissiveIntensity": 0.5,
                     "roughness": 0.3, "metallic": 0.4, "program": "choir"}}}


def fins():
    """Colossi: horns 80-230 m tall that rise and bend over the far world; the music climbs them."""
    return {"name": "fins", "kind": "procedural", "procedural": {
        "source": {"kind": "tube", "tubeRadius": 0.06, "tubeTaper": 0.94, "tubeSides": 14, "tubeSegments": 48,
                   "tubeTwist": 0.0, "tubeCaps": False,
                   "curve": curve([(0, 0, 0), (0.05, 0.3, 0), (0.18, 0.62, 0), (0.42, 0.88, 0), (0.72, 0.98, 0),
                                   (0.95, 0.9, 0)], samples=8)},
        "sourceTransform": {"position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [100, 100, 100]},
        "variation": {"seed": 91},
        "distribution": gen(95.0, 1700.0, presence=0.4, clusterSize=4.0, clusterContrast=0.7, sizeMin=0.8,
                            sizeMax=2.3, tilt=0.18, groundSeed=5),
        "effectors": [
            {"field": "corridorWide", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 3.0},
            {"field": "strike", "op": "emission", "blend": "add", "strength": 6.0},
        ],
        "lod": {"cull": True, "maxDistance": 1700.0, "count": 1},
        "material": {"baseColor": [0.012, 0.012, 0.016], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.18, "metallic": 0.85, "program": "fin"}}}


def drifters():
    return {"name": "drifters", "kind": "procedural", "procedural": {
        "source": {"kind": "sphere", "segments": 10, "rings": 7},
        "sourceTransform": {"position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [0.9, 0.9, 0.9]},
        "variation": {"seed": 123},
        "distribution": gen(24.0, 560.0, presence=0.3, clusterSize=5.0, clusterContrast=0.6, sizeMin=0.35,
                            sizeMax=2.4, groundHeight=36.0, groundAmplitude=18.0, groundFrequency=0.004,
                            groundSeed=11),
        "effectors": [
            {"field": "air", "op": "emission", "blend": "add", "strength": 7.0},
            {"field": "wander", "op": "positionOffset", "blend": "add", "strength": 4.0},
        ],
        "lod": {"cull": True, "maxDistance": 560.0, "count": 1},
        "material": {"baseColor": [0.02, 0.02, 0.025], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.5, "metallic": 0.0, "program": "lantern"}}}


def spores():
    return {"name": "spores", "kind": "particles", "particles": {
        "capacity": 65536, "seed": 9, "shape": "disc", "position": [0, 0.3, 30], "extent": [45, 0, 70],
        "direction": [0, 1, 0], "spread": 0.4, "spawnRate": 300,
        "speedMin": 0.2, "speedMax": 0.8, "gravity": [0, 0.15, 0], "drag": 0.12,
        "turbulence": 0.5, "turbulenceScale": 0.06, "turbulenceSpeed": 0.25,
        "velocityStretch": 0.6, "stretchMin": 1.0, "stretchMax": 6.0,
        "sizeStart": 0.06, "sizeEnd": 0.015, "sizeVariance": 0.7, "lifetimeMin": 6.0, "lifetimeMax": 10.0,
        "colorStart": [1.0, 0.62, 0.3, 1.0], "colorEnd": [0.9, 0.3, 0.5, 0.0], "emissive": 9.0,
        "blend": "additive", "softness": 0.6}}


def scene():
    return {
        "format": "avgen-scene", "version": 1, "name": "PHONOTAXIS",
        "_note": "The flagship LIVE scene: a flight through an endless world that hears. Generated by build.py.",
        "camera": {"mode": 1, "position": [0, 9, 0], "target": [0, 5, 70], "fov": 56, "orbitSpeed": 0.0},
        "environment": {
            "background": [0.0015, 0.0016, 0.004], "intensity": 0.05,
            "fogColor": [0.02, 0.014, 0.045], "horizonDensity": 0.8,
            # a thin medium the traveller's light scatters into, and that swallows the far world gradually
            "volumeDensity": 0.006, "volumeScattering": 1.0, "volumeAbsorption": 0.6, "volumeAnisotropy": 0.35,
            "volumeLocalLights": 0.45, "volumeSteps": 16, "volumeJitter": 0.5, "volumeMaxDistance": 900.0,
            "volumeNoise": 0.0,
            "sky": {"enabled": True, "background": True, "zenithColor": [0.0009, 0.0010, 0.003],
                    "horizonColor": [0.03, 0.014, 0.04], "groundColor": [0.001, 0.001, 0.002], "haze": 0.0,
                    "sunIntensity": 0.0, "intensity": 1.0, "useKeyLight": False}},
        "lights": [
            # the traveller's own light, riding 25 m ahead (lights/lantern/position follows the flight)
            {"name": "lantern", "id": "lantern", "type": "point", "position": [0, 5.0, 25.0],
             "color": [1.0, 0.42, 0.18], "intensity": 250, "range": 45, "radius": 1.0, "castsShadow": False,
             "volumetric": 0.12},
            {"name": "moon", "id": "moon", "type": "directional", "direction": [-0.3, -0.8, 0.5],
             "color": [0.55, 0.65, 1.0], "intensity": 0.3, "castsShadow": False, "volumetric": 0.0},
        ],
        "effects": [
            {"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True,
             "parameters": {"brightness": 0.9, "density": 0.45, "magnitudeSlope": 6, "colorSpread": 0.4,
                            "twinkle": 0.2, "twinkleRate": 1.0, "horizonFade": 0.5, "band": 0.35, "bandTilt": 20,
                            "daylight": 0}}],
        "grids": [organism()],
        "materialPrograms": programs(),
        "nodes": [*fields(), bed(), canopy(), reeds(), spires(), fins(), drifters(), spores()],
    }


# ===================================================================================== the performance
ZERO_DEFAULTS = [
    {"source": "audio.bass", "target": "root/scale", "op": "add", "amount": 0.0},
    {"source": "audio.mid", "target": "root/rotationSpeed", "op": "add", "amount": 0.0},
    {"source": "audio.rms", "target": "scene/brightness", "op": "add", "amount": 0.0},
    {"source": "audio.onset", "target": "root/impulse", "op": "add", "amount": 0.0},
]

POST = {
    "post/bloom/enabled": True, "post/bloom/threshold": 0.9,
    "post/bloom/emissionWeight": 0.8, "post/bloom/radius": 0.8,
    "post/tonemap/chroma-retention": 0.65, "post/output/vignette": 0.45, "post/output/grain": 0.015,
    "post/halation/enabled": True, "post/halation/intensity": 0.18, "post/halation/warmth": 0.5,
    "post/grade/contrast": 1.08, "post/grade/saturation": 1.05,
    "camera/exposure/mode": 0,
    "temporal/echo/enabled": True, "temporal/echo/frames": 8.0,
    "camera/mode": 1,
    # the flight's drifts: weave (two rates), the heading leads the weave, the bank follows it, a slow crane
    "sources/weaveA/rate": 0.031, "sources/leadA/rate": 0.031, "sources/leadA/phase": 0.07,
    "sources/weaveB/rate": 0.0193, "sources/leadB/rate": 0.0193, "sources/leadB/phase": 0.07,
    "sources/bank/rate": 0.031, "sources/bank/phase": 0.25,
    "sources/crane/rate": 0.0123,
}
# Owned by the states (presets), never set here: camera/position, camera/target, camera/fov, exposure
# compensation, temporal/echo/strength, post/bloom/intensity, macros/speed.

LISTEN = {"kind": "interpret", "name": "listen", "settings": {"mappings": [
    {"name": "drive", "combine": "mean", "inputs": [
        {"signal": "audio.rms", "weight": 1.0}, {"signal": "audio.spectralFlux", "weight": 1.0},
        {"signal": "audio.treble", "weight": 1.0}], "bias": -0.1, "gain": 2.6, "curve": 1.0}]}}

LFOS = [{"kind": "lfo", "name": n, "settings": {"shape": "sine"}}
        for n in ("weaveA", "leadA", "weaveB", "leadB", "bank", "crane")]

# The performer's knobs, each neutral at its default.  (name, label, cc, default, [(path, min, max, op)])
KNOBS = [
    ("energy", "ENERGY", 1, 0.0, []),
    ("hunger", "HUNGER", 21, 0.5, [("grid/organism/depositAmount", -0.034, 0.084, "add")]),
    ("restless", "RESTLESS", 22, 0.5, [("grid/organism/turnAngle", -0.3, 0.6, "add"),
                                        ("grid/organism/sensorAngle", -0.2, 0.4, "add")]),
    ("current", "CURRENT", 23, 0.5, [("field/wander/strength", -0.8, 1.6, "add")]),
    ("memory", "MEMORY", 24, 0.0, [("temporal/echo/strength", 0.0, 0.6, "add"),
                                   ("temporal/echo/decay", 0.0, 0.3, "add")]),
    ("altitude", "ALTITUDE", 25, 0.5, []),   # routes below: the flight's height, +-30 m
    ("throttle", "SPEED", 26, 0.5, [("macros/speed", -0.3, 0.4, "add")]),
    ("glow", "GLOW", 27, 0.5, [("post/bloom/intensity", -0.3, 0.45, "add"),
                               ("post/halation/intensity", -0.15, 0.3, "add")]),
    ("sensitivity", "SENSITIVITY", 28, 0.25, []),  # the depth of the music's push on the energy (0..4; 0.25 = 1)
    ("speed", "speed (set by the states)", -1, 0.3, []),  # the flight's speed, x MAX_SPEED m/s
]
ENERGY_TARGETS = [
    ("macros/speed", 0.0, 0.12, "add"),
    ("material/choir/emissionIntensity", 0.0, 0.4, "add"),
    ("material/fin/emissionIntensity", 0.0, 0.5, "add"),
    ("particles/spores/spawnRate", 0.0, 400.0, "add"),
    ("post/bloom/intensity", 0.0, 0.12, "add"),
]
PADS = {"strike": 36, "scatter": 37, "Dormant": 40, "Germination": 41, "Chorus": 43, "Surge": 45,
        "Eruption": 47, "Collapse": 48, "Rebirth": 50}

# ------------------------------------------------------------------------------------------ states
# Stages of the journey: each sets the world's configuration and the flight (speed, altitude, where it looks).
COLS = ["growth", "lift", "reedGain", "finGain", "bedGain", "deposit", "fade", "turn", "gaze", "reach", "wander",
        "spores", "kick", "speed", "alt", "lookY", "ahead", "fov", "exposure", "echo", "bloom", "lantern", "air"]
STATES = {
    #              grow lift reed  fin  bed   dep    fade turn gaze rch  wand spor kick spd   alt  lkY ahd fov  exp   echo bloom lant air
    "Dormant":     (0.15, 0.4, 0.35, 0.3, 0.55, 0.022, 1.8, 0.35, 0.5, 5.0, 1.0, 40, 0.4, 0.06, 4.5, 3.0, 60, 58, -0.45, 0.0, 0.40, 120, 0.5),
    "Germination": (0.45, 1.2, 0.6, 0.45, 0.75, 0.034, 2.5, 0.45, 0.5, 5.0, 1.0, 120, 0.7, 0.16, 2.6, 2.0, 50, 62, -0.25, 0.0, 0.45, 200, 0.7),
    "Chorus":      (0.85, 2.2, 1.0, 0.85, 1.0, 0.042, 3.5, 0.5, 0.5, 5.0, 1.0, 280, 1.0, 0.36, 9.0, 5.0, 70, 56, 0.0, 0.0, 0.50, 250, 1.0),
    "Surge":       (1.0, 3.0, 1.25, 1.3, 1.0, 0.056, 3.5, 0.6, 0.55, 5.0, 1.0, 420, 1.3, 0.6, 60.0, 46.0, 150, 50, 0.0, 0.0, 0.55, 250, 1.2),
    "Eruption":    (1.25, 3.6, 1.6, 1.6, 1.2, 0.084, 3.0, 0.9, 0.7, 4.0, 1.5, 1300, 1.5, 0.9, 3.2, 2.5, 45, 74, 0.15, 0.25, 0.65, 320, 1.6),
    "Collapse":    (0.0, 0.5, 0.3, 0.5, 0.8, 0.008, 2.6, 0.5, 0.5, 5.0, 2.5, 45, 0.5, 0.05, 55.0, 0.0, 25, 50, -0.2, 0.0, 0.45, 150, 0.6),
    "Rebirth":     (0.85, 2.2, 1.0, 1.0, 1.0, 0.042, 3.5, 0.25, 0.25, 14.0, 1.0, 280, 1.0, 0.28, 5.5, 4.5, 90, 40, 0.0, 0.0, 0.50, 250, 1.0),
}
PALETTE_BY_STATE = {"Dormant": NIGHT, "Rebirth": REBIRTH}


def colour_paths(prog_ops, prog_name, colour):
    out = {}
    for i, o in enumerate(prog_ops, start=1):
        if o["kind"] == "ramp":
            out[f"material/{prog_name}/op/{i}/ramp/constant"] = colour["lo"] + [1.0]
            out[f"material/{prog_name}/op/{i}/ramp/constant2"] = colour["mid"] + [1.0]
            out[f"material/{prog_name}/op/{i}/ramp/constant3"] = colour["hi"] + [1.0]
        if o["kind"] == "constant" and o["constant"][:3] in (EMBER, TEAL, VIOLET):
            key = {tuple(EMBER): "lo", tuple(TEAL): "mid", tuple(VIOLET): "hi"}[tuple(o["constant"][:3])]
            out[f"material/{prog_name}/op/{i}/constant/constant"] = colour[key] + [0.0]
    return out


def presets():
    progs = {p["name"]: p["ops"] for p in programs()}
    out = []
    for name, row in STATES.items():
        v = dict(zip(COLS, row))
        values = {
            "procedural/choir/effector/1/strength": [v["growth"]],
            "procedural/choir/effector/2/strength": [v["lift"]],
            "material/choir/emissionIntensity": [v["reedGain"]],
            "material/fin/emissionIntensity": [v["finGain"]],
            "material/bed/emissionIntensity": [v["bedGain"]],
            "material/canopy/emissionIntensity": [v["bedGain"] * ROOF_GAIN],
            "material/lantern/emissionIntensity": [v["air"]],
            "grid/organism/depositAmount": [v["deposit"]],
            "grid/organism/dissipation": [v["fade"]],
            "grid/organism/turnAngle": [v["turn"]],
            "grid/organism/sensorAngle": [v["gaze"]],
            "grid/organism/sensorDistance": [v["reach"]],
            "field/wander/strength": [v["wander"]],
            "particles/spores/spawnRate": [v["spores"]],
            "field/kickRing/strength": [v["kick"]],
            "macros/speed": [v["speed"]],
            "camera/position": [0.0, v["alt"], 0.0],
            "camera/target": [0.0, v["lookY"], v["ahead"]],
            "camera/fov": [v["fov"]],
            "camera/exposure/compensation": [v["exposure"]],
            "temporal/echo/strength": [v["echo"]],
            "post/bloom/intensity": [v["bloom"]],
            "lights/lantern/intensity": [v["lantern"]],
            "lights/lantern/position": [0.0, max(v["alt"] - 2.0, 2.5), 25.0],
        }
        if name in PALETTE_BY_STATE:
            pal = PALETTE_BY_STATE[name]
            for prog in ("bed", "canopy", "choir", "fin", "lantern"):
                for path, c in colour_paths(progs[prog], prog, pal).items():
                    values[path] = c
        out.append({"name": name.lower(), "values": values})
    return out


def trig_energy(threshold, frm=None, falling=False):
    t = {"kind": "macro", "signal": "energy", "threshold": threshold}
    if frm:
        t["from"] = frm
    if falling:
        t["falling"] = True
    return t


def pad(name):
    return {"kind": "signal", "signal": f"control.pad{name}", "threshold": 0.3}


# Energy thresholds. Tuned on the scene's test track (Feline Footwear) through production's analysis and the
# route chain (--sonic-trace); see the doc.
T = {"wake": 0.3, "chorus": 0.53, "surge": 0.70, "surgeOut": 0.6, "erupt": 0.845, "eruptOut": 0.79,
     "collapseChorus": 0.38, "collapseSurge": 0.5, "collapseErupt": 0.55, "collapseRebirth": 0.38, "rebirth": 0.5,
     "sleep": 0.08}


def states():
    up = ["Germination", "Chorus", "Surge", "Eruption", "Rebirth"]
    return {"initial": "Dormant", "states": [
        {"name": "Dormant", "preset": "dormant", "transition": {"seconds": 10, "easing": "smooth"},
         "triggers": [pad("Dormant")] + [trig_energy(T["sleep"], frm=f, falling=True) for f in up + ["Collapse"]]},
        {"name": "Germination", "preset": "germination", "transition": {"seconds": 8, "easing": "smooth"},
         "triggers": [pad("Germination"), trig_energy(T["wake"], frm="Dormant")]},
        {"name": "Chorus", "preset": "chorus", "transition": {"seconds": 6, "easing": "smooth"},
         "triggers": [pad("Chorus"), trig_energy(T["chorus"], frm="Germination"),
                      trig_energy(T["surgeOut"], frm="Surge", falling=True)]},
        {"name": "Surge", "preset": "surge", "transition": {"seconds": 5, "easing": "easeInOut"},
         "triggers": [pad("Surge"), trig_energy(T["surge"], frm="Chorus"), trig_energy(T["surge"], frm="Rebirth"),
                      trig_energy(T["eruptOut"], frm="Eruption", falling=True)]},
        {"name": "Eruption", "preset": "eruption", "transition": {"seconds": 2.5, "easing": "easeIn"},
         "triggers": [pad("Eruption"), trig_energy(T["erupt"], frm="Surge")]},
        {"name": "Collapse", "preset": "collapse", "transition": {"seconds": 5, "easing": "easeOut"},
         "triggers": [pad("Collapse")] + [trig_energy(T[k], frm=f, falling=True)
                                          for f, k in (("Chorus", "collapseChorus"), ("Surge", "collapseSurge"),
                                                       ("Eruption", "collapseErupt"), ("Rebirth", "collapseRebirth"))]},
        {"name": "Rebirth", "preset": "rebirth", "transition": {"seconds": 10, "easing": "smooth"},
         "triggers": [pad("Rebirth"), trig_energy(T["rebirth"], frm="Collapse")]},
    ]}


def macros():
    out = []
    for name, label, _cc, default, targets in KNOBS:
        ts = [{"path": p, "min": lo, "max": hi, "op": op} for p, lo, hi, op in targets]
        if name == "energy":
            ts = [{"path": p, "min": lo, "max": hi, "op": op} for p, lo, hi, op in ENERGY_TARGETS]
        out.append({"name": name, "label": label, "default": default, "targets": ts})
    return out


def midi():
    bindings = []
    for name, _label, cc, _d, _t in KNOBS:
        if cc < 0:
            continue
        bindings.append({"source": "*", "channel": -1, "kind": "cc", "number": cc, "parameter": f"macros/{name}",
                         "component": 0, "min": 0.0, "max": 1.0})
    for name, note in PADS.items():
        sig = name if name in ("strike", "scatter") else f"pad{name}"
        bindings.append({"source": "*", "channel": -1, "kind": "noteEvent", "number": note, "signal": sig})
    return {"enabled": True, "filter": "*", "bindings": bindings}


# Everything that travels with the camera, and the component that moves.
FOLLOWERS = ["camera/position", "camera/target", "nodes/bed/position", "nodes/canopy/position", "field/echo/position",
             "field/kickRing/position", "field/strike/position", "field/air/position", "field/corridor/position",
             "field/corridorWide/position", "particles/spores/position", "lights/lantern/position"]
WEAVERS = ["camera/position", "field/corridor/position", "particles/spores/position", "lights/lantern/position"]


def routes():
    r = list(ZERO_DEFAULTS)
    # The flight: speed (m/s) integrated into distance, for the camera and everything that rides with it.
    for path in FOLLOWERS:
        r.append({"source": "macro.speed", "target": path, "op": "add", "amount": MAX_SPEED, "component": 2,
                  "chain": {"integrate": True}})
    # Weave: two incommensurate slow sways; the heading leads them, the bank follows them, the crane drifts.
    for path in WEAVERS:
        r.append({"source": "lfo.weaveA.bipolar", "target": path, "op": "add", "amount": 9.0, "component": 0})
        r.append({"source": "lfo.weaveB.bipolar", "target": path, "op": "add", "amount": 5.0, "component": 0})
    r.append({"source": "lfo.leadA.bipolar", "target": "camera/target", "op": "add", "amount": 9.0, "component": 0})
    r.append({"source": "lfo.leadB.bipolar", "target": "camera/target", "op": "add", "amount": 5.0, "component": 0})
    r.append({"source": "lfo.bank.bipolar", "target": "camera/roll", "op": "add", "amount": -6.0})
    for path in ("camera/position", "camera/target"):
        r.append({"source": "lfo.crane.bipolar", "target": path, "op": "add", "amount": 2.0, "component": 1})
        # the ALTITUDE knob, neutral at 0.5
        r.append({"source": "macro.altitude", "target": path, "op": "add", "amount": 30.0, "component": 1,
                  "polarity": "bipolar", "chain": {"attackMs": 400, "decayMs": 400}})
    # The arc: what the music does to the world's energy, slowly, scaled by SENSITIVITY.
    r.append({"source": "visual.drive", "target": "macros/energy", "op": "add", "amount": 1.0,
              "depthSource": "macro.sensitivity", "depthMin": 0.0, "depthMax": 4.0,
              "chain": {"attackMs": 2500, "decayMs": 4000}})
    # LOW is mass and fronts (fields). A heavy kick nudges the camera when the energy is high.
    r.append({"source": "audio.onsetLow", "target": "camera/shake/amplitude", "op": "add", "amount": 0.05,
              "depthSource": "macro.energy", "depthMin": 0.0, "depthMax": 1.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 30, "envelopeFallPerSecond": 6.0}})
    # MID is movement: the organism grows restless and wanders.
    r.append({"source": "audio.mid", "target": "grid/organism/turnAngle", "op": "add", "amount": 0.22,
              "chain": {"attackMs": 600, "decayMs": 2500}})
    r.append({"source": "audio.mid", "target": "field/wander/strength", "op": "add", "amount": 0.6,
              "chain": {"attackMs": 800, "decayMs": 3000}})
    # HIGH is emission: spores rise and the reeds' tips glint.
    r.append({"source": "audio.treble", "target": "particles/spores/spawnRate", "op": "add", "amount": 700.0,
              "chain": {"attackMs": 80, "decayMs": 700}})
    r.append({"source": "audio.onsetHigh", "target": "material/choir/emissionIntensity", "op": "add",
              "amount": 0.35, "chain": {"envelope": "peakhold", "envelopeHoldMs": 20, "envelopeFallPerSecond": 5.0}})
    # The performer's events: a strike is a front, a lunge forward, a burst of spores and a jolt.
    r.append({"source": "control.strike", "target": "macros/speed", "op": "add", "amount": 0.35,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 300, "envelopeFallPerSecond": 0.5}})
    r.append({"source": "control.strike", "target": "particles/spores/burst", "op": "add", "amount": 2500.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 120, "envelopeFallPerSecond": 8.0}})
    r.append({"source": "control.strike", "target": "camera/shake/amplitude", "op": "add", "amount": 0.12,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 40, "envelopeFallPerSecond": 3.0}})
    r.append({"source": "control.scatter", "target": "particles/spores/burst", "op": "add", "amount": 1800.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 200, "envelopeFallPerSecond": 5.0}})
    return r


def project(name, audio, live=False, sensitivity=None):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "phonotaxis.scene.json"}},
        "live": {"qualityStrategy": "effects_first", "targetFps": 60},
        "parameters": dict(POST),
        "sources": [LISTEN, *LFOS],
        "routes": routes(),
        "worldMacros": macros(),
        "presets": presets(),
        "states": states(),
        "control": {"midi": midi()},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "sequence", "path": "renders/phonotaxis"},
    }
    if audio:
        p["assets"]["audio"] = {"path": audio}
    if sensitivity is not None:
        # the energy arc reads absolute loudness and air; dark or quiet material is trimmed up here, as a
        # performer would with the SENSITIVITY knob
        p["parameters"]["macros/sensitivity"] = sensitivity
    if live:
        p["sonic"] = {"live": True}
    return p


def main():
    (HERE / "phonotaxis.scene.json").write_text(json.dumps(scene(), indent=1) + "\n")
    projects = {
        # development, review renders, captures, the Critic: the owner's test track for this scene
        "phonotaxis.json": project("PHONOTAXIS", TRACK),
        # a second, darker track (graceful with other music)
        "phonotaxis-night-shift.json": project("PHONOTAXIS / Night Shift", "../../assets/audio/night-shift.wav",
                                               sensitivity=0.8),
        # the instrument: live input (pick the device in the Live panel) and MIDI
        "phonotaxis-live.json": project("PHONOTAXIS LIVE", None, live=True),
    }
    for name, p in projects.items():
        (HERE / name).write_text(json.dumps(p, indent=1) + "\n")
    print("wrote", HERE / "phonotaxis.scene.json", *projects)


if __name__ == "__main__":
    main()
