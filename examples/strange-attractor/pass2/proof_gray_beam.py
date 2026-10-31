"""Art Pass 2, Gate R2 proof: the Gray (an MPFB slim base with the cranium enlarged, almond eyes), the forehead
triangle (purple glowing interior, gold rim) and a purple beam toward the camera, exported as glb for AV Gen.

    BLENDER_USER_RESOURCES=<dir> Blender --background <cast>/gray-base.blend --python proof_gray_beam.py -- <out.glb>
"""
import math
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

out = sys.argv[sys.argv.index("--") + 1]
body = next(o for o in bpy.data.objects if o.type == "MESH" and "body" in [g.name for g in o.vertex_groups] or (o.type == "MESH" and len(o.data.vertices) > 10000))

# bake MPFB's targets (shape keys) into the mesh so the edits below are the final shape
bpy.context.view_layer.objects.active = body
body.select_set(True)
if body.data.shape_keys:
    bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
# the cranium: the head group's vertices above the eyes swell outward and up (a Gray's oversized smooth skull)
vg = body.vertex_groups.get("head")
idx = vg.index if vg else None
me = body.data
head = [v for v in me.vertices if idx is not None and any(g.group == idx and g.weight > 0.5 for g in v.groups)]
lo = min(v.co.z for v in head); hi = max(v.co.z for v in head)
cx = sum(v.co.x for v in head) / len(head); cy = sum(v.co.y for v in head) / len(head)
eye_z = lo + (hi - lo) * 0.55
for v in head:
    t = max(0.0, (v.co.z - eye_z) / (hi - eye_z))
    s = 1.0 + 0.55 * t ** 0.8
    v.co.x = cx + (v.co.x - cx) * s
    v.co.y = cy + (v.co.y - cy) * (1.0 + 0.35 * t)
    v.co.z = v.co.z + (v.co.z - eye_z) * 0.45 * t
    # the jaw and chin narrow
    if v.co.z < eye_z:
        k = (eye_z - v.co.z) / (eye_z - lo)
        v.co.x = cx + (v.co.x - cx) * (1.0 - 0.35 * k)
skin = bpy.data.materials.new("gray_skin")
skin.use_nodes = True
b = skin.node_tree.nodes["Principled BSDF"]
b.inputs["Base Color"].default_value = (0.42, 0.44, 0.45, 1)
b.inputs["Roughness"].default_value = 0.45
me.materials.clear()
me.materials.append(skin)
for o in list(bpy.data.objects):
    if o.type == "MESH" and o is not body:
        bpy.data.objects.remove(o)

face_y = min(v.co.y for v in head)   # MPFB faces -y
front = Vector((cx, face_y, eye_z))


def mat(name, color, emit=0.0, metal=0.0, rough=0.4):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Metallic"].default_value = metal
    p.inputs["Roughness"].default_value = rough
    if emit > 0:
        p.inputs["Emission Color"].default_value = (*color, 1)
        p.inputs["Emission Strength"].default_value = emit
    return m


def add(name, bm, material, parent=body):
    m = bpy.data.meshes.new(name)
    bm.to_mesh(m)
    o = bpy.data.objects.new(name, m)
    bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(material)
    return o

h = hi - lo
# almond eyes: flattened, tilted black ellipsoids
for side in (-1, 1):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((h * 0.085, h * 0.03, h * 0.045, 1)), verts=bm.verts)
    bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(-25 * side), 3, "Y"))
    bmesh.ops.translate(bm, verts=bm.verts, vec=(cx + side * h * 0.075, face_y + h * 0.03, eye_z - h * 0.02))
    add(f"eye{side}", bm, mat("eye", (0.01, 0.01, 0.012), metal=0.0, rough=0.05))
# the forehead triangle: a gold rim and a glowing purple interior, set into the skin
tz = eye_z + h * 0.16
r_out, r_in = h * 0.07, h * 0.05
def tri(r, y):
    return [Vector((cx + r * math.sin(a), y, tz + r * math.cos(a))) for a in (0, 2.094, 4.189)]
bm = bmesh.new()
outer = [bm.verts.new(p) for p in tri(r_out, face_y - h * 0.012)]
inner = [bm.verts.new(p) for p in tri(r_in, face_y - h * 0.014)]
for i in range(3):
    j = (i + 1) % 3
    bm.faces.new((outer[i], outer[j], inner[j], inner[i]))
add("triangle_rim", bm, mat("gold", (1.0, 0.72, 0.25), emit=2.0, metal=1.0, rough=0.25))
bm = bmesh.new()
inn = [bm.verts.new(p) for p in tri(r_in, face_y - h * 0.013)]
bm.faces.new(inn)
add("triangle_core", bm, mat("violet", (0.62, 0.18, 1.0), emit=25.0))
# the beam: a narrow cone out of the triangle toward -y (the viewer), built ring by ring
bm = bmesh.new()
n = 24
near = [bm.verts.new((cx + r_in * 0.5 * math.cos(2 * math.pi * k / n), face_y - h * 0.02, tz + r_in * 0.5 * math.sin(2 * math.pi * k / n))) for k in range(n)]
far = [bm.verts.new((cx + 0.12 * math.cos(2 * math.pi * k / n), face_y - 2.4, tz + 0.12 * math.sin(2 * math.pi * k / n))) for k in range(n)]
for k in range(n):
    j = (k + 1) % n
    bm.faces.new((near[k], near[j], far[j], far[k]))
add("beam", bm, mat("beam", (0.55, 0.15, 1.0), emit=8.0))
bpy.ops.export_scene.gltf(filepath=out, export_apply=True)  # applies the helper mask (not the armature)
print("GRAY", out, "head height", h, "front", tuple(front))
