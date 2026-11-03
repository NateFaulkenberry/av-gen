"""Hero H3 (owner note 2026-11-03): a copy of pass2/build_sasquatch.py whose sitting pose can be raised and moved back
(env SIT_HIPS="dy,dz" in units of the height), so the sitting individual rests ON its stone instead of sinking into it.
Everything else is the original builder, unchanged.

Original docstring: Art Pass 2, M1: a Sasquatch for the canyon encounters, built headless in Blender from the owner-downloaded "Bigfoot"
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
# M3b (owner: three copies of one Sasquatch look bad): three individuals -- build, height, fur colour and length, the
# head, age -- each with its own base pose (standing, crouching, sitting), feet planted by leg IK
V = [dict(height=2.35, fur=(0.045, 0.028, 0.017), grey=None, furlen=1.0, lean=6.0,        # A: a dark-brown adult
          shoulders=1.0, belly=0.0, head=1.0, arms=1.0, pose="stand"),
     dict(height=2.55, fur=(0.03, 0.025, 0.021), grey=(0.2, 0.19, 0.18), furlen=1.45, lean=12.0,  # B: an old silverback
          shoulders=1.2, belly=0.05, head=1.07, arms=1.08, pose="crouch"),
     dict(height=2.2, fur=(0.085, 0.034, 0.015), grey=None, furlen=0.8, lean=3.0,         # C: young, lean, auburn
          shoulders=0.9, belly=-0.015, head=0.94, arms=1.05, pose="sit")][variant % 3]

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


def bump(x, a, b):
    """0 outside [a, b], 1 in the middle, smooth."""
    if x <= a or x >= b:
        return 0.0
    t = (x - a) / (b - a)
    return math.sin(math.pi * t) ** 2


# the individual's build, warped into the T-posed scan before the rig is fitted
hc = Vector((0.0, sum(v.co.y for v in me.vertices if v.co.z > 0.9 * H) / max(1, sum(1 for v in me.vertices if v.co.z > 0.9 * H)), 0.93 * H))
for v in me.vertices:
    c = v.co
    w = bump(c.z, 0.55 * H, 0.9 * H) * (1.0 if abs(c.x) < 0.3 * H else 0.0)
    c.x *= 1.0 + (V["shoulders"] - 1.0) * w
    if c.y < 0.0:
        c.y -= V["belly"] * H * bump(c.z, 0.36 * H, 0.64 * H) * max(0.0, 1.0 - abs(c.x) / (0.22 * H))
    if abs(c.x) > 0.14 * H and c.z > 0.6 * H:
        c.x = math.copysign(0.14 * H + (abs(c.x) - 0.14 * H) * V["arms"], c.x)
    if c.z > 0.84 * H:
        k = bump(c.z, 0.84 * H, 1.2 * H) if c.z < 0.98 * H else 1.0
        f = 1.0 + (V["head"] - 1.0) * min(1.0, k * 2.0)
        c.x, c.y, c.z = hc.x + (c.x - hc.x) * f, hc.y + (c.y - hc.y) * f, hc.z + (c.z - hc.z) * f
zs = [v.co.z for v in me.vertices]
s2 = V["height"] / (max(zs) - min(zs))
me.transform(Matrix.Translation((0, 0, -min(zs) * s2)) @ Matrix.Scale(s2, 4))


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

# 3. the strand texture, the fur colour baked in (the silverback has a second, grey one for its head and back)
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
fur_grey = None
if V["grey"] is not None:
    fur_grey = fur.copy()
    fur_grey.name = "sasquatch_fur_grey"
    img_g = img.copy()
    img_g.name = "sasquatch_strands_grey"
    pg = list(img.pixels)
    gk = [V["grey"][i] / max(V["fur"][i], 1e-4) for i in range(3)]
    for i in range(0, len(pg), 4):
        for c in range(3):
            pg[i + c] = min(1.0, pg[i + c] * gk[c])
    img_g.pixels = pg
    img_g.pack()
    for nd in fur_grey.node_tree.nodes:
        if nd.type == "TEX_IMAGE":
            nd.image = img_g

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
    # the silverback greys on the head, shoulders and back, and a little everywhere
    grey = fur_grey is not None and (rng.random() < 0.18 or (zr > 0.58 and (pt.y > -0.02 * H or zr > 0.8) and rng.random() < 0.85))
    for (i0, i1, i2, i3), (v0, v1) in (((0, 1, 3, 2), (0.0, 0.5)), ((2, 3, 5, 4), (0.5, 1.0))):
        face = cards.faces.new((ring[i0], ring[i1], ring[i3], ring[i2]))
        face.material_index = 1 if grey else 0
        for loop, (uu, vv) in zip(face.loops, ((u0, v0), (u0 + 0.25, v0), (u0 + 0.25, v1), (u0, v1))):
            loop[uvl].uv = (uu, vv)
cme = bpy.data.meshes.new("sasquatch_fur")
cards.to_mesh(cme)
fur_obj = bpy.data.objects.new("sasquatch_fur", cme)
bpy.context.scene.collection.objects.link(fur_obj)
fur_obj.data.materials.append(fur)
if fur_grey is not None:
    fur_obj.data.materials.append(fur_grey)
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

# 5. the idle. M3 built the upper-body motion (owner: "looks good"); M3b keeps it and fixes what it got wrong:
# - the feet: the weight shift moved the hips and the legs went with them, so the figure slid. The legs now solve by IK
#   to targets fixed on the ground (and the feet hold their angle), so every shift comes from the hips over planted
#   feet; the result is baked, and the feet's drift across the loop is measured and printed ("FEET ...");
# - the scratch: the arm went back into the body. It now comes up in front of the chest to the jaw;
# - the base pose: one per individual -- A stands with its weight on one leg, B crouches on its haunches with its
#   forearms over its knees, C sits (on a boulder the scene places behind it).
# A 24 s seamless loop: sums of sines with whole cycles per loop, plus held beats, different per individual.
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode="POSE")
fps, loop = 24, 24.0
frames = int(fps * loop)
bpy.context.scene.render.fps = fps
bpy.context.scene.frame_start, bpy.context.scene.frame_end = 0, frames
for b_ in pb:
    b_.rotation_mode = "QUATERNION"


def wave(t, cycles, phase=0.0):
    return math.sin(2.0 * math.pi * (cycles * t / loop) + phase)


def beat(t, start, rise, hold, fall):
    def ease(x):
        x = min(max(x, 0.0), 1.0)
        return x * x * (3 - 2 * x)
    return ease((t - start) / rise) * (1.0 - ease((t - start - rise - hold) / fall))


# the IK: targets at the ankles' rest positions, poles ahead of the knees, the feet holding their rest orientation
ik_targets = {}
for side in ("L", "R"):
    ank = arm_obj.matrix_world @ arm_obj.data.bones[f"foot.{side}"].head_local
    knee = arm_obj.matrix_world @ arm_obj.data.bones[f"shin.{side}"].head_local
    tgt = bpy.data.objects.new(f"ik_{side}", None)
    bpy.context.scene.collection.objects.link(tgt)
    tgt.location = ank
    pole = bpy.data.objects.new(f"pole_{side}", None)
    bpy.context.scene.collection.objects.link(pole)
    pole.location = knee + Vector((0.0, -1.0 * H, 0.0))
    frot = bpy.data.objects.new(f"footrot_{side}", None)
    bpy.context.scene.collection.objects.link(frot)
    frot.matrix_world = arm_obj.matrix_world @ arm_obj.data.bones[f"foot.{side}"].matrix_local
    c = pb[f"shin.{side}"].constraints.new("IK")
    c.target, c.pole_target, c.chain_count = tgt, pole, 2
    c.pole_angle = 0.0
    c2 = pb[f"foot.{side}"].constraints.new("COPY_ROTATION")
    c2.target = frot
    ik_targets[side] = (tgt, pole, c)
bpy.context.view_layer.update()


def knee_forward(side):
    bpy.context.view_layer.update()
    k = arm_obj.matrix_world @ pb[f"shin.{side}"].head
    h = arm_obj.matrix_world @ pb[f"thigh.{side}"].head
    return h.y - k.y   # > 0: the knee ahead of the hip (-y is forward)


# the pole angle that bends the knees forward, found rather than assumed (it depends on the bones' roll)
pb["hips"].location = Vector((0.0, 0.0, 0.0))
pb["hips"].matrix = Matrix.Translation((0, 0, -0.15 * H)) @ pb["hips"].matrix
for side in ("L", "R"):
    best = max((-math.pi / 2, 0.0, math.pi / 2, math.pi), key=lambda a: (setattr(ik_targets[side][2], "pole_angle", a), knee_forward(side))[1])
    ik_targets[side][2].pole_angle = best
pb["hips"].location = Vector((0.0, 0.0, 0.0))
bpy.context.view_layer.update()

POSES = {
    # hips (dx, dy, dz) in H, hips tilt about y (deg), chest pitch, per-arm (upperarm forward, forearm forward) deg
    "stand": dict(hips=(0.035, 0.0, -0.012), tilt=3.5, chest=0.0, arm=((6.0, 8.0), (4.0, 18.0)), head=(-3.0, 0.0)),
    "crouch": dict(hips=(0.0, 0.07, -0.3), tilt=0.0, chest=26.0, arm=((38.0, 42.0), (34.0, 48.0)), head=(-24.0, 0.0)),
    "sit": dict(hips=(0.0, 0.16, -0.23), tilt=0.0, chest=10.0, arm=((24.0, 58.0), (20.0, 62.0)), head=(-8.0, 0.0)),
}
P = POSES[V["pose"]]
import os as _os
if V["pose"] == "sit" and _os.environ.get("SIT_HIPS"):
    _dy, _dz = (float(x) for x in _os.environ["SIT_HIPS"].split(","))
    P = dict(P, hips=(P["hips"][0], _dy, _dz))
    print("SIT hips", P["hips"])

V_IDLE = [
    dict(breath=6, sway=2, phase=0.0, turns=[(1.5, 1.6, 4.0, 1.8, 16.0), (12.0, 1.4, 3.0, 1.6, -11.0)], gesture="scratch"),
    dict(breath=5, sway=2, phase=1.9, turns=[(4.0, 1.8, 5.0, 2.0, -14.0), (16.5, 1.5, 2.5, 1.5, 9.0)], gesture="point"),
    dict(breath=6, sway=3, phase=3.7, turns=[(7.0, 1.5, 3.5, 1.6, 18.0), (19.0, 1.2, 2.0, 1.5, -20.0)], gesture="look"),
][variant % 3]
ph0 = V_IDLE["phase"]


def rotate(name, axis, deg):
    """Turn a bone about a WORLD axis through its head (the pose's parents first, so children follow)."""
    if abs(deg) < 1e-6:
        return
    b_ = pb[name]
    h = b_.head.copy()
    b_.matrix = Matrix.Translation(h) @ Matrix.Rotation(math.radians(deg), 4, axis) @ Matrix.Translation(-h) @ b_.matrix
    bpy.context.view_layer.update()


def pose_at(t):
    for b_ in pb:
        b_.location = Vector((0, 0, 0))
        b_.rotation_quaternion = (1, 0, 0, 0)
    bpy.context.view_layer.update()
    br = wave(t, V_IDLE["breath"], ph0)
    sw = wave(t, V_IDLE["sway"], ph0 + 0.7)
    # the hips over the planted feet: the base offset, the weight shift side to side, a little rise with the breath
    dx, dy, dz = P["hips"]
    pb["hips"].matrix = Matrix.Translation((H * (dx + 0.012 * sw), H * dy, H * (dz + 0.002 * br))) @ pb["hips"].matrix
    bpy.context.view_layer.update()
    rotate("hips", "Y", P["tilt"] + 2.0 * sw)
    rotate("spine", "Y", -(P["tilt"] + 2.0 * sw) * 0.8)        # the spine counters the hips: the shoulders stay level
    rotate("spine", "X", P["chest"] * 0.5 + 1.0 * br)
    rotate("chest", "X", P["chest"] * 0.5 + 2.4 * br)
    yaw = sum(deg * beat(t, st, ri, ho, fa) for st, ri, ho, fa, deg in V_IDLE["turns"])
    g = V_IDLE["gesture"]
    look = beat(t, 13.5, 1.4, 1.8, 1.6) if g == "look" else 0.0
    rotate("chest", "Z", 9.0 * look)
    rotate("neck", "X", P["head"][0] * 0.5 + 2.0 * wave(t, 3, ph0 + 2.1))
    rotate("neck", "Z", yaw * 0.3)
    rotate("head", "X", P["head"][0] * 0.5)
    rotate("head", "Z", yaw * 0.7 + 22.0 * look)
    for side, sg, k in (("L", 1.0, 0.0), ("R", -1.0, 1.4)):
        (ua, fa) = P["arm"][0 if side == "L" else 1]
        rotate(f"shoulder.{side}", "Y", -sg * 1.2 * br)
        # (about world X a hanging limb swings BACK for a positive angle: forward is negative)
        rotate(f"upperarm.{side}", "X", -(ua + 3.5 * wave(t, 2, ph0 + k)))
        rotate(f"upperarm.{side}", "Y", -sg * 2.0 * wave(t, 3, ph0 + k + 0.5))
        rotate(f"forearm.{side}", "X", -(fa + 5.0 * (0.5 + 0.5 * wave(t, 2, ph0 + k + 1.0))))
    if g == "scratch":
        # in front of the body: the right forearm comes up across the chest and scratches it, a few slow strokes
        s_ = beat(t, 8.0, 1.1, 1.8, 1.2)
        rotate("upperarm.R", "X", -26.0 * s_)
        rotate("forearm.R", "X", (-84.0 + 7.0 * math.sin(2.0 * math.pi * 1.6 * t)) * s_)
        rotate("head", "X", 8.0 * s_)
    elif g == "point":
        # the old one reaches toward the fire, palm down, and settles back
        s_ = beat(t, 10.0, 1.0, 1.4, 1.3)
        rotate("upperarm.L", "X", -28.0 * s_)
        rotate("forearm.L", "X", 30.0 * s_)


hand_front = []
for fr in range(0, frames + 1, 3):
    bpy.context.scene.frame_set(fr)
    pose_at(fr / fps)
    for b_ in pb:
        b_.keyframe_insert("location", frame=fr)
        b_.keyframe_insert("rotation_quaternion", frame=fr)
    if V_IDLE["gesture"] == "scratch" and 9.4 <= fr / fps <= 10.6:
        hand = arm_obj.matrix_world @ pb["hand.R"].tail
        chest = arm_obj.matrix_world @ pb["chest"].tail
        hand_front.append(chest.y - hand.y)   # > 0: the hand is in front of the chest
if hand_front:
    print("SCRATCH hand ahead of the chest: min %.3f m" % min(hand_front))
# bake the IK into plain keys, then drop the constraints and helpers
bpy.ops.pose.select_all(action="SELECT")
bpy.ops.nla.bake(frame_start=0, frame_end=frames, step=3, only_selected=False, visual_keying=True,
                 clear_constraints=True, use_current_action=True, bake_types={"POSE"})
for side in ("L", "R"):
    for nm in (f"ik_{side}", f"pole_{side}", f"footrot_{side}"):
        bpy.data.objects.remove(bpy.data.objects[nm])
arm_obj.animation_data.action.name = "idle"
# the evidence: the feet across the loop
drift = {"L": [], "R": []}
base = {}
for fr in range(0, frames + 1, 6):
    bpy.context.scene.frame_set(fr)
    for side in ("L", "R"):
        p_ = arm_obj.matrix_world @ pb[f"foot.{side}"].head
        base.setdefault(side, p_.copy())
        drift[side].append((p_ - base[side]).length)
print("FEET max drift over the loop: L %.4f m, R %.4f m" % (max(drift["L"]), max(drift["R"])))
import json as _json
_json.dump({"variant": variant, "pose": V["pose"], "foot_drift_max_m": {k: max(v) for k, v in drift.items()},
            "foot_drift_samples_m": drift}, open(out + ".feet.json", "w"))
bpy.ops.object.mode_set(mode="OBJECT")
bpy.context.scene.frame_start, bpy.context.scene.frame_end = 0, frames
bpy.ops.export_scene.gltf(filepath=out, export_animations=True, export_animation_mode="ACTIONS")


def mask_fur(path):
    """M1b: the fur cards alpha-tested (glTF MASK), not blended. Blender 5's exporter writes BLEND whatever the
    material asks, and blended cards let the bright ground behind a back-lit Sasquatch show through its fur: it read
    pale and cottony."""
    import json as _json, struct as _struct
    b = open(path, "rb").read()
    n = _struct.unpack("<I", b[12:16])[0]
    js = _json.loads(b[20:20 + n])
    rest = b[20 + n:]
    for m in js.get("materials", []):
        if m.get("name") == "sasquatch_fur":
            m["alphaMode"] = "MASK"
            m["alphaCutoff"] = 0.45
    enc = _json.dumps(js, separators=(",", ":")).encode()
    enc += b" " * ((-len(enc)) % 4)
    total = 12 + 8 + len(enc) + len(rest)
    open(path, "wb").write(_struct.pack("<III", 0x46546C67, 2, total) + _struct.pack("<II", len(enc), 0x4E4F534A) + enc + rest)


mask_fur(out)
print("SASQUATCH", out, "height", H, "verts", len(me.vertices), "cards", N)
