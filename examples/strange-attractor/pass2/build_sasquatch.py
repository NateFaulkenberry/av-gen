"""Art Pass 2, M1: a Sasquatch for the canyon encounters, built headless in Blender from the owner-downloaded "Bigfoot"
by Mat Rex (Sketchfab, CC BY 4.0; assets-pass2.md), exported as a skinned glb with an idle.

    Blender --background --factory-startup --python build_sasquatch.py -- <bigfoot.glb> <out.glb> <variant 0|1|2>

- The scan arrives in a T-pose with Blender's default cube, camera and light alongside: those are dropped and the mesh
  baked to metres (2.3-2.5 m tall by variant).
- A simple biped armature is fitted to the body's measured landmarks and skinned with automatic weights; the arms are
  then posed hanging and that pose applied as the rest pose.
- Fur: alpha-tested hair cards over the body (lying down it with gravity, longer on the shoulders, back and forearms),
  skinned by copying the body's weights; the strand colour is baked into the texture (glTF carries no node maths).
- An idle: breathing, a slow head turn, a little weight shift and arm sway (12 s loop, clip "idle"), its phase offset by
  variant so three of them never move together.
"""
import bisect
import math
import random
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

src, out = sys.argv[sys.argv.index("--") + 1:][:2]
variant = int(sys.argv[sys.argv.index("--") + 3]) if len(sys.argv) > sys.argv.index("--") + 3 else 0
rng = random.Random(29 + variant)
V = [dict(height=2.35, fur=(0.045, 0.028, 0.017), furlen=1.0, lean=6.0),
     dict(height=2.5, fur=(0.06, 0.038, 0.022), furlen=1.2, lean=10.0),
     dict(height=2.25, fur=(0.03, 0.02, 0.014), furlen=0.9, lean=4.0)][variant % 3]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
body = max((o for o in bpy.data.objects if o.type == "MESH" and o.name != "Cube"), key=lambda o: len(o.data.vertices))
# bake the body to world space, drop everything else
mw = body.matrix_world.copy()
body.parent = None
body.data.transform(mw)
body.matrix_world = Matrix.Identity(4)
for o in list(bpy.data.objects):
    if o is not body:
        bpy.data.objects.remove(o)
me = body.data
zs = [v.co.z for v in me.vertices]
z0, z1 = min(zs), max(zs)
s = V["height"] / (z1 - z0)
me.transform(Matrix.Translation((0, 0, -z0 * s)) @ Matrix.Scale(s, 4))
H = V["height"]


def band(f):
    pts = [v.co for v in me.vertices if f(v.co)]
    return pts


arms = band(lambda c: abs(c.x) > 0.3 * H)
arm_z = sum(c.z for c in arms) / len(arms)
reach = max(abs(c.x) for c in arms)
head = band(lambda c: c.z > 0.9 * H)
head_y = sum(c.y for c in head) / len(head)

# 1. the armature, fitted to those landmarks (Blender: -y is the face, +z is up)
arm_obj = bpy.data.objects.new("rig", bpy.data.armatures.new("rig"))
bpy.context.scene.collection.objects.link(arm_obj)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode="EDIT")
eb = arm_obj.data.edit_bones


def bone(name, a, b, parent=None):
    x = eb.new(name)
    x.head, x.tail = Vector(a), Vector(b)
    if parent:
        x.parent = eb[parent]
        x.use_connect = (x.parent.tail - x.head).length < 1e-4
    return name


hip = 0.47 * H
bone("hips", (0, 0, hip - 0.05 * H), (0, 0, hip + 0.08 * H))
bone("spine", (0, 0, hip + 0.08 * H), (0, -0.01 * H, 0.66 * H), "hips")
bone("chest", (0, -0.01 * H, 0.66 * H), (0, -0.02 * H, arm_z + 0.02 * H), "spine")
bone("neck", (0, -0.02 * H, arm_z + 0.02 * H), (0, head_y * 0.6, 0.86 * H), "chest")
bone("head", (0, head_y * 0.6, 0.86 * H), (0, head_y, 0.99 * H), "neck")
for side, sg in (("L", 1), ("R", -1)):
    bone(f"shoulder.{side}", (sg * 0.04 * H, -0.02 * H, arm_z), (sg * 0.14 * H, -0.02 * H, arm_z), "chest")
    bone(f"upperarm.{side}", (sg * 0.14 * H, -0.02 * H, arm_z), (sg * 0.40 * reach, -0.03 * H, arm_z), f"shoulder.{side}")
    bone(f"forearm.{side}", (sg * 0.40 * reach, -0.03 * H, arm_z), (sg * 0.80 * reach, -0.04 * H, arm_z - 0.01 * H), f"upperarm.{side}")
    bone(f"hand.{side}", (sg * 0.80 * reach, -0.04 * H, arm_z - 0.01 * H), (sg * reach, -0.04 * H, arm_z - 0.02 * H), f"forearm.{side}")
    bone(f"thigh.{side}", (sg * 0.07 * H, 0, hip - 0.04 * H), (sg * 0.075 * H, 0, 0.26 * H), "hips")
    bone(f"shin.{side}", (sg * 0.075 * H, 0, 0.26 * H), (sg * 0.075 * H, 0.005 * H, 0.04 * H), f"thigh.{side}")
    bone(f"foot.{side}", (sg * 0.075 * H, 0.005 * H, 0.04 * H), (sg * 0.075 * H, -0.08 * H, 0.01 * H), f"shin.{side}")
bpy.ops.object.mode_set(mode="OBJECT")
for o in bpy.context.view_layer.objects:
    o.select_set(False)
body.select_set(True)
arm_obj.select_set(True)
bpy.context.view_layer.objects.active = arm_obj
# skin weights by distance to the bone segments (bone heat fails on a scan this irregular): the nearest bones, falling
# off as d^-4, the two strongest kept and normalised
bpy.ops.object.parent_set(type="ARMATURE_NAME")
segs = [(b.name, b.head_local.copy(), b.tail_local.copy()) for b in arm_obj.data.bones]
groups = {name: body.vertex_groups.get(name) or body.vertex_groups.new(name=name) for name, _, _ in segs}


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
    return (a + ab * t - p).length


for v in me.vertices:
    ds = sorted(((seg_dist(v.co, a, b), name) for name, a, b in segs))[:3]
    ws = [(1.0 / max(d, 0.01) ** 4, name) for d, name in ds]
    # an arm vertex never takes a leg or the other arm (and the reverse): keep the nearest bone's side and limb
    total = sum(w for w, _ in ws[:2])
    for w, name in ws[:2]:
        groups[name].add([v.index], w / total, "REPLACE")
if not any(m.type == "ARMATURE" for m in body.modifiers):
    body.modifiers.new("Armature", "ARMATURE").object = arm_obj

# 2. the arms hanging, applied as the rest pose
bpy.ops.object.mode_set(mode="POSE")
pb = arm_obj.pose.bones
def turn_about_head(name, axis, deg):
    b = pb[name]
    h = b.head.copy()
    b.matrix = Matrix.Translation(h) @ Matrix.Rotation(math.radians(deg), 4, axis) @ Matrix.Translation(-h) @ b.matrix
    bpy.context.view_layer.update()


for side, sg in (("L", 1), ("R", -1)):
    turn_about_head(f"upperarm.{side}", "Y", 68 * sg)     # down to the sides (world axes: -y is forward)
    turn_about_head(f"upperarm.{side}", "X", 8)          # a little forward
    turn_about_head(f"forearm.{side}", "X", 14)          # elbows soft
pb["chest"].rotation_mode = "XYZ"
pb["chest"].rotation_euler = Euler((math.radians(V["lean"]), 0, 0))   # hunched forward a little
bpy.ops.object.mode_set(mode="OBJECT")
bpy.context.view_layer.objects.active = body
for m in list(body.modifiers):
    if m.type == "ARMATURE":
        bpy.ops.object.modifier_apply(modifier=m.name)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode="POSE")
bpy.ops.pose.armature_apply(selected=False)
bpy.ops.object.mode_set(mode="OBJECT")
body.modifiers.new("Armature", "ARMATURE").object = arm_obj

# 3. the strand texture, the fur colour baked in
W = 256
img = bpy.data.images.new("sasquatch_strands", W, W, alpha=True)
px = [0.0] * (W * W * 4)
for k in range(48):
    x0 = rng.uniform(0, W)
    width = rng.uniform(2.0, 4.5)
    bend = rng.uniform(-14, 14)
    tone = rng.uniform(0.6, 1.5)
    top = rng.uniform(0.75, 1.0) * W
    col = [min(1.0, c * tone * 2.2) for c in V["fur"]]
    for y in range(int(top)):
        t = y / W
        x = x0 + bend * t * t
        w = width * (1.0 - t / (top / W)) + 0.6
        for dx in range(int(-w - 1), int(w + 2)):
            xi = int(x + dx) % W
            a = max(0.0, 1.0 - abs(dx) / w)
            i = (y * W + xi) * 4
            if a > px[i + 3]:
                px[i:i + 4] = [col[0], col[1], col[2], a]
img.pixels = px
img.pack()
fur = bpy.data.materials.new("sasquatch_fur")
fur.use_nodes = True
nt = fur.node_tree
bsdf = nt.nodes["Principled BSDF"]
tex = nt.nodes.new("ShaderNodeTexImage")
tex.image = img
nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
bsdf.inputs["Roughness"].default_value = 0.95
try:
    fur.blend_method = "CLIP"
except Exception:
    pass

# 4. hair cards over the body, by area
bm = bmesh.new()
bm.from_mesh(me)
bm.faces.ensure_lookup_table()
cum, acc = [], 0.0
for f in bm.faces:
    acc += f.calc_area()
    cum.append(acc)
N = 24000
cards = bmesh.new()
uvl = cards.loops.layers.uv.new()
down = Vector((0, 0, -1))
for k in range(N):
    f = bm.faces[min(len(cum) - 1, bisect.bisect_left(cum, rng.uniform(0, acc)))]
    a, b = rng.random(), rng.random()
    if a + b > 1:
        a, b = 1 - a, 1 - b
    vs = [l.vert.co for l in f.loops]
    pt = vs[0] + (vs[1] - vs[0]) * a + (vs[2] - vs[0]) * b
    n = f.normal.normalized()
    zr = pt.z / H
    L = 0.085 * V["furlen"] * (0.6 + 0.8 * math.exp(-((zr - 0.72) / 0.16) ** 2)) * rng.uniform(0.7, 1.3)
    if zr > 0.86 and n.y < -0.5:   # the face stays almost bare
        L *= 0.25
    t = down - n * down.dot(n)
    if t.length < 1e-3:
        t = Vector((0, 1, 0)) - n * n.y
    t.normalize()
    sd = n.cross(t).normalized()
    w = L * rng.uniform(0.32, 0.45)
    root, mid, tip = pt - n * 0.004, pt + n * L * 0.28 + t * L * 0.45, pt + n * L * 0.35 + t * L * 0.95
    ring = [cards.verts.new(p) for p in (root - sd * w, root + sd * w, mid - sd * w * 0.85, mid + sd * w * 0.85,
                                         tip - sd * w * 0.6, tip + sd * w * 0.6)]
    u0 = rng.choice((0.0, 0.25, 0.5, 0.75))
    for (i0, i1, i2, i3), (v0, v1) in (((0, 1, 3, 2), (0.0, 0.5)), ((2, 3, 5, 4), (0.5, 1.0))):
        face = cards.faces.new((ring[i0], ring[i1], ring[i3], ring[i2]))
        for loop, (uu, vv) in zip(face.loops, ((u0, v0), (u0 + 0.25, v0), (u0 + 0.25, v1), (u0, v1))):
            loop[uvl].uv = (uu, vv)
cme = bpy.data.meshes.new("sasquatch_fur")
cards.to_mesh(cme)
fur_obj = bpy.data.objects.new("sasquatch_fur", cme)
bpy.context.scene.collection.objects.link(fur_obj)
fur_obj.data.materials.append(fur)
for g in body.vertex_groups:
    fur_obj.vertex_groups.new(name=g.name)
dt = fur_obj.modifiers.new("weights", "DATA_TRANSFER")
dt.object = body
dt.use_vert_data = True
dt.data_types_verts = {"VGROUP_WEIGHTS"}
dt.vert_mapping = "POLYINTERP_NEAREST"
for o in bpy.context.view_layer.objects:
    o.select_set(False)
fur_obj.select_set(True)
bpy.context.view_layer.objects.active = fur_obj
bpy.ops.object.modifier_apply(modifier="weights")
fur_obj.parent = arm_obj
fur_obj.modifiers.new("Armature", "ARMATURE").object = arm_obj

# 5. the idle
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode="POSE")
arm_obj.animation_data_create()
arm_obj.animation_data.action = bpy.data.actions.new("idle")
fps, frames = 24, 288
bpy.context.scene.render.fps = fps
off = variant * 1.7
for fr in range(0, frames + 1, 6):
    ph = 2 * math.pi * fr / fps
    for name, axis, amp, period in (("chest", "x", 2.0, 4.0), ("spine", "x", 1.0, 4.0), ("head", "z", 10.0, 12.0),
                                    ("neck", "x", 3.0, 6.0), ("upperarm.L", "x", 3.0, 6.0), ("upperarm.R", "x", 3.0, 6.0),
                                    ("hips", "y", 1.5, 12.0)):
        b = pb[name]
        b.rotation_mode = "XYZ"
        e = Euler((0, 0, 0))
        setattr(e, axis, math.radians(amp) * math.sin(ph / period + off + (1.3 if name.endswith(".R") else 0.0)))
        b.rotation_euler = e
        b.keyframe_insert("rotation_euler", frame=fr)
bpy.ops.object.mode_set(mode="OBJECT")
bpy.context.scene.frame_start, bpy.context.scene.frame_end = 0, frames
bpy.ops.export_scene.gltf(filepath=out, export_animations=True, export_animation_mode="ACTIONS")
print("SASQUATCH", out, "height", H, "verts", len(me.vertices), "cards", N)
