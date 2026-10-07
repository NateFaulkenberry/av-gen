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

def fibers(name, *, grid=(64, 64, 64), spacing=(0.5, 0.5, 0.5), centre=(0, 0, 0), jitter=None,
           length=2.5, width=0.012, taper=0.3, segments=12, min_px=0.8, flow="flow", steer=4.0,
           stiffness=0.0, seed=11, material=None, effectors=(), material_variation=None,
           orient=(math.pi, math.pi, math.pi), size_random=0.4, cast_shadows=True):
    if jitter is None:
        jitter = [s * 0.5 for s in spacing]
    proc = {
        "source": {"kind": "fiber", "fiberLength": length, "fiberWidth": width, "fiberTaper": taper,
                   "fiberSegments": segments, "fiberMinPixels": min_px},
        "variation": {"seed": seed, "position": list(jitter), "rotation": list(orient),
                      "uniformScale": size_random},
        "distribution": {"kind": "grid", "gridCount": list(grid), "gridSpacing": list(spacing),
                         "center": list(centre)},
        "deformers": [{"kind": "streamline", "space": "world", "field": flow, "amount": steer,
                       "falloff": stiffness}],
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
         "color": [1.0, 0.94, 0.86], "intensity": 3.2, "castsShadow": True, "shadowStrength": 0.92,
         "softness": 1.2},
        {"name": "rim", "type": "directional", "role": "rim", "direction": [0.55, -0.2, 0.8],
         "color": [0.55, 0.7, 1.0], "intensity": 2.2, "castsShadow": False, "shadowStrength": 0.0,
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
    """Emergence: a curtain falling from a crown, parted by two counter-rotating eddies and gathered
    at a chin. Nothing is a face; the wakes behind the eddies are."""
    nodes = [
        field("fall", "direction", axis=[0, -1, 0], strength=1.0),
        field("eyeL", "vortex", position=[-4.2, 3.0, 0], axis=[0, 0, 1], strength=2.4,
              falloff=smooth(1.5, 5.5)),
        field("eyeR", "vortex", position=[4.2, 3.0, 0], axis=[0, 0, -1], strength=2.4,
              falloff=smooth(1.5, 5.5)),
        field("chin", "attractor", position=[0, -16, 0], strength=0.7, falloff=smooth(4, 22)),
        field("flow", "compound", children=["fall", "eyeL", "eyeR", "chin"], combine="add"),
        fibers("fibers", grid=(150, 1, 18), spacing=(0.2, 1, 0.3), centre=(0, 14, 0), jitter=(0.1, 0.4, 0.15),
               length=30.0, width=0.01, segments=32, steer=6.0, orient=(0.05, 3.14, 0.05), size_random=0.15,
               min_px=0.7),
    ]
    return scene("Chorus Field p2 mask", nodes, cam_pos=[0, 0, 42], cam_target=[0, -1, 0], fov=45)


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
