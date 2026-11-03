"""The deep ocean's animals (Art Pass 2, M3d): procedural, rigged and animated, nothing downloaded.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_sealife.py -- assets/strange-attractor/build

Writes, each facing +z (glTF), its loop a clip named "swim":
- fish.glb: a 1 m fish (a lofted spindle, a forked tail, dorsal and pectoral fins), four spine bones; the swim is a
  travelling wave down the body, the tail sweeping widest (2 s loop);
- ray.glb: a 2.4 m ray (a flat diamond disc with a whip tail), its wing bones flapping in a slow wave (4 s loop);
- jelly.glb: a 0.8 m jellyfish (a domed bell, trailing tentacles), its bell bone pulsing (contract and relax, 2.4 s),
  the bell softly emissive.
"""
import bpy
import bmesh
import math
import sys
from mathutils import Vector

out = sys.argv[sys.argv.index("--") + 1]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def mat(name, col, rough=0.5, emit=None, strength=0.0, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*col, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1.0)
        b.inputs["Emission Strength"].default_value = strength
    return m


def armature(name, bones):
    """bones: [(name, head, tail, parent)] in Blender coords (y forward along the body)."""
    arm = bpy.data.armatures.new(name)
    ob = bpy.data.objects.new(name, arm)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    for bn, h, t, par in bones:
        e = arm.edit_bones.new(bn)
        e.head, e.tail = Vector(h), Vector(t)
        if par:
            e.parent = arm.edit_bones[par]
    bpy.ops.object.mode_set(mode="OBJECT")
    return ob


def skin(mesh_ob, arm_ob):
    mesh_ob.parent = arm_ob
    mod = mesh_ob.modifiers.new("arm", "ARMATURE")
    mod.object = arm_ob
    bpy.ops.object.select_all(action="DESELECT")
    mesh_ob.select_set(True)
    arm_ob.select_set(True)
    bpy.context.view_layer.objects.active = arm_ob
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")


def export(name, objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=f"{out}/{name}.glb", use_selection=True, export_animations=True,
                              export_apply=False)
    print("SEALIFE", name)


def keyed_loop(arm_ob, frames, pose_fn, name="swim"):
    act = bpy.data.actions.new(name)
    arm_ob.animation_data_create()
    arm_ob.animation_data.action = act
    bpy.context.scene.frame_start, bpy.context.scene.frame_end = 0, frames
    for f in range(0, frames + 1, 2):
        ph = 2 * math.pi * f / frames
        pose_fn(arm_ob, ph)
        for pb in arm_ob.pose.bones:
            pb.keyframe_insert("rotation_quaternion", frame=f)
            pb.keyframe_insert("scale", frame=f)


# ---- fish
reset()
bm = bmesh.new()
rings, seg = 18, 12
L = 1.0
verts = []
for i in range(rings + 1):
    t = i / rings
    y = L * (0.5 - t)                      # nose at +y (front)
    r = 0.13 * math.sin(math.pi * min(1.0, t * 1.05)) ** 0.8 + 0.004
    ring = []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        ring.append(bm.verts.new((r * 0.55 * math.cos(a), y, r * math.sin(a))))
    verts.append(ring)
for i in range(rings):
    for k in range(seg):
        bm.faces.new((verts[i][k], verts[i][(k + 1) % seg], verts[i + 1][(k + 1) % seg], verts[i + 1][k]))
# forked tail, dorsal and pectoral fins (flat)
tail = [bm.verts.new(v) for v in ((0, -0.48, 0.0), (0, -0.66, 0.17), (0, -0.6, 0.0), (0, -0.66, -0.17))]
bm.faces.new(tail)
dorsal = [bm.verts.new(v) for v in ((0, 0.12, 0.11), (0, -0.05, 0.22), (0, -0.16, 0.1))]
bm.faces.new(dorsal)
for sx in (-1, 1):
    pec = [bm.verts.new(v) for v in ((sx * 0.06, 0.18, -0.04), (sx * 0.2, 0.06, -0.08), (sx * 0.06, 0.08, -0.05))]
    bm.faces.new(pec)
me = bpy.data.meshes.new("fish")
bm.to_mesh(me)
fish = bpy.data.objects.new("fish", me)
bpy.context.scene.collection.objects.link(fish)
for p in me.polygons:
    p.use_smooth = True
me.materials.append(mat("fish_skin", (0.32, 0.4, 0.42), 0.35, metal=0.4))
arm = armature("fish_rig", [("b0", (0, 0.5, 0), (0, 0.2, 0), None), ("b1", (0, 0.2, 0), (0, -0.05, 0), "b0"),
                            ("b2", (0, -0.05, 0), (0, -0.3, 0), "b1"), ("b3", (0, -0.3, 0), (0, -0.66, 0), "b2")])
skin(fish, arm)


def fish_pose(a, ph):
    from mathutils import Quaternion
    for i, n in enumerate(("b0", "b1", "b2", "b3")):
        amp = (0.04, 0.12, 0.22, 0.38)[i]
        a.pose.bones[n].rotation_mode = "QUATERNION"
        a.pose.bones[n].rotation_quaternion = Quaternion((0, 0, 1), amp * math.sin(ph - i * 0.9))


keyed_loop(arm, 48, fish_pose)
export("fish", [fish, arm])

# ---- ray
reset()
bm = bmesh.new()
nx, ny = 16, 12
grid = []
for j in range(ny + 1):
    y = 0.6 - 1.2 * j / ny
    row = []
    for i in range(nx + 1):
        x = -1.2 + 2.4 * i / nx
        w = 1.2 * (1 - abs(y) / 0.6) ** 0.7 if abs(y) < 0.6 else 0.0
        xx = max(-w, min(w, x)) if w > 0 else 0.0
        z = 0.08 * (1 - (xx / 1.2) ** 2) * (1 - abs(y) / 0.6)
        row.append(bm.verts.new((xx, y, z)))
    grid.append(row)
for j in range(ny):
    for i in range(nx):
        try:
            bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
        except ValueError:
            pass
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
tailv = [bm.verts.new(v) for v in ((-0.02, -0.55, 0.0), (0.02, -0.55, 0.0), (0.0, -1.6, 0.0))]
bm.faces.new(tailv)
me = bpy.data.meshes.new("ray")
bm.to_mesh(me)
ray = bpy.data.objects.new("ray", me)
bpy.context.scene.collection.objects.link(ray)
for p in me.polygons:
    p.use_smooth = True
me.materials.append(mat("ray_skin", (0.18, 0.2, 0.22), 0.5))
arm = armature("ray_rig", [("body", (0, 0.4, 0), (0, -0.4, 0), None), ("wl", (-0.1, 0, 0), (-1.2, 0, 0), "body"),
                           ("wr", (0.1, 0, 0), (1.2, 0, 0), "body"), ("tail", (0, -0.5, 0), (0, -1.6, 0), "body")])
skin(ray, arm)


def ray_pose(a, ph):
    from mathutils import Quaternion
    for n, sgn in (("wl", 1), ("wr", -1)):
        a.pose.bones[n].rotation_mode = "QUATERNION"
        a.pose.bones[n].rotation_quaternion = Quaternion((0, 1, 0), sgn * 0.45 * math.sin(ph))
    a.pose.bones["tail"].rotation_mode = "QUATERNION"
    a.pose.bones["tail"].rotation_quaternion = Quaternion((0, 0, 1), 0.15 * math.sin(ph * 2))


keyed_loop(arm, 96, ray_pose)
export("ray", [ray, arm])

# ---- jellyfish
reset()
bm = bmesh.new()
bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=0.4)
for v in list(bm.verts):
    if v.co.z < -0.02:
        bm.verts.remove(v)
for v in bm.verts:
    v.co.z *= 0.75
for k in range(10):
    a = 2 * math.pi * k / 10
    r = 0.3
    base = Vector((r * math.cos(a), r * math.sin(a), 0.0))
    prev = None
    for s in range(9):
        p = base + Vector((0.03 * math.sin(s + k), 0.03 * math.cos(s * 1.3 + k), -0.18 * s))
        q = p + Vector((0.012, 0, 0))
        v1, v2 = bm.verts.new(p), bm.verts.new(q)
        if prev:
            bm.faces.new((prev[0], prev[1], v2, v1))
        prev = (v1, v2)
me = bpy.data.meshes.new("jelly")
bm.to_mesh(me)
jel = bpy.data.objects.new("jelly", me)
bpy.context.scene.collection.objects.link(jel)
for p in me.polygons:
    p.use_smooth = True
me.materials.append(mat("jelly_bell", (0.5, 0.75, 0.95), 0.2, emit=(0.35, 0.75, 1.0), strength=2.5))
arm = armature("jelly_rig", [("bell", (0, 0, -0.3), (0, 0, 0.35), None), ("trail", (0, 0, -0.3), (0, 0, -1.6), "bell")])
skin(jel, arm)


def jelly_pose(a, ph):
    s = 1.0 - 0.18 * max(0.0, math.sin(ph)) ** 2
    a.pose.bones["bell"].scale = (s, s, 1.0 + 0.5 * (1.0 - s))
    a.pose.bones["trail"].scale = (1.0, 1.0 + 0.12 * math.sin(ph - 1.0), 1.0)


keyed_loop(arm, 72, jelly_pose)
export("jelly", [jel, arm])
