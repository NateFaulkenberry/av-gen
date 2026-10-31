"""Art Pass 2, Gate R2: candidate human cast built with MPFB 2 (MakeHuman for Blender; its bundled and Suits 01 assets
are CC0) and a turntable still per character.

Run headless, with MPFB installed into a private Blender user directory (the user's own Blender is not touched):

    BLENDER_USER_RESOURCES=<dir> /Applications/Blender.app/Contents/MacOS/Blender --background \
        --python examples/strange-attractor/pass2/mpfb_cast.py -- <out_dir> [names...]

Writes <out_dir>/<name>.blend and <out_dir>/<name>-turntable.png (four views, 0/90/180/270 degrees). Nothing it makes is
committed: the outputs are third-party-derived (CC0, but the rule is that assets stay out of the public repository).
"""
import math
import os
import sys

import bpy
from mathutils import Vector

from bl_ext.user_default.mpfb.services.humanservice import HumanService

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/cast"
WANT = set(argv[1:])
os.makedirs(OUT, exist_ok=True)


def phenotype(gender, age, muscle, weight, height, proportions=0.5, caucasian=1.0):
    return {"gender": gender, "age": age, "muscle": muscle, "weight": weight, "proportions": proportions,
            "height": height, "cupsize": 0.5, "firmness": 0.5,
            "race": {"asian": (1 - caucasian) / 2, "caucasian": caucasian, "african": (1 - caucasian) / 2}}


# The cast. Each is a plan for the scene, not yet a final asset (Gate R2 shows the candidates).
CAST = {
    # The suited man on the gurney (brief 6.3): dark suit, white shirt, tie.
    "suited-man": dict(phenotype=phenotype(1.0, 0.45, 0.5, 0.55, 0.55), skin="young_caucasian_male/young_caucasian_male.mhmat",
                       hair="short02/short02.mhclo", eyebrows="eyebrow001/eyebrow001.mhclo",
                       clothes=["toigo_male_suit_tie_and_jacket/toigo_male_suit_tie_and_jacket.mhclo", "shoes02/shoes02.mhclo"]),
    # The Nordic / angelic humanoids (brief 6.2): tall, symmetrical, pale, light hair, simple garments.
    "nordic-female": dict(phenotype=phenotype(0.0, 0.4, 0.45, 0.4, 1.0, proportions=1.0), skin="young_caucasian_female/young_caucasian_female.mhmat",
                          hair="long01/long01.mhclo", eyebrows="eyebrow010/eyebrow010.mhclo",
                          clothes=["female_elegantsuit01/female_elegantsuit01.mhclo"]),
    "nordic-male": dict(phenotype=phenotype(1.0, 0.4, 0.55, 0.4, 1.0, proportions=1.0), skin="young_caucasian_male2/young_caucasian_male2.mhmat",
                        hair="long01/long01.mhclo", eyebrows="eyebrow002/eyebrow002.mhclo",
                        clothes=["male_elegantsuit01/male_elegantsuit01.mhclo"]),
    # Base bodies to be built over (Blender sculpt + AV Gen shell fur / proportions): not final characters.
    "sasquatch-base": dict(phenotype=phenotype(1.0, 0.6, 1.0, 1.0, 1.0, proportions=0.0), skin="", hair="", eyebrows="", clothes=[]),
    "gray-base": dict(phenotype=phenotype(0.5, 0.5, 0.0, 0.0, 0.3, proportions=0.0), skin="", hair="", eyebrows="", clothes=[]),
}


def clear():
    bpy.ops.wm.read_homefile(use_empty=True)


def build(name, spec):
    info = HumanService._create_default_human_info_dict()
    info["name"] = name
    info["phenotype"] = spec["phenotype"]
    info["eyes"] = "high-poly/high-poly.mhclo"
    info["eyebrows"] = spec["eyebrows"]
    info["eyelashes"] = "eyelashes01/eyelashes01.mhclo"
    info["hair"] = spec["hair"]
    info["clothes"] = spec["clothes"]
    info["skin_mhmat"] = spec["skin"]
    info["skin_material_type"] = "ENHANCED_SSS" if spec["skin"] else "NONE"
    info["rig"] = "default"
    info["alternative_materials"] = {}
    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 0
    return HumanService.deserialize_from_dict(info, settings)


def bounds():
    lo, hi = Vector((1e9, 1e9, 1e9)), Vector((-1e9, -1e9, -1e9))
    for o in bpy.context.scene.objects:
        if o.type != "MESH" or o.hide_render:
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
    return lo, hi


def stage_and_render(name):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = 640, 960
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.05, 0.05, 0.06, 1)
    scene.world = world
    lo, hi = bounds()
    h = hi.z - lo.z
    centre = (lo + hi) / 2
    key = bpy.data.objects.new("key", bpy.data.lights.new("key", "SUN"))
    key.data.energy = 3.0
    key.rotation_euler = (math.radians(50), 0, math.radians(35))
    scene.collection.objects.link(key)
    fill = bpy.data.objects.new("fill", bpy.data.lights.new("fill", "SUN"))
    fill.data.energy = 0.8
    fill.rotation_euler = (math.radians(70), 0, math.radians(-140))
    scene.collection.objects.link(fill)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 50
    scene.collection.objects.link(cam)
    scene.camera = cam
    frames = []
    for k, ang in enumerate((0, 90, 180, 270)):
        a = math.radians(ang)
        d = h * 1.7
        cam.location = centre + Vector((math.sin(a) * -d, math.cos(a) * -d, 0.05 * h))
        cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
        path = os.path.join(OUT, f"{name}-{ang:03d}.png")
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        frames.append(path)
    return frames


for name, spec in CAST.items():
    if WANT and name not in WANT:
        continue
    clear()
    build(name, spec)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, f"{name}.blend"))
    stage_and_render(name)
    print("CAST", name, "done")
