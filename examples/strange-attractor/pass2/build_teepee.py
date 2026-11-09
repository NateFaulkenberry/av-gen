"""The night camp's fire logs (Art Pass 2, M3d): five split logs leaned into a teepee, made from Poly Haven's
`dead_tree_trunk` (CC0; downloaded to assets/strange-attractor/polyhaven/models, never committed), charred at the foot.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_teepee.py -- assets/strange-attractor/polyhaven/models \
        assets/strange-attractor/build

Writes ph-teepee-m3e.glb: the logs' feet on a 0.42 m circle, leaning in to meet 0.7 m up, the base at y = 0.
M3e (the hero agent's audit): the logs lay flat -- an imported glTF object's rotation mode is QUATERNION, so setting its
Euler did nothing; the copies now rotate in XYZ. The scan is decimated to 8% (it was 509k triangles for five logs).
An interim fire: the hero-asset agent replaces the logs and flames (H4). A new name, so M3d's file is kept.
"""
import bpy
import math
import os
import sys
from mathutils import Euler, Vector

src, out = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
d = os.path.join(src, "dead_tree_trunk")
bpy.ops.import_scene.gltf(filepath=os.path.join(d, [f for f in os.listdir(d) if f.endswith(".gltf")][0]))
log = [o for o in bpy.data.objects if o.type == "MESH"][0]
bpy.context.view_layer.objects.active = log
log.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
# its length along x (3.05 m): centre it, scale to a 1.0 m log
lo = Vector((min(v.co.x for v in log.data.vertices), min(v.co.y for v in log.data.vertices), min(v.co.z for v in log.data.vertices)))
hi = Vector((max(v.co.x for v in log.data.vertices), max(v.co.y for v in log.data.vertices), max(v.co.z for v in log.data.vertices)))
c = (lo + hi) / 2
for v in log.data.vertices:
    v.co = (v.co - c) * (1.0 / (hi.x - lo.x))
dec = log.modifiers.new("decimate", "DECIMATE")
dec.ratio = 0.08
bpy.ops.object.modifier_apply(modifier=dec.name)
for k in range(5):
    o = log.copy()
    o.data = log.data.copy()
    bpy.context.scene.collection.objects.link(o)
    yaw = k * 2 * math.pi / 5 + 0.3
    lean = math.radians(52.0 + 6.0 * ((k * 37) % 5) / 4.0)
    # a log along x, its foot at the circle, leaning in: rotate up about y by the lean, then round z by the yaw
    o.rotation_mode = "XYZ"   # an imported glTF object is QUATERNION: an Euler set on it is ignored
    o.rotation_euler = Euler((0.0, -lean, yaw), "XYZ")
    foot = Vector((0.42 * math.cos(yaw + math.pi), 0.42 * math.sin(yaw + math.pi), 0.0))
    o.location = foot + Vector((math.cos(yaw), math.sin(yaw), 0.0)) * 0.5 * math.cos(lean) * -1.0 * -1.0 + Vector((0, 0, 0.5 * math.sin(lean)))
bpy.data.objects.remove(log)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=os.path.join(out, "ph-teepee-m3e.glb"), use_selection=True, export_apply=True)
print("TEEPEE ok")
