"""Cursed Slammers study: a STAND-IN for the H1 scan, so the prep recipe and the render setup can be proven before the
owner's download lands. Shaped like the scan: a front mask only (no back), dense (about 300 k tri), wrinkle detail in
the geometry, a UV atlas and a bronze-brown colour image. Not a gorilla; a test object.

    Blender -b --factory-startup -P make_standin_scan.py -- --out <dir>     # writes <dir>/standin-scan.glb
"""
import argparse
import math
import os
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
args = ap.parse_args(argv)
OUT = os.path.expanduser(args.out)
os.makedirs(OUT, exist_ok=True)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()


def ellipsoid(loc, size):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=1.0, location=loc)
    o = bpy.context.active_object
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    return o


parts = [ellipsoid((0, 0.10, 0.05), (0.50, 0.46, 0.52)),
         ellipsoid((0, -0.30, 0.18), (0.40, 0.14, 0.12)),        # brow bar
         ellipsoid((0, -0.36, -0.22), (0.36, 0.26, 0.24)),       # muzzle
         ellipsoid((0, -0.52, -0.06), (0.19, 0.10, 0.11)),       # nose
         ellipsoid((-0.33, -0.16, -0.10), (0.18, 0.20, 0.22)),
         ellipsoid((0.33, -0.16, -0.10), (0.18, 0.20, 0.22))]
bpy.ops.object.select_all(action="DESELECT")
for o in parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
mask = bpy.context.active_object
m = mask.modifiers.new("r", "REMESH")
m.mode = "VOXEL"
m.voxel_size = 0.006
bpy.ops.object.modifier_apply(modifier="r")
sm = mask.modifiers.new("s", "SMOOTH")
sm.iterations = 8
bpy.ops.object.modifier_apply(modifier="s")
# sockets with stone eyes (as the scan has): cut, then a sphere seated in each
for sx in (-1, 1):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.10, location=(0.165 * sx, -0.33, 0.06))
    cutter = bpy.context.active_object
    b = mask.modifiers.new("b", "BOOLEAN")
    b.operation = "DIFFERENCE"
    b.object = cutter
    bpy.context.view_layer.objects.active = mask
    bpy.ops.object.modifier_apply(modifier="b")
    bpy.data.objects.remove(cutter)
# keep the front only (Blender -Y is the face): delete everything behind y = 0.05
bpy.context.view_layer.objects.active = mask
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="DESELECT")
bpy.ops.object.mode_set(mode="OBJECT")
for v in mask.data.vertices:
    v.select = v.co.y > 0.05
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.delete(type="VERT")
bpy.ops.object.mode_set(mode="OBJECT")
for sx in (-1, 1):
    eye = ellipsoid((0.165 * sx, -0.30, 0.06), (0.075, 0.075, 0.075))
    bpy.ops.object.select_all(action="DESELECT")
    eye.select_set(True)
    mask.select_set(True)
    bpy.context.view_layer.objects.active = mask
    bpy.ops.object.join()
# wrinkles: a displacement from a stretched Voronoi crease texture plus clouds
tex = bpy.data.textures.new("wr", "VORONOI")
tex.noise_scale = 0.05
tex.distance_metric = "DISTANCE"
d = mask.modifiers.new("d", "DISPLACE")
d.texture = tex
d.strength = 0.004
d.texture_coords = "LOCAL"
bpy.ops.object.modifier_apply(modifier="d")
bpy.ops.object.shade_smooth()
# UV atlas
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.smart_project(island_margin=0.004)
bpy.ops.object.mode_set(mode="OBJECT")
# a colour image: bronze-brown patina noise
N = 2048
rng = np.random.default_rng(7)
base = np.array([0.18, 0.11, 0.07])
n = rng.random((N // 16, N // 16))
n = np.kron(n, np.ones((16, 16)))
fine = rng.random((N, N)) * 0.25
v = (0.55 + 0.45 * n + fine)[..., None]
rgb = np.clip(base[None, None, :] * v * 1.6, 0, 1)
img = bpy.data.images.new("standin_color", N, N)
img.pixels.foreach_set(np.concatenate([rgb, np.ones((N, N, 1))], axis=2).astype(np.float32).ravel())
img.filepath_raw = os.path.join(OUT, "standin_color.png")
img.file_format = "PNG"
img.save()
mat = bpy.data.materials.new("standin")
mat.use_nodes = True
t = mat.node_tree.nodes.new("ShaderNodeTexImage")
t.image = img
mat.node_tree.links.new(t.outputs["Color"], mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"])
mask.data.materials.clear()
mask.data.materials.append(mat)
# scan-like: an arbitrary scale and offset (the prep must normalise it)
mask.scale = (0.37, 0.37, 0.37)
mask.location = (0.2, 0.0, 1.1)
bpy.ops.object.select_all(action="DESELECT")
mask.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "standin-scan.glb"), export_format="GLB", use_selection=True,
                          export_yup=True, export_texcoords=True, export_materials="EXPORT")
print("stand-in", len(mask.data.polygons), "faces")
