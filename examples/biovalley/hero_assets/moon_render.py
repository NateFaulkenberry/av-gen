"""Moon phase renders: the sphere alone in black, lit from the side so the crescent comes from the terminator."""
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
glb, out = argv[0], argv[1]
build.clear_scene()
bpy.ops.import_scene.gltf(filepath=glb)
sc = bpy.context.scene
build.setup_lookdev(800)
for o in list(sc.objects):
    if o.type == "LIGHT" or o.name == "ground":
        bpy.data.objects.remove(o, do_unlink=True)
sc.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.0, 0.0, 0.0, 1)
sun = bpy.data.lights.new("sun", "SUN")
sun.energy = 6.0
so = bpy.data.objects.new("sun", sun)
sc.collection.objects.link(so)
cd = bpy.data.cameras.new("cam")
cd.lens = 85
cam = bpy.data.objects.new("cam", cd)
sc.collection.objects.link(cam)
cam.location = (0, -8, 0)
build.look_at(cam, (0, 0, 0))
sc.camera = cam
for k, ang in enumerate((115, 80, 35)):  # crescent, half, gibbous
    a = math.radians(ang)
    so.rotation_euler = (0, 0, 0)
    d = (math.sin(a), math.cos(a), 0.15)
    from mathutils import Vector
    so.rotation_euler = (-Vector(d)).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = os.path.join(out, f"moon_phase{k}.png")
    bpy.ops.render.render(write_still=True)
