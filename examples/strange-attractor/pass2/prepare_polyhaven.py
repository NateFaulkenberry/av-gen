"""Poly Haven models (CC0, downloaded by the M3d fetch into assets/strange-attractor/polyhaven/models/; never
committed) prepared for the scenes: each imported, its base set on the ground (y = 0 after export) and centred in x/z,
exported as one .glb with its textures embedded to assets/strange-attractor/build/ph-<id>.glb.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/prepare_polyhaven.py -- assets/strange-attractor/polyhaven/models \
        assets/strange-attractor/build
"""
import bpy
import os
import sys
from mathutils import Vector

src, out = sys.argv[sys.argv.index("--") + 1:][:2]
for aid in sorted(os.listdir(src)):
    files = [f for f in os.listdir(os.path.join(src, aid)) if f.endswith(".gltf")]
    if not files:
        continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(src, aid, files[0]))
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    shift = Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
    for o in bpy.data.objects:
        if o.parent is None:
            o.location += shift
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, f"ph-{aid}.glb"), use_selection=True, export_apply=True)
    d = hi - lo
    print("PH", aid, "size", round(d.x, 2), round(d.y, 2), round(d.z, 2), "meshes", len(meshes))
