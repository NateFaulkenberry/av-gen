"""Cursed Slammers, THROWAWAY optical probe subject (not the hero asset).

A crude ape-ish head (voxel-fused ellipsoids: cranium, crest, brow bar, muzzle; two socket cuts), an eyeball, and a
procedural hand gripping a vertical bar: four three-segment fingers wrapped round the bar's front, a thumb, a palm mass
behind. Enough to judge parallax and the optical surface on a slow side-to-side camera; nothing here is the hero.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P make_probe_subject.py -- --out <dir>

Blender Z-up, face looking -Y; the glTF exporter writes +Y up, face +Z. Units: the head is ~1.0 wide.
Hand frame: the bar's axis is glTF +Y through the origin, the fingers wrap round the bar's +Z (camera) side.
"""
import argparse
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
args = ap.parse_args(argv)
OUT = os.path.expanduser(args.out)
os.makedirs(OUT, exist_ok=True)


def clear():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def ellipsoid(loc, size, name="e"):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=1.0, location=loc)
    o = bpy.context.active_object
    o.scale = size
    o.name = name
    bpy.ops.object.transform_apply(scale=True)
    return o


def capsule(a, b, r, name="c"):
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=r, depth=d.length, location=(a + b) / 2)
    o = bpy.context.active_object
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    bpy.ops.object.transform_apply(rotation=True)
    parts = [o]
    for p in (a, b):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=r, location=p)
        parts.append(bpy.context.active_object)
    return join(parts, name)


def join(objs, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    return o


def fuse(o, voxel, smooth=6, faces=0):
    m = o.modifiers.new("remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier="remesh")
    s = o.modifiers.new("smooth", "SMOOTH")
    s.iterations = smooth
    s.factor = 0.6
    bpy.ops.object.modifier_apply(modifier="smooth")
    if faces:
        d = o.modifiers.new("dec", "DECIMATE")
        d.ratio = min(1.0, faces / max(1, len(o.data.polygons)))
        bpy.ops.object.modifier_apply(modifier="dec")
    bpy.ops.object.shade_smooth()
    return o


def cut(o, cutter):
    b = o.modifiers.new("cut", "BOOLEAN")
    b.operation = "DIFFERENCE"
    b.object = cutter
    b.solver = "EXACT"
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier="cut")
    bpy.data.objects.remove(cutter)


def material(o, name, rgb, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    o.data.materials.clear()
    o.data.materials.append(m)


def export(o, path):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_normals=True, export_texcoords=False,
                              export_materials="EXPORT")
    print("wrote", path, len(o.data.polygons), "faces")


# ---------------------------------------------------------------- head (face toward -Y in Blender)
clear()
parts = [
    ellipsoid((0, 0.10, 0.05), (0.50, 0.46, 0.52), "cranium"),
    ellipsoid((0, 0.18, 0.42), (0.16, 0.36, 0.16), "crest"),
    capsule((-0.36, -0.30, 0.18), (0.36, -0.30, 0.18), 0.105, "brow"),
    ellipsoid((0, -0.36, -0.22), (0.36, 0.26, 0.24), "muzzle"),
    ellipsoid((0, -0.50, -0.08), (0.17, 0.10, 0.10), "nose"),
    ellipsoid((-0.33, -0.16, -0.10), (0.18, 0.20, 0.22), "cheekL"),
    ellipsoid((0.33, -0.16, -0.10), (0.18, 0.20, 0.22), "cheekR"),
    ellipsoid((0, -0.22, -0.46), (0.30, 0.22, 0.12), "chin"),
]
head = fuse(join(parts, "head"), 0.014, smooth=8)
for sx in (-1, 1):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.105, location=(0.165 * sx, -0.33, 0.06))
    cut(head, bpy.context.active_object)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, -0.58, -0.30))
mouth = bpy.context.active_object
mouth.scale = (0.30, 0.10, 0.012)
bpy.ops.object.transform_apply(scale=True)
cut(head, mouth)
d = head.modifiers.new("dec", "DECIMATE")
d.ratio = min(1.0, 40000 / max(1, len(head.data.polygons)))
bpy.ops.object.modifier_apply(modifier="dec")
bpy.ops.object.shade_smooth()
material(head, "skin", (0.035, 0.03, 0.028), 0.55)
export(head, os.path.join(OUT, "probe-head.glb"))

# ---------------------------------------------------------------- eye (centre at the origin; iris toward -Y)
clear()
eye = ellipsoid((0, 0, 0), (0.07, 0.07, 0.07), "eye")
material(eye, "eye", (0.25, 0.12, 0.05), 0.08)
export(eye, os.path.join(OUT, "probe-eye.glb"))

# ---------------------------------------------------------------- hand on a bar
# Bar axis = Blender Z through the origin (glTF +Y). Camera side = Blender -Y (glTF +Z). The hand is behind the bar
# (+Y), its fingers come round the bar's sides and over its front, fingertips ending on the far side.
clear()
BAR_R = 0.055
parts = []
finger_r = [0.050, 0.054, 0.052, 0.044]
finger_z = [0.17, 0.055, -0.06, -0.165]
for k in range(4):
    r = finger_r[k]
    R = BAR_R + r * 0.92
    # angles in the XY plane measured from +Y (behind): the finger starts behind-right, wraps over the front (-Y)
    # and ends on the left side, three phalanges.
    angs = [math.radians(a) for a in (-70, -10, 75, 165, 235)]
    pts = []
    for i, a in enumerate(angs):
        rr = R if i > 0 else R + 0.07          # the knuckle stands off the bar
        pts.append((rr * math.sin(a) * -1.0, rr * math.cos(a), finger_z[k] + 0.006 * i))
    for i in range(len(pts) - 1):
        rad = r * (1.0 - 0.12 * i)
        parts.append(capsule(pts[i], pts[i + 1], rad, "f%d_%d" % (k, i)))
    parts.append(ellipsoid(pts[1], (r * 1.25, r * 1.25, r * 1.15), "knuckle%d" % k))
# palm mass behind the bar, back of the hand rising toward the wrist
parts.append(ellipsoid((0.10, 0.20, 0.0), (0.16, 0.10, 0.25), "palm"))
parts.append(capsule((0.14, 0.26, -0.05), (0.34, 0.55, -0.12), 0.12, "wrist"))
# thumb from the palm's lower edge round the bar's other side
parts.append(capsule((-0.04, 0.17, -0.22), (-0.10, 0.04, -0.25), 0.045, "thumb0"))
parts.append(capsule((-0.10, 0.04, -0.25), (-0.06, -0.06, -0.27), 0.040, "thumb1"))
hand = fuse(join(parts, "hand"), 0.008, smooth=5, faces=30000)
material(hand, "skin", (0.04, 0.034, 0.03), 0.5)
export(hand, os.path.join(OUT, "probe-hand.glb"))
# the right hand: the same hand mirrored across the bar (x -> -x), winding and normals fixed
hand.scale = (-1.0, 1.0, 1.0)
bpy.ops.object.select_all(action="DESELECT")
hand.select_set(True)
bpy.context.view_layer.objects.active = hand
bpy.ops.object.transform_apply(scale=True)
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode="OBJECT")
export(hand, os.path.join(OUT, "probe-hand-r.glb"))

