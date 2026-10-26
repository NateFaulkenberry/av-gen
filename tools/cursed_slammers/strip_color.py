"""Cursed Slammers POC 4: the same face mesh with its colour texture removed (base colour white), the baked normal map
kept. A material program's base colour is multiplied by the asset's colour texture after the program runs, so on the
scan's near-black texture no metal could read as silver; the surface variants draw this copy and set every colour
themselves. Input and output are gitignored (derived from the CC-BY scan).

    Blender -b --factory-startup -P tools/cursed_slammers/strip_color.py -- --src <in.glb> --out <out.glb>
"""
import argparse
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--src", required=True)
ap.add_argument("--out", required=True)
a = ap.parse_args(argv)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()
bpy.ops.import_scene.gltf(filepath=a.src)
for m in bpy.data.materials:
    if not m.node_tree:
        continue
    bsdf = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        continue
    for link in list(bsdf.inputs["Base Color"].links):
        m.node_tree.links.remove(link)
    bsdf.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
ob = [o for o in bpy.context.scene.objects if o.type == "MESH"][0]
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=a.out, export_format="GLB", use_selection=True, export_yup=True,
                          export_apply=True, export_normals=True, export_texcoords=True, export_materials="EXPORT")
print("[strip_color] wrote", a.out)
