"""Hero-asset test stage: a 24 m ground disc in Poly Haven's red_sand (CC0), for isolated AV Gen proofs of one asset
under one fire and one moon. Never the real scene.

    Blender -b --factory-startup --python examples/strange-attractor/hero/proof_stage.py -- \
        assets/strange-attractor/polyhaven/textures/red_sand assets/strange-attractor/hero/stage-ground.glb
"""
import os
import sys

import bpy

texdir, out = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_circle_add(vertices=96, radius=12.0, fill_type="TRIFAN")
g = bpy.context.active_object
g.data.uv_layers.new(name="UVMap")
uv = g.data.uv_layers.active.data
for lp in g.data.loops:
    v = g.data.vertices[lp.vertex_index].co
    uv[lp.index].uv = (v.x / 2.0, v.y / 2.0)      # a 2 m tile
m = bpy.data.materials.new("red_sand")
m.use_nodes = True
nt = m.node_tree
b = nt.nodes["Principled BSDF"]
files = os.listdir(texdir)
diff = next(f for f in files if "diff" in f)
nor = next(f for f in files if "nor_gl" in f)
t = nt.nodes.new("ShaderNodeTexImage")
t.image = bpy.data.images.load(os.path.join(texdir, diff))
nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
tn = nt.nodes.new("ShaderNodeTexImage")
tn.image = bpy.data.images.load(os.path.join(texdir, nor))
tn.image.colorspace_settings.name = "Non-Color"
nm = nt.nodes.new("ShaderNodeNormalMap")
nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
b.inputs["Roughness"].default_value = 0.95
g.data.materials.append(m)
bpy.ops.export_scene.gltf(filepath=out, use_selection=False)
print("STAGE", out)
