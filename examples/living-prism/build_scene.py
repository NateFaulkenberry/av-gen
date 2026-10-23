#!/usr/bin/env python3
"""The Living Prism: write the scene and the live project from layout.py, palette.py and the generated meshes.

    python3 examples/living-prism/build_scene.py --assets build/living-prism/assets
        [--scene-dir examples/living-prism/scene] [--project examples/living-prism/living-prism.json]

Stage 3: the materials (four programs), the lighting and the palette looks.
  - lp_rock    dark faceted rock: tone noise + a per-facet value, luminous voronoi seams in some regions only
  - lp_floor   the floor: the rock's tones, a wet dark glossy band at the waterline
  - lp_hero    the translucent-candidate heroes (spire, fan, ember): a dark body per facet, an emissive core
               strongest low and facing the viewer, a pale fresnel rim, bright terminations, selected bright facets
  - lp_xtal    dense crystal (druses, crust, chandelier, geode lining, arch crust, shards, the geode's rim band)
Each crystal node's own emissiveColor is a palette role (the program reads it as `materialEmission`), so one program
serves every formation and the palette recolours them all; the rock's tones and seam colour are program constants
the palette writes. The scene and its meshes (scene/) are generated and gitignored; the project is committed and is
a pure function of the scripts.
"""
import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import layout as L  # noqa: E402
import palette as PAL  # noqa: E402
import music  # noqa: E402

LOOKS = PAL.looks_linear()
UV = dict(LOOKS[0][1])  # the home look (Ultraviolet) is the rest state

# Screen-space refraction (ADR-1603, P1-4, eng/living-prism-p1): authored here, written only when the engine reads it.
# Strength 0 = off; at Low and Emergency the ladder draws these heroes as the opaque material below.
REFRACTION = os.environ.get("LP_REFRACTION", "0") == "1"
REFRACT = {"spire": {"strength": 0.035, "opacity": 0.35, "thickness": 1.2, "absorption": [1.2, 2.2, 0.4]},
           "fan": {"strength": 0.03, "opacity": 0.3, "thickness": 0.6, "absorption": [2.0, 1.2, 0.3]}}


class Prog:
    """A material program with labelled ops, so palette bindings and routes name ops, not indices."""

    def __init__(self, name, intensity=1.0):
        self.name, self.ops, self.labels, self.out = name, [], {}, {}
        self.intensity = intensity

    def op(self, kind, dst, label=None, **kw):
        o = {"kind": kind, "dst": dst}
        for k, v in kw.items():
            o[{"a": "srcA", "b": "srcB", "c": "srcC", "f": "value", "k": "constant", "k2": "constant2"}.get(k, k)] = v
        if label:
            self.labels[label] = len(self.ops)
        self.ops.append(o)
        return len(self.ops) - 1

    def path(self, label, field="constant"):
        i = self.labels[label]   # op paths are 1-based (material_params.cpp opPath, ADR-232)
        return "material/%s/op/%d/%s/%s" % (self.name, i + 1, self.ops[i]["kind"], field)

    def json(self, base, emission=-1, roughness=-1):
        return {"name": self.name, "baseColor": base, "metallic": -1, "roughness": roughness, "emission": emission,
                "emissionIntensity": self.intensity, "opacity": -1, "ops": self.ops}


def c4(rgb, a=1.0):
    return [round(v, 4) for v in rgb] + [a]


def rock_program():
    p = Prog("lp_rock")
    p.op("input", 0, input="worldPosition")
    p.op("noise", 1, a=0, f=0.45, seed=3)
    p.op("smoothstep", 1, a=1, k=[0.3, 0.75, 0, 0])
    p.op("input", 2, input="normal")
    p.op("quantize", 2, a=2, f=3.0, k=[0.37, 0.21, 0.53, 0], seed=7)   # .w: a uniform random per facet
    p.op("swizzle", 2, a=2, k=[3, 3, 3, 3])
    p.op("remap", 7, a=2, k=[0, 1, 0.55, 0.92], f=1.0)                  # roughness per facet: some facets glint
    p.op("remap", 2, a=2, k=[0, 1, 0, 0.6])
    p.op("add", 1, a=1, b=2)
    p.op("remap", 1, a=1, k=[0, 1.6, 0, 1], f=1.0)
    p.op("constant", 3, label="shadow", k=c4(UV["shadow"]))
    p.op("constant", 4, label="rockLit", k=c4(UV["rockLit"]))
    p.op("mixBy", 3, a=3, b=4, c=1)
    # seams: straight mineral cracks (voronoi cell edges, F2 - F1 near 0), thin, and only inside sparse regions (about
    # a tenth of the rock): light gathers in a few cracks instead of netting every surface
    p.op("voronoiEdge", 5, a=0, f=0.42, seed=11)
    p.op("swizzle", 5, a=5, k=[0, 0, 0, 0])   # voronoiEdge is (F2-F1, F1, cell, F2): keep the edge distance only
    p.op("smoothstep", 5, a=5, k=[0.0, 0.022, 0, 0])
    p.op("remap", 5, a=5, k=[0, 1, 1, 0], f=1.0)
    p.op("noise", 6, a=0, f=0.7, seed=31)                        # break the net into crack segments
    p.op("smoothstep", 6, a=6, k=[0.47, 0.6, 0, 0])
    p.op("multiply", 5, a=5, b=6)
    p.op("noise", 6, a=0, f=0.08, seed=24)
    p.op("smoothstep", 6, a=6, k=[0.66, 0.72, 0, 0], label="seamMask")
    p.op("multiply", 5, a=5, b=6)
    p.op("constant", 6, label="seam", k=c4(UV["seam"]))
    p.op("multiply", 6, a=5, b=6)
    p.op("constant", 5, label="seamGain", k=[0.45, 0.45, 0.45, 1])
    p.op("multiply", 6, a=6, b=5)
    p.op("field", 5, field="front")                                     # the travelling front of light (Stage 4)
    p.op("constant", 4, label="frontColor", k=c4([v * 2.6 for v in UV["fill"]]))
    p.op("multiply", 5, a=5, b=4)
    p.op("add", 6, a=6, b=5)
    return p, p.json(3, emission=6, roughness=7)


def floor_program():
    p = Prog("lp_floor")
    p.op("input", 0, input="worldPosition")
    p.op("noise", 1, a=0, f=0.6, seed=4)
    p.op("smoothstep", 1, a=1, k=[0.3, 0.75, 0, 0])
    p.op("constant", 2, label="shadow", k=c4([v * 0.8 for v in UV["shadow"]]))
    p.op("constant", 3, label="rockLit", k=c4([v * 0.55 for v in UV["rockLit"]]))
    p.op("mixBy", 3, a=2, b=3, c=1)
    # the waterline: wet (darker, glossy) below 0.45 m, so the shore picks up the crystals' light
    p.op("gradient", 4, a=0, k=[0, 1, 0, 1.4], f=-3.0)
    p.op("remap", 5, a=4, k=[0, 1, 1, 0.45], f=1.0)
    p.op("multiply", 3, a=3, b=5)
    p.op("remap", 6, a=4, k=[0, 1, 0.82, 0.12], f=1.0)
    p.op("field", 5, field="front")
    p.op("constant", 4, label="frontColor", k=c4([v * 1.2 for v in UV["fill"]]))
    p.op("multiply", 5, a=5, b=4)
    return p, p.json(3, emission=5, roughness=6)


def crystal_program(name, hero):
    """The crystal look on an opaque surface. r0 = the node's role colour (materialEmission)."""
    p = Prog(name)
    p.op("input", 0, input="materialEmission")
    p.op("input", 1, input="normal")
    p.op("quantize", 3, a=1, f=3.0, k=[0.31, 0.47, 0.23, 0], seed=5)
    p.op("swizzle", 3, a=3, k=[3, 3, 3, 3])                              # r3: the facet's random
    p.op("input", 2, input="viewDirection")
    p.op("fresnel", 5, a=1, b=2, f=2.5)                                  # r5: rim
    p.op("constant", 6, k=[1, 1, 1, 1])
    p.op("mix", 6, a=0, b=6, f=0.3, label="tipMix")                     # r6: tip colour, C toward white
    p.op("remap", 7, a=0, k=[0, 1, 0, 0.06 if hero else 0.08])           # r7: a dark, saturated body
    p.op("remap", 2, a=3, k=[0, 1, 0.35, 1.65])
    p.op("multiply", 7, a=7, b=2)
    p.op("remap", 1, a=5, k=[0, 1, 1, 0.1], f=1.0)                       # r1: facing the viewer
    if hero:
        p.op("input", 4, input="localPosition")
        p.op("gradient", 4, a=4, k=[0, 1, 0, 0], f=0.19)                 # r4: base 0 -> tip 1 (~5 m)
        p.op("remap", 2, a=4, k=[0, 1, 1.35, 0.3], f=1.0)                # the core sits low
        p.op("multiply", 1, a=1, b=2)
    p.op("power", 2, a=3, f=2.0)                                         # facets differ strongly: some dark,
    p.op("remap", 2, a=2, k=[0, 1, 0.12, 1.5])                           # some lit from inside
    p.op("multiply", 1, a=1, b=2)
    p.op("multiply", 1, a=1, b=0, label="core")                          # core emission = C x weight
    p.op("smoothstep", 2, a=5, k=[0.5, 1.0, 0, 0])
    p.op("multiply", 2, a=2, b=6)
    p.op("remap", 2, a=2, k=[0, 1, 0, 0.55])
    p.op("add", 1, a=1, b=2, label="rim")
    p.op("smoothstep", 2, a=3, k=[0.86, 0.92, 0, 0], label="sparkle")    # ~8% of facets are bright mirrors
    p.op("multiply", 2, a=2, b=6)
    p.op("constant", 5, label="sparkleGain", k=[1.6, 1.6, 1.6, 1])
    p.op("multiply", 2, a=2, b=5)
    p.op("add", 1, a=1, b=2)
    if hero:
        p.op("smoothstep", 2, a=4, k=[0.78, 1.0, 0, 0])                  # luminous terminations
        p.op("multiply", 2, a=2, b=6)
        p.op("remap", 2, a=2, k=[0, 1, 0, 0.4])
        p.op("add", 1, a=1, b=2)
    p.op("field", 2, field="front")                                      # the front lights each crystal it
    p.op("multiply", 2, a=2, b=0)                                        # crosses, in its own colour
    p.op("constant", 5, label="frontGain", k=[3.2, 3.2, 3.2, 1])
    p.op("multiply", 2, a=2, b=5)
    p.op("add", 1, a=1, b=2)
    p.op("remap", 3, a=3, k=[0, 1, 0.03, 0.18])                         # roughness per facet: crisp highlights
    return p, p.json(7, emission=1, roughness=3)


ROCK_P, ROCK_J = rock_program()
FLOOR_P, FLOOR_J = floor_program()
HERO_P, HERO_J = crystal_program("lp_hero", True)
XTAL_P, XTAL_J = crystal_program("lp_xtal", False)
PROGRAMS = [ROCK_J, FLOOR_J, HERO_J, XTAL_J]
ROCK = {"baseColor": [0.02, 0.016, 0.03], "roughness": 0.8, "metallic": 0.0, "program": "lp_rock"}
FLOOR = {"baseColor": [0.014, 0.012, 0.024], "roughness": 0.7, "metallic": 0.0, "program": "lp_floor"}

# crystal nodes: (palette role, emissive strength, program); strengths set the hierarchy (heroes > crust)
XNODE = {"spire": ("primary", 1.7, "lp_hero"), "fan": ("secondary", 1.3, "lp_hero"), "ember": ("accent", 0.75, "lp_hero"),
         "geode": ("tertiary", 1.9, "lp_xtal"), "geodecap": ("tertiary", 1.2, "lp_xtal"),
         "chandelier": ("secondary", 1.5, "lp_xtal"), "arch": ("primary", 1.3, "lp_xtal"),
         "druse": ("secondary", 0.8, "lp_xtal"), "crust": ("core", 0.45, "lp_xtal"), "shard": ("core", 0.6, "lp_xtal"),
         "geoderim": ("tertiary", 0.1, "lp_xtal")}

WATER = {  # ADR-1286's cinematic path: a dark absorbing mirror; SSR reflects the lit grotto
    "shallowColor": [0.004, 0.004, 0.014], "deepColor": [0.002, 0.002, 0.008], "shallow": 1.0,
    "clarity": 1.2, "edgeFade": 0.15, "maxOpacity": 0.97, "fresnel": 0.16, "reflection": 1.0,
    "reflectionTint": [0.06, 0.04, 0.16], "specular": 1.4, "roughness": 0.025,
    "ripple": 0.02, "rippleScale": 0.8, "rippleSpeed": 0.35, "chop": 0.15, "foam": 0.0, "refraction": 0.08,
    "glow": 0.0, "sparkle": 0.0,
    "ssr": 1.0, "ssrDistance": 30.0, "ssrSteps": 64.0, "ssrGain": 1.5, "streak": 0.04, "reflectionRays": 2.0,
    "transmission": 1.0, "absorption": [2.2, 1.8, 1.0], "undulation": 0.02, "undulationScale": 0.2,
    "roughnessVariation": 0.25, "tintPatches": 0.0, "glints": 0.0,
}
# the haze: it carries the shaft and the passage's depth; Stage 3's A/B decides its cost (06/08)
HAZE = os.environ.get("LP_HAZE", "1") == "1"


def mesh_node(name, asset, points, material, shadow=True):
    pr = {"source": {"kind": "mesh", "asset": asset}, "distribution": {"kind": "points", "points": points},
          "material": dict(material)}
    if not shadow:
        pr["castsShadow"] = False
    return {"name": name, "kind": "procedural", "procedural": pr}


def xmat(key, hero_name=None):
    role, k, prog = XNODE[key]
    m = {"baseColor": [0.05, 0.02, 0.08], "emissiveColor": UV[role], "emissiveIntensity": k, "roughness": 0.1,
         "metallic": 0.0, "program": prog}
    if prog == "lp_hero":
        m["thinFilm"] = {"thickness": 420.0, "ior": 1.45}   # a faint interference sheen on the facets
    if REFRACTION and hero_name in REFRACT:
        m["refraction"] = dict(REFRACT[hero_name])
    return m


def scene_json():
    nodes = [{"name": "floor", "kind": "terrain", "position": [0, 0, 0], "world": L.world_json(),
              "terrain": {"chunkSize": 20.0, "resolution": 64, "lodLevels": 1, "lodDistance": 200.0,
                          "viewDistance": 200.0, "shadowDistance": 40.0, "skirtDepth": 0.5, "groundMottle": False,
                          "water": WATER},
              "material": dict(FLOOR)}]
    nodes.append(mesh_node("shell", "shell.glb", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]], ROCK, shadow=False))
    nodes.append(mesh_node("crust_xtal", "crust_xtal.glb", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]], xmat("crust"),
                           shadow=False))
    for h in L.HEROES:
        rec = L.hero_placement(h)
        nodes.append(mesh_node(h["name"] + "_xtal", h["name"] + "_xtal.glb", [rec], xmat(h["name"], h["name"])))
        nodes.append(mesh_node(h["name"] + "_rock", h["name"] + "_rock.glb", [rec], ROCK))
        if h["name"] == "geode":
            nodes.append(mesh_node("geode_rim", "geode_rim.glb", [rec], xmat("geoderim")))
            nodes.append(mesh_node("geodecap_xtal", "geodecap_xtal.glb", [rec], xmat("geodecap")))
    a = L.ARCH
    rec = L.placement(*a["pos"], a["yaw"], a["scale"], a["sink"])
    nodes.append(mesh_node("arch_rock", "arch_rock.glb", [rec], ROCK))
    nodes.append(mesh_node("arch_xtal", "arch_xtal.glb", [rec], xmat("arch")))
    groups = {}
    for v, x, z, yaw, s in L.CLUSTERS:
        groups.setdefault(v, []).append(L.placement(x, z, yaw, s, 0.12 * s))
    for v, pts in sorted(groups.items()):
        nodes.append(mesh_node(v + "_xtal", v + "_xtal.glb", pts, xmat("druse")))
        nodes.append(mesh_node(v + "_rock", v + "_rock.glb", pts, ROCK))
    groups = {}
    for v, x, z, yaw, s, lean in L.SHARDS:
        groups.setdefault(v, []).append(L.placement(x, z, yaw, s, 0.1 * s, (lean, 0.0)))
    for v, pts in sorted(groups.items()):
        nodes.append(mesh_node(v + "_xtal", v + "_xtal.glb", pts, xmat("shard"), shadow=False))
    groups = {}
    for v, x, z, yaw, s, sink in L.BOULDERS:
        groups.setdefault(v, []).append(L.placement(x, z, yaw, s, sink))
    for v, pts in sorted(groups.items()):
        nodes.append(mesh_node(v, v + ".glb", pts, ROCK))

    p0, t0, _ = L.CAMERA_STATIONS[0]
    lights = [
        # the shaft: a narrow spot from above the roof (the shell casts no shadow) down onto the spire and the water
        {"name": "shaft", "id": "shaft", "type": "spot", "role": "key", "position": [-3.0, 14.0, 1.5],
         "direction": [0.18, -0.85, -0.5], "color": UV["key"], "intensity": 300.0, "range": 34.0,
         "innerCone": 6.0, "outerCone": 14.0, "castsShadow": True, "volumetric": 1.0},
        # the passage behind the arch: the depth cue
        {"name": "passage", "id": "passage", "type": "point", "position": [-2.5, 3.0, -19.0],
         "color": UV["deep"], "intensity": 320.0, "range": 16.0, "radius": 1.5, "castsShadow": False,
         "volumetric": 0.7},
        # the roof wash: a broad, weak light under the roof so its rock and crust read (no dead space above)
        {"name": "roofwash", "id": "roofwash", "type": "point", "position": [0.5, 5.4, -0.5], "color": UV["fill"],
         "intensity": 90.0, "range": 14.0, "radius": 3.0, "castsShadow": False, "volumetric": 0.0},
        {"name": "backwash", "id": "backwash", "type": "point", "position": [0.0, 6.5, -7.5], "color": UV["deep"],
         "intensity": 45.0, "range": 12.0, "radius": 3.0, "castsShadow": False, "volumetric": 0.0},
        {"name": "frontwash", "id": "frontwash", "type": "point", "position": [-1.0, 3.6, 5.0], "color": UV["deep"],
         "intensity": 3.0, "range": 8.0, "radius": 2.0, "castsShadow": False, "volumetric": 0.0},
    ]

    def at(name, dx, dy, dz):
        h = next(h for h in L.HEROES if h["name"] == name)
        r = L.hero_placement(h)
        return [round(r[0] + dx, 3), round(r[1] + dy, 3), round(r[2] + dz, 3)]
    for name, off, role, k, rng_, rad in (
            ("spire", (0.1, 1.0, 1.6), "primary", 50.0, 9.0, 0.6),
            ("geode", (-0.4, 1.4, 0.4), "tertiary", 26.0, 4.5, 0.4),
            ("fan", (1.8, 2.4, 0.6), "secondary", 26.0, 7.0, 0.5),
            ("chandelier", (0.0, -3.4, 0.0), "secondary", 22.0, 7.0, 0.5),
            ("ember", (0.9, 0.35, -1.1), "accent", 10.0, 4.0, 0.3)):
        lights.append({"name": name + "Glow", "id": name + "Glow", "type": "point", "position": at(name, *off),
                       "color": UV[role], "intensity": k, "range": rng_, "radius": rad, "castsShadow": False,
                       "volumetric": 0.0})
    env = {
        "background": [0.004, 0.003, 0.012], "intensity": 0.04, "lightFromEnvironment": False,
        "fogColor": [round(v * 0.3, 4) for v in UV["haze"]],
        "volumeDensity": 0.005 if HAZE else 0.0, "volumeScattering": 0.9,
        "volumeAbsorption": 0.3, "volumeAnisotropy": 0.45, "volumeSteps": 12, "volumeMaxDistance": 30.0,
        "volumeLocalLights": 1.0, "volumeNoise": 0.35, "volumeNoiseScale": 0.18, "volumeEmission": 0.0,
        "fogHeight": 3.0, "fogHeightFalloff": 0.25, "fogUpperDensity": 0.15,
        "shadowCascades": 1, "shadowRange": 40.0, "ambientOcclusion": True, "depthPrepass": True,
        "sky": {"enabled": True, "background": True, "useKeyLight": False, "zenithColor": [0.01, 0.006, 0.03],
                "horizonColor": [0.02, 0.012, 0.05], "groundColor": [0.006, 0.004, 0.012],
                "sunColor": [0.6, 0.5, 1.0], "sunDirection": [0.2, 0.9, 0.3], "haze": 0.1,
                "sunIntensity": 0.0, "sunSize": 0.01, "sunGlow": 0.0, "intensity": 1.0}}
    return {
        "format": "avgen-scene", "version": 1, "name": "The Living Prism",
        "_note": "Generated by examples/living-prism/build_scene.py from layout.py and palette.py. Do not edit.",
        "camera": {"mode": 1, "position": list(p0), "target": list(t0), "fov": L.CAMERA_FOV, "orbitSpeed": 0,
                   "near": 0.1, "far": 120.0},
        "environment": env,
        "lights": lights,
        "materialPrograms": PROGRAMS,
        "nodes": nodes + [front_field(), motes()],
    }


def front_field():
    """The kick's front: a thin spherical shell of light leaving the spire's base; its radius and strength are
    routed from signal.front (music.py), so it is a function of the music, not of a clock."""
    h = next(h for h in L.HEROES if h["name"] == "spire")
    r = L.hero_placement(h)
    c = [r[0], r[1] + 0.6, r[2]]
    # a field node (its parameters register as field/front/..., which the routes write)
    return {"name": "front", "kind": "field", "position": [0.0, 0.0, 0.0], "field": {
        "kind": "wave", "enabled": True, "space": "world", "position": c, "point": c,
        "strength": 1.0, "waveGeometry": "spherical", "waveShape": "pulse", "amplitude": 0.0,
        "wavelength": 2.2, "waveSpeed": 0.0, "waveWidth": 0.0, "waveOrigin": 13.0,
        "falloff": {"kind": "smoothstep", "inner": 3.0, "outer": 15.0}}}


def motes():
    """Fine detail: a sparse drift of crystal motes in the air of the focal area (well under 1% of the frame), rising
    slowly on the turbulence; their spawn and brightness follow the synth (music.py)."""
    return {"name": "motes", "kind": "particles", "particles": {
        "enabled": True, "capacity": 4000, "seed": 17, "shape": "box", "position": [0.0, 2.6, -4.0],
        "extent": [9.0, 2.6, 5.5], "spawnRate": 90.0, "lifetimeMin": 5.0, "lifetimeMax": 9.0, "spread": 1.0,
        "speedMin": 0.02, "speedMax": 0.12, "gravity": [0.0, 0.035, 0.0], "drag": 0.3, "turbulence": 0.25,
        "turbulenceScale": 0.4, "turbulenceSpeed": 0.15, "sizeStart": 0.022, "sizeEnd": 0.0,
        "colorStart": [0.85, 0.7, 1.0, 1.0], "colorEnd": [0.5, 0.35, 1.0, 0.0], "emissive": 2.5, "blend": "additive"}}


def crystal_nodes():
    """[(node name, XNODE key)] for every crystal node the scene draws."""
    out = [("crust_xtal", "crust"), ("arch_xtal", "arch"), ("geode_rim", "geoderim"), ("geodecap_xtal", "geodecap")]
    out += [(h["name"] + "_xtal", h["name"]) for h in L.HEROES]
    out += [(v + "_xtal", "druse") for v in sorted({c[0] for c in L.CLUSTERS})]
    out += [(v + "_xtal", "shard") for v in sorted({c[0] for c in L.SHARDS})]
    return out


def palette_json():
    b = [{"role": "shadow", "target": ROCK_P.path("shadow")}, {"role": "rockLit", "target": ROCK_P.path("rockLit")},
         {"role": "seam", "target": ROCK_P.path("seam")},
         {"role": "fill", "target": ROCK_P.path("frontColor"), "gain": 2.6},
         {"role": "fill", "target": FLOOR_P.path("frontColor"), "gain": 1.2},
         {"role": "shadow", "target": FLOOR_P.path("shadow"), "gain": 0.8},
         {"role": "rockLit", "target": FLOOR_P.path("rockLit"), "gain": 0.55},
         {"role": "key", "target": "lights/shaft/color"}, {"role": "deep", "target": "lights/passage/color"},
         {"role": "fill", "target": "lights/roofwash/color"}, {"role": "deep", "target": "lights/backwash/color"}, {"role": "deep", "target": "lights/frontwash/color"},
         {"role": "primary", "target": "lights/spireGlow/color"}, {"role": "tertiary", "target": "lights/geodeGlow/color"},
         {"role": "secondary", "target": "lights/fanGlow/color"},
         {"role": "secondary", "target": "lights/chandelierGlow/color"},
         {"role": "accent", "target": "lights/emberGlow/color"},
         {"role": "haze", "target": "scene/fogColor", "gain": 0.3},
         {"role": "water", "target": "nodes/floor/water/shallowColor", "gain": 0.04},
         {"role": "water", "target": "nodes/floor/water/deepColor", "gain": 0.02}]
    for node, key in crystal_nodes():
        b.append({"role": XNODE[key][0], "target": "procedural/%s/material/emissiveColor" % node})
    states = [{"name": name, "colors": {r: roles[r] for r in PAL.ROLES}} for name, roles in LOOKS]
    # no automatic advance: a look changes on a performer's CC 9 (and, from Stage 4, on a musical reason)
    # A look changes only for a musical reason: on a phrase boundary where the music's character has moved (the
    # selector's change detector over energy, brightness and density), never more often than every 45 s; CC 10
    # locks the performer's choice. Order: from the home look toward the warm and back through the cool.
    sel = {"select": 0, "transition": 6.0, "lock": False, "hold": 45.0,
           "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": [0, 4, 1, 3, 2, 5],
                       "change": {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"], "fast": 3.0,
                                  "slow": 24.0, "threshold": 0.07, "rearm": 0.03, "primaryTimeout": 40.0,
                                  "maxHold": 150.0}}}
    return {"states": states, "bindings": b, "selector": sel}


def project_json(scene_rel):
    samples, _ = L.camera_samples(0.25)
    sources, routes = [], []
    for key, idx, comp in (("camPX", 1, 0), ("camPY", 1, 1), ("camPZ", 1, 2),
                           ("camTX", 2, 0), ("camTY", 2, 1), ("camTZ", 2, 2)):
        keys = [{"time": s[0], "value": round(s[idx][comp], 4), "interp": "linear"} for s in samples]
        sources.append({"kind": "timeline", "name": key, "settings": {"loopLength": round(L.CAMERA_LOOP, 4),
                                                                      "keys": keys}})
        routes.append({"source": "timeline." + key, "target": "camera/position" if idx == 1 else "camera/target",
                       "amount": 1.0, "op": "replace", "polarity": "unipolar", "component": comp})
    sources += music.signals()
    small = sorted({v + "_xtal" for v, *_ in L.CLUSTERS})
    routes += music.routes(ROCK_P, FLOOR_P, HERO_P, XTAL_P, {"secondary_small": small})
    n = len(LOOKS)
    routes += [{"source": "control.palette", "target": "palette/select", "amount": float(n - 1), "op": "add",
                "polarity": "unipolar", "chain": {"attackMs": 150, "decayMs": 150}},
               {"source": "control.paletteLock", "target": "palette/lock", "amount": 1.0, "op": "add",
                "polarity": "unipolar", "chain": {"attackMs": 150, "decayMs": 150}}]
    return {
        "format": "avgen-project", "version": 4, "app": {"name": "The Living Prism"},
        "assets": {"scene": {"kind": "composition", "path": scene_rel}},
        "sonic": {"live": True},
        "sonicScene": {"category": "world", "id": "living-prism", "title": "The Living Prism",
                       "thesis": "A compact crystal grotto over a mirror pool, played by the music: the drums strike "
                                 "the crystals, the bass moves the water and the stone, the synth colours the light."},
        "parameters": {
            "camera/exposure/compensation": -0.2, "post/bloom/enabled": True, "post/bloom/threshold": 1.1,
            "post/bloom/intensity": 0.32, "post/bloom/radius": 0.9, "post/tonemap/chroma-retention": 0.9,
            "post/output/vignette": 0.25, "post/grade/saturation": 1.2, "post/grade/contrast": 1.08,
        },
        "sources": sources,
        "routes": routes,
        "palette": palette_json(),
        "control": {"midi": {"enabled": True, "filter": "*", "bindings": [
            {"source": "*", "channel": -1, "kind": "cc", "number": 1, "signal": "impact"},
            {"source": "*", "channel": -1, "kind": "cc", "number": 2, "signal": "world"},
            {"source": "*", "channel": -1, "kind": "cc", "number": 9, "signal": "palette"},
            {"source": "*", "channel": -1, "kind": "cc", "number": 10, "signal": "paletteLock"}]}},
        "render": {"width": 1920, "height": 1080, "fps": 30},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", required=True)
    ap.add_argument("--scene-dir", default=os.path.join(HERE, "scene"))
    ap.add_argument("--project", default=os.path.join(HERE, "living-prism.json"))
    a = ap.parse_args()
    os.makedirs(a.scene_dir, exist_ok=True)
    sc = scene_json()
    used = set()
    for n in sc["nodes"]:
        src = n.get("procedural", {}).get("source", {})
        if src.get("kind") == "mesh":
            used.add(src["asset"])
    for f in sorted(used):
        shutil.copy(os.path.join(a.assets, f), os.path.join(a.scene_dir, f))
    path = os.path.join(a.scene_dir, "living-prism.scene.json")
    with open(path, "w") as f:
        json.dump(sc, f, indent=1)
    rel = os.path.relpath(path, os.path.dirname(os.path.abspath(a.project)))
    with open(a.project, "w") as f:
        json.dump(project_json(rel), f, indent=1)
        f.write("\n")
    # the offline companion: the same world playing Feline Footwear's mix, analysed from its three stems (they are
    # analysed, never played: ADR-1520). Gitignored assets; the project names them relative to the repository.
    off = project_json(rel)
    off["app"]["name"] = "The Living Prism (offline, Feline Footwear)"
    off["sonic"] = {"live": False}
    root = os.path.relpath(os.path.join(HERE, "..", ".."), os.path.dirname(os.path.abspath(a.project)))
    off["assets"]["audio"] = {"path": os.path.join(root, "assets/audio/feline-footwear.wav")}
    off["assets"]["stems"] = [{"key": k, "label": k.title(), "file": os.path.join(
        root, "assets/audio/stems/feline-footwear (%s).wav" % k.title())} for k in ("drums", "bass", "synth")]
    with open(os.path.join(os.path.dirname(os.path.abspath(a.project)), "living-prism-offline.json"), "w") as f:
        json.dump(off, f, indent=1)
        f.write("\n")
    print("wrote %s (%d nodes, %d meshes, %d programs, %d ops max) and %s" % (
        path, len(sc["nodes"]), len(used), len(PROGRAMS), max(len(p["ops"]) for p in PROGRAMS), a.project))
    print("station times:", L.station_times())


if __name__ == "__main__":
    main()
