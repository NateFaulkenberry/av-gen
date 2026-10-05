#!/usr/bin/env python3
"""PHONOTAXIS: the flagship LIVE scene's source of truth.

Writes phonotaxis.scene.json (the world) and the projects beside it. Edit this file, not the JSON:
the JSON is generated so the three species' colours, the band split and the world's dimensions stay
consistent across the floor, the forest, the Cochlea and the spores.

    python3 examples/phonotaxis/build.py

Design notes: docs/research/gpu-world-productionization.md, "Phase 4: PHONOTAXIS".
"""
from __future__ import annotations

import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ---------------------------------------------------------------- the one colour rule: colour is frequency
EMBER = [1.00, 0.36, 0.10]   # lows  (species 0, helix strand 0)
TEAL = [0.10, 0.85, 0.78]    # mids  (species 1, helix strand 1)
VIOLET = [0.62, 0.32, 1.00]  # highs (species 2, helix strand 2)

BASIN = 90.0          # half extent of the organism's plane (m)
FOREST_R = 80.0       # radius of the forest disc
HELIX_H = 36.0        # height of the Cochlea
HELIX_SPEED = 3.2     # m/s the present rises up the Cochlea (36 m = 11.3 s of memory)
ECHO_SPEED = 5.5      # m/s the present spreads over the forest (58 m = 10.5 s)
RING_SPEED = 22.0     # m/s a kick front races outward (to the horizon spires in ~10-20 s)


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


def programs():
    # The Bed: the organism's trail, compressed, coloured by species, lifted where a kick front passes.
    bed = [
        op("field", 0, field="trail"),
        # thin the veins: trail / T, clamped, then a convex curve, so only a lane's core is bright
        op("remap", 0, srcA=0, value=1, constant=[0.15, 3.6, 0.0, 1.0]),
        op("power", 0, srcA=0, value=1.4),
        *species_colour_ops(0, 1, (2, 3)),
        # white-hot where the lanes are densest: saturate((x + y + z) / 2)^3, so the hierarchy is in value, not hue
        op("gradient", 2, srcA=0, value=0.5, constant=[1, 1, 1, 0]),
        op("power", 2, srcA=2, value=3.0),
        op("constant", 3, constant=[0.35, 0.33, 0.30, 0]),
        op("multiply", 2, srcA=2, srcB=3),
        op("add", 1, srcA=1, srcB=2),
        # kick fronts lift what they cross
        op("field", 4, field="kickRing"),
        op("field", 5, field="strike"),  # the performer's strike: the biggest front there is
        op("add", 4, srcA=4, srcB=5),
        op("remap", 4, srcA=4, value=0, constant=[0.0, 1.0, 1.0, 5.5]),  # 1 + 4.5 x fronts
        op("multiply", 1, srcA=1, srcB=4),
        op("constant", 6, constant=[0.004, 0.004, 0.006, 1]),
    ]
    choir = [
        # each reed hears its own band (the echo field's element band is the record's random lane w), so it wears
        # that band's colour: lows ember, mids teal, highs violet -- colour is frequency, with no field sampled per
        # fragment (sampling the organism's trail here cost ~2 ms at Medium)
        op("input", 0, input="instanceRandom"),
        op("swizzle", 0, srcA=0, constant=[3, 3, 3, 3]),
        op("ramp", 1, srcA=0, constant=EMBER + [1.0], constant2=TEAL + [1.0], constant3=VIOLET + [1.0]),
        # tip gradient: local y of the unit source (0 at the root, 1 at the tip)
        op("input", 4, input="localPosition"),
        op("swizzle", 4, srcA=4, constant=[1, 1, 1, 1]),
        op("power", 4, srcA=4, value=2.2),
        op("input", 5, input="instanceEmissive"),
        op("swizzle", 5, srcA=5, constant=[0, 0, 0, 0]),
        op("multiply", 4, srcA=4, srcB=5),
        op("multiply", 1, srcA=1, srcB=4),
        op("constant", 6, constant=[0.01, 0.01, 0.012, 1]),
    ]
    # The Throat: the strands' brightness is their band's energy at their moment, with contrast, so the
    # loud moments read as bright rings climbing the funnel and the quiet ones as gaps.
    throat = [
        op("input", 0, input="materialEmission"),
        op("input", 1, input="instanceEmissive"),
        op("swizzle", 1, srcA=1, constant=[0, 0, 0, 0]),
        # the multiplier is 1 + 3 x band energy; only the loudest part of each band's range lights (energy > ~0.55),
        # so a loud moment is a bright ring that climbs and fades, and the rest of the tower is dark glass
        op("remap", 1, srcA=1, value=1, constant=[2.6, 4.0, 0.0, 1.0]),
        op("power", 1, srcA=1, value=2.0),
        op("constant", 2, constant=[60.0, 60.0, 60.0, 0.0]),
        op("multiply", 1, srcA=1, srcB=2),
        op("multiply", 0, srcA=0, srcB=1),
        op("constant", 6, constant=[0.02, 0.02, 0.024, 1]),
    ]
    return [
        {"name": "throat", "ops": throat, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 0,
         "emissionIntensity": 1.0, "opacity": -1},
        {"name": "bed", "ops": bed, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 1,
         "emissionIntensity": 1.0, "opacity": -1},
        {"name": "choir", "ops": choir, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 1,
         "emissionIntensity": 1.0, "opacity": -1},
    ]


def fields():
    return [
        # what the organism hears: each species its own band (fieldElement = (species + 0.5) / 3)
        field("bands", kind="spectrum", audioBand="element", bandLow=0.0, bandHigh=1.0, strength=1.0),
        field("hunger", kind="compound", children=["bands", "kickRing", "strike"], combine="add", strength=1.0),
        field("wander", kind="curlNoise", frequency=0.035, speed=0.08, strength=1.0),
        field("inflow", kind="spiral", axis=[0, 1, 0], spiralBias=-0.55, strength=0.9,
              falloff={"kind": "smoothstep", "inner": 6.0, "outer": 85.0}),
        field("drift", kind="compound", children=["wander", "inflow"], combine="add", strength=1.0),
        # the organism's memory, read by the floor, the forest and the spores
        field("trail", kind="grid", reference="organism", strength=1.0),
        field("trailN", kind="grid", reference="organism", strength=0.8),
        field("trailCap", kind="constant", strength=1.0),
        field("growth", kind="compound", children=["trailN", "trailCap"], combine="min", strength=1.0),
        # the forest hears the song delayed by its distance from the Cochlea
        field("echo", kind="spectrum", audioBand="element", bandLow=0.0, bandHigh=1.0, audioSpeed=ECHO_SPEED,
              waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
        # kick fronts racing outward from the Cochlea's foot
        field("kickRing", kind="onset", onsetSource="low", onsetDecay=0.8, onsetWidth=6.5, audioSpeed=RING_SPEED,
              waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
        # the performer's strike (MIDI pad 36): one shock front from the Cochlea's foot to the horizon
        field("strike", kind="wave", waveGeometry="radial", waveShape="pulse", axis=[0, 1, 0], amplitude=1.0,
              wavelength=2000.0, waveSpeed=30.0, waveWidth=9.0, waveOrigin=0.0, strength=3.0,
              trigger={"source": "signal", "name": "control.strike", "threshold": 0.3}),
        # the Cochlea's three strands, each a band, heard later the higher it is
        field("helixLow", kind="spectrum", audioBand="range", bandLow=0.0, bandHigh=0.3, audioSpeed=HELIX_SPEED,
              waveGeometry="planar", axis=[0, 1, 0], strength=1.0),
        field("helixMid", kind="spectrum", audioBand="range", bandLow=0.3, bandHigh=0.65, audioSpeed=HELIX_SPEED,
              waveGeometry="planar", axis=[0, 1, 0], strength=1.0),
        field("helixHigh", kind="spectrum", audioBand="range", bandLow=0.65, bandHigh=1.0, audioSpeed=HELIX_SPEED,
              waveGeometry="planar", axis=[0, 1, 0], strength=1.0),
        # the horizon: no spires inside the basin
        field("basinMask", kind="sphere", radius=BASIN + 40.0, softness=12.0, invert=True, strength=1.0),
    ]


def organism():
    return {
        "name": "organism", "enabled": True, "mode": "agents", "wrap": "wrap",
        "resolution": [1024, 1, 1024], "boundsMin": [-BASIN, -1, -BASIN], "boundsMax": [BASIN, 1, BASIN],
        "velocityField": "drift", "advect": 0.15, "diffusion": 0.12, "dissipation": 3.5,
        "simRate": 30.0, "maxSubSteps": 4, "seed": 2026, "agentCount": 380000, "species": 3,
        "sensorAngle": 0.5, "sensorDistance": 5.0, "turnAngle": 0.5, "stepSize": 2.0,
        "depositAmount": 0.042, "repel": 0.7, "depositField": "hunger", "checkpointInterval": 5.0,
    }


def bed():
    return {"name": "bed", "kind": "procedural", "procedural": {
        # a disc, so the basin's edge needs no mask in the program (a subdivided floor heaved by a field
        # deformer cost 21 ms at 1080p: 0.8M triangles each running the onset loop; the forest heaves instead)
        "source": {"kind": "cylinder", "radius": BASIN, "height": 0.02, "radialSegments": 160, "caps": True},
        "distribution": {"kind": "single"},
        "lod": {"cull": False, "count": 1},
        "material": {"baseColor": [0.006, 0.006, 0.009], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.85, "metallic": 0.0, "program": "bed"}}}


def choir():
    return {"name": "choir", "kind": "procedural", "procedural": {
        "source": {"kind": "cylinder", "radius": 0.045, "height": 1.0, "radialSegments": 3, "caps": False},
        "sourceTransform": {"position": [0, 0.5, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
        "variation": {"seed": 11},
        "distribution": {"kind": "generator", "generator": {
            "cellSize": 0.52, "viewDistance": 62.0, "presence": 1.0, "jitter": 1.0, "sizeMin": 0.7, "sizeMax": 1.5,
            "tilt": 0.1, "bounded": True, "regionMin": [-FOREST_R, -FOREST_R], "regionMax": [FOREST_R, FOREST_R],
            "regionRadius": FOREST_R, "groundHeight": 0.0, "groundAmplitude": 0.0, "groundFrequency": 0.01,
            "groundSeed": 7}},
        "effectors": [
            # grows only where the organism has walked: scale x min(trail / 30, 1)
            {"field": "growth", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            # each filament's own band, at its own moment, lifts it
            {"field": "echo", "op": "scale", "blend": "add", "strength": 2.2, "scaleAxis": [0, 1, 0]},
            {"field": "echo", "op": "emission", "blend": "add", "strength": 5.0},
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 8.0},
            {"field": "strike", "op": "emission", "blend": "add", "strength": 8.0},
            # LOW is mass: the forest heaves as a kick front passes under it
            {"field": "kickRing", "op": "positionOffset", "blend": "add", "strength": 0.6, "axis": [0, 1, 0]},
        ],
        "lod": {"cull": True, "maxDistance": 66.0, "count": 1},
        "material": {"baseColor": [0.01, 0.01, 0.012], "emissiveColor": [1, 1, 1], "emissiveIntensity": 0.6,
                     "roughness": 0.4, "metallic": 0.0, "program": "choir"}}}


# Each strand's gain into the tower's contrast curve (throat program: lit above a multiplier of 2.6). Mastered
# music carries more normalised energy in the lows than in the air, so the higher strands get more gain and all
# three bands light about as often.
STRAND_GAIN = {"helixLow": 3.1, "helixMid": 3.5, "helixHigh": 3.5}


def spiral_points(n, radius, growth, turns, height, y0, angle0):
    """Placements along a spiral: each a thread segment whose local +x lies along the spiral and whose length
    (scale x) is the arc to the next one, so consecutive segments meet."""
    out = []
    pts = []
    for k in range(n + 1):
        u = k / n
        a = angle0 + u * turns * 2.0 * math.pi
        r = radius + growth * u
        pts.append((r * math.cos(a), y0 + height * u, r * math.sin(a)))
    for k in range(n):
        (x0, y_0, z0), (x1, y1, z1) = pts[k], pts[k + 1]
        dx, dy, dz = x1 - x0, y1 - y_0, z1 - z0
        length = math.sqrt(dx * dx + dy * dy + dz * dz)
        yaw = math.atan2(-dz, dx)            # about +y: local +x -> (cos yaw, 0, -sin yaw)
        pitch = math.atan2(dy, math.hypot(dx, dz))  # then about local +z, to follow the rise
        cy, sy, cp, sp = math.cos(yaw / 2), math.sin(yaw / 2), math.cos(pitch / 2), math.sin(pitch / 2)
        # q = q_yaw(y) * q_pitch(z)
        qx, qy, qz, qw = -sy * sp, sy * cp, cy * sp, cy * cp
        mx, my, mz = (x0 + x1) / 2, (y_0 + y1) / 2, (z0 + z1) / 2
        out.append([round(v, 5) for v in (mx, my, mz, qx, qy, qz, qw, length * 1.12, 1.0, 1.0)])
    return out


def helix(name, strand, colour, fieldname):
    """One band of the Cochlea as one object: a flared root (radius 7 -> 2.2 over 0..5 m), the throat opening into
    the canopy (2.2 -> 19 over 5..HELIX_H m), and the same throat wound the other way, so the threads cross into a
    lattice. One object per band: a procedural object costs ~0.4 ms of fixed GPU work whatever its size, and nine
    strand objects cost 3.4 ms at 730x410."""
    a = strand * 2.0943951
    pts = (spiral_points(120, 7.0, -4.8, 1.2, 5.0, 0.3, a)
           + spiral_points(620, 2.2, 16.8, 4.6, HELIX_H - 5.0, 5.3, a + 1.2 * 2 * math.pi)
           + spiral_points(620, 2.2, 16.8, -4.6, HELIX_H - 5.0, 5.3, a + 1.2 * 2 * math.pi + 1.0471976))
    return {"name": name, "kind": "procedural", "procedural": {
        # a thread segment: end to end they draw the tower as threads of light circling upward; its band's energy
        # at its moment thickens and lights it
        # round, so a close camera sees a thread, not a stair of box ends (a cylinder's axis is y: turned onto x)
        "source": {"kind": "cylinder", "radius": 0.028, "height": 1.0, "radialSegments": 5, "caps": False},
        "sourceTransform": {"position": [0, 0, 0], "rotation": [0, 0, 90], "scale": [1, 1, 1]},
        "variation": {"seed": 40 + strand},
        "distribution": {"kind": "points", "points": pts},
        "effectors": [
            {"field": fieldname, "op": "scale", "blend": "add", "strength": 4.0, "scaleAxis": [0.08, 0.3, 0.3]},
            {"field": fieldname, "op": "emission", "blend": "add", "strength": STRAND_GAIN[fieldname]},
            # a kick's front crosses the foot at once and the rim ~0.9 s later: each kick ripples up and out
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 0.9},
        ],
        "lod": {"cull": True, "count": 1},
        "material": {"baseColor": [0.015, 0.015, 0.018], "emissiveColor": colour, "emissiveIntensity": 0.025,
                     "roughness": 0.22, "metallic": 0.85, "program": "throat"}}}


def plain():
    """The dark land beyond the basin, so the horizon's spires stand on something."""
    return {"name": "plain", "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [1000.0, 0.02, 1000.0], "subdivisions": 1},
        "sourceTransform": {"position": [0, -0.03, 0], "rotation": [0, 0, 0], "scale": [4, 1, 4]},
        "distribution": {"kind": "single"},
        "lod": {"cull": False, "count": 1},
        "material": {"baseColor": [0.004, 0.004, 0.006], "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.45, "metallic": 0.0}}}


def spine():
    return {"name": "spine", "kind": "procedural", "procedural": {
        "source": {"kind": "cylinder", "radius": 0.22, "height": HELIX_H + 4, "radialSegments": 24, "caps": True},
        "sourceTransform": {"position": [0, (HELIX_H + 4) / 2, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
        "distribution": {"kind": "single"},
        "material": {"baseColor": [0.01, 0.01, 0.012], "emissiveColor": [1.0, 0.85, 0.7],
                     "emissiveIntensity": 0.0, "roughness": 0.15, "metallic": 0.9}}}


def horizon():
    return {"name": "horizon", "kind": "procedural", "procedural": {
        "source": {"kind": "cylinder", "radius": 0.3, "height": 1.0, "radialSegments": 6, "caps": True},
        "sourceTransform": {"position": [0, 0.5, 0], "rotation": [0, 0, 0], "scale": [1, 14, 1]},
        "variation": {"seed": 77},
        "distribution": {"kind": "generator", "generator": {
            "cellSize": 9.0, "viewDistance": 420.0, "presence": 0.32, "clusterSize": 7.0, "clusterContrast": 0.8,
            "jitter": 1.0, "sizeMin": 0.5, "sizeMax": 2.4, "tilt": 0.05, "bounded": False,
            "groundHeight": 0.0, "groundAmplitude": 0.0, "groundFrequency": 0.006, "groundSeed": 3}},
        "effectors": [
            {"field": "basinMask", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 220.0},
            {"field": "strike", "op": "emission", "blend": "add", "strength": 200.0},
        ],
        "lod": {"cull": True, "maxDistance": 420.0, "count": 1},
        "material": {"baseColor": [0.004, 0.004, 0.005], "emissiveColor": [0.75, 0.8, 1.0],
                     "emissiveIntensity": 0.002, "roughness": 0.8, "metallic": 0.0}}}


def spores():
    return {"name": "spores", "kind": "particles", "particles": {
        "capacity": 65536, "seed": 9, "shape": "disc", "position": [0, 0.15, 0], "extent": [FOREST_R, 0, FOREST_R],
        "direction": [0, 1, 0], "spread": 0.35, "spawnRate": 600,
        "speedMin": 0.15, "speedMax": 0.6, "gravity": [0, 0.05, 0], "drag": 0.12,
        "turbulence": 0.3, "turbulenceScale": 0.08, "turbulenceSpeed": 0.2,
        # what the veins release is drawn into the throat and spirals up it: floor, forest and tower are one system
        "attractorPosition": [0, 14, 0], "attractorStrength": 0.8, "attractorRadius": 90.0, "orbit": 0.9,
        "velocityStretch": 0.6, "stretchMin": 1.0, "stretchMax": 6.0,
        "emitMaskField": "trail",
        "sizeStart": 0.06, "sizeEnd": 0.015, "sizeVariance": 0.7, "lifetimeMin": 7.0, "lifetimeMax": 12.0,
        "colorStart": [1.0, 0.62, 0.3, 1.0], "colorEnd": [0.9, 0.3, 0.5, 0.0], "emissive": 9.0,
        "blend": "additive", "softness": 0.6}}


def scene():
    return {
        "format": "avgen-scene", "version": 1, "name": "PHONOTAXIS",
        "_note": "The flagship LIVE scene. Generated by build.py; edit that, not this file.",
        "camera": {"mode": 1, "position": [30, 4, 34], "target": [0, 12, 0], "fov": 42, "orbitSpeed": 0.0},
        "environment": {
            "background": [0.0015, 0.0016, 0.004], "intensity": 0.05,
            "fogColor": [0.016, 0.012, 0.034], "horizonDensity": 0.5,
            # a thin medium the heart scatters into: the throat glows, the basin's edge dissolves into air
            "volumeDensity": 0.008, "volumeScattering": 1.0, "volumeAbsorption": 0.8, "volumeAnisotropy": 0.35,
            "volumeLocalLights": 0.45, "volumeSteps": 16, "volumeJitter": 0.5, "volumeMaxDistance": 600.0,
            "volumeNoise": 0.35, "volumeNoiseScale": 0.03, "volumeNoiseSpeed": 0.05,
            "sky": {"enabled": True, "background": True, "zenithColor": [0.0006, 0.0007, 0.002],
                    "horizonColor": [0.014, 0.009, 0.026], "groundColor": [0.001, 0.001, 0.002], "haze": 0.0,
                    "sunIntensity": 0.0, "intensity": 1.0, "useKeyLight": False}},
        "lights": [
            {"name": "heart", "id": "heart", "type": "point", "position": [0, 4.0, 0], "color": [1.0, 0.36, 0.13],
             "intensity": 900, "range": 42, "radius": 1.0, "castsShadow": False, "volumetric": 0.3},
            {"name": "moon", "id": "moon", "type": "directional", "direction": [-0.3, -0.8, -0.5],
             "color": [0.55, 0.65, 1.0], "intensity": 0.35, "castsShadow": False, "volumetric": 0.0},
        ],
        "effects": [
            {"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True,
             "parameters": {"brightness": 0.6, "density": 0.25, "magnitudeSlope": 6, "colorSpread": 0.4,
                            "twinkle": 0.2, "twinkleRate": 1.0, "horizonFade": 0.7, "band": 0.15, "bandTilt": 25,
                            "daylight": 0}}],
        "grids": [organism()],
        "materialPrograms": programs(),
        "nodes": [*fields(), bed(), choir(),
                  # objects named apart from their fields: a node name shared with a field node loses its parameters
                  *[helix(n.replace("helix", "tower"), i, c, n)
                    for i, (n, c) in enumerate([("helixLow", EMBER), ("helixMid", TEAL), ("helixHigh", VIOLET)])],
                  plain(), horizon(), spores()],
    }


# ===================================================================================== the performance
#
# The composition's four default routes, zeroed and re-routed on purpose below (pitfall 1).
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
    "temporal/echo/decay": 0.6,
    "camera/mode": 0, "camera/orbitPivotWeight": 1.0,
}
# Owned by the states (presets), so never set here: a project's parameters apply after the initial state's
# preset and would pin them. camera/distance, height, fov, orbitPivot, orbitSpeed, exposure compensation,
# temporal/echo/strength, post/bloom/intensity.

# What the world listens to as one number: loudness, change and air, read as a mean and shaped. The slow
# follower that turns it into the piece's energy lives on the route into the `energy` macro.
LISTEN = {"kind": "interpret", "name": "listen", "settings": {"mappings": [
    {"name": "drive", "combine": "mean", "inputs": [
        {"signal": "audio.rms", "weight": 1.0}, {"signal": "audio.spectralFlux", "weight": 1.0},
        {"signal": "audio.treble", "weight": 1.0}], "bias": -0.1, "gain": 2.6, "curve": 1.0}]}}

# The performer's knobs. Each is neutral at its default, so a controller that is not plugged in changes
# nothing; each moves a few parameters that mean one thing together.
#   (name, label, cc, default, [(path, min, max, op)])
KNOBS = [
    ("energy", "ENERGY", 1, 0.0, []),  # targets below: the arc itself
    ("hunger", "HUNGER", 21, 0.5, [("grid/organism/depositAmount", -0.034, 0.084, "add")]),
    ("restless", "RESTLESS", 22, 0.5, [("grid/organism/turnAngle", -0.3, 0.6, "add"),
                                        ("grid/organism/sensorAngle", -0.2, 0.4, "add")]),
    ("current", "CURRENT", 23, 0.5, [("field/inflow/strength", -1.0, 1.6, "add")]),
    ("memory", "MEMORY", 24, 0.0, [("temporal/echo/strength", 0.0, 0.6, "add"),
                                   ("temporal/echo/decay", 0.0, 0.3, "add")]),
    ("reach", "REACH", 25, 0.5, [("camera/distance", -14.0, 40.0, "add"), ("camera/height", -3.0, 30.0, "add")]),
    ("orbit", "ORBIT", 26, 0.5, [("camera/orbitSpeed", -0.18, 0.18, "add")]),
    ("glow", "GLOW", 27, 0.5, [("post/bloom/intensity", -0.3, 0.45, "add"),
                               ("post/halation/intensity", -0.15, 0.3, "add")]),
    ("sensitivity", "SENSITIVITY", 28, 0.5, []),  # the depth of the music's push on the energy (a route depth)
]
# The energy arc (macro.energy, 0..1) adds this much on top of whatever the state set.
ENERGY_TARGETS = [
    ("camera/height", 0.0, 7.0, "add"),
    ("material/throat/emissionIntensity", 0.0, 0.6, "add"),
    ("material/choir/emissionIntensity", 0.0, 0.4, "add"),
    ("particles/spores/spawnRate", 0.0, 500.0, "add"),
    ("post/bloom/intensity", 0.0, 0.12, "add"),
]

# Pads (note numbers, General MIDI drum map so any pad controller lands somewhere sensible).
PADS = {"strike": 36, "scatter": 37, "Dormant": 40, "Germination": 41, "Chorus": 43, "Surge": 45,
        "Eruption": 47, "Collapse": 48, "Rebirth": 50}

# ------------------------------------------------------------------------------------------ states
# Each state is a preset: the large-scale configuration of the world. Continuous expression (the energy
# arc, the bands, the knobs) rides on top as routes. Colour rule kept in every state: colour is frequency.
# The reborn world keeps the rule (colour is frequency) in one molten hue family: ember, gold, white.
REBIRTH = {"lo": [1.0, 0.28, 0.04], "mid": [1.0, 0.66, 0.2], "hi": [0.88, 0.9, 1.0]}
NIGHT = {"lo": EMBER, "mid": TEAL, "hi": VIOLET}

COLS = ["growth", "lift", "choirGain", "throatGain", "bedGain", "deposit", "fade", "turn", "gaze", "reach",
        "inflow", "wander", "spores", "kick", "horizon", "dist", "height", "fov", "target", "exposure", "echo",
        "bloom", "heart", "orbit"]
STATES = {
    #            grow lift  chG  thG  bedG  dep    fade turn gaze rch  infl wand spor  kick hor  dist hgt  fov tgtY  exp   echo bloom heart orbit
    "Dormant":     (0.0, 0.4, 0.15, 0.06, 0.22, 0.0112, 1.6, 0.35, 0.5, 5.0, 0.4, 1.0, 25, 0.3, 25, 28, 1.5, 56, 15, -0.7, 0.00, 0.40, 125, 0.015),
    "Germination": (0.35, 1.2, 0.55, 0.45, 0.6, 0.0336, 2.5, 0.45, 0.5, 5.0, 0.7, 1.0, 110, 0.6, 60, 42, 4.0, 48, 8, -0.35, 0.0, 0.45, 300, 0.03),
    "Chorus":      (0.8, 2.2, 1.0, 1.0, 1.0, 0.0420, 3.5, 0.5, 0.5, 5.0, 0.9, 1.0, 275, 1.0, 120, 64, 14, 44, 13, 0.0, 0.12, 0.50, 450, 0.04),
    "Surge":       (1.0, 3.0, 1.25, 1.3, 1.0, 0.0560, 3.5, 0.6, 0.55, 5.0, 1.1, 1.0, 425, 1.3, 180, 98, 48, 40, 4, 0.0, 0.2, 0.55, 600, 0.06),
    "Eruption":    (1.25, 3.6, 1.7, 2.0, 1.2, 0.0840, 3.0, 0.9, 0.7, 4.0, 1.8, 1.5, 1300, 1.5, 220, 32, 3.0, 70, 18, 0.15, 0.45, 0.70, 600, 0.12),
    "Collapse":    (0.0, 0.5, 0.2, 0.6, 0.6, 0.0000, 6.0, 0.5, 0.5, 5.0, 0.0, 2.5, 45, 0.5, 60, 16, 108, 46, 0, -0.2, 0.35, 0.45, 100, 0.02),
    "Rebirth":     (0.8, 2.2, 1.0, 1.0, 1.0, 0.0420, 3.5, 0.25, 0.25, 14.0, 0.5, 1.0, 275, 1.0, 120, 78, 2.2, 32, 11, 0.0, 0.12, 0.50, 450, -0.035),
}
# Off-hero pivots: the orbit circles a point beside the Cochlea, so the hero drifts through the frame with parallax
# instead of sitting dead centre in every state.
PIVOT_X = {"Germination": 22.0, "Rebirth": -26.0, "Surge": 18.0, "Dormant": -6.0}
PIVOT_Z = {"Germination": 10.0, "Rebirth": 14.0, "Surge": -12.0, "Dormant": 4.0}
PALETTE_BY_STATE = {"Dormant": NIGHT, "Rebirth": REBIRTH}  # only the world-defining states recolour


def colour_paths(prog_ops, prog_name, colour):
    """The bed and choir programs' three species constants: the op paths (1-indexed) holding them."""
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
            "field/trailN/strength": [v["growth"]],
            "procedural/choir/effector/2/strength": [v["lift"]],
            "material/choir/emissionIntensity": [v["choirGain"]],
            "material/throat/emissionIntensity": [v["throatGain"]],
            "material/bed/emissionIntensity": [v["bedGain"]],
            "grid/organism/depositAmount": [v["deposit"]],
            "grid/organism/dissipation": [v["fade"]],
            "grid/organism/turnAngle": [v["turn"]],
            "grid/organism/sensorAngle": [v["gaze"]],
            "grid/organism/sensorDistance": [v["reach"]],
            "field/inflow/strength": [v["inflow"]],
            "field/wander/strength": [v["wander"]],
            "particles/spores/spawnRate": [v["spores"]],
            "field/kickRing/strength": [v["kick"]],
            "procedural/horizon/effector/2/strength": [v["horizon"]],
            "camera/distance": [v["dist"]],
            "camera/height": [v["height"]],
            "camera/fov": [v["fov"]],
            "camera/orbitPivot": [PIVOT_X.get(name, 0.0), v["target"], PIVOT_Z.get(name, 0.0)],
            "camera/exposure/compensation": [v["exposure"]],
            "temporal/echo/strength": [v["echo"]],
            "post/bloom/intensity": [v["bloom"]],
            "lights/heart/intensity": [v["heart"]],
            "camera/orbitSpeed": [v["orbit"]],
        }
        if name in PALETTE_BY_STATE:
            pal = PALETTE_BY_STATE[name]
            for prog in ("bed", "choir"):
                for path, c in colour_paths(progs[prog], prog, pal).items():
                    values[path] = c
            for strand, key in (("helixLow", "lo"), ("helixMid", "mid"), ("helixHigh", "hi")):
                values[f"procedural/{strand.replace('helix', 'tower')}/material/emissiveColor"] = pal[key]
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


def states():
    up = ["Germination", "Chorus", "Surge", "Eruption", "Rebirth"]
    return {"initial": "Dormant", "states": [
        {"name": "Dormant", "preset": "dormant", "transition": {"seconds": 10, "easing": "smooth"},
         "triggers": [pad("Dormant")] + [trig_energy(0.08, frm=f, falling=True) for f in up + ["Collapse"]]},
        {"name": "Germination", "preset": "germination", "transition": {"seconds": 8, "easing": "smooth"},
         "triggers": [pad("Germination"), trig_energy(0.15, frm="Dormant")]},
        {"name": "Chorus", "preset": "chorus", "transition": {"seconds": 6, "easing": "smooth"},
         "triggers": [pad("Chorus"), trig_energy(0.45, frm="Germination"),
                      trig_energy(0.62, frm="Surge", falling=True)]},
        {"name": "Surge", "preset": "surge", "transition": {"seconds": 4, "easing": "easeInOut"},
         "triggers": [pad("Surge"), trig_energy(0.72, frm="Chorus"), trig_energy(0.72, frm="Rebirth"),
                      trig_energy(0.7, frm="Eruption", falling=True)]},
        {"name": "Eruption", "preset": "eruption", "transition": {"seconds": 1.5, "easing": "easeIn"},
         "triggers": [pad("Eruption"), trig_energy(0.86, frm="Surge")]},
        {"name": "Collapse", "preset": "collapse", "transition": {"seconds": 3, "easing": "easeOut"},
         # well below each source state's own entry, so a state never collapses on the wobble that entered it
         "triggers": [pad("Collapse")] + [trig_energy(t, frm=f, falling=True)
                                          for f, t in (("Chorus", 0.3), ("Surge", 0.5), ("Eruption", 0.55),
                                                       ("Rebirth", 0.3))]},
        {"name": "Rebirth", "preset": "rebirth", "transition": {"seconds": 10, "easing": "smooth"},
         "triggers": [pad("Rebirth"), trig_energy(0.5, frm="Collapse")]},
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
        bindings.append({"source": "*", "channel": -1, "kind": "cc", "number": cc, "parameter": f"macros/{name}",
                         "component": 0, "min": 0.0, "max": 1.0})
    for name, note in PADS.items():
        sig = name if name in ("strike", "scatter") else f"pad{name}"
        bindings.append({"source": "*", "channel": -1, "kind": "noteEvent", "number": note, "signal": sig})
    return {"enabled": True, "filter": "*", "bindings": bindings}


def routes():
    r = list(ZERO_DEFAULTS)
    # The arc: what the music does to the world's energy, slowly, scaled by the SENSITIVITY knob.
    r.append({"source": "visual.drive", "target": "macros/energy", "op": "add", "amount": 1.0,
              "depthSource": "macro.sensitivity", "depthMin": 0.0, "depthMax": 2.0,
              "chain": {"attackMs": 2500, "decayMs": 4000}})
    # LOW is mass and fronts (the kick fields, the heaving floor, the low strand): it lives in fields.
    # A heavy kick, when the energy is high, nudges the camera; never in the quiet states.
    r.append({"source": "audio.onsetLow", "target": "camera/shake/amplitude", "op": "add", "amount": 0.05,
              "depthSource": "macro.energy", "depthMin": 0.0, "depthMax": 1.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 30, "envelopeFallPerSecond": 6.0}})
    # MID is movement: the organism grows restless and wanders with the mids.
    r.append({"source": "audio.mid", "target": "grid/organism/turnAngle", "op": "add", "amount": 0.22,
              "chain": {"attackMs": 600, "decayMs": 2500}})
    r.append({"source": "audio.mid", "target": "field/wander/strength", "op": "add", "amount": 0.6,
              "chain": {"attackMs": 800, "decayMs": 3000}})
    # HIGH is emission: spores rise and the forest's tips glint with the air.
    r.append({"source": "audio.treble", "target": "particles/spores/spawnRate", "op": "add", "amount": 700.0,
              "chain": {"attackMs": 80, "decayMs": 700}})
    r.append({"source": "audio.onsetHigh", "target": "material/choir/emissionIntensity", "op": "add",
              "amount": 0.35, "chain": {"envelope": "peakhold", "envelopeHoldMs": 20, "envelopeFallPerSecond": 5.0}})
    # The performer's events: a strike is a front (a field) plus a burst of spores; scatter is the burst alone.
    r.append({"source": "control.strike", "target": "particles/spores/burst", "op": "add", "amount": 2500.0,
              "depthSource": "control.strike", "depthMin": 0.4, "depthMax": 1.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 120, "envelopeFallPerSecond": 8.0}})
    r.append({"source": "control.strike", "target": "camera/shake/amplitude", "op": "add", "amount": 0.12,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 40, "envelopeFallPerSecond": 3.0}})
    r.append({"source": "control.scatter", "target": "particles/spores/burst", "op": "add", "amount": 1800.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 200, "envelopeFallPerSecond": 5.0}})
    return r


def project(name, audio, live=False):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "phonotaxis.scene.json"}},
        "live": {"qualityStrategy": "effects_first", "targetFps": 60},
        "parameters": dict(POST),
        "sources": [LISTEN],
        "routes": routes(),
        "worldMacros": macros(),
        "presets": presets(),
        "states": states(),
        "control": {"midi": midi()},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "sequence", "path": "renders/phonotaxis"},
    }
    if audio:
        p["assets"]["audio"] = {"path": audio}
    if live:
        p["sonic"] = {"live": True}
    return p


def main():
    (HERE / "phonotaxis.scene.json").write_text(json.dumps(scene(), indent=1) + "\n")
    projects = {
        # development and the review renders: the song the spike's Echo Field was made on
        "phonotaxis.json": project("PHONOTAXIS", "~/Desktop/All You Got.wav"),
        # a second, shorter track with a long quiet intro and a breakdown (graceful with other music)
        "phonotaxis-night-shift.json": project("PHONOTAXIS / Night Shift", "../../assets/audio/night-shift.wav"),
        # the instrument: live input (pick the device in the Live panel) and MIDI
        "phonotaxis-live.json": project("PHONOTAXIS LIVE", None, live=True),
    }
    for name, p in projects.items():
        (HERE / name).write_text(json.dumps(p, indent=1) + "\n")
    print("wrote", HERE / "phonotaxis.scene.json", *projects)


if __name__ == "__main__":
    main()
