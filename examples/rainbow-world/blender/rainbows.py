"""Rainbow World, Stage 2 prototype: the great rainbow arch (03-art-direction.md sections 5-7, E3's lessons).

A designed luminous arch built with the cues of a real bow, so it reads as light in air rather than paint:
  * a soft emissive VOLUME (no surface): a smooth spectral ramp across the tube, violet inside, red outside,
    gaussian density across the tube's section, so edges dissolve;
  * a narrow, faint inner skirt (the sky inside a real bow is brighter) -- E3's was too wide and milky;
  * Alexander's dark band: a thin ABSORBING volume between primary and secondary, which darkens the sky behind;
  * a faint, wider, reversed secondary;
  * feet that dissolve into a low glowing mist instead of ending at the ground;
  * coloured spill on the land under each foot.
The arch stands against a deep backdrop (E3: chroma comes from contrast, not from more emission).

Outputs (args: <outdir> [w h samples]): rainbow_<view>.png, rainbow.blend, rainbow_arcs.json (the arc
definitions, Y-up, for the AV Gen implementation in Stage 4/6)
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw_common as rw  # noqa: E402

ARCH = {"centre": (0.0, 160.0, -20.0), "radius": 115.0, "tube": 9.0, "normal": (0.0, 1.0, 0.0)}


def torus(name, centre, major, minor, segs=220):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=segs, minor_segments=36,
                                     location=centre, rotation=(math.radians(90), 0, 0))
    o = bpy.context.object
    o.name = name
    return o


def radial_coord(nt):
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs["Object"], sep.inputs[0])
    cmb = nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(sep.outputs[0], cmb.inputs[0])
    nt.links.new(sep.outputs[1], cmb.inputs[1])
    ln = nt.nodes.new("ShaderNodeVectorMath")
    ln.operation = "LENGTH"
    nt.links.new(cmb.outputs[0], ln.inputs[0])
    return sep, ln.outputs["Value"]


def tube_falloff(nt, sep, rad, major, sigma):
    d = rw.math_node(nt, "SUBTRACT", rad, major)
    d2 = rw.math_node(nt, "MULTIPLY", d, d)
    z2 = rw.math_node(nt, "MULTIPLY", sep.outputs[2], sep.outputs[2])
    q = rw.math_node(nt, "ADD", d2, z2)
    q = rw.math_node(nt, "MULTIPLY", q, 1.0 / (2 * sigma * sigma))
    q = rw.math_node(nt, "MULTIPLY", q, q)   # super-gaussian: a flat, evenly dense band with a soft edge
    q = rw.math_node(nt, "MULTIPLY", q, -1.0)
    return rw.math_node(nt, "EXPONENT", q)


def feet_fade(nt, sep):
    """Object-space y is 'up' for the rotated torus (local y -> world z): fade the arc toward the ground."""
    f = rw.math_node(nt, "MULTIPLY_ADD", sep.outputs[1], 1.0 / 60.0)
    f.node.inputs[2].default_value = 0.35
    return rw.math_node(nt, "MINIMUM", rw.math_node(nt, "MAXIMUM", f, 0.0), 1.0)


def emissive_bow(name, major, minor, stops, strength, sigma, absorb=0.0):
    o = torus(name, ARCH["centre"], major, minor)
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    sep, rad = radial_coord(nt)
    t = rw.math_node(nt, "MULTIPLY_ADD", rad, 1.0 / (2 * minor))
    t.node.inputs[2].default_value = 0.5 - major / (2 * minor)
    r = rw.ramp(nt, stops)
    nt.links.new(t, r.inputs[0])
    dens = tube_falloff(nt, sep, rad, major, sigma)
    dens = rw.math_node(nt, "MULTIPLY", dens, feet_fade(nt, sep))
    em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(r.outputs[0], em.inputs["Color"])
    nt.links.new(rw.math_node(nt, "MULTIPLY", dens, strength), em.inputs["Strength"])
    if absorb > 0:
        ab = nt.nodes.new("ShaderNodeVolumeAbsorption")
        ab.inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        nt.links.new(rw.math_node(nt, "MULTIPLY", dens, absorb), ab.inputs["Density"])
        add = nt.nodes.new("ShaderNodeAddShader")
        nt.links.new(em.outputs[0], add.inputs[0])
        nt.links.new(ab.outputs[0], add.inputs[1])
        nt.links.new(add.outputs[0], out.inputs["Volume"])
    else:
        nt.links.new(em.outputs[0], out.inputs["Volume"])
    o.data.materials.append(m)
    return o


def build_rainbow(strength=1.0):
    R, T = ARCH["radius"], ARCH["tube"]
    objs = [emissive_bow("bow_primary", R, T * 1.8, rw.SPECTRUM, 0.045 * strength, T * 1.25, absorb=0.06)]
    objs.append(emissive_bow("bow_skirt", R - T * 2.6, T * 1.6, [(0.0, (1.0, 0.97, 0.93)), (1.0, (1.0, 0.98, 0.95))],
                             0.0025 * strength, T * 0.75))
    objs.append(emissive_bow("bow_supernumerary", R - T * 1.55, T * 0.7, [(0.0, (0.25, 0.05, 0.8)), (0.5, (0.0, 0.6, 0.4)),
                                                                          (1.0, (0.3, 0.1, 0.8))], 0.03 * strength, T * 0.3))
    rev = [(1 - p, c) for p, c in reversed(rw.SPECTRUM)]
    objs.append(emissive_bow("bow_secondary", R * 1.24, T * 2.8, rev, 0.014 * strength, T * 1.1))
    # Alexander's dark band: absorption only, between the bows
    objs.append(emissive_bow("bow_darkband", R * 1.12, T * 1.6, [(0.0, (0, 0, 0)), (1.0, (0, 0, 0))], 0.0, T * 0.9,
                             absorb=0.012))
    return objs


def spill_lights(strength=1.0):
    c, R = Vector(ARCH["centre"]), ARCH["radius"]
    for side in (-1, 1):
        for k, (t, col) in enumerate(((0.15, (0.5, 0.2, 1.0)), (0.5, (0.3, 1.0, 0.4)), (0.85, (1.0, 0.35, 0.1)))):
            r = R - ARCH["tube"] * 1.5 + t * ARCH["tube"] * 3.0
            bpy.ops.object.light_add(type="AREA", location=c + Vector((side * r, 0, -c.z + 14)))
            L = bpy.context.object.data
            L.shape = "DISK"
            L.size = 30
            L.energy = 1.5e4 * strength
            L.color = col


def write_arcs(path):
    c = rw.to_avgen(ARCH["centre"])
    n = rw.to_avgen(ARCH["normal"])
    data = {"arcs": [{"name": "great_arch", "centre": list(c), "normal": list(n), "radius": ARCH["radius"],
                      "width": ARCH["tube"] * 2, "secondary": 1.24, "skirt": 0.18, "darkband": 0.35}]}
    with open(path, "w") as f:
        json.dump(data, f, indent=1)


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0] if args else "build/rw/stage2/rainbow")
    W = int(args[1]) if len(args) > 1 else 960
    H = int(args[2]) if len(args) > 2 else 540
    S = int(args[3]) if len(args) > 3 else 64
    os.makedirs(out, exist_ok=True)
    sc = rw.reset()
    sun = Vector((0.2, -0.75, 0.55)).normalized()   # sun behind the viewer, as for a real bow
    rw.sky_world(sc, sun, strength=0.9)
    rw.sun_light(sun, strength=4.5)
    # meadow with two hills either side so the feet land somewhere
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=200, y_subdivisions=200, size=900, location=(0, 200, 0))
    g = bpy.context.object
    for v in g.data.vertices:
        x, y = v.co.x, v.co.y + 200
        v.co.z = (16 * math.exp(-((x - 120) ** 2 + (y - 170) ** 2) / 5000) + 22 * math.exp(-((x + 130) ** 2 + (y - 150) ** 2) / 6000)
                  + 30 * math.exp(-((x) ** 2 + (y - 420) ** 2) / 20000) + 3 * math.sin(x * 0.03) * math.cos(y * 0.02))
    bpy.ops.object.shade_smooth()
    gm = bpy.data.materials.new("meadow")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = rw.P["meadow"]
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.95
    g.data.materials.append(gm)
    build_rainbow()
    write_arcs(os.path.join(out, "rainbow_arcs.json"))
    rw.setup_render(sc, W, H, samples=S)
    sc.cycles.volume_step_rate = 0.5
    cam = rw.camera(lens=20)
    rw.aim(cam, (0, -60, 8), (0, 160, 70))
    rw.render(sc, os.path.join(out, "rainbow_1_meadow.png"))
    # looking up at it, against the deep zenith
    rw.aim(cam, (20, 40, 4), (0, 160, 120))
    rw.render(sc, os.path.join(out, "rainbow_2_under_zenith.png"))
    spill_lights()
    rw.aim(cam, (-60, 40, 10), (-110, 160, 10))
    rw.render(sc, os.path.join(out, "rainbow_3_foot_spill.png"))
    rw.save_blend(os.path.join(out, "rainbow.blend"))
    print("done")


if __name__ == "__main__":
    main()
