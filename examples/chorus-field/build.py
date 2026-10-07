#!/usr/bin/env python3
"""CHORUS FIELD: generates the scene and project files of every look from one description.

Never edit the generated JSON; edit this file and run `python3 examples/chorus-field/build.py`.

The scene is a demonstration of ADR-1180/1181 (the Fiber source and the Streamline deformer): millions
of hair-thin metallic fibers whose centre lines are the streamlines of a few cheap vector fields. All
the structure is in the fields; nothing in the scene is modelled.
"""
import copy
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
AUDIO = "../../assets/audio/trench.wav"


# ---- fields ---------------------------------------------------------------------------------------

def field(name, kind, **kw):
    f = {"kind": kind, "falloff": kw.pop("falloff", {"kind": "none"})}
    pos = kw.pop("position", [0, 0, 0])
    f.update(kw)
    return {"name": name, "kind": "field", "position": pos, "field": f}


def smooth(inner, outer):
    return {"kind": "smoothstep", "inner": inner, "outer": outer}


# ---- the fiber layer ------------------------------------------------------------------------------

def fibers(name, *, grid=(64, 64, 64), tensions=None, tension=0.0, spacing=(0.5, 0.5, 0.5), centre=(0, 0, 0), rotate=(0, 0, 0), jitter=None,
           length=2.5, width=0.012, taper=0.3, segments=12, min_px=0.8, flow="flow", steer=4.0,
           stiffness=0.0, seed=11, material=None, effectors=(), material_variation=None, pulls=(),
           orient=(math.pi, math.pi, math.pi), size_random=0.4, cast_shadows=True):
    if jitter is None:
        jitter = [s * 0.5 for s in spacing]
    proc = {
        "source": {"kind": "fiber", "fiberLength": length, "fiberWidth": width, "fiberTaper": taper,
                   "fiberSegments": segments, "fiberMinPixels": min_px},
        "variation": {"seed": seed, "position": list(jitter), "rotation": list(orient),
                      "uniformScale": size_random},
        "distribution": {"kind": "grid", "gridCount": list(grid), "gridSpacing": list(spacing)},
        "distributionTransform": {"position": list(centre), "rotation": list(rotate), "scale": [1, 1, 1]},
        "deformers": [{"kind": "streamline", "space": "world", "field": flow, "amount": steer,
                       "falloff": stiffness, "tension": tension}] + [
            {"kind": "streamline", "space": "world", "field": f, "amount": a, "falloff": stiffness,
             "tension": (tensions or {}).get(f, 0.0)} for (f, a) in pulls],
        "effectors": list(effectors),
        "lod": {"cull": True, "maxDistance": 0.0, "count": 1},
        "material": material or metal(),
    }
    if material_variation:
        proc["materialVariation"] = material_variation
    return {"name": name, "kind": "procedural", "position": list(centre) if False else [0, 0, 0],
            "procedural": proc}


def metal(base=(0.62, 0.63, 0.66), rough=0.28, emissive=(0, 0, 0), ei=0.0):
    return {"baseColor": list(base), "roughness": rough, "metallic": 1.0,
            "emissiveColor": list(emissive), "emissiveIntensity": ei}


# ---- a look ---------------------------------------------------------------------------------------

def scene(name, nodes, *, cam_pos, cam_target, fov=40, lights=None, env=None, post=None, programs=None):
    sc = {
        "format": "avgen-scene", "version": 1, "name": name,
        "camera": {"mode": 1, "position": cam_pos, "target": cam_target, "fov": fov, "orbitSpeed": 0},
        "lights": lights if lights is not None else default_lights(),
        "environment": env if env is not None else default_env(),
        "post": post if post is not None else default_post(),
        "nodes": nodes,
    }
    if programs:
        sc["materialPrograms"] = programs
    return sc


def default_lights():
    return [
        {"name": "key", "type": "directional", "role": "key", "direction": [-0.45, -0.55, -0.7],
         "color": [1.0, 0.94, 0.86], "intensity": 6.0, "castsShadow": True, "shadowStrength": 0.92,
         "softness": 1.2},
        {"name": "rim", "type": "directional", "role": "rim", "direction": [0.55, -0.2, 0.8],
         "color": [0.55, 0.7, 1.0], "intensity": 4.0, "castsShadow": False, "shadowStrength": 0.0,
         "softness": 2.0},
        {"name": "under", "type": "directional", "role": "fill", "direction": [0.1, 0.9, 0.3],
         "color": [0.9, 0.55, 0.35], "intensity": 0.35, "castsShadow": False, "shadowStrength": 0.0,
         "softness": 3.0},
    ]


def default_env(zenith=(0.05, 0.055, 0.07), horizon=(0.16, 0.16, 0.17), ground=(0.012, 0.011, 0.012),
                intensity=0.6):
    return {
        "background": [0.004, 0.0045, 0.006],
        "intensity": intensity,
        "sky": {"enabled": True, "background": False, "useKeyLight": False, "sunDirection": [0.3, 0.6, -0.5],
                "zenithColor": list(zenith), "horizonColor": list(horizon), "groundColor": list(ground),
                "haze": 0.3, "sunColor": [1.0, 0.95, 0.9], "sunIntensity": 6.0, "sunSize": 0.02, "sunGlow": 0.05,
                "intensity": 1.0},
    }


def default_post():
    return {"tonemap": 1, "chromaRetention": 0.6, "bloomEnabled": True, "bloomIntensity": 0.08,
            "bloomThreshold": 1.2, "bloomKnee": 0.5, "bloomRadius": 1.0, "bloomLevels": 6,
            "bloomEmissionWeight": 0.75, "antialias": 0.85}


def project(name, scene_file, audio=AUDIO):
    return {
        "format": "avgen-project", "version": 4, "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": scene_file}, "audio": {"path": audio}},
        "_note": "Generated by build.py. The composition's four default audio routes are stopped (amount 0) "
                 "so nothing moves the whole world; the field does the moving.",
        "routes": [
            {"source": "audio.bass", "target": "root/scale", "op": "add", "amount": 0.0},
            {"source": "audio.mid", "target": "root/rotationSpeed", "op": "add", "amount": 0.0},
            {"source": "audio.rms", "target": "scene/brightness", "op": "add", "amount": 0.0},
            {"source": "audio.onset", "target": "root/impulse", "op": "add", "amount": 0.0},
        ],
    }


LOOKS = {}


def look(fn):
    LOOKS[fn.__name__] = fn
    return fn


# ---- Phase 1: the raw field ------------------------------------------------------------------------

@look
def p1_curl():
    """The smallest field: one curl noise. Fibers as streamlines of a divergence-free flow."""
    nodes = [
        field("flow", "curlNoise", frequency=0.07, strength=1.0, seed=3),
        fibers("fibers", grid=(64, 64, 64), spacing=(0.5, 0.5, 0.5)),
    ]
    return scene("Chorus Field p1 curl", nodes, cam_pos=[0, 4, 34], cam_target=[0, 0, 0])


@look
def p1_vortex_curl():
    """A large vortex threaded with curl: streams that wrap around one axis and break into eddies."""
    nodes = [
        field("vortex", "vortex", axis=[0.2, 1, 0.1], strength=1.0),
        field("curl", "curlNoise", frequency=0.09, strength=0.8, seed=5),
        field("flow", "compound", children=["vortex", "curl"], combine="add"),
        fibers("fibers", grid=(64, 64, 64), spacing=(0.5, 0.5, 0.5), length=3.5, steer=6.0),
    ]
    return scene("Chorus Field p1 vortex+curl", nodes, cam_pos=[6, 10, 30], cam_target=[0, 0, 0], fov=45)


@look
def p1_sheet():
    """A slab of roots combed by one direction and a slow curl: a sheet of metal hair, low camera, gunmetal."""
    nodes = [
        field("comb", "direction", axis=[1, 0.15, 0], strength=1.0),
        field("curl", "curlNoise", frequency=0.05, strength=1.4, seed=9),
        field("flow", "compound", children=["comb", "curl"], combine="add"),
        fibers("fibers", grid=(160, 6, 160), spacing=(0.3, 0.6, 0.3), length=6.0, width=0.01, segments=16,
               steer=3.0, material=metal(base=(0.32, 0.33, 0.36), rough=0.18)),
    ]
    return scene("Chorus Field p1 sheet", nodes, cam_pos=[-22, 3, 18], cam_target=[4, 0, -2], fov=38)


# ---- Phase 2/3 building blocks -------------------------------------------------------------------

def thin(name, fieldname, strength=1.0):
    """A Scale effector that multiplies fiber size by a scalar field: where it reads 0 there is no fiber."""
    return {"field": fieldname, "op": "scale", "blend": "multiply", "strength": strength, "scaleAxis": [1, 1, 1]}


@look
def p2_mask():
    """Emergence: a curtain falling from above the frame, parted by two counter-rotating eddies and
    gathered at a chin. Nothing is a face; the wakes behind the eddies are."""
    return mask_scene("Chorus Field p2 mask")


def mask_scene(title, *, eye_spin=1.0, eye_y=2.5, eye_x=4.6, chin=0.8, curl=0.25, cam=(0, 0, 44),
               target=(0, -1, 0), fov=45, material=None, count_x=220, depth=24, length=36.0, extra=()):
    nodes = [
        field("fall", "direction", axis=[0, -1, 0], strength=1.0),
        field("eyeL", "vortex", position=[-eye_x, eye_y, 0], axis=[0, 0, -eye_spin], strength=2.6,
              falloff=smooth(1.2, 6.0)),
        field("eyeR", "vortex", position=[eye_x, eye_y, 0], axis=[0, 0, eye_spin], strength=2.6,
              falloff=smooth(1.2, 6.0)),
        field("chin", "attractor", position=[0, -18, 0], strength=chin, falloff=smooth(6, 26)),
        field("curl", "curlNoise", frequency=0.11, strength=curl, seed=21),
        field("flow", "compound", children=["fall", "eyeL", "eyeR", "chin"], combine="add"),
        # The curtain's edge is torn, not cut: an ellipse of fertility, its rim eaten by noise.
        field("crown", "sphere", position=[0, 19, 0], radius=12.0, softness=6.0, scale=[1.0, 1.0, 1.0],
              falloff={"kind": "noiseModulated", "inner": 6, "outer": 18, "noiseAmount": 2.5, "noiseScale": 0.25}),
        fibers("fibers", grid=(count_x, 1, depth), spacing=(30.0 / count_x, 1, 0.25), centre=(0, 19, 0),
               rotate=(0, 0, 180), jitter=(15.0 / count_x, 0.6, 0.12), length=length, width=0.008, segments=32,
               steer=6.0, orient=(0.05, 3.14, 0.05), size_random=0.2, min_px=0.7,
               effectors=[thin("crown", "crown")], material=material, pulls=[("curl", 6.0)]),
    ]
    nodes[0:0] = list(extra)
    return scene(title, nodes, cam_pos=list(cam), cam_target=list(target), fov=fov)


@look
def p2_flow():
    """Flow: a wall of roots releases one enormous stream that bends around three invisible bodies."""
    nodes = [
        field("wind", "direction", axis=[1, 0.05, 0], strength=1.0),
        field("bodyA", "repulsor", position=[-6, 3, -2], strength=1.6, falloff=smooth(3, 9)),
        field("bodyB", "repulsor", position=[10, -4, 3], strength=1.4, falloff=smooth(2, 8)),
        field("bodyC", "repulsor", position=[22, 6, -4], strength=1.8, falloff=smooth(4, 11)),
        field("flow", "compound", children=["wind", "bodyA", "bodyB", "bodyC"], combine="add"),
        field("curl", "curlNoise", frequency=0.05, strength=1.0, seed=4),
        field("band", "box", position=[-26, 0, 0], size=[1, 9, 9], softness=4.0,
              falloff={"kind": "noiseModulated", "inner": 2, "outer": 30, "noiseAmount": 2.0, "noiseScale": 0.2}),
        fibers("fibers", grid=(4, 110, 110), spacing=(0.5, 0.22, 0.22), centre=(-26, 0, 0), rotate=(0, 0, -90),
               jitter=(0.4, 0.11, 0.11), length=60.0, width=0.008, segments=32, steer=5.0,
               orient=(0.05, 0.05, 0.05), size_random=0.25, effectors=[thin("band", "band")],
               pulls=[("curl", 0.6)]),
    ]
    return scene("Chorus Field p2 flow", nodes, cam_pos=[-34, -6, 26], cam_target=[6, 1, -2], fov=50)


@look
def p2_void():
    """Void: dense streams orbit three enormous empty spheres."""
    nodes = [
        field("wind", "direction", axis=[0, -1, 0.15], strength=0.8),
        field("voidA", "vortex", position=[-7, 4, 0], axis=[0.3, 0.2, 1], strength=2.4, falloff=smooth(5, 11)),
        field("voidB", "vortex", position=[8, -3, -3], axis=[-0.2, 0.4, -1], strength=2.2, falloff=smooth(4, 10)),
        field("voidC", "vortex", position=[-1, -13, 2], axis=[1, 0.1, 0.3], strength=2.0, falloff=smooth(3, 8)),
        field("flow", "compound", children=["wind", "voidA", "voidB", "voidC"], combine="add"),
        field("pushA", "repulsor", position=[-7, 4, 0], strength=2.0, falloff=smooth(5, 6.5)),
        field("pushB", "repulsor", position=[8, -3, -3], strength=2.0, falloff=smooth(4, 5.5)),
        field("push", "compound", children=["pushA", "pushB"], combine="add"),
        field("holeA", "sphere", position=[-7, 4, 0], radius=5.5, softness=0.6, invert=True),
        field("holeB", "sphere", position=[8, -3, -3], radius=4.5, softness=0.6, invert=True),
        fibers("fibers", grid=(110, 22, 110), spacing=(0.36, 1.6, 0.36), centre=(0, 0, 0),
               jitter=(0.18, 0.8, 0.18), length=14.0, width=0.008, segments=24, steer=5.0,
               orient=(0.3, 3.14, 0.3), size_random=0.3,
               effectors=[thin("ha", "holeA"), thin("hb", "holeB")], pulls=[("push", 4.0)]),
    ]
    return scene("Chorus Field p2 void", nodes, cam_pos=[30, 10, 80], cam_target=[0, -2, 0], fov=40)


@look
def p2_choir():
    """Choir: sparse roots on the ground rise as columns, gathered by attractors at height, braided by curl."""
    nodes = [
        field("rise", "direction", axis=[0, 1, 0], strength=1.0),
        field("gatherA", "attractor", position=[-9, 14, -4], strength=0.9, falloff=smooth(2, 12)),
        field("gatherB", "attractor", position=[2, 20, 2], strength=0.9, falloff=smooth(2, 13)),
        field("gatherC", "attractor", position=[11, 12, -2], strength=0.9, falloff=smooth(2, 11)),
        field("flow", "compound", children=["rise", "gatherA", "gatherB", "gatherC"], combine="add"),
        field("braid", "vortex", position=[0, 0, 0], axis=[0, 1, 0], strength=0.5),
        field("curl", "curlNoise", frequency=0.08, strength=1.0, seed=12),
        field("ground", "constant", falloff={"kind": "noiseModulated", "inner": 30, "outer": 40,
                                             "noiseAmount": 3.0, "noiseScale": 0.12}),
        fibers("fibers", grid=(150, 1, 100), spacing=(0.24, 1, 0.24), centre=(0, -12, 0),
               jitter=(0.12, 0, 0.12), length=40.0, width=0.008, segments=32, steer=4.0,
               orient=(0.1, 3.14, 0.1), size_random=0.3, effectors=[thin("g", "ground")],
               pulls=[("braid", 2.0), ("curl", 1.2)]),
    ]
    return scene("Chorus Field p2 choir", nodes, cam_pos=[-20, -14, 78], cam_target=[0, 8, 0], fov=46)


@look
def p2_mask_calm():
    """The mask with the curl turned down: the face from long streams instead of glitter."""
    return mask_scene("Chorus Field p2 mask calm", curl=0.06, cam=(0, -3, 46), target=(0, -3, 0), fov=40)


@look
def p2_mask_deep():
    """The mask with depth: the curtain is 12 m thick, so the eyes are tunnels; seen three-quarters."""
    return mask_scene("Chorus Field p2 mask deep", curl=0.06, depth=48, cam=(-26, -3, 38), target=(0, -1, 0),
                      fov=42)


@look
def p2_mask_far():
    """The mask pulled back: the whole entity, with dark around it."""
    return mask_scene("Chorus Field p2 mask far", curl=0.06, depth=48, cam=(0, -2, 62), target=(0, -2, 0), fov=40)


@look
def p3_roll():
    """Vortex as a personality: a released sheet is rolled into a breaking curl by one great horizontal
    vortex tube; the camera looks down the tube from inside its mouth."""
    nodes = [
        field("wind", "direction", axis=[1, 0, 0], strength=0.45),
        field("roll", "spiral", position=[2, 2, 0], axis=[0.15, 0, -1], spiralBias=-0.35, strength=3.0,
              falloff=smooth(9, 22)),
        field("roll2", "vortex", position=[20, 4, 0], axis=[0, 0.3, -1], strength=2.0, falloff=smooth(2, 8)),
        field("flow", "compound", children=["wind", "roll", "roll2"], combine="add"),
        field("curl", "curlNoise", frequency=0.06, strength=1.0, seed=8),
        field("band", "box", position=[-24, 6, 0], size=[1, 3, 20], softness=2.5,
              falloff={"kind": "noiseModulated", "inner": 2, "outer": 40, "noiseAmount": 2.0, "noiseScale": 0.15}),
        fibers("fibers", grid=(3, 50, 220), spacing=(0.5, 0.16, 0.2), centre=(-24, 6, 0), rotate=(0, 0, -90),
               jitter=(0.4, 0.08, 0.1), length=70.0, width=0.008, segments=32, steer=5.0,
               orient=(0.05, 0.05, 0.05), size_random=0.3, effectors=[thin("band", "band")],
               pulls=[("curl", 0.5)]),
    ]
    lights = default_lights()
    lights[2].update({"direction": [-0.2, 0.75, -0.6], "intensity": 3.0, "color": [0.75, 0.82, 1.0]})
    return scene("Chorus Field p3 roll", nodes, cam_pos=[4, -2, 46], cam_target=[3, 1, 0], fov=50, lights=lights)


@look
def p3_roll_inside():
    """The same roll from inside the tube, looking along its axis."""
    sc = p3_roll()
    sc["camera"].update({"position": [1, -1, 26], "target": [3, 1, -10], "fov": 70})
    sc["name"] = "Chorus Field p3 roll inside"
    return sc


@look
def p4_tendons():
    """Macrostructure: two diffuse clouds of roots drawn to a throat between them, so their streams
    gather into tendons -- thick at the clouds, cabled where they meet."""
    nodes = [
        field("throatA", "attractor", position=[0, 2, 0], strength=1.0, falloff=smooth(4, 40)),
        field("throatB", "attractor", position=[0, -6, 0], strength=0.6, falloff=smooth(3, 30)),
        field("twist", "vortex", position=[0, 0, 0], axis=[1, 0.1, 0], strength=0.5, falloff=smooth(2, 18)),
        field("flow", "compound", children=["throatA", "throatB", "twist"], combine="add"),
        field("curl", "curlNoise", frequency=0.05, strength=1.0, seed=31),
        field("cloudL", "sphere", position=[-20, 4, 0], radius=7.0, softness=5.0,
              falloff={"kind": "noiseModulated", "inner": 4, "outer": 14, "noiseAmount": 2.5, "noiseScale": 0.2}),
        field("cloudR", "sphere", position=[20, 2, 0], radius=7.0, softness=5.0,
              falloff={"kind": "noiseModulated", "inner": 4, "outer": 14, "noiseAmount": 2.5, "noiseScale": 0.2}),
        field("clouds", "compound", children=["cloudL", "cloudR"], combine="max"),
        fibers("fibers", grid=(120, 40, 40), spacing=(0.42, 0.42, 0.42), jitter=(0.21, 0.21, 0.21),
               length=24.0, width=0.008, segments=32, steer=4.0, size_random=0.3,
               effectors=[thin("c", "clouds")], pulls=[("curl", 0.8)]),
    ]
    return scene("Chorus Field p4 tendons", nodes, cam_pos=[6, 4, 58], cam_target=[0, 0, 0], fov=45)


# ---- Phase 3: entity emergence ---------------------------------------------------------------------

def entity_scene(title, *, eyes=True, eye_x=4.6, eye_y=2.5, eye_spin=1.0, eye_r=(1.2, 6.0), eye_strength=2.6,
                 chin=0.8, chin_y=-18.0, horns=0.0, horn_y=11.0, horn_x=9.0, mouth=0.0, mouth_y=-6.5,
                 brow=0.0, curl=0.06, cam=(0, -3, 46), target=(0, -3, 0), fov=40, material=None,
                 count_x=220, depth=24, length=36.0, width=0.008, top=19.0, lights=None, env=None, post=None,
                 spread=30.0, jitter=1.0, taper=0.3, size_random=0.2, extra_nodes=(), extra_effectors=(),
                 mvar=None, segments=32, tube=1.0):
    """The curtain entity, with every feature optional. Each feature is a pull (its own streamline
    deformer), so audio can later weight one feature of the face without touching the others."""
    nodes = [
        field("fall", "direction", axis=[0, -1, 0], strength=1.0),
        field("chin", "attractor", position=[0, chin_y, 0], strength=chin, falloff=smooth(6, 26)),
        field("curl", "curlNoise", frequency=0.11, strength=1.0, seed=21),
    ]
    pulls = [("chin", 1.0), ("curl", curl)]
    if eyes:
        nodes += [
            field("eyeL", "vortex", position=[-eye_x, eye_y, 0], axis=[0, 0, -eye_spin], strength=eye_strength,
                  falloff=smooth(*eye_r), scale=[1, 1, tube]),
            field("eyeR", "vortex", position=[eye_x, eye_y, 0], axis=[0, 0, eye_spin], strength=eye_strength,
                  falloff=smooth(*eye_r), scale=[1, 1, tube]),
            field("eyes", "compound", children=["eyeL", "eyeR"], combine="add"),
        ]
        pulls.append(("eyes", 1.0))
    if horns:
        # Temples: counter-rotating eddies that throw the outer streams up and out before they fall.
        nodes += [
            field("hornL", "vortex", position=[-horn_x, horn_y, 0], axis=[0, 0, 1], strength=2.0,
                  falloff=smooth(2, 9), scale=[1, 1, tube]),
            field("hornR", "vortex", position=[horn_x, horn_y, 0], axis=[0, 0, -1], strength=2.0,
                  falloff=smooth(2, 9), scale=[1, 1, tube]),
            field("horns", "compound", children=["hornL", "hornR"], combine="add"),
        ]
        pulls.append(("horns", horns))
    if mouth:
        # A slot: two eddies side by side whose shared wake is a horizontal void.
        nodes += [
            field("mouthL", "vortex", position=[-2.2, mouth_y, 0], axis=[0, 0, -1], strength=2.0,
                  falloff=smooth(0.8, 3.2)),
            field("mouthR", "vortex", position=[2.2, mouth_y, 0], axis=[0, 0, 1], strength=2.0,
                  falloff=smooth(0.8, 3.2)),
            field("mouth", "compound", children=["mouthL", "mouthR"], combine="add"),
        ]
        pulls.append(("mouth", mouth))
    if brow:
        nodes.append(field("brow", "attractor", position=[0, eye_y + 4.5, 2.5], strength=1.0,
                           falloff=smooth(2, 8)))
        pulls.append(("brow", brow))
    nodes += [
        field("crown", "sphere", position=[0, top, 0], radius=spread * 0.4, softness=6.0,
              falloff={"kind": "noiseModulated", "inner": 6, "outer": 18, "noiseAmount": 2.5, "noiseScale": 0.25}),
        fibers("fibers", grid=(count_x, 1, depth), spacing=(spread / count_x, 1, 0.25), centre=(0, top, 0),
               rotate=(0, 0, 180), jitter=(jitter * spread / count_x / 2, 0.6 * jitter, 0.12 * jitter),
               length=length, width=width, taper=taper, segments=segments, flow="fall", steer=6.0,
               orient=(0.05 * jitter, 3.14 * jitter, 0.05 * jitter), size_random=size_random, min_px=0.7,
               effectors=[thin("crown", "crown")] + list(extra_effectors), material=material,
               material_variation=mvar, pulls=[(f, 6.0 * a) for f, a in pulls]),
    ]
    nodes[-1:-1] = list(extra_nodes)
    return scene(title, nodes, cam_pos=list(cam), cam_target=list(target), fov=fov, lights=lights, env=env,
                 post=post)


ENTITY_VARIANTS = {
    "e1-base": {},
    "e2-horned": {"horns": 0.8},
    "e3-mouth": {"mouth": 0.7},
    "e4-horned-mouth": {"horns": 0.8, "mouth": 0.7, "brow": 0.4},
    "e5-wide-eyes": {"eye_x": 6.0, "eye_r": (2.0, 8.0), "eye_strength": 3.2, "chin": 1.2},
    "e6-low": {"horns": 0.8, "mouth": 0.7, "cam": (0, -24, 34), "target": (0, 0, 0), "fov": 50},
    "e7-storm-hair": {"horns": 0.8, "mouth": 0.7, "brow": 0.4, "curl": 0.36},
    "e8-wild": {"horns": 0.8, "mouth": 0.7, "brow": 0.4, "curl": 0.2, "eye_strength": 3.6, "chin": 1.4},
}

for _name, _kw in ENTITY_VARIANTS.items():
    def _make(_kw=_kw, _name=_name):
        return entity_scene("Chorus Field p3 " + _name, **_kw)
    _make.__name__ = "p3_" + _name.replace("-", "_")
    LOOKS[_make.__name__] = _make


@look
def dbg_roll():
    sc = p3_roll()
    for n in sc["nodes"]:
        if n["kind"] == "procedural":
            pr = n["procedural"]
            pr["distribution"]["gridCount"] = [1, 6, 5]
            pr["distribution"]["gridSpacing"] = [1, 1.5, 8]
            pr["effectors"] = []
            pr["source"]["fiberWidth"] = 0.15
            pr["material"] = metal(emissive=(1, 0.6, 0.3), ei=3.0)
    sc["camera"].update({"position": [0, 0, 80], "target": [0, 0, 0], "fov": 60})
    return sc


# ---- Phase 4: material ----------------------------------------------------------------------------

FACE = {"horns": 0.8, "mouth": 0.7, "brow": 0.4}


def film(base, rough, thickness, ior=1.6, emissive=(0, 0, 0), ei=0.0):
    m = metal(base=base, rough=rough, emissive=emissive, ei=ei)
    m["thinFilm"] = {"thickness": thickness, "ior": ior}
    return m


def oxide_regions():
    """Field-driven material regions: a slow noise of colour, silver against oxidised bronze-verdigris."""
    return [field("oxide", "noiseColor", frequency=0.09, seed=17, colorA=[0.30, 0.34, 0.40, 1],
                  colorB=[0.42, 0.26, 0.13, 1])], [
        {"field": "oxide", "op": "color", "blend": "add", "strength": 1.0}]


MATERIALS = {
    "m1-oilslick": dict(material=film((0.30, 0.30, 0.32), 0.22, 420.0, 1.7)),
    "m2-blackchrome": dict(material=metal(base=(0.10, 0.10, 0.11), rough=0.12)),
    "m3-engraved": dict(material=film((0.55, 0.52, 0.48), 0.25, 280.0, 1.45), jitter=0.0, taper=1.0,
                        size_random=0.0, width=0.006),
    "m4-veins": dict(material=metal(base=(0.20, 0.20, 0.22), rough=0.2, emissive=(1.0, 0.42, 0.16), ei=6.0),
                     mvar={"emissiveSparsity": 0.965, "emissiveRandom": 0.6}),
    "m5-oxide": dict(material=film((1, 1, 1), 0.3, 340.0, 1.5)),
    "m6-gunmetal-oxide": dict(material=metal(base=(1, 1, 1), rough=0.22)),
    "m7-chrome-split": dict(material=metal(base=(0.14, 0.14, 0.15), rough=0.14, emissive=(1.0, 0.36, 0.12), ei=5.0),
                            mvar={"emissiveSparsity": 0.98, "emissiveRandom": 0.5}, lights="split"),
    "m8-chrome-split-film": dict(material=film((0.16, 0.16, 0.17), 0.14, 190.0, 1.5, emissive=(1.0, 0.36, 0.12),
                                               ei=5.0),
                                 mvar={"emissiveSparsity": 0.98, "emissiveRandom": 0.5}, lights="split",
                                 oxide=True),
    "m9-studio-sky": dict(material=metal(base=(0.5, 0.5, 0.52), rough=0.16, emissive=(1.0, 0.36, 0.12), ei=5.0),
                          mvar={"emissiveSparsity": 0.985, "emissiveRandom": 0.5}, lights="split", env="spectral"),
    "m10-studio-film": dict(material=film((0.45, 0.45, 0.47), 0.16, 230.0, 1.45), lights="split", env="spectral"),
}


def spectral_env():
    """Colour from reflection: a sky the fibers mirror, deep blue overhead, ember at the horizon,
    violet below. The background stays black; only the metal sees it."""
    return default_env(zenith=(0.03, 0.08, 0.30), horizon=(0.75, 0.38, 0.12), ground=(0.16, 0.04, 0.22),
                       intensity=1.4)


def split_lights():
    """Colour from light, not from the fibers: a warm key and a cold, strong rim from behind."""
    return [
        {"name": "key", "type": "directional", "role": "key", "direction": [-0.5, -0.45, -0.75],
         "color": [1.0, 0.82, 0.62], "intensity": 6.0, "castsShadow": True, "shadowStrength": 0.92,
         "softness": 1.2},
        {"name": "rim", "type": "directional", "role": "rim", "direction": [0.45, -0.25, 0.85],
         "color": [0.45, 0.68, 1.0], "intensity": 7.0, "castsShadow": False, "shadowStrength": 0.0,
         "softness": 2.0},
        {"name": "under", "type": "directional", "role": "fill", "direction": [0.1, 0.9, 0.3],
         "color": [0.55, 0.3, 0.75], "intensity": 0.8, "castsShadow": False, "shadowStrength": 0.0,
         "softness": 3.0},
    ]

for _name, _kw in MATERIALS.items():
    def _make(_kw=_kw, _name=_name):
        kw = dict(FACE)
        kw.update(_kw)
        if kw.pop("lights", None) == "split":
            kw["lights"] = split_lights()
        if kw.pop("env", None) == "spectral":
            kw["env"] = spectral_env()
        if kw.pop("oxide", False) or _name in ("m5-oxide", "m6-gunmetal-oxide"):
            nodes, effs = oxide_regions()
            kw["extra_nodes"], kw["extra_effectors"] = nodes, effs
        return entity_scene("Chorus Field p4 " + _name, **kw)
    _make.__name__ = "p4_" + _name.replace("-", "_")
    LOOKS[_make.__name__] = _make


# ---- Phase 4: depth -------------------------------------------------------------------------------

def depth_layers(curl_field="curl"):
    """A deep storm far behind (an enormous distant structure) and a few strands crossing the lens."""
    back = fibers("deep", grid=(160, 1, 4), spacing=(1.6, 1, 6.0), centre=(-10, 70, -150), rotate=(0, 0, 180),
                  jitter=(0.8, 3.0, 3.0), length=180.0, width=0.05, segments=24, flow="fall", steer=4.0,
                  orient=(0.1, 3.14, 0.1), size_random=0.3, min_px=0.6, seed=77,
                  material=metal(base=(0.07, 0.075, 0.09), rough=0.35), pulls=[("deepswirl", 2.5)])
    front = fibers("near", grid=(14, 1, 3), spacing=(3.0, 1, 2.5), centre=(6, 30, 26), rotate=(0, 0, 180),
                   jitter=(1.5, 2.0, 1.2), length=70.0, width=0.03, segments=32, flow="fall", steer=4.0,
                   orient=(0.1, 3.14, 0.1), size_random=0.2, min_px=0.8, seed=78,
                   material=metal(base=(0.5, 0.5, 0.52), rough=0.16), pulls=[(curl_field, 0.9)])
    return [front]  # the deep storm read as rain behind the entity, killing its negative space; kept out


@look
def p4_depth():
    """The entity in a world: m9's material, the curtain 12 m deep (the eddies are tubes through it), a
    storm far behind and strands across the lens; seen three-quarters."""
    kw = dict(FACE)
    kw.update(MATERIALS["m9-studio-sky"])
    kw.pop("lights"), kw.pop("env")
    deep = field("deepswirl", "spiral", position=[30, -10, -150], axis=[0.2, 0.1, 1], spiralBias=-0.2,
                 strength=1.0, falloff=smooth(30, 120))
    return entity_scene("Chorus Field p4 depth", **kw, lights=split_lights(), env=spectral_env(), depth=48,
                        tube=2.5, extra_nodes=[deep] + depth_layers(), cam=(-16, -4, 44), target=(0, -3, 0),
                        fov=44)


@look
def p4_depth_front():
    sc = p4_depth()
    sc["camera"].update({"position": [0, -3, 50], "target": [0, -3, 0], "fov": 42})
    return sc


# ---- Phase 5: audio -- topology first ---------------------------------------------------------------

def route(source, target, amount, op="add", component=None, **chain):
    r = {"source": source, "target": target, "op": op, "amount": amount}
    if component is not None:
        r["component"] = component
    if chain:
        r["chain"] = chain
    return r


@look
def p5_chorus():
    """The entity on the music. Topology first:
      bass      -> the face itself: the eye and temple eddies and the chin's pull. With no bass (the
                   breakdowns) there is no face, only a curtain; the drop pulls it out of the curtain.
      kick      -> a shockwave: a radial push riding the low onset fronts outward from the face (8 m/s).
      snare     -> tearing: the curl pull bursts and decays.
      mid       -> coherence: it calms the curl, so a full mid range holds the structure together.
      treble    -> microstructure: a fine, fast curl shimmers every filament.
    Appearance second: the threads that glow each hear their own band (spectrum, element), and the film's
    thickness drifts with the treble."""
    kw = {"horns": 0.8}  # the mouth and brow are left out: 16 GPU field slots and 8 deformers are the budget
    kw.update(MATERIALS["m9-studio-sky"])
    kw.pop("lights"), kw.pop("env")
    sc = entity_scene("Chorus Field p5 chorus", **kw, lights=split_lights(), env=spectral_env())
    nodes = sc["nodes"]
    nodes[-1:-1] = [
        field("kickPush", "radialVector", position=[0, 1, 0], strength=1.0),
        field("kickFront", "onset", position=[0, 1, 0], onsetSource="low", audioSpeed=9.0, onsetWidth=3.0,
              onsetDecay=1.6, waveGeometry="spherical", axis=[1, 1, 1], strength=1.0),
        field("kick", "compound", children=["kickPush", "kickFront"], combine="multiply"),
        field("shimmer", "curlNoise", frequency=0.6, speed=2.0, strength=1.0, seed=5),
        field("bands", "spectrum", audioBand="element", bandLow=0.05, bandHigh=0.95, audioSpeed=12.0,
              position=[0, 1, 0], waveGeometry="spherical", strength=1.0),
    ]
    fib = nodes[-1]["procedural"]
    # Deformer order: fall, chin, curl, eyes, horns (entity_scene), then kick, shimmer.
    fib["deformers"] += [
        {"kind": "streamline", "space": "world", "field": "kick", "amount": 0.0},
        {"kind": "streamline", "space": "world", "field": "shimmer", "amount": 0.0},
    ]
    fib["effectors"].append({"field": "bands", "op": "emission", "blend": "add", "strength": 3.0})
    d = "procedural/fibers/deform/"
    sc["_routes"] = [
        # bass -> topology: the face exists in proportion to the low end (smoothed, so it breathes).
        route("audio.bass", d + "4/amount", 1.0, op="multiply", attackMs=60, decayMs=900, gain=2.2,
              clampEnabled=True, clampMin=0.0, clampMax=1.3),
        route("audio.bass", d + "5/amount", 1.0, op="multiply", attackMs=60, decayMs=1200, gain=2.2,
              clampEnabled=True, clampMin=0.0, clampMax=1.3),
        route("audio.bass", d + "2/amount", 1.0, op="multiply", attackMs=200, decayMs=1500, gain=2.0,
              clampEnabled=True, clampMin=0.2, clampMax=1.4),
        # kick -> a travelling shockwave through the field.
        route("audio.onsetLow", d + "6/amount", 14.0, op="add", envelope="peakhold", envelopeHoldMs=60,
              envelopeFallPerSecond=3.0),
        # snare -> tearing: a short burst of curl.
        route("audio.onsetMid", d + "3/amount", 1.4, op="add", envelope="peakhold", envelopeHoldMs=40,
              envelopeFallPerSecond=6.0),
        # mid -> coherence (calms the curl).
        route("audio.mid", d + "3/amount", -0.12, op="add", attackMs=100, decayMs=600),
        # treble -> microstructure: a fine curl that only ever shivers the filaments.
        route("audio.treble", d + "7/amount", 0.5, op="add", attackMs=20, decayMs=200),
        # appearance second: the glowing threads brighten with the highs.
        route("audio.treble", "procedural/fibers/material/emissive", 6.0, op="add", attackMs=30, decayMs=300),
    ]
    return sc


# ---- Phase 4 (directed): the god in the field -----------------------------------------------------------
#
# The owner's direction after CP5: the entity is the heart; kill "hair"; make it a volume; give it scale;
# transformation is the show. The curtain no longer starts at a cut: it is gathered out of a wide ceiling
# of flow by a funnel, so above the face there are converging streams, not an edge.

def wire_program():
    """Field-driven metal, per fragment. The eddies' weight is the shear: bright silver ridges where they
    turn the streams (brow, cheekbones, temple curls), gunmetal where the streams only fall, an oxide
    undertone where nothing shears them -- the shadow tone, not the overall tone. A thin ember line on the
    fibers that skirt a socket: the eddy weight just short of full. The eye fields' STRENGTH is what audio
    routes, so when an eye weakens its rim band falls below the line and the glow goes with the face."""
    return {
        "name": "chorusWire",
        "ops": [
            # The rim (emission, r7): the eyes' weight just short of full.
            # (A vector field reads as its vector; a compound reads as the sum of its children's weights,
            # which is the scalar wanted here -- hence the program-only "eyes" compound.)
            {"kind": "field", "dst": 0, "field": "eyes"},
            {"kind": "smoothstep", "dst": 6, "srcA": 0, "constant": [1.9, 2.4, 0, 0]},
            {"kind": "remap", "dst": 7, "srcA": 0, "constant": [2.52, 2.58, 1.0, 0.0], "value": 1.0},
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
            {"kind": "constant", "dst": 7, "constant": [1.0, 0.78, 0.5, 1.0]},
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 6},
            # The metal (r4) and its roughness (r5): the shear of the whole face.
            {"kind": "field", "dst": 2, "field": "face"},
            {"kind": "smoothstep", "dst": 3, "srcA": 2, "constant": [0.08, 1.5, 0, 0]},
            {"kind": "ramp", "dst": 4, "srcA": 3, "constant": [0.40, 0.35, 0.30, 1.0],
             "constant2": [0.64, 0.65, 0.68, 1.0], "constant3": [0.95, 0.95, 0.97, 1.0]},
            {"kind": "remap", "dst": 5, "srcA": 3, "constant": [0.0, 1.0, 0.34, 0.13], "value": 1.0},
            # The god emerges from the dark: below the mouth the falling streams fade towards a quarter.
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "gradient", "dst": 1, "srcA": 0, "value": 0.1, "constant": [0.0, 1.0, 0.0, 1.0]},
            {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, 0.22, 1.0], "value": 1.0},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 1},
            {"kind": "constant", "dst": 1, "constant": [1.0, 1.0, 1.0, 1.0]},
        ],
        "baseColor": 4, "metallic": 1, "roughness": 5, "emission": 7, "emissionIntensity": 0.45,
    }


def god_scene(title, *, eyes=2.6, horns=1.6, mouth=1.4, maw=0.0, chin=1.0, curl=0.06, kick=0.0,
              cam=(0, -3, 46), target=(0, -3, 0), fov=40, debris=True, tension=0.3, segments=48,
              length=33.0, count_x=260, depth=24, width=0.006, top=21.0, ragged=6.0, film=0.0,
              lights=None, env=None):
    """CP5's e4 geometry -- the proven skull -- made of wire: the curtain's top is ragged (roots scattered
    over 2 x `ragged` metres of height and thinned by noise), the eddies are tubes through its depth, and the
    mouth is a maw that can roll open into a tunnel. Feature strengths are FIELD strengths, so the glow,
    the pull and the material all follow one routable number per feature."""
    # Sixteen GPU field slots and eight deformers are the budget, so features share pulls through
    # compounds and are weighted by their FIELD strengths (each routable): base = fall + chin + brow,
    # face = both eyes + both temples, mouth = the twin eddies whose shared wake is the slot.
    nodes = [
        field("fall", "direction", axis=[0, -1, 0], strength=1.0),
        field("chin", "attractor", position=[0, -18, 0], strength=0.8 * chin, falloff=smooth(6, 26)),
        field("base", "compound", children=["fall", "chin"], combine="add"),
        field("curl", "curlNoise", frequency=0.11, strength=1.0, seed=21),
        field("eyeL", "vortex", position=[-4.6, 2.5, 0], axis=[0, 0, -1], strength=eyes, falloff=smooth(1.2, 6.0),
              scale=[1, 1, 3]),
        field("eyeR", "vortex", position=[4.6, 2.5, 0], axis=[0, 0, 1], strength=eyes, falloff=smooth(1.2, 6.0),
              scale=[1, 1, 3]),
        field("hornL", "vortex", position=[-9, 11, 0], axis=[0, 0, 1], strength=horns, falloff=smooth(2, 9),
              scale=[1, 1, 3]),
        field("hornR", "vortex", position=[9, 11, 0], axis=[0, 0, -1], strength=horns, falloff=smooth(2, 9),
              scale=[1, 1, 3]),
        field("face", "compound", children=["eyeL", "eyeR", "hornL", "hornR"], combine="add"),
        field("eyes", "compound", children=["eyeL", "eyeR"], combine="add"),  # read by chorusWire only
        field("mouthL", "vortex", position=[-2.2, -6.5, 0], axis=[0, 0, -1], strength=mouth, falloff=smooth(0.8, 3.2),
              scale=[1, 1, 3]),
        field("mouthR", "vortex", position=[2.2, -6.5, 0], axis=[0, 0, 1], strength=mouth, falloff=smooth(0.8, 3.2),
              scale=[1, 1, 3]),
        # The maw: an inward spiral in the mouth (p3-roll's coil, turned to face us); 0 until the music opens it.
        field("maw", "spiral", position=[0, -6.5, 0], axis=[1, 0, 0], spiralBias=-0.3, strength=maw,
              falloff=smooth(1.5, 6), scale=[1.6, 1, 3]),
        field("mouth", "compound", children=["mouthL", "mouthR", "maw"], combine="add"),
        # The kick: onset fronts leaving the face at 9 m/s, read as a vector along +Y (a lift that travels).
        field("kick", "onset", position=[0, 1, 0], onsetSource="low", audioSpeed=9.0, onsetWidth=3.0,
              onsetDecay=1.6, waveGeometry="spherical", axis=[0, 1, 0], strength=1.0),
        field("crown", "sphere", position=[0, top, 0], radius=12.0, softness=6.0,
              falloff={"kind": "noiseModulated", "inner": 6, "outer": 18, "noiseAmount": 2.5, "noiseScale": 0.25}),
    ]
    pulls = [("curl", 6.0 * curl), ("face", 6.0), ("mouth", 6.0), ("kick", kick)]
    mat = metal(base=(1, 1, 1), rough=0.2)
    mat["program"] = "chorusWire"
    if film > 0:
        mat["thinFilm"] = {"thickness": film, "ior": 1.45}
    mat["emissiveColor"] = [1.0, 1.0, 1.0]  # the program writes the ember colour; this multiplies it
    mat["emissiveIntensity"] = 1.0
    spread = 30.0
    face = fibers("fibers", grid=(count_x, 1, depth), spacing=(spread / count_x, 1, 0.25), centre=(0, top, 0),
                  rotate=(0, 0, 180), jitter=(spread / count_x / 2, ragged, 0.12), length=length, width=width,
                  taper=1.0, segments=segments, steer=6.0, orient=(0.03, 3.14, 0.03), size_random=0.0,
                  min_px=0.7, effectors=[thin("crown", "crown")], material=mat, pulls=pulls, flow="base",
                  tensions={"face": tension, "mouth": tension})
    nodes.append(face)
    if debris:
        # Scale: a few short, bright sparks of wire near the lens, off the face's axis.
        sm = metal(base=(0.9, 0.9, 0.92), rough=0.1, emissive=(1.0, 0.6, 0.3), ei=2.0)
        nodes.append(fibers("debris", grid=(3, 3, 2), spacing=(8.0, 6.0, 4.0),
                            centre=(cam[0] * 0.7, cam[1] * 0.7 + 1, cam[2] - 14), jitter=(4.0, 3.0, 2.0),
                            length=0.7, width=0.01, taper=1.0, segments=4, flow="curl", steer=0.5,
                            size_random=0.5, min_px=1.0, seed=99, material=sm))
    return scene(title, nodes, cam_pos=list(cam), cam_target=list(target), fov=fov,
                 lights=lights or default_lights(), env=env or default_env(), programs=[wire_program()])


GOD_SHOTS = {
    "front": {},
    "low": {"cam": (0, -26, 36), "target": (0, 2, 0), "fov": 52},
    "three-quarter": {"cam": (-26, -5, 40), "target": (0, -2, 0), "fov": 42},
    "maw": {"mouth": 1.4, "maw": 3.0},
    "dissolved": {"eyes": 0.3, "horns": 0.25, "mouth": 0.2, "chin": 0.4, "curl": 0.12},
    "split-light": {"lights": "split", "env": "spectral"},
    "taut": {"tension": 1.0},
    "slack": {"tension": 0.0},
}

for _name, _kw in GOD_SHOTS.items():
    def _make(_kw=_kw, _name=_name):
        kw = dict(_kw)
        if kw.get("lights") == "split":
            kw["lights"], kw["env"] = split_lights(), spectral_env()
        return god_scene("Chorus Field p7 god " + _name, **kw)
    _make.__name__ = "p7_god_" + _name.replace("-", "_")
    LOOKS[_make.__name__] = _make


@look
def p8_god_trench():
    """The show on Trench. Topology first; each feature of the god is its own pull, weighted by the music:
      bass (smoothed)   -> the eyes, the temples and the chin: the god exists in proportion to the low end.
                           In the bass-less breakdowns (86-94 s, 134-144 s) it lets go into falling flow.
      highs (slow)      -> the maw: when the treble climbs (the second drop and the climax) the mouth rolls
                           open into a tunnel, p3-roll's coil turned to face us.
      mids (slow)       -> where the eyes are: the spacing drifts with the mid range, so the face that reforms
                           after each breakdown is not the face that left.
      kick              -> a shockwave: a radial push riding the low onset fronts out from the face at 9 m/s.
      snare             -> tearing: a short burst of curl.
    Appearance follows the topology for free: the ember rim exists only while an eye is strong (the material
    program reads the eye fields' weight), so the glow comes and goes with the face."""
    sc = god_scene("Chorus Field p8 god on Trench", cam=(0, -3, 100), mouth=1.4)
    d = "procedural/fibers/deform/"
    # Deformer order (god_scene): 1 base (fall + chin + brow), 2 curl, 3 face, 4 mouth, 5 kick.
    # The bass is normalised to its own running peak (ADR-1182, 20 s fall-back) and then cubed: the intro
    # is a hint, the build gathers, every drop -- the quieter second one too -- re-forms fully, on any song.
    norm = dict(normalizeSeconds=20.0, normalizeFloor=0.08, normalizeSmoothMs=800.0)
    gate = dict(**norm, gain=2.5, offset=-1.5, clampEnabled=True, clampMin=0.0, clampMax=1.0)
    bass = dict(**norm, attackMs=80, decayMs=1500, gain=1.1, curve="power", curveAmount=3.0, clampEnabled=True,
                clampMin=0.05, clampMax=1.05)
    sc["_routes"] = [
        route("audio.bass", "field/eyeL/strength", 1.0, op="multiply", **bass),
        route("audio.bass", "field/eyeR/strength", 1.0, op="multiply", **bass),
        route("audio.bass", "field/hornL/strength", 1.0, op="multiply", **bass),
        route("audio.bass", "field/hornR/strength", 1.0, op="multiply", **bass),
        route("audio.bass", "field/chin/strength", 1.0, op="multiply", **norm, attackMs=300, decayMs=2000, gain=1.2,
              clampEnabled=True, clampMin=0.3, clampMax=1.2),
        # highs (slow) open the mouth into a tunnel: the twin eddies grow until their wake is a gape.
        route("audio.treble", "field/mouthL/strength", 2.2, op="add", attackMs=2500, decayMs=4000, gain=3.0,
              clampEnabled=True, clampMin=0.0, clampMax=1.0, curve="power", curveAmount=2.0),
        route("audio.treble", "field/mouthR/strength", 2.2, op="add", attackMs=2500, decayMs=4000, gain=3.0,
              clampEnabled=True, clampMin=0.0, clampMax=1.0, curve="power", curveAmount=2.0),
        # ... and past a threshold the maw rolls the mouth into a tunnel (an inward spiral in the mouth compound).
        route("audio.treble", "field/maw/strength", 3.0, op="add", **norm, attackMs=3000, decayMs=4000, gain=1.0,
              curve="power", curveAmount=4.0, clampEnabled=True, clampMin=0.0, clampMax=1.0),
        route("audio.mid", "field/eyeL/position", -3.0, op="add", component=0, attackMs=1500, decayMs=3000),
        route("audio.mid", "field/eyeR/position", 3.0, op="add", component=0, attackMs=1500, decayMs=3000),
        route("audio.onsetLow", d + "5/amount", 10.0, op="add", envelope="peakhold", envelopeHoldMs=60,
              envelopeFallPerSecond=3.0),
        route("audio.onsetMid", d + "2/amount", 1.2, op="add", envelope="peakhold", envelopeHoldMs=40,
              envelopeFallPerSecond=6.0),
        # ---- the camera: phrase-scale, sprung, no shake, driven by the same normalised bass ----
        # G = the normalised bass mapped so it is 0 below 60% of its running peak and 1 at it. A fast copy of G
        # and a slow one (6 s) are routed with opposite signs; their difference is a transient that only exists
        # while the god is changing state -- a drop throws the eye back (the reveal) and then the slow copy
        # pushes it in as the god gathers; a breakdown carries it forward through the falling curtain before
        # it eases back out. Base: z 100; at a held drop 100 + 45 - 99 = 46.
        route("audio.bass", "camera/position", 45.0, op="add", component=2, **gate, attackMs=300, decayMs=300,
              springHz=0.35),
        route("audio.bass", "camera/position", -99.0, op="add", component=2, **gate, attackMs=6000, decayMs=6000,
              springHz=0.2),
        # The breakdown's drift is also a drift sideways and down, into the curtain's flank.
        route("audio.bass", "camera/position", 5.0, op="add", component=0, **gate, attackMs=300, decayMs=300,
              springHz=0.25),
        route("audio.bass", "camera/position", -5.0, op="add", component=0, **gate, attackMs=6000, decayMs=6000,
              springHz=0.25),
        # An orbit to three-quarter at a hold: the bass held at its peak charges a bounded integral (ADR-1161)
        # that swings the eye 30 m to the side over ~7 s; when the bass goes, it swings back.
        route("audio.bass", "camera/position", -26.0, op="add", component=0, **norm, gain=1.0, offset=-0.95,
              integrate=True, integrateMin=0.0, integrateMax=1.0, springHz=0.15),
    ]
    return sc


# ---- Phase 6: the cost of a fiber (the directed god, p7-god-front, at other counts) ----------------------

BENCH = {
    # name: (count_x, depth rows, segments, width)
    "g006k-s48": (260, 24, 48, 0.006),
    "g006k-s24": (260, 24, 24, 0.006),
    "g006k-s12": (260, 24, 12, 0.006),
    "g025k-s24": (520, 48, 24, 0.004),
    "g050k-s24": (1040, 48, 24, 0.003),
    "g100k-s16": (2080, 48, 16, 0.002),
    "g200k-s16": (4160, 48, 16, 0.0015),
    "g400k-s12": (4160, 96, 12, 0.0012),
    "g800k-s08": (8320, 96, 8, 0.001),
}

for _name, (_cx, _d, _seg, _w) in BENCH.items():
    def _make(_cx=_cx, _d=_d, _seg=_seg, _w=_w, _name=_name):
        return god_scene("Chorus Field p6 " + _name, count_x=_cx, depth=_d, segments=_seg, width=_w)
    _make.__name__ = "p6_" + _name.replace("-", "_")
    LOOKS[_make.__name__] = _make


def main(argv):
    names = argv[1:] or list(LOOKS)
    for name in names:
        sc = LOOKS[name]()
        routes = sc.pop("_routes", [])
        base = name.replace("_", "-")
        with open(os.path.join(HERE, base + ".scene.json"), "w") as f:
            json.dump(sc, f, indent=1)
        with open(os.path.join(HERE, base + ".json"), "w") as f:
            pr = project("Chorus Field " + base, base + ".scene.json")
            pr["routes"] += routes
            json.dump(pr, f, indent=1)
        print("wrote", base)


if __name__ == "__main__":
    main(sys.argv)
