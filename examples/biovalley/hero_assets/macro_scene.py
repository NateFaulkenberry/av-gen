"""The asset-side macro composition for Checkpoints A and B (05-hero-asset-breakthrough.md section 3.3).

    Blender -b --factory-startup -P macro_scene.py -- <glb dir> <out.png> <final|clay> [layout]

The signature organism is placed with a few supporting forms at different depths, under one cool key light from the
upper left and a strong violet-blue rim from behind, with a dark ground, clear negative space and depth of field
focused on the hero. "clay" renders the same frame in neutral grey with no emission: the geometry must carry the image
on its own.
Layouts: "hero" (Checkpoint A) and "family" (every archetype in a row under the same light, Checkpoint B).
"""
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402
from render_glbs import clay  # noqa: E402

LAYOUTS = {
    # name: [(asset, x, y, yaw degrees, scale)], camera (location, target, lens, focus distance)
    "hero": dict(
        items=[("reliquary", 0.0, 0.0, 20, 1.0), ("veilcap", -0.46, 0.42, 0, 1.0), ("lacefan", 0.5, 0.55, -30, 1.0),
               ("chalice", 0.40, -0.12, 150, 1.0), ("lanternberry", -0.36, -0.10, 40, 1.0),
               ("glowmound", -0.42, -0.85, 0, 0.55)],
        cam=((0.05, -1.45, 0.22), (0.0, 0.0, 0.27), 36, 1.45)),
    "family": dict(
        items=[("reliquary", 0.0, 0.0, 20, 1.0), ("veilcap", -0.55, 0.05, 0, 1.0), ("chalice", 0.5, 0.0, 150, 1.0),
               ("filigree", -1.05, 0.1, 0, 1.0), ("lacefan", 1.02, 0.05, -10, 1.0), ("veilfern", -1.65, 0.15, 30, 0.8),
               ("bellspire", 1.55, 0.2, 0, 0.75), ("lanternberry", -2.15, 0.1, 40, 1.0),
               ("beadcluster", 2.0, 0.0, 0, 2.0), ("glowmound", -2.75, 0.25, 0, 0.45),
               ("bulbstalk", 2.4, 0.0, 0, 1.4), ("glowsedge", 2.9, 0.3, 0, 0.45), ("glowmat", -3.4, 0.1, 0, 0.9)],
        cam=((-0.2, -5.2, 0.55), (-0.2, 0.0, 0.25), 50, 5.2)),
}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    src, out, mode = argv[0], argv[1], argv[2]
    layout = LAYOUTS[argv[3] if len(argv) > 3 else "hero"]
    build.clear_scene()
    for w in list(bpy.data.worlds):
        bpy.data.worlds.remove(w)
    for asset, x, y, yaw, s in layout["items"]:
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=os.path.join(src, f"hero_{asset}_lod0.glb"))
        for o in set(bpy.data.objects) - before:
            if o.parent is None:
                o.location = (x, y, 0)
                o.rotation_euler = (o.rotation_euler[0], o.rotation_euler[1], math.radians(yaw))
                o.scale = (s, s, s)
    build.setup_lookdev(1200)
    sc = bpy.context.scene
    sc.render.resolution_x = 1600
    sc.render.resolution_y = 900
    # replace the turntable rig: a cool key from the upper left, a strong rim from behind, nothing else
    for o in list(sc.objects):
        if o.type == "LIGHT":
            bpy.data.objects.remove(o, do_unlink=True)

    def light(name, loc, energy, color, size):
        ld = bpy.data.lights.new(name, "AREA")
        ld.energy, ld.color, ld.size = energy, color, size
        o = bpy.data.objects.new(name, ld)
        o.location = loc
        sc.collection.objects.link(o)
        d = Vector((0, 0, 0.25)) - o.location
        o.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    k = 1.0 if argv[3:] in ([], ["hero"]) else 4.0
    light("key", (-1.0 * k, -0.9 * k, 1.3 * k), 7.0 * k * k, (0.6, 0.7, 1.0), 0.8 * k)
    light("rim", (0.3 * k, 1.6 * k, 0.9 * k), 22.0 * k * k, (0.45, 0.5, 1.0), 0.6 * k)
    sc.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.002, 0.003, 0.009, 1)
    if mode == "clay":
        clay()
        sc.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.02, 0.02, 0.025, 1)
        for o in sc.objects:
            if o.type == "LIGHT":
                o.data.energy *= 2.5
                o.data.color = (1, 1, 1)
    loc, tgt, lens, focus = layout["cam"]
    cd = bpy.data.cameras.new("cam")
    cd.lens = lens
    cd.clip_start = 0.01
    cd.dof.use_dof = True
    cd.dof.focus_distance = focus
    cd.dof.aperture_fstop = 2.8 if argv[3:] in ([], ["hero"]) else 8.0
    cam = bpy.data.objects.new("cam", cd)
    sc.collection.objects.link(cam)
    cam.location = loc
    build.look_at(cam, tgt)
    sc.camera = cam
    sc.render.filepath = out
    bpy.ops.render.render(write_still=True)


main()
