"""CHRONOS, The Devourer (Gate D1): survey the owner's downloaded bases under the creature's lighting.

For each glb in assets/chronos/sketchfab/*/: triangles, materials and textures, bounds; then one chiaroscuro render
(the D0/D1 light: a hard dirty-amber key high to one side, a faint crimson under-light, a burgundy rim, near black)
from the front and from three-quarter, so the bases can be compared on the same footing.

    /Applications/Blender.app/Contents/MacOS/Blender --background --python examples/chronos/devourer/survey_scans.py \
        -- <sketchfab_dir> <out_dir>

Prints a table and writes <out_dir>/<folder>-<view>.png. Nothing it makes is committed.
"""
import glob
import json
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC, OUT = argv[0], argv[1]
os.makedirs(OUT, exist_ok=True)
report = {}

for path in sorted(glob.glob(os.path.join(SRC, "*", "*.glb"))):
    folder = os.path.basename(os.path.dirname(path))
    bpy.ops.wm.read_homefile(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    tris = 0
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for o in meshes:
        dg = bpy.context.evaluated_depsgraph_get()
        me = o.evaluated_get(dg).to_mesh()
        me.calc_loop_triangles()
        tris += len(me.loop_triangles)
        for v in me.vertices:
            w = o.matrix_world @ v.co
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
        o.evaluated_get(dg).to_mesh_clear()
    textures = sorted({f"{n.image.name} {n.image.size[0]}x{n.image.size[1]}" for m in bpy.data.materials if m.use_nodes
                       for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image})
    rigs = [o.name for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    report[folder] = {"file": os.path.basename(path), "meshes": len(meshes), "triangles": tris,
                      "size": [round(x, 3) for x in (hi - lo)], "textures": textures, "rigs": rigs,
                      "materials": len(bpy.data.materials)}
    # frame and light
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 900, 900
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - High Contrast"
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.01, 0.003, 0.003, 1)
    scene.world = world
    c = (lo + hi) * 0.5
    size = max((hi - lo).x, (hi - lo).y, (hi - lo).z)
    # glTF imports Y-up as Blender Z-up with the model facing -Y (glTF +Z toward the viewer)

    def area(name, energy, color, offset, sz):
        L = bpy.data.lights.new(name, "AREA")
        L.energy, L.color, L.size = energy * size * size, color, sz * size
        o = bpy.data.objects.new(name, L)
        o.location = c + Vector(offset) * size
        o.rotation_euler = (c - o.location).to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(o)
    area("key", 600.0, (1.0, 0.7, 0.4), (-1.1, -1.4, 1.1), 0.2)
    area("under", 40.0, (0.9, 0.1, 0.04), (0.1, -1.4, -0.9), 0.3)
    area("rim", 200.0, (0.6, 0.14, 0.2), (1.0, 1.0, 0.6), 0.4)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.data.lens = 60
    for view, az in (("front", 0.0), ("three-quarter", 35.0)):
        a = math.radians(az)
        d = size * 2.1
        cam.location = c + Vector((-math.sin(a) * d, -math.cos(a) * d, 0.05 * size))
        cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(OUT, f"{folder}-{view}.png")
        bpy.ops.render.render(write_still=True)
    print("surveyed", folder, report[folder])

with open(os.path.join(OUT, "survey.json"), "w") as f:
    json.dump(report, f, indent=1)
print("done")
