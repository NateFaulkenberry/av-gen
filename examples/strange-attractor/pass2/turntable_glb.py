"""Art Pass 2, Gate R2: a four-view still of any glb/gltf (the cast sheet's candidates already in the asset library).

    Blender --background --factory-startup --python turntable_glb.py -- <asset> <out_prefix>
"""
import math
import sys

import bpy
from mathutils import Vector

asset, prefix = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=asset)
scene = bpy.context.scene
lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
tris = 0
for o in scene.objects:
    if o.type != "MESH":
        continue
    tris += sum(len(p.vertices) - 2 for p in o.data.polygons)
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        lo, hi = Vector(map(min, lo, w)), Vector(map(max, hi, w))
rigged = any(o.type == "ARMATURE" for o in scene.objects)
size = max(hi - lo)
centre = (lo + hi) / 2
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x, scene.render.resolution_y = 640, 640
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.05, 0.05, 0.06, 1)
scene.world = world
for rot, e in (((math.radians(50), 0, math.radians(35)), 3.0), ((math.radians(70), 0, math.radians(-140)), 0.8)):
    l = bpy.data.objects.new("l", bpy.data.lights.new("l", "SUN"))
    l.data.energy = e
    l.rotation_euler = rot
    scene.collection.objects.link(l)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scene.collection.objects.link(cam)
scene.camera = cam
for ang in (0, 90, 180, 270):
    a = math.radians(ang)
    d = size * 2.2
    cam.location = centre + Vector((math.sin(a) * -d, math.cos(a) * -d, size * 0.25))
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = f"{prefix}-{ang:03d}.png"
    bpy.ops.render.render(write_still=True)
print("TURNTABLE", asset, "tris", tris, "rigged", rigged, "size", tuple(round(x, 2) for x in (hi - lo)))
