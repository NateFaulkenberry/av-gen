"""CHRONOS, The Devourer (Gate D1): build the Chronos creature in Blender, headless, and export it for AV Gen.

The MPFB route of docs/prototypes/chronos/12-devourer-recon-plan.md §3, taken past the D0 look-dev:

  1. an MPFB adult (CC0) pushed past human by its own anatomical targets (gaunt, aged, eyes sunk and staring, the jaw
     dropped), with the rig refitted to that shape;
  2. a scripted sculpt beyond human proportion on the baked mesh -- a longer, hanging lower jaw, deeper orbits, carved
     cheek hollows under raised cheekbones, a brow ridge, sunken temples, a taller narrower skull -- and the clothes
     and rig refitted to it;
  3. irregular teeth: MPFB's set split into teeth, each lengthened, tilted and shifted on its own, yellowed and worn;
  4. hair as a matted MASS, not a wig: a few hundred clumped, tapering, wavy strand tubes rooted over the whole scalp
     and falling to the shoulders, plus a thin skull-cap layer so no bald crown shows;
  5. the skin baked for the engine: MPFB's old-skin diffuse graded toward ochre and umber, darkened in every crevice by
     baked ambient occlusion and mottled by a painterly noise (Goya's paint, not clean CG), and a tangent-space normal
     map of wrinkles and pores baked from a procedural bump;
  6. one looping 12 s life clip on the rig (`idle`): irregular jaw breathing with one slow, wide hungry opening to
     64 degrees, eye saccades, a slight drift of the head;
  7. export: glTF binary with the skinned mesh, the baked textures and the clip.

    BLENDER_USER_RESOURCES=<dir with MPFB> /Applications/Blender.app/Contents/MacOS/Blender --background \
        --python examples/chronos/devourer/build_creature.py -- <out.glb> [<stills_dir>]

The outputs are derived from CC0 assets but stay out of the public repository (assets/chronos/ is gitignored); this
script is original code and is committed.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

from bl_ext.user_default.mpfb.services.humanservice import HumanService
from bl_ext.user_default.mpfb.services.targetservice import TargetService

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/chronos-creature.glb"
STILLS = argv[1] if len(argv) > 1 else ""
os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
TEX_DIR = os.path.abspath(os.path.join(os.path.dirname(OUT), "chronos-creature-textures"))  # see build_scan_creature.py
os.makedirs(TEX_DIR, exist_ok=True)
rng = random.Random(1933)


def mpfb_targets():
    import bl_ext.user_default.mpfb as mpfb
    return os.path.join(os.path.dirname(mpfb.__file__), "data", "targets")


FACE = {
    "cheek/{s}-cheek-volume-decr": 1.0, "cheek/{s}-cheek-bones-incr": 1.0, "cheek/{s}-cheek-inner-decr": 1.0,
    "cheek/{s}-cheek-trans-down": 0.4,
    "eyes/{s}-eye-push1-in": 1.0, "eyes/{s}-eye-push2-in": 1.0, "eyes/{s}-eye-bag-incr": 1.0,
    "eyes/{s}-eye-bag-height-incr": 0.8, "eyes/{s}-eye-scale-incr": 0.75, "eyes/{s}-eye-height2-incr": 0.8,
}
SINGLE = {
    "head/head-age-incr": 1.0, "head/head-fat-decr": 1.0, "head/head-scale-vert-incr": 1.0, "head/head-oval": 0.6,
    "forehead/forehead-trans-forward": 0.8, "forehead/forehead-temple-decr": 1.0, "forehead/forehead-nubian-incr": 0.6,
    "chin/chin-jaw-drop-incr": 1.0, "chin/chin-height-incr": 1.0, "chin/chin-prognathism-incr": 0.4,
    "chin/chin-bones-incr": 0.8,
    "mouth/mouth-scale-horiz-incr": 1.0, "mouth/mouth-scale-vert-incr": 0.8, "mouth/mouth-lowerlip-volume-decr": 0.9,
    "mouth/mouth-upperlip-volume-decr": 0.9, "mouth/mouth-laugh-lines-in": 1.0, "mouth/mouth-trans-down": 0.5,
    "neck/neck-scale-horiz-decr": 0.6,
    "asym/asym-brown-1-l": 0.8, "asym/asym-cheek-1-r": 0.7, "asym/asym-brown-2-r": 0.5,
    "expression/units/caucasian/eye-left-opened-up": 1.0, "expression/units/caucasian/eye-right-opened-up": 0.85,
    "expression/units/caucasian/eyebrows-left-inner-up": 0.9, "expression/units/caucasian/eyebrows-right-inner-up": 0.7,
    "expression/units/caucasian/mouth-depression": 0.7, "expression/units/caucasian/neck-platysma": 1.0,
    "expression/units/caucasian/nose-left-dilatation": 0.6, "expression/units/caucasian/mouth-retraction": 0.5,
}


# ---- 1. the base -------------------------------------------------------------------------------------------------

def build_base():
    bpy.ops.wm.read_homefile(use_empty=True)
    info = HumanService._create_default_human_info_dict()
    info["name"] = "chronos"
    info["phenotype"] = {"gender": 1.0, "age": 1.0, "muscle": 0.25, "weight": 0.0, "proportions": 0.0, "height": 0.5,
                         "cupsize": 0.5, "firmness": 0.5, "race": {"asian": 0.0, "caucasian": 1.0, "african": 0.0}}
    info["eyes"] = "high-poly/high-poly.mhclo"
    info["eyebrows"] = "eyebrow003/eyebrow003.mhclo"
    info["eyelashes"] = "eyelashes01/eyelashes01.mhclo"
    info["hair"] = ""
    info["teeth"] = "teeth_base/teeth_base.mhclo"
    info["tongue"] = "tongue01/tongue01.mhclo"
    info["clothes"] = []
    info["skin_mhmat"] = "old_caucasian_male/old_caucasian_male.mhmat"
    info["skin_material_type"] = "ENHANCED_SSS"
    info["rig"] = "default"
    info["alternative_materials"] = {}
    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 0
    basemesh = HumanService.deserialize_from_dict(info, settings)
    root = mpfb_targets()
    for key, w in FACE.items():
        for side, k in (("l", 1.0), ("r", 0.9)):
            path = os.path.join(root, key.format(s=side) + ".target.gz")
            if os.path.exists(path):
                TargetService.load_target(basemesh, path, weight=w * k)
    for key, w in SINGLE.items():
        path = os.path.join(root, key + ".target.gz")
        if os.path.exists(path):
            TargetService.load_target(basemesh, path, weight=w)
    TargetService.bake_targets(basemesh)
    HumanService.refit(basemesh)
    return basemesh


def rig_object():
    return next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")


def bone_head(rig, name):
    return rig.matrix_world @ rig.data.bones[name].head_local


def bone_tail(rig, name):
    return rig.matrix_world @ rig.data.bones[name].tail_local


def smooth01(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


# ---- 2. the sculpt beyond human -----------------------------------------------------------------------------------

def sculpt(basemesh):
    rig = rig_object()
    eyeL, eyeR = bone_head(rig, "eye.L"), bone_head(rig, "eye.R")
    eyes = (eyeL + eyeR) * 0.5
    jaw_hinge = bone_head(rig, "jaw")
    head_top = bone_tail(rig, "head")
    up = Vector((0, 0, 1))
    # MPFB faces -Y
    fwd = Vector((0, -1, 0))
    lat = (eyeL - eyeR).normalized()
    scale = (eyeL - eyeR).length / 0.064  # about 1 for a real face
    me = basemesh.data
    jaw_group = basemesh.vertex_groups.get("jaw")
    head_group = basemesh.vertex_groups.get("head")
    jw = [0.0] * len(me.vertices)
    hw = [0.0] * len(me.vertices)
    for v in me.vertices:
        for g in v.groups:
            if jaw_group and g.group == jaw_group.index:
                jw[v.index] = g.weight
            if head_group and g.group == head_group.index:
                hw[v.index] = g.weight
    me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
    mw = basemesh.matrix_world
    inv = mw.inverted()

    def bump(p, centre, radius):
        d = (p - centre).length / (radius * scale)
        return smooth01(1.0 - d)

    cheek_l = eyes + lat * 0.046 * scale - up * 0.045 * scale - fwd * 0.004 * scale
    cheek_r = eyes - lat * 0.046 * scale - up * 0.045 * scale - fwd * 0.004 * scale
    bone_l = eyes + lat * 0.05 * scale - up * 0.02 * scale + fwd * 0.004 * scale
    bone_r = eyes - lat * 0.05 * scale - up * 0.02 * scale + fwd * 0.004 * scale
    brow_l = eyeL + up * 0.021 * scale + fwd * 0.006 * scale
    brow_r = eyeR + up * 0.021 * scale + fwd * 0.006 * scale
    temple_l = eyes + lat * 0.066 * scale + up * 0.012 * scale - fwd * 0.03 * scale
    temple_r = eyes - lat * 0.066 * scale + up * 0.012 * scale - fwd * 0.03 * scale
    for v in me.vertices:
        p = mw @ v.co
        if (p - eyes).length > 0.26 * scale:
            continue
        n = (mw.to_3x3() @ v.normal).normalized()
        d = Vector((0, 0, 0))
        # the lower jaw lengthens and hangs: the further below the eyes, the more it drops
        below = smooth01((eyes.z - p.z) / (0.13 * scale))
        d += -up * 0.042 * scale * jw[v.index] * below
        d += fwd * 0.01 * scale * jw[v.index] * below  # and juts a little
        # the orbits sink
        for e in (eyeL, eyeR):
            d += -fwd * 0.009 * scale * bump(p, e + fwd * 0.012 * scale, 0.034)
        # hollows under the cheekbones, the cheekbones raised
        d += -n * 0.013 * scale * (bump(p, cheek_l, 0.036) + bump(p, cheek_r, 0.036))
        d += n * 0.005 * scale * (bump(p, bone_l, 0.022) + bump(p, bone_r, 0.022))
        # the brow ridge overhangs the sunk eyes
        d += fwd * 0.007 * scale * (bump(p, brow_l, 0.022) + bump(p, brow_r, 0.022))
        # temples sink
        d += -n * 0.01 * scale * (bump(p, temple_l, 0.03) + bump(p, temple_r, 0.03))
        # a taller, narrower skull
        crown = smooth01((p.z - (eyes.z + 0.04 * scale)) / (0.1 * scale)) * hw[v.index]
        d += up * 0.03 * scale * crown
        side = (p - eyes).dot(lat)
        d += -lat * side * 0.12 * crown
        v.co = inv @ (p + d)
    me.update()
    HumanService.refit(basemesh)


# ---- 3. the teeth ------------------------------------------------------------------------------------------------

def rework_teeth():
    rig = rig_object()
    teeth = next((o for o in bpy.context.scene.objects if o.type == "MESH" and "teeth" in o.name.lower()), None)
    if teeth is None:
        print("no teeth")
        return
    hinge = bone_head(rig, "jaw")
    bm = bmesh.new()
    bm.from_mesh(teeth.data)
    bm.verts.ensure_lookup_table()
    # loose parts = teeth (and gums)
    seen = set()
    parts = []
    for v in bm.verts:
        if v.index in seen:
            continue
        stack, part = [v], []
        seen.add(v.index)
        while stack:
            x = stack.pop()
            part.append(x)
            for e in x.link_edges:
                o = e.other_vert(x)
                if o.index not in seen:
                    seen.add(o.index)
                    stack.append(o)
        parts.append(part)
    mw = teeth.matrix_world
    centre_all = sum((mw @ v.co for v in bm.verts), Vector()) / len(bm.verts)
    for part in parts:
        if len(part) > 400:  # the gums: leave
            continue
        c = sum((v.co for v in part), Vector()) / len(part)
        upper = (mw @ c).z > centre_all.z
        axis = Vector((0, 0, -1 if upper else 1))  # the tooth points toward the bite
        length = rng.uniform(1.25, 1.95)
        tilt = Matrix.Rotation(math.radians(rng.uniform(-9, 9)), 3, Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)).normalized() if True else Vector((1, 0, 0)))
        shift = Vector((rng.uniform(-0.6, 0.6), rng.uniform(-0.6, 0.6), 0)) * 0.001
        lo = min((v.co - c).dot(axis) for v in part)
        for v in part:
            r = v.co - c
            along = r.dot(axis)
            t = (along - lo) / max(1e-6, -lo * 2)  # 0 at the root
            r2 = r + axis * along * (length - 1.0) * max(0.0, along / max(1e-6, abs(lo)))
            v.co = c + shift + tilt @ r2
    bm.to_mesh(teeth.data)
    bm.free()
    teeth.data.update()


# ---- 4. the hair mass --------------------------------------------------------------------------------------------

def hair_mass(basemesh):
    rig = rig_object()
    eyeL, eyeR = bone_head(rig, "eye.L"), bone_head(rig, "eye.R")
    eyes = (eyeL + eyeR) * 0.5
    up, fwd = Vector((0, 0, 1)), Vector((0, -1, 0))
    lat = (eyeL - eyeR).normalized()
    s = (eyeL - eyeR).length / 0.064
    mw = basemesh.matrix_world
    # roots: scalp vertices (above the brow line and behind the hairline)
    roots = []
    for v in basemesh.data.vertices:
        p = mw @ v.co
        rel = p - eyes
        if rel.length > 0.2 * s:
            continue
        if rel.z > 0.035 * s and rel.dot(fwd) < 0.03 * s:
            n = (mw.to_3x3() @ v.normal).normalized()
            roots.append((p, n))
    rng.shuffle(roots)
    roots = roots[:420]
    verts, faces = [], []
    sides = 6
    for (p, n) in roots:
        length = rng.uniform(0.22, 0.48) * s
        r0 = rng.uniform(0.004, 0.009) * s
        segs = 16
        # the strand leaves the scalp along the normal, then falls; it clumps and waves
        side = (p - eyes).dot(lat)
        out = n * 0.012 * s
        pos = p - n * 0.002 * s
        vel = (n * 0.6 + Vector((0, 0, -0.4))).normalized()
        wave = rng.uniform(0, 6.28)
        pts = []
        for k in range(segs + 1):
            t = k / segs
            pts.append(pos.copy())
            grav = Vector((0, 0, -1)) * (0.4 + 1.6 * t)
            push = lat * math.copysign(0.25, side) * (1 - t)  # away from the face at first
            back = -fwd * 0.25 * (1 - t)
            jitter = Vector((math.sin(wave + 9 * t), math.cos(wave * 1.3 + 7 * t), 0)) * 0.35
            vel = (vel * 0.7 + (grav + push + back + jitter) * 0.3).normalized()
            pos = pos + vel * (length / segs)
        base = len(verts)
        for k, c in enumerate(pts):
            t = k / segs
            r = r0 * (1.0 - 0.85 * t)
            tdir = (pts[min(k + 1, segs)] - pts[max(k - 1, 0)]).normalized()
            a = tdir.orthogonal().normalized()
            b = tdir.cross(a)
            for j in range(sides):
                ang = 2 * math.pi * j / sides
                verts.append(c + (a * math.cos(ang) + b * math.sin(ang)) * r)
        for k in range(segs):
            for j in range(sides):
                i0 = base + k * sides + j
                i1 = base + k * sides + (j + 1) % sides
                faces.append((i0, i1, i1 + sides, i0 + sides))
    me = bpy.data.meshes.new("chronos-hair")
    me.from_pydata([tuple(v) for v in verts], [], faces)
    me.update()
    hair = bpy.data.objects.new("chronos-hair", me)
    bpy.context.scene.collection.objects.link(hair)
    # a thin cap of matted hair over the skull, so no crown shows between strands
    cap = basemesh.copy()
    cap.data = basemesh.data.copy()
    cap.name = "chronos-haircap"
    bpy.context.scene.collection.objects.link(cap)
    bm = bmesh.new()
    bm.from_mesh(cap.data)
    keep = set()
    for v in bm.verts:
        rel = (mw @ v.co) - eyes
        if rel.length < 0.2 * s and rel.z > 0.04 * s and rel.dot(fwd) < 0.025 * s:
            keep.add(v.index)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in keep], context="VERTS")
    for v in bm.verts:
        v.co += v.normal * (0.006 * s * (0.7 + 0.6 * rng.random()))
    bm.to_mesh(cap.data)
    bm.free()
    cap.modifiers.clear()
    cap.data.shape_keys and cap.shape_key_clear()
    # both ride the head bone
    for o in (hair, cap):
        o.vertex_groups.clear()
        g = o.vertex_groups.new(name="head")
        g.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
        o.parent = rig
        o.matrix_parent_inverse = rig.matrix_world.inverted()
        mod = o.modifiers.new("Armature", "ARMATURE")
        mod.object = rig
    mat = bpy.data.materials.new("chronos-hair")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.11, 0.095, 0.08, 1)
    bsdf.inputs["Roughness"].default_value = 0.75
    for o in (hair, cap):
        o.data.materials.clear()
        o.data.materials.append(mat)


# ---- 5. the skin bake --------------------------------------------------------------------------------------------

def skin_material_bake(basemesh):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 32
    mat = basemesh.active_material
    # the source: MPFB's diffuse texture, if found
    diffuse_img = None
    for m in basemesh.data.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image and "diffuse" in n.image.name.lower():
                    diffuse_img = n.image
    w = 2048
    albedo = bpy.data.images.new("chronos-albedo", w, w)
    normal = bpy.data.images.new("chronos-normal", w, w)
    normal.colorspace_settings.name = "Non-Color"
    # a bake material: graded diffuse x AO x painterly mottle as emission (for the albedo), and a wrinkle bump
    bake = bpy.data.materials.new("chronos-skin-bake")
    bake.use_nodes = True
    nt = bake.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = diffuse_img
    hsv = nt.nodes.new("ShaderNodeHueSaturation")
    hsv.inputs["Hue"].default_value = 0.53
    hsv.inputs["Saturation"].default_value = 0.32
    hsv.inputs["Value"].default_value = 0.5
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.02
    aoramp = nt.nodes.new("ShaderNodeMapRange")
    aoramp.inputs["To Min"].default_value = 0.25
    mottle = nt.nodes.new("ShaderNodeTexNoise")
    mottle.inputs["Scale"].default_value = 18.0
    mottle.inputs["Detail"].default_value = 6.0
    mramp = nt.nodes.new("ShaderNodeValToRGB")
    mramp.color_ramp.elements[0].color = (0.62, 0.42, 0.24, 1)   # umber
    mramp.color_ramp.elements[1].color = (1.0, 0.86, 0.66, 1)    # dirty ochre-bone
    mix1 = nt.nodes.new("ShaderNodeMix")
    mix1.data_type = "RGBA"
    mix1.blend_type = "MULTIPLY"
    mix1.inputs["Factor"].default_value = 0.85
    mix2 = nt.nodes.new("ShaderNodeMix")
    mix2.data_type = "RGBA"
    mix2.blend_type = "MULTIPLY"
    mix2.inputs["Factor"].default_value = 1.0
    if diffuse_img:
        nt.links.new(tex.outputs["Color"], hsv.inputs["Color"])
    else:
        hsv.inputs["Color"].default_value = (0.5, 0.36, 0.27, 1)
    nt.links.new(mottle.outputs["Fac"], mramp.inputs["Fac"])
    nt.links.new(hsv.outputs["Color"], mix1.inputs["A"])
    nt.links.new(mramp.outputs["Color"], mix1.inputs["B"])
    nt.links.new(ao.outputs["AO"], aoramp.inputs["Value"])
    nt.links.new(mix1.outputs["Result"], mix2.inputs["A"])
    nt.links.new(aoramp.outputs["Result"], mix2.inputs["B"])
    nt.links.new(mix2.outputs["Result"], emit.inputs["Color"])
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    target = nt.nodes.new("ShaderNodeTexImage")
    target.image = albedo
    nt.nodes.active = target
    basemesh.data.materials.clear()
    basemesh.data.materials.append(bake)
    for o in bpy.context.scene.objects:
        o.select_set(False)
    basemesh.select_set(True)
    bpy.context.view_layer.objects.active = basemesh
    bpy.ops.object.bake(type="EMIT", margin=8)
    albedo.filepath_raw = os.path.join(TEX_DIR, "chronos-albedo.png")
    albedo.file_format = "PNG"
    albedo.save()
    # the wrinkle normal: a bump of fine creases and pores, baked to tangent space
    for n in list(nt.nodes):
        if n not in (out, target):
            nt.nodes.remove(n)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    wr = nt.nodes.new("ShaderNodeTexWave")
    wr.wave_type = "BANDS"
    wr.bands_direction = "Z"
    wr.inputs["Scale"].default_value = 55.0
    wr.inputs["Distortion"].default_value = 9.0
    wr.inputs["Detail"].default_value = 4.0
    pores = nt.nodes.new("ShaderNodeTexVoronoi")
    pores.inputs["Scale"].default_value = 900.0
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = "ADD"
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.6
    bump.inputs["Distance"].default_value = 0.002
    nt.links.new(wr.outputs["Fac"], add.inputs[0])
    nt.links.new(pores.outputs["Distance"], add.inputs[1])
    nt.links.new(add.outputs["Value"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    target.image = normal
    nt.nodes.active = target
    bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", margin=8)
    normal.filepath_raw = os.path.join(TEX_DIR, "chronos-normal.png")
    normal.file_format = "PNG"
    normal.save()
    # the export material: baked albedo + normal, rough, a touch of sheen-less specular
    final = bpy.data.materials.new("chronos-skin")
    final.use_nodes = True
    fnt = final.node_tree
    fb = fnt.nodes["Principled BSDF"]
    ta = fnt.nodes.new("ShaderNodeTexImage")
    ta.image = albedo
    tn = fnt.nodes.new("ShaderNodeTexImage")
    tn.image = normal
    nm = fnt.nodes.new("ShaderNodeNormalMap")
    fnt.links.new(ta.outputs["Color"], fb.inputs["Base Color"])
    fnt.links.new(tn.outputs["Color"], nm.inputs["Color"])
    fnt.links.new(nm.outputs["Normal"], fb.inputs["Normal"])
    fb.inputs["Roughness"].default_value = 0.68
    basemesh.data.materials.clear()
    basemesh.data.materials.append(final)


def grade_parts():
    for o in bpy.context.scene.objects:
        low = o.name.lower()
        if o.type != "MESH":
            continue
        if "teeth" in low or "tongue" in low:
            m = bpy.data.materials.new(o.name + "-aged")
            m.use_nodes = True
            b = m.node_tree.nodes["Principled BSDF"]
            b.inputs["Base Color"].default_value = (0.42, 0.31, 0.15, 1) if "teeth" in low else (0.06, 0.012, 0.01, 1)
            b.inputs["Roughness"].default_value = 0.45
            o.data.materials.clear()
            o.data.materials.append(m)


# ---- 6. the life clip --------------------------------------------------------------------------------------------

def life_clip():
    rig = rig_object()
    fps = 24
    bpy.context.scene.render.fps = fps
    act = bpy.data.actions.new("idle")
    rig.animation_data_create()
    rig.animation_data.action = act
    pb = rig.pose.bones
    for n in ("jaw", "eye.L", "eye.R", "head", "neck02"):
        pb[n].rotation_mode = "XYZ"
    # (seconds, jaw degrees): irregular breathing, then one slow, wide, hungry opening, held, and back
    jaw = [(0, 12), (1.6, 19), (2.9, 13), (4.4, 22), (5.6, 15), (6.6, 30), (8.2, 64), (9.4, 66), (10.6, 34),
           (11.4, 18), (12.0, 12)]
    for t, deg in jaw:
        pb["jaw"].rotation_euler = Euler((math.radians(deg), 0, math.radians(-3)))
        pb["jaw"].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    # eyes: saccades -- holds broken by quick jumps (pitch, yaw degrees)
    look = [(0, (-4, 3)), (2.0, (-4, 3)), (2.15, (-7, -6)), (4.5, (-7, -6)), (4.65, (-2, 9)), (7.4, (-2, 9)),
            (7.55, (-9, 1)), (10.2, (-9, 1)), (10.35, (-4, 3)), (12.0, (-4, 3))]
    for t, (p, y) in look:
        for n in ("eye.L", "eye.R"):
            pb[n].rotation_euler = Euler((math.radians(p), 0, math.radians(y)))
            pb[n].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    # the head drifts a degree or two, and lifts into the wide opening
    head = [(0, (-10, 0, 5)), (4.0, (-9, 0, 3.5)), (8.2, (-14, 0, 6)), (9.4, (-15, 0, 6.5)), (12.0, (-10, 0, 5))]
    for t, rot in head:
        pb["head"].rotation_euler = Euler(tuple(math.radians(v) for v in rot))
        pb["head"].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    for fc in act.fcurves if hasattr(act, "fcurves") else []:
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
    bpy.context.scene.frame_start = 0
    bpy.context.scene.frame_end = 12 * fps


# ---- 7. export and stills ----------------------------------------------------------------------------------------

def strip_for_export():
    # keep the body from the shoulders up: the brief needs head, neck and upper torso only
    for o in list(bpy.context.scene.objects):
        if o.type == "MESH" and o.name.lower().startswith(("eyelash",)):
            pass


def export():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", use_selection=False, export_apply=True,
                              export_animations=True, export_skins=True, export_morph=False,
                              export_image_format="AUTO")
    print("exported", OUT)


def stills():
    if not STILLS:
        return
    os.makedirs(STILLS, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1200
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - High Contrast"
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.01, 0.003, 0.003, 1)
    scene.world = world
    rig = rig_object()
    b = rig.pose.bones["head"]
    c = rig.matrix_world @ (b.head + (b.tail - b.head) * 0.35)
    s = 0.1

    def area(name, energy, color, loc, size):
        L = bpy.data.lights.new(name, "AREA")
        L.energy, L.color, L.size = energy, color, size
        o = bpy.data.objects.new(name, L)
        o.location = c + Vector(loc)
        o.rotation_euler = (c - o.location).to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(o)
    area("key", 9.0, (1.0, 0.7, 0.4), (-4.2 * s, -1.6 * s, 3.8 * s), 0.05)
    area("mouth", 0.4, (0.9, 0.1, 0.04), (0.4 * s, -2.2 * s, -1.4 * s), 0.06)
    area("rim", 2.5, (0.6, 0.14, 0.2), (3.4 * s, 2.0 * s, 2.0 * s), 0.1)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    for name, frame, az, el, dist, lens in (("frontal-breath", 48, 0, 2, 1.15, 70), ("three-quarter-breath", 48, 34, 4, 1.15, 70),
                                            ("frontal-devour", 220, 0, -2, 1.15, 70), ("three-quarter-devour", 220, 30, 2, 1.1, 70)):
        scene.frame_set(frame)
        a, e = math.radians(az), math.radians(el)
        cam.data.lens = lens
        cam.location = c + Vector((math.sin(a) * -dist * math.cos(e), -math.cos(a) * dist * math.cos(e), dist * math.sin(e)))
        cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(STILLS, f"chronos-mpfb-{name}.png")
        bpy.ops.render.render(write_still=True)


basemesh = build_base()
sculpt(basemesh)
rework_teeth()
hair_mass(basemesh)
skin_material_bake(basemesh)
grade_parts()
life_clip()
bpy.ops.wm.save_as_mainfile(filepath=os.path.splitext(OUT)[0] + ".blend")
export()
stills()
print("done")
