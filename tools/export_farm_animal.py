"""Export one animal from FarmAnimalsLowpoly.blend as a GLB.

Run as:  blender -b <blend> --python tools/export_farm_animal.py -- \
             <mesh-object> <armature-object> <action> <out.glb>

The source is one scene holding all nine animals laid out along X for a turntable render, plus a
`Render` collection (camera, backdrop plane, three area lights, an animated empty) that is
authoring apparatus. Each animal is an armature with one skinned mesh child and one `*Walk` action.
Every animal shares `MainMaterial` and its packed 16x16 colour-palette texture.

Three things this does beyond "select and export", each for a reason recorded in ADR-205:

* It **deletes** the other eight animals and the render rig rather than hiding them. An exporter set
  to "selected objects" still walks parents, and Blender's glTF exporter in ACTIONS mode tries every
  action in the file against the armature -- leaving `CowWalk` in the file while exporting the bull
  yields a bull.glb with nine animations, eight of them the wrong skeleton's.
* It **applies the non-armature modifiers**. The pig is modelled as a half-pig with a Mirror
  modifier; `export_apply=False` (mandatory, since applying modifiers would bake the armature away)
  would otherwise ship half a pig.
* It **zeroes the armature's object location**. The animals are laid out along X in the source --
  the horse sits at x=-1.76, the chick at x=+2.41 -- which is a property of the turntable scene, not
  of the animal. A prop belongs at its own origin.
"""
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
mesh_name, arm_name, action_name, out = argv[0], argv[1], argv[2], argv[3]

keep = {mesh_name, arm_name}
for o in list(bpy.data.objects):
    if o.name not in keep:
        bpy.data.objects.remove(o, do_unlink=True)

mesh = bpy.data.objects.get(mesh_name)
arm = bpy.data.objects.get(arm_name)
if mesh is None or arm is None:
    raise SystemExit(f"missing object: {mesh_name} / {arm_name}")

# One animal, one clip, and the clip is called `Walk`. The source names it per species -- `BullWalk`,
# `ChickWalk` -- which is how it has to be when nine rigs share one file. Once each animal is its own
# GLB the species is the filename, and a scene author asking for `"state": "Walk"` should get it from
# any of the nine. ADR-192 cleaned the alien clip names at export for the same reason. The source
# action names are recorded in assets/farm/ATTRIBUTION.md.
if arm.animation_data is not None:
    # The cow's armature carries an `[Action Stash]` NLA track holding the *bull's* action. Stashes
    # are authoring undo, and `export_nla_strips=False` ignores them, but the action it pins would
    # survive the sweep below as a strip user.
    for track in list(arm.animation_data.nla_tracks):
        arm.animation_data.nla_tracks.remove(track)
for action in list(bpy.data.actions):
    if action.name != action_name:
        bpy.data.actions.remove(action, do_unlink=True)
action = bpy.data.actions.get(action_name)
if action is None:
    raise SystemExit(f"missing action {action_name}")
action.name = "Walk"
if arm.animation_data is None:
    arm.animation_data_create()
arm.animation_data.action = action

# The pig is half a pig plus a Mirror modifier. Applying the *armature* modifier would bake the rig
# away, so the export must run with `export_apply=False` -- which means anything else in the stack
# has to be applied here, by hand, first.
bpy.context.view_layer.objects.active = mesh
for mod in [m for m in mesh.modifiers if m.type != "ARMATURE"]:
    bpy.ops.object.modifier_apply(modifier=mod.name)

# Laid out along X for a turntable render. That belongs to the source scene, not to the animal.
arm.location = (0.0, 0.0, 0.0)

for m in list(bpy.data.materials):
    if m.users == 0 or m.name in ("Dots Stroke", "BG"):
        bpy.data.materials.remove(m, do_unlink=True)
for img in list(bpy.data.images):
    if img.name == "Render Result":
        bpy.data.images.remove(img, do_unlink=True)

bpy.ops.object.select_all(action="DESELECT")
for o in (mesh, arm):
    o.hide_set(False)
    o.hide_viewport = False
    o.hide_render = False
    o.select_set(True)
bpy.context.view_layer.objects.active = arm

os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
bpy.ops.export_scene.gltf(
    filepath=out,
    export_format="GLB",
    use_selection=True,
    export_yup=True,                  # glTF convention; the importer expects it. Blender -Y (which
                                      # is where all nine animals face) becomes glTF +Z, which is
                                      # the direction the spec says the front of an asset faces.
    export_apply=False,               # never: applying modifiers would bake the armature away
    export_animations=True,
    export_animation_mode="ACTIONS",  # one glTF animation per Blender action, named after it
    export_nla_strips=False,
    export_bake_animation=False,
    export_anim_single_armature=True,
    export_def_bones=False,           # 14-29 bones per rig, all deforming; nothing to strip
    export_rest_position_armature=True,
    export_morph=False,               # no shape keys in the source, and AV Gen ignores morphs
    export_skins=True,
    export_materials="EXPORT",
    export_image_format="AUTO",       # The one texture is a 16x16 colour palette, packed into the
                                      # .blend as a JPEG, and every material in the file reads its
                                      # base colour from a single texel of it -- so a second
                                      # generation of JPEG ringing would be ringing in the animal's
                                      # actual colour. AUTO does not re-encode: it copies the packed
                                      # bytes verbatim (verified byte-identical, 4,066 of them), and
                                      # AV Gen decodes JPEG through stb_image like any other format.
    export_cameras=False,
    export_lights=False,
    export_extras=False,
    # Every frame, and no curve simplification. The source F-curves are Bezier with 5-14 keys over a
    # 25-28 frame cycle, and glTF has no Bezier sampler -- only STEP, LINEAR and CUBICSPLINE -- so
    # Blender samples the curve instead (`export_force_sampling`, on by default and pinned here).
    # `export_optimize_animation_size` then thins those samples back to the ones a straight line can
    # rejoin. Measured frame by frame against Blender's own posed mesh, that thinning changes
    # *nothing* at frame times: it only ever drops a sample its neighbours already reproduce. It is
    # off anyway, because the engine samples a clip at continuous render time rather than at frame
    # boundaries, where the thinning is a tolerance rather than an identity -- and 28 samples a
    # channel instead of 10 costs 19 KB on a 367 KB file.
    export_force_sampling=True,
    export_optimize_animation_size=False,
    # Without this the first sample lands at frame_start/fps = 1/30 s, not at 0, and the clip opens
    # by holding its first pose for a frame and closes a frame short of its own loop point.
    export_anim_slide_to_zero=True,
)
print(f"###EXPORTED### {out} {os.path.getsize(out)}")
