"""Art Pass 2, Gate R2 proof: an alien examination room (a ribbed elliptical hall with a domed ceiling, light slots in
the ribs, a pedestal gurney under a ring light) with the suited man lying on the gurney, exported as glb for AV Gen.

    BLENDER_USER_RESOURCES=<dir> Blender --background <cast>/suited-man.blend --python proof_interior.py -- <out.glb>
"""
import math
import sys

import bmesh
import bpy
from mathutils import Matrix

out = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene


def mat(name, color, metal=0.0, rough=0.4, emit=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Metallic"].default_value = metal
    p.inputs["Roughness"].default_value = rough
    if emit:
        p.inputs["Emission Color"].default_value = (*color, 1)
        p.inputs["Emission Strength"].default_value = emit
    return m


def obj(name, bm, material):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    o = bpy.data.objects.new(name, me)
    scene.collection.objects.link(o)
    o.data.materials.append(material)
    return o

hull = mat("hull", (0.32, 0.34, 0.36), metal=0.85, rough=0.32)
dark = mat("dark", (0.05, 0.055, 0.06), metal=0.7, rough=0.5)
glow = mat("glow", (0.55, 0.9, 1.0), emit=5.0)
pad = mat("pad", (0.12, 0.13, 0.14), metal=0.0, rough=0.7)

# the hall: an elliptical drum 18 x 13 m, 6 m walls, with a shallow dome (inside faces)
bm = bmesh.new()
bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=1.0)
bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < -0.05], context="VERTS")
bmesh.ops.transform(bm, matrix=Matrix.Diagonal((9.0, 6.5, 4.0, 1)), verts=bm.verts)
for v in bm.verts:
    v.co.z += 3.0 if v.co.z > 0.0 else 0.0
bmesh.ops.reverse_faces(bm, faces=bm.faces)
obj("hall", bm, hull)
bm = bmesh.new()
bmesh.ops.create_circle(bm, cap_ends=True, segments=48, radius=1.0)
bmesh.ops.transform(bm, matrix=Matrix.Diagonal((9.0, 6.5, 1.0, 1)), verts=bm.verts)
obj("floor", bm, dark)
# ribs with light slots, every 30 degrees
for k in range(12):
    a = 2 * math.pi * k / 12
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((0.35, 0.5, 6.5, 1)), verts=bm.verts)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 3.25))
    bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(a, 3, "Z"))
    bmesh.ops.translate(bm, verts=bm.verts, vec=(8.6 * math.cos(a), 6.2 * math.sin(a), 0))
    obj(f"rib{k}", bm, dark)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((0.08, 0.12, 4.5, 1)), verts=bm.verts)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 3.0))
    bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(a, 3, "Z"))
    bmesh.ops.translate(bm, verts=bm.verts, vec=(8.35 * math.cos(a), 6.0 * math.sin(a), 0))
    obj(f"slot{k}", bm, glow)
# the gurney: a pedestal, a padded table, two straps
for name, size, z, m in (("pedestal", (0.5, 0.4, 0.8), 0.4, hull), ("table", (0.8, 2.1, 0.12), 0.86, hull), ("padding", (0.72, 2.0, 0.06), 0.95, pad)):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((*size, 1)), verts=bm.verts)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, z))
    obj(name, bm, m)
for y in (-0.35, 0.35):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((0.78, 0.09, 0.32, 1)), verts=bm.verts)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, y, 1.1))
    obj(f"strap{y}", bm, dark)
# the ring light over the table
bm = bmesh.new()
bmesh.ops.create_circle(bm, segments=48, radius=1.3)
ring = obj("ring", bm, glow)
ring.location = (0, 0, 3.4)
sk = ring.modifiers.new("t", "SKIN")
# the man lies on the table: MPFB's human, laid back (head toward +y)
for o in bpy.data.objects:
    if o.type == "ARMATURE" or (o.parent is None and o.type == "MESH" and o.name not in [x.name for x in bpy.data.objects if x.name.startswith(("hall", "floor", "rib", "slot", "pedestal", "table", "padding", "strap", "ring"))]):
        if o.type in ("ARMATURE", "MESH") and o.name.startswith(("suited", "Human", "human")) or o.type == "ARMATURE":
            o.rotation_euler = (math.radians(-90), 0, math.radians(180))
            o.location = (0, -0.88, 1.0)
bpy.ops.export_scene.gltf(filepath=out, export_apply=True)
print("ROOM", out)
