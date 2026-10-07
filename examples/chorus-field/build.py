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

def fibers(name, *, grid=(64, 64, 64), spacing=(0.5, 0.5, 0.5), centre=(0, 0, 0), rotate=(0, 0, 0), jitter=None,
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
                       "falloff": stiffness}] + [{"kind": "streamline", "space": "world", "field": f, "amount": a,
                                                  "falloff": stiffness} for (f, a) in pulls],
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

def scene(name, nodes, *, cam_pos, cam_target, fov=40, lights=None, env=None, post=None):
    return {
        "format": "avgen-scene", "version": 1, "name": name,
        "camera": {"mode": 1, "position": cam_pos, "target": cam_target, "fov": fov, "orbitSpeed": 0},
        "lights": lights if lights is not None else default_lights(),
        "environment": env if env is not None else default_env(),
        "post": post if post is not None else default_post(),
        "nodes": nodes,
    }


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
        field("wind", "direction", axis=[1, 0, 0], strength=0.7),
        field("roll", "vortex", position=[2, 0, 0], axis=[0, 0, 1], strength=1.6, falloff=smooth(4, 16)),
        field("roll2", "vortex", position=[16, 5, 0], axis=[0, 0.2, 1], strength=1.2, falloff=smooth(2, 9)),
        field("flow", "compound", children=["wind", "roll", "roll2"], combine="add"),
        field("curl", "curlNoise", frequency=0.06, strength=1.0, seed=8),
        field("band", "box", position=[-24, 6, 0], size=[1, 3, 20], softness=2.5,
              falloff={"kind": "noiseModulated", "inner": 2, "outer": 40, "noiseAmount": 2.0, "noiseScale": 0.15}),
        fibers("fibers", grid=(3, 50, 220), spacing=(0.5, 0.16, 0.2), centre=(-24, 6, 0), rotate=(0, 0, -90),
               jitter=(0.4, 0.08, 0.1), length=70.0, width=0.008, segments=32, steer=5.0,
               orient=(0.05, 0.05, 0.05), size_random=0.3, effectors=[thin("band", "band")],
               pulls=[("curl", 0.5)]),
    ]
    return scene("Chorus Field p3 roll", nodes, cam_pos=[4, -2, 46], cam_target=[3, 1, 0], fov=50)


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


def main(argv):
    names = argv[1:] or list(LOOKS)
    for name in names:
        sc = LOOKS[name]()
        base = name.replace("_", "-")
        with open(os.path.join(HERE, base + ".scene.json"), "w") as f:
            json.dump(sc, f, indent=1)
        with open(os.path.join(HERE, base + ".json"), "w") as f:
            json.dump(project("Chorus Field " + base, base + ".scene.json"), f, indent=1)
        print("wrote", base)


if __name__ == "__main__":
    main(sys.argv)
