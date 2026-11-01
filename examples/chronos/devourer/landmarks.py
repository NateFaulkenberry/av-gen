"""CHRONOS, The Devourer (Gate D1): orthographic front and side views of a scan, on a metric grid, for picking
landmarks (eyes, mouth line, chin, neck cut) by eye. The landmarks then live in build_scan_creature.py.

    /Applications/Blender.app/Contents/MacOS/Blender --background --python examples/chronos/devourer/landmarks.py \
        -- <scan.glb> <out_dir> [zoom_centre_x zoom_centre_z zoom_size]

Writes <out_dir>/landmarks-front.png and -side.png with grid lines every 0.05 units (labelled every 0.1) in the
scan's own normalised frame (the head is scaled so the bounding box is 1 unit tall and centred on x and y).
"""
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
SCAN, OUT = argv[0], argv[1]
ZOOM = [float(x) for x in argv[2:5]] if len(argv) >= 5 else None
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_homefile(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SCAN)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
for o in list(bpy.context.scene.objects):
    if o.type == "EMPTY":
        bpy.data.objects.remove(o)
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
lo = Vector([min(v.co[i] for v in obj.data.vertices) for i in range(3)])
hi = Vector([max(v.co[i] for v in obj.data.vertices) for i in range(3)])
s = 1.0 / (hi.z - lo.z)
for v in obj.data.vertices:
    v.co = Vector(((v.co.x - (lo.x + hi.x) / 2) * s, (v.co.y - (lo.y + hi.y) / 2) * s, (v.co.z - lo.z) * s))
obj.data.update()
print("normalised: x", (hi.x - lo.x) * s, "y", (hi.y - lo.y) * s, "z 1.0; scale factor", s)
scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = scene.render.resolution_y = 1200
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.2, 0.2, 0.22, 1)
scene.world = world
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(55), 0, math.radians(-30))
scene.collection.objects.link(sun)
# grid lines as thin boxes
mat = bpy.data.materials.new("grid")
mat.use_nodes = True
mat.node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (1, 0.2, 0.1, 1)
mat.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 3.0
mat2 = bpy.data.materials.new("grid2")
mat2.use_nodes = True
mat2.node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (0.1, 0.8, 1, 1)
mat2.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 3.0


def line(a, b, w, m):
    bpy.ops.mesh.primitive_cube_add(size=1)
    c = bpy.context.active_object
    c.location = (a + b) / 2
    d = b - a
    c.scale = (max(abs(d.x), w), max(abs(d.y), w), max(abs(d.z), w))
    c.data.materials.append(m)


for k in range(0, 21):
    z = k * 0.05
    m = mat if k % 2 == 0 else mat2
    w = 0.0015 if k % 2 == 0 else 0.0008
    line(Vector((-0.6, -0.7, z)), Vector((0.6, -0.7, z)), w, m)   # front view horizontals (in front)
    line(Vector((0.7, -0.6, z)), Vector((0.7, 0.6, z)), w, m)     # side view horizontals (to the right)
for k in range(-12, 13):
    x = k * 0.05
    m = mat if k % 2 == 0 else mat2
    w = 0.0015 if k % 2 == 0 else 0.0008
    line(Vector((x, -0.7, 0)), Vector((x, -0.7, 1)), w, m)       # front verticals = x
    line(Vector((0.7, x, 0)), Vector((0.7, x, 1)), w, m)         # side verticals = y
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.type = "ORTHO"
scene.collection.objects.link(cam)
scene.camera = cam
for name, loc, rot in (("front", (0, -3, 0.5), (math.radians(90), 0, 0)), ("side", (-3, 0, 0.5), (math.radians(90), 0, math.radians(-90)))):
    cam.location = loc
    cam.rotation_euler = rot
    cam.data.ortho_scale = 1.15
    if ZOOM:
        cx, cz, sz = ZOOM
        if name == "front":
            cam.location = (cx, -3, cz)
        else:
            cam.location = (-3, 0, cz)
        cam.data.ortho_scale = sz
    scene.render.filepath = os.path.join(OUT, f"landmarks-{name}.png")
    bpy.ops.render.render(write_still=True)
print("done")
