"""CHRONOS, The Devourer (Gate D2): build the souls in Blender, headless, and export them for AV Gen.

docs/prototypes/chronos/12-devourer-recon-plan.md §6: MPFB CC0 adults of varied sex, build, height and age, each
posed lying flat with its own limbs (arms at the sides, arms up, one knee bent, curled, reaching, limp), the pose baked
into a static mesh so nothing is skinned at run time; a plain grey-white shroud from shoulders to mid-thigh (neutral,
never sexualised); pale grey-ivory skin and blank pale eyes, like a painted figure; under 15k triangles each.

Each soul is exported standing, its long axis along +Y (glTF up), origin at the pelvis; the scene lays it horizontal
and carries it along the procession.

    BLENDER_USER_RESOURCES=<dir with MPFB> /Applications/Blender.app/Contents/MacOS/Blender --background \
        --python examples/chronos/devourer/build_souls.py -- <out_dir> [<stills_dir>]

Writes <out_dir>/soul-<n>.glb for n = 1..8. The outputs are derived from CC0 assets but stay out of the public
repository (assets/chronos/ is gitignored); this script is original code and is committed.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Vector
from mathutils.kdtree import KDTree

from bl_ext.user_default.mpfb.services.humanservice import HumanService

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/chronos-souls"
STILLS = argv[1] if len(argv) > 1 else ""
os.makedirs(OUT, exist_ok=True)

# (gender, age, muscle, weight, height, proportions) -- MPFB macros, 0..1 (age 0.5 is 25 years, 1.0 is 90)
BODIES = [
    (1.0, 0.55, 0.5, 0.45, 0.6, 0.5),   # a man, lean
    (0.0, 0.52, 0.45, 0.5, 0.45, 0.5),  # a woman
    (1.0, 0.85, 0.3, 0.55, 0.45, 0.4),  # an old man
    (0.0, 0.8, 0.35, 0.6, 0.4, 0.45),   # an old woman
    (0.6, 0.6, 0.6, 0.3, 0.7, 0.6),     # tall and thin
    (0.2, 0.65, 0.4, 0.8, 0.35, 0.4),   # short and heavy
    (1.0, 0.5, 0.8, 0.5, 0.55, 0.6),    # strong
    (0.0, 0.58, 0.5, 0.35, 0.55, 0.55), # slight
]

# pose: bone -> euler degrees in the bone's local frame (MPFB's default rig rests in an A-pose)
SIDES = {"upperarm01.L": (0, 0, -50), "upperarm01.R": (0, 0, 50)}
POSES = [
    dict(SIDES),                                                                      # arms at the sides
    {"upperarm01.L": (0, 0, 115), "upperarm01.R": (0, 0, -115), "lowerarm01.L": (0, 0, 10),
     "lowerarm01.R": (0, 0, -10)},                                                    # arms up past the head
    {**SIDES, "upperleg01.L": (-55, 0, 0), "lowerleg01.L": (95, 0, 0)},              # one knee bent
    {"upperarm01.L": (-45, 0, -45), "upperarm01.R": (-45, 0, 45), "lowerarm01.L": (0, 0, 60),
     "lowerarm01.R": (0, 0, -60), "upperleg01.L": (-85, 0, 4), "upperleg01.R": (-85, 0, -4),
     "lowerleg01.L": (110, 0, 0), "lowerleg01.R": (110, 0, 0), "spine02": (18, 0, 0), "neck01": (20, 0, 0)},  # curled
    {"upperarm01.L": (0, 0, 120), "upperarm01.R": (0, 0, 50), "lowerarm01.L": (0, 0, 5)},  # one arm reaching
    {"upperarm01.L": (0, 0, -12), "upperarm01.R": (0, 0, 12), "lowerarm01.L": (0, 0, 20),
     "lowerarm01.R": (0, 0, -20), "upperleg01.L": (0, 0, 6), "upperleg01.R": (0, 0, -6), "neck01": (-22, 0, 0),
     "head": (-10, 0, 0)},                                                            # limp, head back
    {**SIDES, "upperleg01.R": (-35, 0, 0), "lowerleg01.R": (60, 0, 0), "upperarm01.L": (-40, 0, -15)},  # turning
    {"upperarm01.L": (-90, 0, 10), "upperarm01.R": (-90, 0, -10), "lowerarm01.L": (0, 0, 15),
     "lowerarm01.R": (0, 0, -15)},                                                    # arms forward, reaching ahead
]

TORSO = {"spine05", "spine04", "spine03", "spine02", "spine01", "pelvis.L", "pelvis.R", "root", "breast.L", "breast.R",
         "clavicle.L", "clavicle.R", "upperleg01.L", "upperleg01.R", "upperleg02.L", "upperleg02.R"}
LIMB_OUT = ("shoulder01", "upperarm", "lowerarm", "wrist", "finger", "metacarpal", "neck", "head", "jaw", "lowerleg",
            "foot", "toe")


def material(name, color, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    return m


def make_shift(body_obj, keep, index):
    """A loose shift lofted around the torso, shoulders to mid-thigh. Each ring is the slice's outer silhouette (the
    farthest torso vertex in each direction from the slice's centre) plus 2.5 cm, and going down a ring never comes in
    by more than a little from the one above -- so the cloth hangs from the shoulders and chest like a real shift and
    the body's forms are not drawn on it. Skinned from the nearest body vertex, so it takes the pose with the body."""
    me = body_obj.data
    pts = [v.co.copy() for v in me.vertices if keep[v.index]]
    top, bot = max(p.z for p in pts), min(p.z for p in pts)
    top -= 0.02
    NZ, NA = 36, 48
    rings, centres = [], []
    for i in range(NZ):
        z = top + (bot - top) * i / (NZ - 1)
        band = [p for p in pts if abs(p.z - z) < 0.035]
        if not band:
            band = pts
        c = sum((Vector((p.x, p.y, 0)) for p in band), Vector()) / len(band)
        r = [0.0] * NA
        for p in band:
            d = Vector((p.x - c.x, p.y - c.y, 0))
            k = int((math.atan2(d.y, d.x) + math.pi) / (2 * math.pi) * NA) % NA
            for kk in (k - 1, k, k + 1):
                r[kk % NA] = max(r[kk % NA], d.length)
        for _ in range(3):  # fill and soften around the ring
            r = [max(r[k], 0.5 * (r[k - 1] + r[(k + 1) % NA])) for k in range(NA)]
        rings.append(r)
        centres.append(c)
    for i in range(1, NZ):  # hang: never in by more than 1.5% a ring
        rings[i] = [max(rings[i][k], rings[i - 1][k] * 0.985) for k in range(NA)]
        centres[i] = centres[i - 1] * 0.6 + centres[i] * 0.4
    bm = bmesh.new()
    grid = []
    for i in range(NZ):
        z = top + (bot - top) * i / (NZ - 1)
        row = []
        for k in range(NA):
            a = (k + 0.5) / NA * 2 * math.pi - math.pi
            rr = rings[i][k] + 0.025
            row.append(bm.verts.new((centres[i].x + math.cos(a) * rr, centres[i].y + math.sin(a) * rr, z)))
        grid.append(row)
    for i in range(NZ - 1):
        for k in range(NA):
            bm.faces.new((grid[i][k], grid[i][(k + 1) % NA], grid[i + 1][(k + 1) % NA], grid[i + 1][k]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(f"soul{index}-shroud")
    bm.to_mesh(mesh)
    bm.free()
    shroud = bpy.data.objects.new(f"soul{index}-shroud", mesh)
    bpy.context.scene.collection.objects.link(shroud)
    # skin it from the nearest torso vertex
    tree = KDTree(len(me.vertices))
    for v in me.vertices:
        if keep[v.index]:
            tree.insert(v.co, v.index)
    tree.balance()
    for gr in body_obj.vertex_groups:
        shroud.vertex_groups.new(name=gr.name)
    for v in mesh.vertices:
        co, idx, dist = tree.find(v.co)
        for gw in me.vertices[idx].groups:
            shroud.vertex_groups[body_obj.vertex_groups[gw.group].name].add([v.index], gw.weight, "REPLACE")
    arm = shroud.modifiers.new("Armature", "ARMATURE")
    arm.object = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
    return shroud


def build(index, body, pose):
    bpy.ops.wm.read_homefile(use_empty=True)
    g, age, muscle, weight, height, prop = body
    info = HumanService._create_default_human_info_dict()
    info["name"] = f"soul{index}"
    info["phenotype"] = {"gender": g, "age": age, "muscle": muscle, "weight": weight, "proportions": prop,
                         "height": height, "cupsize": 0.4, "firmness": 0.5,
                         "race": {"asian": 0.33, "caucasian": 0.34, "african": 0.33}}
    info["eyes"] = "high-poly/high-poly.mhclo"
    info["eyebrows"] = ""
    info["eyelashes"] = ""
    info["hair"] = ""
    info["teeth"] = ""
    info["tongue"] = ""
    info["clothes"] = []
    info["rig"] = "default"
    info["alternative_materials"] = {}
    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 0
    body_obj = HumanService.deserialize_from_dict(info, settings)
    rig = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    for o in meshes:   # the phenotype lives in shape keys; bake them, or no modifier can be applied
        if o.data.shape_keys:
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
    # the helper geometry (clothing helpers, joint cubes) goes before anything else
    bm = bmesh.new()
    bm.from_mesh(body_obj.data)
    deform = bm.verts.layers.deform.verify()
    helpers = {body_obj.vertex_groups[n].index for n in ("HelperGeometry", "JointCubes") if n in body_obj.vertex_groups}
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if any(k in helpers and w > 0 for k, w in v[deform].items())],
                     context="VERTS")
    bm.to_mesh(body_obj.data)
    bm.free()
    for m in list(body_obj.modifiers):
        if m.type == "MASK":
            body_obj.modifiers.remove(m)
    # the shroud: the torso's faces, shoulders to mid-thigh, before the pose is applied (groups are by bone)
    names = {gr.index: gr.name for gr in body_obj.vertex_groups}
    me = body_obj.data
    keep = []
    for v in me.vertices:
        torso = sum(gw.weight for gw in v.groups if names.get(gw.group) in TORSO)
        out = sum(gw.weight for gw in v.groups if names.get(gw.group, "").startswith(LIMB_OUT))
        low = sum(gw.weight for gw in v.groups if names.get(gw.group) in ("upperleg02.L", "upperleg02.R"))
        keep.append(torso > 0.5 and out < 0.25 and low < 0.55)
    shroud = make_shift(body_obj, keep, index)
    # the pose
    for name, (x, y, z) in pose.items():
        pb = rig.pose.bones.get(name)
        if pb:
            pb.rotation_mode = "XYZ"
            pb.rotation_euler = Euler((math.radians(x), math.radians(y), math.radians(z)))
    bpy.context.view_layer.update()
    for o in meshes + [shroud]:
        bpy.context.view_layer.objects.active = o
        for m in list(o.modifiers):
            if m.type == "ARMATURE":
                m.object = rig
            try:
                bpy.ops.object.modifier_apply(modifier=m.name)
            except RuntimeError:
                o.modifiers.remove(m)
        o.parent = None
        o.vertex_groups.clear()
    bpy.data.objects.remove(rig)
    # pale, flat materials: a painted figure, not a CG person
    skin = material("soul-skin", (0.5, 0.48, 0.45), 0.72)
    cloth = material("soul-shroud", (0.84, 0.83, 0.8), 0.95)
    eyes = material("soul-eyes", (0.7, 0.69, 0.66), 0.35)
    for o in meshes + [shroud]:
        o.data.materials.clear()
        o.data.materials.append(cloth if o is shroud else eyes if o is not body_obj else skin)
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.shade_smooth()
    dec = body_obj.modifiers.new("dec", "DECIMATE")
    dec.ratio = 0.36
    bpy.context.view_layer.objects.active = body_obj
    bpy.ops.object.modifier_apply(modifier="dec")
    # one object, origin at the pelvis
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes + [shroud]:
        o.select_set(True)
    bpy.context.view_layer.objects.active = body_obj
    bpy.ops.object.join()
    soul = bpy.context.active_object
    soul.name = f"soul{index}"
    zs = [v.co.z for v in soul.data.vertices]
    pelvis = Vector((0.0, 0.0, min(zs) + 0.53 * (max(zs) - min(zs))))
    for v in soul.data.vertices:
        v.co -= pelvis
    tris = sum(len(p.vertices) - 2 for p in soul.data.polygons)
    path = os.path.join(OUT, f"soul-{index}.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=False, export_apply=True,
                              export_animations=False, export_skins=False, export_morph=False)
    print("exported", path, "triangles", tris)
    if STILLS:
        os.makedirs(STILLS, exist_ok=True)
        scene = bpy.context.scene
        scene.render.engine = "BLENDER_EEVEE"
        scene.render.resolution_x, scene.render.resolution_y = 600, 800
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = 2.3
        scene.collection.objects.link(cam)
        scene.camera = cam
        cam.location = (1.6, -4.0, 0.2)
        cam.rotation_euler = (Vector((0, 0, 0)) - cam.location).to_track_quat("-Z", "Y").to_euler()
        sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
        sun.data.energy = 4.0
        sun.rotation_euler = (math.radians(50), 0, math.radians(30))
        scene.collection.objects.link(sun)
        world = bpy.data.worlds.new("w")
        world.use_nodes = True
        world.node_tree.nodes["Background"].inputs[0].default_value = (0.05, 0.03, 0.03, 1)
        scene.world = world
        scene.render.filepath = os.path.join(STILLS, f"soul-{index}.png")
        bpy.ops.render.render(write_still=True)


for i, (body, pose) in enumerate(zip(BODIES, POSES), start=1):
    build(i, body, pose)
print("done")
