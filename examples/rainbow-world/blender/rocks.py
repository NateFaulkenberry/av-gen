"""Rainbow World, Stage 2: the supporting rock -- Poly Haven CC0 scans, ADAPTED to the painterly language.

Sources are the CC0 Poly Haven rocks already in the asset library (assets/terrain/rocks/, fetched by
tools/fetch_polyhaven.py; recorded in assets/rainbow-world.manifest.json). They are read, never modified.

Adaptation (02-research.md A: shape and value first, texture last):
  1. import, join, normalise to a 1 m nominal size, rest on z = 0;
  2. voxel remesh, then collapse-decimate to a few hundred faces: the scan's silhouette survives, its
     high-frequency noise does not -- big planar facets that catch light like a carved form;
  3. a narrow bevel plus angle-based smoothing, so facets meet in soft chamfers;
  4. a stylised material: warm stone in light, cool in cavities, moss/grass on the faces that look up
     (object +Z, so the moss follows an instance however it is oriented), the scan's albedo kept only as a
     30% value breakup.

Outputs (args: <outdir> [w h samples]): rocks_source.png, rocks_adapted.png, rock_<id>.glb (adapted, with a
baked 1k colour texture), rocks.blend
"""
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw_common as rw  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
SOURCES = ["boulder_01", "namaqualand_boulder_03", "rock_07", "rock_09", "stone_01", "rock_moss_set_01"]


def import_rock(rid):
    path = os.path.join(REPO, "assets", "terrain", "rocks", rid, rid + "_1k.gltf")
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    objs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    return objs


def normalise(objs, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.name = name
    o.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")
    o.location = (0, 0, 0)
    bpy.ops.object.transform_apply(location=True)
    dims = o.dimensions
    s = 1.0 / max(dims)
    o.scale = (s, s, s)
    bpy.ops.object.transform_apply(scale=True)
    zmin = min(v.co.z for v in o.data.vertices)
    for v in o.data.vertices:
        v.co.z -= zmin
    return o


def stylised_material(name, albedo_img=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    b = nt.nodes.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = 0.85
    b.inputs["Specular IOR Level"].default_value = 0.25
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    # object-space up for the moss (follows the instance)
    vt = nt.nodes.new("ShaderNodeVectorTransform")
    vt.vector_type = "NORMAL"
    vt.convert_from = "WORLD"
    vt.convert_to = "OBJECT"
    nt.links.new(geo.outputs["Normal"], vt.inputs[0])
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(vt.outputs[0], sep.inputs[0])
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 3.0
    nz.inputs["Detail"].default_value = 2.0
    nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
    up = rw.math_node(nt, "ADD", sep.outputs[2], rw.math_node(nt, "MULTIPLY", nz.outputs["Fac"], 0.5))
    moss = rw.math_node(nt, "MULTIPLY_ADD", up, 4.0)
    moss.node.inputs[2].default_value = -3.3
    moss = rw.math_node(nt, "MAXIMUM", moss, 0.0)
    moss = rw.math_node(nt, "MINIMUM", moss, 1.0)
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.25
    cav = rw.math_node(nt, "SUBTRACT", 1.0, ao.outputs["AO"])
    cav = rw.math_node(nt, "MULTIPLY", cav, 1.6, clamp=True)
    pt = rw.math_node(nt, "MULTIPLY_ADD", geo.outputs["Pointiness"], 4.0)
    pt.node.inputs[2].default_value = -2.0
    pt = rw.math_node(nt, "MAXIMUM", pt, 0.0)
    stone = rw.mix_rgb(nt, rw.math_node(nt, "MULTIPLY", nz.outputs["Fac"], 0.7), rw.hexlin("#A07E62"), rw.hexlin("#8C8486"))
    stone = rw.mix_rgb(nt, rw.math_node(nt, "MINIMUM", pt, 0.6), stone, rw.hexlin("#CDB08A"))
    stone = rw.mix_rgb(nt, cav, stone, rw.P["stone_shadow"])
    if albedo_img is not None:
        it = nt.nodes.new("ShaderNodeTexImage")
        it.image = albedo_img
        bw = nt.nodes.new("ShaderNodeRGBToBW")
        nt.links.new(it.outputs["Color"], bw.inputs[0])
        br = rw.math_node(nt, "MULTIPLY_ADD", bw.outputs[0], 0.6)
        br.node.inputs[2].default_value = 0.78
        stone = rw.mix_rgb(nt, 1.0, stone, br, "MULTIPLY")
    mossc = rw.mix_rgb(nt, nz.outputs["Fac"], rw.P["olive"], rw.P["meadow"])
    col = rw.mix_rgb(nt, moss, stone, mossc)
    nt.links.new(col, b.inputs["Base Color"])
    nt.links.new(b.outputs[0], out.inputs[0])
    return m


def adapt(o, target_faces=170, bevel=0.01):
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    r = o.modifiers.new("remesh", "REMESH")
    r.mode = "VOXEL"
    r.voxel_size = 0.02
    bpy.ops.object.modifier_apply(modifier="remesh")
    d = o.modifiers.new("dec", "DECIMATE")
    d.ratio = max(0.001, target_faces / max(1, len(o.data.polygons)))
    bpy.ops.object.modifier_apply(modifier="dec")
    bv = o.modifiers.new("bevel", "BEVEL")
    bv.width = bevel
    bv.segments = 2
    bv.limit_method = "ANGLE"
    bv.angle_limit = math.radians(25)
    bpy.ops.object.modifier_apply(modifier="bevel")
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(22))
    # a fresh UV for the bake
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    return o


def lineup(objs, spacing=1.35):
    for i, o in enumerate(objs):
        o.location = ((i - (len(objs) - 1) / 2) * spacing, 0, 0)


def main():
    args = rw.argv_after_dashes()
    out = args[0] if args else "build/rw/stage2/rocks"
    W = int(args[1]) if len(args) > 1 else 1280
    H = int(args[2]) if len(args) > 2 else 400
    S = int(args[3]) if len(args) > 3 else 48
    os.makedirs(out, exist_ok=True)
    sc = rw.reset()
    sun = Vector((-0.6, -0.5, 0.6)).normalized()
    rw.sky_world(sc, Vector((0, 0, 1)), strength=0.6)
    rw.sun_light(sun, strength=4.5)
    rw.setup_render(sc, W, H, samples=S)
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, 0))
    ground = bpy.context.object
    gm = bpy.data.materials.new("g")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = rw.P["meadow"]
    ground.data.materials.append(gm)
    srcs, info = [], []
    for rid in SOURCES:
        o = normalise(import_rock(rid), "src_" + rid)
        info.append((rid, len(o.data.polygons)))
        srcs.append(o)
    lineup(srcs)
    cam = rw.camera(lens=35)
    rw.aim(cam, (0, -8.0, 2.2), (0, 0, 0.3))
    rw.render(sc, os.path.join(out, "rocks_source.png"))
    print("SOURCE TRIS", info)
    adapted = []
    for o in srcs:
        o.hide_render = True
        a = o.copy()
        a.data = o.data.copy()
        a.name = o.name.replace("src_", "rock_")
        sc.collection.objects.link(a)
        a.hide_render = False
        albedo = None
        for m in o.data.materials:
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image and "diff" in n.image.name:
                    albedo = n.image
        adapt(a)
        a.data.materials.clear()
        a.data.materials.append(stylised_material("mat_" + a.name, albedo))
        adapted.append(a)
        print("ADAPTED", a.name, len(a.data.polygons))
    lineup(adapted)
    rw.render(sc, os.path.join(out, "rocks_adapted.png"))
    # the chosen boulder, close, three sides (it becomes the rim kit's base: one language for all rock)
    hero = adapted[1]
    for a in adapted:
        a.hide_render = a is not hero
    hero.location = (0, 0, 0)
    sc.render.resolution_x, sc.render.resolution_y = 640, 480
    for k, ang in enumerate((0, 120, 240)):
        r = math.radians(ang)
        rw.aim(cam, (math.sin(r) * 3.2, -math.cos(r) * 3.2, 1.3), (0, 0, 0.35))
        rw.render(sc, os.path.join(out, "rock_hero_%d.png" % k))
    for a in adapted:
        rw.export_glb([a], os.path.join(out, a.name + ".glb"))
    rw.save_blend(os.path.join(out, "rocks.blend"))
    print("done")


if __name__ == "__main__":
    main()
