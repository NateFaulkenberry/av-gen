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

BASIN = 64.0          # half extent of the organism's plane (m)
FOREST_R = 58.0       # radius of the forest disc
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
        # compress: trail / T, clamped, then a soft knee
        op("remap", 0, srcA=0, value=1, constant=[0.0, 3.0, 0.0, 1.0]),
        op("power", 0, srcA=0, value=0.55),
        *species_colour_ops(0, 1, (2, 3)),
        op("field", 4, field="kickRing"),
        op("constant", 5, constant=[2.5, 2.5, 2.5, 0]),
        op("multiply", 4, srcA=4, srcB=5),
        op("constant", 5, constant=[1, 1, 1, 0]),
        op("add", 4, srcA=4, srcB=5),
        op("multiply", 1, srcA=1, srcB=4),
        # the basin is a disc: fade the last 10 m
        op("input", 7, input="worldPosition"),
        op("multiply", 7, srcA=7, srcB=7),
        op("gradient", 7, srcA=7, value=1.0 / (BASIN * BASIN), constant=[1, 0, 1, 0]),  # (r / R)^2
        op("remap", 7, srcA=7, value=1, constant=[0.7, 1.0, 1.0, 0.0]),
        op("multiply", 1, srcA=1, srcB=7),
        op("constant", 6, constant=[0.006, 0.006, 0.009, 1]),
    ]
    # The Choir: each filament takes the colour of whoever walked there (normalised species mix), its
    # brightness from the instance's emission multiplier (echo + kick effectors), brighter at the tip.
    choir = [
        op("field", 0, field="trail"),
        *species_colour_ops(0, 1, (2, 3)),
        # normalise by x + y + z
        op("swizzle", 2, srcA=0, constant=[0, 0, 0, 0]),
        op("swizzle", 3, srcA=0, constant=[1, 1, 1, 1]),
        op("add", 2, srcA=2, srcB=3),
        op("swizzle", 3, srcA=0, constant=[2, 2, 2, 2]),
        op("add", 2, srcA=2, srcB=3),
        op("constant", 3, constant=[0.05, 0.05, 0.05, 0.05]),
        op("add", 2, srcA=2, srcB=3),
        op("power", 2, srcA=2, value=-1.0),
        op("multiply", 1, srcA=1, srcB=2),
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
        op("power", 1, srcA=1, value=2.4),
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
        field("kick", kind="onset", onsetSource="low", onsetDecay=3.0, onsetWidth=0.0, strength=1.2),
        field("hunger", kind="compound", children=["bands", "kick"], combine="add", strength=1.0),
        field("wander", kind="curlNoise", frequency=0.035, speed=0.08, strength=1.0),
        field("inflow", kind="spiral", axis=[0, 1, 0], spiralBias=-0.55, strength=0.9,
              falloff={"kind": "smoothstep", "inner": 6.0, "outer": 60.0}),
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
        field("kickRing", kind="onset", onsetSource="low", onsetDecay=1.1, onsetWidth=4.0, audioSpeed=RING_SPEED,
              waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
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
        "simRate": 60.0, "maxSubSteps": 4, "seed": 2026, "agentCount": 800000, "species": 3,
        "sensorAngle": 0.5, "sensorDistance": 5.0, "turnAngle": 0.5, "stepSize": 1.0,
        "depositAmount": 0.015, "repel": 0.7, "depositField": "hunger", "checkpointInterval": 5.0,
    }


def bed():
    return {"name": "bed", "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [2 * BASIN, 0.02, 2 * BASIN], "subdivisions": 1},
        "distribution": {"kind": "single"},
        "lod": {"cull": False, "count": 1},
        "material": {"baseColor": [0.006, 0.006, 0.009], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.85, "metallic": 0.0, "program": "bed"}}}


def choir():
    return {"name": "choir", "kind": "procedural", "procedural": {
        "source": {"kind": "cylinder", "radius": 0.022, "height": 1.0, "radialSegments": 4, "caps": False},
        "sourceTransform": {"position": [0, 0.5, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
        "variation": {"seed": 11},
        "distribution": {"kind": "generator", "generator": {
            "cellSize": 0.32, "viewDistance": 70.0, "presence": 1.0, "jitter": 1.0, "sizeMin": 0.7, "sizeMax": 1.5,
            "tilt": 0.1, "bounded": True, "regionMin": [-FOREST_R, -FOREST_R], "regionMax": [FOREST_R, FOREST_R],
            "regionRadius": FOREST_R, "groundHeight": 0.0, "groundAmplitude": 0.0, "groundFrequency": 0.01,
            "groundSeed": 7}},
        "effectors": [
            # grows only where the organism has walked: scale x min(trail / 30, 1)
            {"field": "growth", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            # each filament's own band, at its own moment, lifts it
            {"field": "echo", "op": "scale", "blend": "add", "strength": 2.2, "scaleAxis": [0, 1, 0]},
            {"field": "echo", "op": "emission", "blend": "add", "strength": 5.0},
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 3.0},
        ],
        "lod": {"cull": True, "maxDistance": 95.0, "count": 1},
        "material": {"baseColor": [0.01, 0.01, 0.012], "emissiveColor": [1, 1, 1], "emissiveIntensity": 0.6,
                     "roughness": 0.4, "metallic": 0.0, "program": "choir"}}}


def helix(name, strand, colour, fieldname, segment):
    """One strand of the Cochlea. segment "root": the flared foot (radius 7 -> 2.2 over 0..5 m);
    "bell": the throat opening into the canopy (2.2 -> 19 over 5..HELIX_H m)."""
    if segment == "root":
        dist = {"count": 90, "radius": 7.0, "radiusGrowth": -4.8, "turns": 1.2, "spiralHeight": 5.0,
                "center": [0, 0.3, 0]}
    else:
        dist = {"count": 520, "radius": 2.2, "radiusGrowth": 16.8, "turns": 4.6, "spiralHeight": HELIX_H - 5.0,
                "center": [0, 5.3, 0]}
    dist.update({"kind": "spiral", "spiralAngle": strand * 2.0943951 + (0.0 if segment == "root" else 1.2 * 6.2831853),
                 "plane": "xz", "orientation": "outward"})
    return {"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [0.09, 1.5, 0.05], "subdivisions": 1},
        "variation": {"seed": 40 + strand},
        "distribution": dist,
        "effectors": [
            {"field": fieldname, "op": "scale", "blend": "add", "strength": 1.4, "scaleAxis": [0.6, 1.0, 0.6]},
            {"field": fieldname, "op": "emission", "blend": "add", "strength": 3.0},
        ],
        "lod": {"cull": True, "count": 1},
        "material": {"baseColor": [0.02, 0.02, 0.024], "emissiveColor": colour, "emissiveIntensity": 0.05,
                     "roughness": 0.3, "metallic": 0.7, "program": "throat"}}}


def plain():
    """The dark land beyond the basin, so the horizon's spires stand on something."""
    return {"name": "plain", "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [4000.0, 0.02, 4000.0], "subdivisions": 1},
        "sourceTransform": {"position": [0, -0.03, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
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
            {"field": "kickRing", "op": "emission", "blend": "add", "strength": 120.0},
        ],
        "lod": {"cull": True, "maxDistance": 420.0, "count": 1},
        "material": {"baseColor": [0.004, 0.004, 0.005], "emissiveColor": [0.75, 0.8, 1.0],
                     "emissiveIntensity": 0.002, "roughness": 0.8, "metallic": 0.0}}}


def spores():
    return {"name": "spores", "kind": "particles", "particles": {
        "capacity": 65536, "seed": 9, "shape": "disc", "position": [0, 0.15, 0], "extent": [FOREST_R, 0, FOREST_R],
        "direction": [0, 1, 0], "spread": 0.35, "spawnRate": 600, "lifetimeMin": 5.0, "lifetimeMax": 10.0,
        "speedMin": 0.15, "speedMax": 0.6, "gravity": [0, 0.08, 0], "drag": 0.15,
        "turbulence": 0.35, "turbulenceScale": 0.08, "turbulenceSpeed": 0.2,
        "emitMaskField": "trail",
        "sizeStart": 0.05, "sizeEnd": 0.015, "sizeVariance": 0.5,
        "colorStart": [1.0, 0.85, 0.6, 1.0], "colorEnd": [0.55, 0.4, 1.0, 0.0], "emissive": 5.0,
        "blend": "additive", "softness": 0.6}}


def scene():
    return {
        "format": "avgen-scene", "version": 1, "name": "PHONOTAXIS",
        "_note": "The flagship LIVE scene. Generated by build.py; edit that, not this file.",
        "camera": {"mode": 1, "position": [30, 4, 34], "target": [0, 12, 0], "fov": 42, "orbitSpeed": 0.0},
        "environment": {
            "background": [0.0015, 0.0016, 0.004], "intensity": 0.05,
            "fogColor": [0.012, 0.010, 0.026], "fogHeight": 4.0, "fogHeightFalloff": 0.12, "fogHeightAmount": 0.6,
            "horizonDensity": 0.6,
            "sky": {"enabled": True, "background": True, "zenithColor": [0.0008, 0.0009, 0.0026],
                    "horizonColor": [0.006, 0.004, 0.012], "groundColor": [0.001, 0.001, 0.002], "haze": 0.0,
                    "sunIntensity": 0.0, "intensity": 1.0, "useKeyLight": False}},
        "lights": [
            {"name": "heart", "id": "heart", "type": "point", "position": [0, 4.0, 0], "color": [1.0, 0.7, 0.45],
             "intensity": 900, "range": 70, "radius": 1.0, "castsShadow": False, "volumetric": 1.0},
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
                  *[helix(f"{n}{seg.capitalize()}", i, c, n, seg)
                    for i, (n, c) in enumerate([("helixLow", EMBER), ("helixMid", TEAL), ("helixHigh", VIOLET)])
                    for seg in ("root", "bell")],
                  spine(), plain(), horizon(), spores()],
    }


# The composition's four default routes, zeroed here and re-routed on purpose below (pitfall 1).
ZERO_DEFAULTS = [
    {"source": "audio.bass", "target": "root/scale", "op": "add", "amount": 0.0},
    {"source": "audio.mid", "target": "root/rotationSpeed", "op": "add", "amount": 0.0},
    {"source": "audio.rms", "target": "scene/brightness", "op": "add", "amount": 0.0},
    {"source": "audio.onset", "target": "root/impulse", "op": "add", "amount": 0.0},
]

POST = {
    "post/bloom/enabled": True, "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.9,
    "post/bloom/emissionWeight": 0.8, "post/bloom/radius": 0.8,
    "post/tonemap/chroma-retention": 0.65, "post/output/vignette": 0.45, "post/output/grain": 0.015,
    "post/halation/enabled": True, "post/halation/intensity": 0.18, "post/halation/warmth": 0.5,
    "post/grade/contrast": 1.08, "post/grade/saturation": 1.05,
    "camera/exposure/mode": 0, "camera/exposure/compensation": 0.0,
    "temporal/echo/enabled": True, "temporal/echo/frames": 8.0, "temporal/echo/strength": 0.0,
    "temporal/echo/decay": 0.6,
}


def project(name, audio, extra=None):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "phonotaxis.scene.json"}, "audio": {"path": audio}},
        "live": {"qualityStrategy": "resolution_first", "targetFps": 60},
        "parameters": dict(POST),
        "routes": list(ZERO_DEFAULTS),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "sequence", "path": "renders/phonotaxis"},
    }
    if extra:
        p.update(extra)
    return p


def main():
    (HERE / "phonotaxis.scene.json").write_text(json.dumps(scene(), indent=1) + "\n")
    (HERE / "phonotaxis.json").write_text(
        json.dumps(project("PHONOTAXIS", "~/Desktop/All You Got.wav"), indent=1) + "\n")
    print("wrote", HERE / "phonotaxis.scene.json", HERE / "phonotaxis.json")


if __name__ == "__main__":
    main()
