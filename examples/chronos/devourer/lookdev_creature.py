"""CHRONOS, The Devourer (Gate D0): quick look-dev of the creature's face in Blender, headless.

The route under test (docs/prototypes/chronos/12-devourer-recon-plan.md §3): an MPFB human (MakeHuman for Blender; its
assets are CC0) pushed far past human by its own anatomical targets -- gaunt, aged, eyes sunk and staring, the jaw
dropped and extended -- then posed with the default rig's own jaw, eye and neck bones, which the engine's skinned
pipeline already imports. Lit in chiaroscuro: one dirty-amber key, a crimson under-light from the mouth, near black.

    BLENDER_USER_RESOURCES=<dir with MPFB> /Applications/Blender.app/Contents/MacOS/Blender --background \
        --python examples/chronos/devourer/lookdev_creature.py -- <out_dir>

Writes <out_dir>/devourer-d0-look-<view>.png. Nothing it makes is committed (the derived meshes are CC0, but assets stay
out of the public repository); this script is original code.
"""
import math
import os
import sys

import bpy
from mathutils import Euler, Vector

from bl_ext.user_default.mpfb.services.humanservice import HumanService
from bl_ext.user_default.mpfb.services.targetservice import TargetService

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/devourer"
os.makedirs(OUT, exist_ok=True)


def mpfb_data():
    import bl_ext.user_default.mpfb as mpfb
    return os.path.join(os.path.dirname(mpfb.__file__), "data", "targets")


# The face, as target weights. Pairs (l, r) are set together with a small asymmetry: Goya's Saturn is lopsided.
FACE = {
    "cheek/{s}-cheek-volume-decr": 1.0, "cheek/{s}-cheek-bones-incr": 1.0, "cheek/{s}-cheek-inner-decr": 1.0,
    "cheek/{s}-cheek-trans-down": 0.4,
    "eyes/{s}-eye-push1-in": 1.0, "eyes/{s}-eye-push2-in": 1.0, "eyes/{s}-eye-bag-incr": 1.0,
    "eyes/{s}-eye-bag-height-incr": 0.8, "eyes/{s}-eye-scale-incr": 0.7, "eyes/{s}-eye-height2-incr": 0.8,
}
SINGLE = {
    "head/head-age-incr": 1.0, "head/head-fat-decr": 1.0, "head/head-scale-vert-incr": 1.0, "head/head-oval": 0.6,
    "forehead/forehead-trans-forward": 0.8, "forehead/forehead-temple-decr": 1.0, "forehead/forehead-nubian-incr": 0.6,
    "chin/chin-jaw-drop-incr": 1.0, "chin/chin-height-incr": 1.0, "chin/chin-prognathism-incr": 0.4,
    "chin/chin-bones-incr": 0.8,
    "mouth/mouth-scale-horiz-incr": 1.0, "mouth/mouth-scale-vert-incr": 0.8, "mouth/mouth-lowerlip-volume-decr": 0.8,
    "mouth/mouth-upperlip-volume-decr": 0.8, "mouth/mouth-laugh-lines-in": 1.0, "mouth/mouth-trans-down": 0.5,
    "neck/neck-scale-horiz-decr": 0.6,
    "asym/asym-brown-1-l": 0.8, "asym/asym-cheek-1-r": 0.7, "asym/asym-brown-2-r": 0.5,
    # expression units: the stare, the strain of the opened mouth, the corded neck
    "expression/units/caucasian/eye-left-opened-up": 1.0, "expression/units/caucasian/eye-right-opened-up": 0.85,
    "expression/units/caucasian/eyebrows-left-inner-up": 0.9, "expression/units/caucasian/eyebrows-right-inner-up": 0.7,
    "expression/units/caucasian/mouth-open": 1.0, "expression/units/caucasian/mouth-depression": 0.7,
    "expression/units/caucasian/neck-platysma": 1.0, "expression/units/caucasian/nose-left-dilatation": 0.6,
}


def build():
    bpy.ops.wm.read_homefile(use_empty=True)
    info = HumanService._create_default_human_info_dict()
    info["name"] = "chronos"
    info["phenotype"] = {"gender": 1.0, "age": 1.0, "muscle": 0.25, "weight": 0.0, "proportions": 0.0, "height": 0.5,
                         "cupsize": 0.5, "firmness": 0.5, "race": {"asian": 0.0, "caucasian": 1.0, "african": 0.0}}
    info["eyes"] = "high-poly/high-poly.mhclo"
    info["eyebrows"] = "eyebrow003/eyebrow003.mhclo"
    info["eyelashes"] = "eyelashes01/eyelashes01.mhclo"
    info["hair"] = "long01/long01.mhclo"
    info["teeth"] = "teeth_base/teeth_base.mhclo"
    info["tongue"] = "tongue01/tongue01.mhclo"
    info["clothes"] = []
    info["skin_mhmat"] = "old_caucasian_male/old_caucasian_male.mhmat"
    info["skin_material_type"] = "ENHANCED_SSS"
    info["rig"] = "default"
    info["alternative_materials"] = {}
    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 1
    basemesh = HumanService.deserialize_from_dict(info, settings)
    root = mpfb_data()
    for key, w in FACE.items():
        for side, k in (("l", 1.0), ("r", 0.9)):
            path = os.path.join(root, key.format(s=side) + ".target.gz")
            if os.path.exists(path):
                TargetService.load_target(basemesh, path, weight=w * k)
            else:
                print("missing target", path)
    for key, w in SINGLE.items():
        path = os.path.join(root, key + ".target.gz")
        if os.path.exists(path):
            TargetService.load_target(basemesh, path, weight=w)
        else:
            print("missing target", path)
    return basemesh


def pose(jaw_deg):
    rig = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
    if rig is None:
        print("no rig")
        return
    pb = rig.pose.bones
    if "jaw" in pb:
        pb["jaw"].rotation_mode = "XYZ"
        pb["jaw"].rotation_euler = Euler((math.radians(jaw_deg), 0, math.radians(-3)))
    for n, rot in (("head", (-10, 0, 5)), ("neck02", (-4, 0, 2)), ("eye.L", (-6, 0, 4)), ("eye.R", (-5, 0, 7))):
        if n in pb:
            pb[n].rotation_mode = "XYZ"
            pb[n].rotation_euler = Euler(tuple(math.radians(v) for v in rot))
    bpy.context.view_layer.update()


def grade_skin():
    # desaturate and darken the skin toward Goya's dirty flesh; roughen it
    for m in bpy.data.materials:
        if not m.use_nodes:
            continue
        for n in m.node_tree.nodes:
            if n.type == "BSDF_PRINCIPLED":
                if "Roughness" in n.inputs and not n.inputs["Roughness"].is_linked:
                    n.inputs["Roughness"].default_value = 0.62
        if "skin" in m.name.lower() or "chronos" in m.name.lower():
            nt = m.node_tree
            out = next((n for n in nt.nodes if n.type == "OUTPUT_MATERIAL"), None)
            bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
            if bsdf is None or not bsdf.inputs["Base Color"].is_linked:
                continue
            link = bsdf.inputs["Base Color"].links[0]
            hsv = nt.nodes.new("ShaderNodeHueSaturation")
            hsv.inputs["Saturation"].default_value = 0.38   # Goya's dirty ochre flesh, not healthy pink
            hsv.inputs["Value"].default_value = 0.48
            hsv.inputs["Hue"].default_value = 0.53
            nt.links.new(link.from_socket, hsv.inputs["Color"])
            nt.links.new(hsv.outputs["Color"], bsdf.inputs["Base Color"])
    # the mouth's interior goes dark: teeth aged and yellowed, the tongue and gums near black-crimson
    for o in bpy.context.scene.objects:
        low = o.name.lower()
        if o.type == "MESH" and ("teeth" in low or "tongue" in low):
            for slot in o.material_slots:
                m = slot.material
                if m and m.use_nodes:
                    nt = m.node_tree
                    bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
                    if bsdf is None:
                        continue
                    tint = (0.32, 0.24, 0.12, 1) if "teeth" in low else (0.05, 0.008, 0.008, 1)
                    for l in list(bsdf.inputs["Base Color"].links):
                        nt.links.remove(l)
                    bsdf.inputs["Base Color"].default_value = tint
                    bsdf.inputs["Roughness"].default_value = 0.5
    for o in bpy.context.scene.objects:
        if o.type == "MESH" and "hair" in o.name.lower():
            for slot in o.material_slots:
                m = slot.material
                if m and m.use_nodes:
                    for n in m.node_tree.nodes:
                        if n.type == "BSDF_PRINCIPLED" and not n.inputs["Base Color"].is_linked:
                            n.inputs["Base Color"].default_value = (0.09, 0.075, 0.06, 1)


def head_centre():
    # the face's middle (between the eyes and the mouth), not the bone's: the head bone runs from the top of the neck
    # to the crown, so a third of the way up is about the nose
    rig = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
    b = rig.pose.bones["head"]
    return rig.matrix_world @ (b.head + (b.tail - b.head) * 0.38)


def light_and_render(views):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1200
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - High Contrast"
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.012, 0.004, 0.004, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    scene.world = world
    c = head_centre()
    def area(name, energy, color, loc, size):
        L = bpy.data.lights.new(name, "AREA")
        L.energy, L.color, L.size = energy, color, size
        o = bpy.data.objects.new(name, L)
        o.location = c + Vector(loc)
        o.rotation_euler = (c - o.location).to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(o)
    s = 0.1  # MPFB scale: decimetres
    area("key", 9.0, (1.0, 0.7, 0.4), (-4.2 * s, -1.6 * s, 3.8 * s), 0.05)        # dirty amber, high and to one side
    area("mouth", 0.35, (0.9, 0.1, 0.04), (0.4 * s, -2.2 * s, -1.4 * s), 0.06)          # crimson from below
    area("rim", 2.5, (0.6, 0.14, 0.2), (3.4 * s, 2.0 * s, 2.0 * s), 0.1)          # burgundy rim behind
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    for name, (az, el, dist, lens, jaw) in views.items():
        pose(jaw)
        c = head_centre()
        a, e = math.radians(az), math.radians(el)
        cam.data.lens = lens
        cam.location = c + Vector((math.sin(a) * -dist * math.cos(e), -math.cos(a) * dist * math.cos(e), dist * math.sin(e)))
        cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(OUT, f"devourer-d0-look-{name}.png")
        bpy.ops.render.render(write_still=True)


build()
grade_skin()
s = 0.1
light_and_render({
    "frontal": (0.0, 2.0, 11.0 * s, 70, 26),
    "three-quarter": (34.0, 4.0, 11.0 * s, 70, 30),
    "mouth-close": (14.0, -10.0, 6.5 * s, 85, 34),
})
print("done", OUT)
