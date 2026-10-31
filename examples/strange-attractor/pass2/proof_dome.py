"""Art Pass 2, Gate R2 proof: a faceted crater dome (geodesic shell, structural ribs, inset glowing panels), built
headless in Blender and exported as glb for AV Gen.

    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup --python \
        examples/strange-attractor/pass2/proof_dome.py -- <out.glb>
"""
import bmesh
import bpy
import sys

out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)

# the shell: a frequency-3 geodesic hemisphere, unit radius
bm = bmesh.new()
bmesh.ops.create_icosphere(bm, subdivisions=3, radius=100.0)  # 100 m: a node scale is capped at 100
bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < -2.0], context="VERTS")
ribs_bm = bm.copy()

panel = bpy.data.materials.new("dome_panel")
panel.use_nodes = True
p = panel.node_tree.nodes["Principled BSDF"]
p.inputs["Base Color"].default_value = (0.9, 0.75, 0.45, 1)
p.inputs["Emission Color"].default_value = (1.0, 0.72, 0.38, 1)
p.inputs["Emission Strength"].default_value = 6.0
p.inputs["Roughness"].default_value = 0.6
frame = bpy.data.materials.new("dome_frame")
frame.use_nodes = True
f = frame.node_tree.nodes["Principled BSDF"]
f.inputs["Base Color"].default_value = (0.05, 0.05, 0.055, 1)
f.inputs["Roughness"].default_value = 0.45
f.inputs["Metallic"].default_value = 0.6

# panels: each face inset (the frame shows between them), some left dark (an ancient structure, not all lit)
res = bmesh.ops.inset_individual(bm, faces=list(bm.faces), thickness=3.5, depth=-1.0)
me = bpy.data.meshes.new("dome_panels")
bm.to_mesh(me)
obj = bpy.data.objects.new("dome_panels", me)
bpy.context.scene.collection.objects.link(obj)
obj.data.materials.append(panel)
obj.data.materials.append(frame)
import random
random.seed(7)
for poly in me.polygons:
    # the inset rims stay frame; a third of the panels are dark
    poly.material_index = 1 if (poly.area < 40.0 or random.random() < 0.33) else 0

# ribs: the geodesic edges as solid struts
me2 = bpy.data.meshes.new("dome_ribs")
ribs_bm.to_mesh(me2)
ribs = bpy.data.objects.new("dome_ribs", me2)
bpy.context.scene.collection.objects.link(ribs)
w = ribs.modifiers.new("struts", "WIREFRAME")
w.thickness = 3.0
w.offset = 1.0
ribs.data.materials.append(frame)
for o in (obj, ribs):
    o.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.gltf(filepath=out, export_apply=True, use_selection=False)
print("DOME", out)
