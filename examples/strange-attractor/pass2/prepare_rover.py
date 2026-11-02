"""The Curiosity rover (Art Pass 2, M3b): NASA's model (science.nasa.gov, public domain; assets-pass2.md) centred.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/prepare_rover.py -- <Curiosity_static.glb> <out rover.glb>

Centres the rover on its wheelbase at the ground (y = 0 after export), its length along +z. Prints the mast camera's
position in that frame ("MAST x y z"), where the scene puts the camera's flash.
"""
import sys

import bpy
from mathutils import Matrix, Vector

src, out = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
mesh = [o for o in bpy.data.objects if o.type == "MESH"]
for o in mesh:
    mw = o.matrix_world.copy()
    o.parent = None
    o.data.transform(mw)
    o.matrix_world = Matrix.Identity(4)
for o in list(bpy.data.objects):
    if o.type != "MESH":
        bpy.data.objects.remove(o)
pts = [v.co.copy() for o in mesh for v in o.data.vertices]
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
# Blender z-up after import: the ground is min z; centre x and y
c = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
for o in mesh:
    o.data.transform(Matrix.Translation(-c))
top = sorted((p - c for p in pts), key=lambda p: -p.z)[:200]
mast = sum(top, Vector()) / len(top)
print("SIZE", tuple(round(v, 2) for v in (hi - lo)))
# glTF: x stays, Blender z -> y, Blender y -> -z
print("MAST %.3f %.3f %.3f" % (mast.x, mast.z, -mast.y))
bpy.ops.export_scene.gltf(filepath=out, export_apply=True)
print("ROVER", out)
