"""The owner's Sketchfab alien (Graphetta, "Realistic Alien (Grey) Mid Poli", CC BY 4.0; hero-assets.md) as a drop-in for
the night camp's Gray: rigged, skinned and driven by the Gray's own clip, for the side-by-side compare.

    Blender -b --factory-startup --python examples/strange-attractor/hero/rig_sketchfab_alien.py -- \
        assets/strange-attractor/hero/sources/sketchfab-graphetta-alien-grey-mid.glb \
        assets/strange-attractor/build/gray-m3e.glb assets/strange-attractor/hero/alien-sketchfab-v1.glb

The download is a static A-pose sculpt (79k tris, a separate eye mesh, 4K base colour, metal-roughness and normal
maps), with no rig and no animation. This script:
1. scales it to the Gray's height in the Gray's glb units (2.888) and stands it at the origin, so the scene's node
   (position, rotation 54.7, scale 0.68) is unchanged; only the asset path changes;
2. fits a clean 22-bone biped to landmarks measured on the mesh (the crotch, the leg and arm centrelines by slicing,
   the neck's narrowest section, the skull) and skins it with Blender's bone heat; the eyes ride the head;
3. retargets the Gray's clip "mixamo.com" (96 frames, 4 s) onto it in world space: the hips take the source's
   rotation relative to its rest and its translation; every other bone is turned, parent first, to the source bone's
   world direction (so different rest poses -- the Gray's arms-down bind, this A-pose -- do not matter); the hips are
   then lowered or raised so the planted ankle sits where the Gray's does. The clip keeps the name "mixamo.com".
Prints the foot drift over the loop and the hand-to-body clearance.
"""
import json
import math
import struct
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

src_path, gray_path, out = sys.argv[sys.argv.index("--") + 1:][:3]
TARGET_H = 2.888

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# ---------------------------------------------------------------- the alien, baked to world, standing at the origin
bpy.ops.import_scene.gltf(filepath=src_path)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
skin = max(meshes, key=lambda o: len(o.data.vertices))
eyes = [o for o in meshes if o is not skin]
for o in meshes:
    mw = o.matrix_world.copy()
    o.parent = None
    o.data.transform(mw)
    o.matrix_world = Matrix.Identity(4)
for o in list(bpy.data.objects):
    if o.type != "MESH":
        bpy.data.objects.remove(o)
# weld the vertices glTF split at UV seams (UVs stay per loop): split copies take different skin weights and the
# seams open when the limbs move (the H2 preview's dark rings at the shoulders and upper arms)
import bmesh as _bm
for o in meshes:
    b_ = _bm.new()
    b_.from_mesh(o.data)
    _bm.ops.remove_doubles(b_, verts=b_.verts, dist=1e-5)
    b_.to_mesh(o.data)
    b_.free()
P = np.array([v.co[:] for v in skin.data.vertices])
lo, hi = P.min(0), P.max(0)
s = TARGET_H / (hi[2] - lo[2])
T = Matrix.Scale(s, 4) @ Matrix.Translation((-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2]))
for o in meshes:
    o.data.transform(T)
P = np.array([v.co[:] for v in skin.data.vertices])
H = TARGET_H
print("ALIEN height", H, "bounds", P.min(0).round(3), P.max(0).round(3))


# ---------------------------------------------------------------- landmarks (the alien faces -y, its left is +x)
def centroid(mask):
    return P[mask].mean(0) if mask.any() else None


def slice_z(z, band, xsel=None):
    m = np.abs(P[:, 2] - z) < band
    if xsel is not None:
        m &= xsel(P[:, 0])
    return m


# the crotch: the lowest z at which the body's midline has mesh
mid = np.abs(P[:, 0]) < 0.015 * H
crotch = P[mid & (P[:, 2] > 0.2 * H), 2].min()
# the torso's half-width just under the armpits (arms excluded by the A-pose's gap) and the shoulder line
torso = []
for z in np.linspace(crotch + 0.05 * H, 0.85 * H, 60):
    m = slice_z(z, 0.006 * H)
    xs = np.sort(np.abs(P[m, 0]))
    # the torso is the inner run of x without a gap > 2% H
    gap = np.where(np.diff(xs) > 0.02 * H)[0]
    half = xs[gap[0]] if len(gap) else xs.max()
    torso.append((z, half, len(gap) > 0))
arm_z = [z for z, _, g in torso if g]
shoulder_z = max(arm_z) if arm_z else 0.72 * H            # the highest slice where the arm is still separate
torso_half = {round(z, 4): h for z, h, _ in torso}
# the neck: the narrowest midline section above the shoulders; the skull above it
neck = min(((z, h) for z, h, _ in torso if z > shoulder_z), key=lambda t: t[1], default=(0.78 * H, 0.04 * H))
neck_z = neck[0]
skull = P[P[:, 2] > neck_z + 0.04 * H]
head_c = skull.mean(0)
chin_z = neck_z + 0.03 * H

# legs: centroids of slices per side
def leg_point(z, sg):
    m = slice_z(z, 0.012 * H, lambda x: (sg * x > 0.005 * H) & (np.abs(x) < 0.11 * H))   # not the hanging hands
    return Vector(centroid(m))


ankle_z = 0.055 * H
hip_z = crotch + 0.035 * H
knee_z = (hip_z + ankle_z) * 0.5 + 0.01 * H
L = {}
for side, sg in (("L", 1), ("R", -1)):
    hp = leg_point(crotch - 0.03 * H, sg)
    L[f"hip.{side}"] = Vector((hp.x, hp.y, hip_z))
    L[f"knee.{side}"] = leg_point(knee_z, sg) + Vector((0, -0.012 * H, 0))
    an = leg_point(ankle_z + 0.02 * H, sg)
    L[f"ankle.{side}"] = Vector((an.x, an.y + 0.01 * H, ankle_z))
    foot = P[(P[:, 2] < 0.05 * H) & (sg * P[:, 0] > 0.0) & (np.abs(P[:, 0]) < 0.15 * H)]
    toe = foot[foot[:, 1].argmin()]
    L[f"toe.{side}"] = Vector((toe[0], toe[1], 0.015 * H))
    L[f"ball.{side}"] = L[f"ankle.{side}"].lerp(Vector((toe[0], toe[1], 0.02 * H)), 0.55)
    L[f"ball.{side}"].z = 0.025 * H
    # the arm: everything outside the torso above the crotch; its centreline by slicing along its PCA axis
    am = (sg * P[:, 0] > 0) & (P[:, 2] > 0.2 * H) & (P[:, 2] < shoulder_z + 0.02 * H)
    inner = np.where(P[:, 2] > crotch,
                     np.interp(P[:, 2], [t[0] for t in torso], [t[1] for t in torso]) + 0.006 * H, 0.13 * H)
    am &= np.abs(P[:, 0]) > inner
    A = P[am]
    c0 = A.mean(0)
    _, _, vt = np.linalg.svd(A - c0)
    ax = vt[0] if vt[0][2] < 0 else -vt[0]           # pointing down the arm
    tpar = (A - c0) @ ax
    t0, t1 = tpar.min(), tpar.max()
    def along(f):
        t = t0 + (t1 - t0) * f
        m = np.abs(tpar - t) < 0.015 * (t1 - t0) + 0.004 * H
        return Vector(A[m].mean(0))
    sh = along(0.0)
    top = Vector((sg * (np.interp(shoulder_z, [t[0] for t in torso], [t[1] for t in torso]) + 0.01 * H), sh.y,
                  shoulder_z + 0.015 * H))
    L[f"shoulder.{side}"] = top
    L[f"elbow.{side}"] = along(0.36)
    L[f"wrist.{side}"] = along(0.66)
    L[f"fingertip.{side}"] = along(0.985)
    L[f"clav.{side}"] = Vector((sg * 0.03 * H, sh.y, shoulder_z + 0.005 * H))
pel = (L["hip.L"] + L["hip.R"]) / 2
L["pelvis"] = Vector((0, pel.y, hip_z + 0.02 * H))
chest_y = centroid(slice_z(shoulder_z - 0.05 * H, 0.01 * H, lambda x: np.abs(x) < 0.05 * H))[1]
L["neck"] = Vector((0, centroid(slice_z(neck_z, 0.008 * H))[1], shoulder_z + 0.02 * H))
L["headbase"] = Vector((0, centroid(slice_z(neck_z + 0.02 * H, 0.008 * H))[1], chin_z))
L["headtop"] = Vector((0, head_c[1], skull[:, 2].max()))
spine = [L["pelvis"].lerp(L["neck"], f) for f in (0.0, 0.3, 0.6, 1.0)]
for i, p_ in enumerate(spine):
    p_.y = centroid(slice_z(p_.z, 0.01 * H, lambda x: np.abs(x) < 0.05 * H))[1] + 0.01 * H
print("LANDMARKS", {k: tuple(round(x, 3) for x in v) for k, v in L.items()}, "crotch", round(crotch, 3),
      "shoulder", round(shoulder_z, 3), "neck", round(neck_z, 3))

# ---------------------------------------------------------------- the rig
arm = bpy.data.objects.new("alien_rig", bpy.data.armatures.new("alien_rig"))
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="EDIT")
eb = arm.data.edit_bones


def bone(name, a, b, parent=None, connect=False):
    x = eb.new(name)
    x.head, x.tail = Vector(a), Vector(b)
    if parent:
        x.parent = eb[parent]
        x.use_connect = connect
    return name


bone("hips", spine[0], spine[1])
bone("spine", spine[1], spine[2], "hips", True)
bone("spine1", spine[2], spine[3] - Vector((0, 0, 0.02 * H)), "spine", True)
bone("spine2", spine[3] - Vector((0, 0, 0.02 * H)), L["neck"], "spine1", True)
bone("neck", L["neck"], L["headbase"], "spine2", True)
bone("head", L["headbase"], L["headtop"], "neck", True)
for sd in ("L", "R"):
    bone(f"shoulder.{sd}", L[f"clav.{sd}"], L[f"shoulder.{sd}"], "spine2")
    bone(f"upperarm.{sd}", L[f"shoulder.{sd}"], L[f"elbow.{sd}"], f"shoulder.{sd}", True)
    bone(f"forearm.{sd}", L[f"elbow.{sd}"], L[f"wrist.{sd}"], f"upperarm.{sd}", True)
    bone(f"hand.{sd}", L[f"wrist.{sd}"], L[f"fingertip.{sd}"], f"forearm.{sd}", True)
    bone(f"thigh.{sd}", L[f"hip.{sd}"], L[f"knee.{sd}"], "hips")
    bone(f"shin.{sd}", L[f"knee.{sd}"], L[f"ankle.{sd}"], f"thigh.{sd}", True)
    bone(f"foot.{sd}", L[f"ankle.{sd}"], L[f"ball.{sd}"], f"shin.{sd}", True)
    bone(f"toe.{sd}", L[f"ball.{sd}"], L[f"toe.{sd}"], f"foot.{sd}", True)
# rolls: a consistent frame (z toward the front, -y) so the twist the retarget inherits is sane
for b in eb:
    b.align_roll(Vector((0, -1, 0)) if abs(b.vector.normalized().y) < 0.9 else Vector((0, 0, 1)))
if "extra_bones" in globals():          # H2 (rig_alien_h2.py) adds fingers and a jaw here, before the skinning
    extra_bones(eb)
bpy.ops.object.mode_set(mode="OBJECT")

for o in bpy.context.view_layer.objects:
    o.select_set(False)
skin.select_set(True)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.parent_set(type="ARMATURE_AUTO")
heat_ok = all(len(skin.vertex_groups) > 0 for _ in [0]) and sum(1 for v in skin.data.vertices if v.groups) > 0.98 * len(skin.data.vertices)
print("SKIN bone heat:", "ok" if heat_ok else "INCOMPLETE",
      sum(1 for v in skin.data.vertices if not v.groups), "unweighted verts")
if "after_skin" in globals():           # H2: weight smoothing at the shoulders, the jaw's weights
    after_skin()
for e in eyes:
    e.parent = arm
    g = e.vertex_groups.new(name="head")
    g.add(list(range(len(e.data.vertices))), 1.0, "REPLACE")
    e.modifiers.new("Armature", "ARMATURE").object = arm

# ---------------------------------------------------------------- the source: the Gray and its clip
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=gray_path)
gobjs = [o for o in bpy.data.objects if o not in before]
garm = next(o for o in gobjs if o.type == "ARMATURE")
for o in gobjs:
    if o.type == "MESH":
        o.hide_render = True
gpb = garm.pose.bones
act = garm.animation_data.action
f0, f1 = (int(x) for x in act.frame_range)
SRC = {
    "spine": ("mixamorig:Spine_01", "mixamorig:Spine1_02"), "spine1": ("mixamorig:Spine1_02", "mixamorig:Spine2_03"),
    "spine2": ("mixamorig:Spine2_03", "mixamorig:Neck_04"), "neck": ("mixamorig:Neck_04", "mixamorig:Head_05"),
    "head": ("mixamorig:Head_05", None),
}
for sd, gs in (("L", "Left"), ("R", "Right")):
    n = {"Left": (7, 8, 9, 10, 15, 16, 17, 18), "Right": (11, 12, 13, 14, 20, 21, 22, 23)}[gs]
    nm = lambda k, i: f"mixamorig:{gs}{k}_0{i}"
    SRC[f"shoulder.{sd}"] = (nm("Shoulder", n[0]), nm("Arm", n[1]))
    SRC[f"upperarm.{sd}"] = (nm("Arm", n[1]), nm("ForeArm", n[2]))
    SRC[f"forearm.{sd}"] = (nm("ForeArm", n[2]), nm("Hand", n[3]))
    SRC[f"hand.{sd}"] = (nm("Hand", n[3]), None)
    SRC[f"thigh.{sd}"] = (nm("UpLeg", n[4]), nm("Leg", n[5]))
    SRC[f"shin.{sd}"] = (nm("Leg", n[5]), nm("Foot", n[6]))
    SRC[f"foot.{sd}"] = (nm("Foot", n[6]), nm("ToeBase", n[7]))
    SRC[f"toe.{sd}"] = (nm("ToeBase", n[7]), None)
for k, (a_, b_) in SRC.items():
    for x in (a_, b_):
        assert x is None or x in gpb, x


def gworld(name):
    return garm.matrix_world @ gpb[name].matrix


def gdir(a_, b_):
    if b_ is None:
        m = gworld(a_)
        return (m.to_3x3() @ Vector((0, 1, 0))).normalized()
    return ((garm.matrix_world @ gpb[b_].head) - (garm.matrix_world @ gpb[a_].head)).normalized()


# the source's rest frame for the hips: its bind (pose cleared)
garm.data.pose_position = "REST"
bpy.context.view_layer.update()
g_hips_rest = gworld("mixamorig:Hips_00").to_quaternion()
garm.data.pose_position = "POSE"
pb = arm.pose.bones
for b in pb:
    b.rotation_mode = "QUATERNION"
t_hips_rest = (arm.matrix_world @ arm.data.bones["hips"].matrix_local).to_quaternion()
t_hips_head = arm.data.bones["hips"].head_local.copy()
ORDER = ["spine", "spine1", "spine2", "neck", "head"] + [f"{b}.{sd}" for sd in ("L", "R") for b in
                                                        ("shoulder", "upperarm", "forearm", "hand", "thigh", "shin",
                                                         "foot", "toe")]
ORDER.sort(key=lambda n: len(arm.data.bones[n].parent_recursive))
g_ankle0 = None
plant = {}
feet = {"L": [], "R": []}
for fr in range(f0, f1 + 1):
    scene.frame_set(fr)
    for b in pb:
        b.location = (0, 0, 0)
        b.rotation_quaternion = (1, 0, 0, 0)
    bpy.context.view_layer.update()
    # hips: the source's world rotation relative to its rest, applied to ours; its translation as is
    g_h = gworld("mixamorig:Hips_00")
    rot = g_h.to_quaternion() @ g_hips_rest.inverted() @ t_hips_rest
    pos = g_h.translation
    pb["hips"].matrix = Matrix.Translation(pos) @ rot.to_matrix().to_4x4()
    bpy.context.view_layer.update()
    for name in ORDER:
        a_, b_ = SRC[name]
        want = gdir(a_, b_)
        m = pb[name].matrix
        cur = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
        q = cur.rotation_difference(want)
        h = m.translation.copy()
        pb[name].matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ m
        bpy.context.view_layer.update()
    # ground: our lower ankle to the Gray's lower ankle height
    ga = min((garm.matrix_world @ gpb[n].head).z for n in ("mixamorig:LeftFoot_017", "mixamorig:RightFoot_022"))
    ta = min((arm.matrix_world @ pb[f"foot.{sd}"].head).z for sd in ("L", "R"))
    pb["hips"].matrix = Matrix.Translation((0, 0, ga - ta)) @ pb["hips"].matrix
    bpy.context.view_layer.update()
    # planted feet: the Gray's feet do not move in this clip, so ours are pinned to where they stand on the first
    # frame -- a two-bone solve per leg in the plane the knee already bends in, the foot keeping its direction
    for sd in ("L", "R"):
        thigh, shin, foot = pb[f"thigh.{sd}"], pb[f"shin.{sd}"], pb[f"foot.{sd}"]
        fdir = (foot.matrix.to_3x3() @ Vector((0, 1, 0))).normalized()
        if sd not in plant:
            plant[sd] = (arm.matrix_world @ foot.head).copy()
        A = plant[sd]
        Hh = thigh.head.copy()
        K0 = shin.head.copy()
        l1, l2 = arm.data.bones[thigh.name].length, arm.data.bones[shin.name].length
        dv = A - Hh
        d = min(dv.length, (l1 + l2) * 0.9995)
        ang = math.acos(max(-1.0, min(1.0, (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d))))
        nrm = dv.cross(K0 - Hh)
        if nrm.length < 1e-6:
            nrm = Vector((1, 0, 0))
        nrm.normalize()
        K = Hh + (Quaternion(nrm, ang) @ dv.normalized()) * l1
        for bone_, want in ((thigh, K - Hh), (shin, Hh + dv.normalized() * d - K)):
            m = bone_.matrix
            cur = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
            q = cur.rotation_difference(want.normalized())
            h = m.translation.copy()
            bone_.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ m
            bpy.context.view_layer.update()
        m = foot.matrix
        cur = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
        q = cur.rotation_difference(fdir)
        h = m.translation.copy()
        foot.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ m
        bpy.context.view_layer.update()
    for sd in ("L", "R"):
        feet[sd].append((arm.matrix_world @ pb[f"foot.{sd}"].head).copy())
    for b in pb:
        b.keyframe_insert("rotation_quaternion", frame=fr)
        if b.name == "hips":
            b.keyframe_insert("location", frame=fr)
drift = {sd: max((p - feet[sd][0]).length for p in feet[sd]) for sd in feet}
print("FEET max drift over the clip (m, glb units):", {k: round(v, 4) for k, v in drift.items()},
      "in the scene (x0.68):", {k: round(v * 0.68, 4) for k, v in drift.items()})

# ---------------------------------------------------------------- export the alien only
for o in gobjs:
    bpy.data.objects.remove(o, do_unlink=True)
for a in list(bpy.data.actions):
    if a is not arm.animation_data.action:
        bpy.data.actions.remove(a)
arm.animation_data.action.name = "mixamo.com"     # the scene's node plays this clip by name
scene.frame_start, scene.frame_end = f0, f1
bpy.ops.wm.save_as_mainfile(filepath=out.replace(".glb", ".blend"), compress=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=out, export_animations=True, export_animation_mode="ACTIONS",
                          export_image_format="AUTO")
b = open(out, "rb").read()
n = struct.unpack("<I", b[12:16])[0]
js = json.loads(b[20:20 + n])
print("EXPORTED", out, "clips", [a["name"] for a in js.get("animations", [])], "joints",
      [len(s_["joints"]) for s_ in js.get("skins", [])], "materials", [m["name"] for m in js["materials"]])
