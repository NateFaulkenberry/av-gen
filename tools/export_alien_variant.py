"""Export one modular alien variant from Alien_Modular_animated.blend as a GLB.

Run as:  blender -b <blend> --python tools/export_alien_variant.py -- \
             <head> <body> <pack|none> <out.glb> [--morphs]

The source is an Auto-Rig Pro rig: 378 bones of which 89 deform, plus a collection of `cs_*`
control-shape meshes and a 4K HDR that exist only for authoring. None of that ships.
"""
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
head, body, pack, out = argv[0], argv[1], argv[2], argv[3]
# Off by default: AV Gen refuses morph targets in two places on purpose -- the geometry import warns
# "morph targets ignored" (assets/gltf_loader.cpp) and the animation import drops morph-weight
# channels because `AnimationPath` has only translation/rotation/scale. Shipping them would add
# ~104 KB a variant that nothing reads and two warnings to every load of every character.
#
# The source .blend keeps all ten shape keys per head and body, and `--morphs` puts them back the
# day the engine grows a use for them. That is the whole migration.
morphs = "--morphs" in argv

keep = {"rig", head, body}
if pack and pack.lower() != "none":
    keep.add(pack)

# Everything else goes, rather than being hidden: an exporter set to "selected objects" still walks
# parents, and a hidden object that is somebody's parent comes along anyway.
for o in list(bpy.data.objects):
    if o.name not in keep:
        bpy.data.objects.remove(o, do_unlink=True)

# The 4K HDR is an authoring lamp, not a character texture, and it is 32 MB of the 33 MB file.
for img in list(bpy.data.images):
    if img.name.endswith(".hdr") or img.name == "Render Result":
        bpy.data.images.remove(img, do_unlink=True)
for m in list(bpy.data.materials):
    if m.users == 0 or m.name == "Dots Stroke":
        bpy.data.materials.remove(m, do_unlink=True)

# Clean, stable clip names. The source actions carry authoring debris -- a leading underscore on the
# five that were made first, two misspellings, and a hyphen that is awkward in an identifier -- and a
# scene author should be typing `Running`, not `_Runing`.
RENAME = {
    "_Idle": "Idle", "_Idle_turn": "Idle_turn", "_Jumping": "Jumping",
    "_Runing": "Running", "_Walking": "Walking",
    "Jump_runing": "Jump_running", "Dying_forward_": "Dying_forward",
    "Flying-jet": "Flying_jet",
}
for action in bpy.data.actions:
    if action.name in RENAME:
        action.name = RENAME[action.name]

bpy.ops.object.select_all(action="DESELECT")
for name in keep:
    o = bpy.data.objects.get(name)
    if o is None:
        raise SystemExit(f"missing object {name}")
    o.hide_set(False)
    o.hide_viewport = False
    o.hide_render = False
    o.select_set(True)
bpy.context.view_layer.objects.active = bpy.data.objects["rig"]

os.makedirs(os.path.dirname(out), exist_ok=True)
bpy.ops.export_scene.gltf(
    filepath=out,
    export_format="GLB",
    use_selection=True,
    export_yup=True,                 # glTF convention; the importer expects it
    export_apply=False,              # never: applying modifiers would bake the armature away
    export_animations=True,
    export_animation_mode="ACTIONS",  # one glTF animation per Blender action, named after it
    export_nla_strips=False,
    export_bake_animation=False,
    export_anim_single_armature=True,
    export_def_bones=True,           # 89 deform bones, not 378 control bones
    export_rest_position_armature=True,
    export_morph=morphs,
    export_morph_normal=False,       # position deltas only: normals double the morph data
    export_morph_tangent=False,
    export_skins=True,
    export_materials="EXPORT",
    export_image_format="AUTO",
    export_cameras=False,
    export_lights=False,
    export_extras=False,
    export_optimize_animation_size=True,
    export_optimize_animation_keep_anim_armature=True,
)
print(f"###EXPORTED### {out} {os.path.getsize(out)}")
