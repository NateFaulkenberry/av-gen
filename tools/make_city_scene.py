#!/usr/bin/env python3
"""Builds the city for the "Night Shift" proof-of-concept music video (ADR-089).

Writes three scene files under examples/city/:

    plaza.scene.json      a residential block: houses, a street, trees, lamps, a park
    downtown.scene.json   a tower cluster: glass, neon, a wide plaza
    night-shift.scene.json  the piece: both districts as scene nodes, plus the character

Generated rather than hand-written because a city is a *population*, and a population is where
procedural instancing earns its keep: one node is a whole terrace of buildings that differ in
height, footprint, tint and how many of their windows are lit. Hand-authoring forty boxes would
produce forty boxes that all look hand-authored.

Deterministic: every random choice comes from a seeded PRNG here, and the engine's own per-instance
variation is a pure function of (seed, index), so the same arguments produce the same city. That
matters because the piece is rendered offline and compared frame for frame.

## Art direction

A miniature diorama at dusk, not a photograph of a city. What sells that, in order of how much it
matters:

  * **Value, not hue.** Every building sits in a narrow band of mid values with a restrained palette
    -- cream, terracotta, slate, sage, plum, all desaturated to about a third. The only saturated
    things in the frame are the lights.
  * **Warm against cool.** The sky and its bounce are blue; every practical light is sodium amber.
    A frame with both reads as evening whatever the geometry is.
  * **Bevels.** A mathematically sharp edge is the loudest tell that geometry was generated. Every
    box here has a small radius on it, so the edges catch the key light.
  * **Tilt-shift.** The project already has it (ADR-079). Narrowing depth of field to a band across
    the middle is what makes a wide shot of a city read as a model of a city, and it is one
    parameter.

Usage: make_city_scene.py [--out examples/city] [--seed 20260911]
Pure standard library.
"""
import argparse
import json
import math
import os
import random

# ---- palette ----------------------------------------------------------------------------------
# Linear-ish scene colours. Desaturated on purpose: the saturation in this picture comes from the
# lights, and a saturated building next to a sodium lamp fights it.
FACADES = [
    [0.62, 0.56, 0.44],  # cream
    [0.48, 0.30, 0.24],  # terracotta
    [0.26, 0.30, 0.34],  # slate
    [0.32, 0.36, 0.29],  # sage
    [0.33, 0.27, 0.32],  # plum
    [0.54, 0.47, 0.40],  # stone
]
ROOF = [0.085, 0.090, 0.100]
ROAD = [0.022, 0.022, 0.026]
KERB = [0.115, 0.110, 0.100]
GROUND = [0.040, 0.045, 0.040]
PARK = [0.055, 0.095, 0.060]
WINDOW_WARM = [1.0, 0.62, 0.26]
WINDOW_COOL = [0.42, 0.72, 1.0]
LAMP = [1.0, 0.55, 0.18]
NEON_A = [1.0, 0.16, 0.46]
NEON_B = [0.16, 0.82, 1.0]
GLASS = [0.09, 0.12, 0.16]


def box(size, bevel=0.06, subdiv=1):
    return {"kind": "box", "size": size, "subdivisions": subdiv, "bevel": bevel, "bevelSegments": 2}


def material(base, roughness=0.78, metallic=0.0, emissive=None, strength=0.0):
    m = {"baseColor": base, "roughness": roughness, "metallic": metallic}
    if emissive is not None:
        m["emissiveColor"] = emissive
        m["emissiveIntensity"] = strength
    return m


def node(name, proc, position=(0, 0, 0), rotation=(0, 0, 0)):
    return {"name": name, "kind": "procedural", "position": list(position),
            "rotation": list(rotation), "procedural": proc}


def single(source, mat, transform=None, variation=None, matvar=None, deformers=None):
    proc = {"source": source, "distribution": {"kind": "single"}, "material": mat}
    if transform:
        proc["sourceTransform"] = transform
    if variation:
        proc["variation"] = variation
    if matvar:
        proc["materialVariation"] = matvar
    if deformers:
        proc["deformers"] = deformers
    return proc


def linear(source, mat, start, end, count, variation=None, matvar=None, transform=None):
    proc = {"source": source,
            "distribution": {"kind": "linear", "count": count, "start": list(start),
                             "end": list(end), "orientAlong": False},
            "material": mat}
    if transform:
        proc["sourceTransform"] = transform
    if variation:
        proc["variation"] = variation
    if matvar:
        proc["materialVariation"] = matvar
    return proc


def grid(source, mat, counts, spacing, variation=None, matvar=None, transform=None):
    proc = {"source": source,
            "distribution": {"kind": "grid", "gridCount": list(counts), "gridSpacing": list(spacing)},
            "material": mat}
    if transform:
        proc["sourceTransform"] = transform
    if variation:
        proc["variation"] = variation
    if matvar:
        proc["materialVariation"] = matvar
    return proc


# ---- a terrace ---------------------------------------------------------------------------------


def terrace(nodes, name, rng, *, start, end, count, depth, height, spread, facade, storeys,
            face_z, window_colour=WINDOW_WARM, lit=0.62, roof=True):
    """One row of buildings, plus the bands of lit windows across their fronts.

    The whole row is three or four nodes, not `count` of them: the buildings are one linear
    distribution with per-instance scale and tint variation, and each storey's windows are one more.
    That is the difference between a city that is cheap to draw and a city that is a list of boxes.

    `face_z` is +1 when the fronts look towards +Z and -1 when they look the other way, which is
    what puts the windows on the side the street is on.
    """
    seed = rng.randrange(1, 1 << 30)
    body = box([depth if False else (abs(end[0] - start[0]) / max(count, 1)) * 0.78, height, depth],
               bevel=0.10)
    nodes.append(node(name, linear(
        body,
        material(facade, roughness=0.82),
        start, end, count,
        variation={"seed": seed,
                   "randomScale": [0.10, spread, 0.12],
                   "randomRotation": [0.0, 0.02, 0.0],
                   "randomPosition": [0.0, 0.0, 0.25]},
        matvar={"hueShift": 0.035, "valueRandom": 0.22},
        transform={"position": [0.0, height * 0.5, 0.0]})))

    if roof:
        # A parapet: a slightly wider, much shorter slab on top. Cheap, and it is what stops a
        # stylised building reading as an extruded rectangle.
        cap = box([(abs(end[0] - start[0]) / max(count, 1)) * 0.84, height * 0.045, depth * 1.06],
                  bevel=0.05)
        nodes.append(node(name + "-cap", linear(
            cap, material(ROOF, roughness=0.9), start, end, count,
            variation={"seed": seed, "randomScale": [0.10, spread, 0.12],
                       "randomRotation": [0.0, 0.02, 0.0], "randomPosition": [0.0, 0.0, 0.25]},
            transform={"position": [0.0, height, 0.0]})))

    # Window bands. One node per storey: a row of small emissive quads at that height, standing a
    # hair proud of the facade so they never z-fight it. `emissiveRandom` is what leaves some of
    # them dark, which is the whole reason a lit building reads as a building with people in it.
    for s in range(storeys):
        y = height * (0.22 + 0.68 * (s / max(storeys - 1, 1)))
        pitch = abs(end[0] - start[0]) / max(count * 3, 1)
        for side, sign in (("", face_z), ("b", -face_z)):
            nodes.append(node(f"{name}-win{side}{s}", linear(
                box([pitch * 0.42, height * 0.075, 0.06], bevel=0.01),
                material([0.02, 0.02, 0.02], roughness=0.35,
                         emissive=window_colour, strength=7.5 * lit * (1.0 if side == "" else 0.7)),
                [start[0], y, start[2] + sign * (depth * 0.5 + 0.05)],
                [end[0], y, end[2] + sign * (depth * 0.5 + 0.05)],
                count * 3,
                variation={"seed": seed + 17 + s + (0 if side == "" else 911)},
                matvar={"emissiveRandom": 1.0, "valueRandom": 0.15})))


def lamps(nodes, name, start, end, count, height=3.4, colour=LAMP, strength=16.0):
    """A run of street lamps: a post, a head, and a lit bulb. The bulb is the audio-reactive one."""
    nodes.append(node(name + "-post", linear(
        {"kind": "cylinder", "radius": 0.075, "height": height, "radialSegments": 8, "caps": True,
         "bevel": 0.02, "bevelSegments": 2},
        material([0.06, 0.065, 0.07], roughness=0.45, metallic=0.6),
        start, end, count, transform={"position": [0.0, height * 0.5, 0.0]})))
    nodes.append(node(name + "-head", linear(
        box([0.42, 0.10, 0.22], bevel=0.04),
        material([0.07, 0.075, 0.08], roughness=0.4, metallic=0.6),
        [start[0], height, start[2]], [end[0], height, end[2]], count)))
    nodes.append(node(name + "-bulb", linear(
        {"kind": "sphere", "radius": 0.11, "segments": 10, "rings": 6},
        material([0.02, 0.02, 0.02], roughness=0.3, emissive=colour, strength=strength),
        [start[0], height - 0.10, start[2]], [end[0], height - 0.10, end[2]], count)))


def trees(nodes, name, asset_dir, start, end, count, seed, scale=1.0):
    """Street trees, instanced from a CC0 model through the same procedural machinery as everything
    else: the asset says what a tree looks like, the distribution says where and how many."""
    for part, (tint, rough) in enumerate(((([0.24, 0.17, 0.11]), 0.9), (([0.14, 0.26, 0.14]), 0.85))):
        nodes.append(node(f"{name}-{part}", linear(
            {"kind": "mesh", "asset": f"{asset_dir}/CommonTree_2.gltf", "assetPart": part},
            material(tint, roughness=rough),
            start, end, count,
            variation={"seed": seed + part * 7, "randomUniformScale": 0.22,
                       "randomRotation": [0.0, 3.14, 0.0], "randomPosition": [0.4, 0.0, 0.4]},
            transform={"scale": [scale, scale, scale]},
            matvar={"hueShift": 0.02, "valueRandom": 0.18})))


# ---- the districts -----------------------------------------------------------------------------


def plaza_scene(rng, asset_dir):
    """The residential block. Low, warm, walkable: this is where the character is a person rather
    than a dot, so nothing here is taller than about four storeys."""
    nodes = []
    # Ground and the street. The street runs along X at z = 0; the character walks down it.
    nodes.append(node("ground", single(box([260, 1.2, 260], bevel=0.0), material(GROUND, 0.95)),
                      position=(0, -0.6, 0)))
    nodes.append(node("road", single(box([230, 0.16, 11.0], bevel=0.05), material(ROAD, 0.95)),
                      position=(0, 0.02, 0)))
    # A centre line, dashed, because a road with no markings reads as a strip of dark ground.
    nodes.append(node("road-line", linear(
        box([1.9, 0.02, 0.16], bevel=0.0), material([0.30, 0.28, 0.22], 0.9),
        [-62, 0.11, 0.0], [62, 0.11, 0.0], 22)))
    for side in (-1, 1):
        nodes.append(node(f"kerb{side}", single(
            box([230, 0.34, 3.4], bevel=0.06), material(KERB, 0.85)),
            position=(0, 0.12, side * 7.2)))

    # Two terraces facing each other across the street, and a back row behind each to give the
    # frame depth without anything new in the foreground.
    # Four runs along each side rather than one, with a gap between them for a side street and a
    # different base height and palette per run. A single span of seventeen identical footprints is
    # the failure mode of every procedural city: the variation has to be *structural* before the
    # per-instance jitter has anything to sit on.
    terrace(nodes, "north", rng, start=(-58, 0, 13.5), end=(-22, 0, 13.5), count=7, depth=9.0,
            height=11.5, spread=0.30, facade=FACADES[0], storeys=4, face_z=-1, lit=0.55)
    terrace(nodes, "north2", rng, start=(-10, 0, 13.5), end=(18, 0, 13.5), count=5, depth=9.0,
            height=8.2, spread=0.26, facade=FACADES[5], storeys=3, face_z=-1, lit=0.62)
    terrace(nodes, "north3", rng, start=(30, 0, 13.5), end=(66, 0, 13.5), count=5, depth=9.0,
            height=14.0, spread=0.34, facade=FACADES[2], storeys=5, face_z=-1, lit=0.5)
    terrace(nodes, "south", rng, start=(-58, 0, -13.5), end=(-26, 0, -13.5), count=6, depth=9.0,
            height=8.0, spread=0.26, facade=FACADES[1], storeys=3, face_z=+1, lit=0.6)
    terrace(nodes, "south2", rng, start=(-14, 0, -13.5), end=(8, 0, -13.5), count=4, depth=9.0,
            height=12.5, spread=0.30, facade=FACADES[4], storeys=4, face_z=+1, lit=0.5)
    terrace(nodes, "south3", rng, start=(20, 0, -13.5), end=(66, 0, -13.5), count=5, depth=9.0,
            height=9.5, spread=0.24, facade=FACADES[0], storeys=3, face_z=+1, lit=0.66)
    terrace(nodes, "north-back", rng, start=(-62, 0, 27.5), end=(66, 0, 27.5), count=9, depth=11.0,
            height=17.0, spread=0.40, facade=FACADES[2], storeys=5, face_z=-1, lit=0.42)
    terrace(nodes, "south-back", rng, start=(-62, 0, -27.5), end=(66, 0, -27.5), count=9,
            depth=11.0, height=15.0, spread=0.38, facade=FACADES[3], storeys=5, face_z=+1, lit=0.38)
    # A far skyline: too distant to read as buildings, close enough to stop the ground meeting the
    # sky in a straight line.
    terrace(nodes, "far-north", rng, start=(-96, 0, 48.0), end=(96, 0, 48.0), count=8, depth=16.0,
            height=26.0, spread=0.55, facade=FACADES[2], storeys=6, face_z=-1, lit=0.3)
    terrace(nodes, "far-south", rng, start=(-96, 0, -48.0), end=(96, 0, -48.0), count=8,
            depth=16.0, height=22.0, spread=0.5, facade=FACADES[4], storeys=5, face_z=+1, lit=0.28)

    # The park the character passes: a lawn, some trees and a bench-like slab.
    nodes.append(node("park", single(box([26, 0.22, 12], bevel=0.3), material(PARK, 0.95)),
                      position=(28, 0.08, -14.5)))
    trees(nodes, "park-trees", asset_dir, (18, 0.2, -14.5), (38, 0.2, -14.5), 5, seed=101, scale=0.8)
    trees(nodes, "street-trees-n", asset_dir, (-52, 0.2, 8.4), (56, 0.2, 8.4), 11, seed=211, scale=0.58)
    trees(nodes, "street-trees-s", asset_dir, (-48, 0.2, -8.4), (60, 0.2, -8.4), 11, seed=307, scale=0.54)

    lamps(nodes, "lamps-n", (-54, 0.3, 6.4), (58, 0.3, 6.4), 11)
    lamps(nodes, "lamps-s", (-50, 0.3, -6.4), (62, 0.3, -6.4), 11)

    # Parked cars: bodies and a roof slab, one node each, tinted per instance.
    nodes.append(node("cars", linear(
        box([3.9, 0.95, 1.75], bevel=0.32, subdiv=2), material([0.16, 0.16, 0.18], 0.35, 0.25),
        [-52, 0.62, 4.6], [58, 0.62, 4.6], 12,
        variation={"seed": 4242, "randomScale": [0.08, 0.06, 0.05], "randomPosition": [1.2, 0, 0.1]},
        matvar={"hueShift": 0.5, "valueRandom": 0.5})))
    nodes.append(node("cars-top", linear(
        box([2.1, 0.62, 1.58], bevel=0.26, subdiv=2), material([0.10, 0.12, 0.14], 0.2, 0.1),
        [-52.2, 1.35, 4.6], [57.8, 1.35, 4.6], 12,
        variation={"seed": 4242, "randomScale": [0.08, 0.06, 0.05], "randomPosition": [1.2, 0, 0.1]})))

    # The doorway the character walks to, at the end of the street. Lit, so the eye has somewhere
    # to arrive at before the character does.
    nodes.append(node("door-frame", single(box([2.6, 4.2, 0.5], bevel=0.08),
                                           material([0.42, 0.30, 0.22], 0.7)),
                      position=(56, 2.1, -8.6)))
    nodes.append(node("door-glow", single(
        box([1.8, 3.2, 0.16], bevel=0.05),
        material([0.02, 0.02, 0.02], 0.3, emissive=[1.0, 0.72, 0.34], strength=13.0)),
        position=(56, 1.75, -8.35)))
    nodes.append(node("door-sign", single(
        box([2.2, 0.34, 0.12], bevel=0.04),
        material([0.02, 0.02, 0.02], 0.3, emissive=NEON_A, strength=7.0)),
        position=(56, 4.6, -8.3)))

    return {"format": "avgen-scene", "version": 1, "name": "plaza", "nodes": nodes}


def downtown_scene(rng, asset_dir):
    """The tower cluster: taller, colder, and the only place in the piece with neon in it. Placed
    far from the plaza in world space so a cut between the two can never show both."""
    nodes = []
    # The two districts occupy the *same* ground. They are never both visible -- the cut is a Step
    # key on one boolean -- and putting them side by side instead would triple the scene's radius,
    # which is what the shadow cascades are fitted to: a city 600 metres wide spends every shadow
    # texel on the half of it nobody is looking at, and the streets come out flat.
    ox = 0.0
    nodes.append(node("ground", single(box([260, 1.2, 260], bevel=0.0), material([0.030, 0.034, 0.042], 0.95)),
                      position=(ox, -0.6, 0)))
    nodes.append(node("plaza-floor", single(box([58, 0.2, 44], bevel=0.4), material([0.030, 0.033, 0.040], 0.88)),
                      position=(ox, 0.06, 0)))
    nodes.append(node("avenue", single(box([240, 0.16, 14], bevel=0.05), material(ROAD, 0.6)),
                      position=(ox, 0.02, -40)))

    # Four ranks of towers stepping back from the plaza, each rank taller and dimmer than the one in
    # front: atmospheric perspective done with geometry rather than with fog alone.
    ranks = [
        dict(z=34.0, count=7, height=34.0, depth=13.0, facade=FACADES[2], storeys=7, lit=0.55),
        dict(z=56.0, count=6, height=52.0, depth=15.0, facade=FACADES[4], storeys=9, lit=0.45),
        dict(z=80.0, count=5, height=72.0, depth=17.0, facade=GLASS, storeys=11, lit=0.38),
        dict(z=-62.0, count=6, height=40.0, depth=14.0, facade=FACADES[5], storeys=8, lit=0.5),
    ]
    for i, r in enumerate(ranks):
        half = 9.0 * r["count"]
        terrace(nodes, f"tower{i}", rng,
                start=(ox - half, 0, r["z"]), end=(ox + half, 0, r["z"]), count=r["count"],
                depth=r["depth"], height=r["height"], spread=0.45, facade=r["facade"],
                storeys=r["storeys"], face_z=-1 if r["z"] > 0 else +1,
                window_colour=WINDOW_COOL if i == 2 else WINDOW_WARM, lit=r["lit"])

    # Neon: two vertical signs and a horizontal band. Restrained -- spec 49 is right that everything
    # pulsing at once destroys the shot, so these are the only saturated emitters and the bass
    # drives just one of them.
    nodes.append(node("neon-tall", single(
        box([0.9, 11.0, 0.35], bevel=0.05),
        material([0.02, 0.02, 0.02], 0.3, emissive=NEON_A, strength=20.0)),
        position=(ox - 26.0, 15.0, 26.0)))
    nodes.append(node("neon-band", single(
        box([16.0, 0.8, 0.3], bevel=0.05),
        material([0.02, 0.02, 0.02], 0.3, emissive=NEON_B, strength=16.0)),
        position=(ox + 20.0, 9.5, 26.5)))
    nodes.append(node("neon-ring", single(
        {"kind": "torus", "majorRadius": 3.1, "minorRadius": 0.22, "majorSegments": 40,
         "minorSegments": 8},
        material([0.02, 0.02, 0.02], 0.3, emissive=NEON_A, strength=14.0)),
        position=(ox + 2.0, 12.0, 25.5), rotation=(90, 0, 0)))

    lamps(nodes, "dt-lamps", (ox - 30, 0.3, 18.0), (ox + 30, 0.3, 18.0), 9, height=4.2, strength=7.0)
    lamps(nodes, "dt-lamps-w", (ox - 30, 0.3, -20.0), (ox + 30, 0.3, -20.0), 9, height=4.2, strength=7.0)
    trees(nodes, "dt-trees", asset_dir, (ox - 26, 0.2, 8.0), (ox + 26, 0.2, 8.0), 7, seed=515, scale=0.62)

    # A fountain-ish centrepiece for the camera to circle and for the character to arrive at.
    nodes.append(node("basin", single(
        {"kind": "cylinder", "radius": 4.2, "height": 0.7, "radialSegments": 28, "caps": True,
         "bevel": 0.18, "bevelSegments": 3},
        material([0.22, 0.23, 0.26], 0.7)), position=(ox, 0.4, 0)))
    nodes.append(node("basin-water", single(
        {"kind": "cylinder", "radius": 3.7, "height": 0.12, "radialSegments": 28, "caps": True},
        material([0.010, 0.018, 0.030], 0.10, 0.0, emissive=[0.08, 0.22, 0.40], strength=0.45)),
        position=(ox, 0.72, 0)))
    nodes.append(node("basin-column", single(
        {"kind": "cylinder", "radius": 0.42, "height": 3.6, "radialSegments": 16, "caps": True,
         "bevel": 0.08, "bevelSegments": 2},
        material([0.24, 0.25, 0.28], 0.6, 0.0, emissive=[0.9, 0.62, 0.3], strength=1.8)),
        position=(ox, 2.4, 0)))

    return {"format": "avgen-scene", "version": 1, "name": "downtown", "nodes": nodes}


def piece_scene(asset_dir, rig_path):
    """The top level: the two districts as scene nodes -- which is what makes a cut a Step key on
    one boolean -- plus the one node the character lives in."""
    return {
        "format": "avgen-scene",
        "version": 1,
        "name": "night-shift",
        "camera": {"mode": 1, "position": [-46.0, 26.0, 52.0], "target": [-10.0, 2.0, 0.0],
                   "fov": 26.0, "orbitSpeed": 0.0},
        "lightRig": rig_path,
        "environment": {
            "stylized": True,
            "shadowCascades": 4,
            "intensity": 0.085,
            "skyIntensity": 0.22,
            # Dusk: a blue-violet zenith over a warm horizon. The sequence keys these to night.
            "background": [0.030, 0.038, 0.062],
            "fogColor": [0.075, 0.070, 0.090],
            "fogHeight": 40.0,
            "fogHeightFalloff": 0.035,
            "volumeDensity": 0.0030,
            "volumeScattering": 0.45,
            "volumeAnisotropy": 0.42,
            "volumeSteps": 16,
            "volumeMaxDistance": 260.0,
            "styledSkyAmbient": [0.013, 0.019, 0.040],
            "styledGroundAmbient": [0.006, 0.0055, 0.005],
            "sky": {
                "zenithColor": [0.055, 0.080, 0.170],
                "horizonColor": [0.42, 0.26, 0.22],
                "groundColor": [0.020, 0.022, 0.026],
                "haze": 0.40,
                "sunColor": [1.0, 0.62, 0.36],
                "sunIntensity": 4.0,
                "sunSize": 1.4,
                "sunGlow": 0.8,
                "intensity": 0.9,
                "background": True
            }
        },
        "nodes": [
            {"name": "plaza", "kind": "scene", "asset": "plaza.scene.json"},
            {"name": "downtown", "kind": "scene", "asset": "downtown.scene.json", "visible": False},
            # The character. One node, moved by the sequence into whichever district is showing.
            {"name": "walker", "kind": "gltf", "asset": f"{asset_dir}/alien.gltf",
             "position": [-62.0, 0.0, -3.4], "rotation": [0.0, 90.0, 0.0],
             "scale": [0.0135, 0.0135, 0.0135],
             "animation": {"state": "Idle", "blend": 0.3, "updateHz": 0.0, "cullDistance": 0.0}},
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="examples/city")
    parser.add_argument("--seed", type=int, default=20260911)
    parser.add_argument("--assets", default="../../assets/quaternius/glTF")
    parser.add_argument("--imported", default="../../assets/imported")
    parser.add_argument("--rig", default="../lightrigs/night-shift.rig.json")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    rng = random.Random(args.seed)

    files = {
        "plaza.scene.json": plaza_scene(rng, args.assets),
        "downtown.scene.json": downtown_scene(rng, args.assets),
        "night-shift.scene.json": piece_scene(args.imported, args.rig),
    }
    for name, doc in files.items():
        path = os.path.join(args.out, name)
        with open(path, "w") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
        count = len(doc.get("nodes", []))
        print(f"{path}: {count} node(s)")


if __name__ == "__main__":
    main()
