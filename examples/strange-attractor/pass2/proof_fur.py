"""Art Pass 2, Gate R2 proof: Sasquatch fur at a distance in fire light (Blender Cycles, a look test before the AV Gen
representation is chosen). The MPFB heavy base with particle fur, three of them around a fire point light at night.

    BLENDER_USER_RESOURCES=<dir> Blender --background <cast>/sasquatch-base.blend --python proof_fur.py -- <out.png>
"""
import math
import sys

import bpy
from mathutils import Vector

out = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
body = max((o for o in bpy.data.objects if o.type == "MESH"), key=lambda o: len(o.data.vertices))
bpy.context.view_layer.objects.active = body
if body.data.shape_keys:
    bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
for o in list(bpy.data.objects):
    if o.type == "MESH" and o is not body:
        bpy.data.objects.remove(o)
# proportions: longer arms and a heavier upper body are done by sculpt at M1; here the base is scaled up (2.3 m)
rig = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
root = rig if rig else body
root.scale = (1.35, 1.35, 1.38)

fur = bpy.data.materials.new("fur")
fur.use_nodes = True
nt = fur.node_tree
nt.nodes.clear()
hair = nt.nodes.new("ShaderNodeBsdfHairPrincipled")
hair.parametrization = "MELANIN"
hair.inputs["Melanin"].default_value = 0.92
hair.inputs["Roughness"].default_value = 0.45
o = nt.nodes.new("ShaderNodeOutputMaterial")
nt.links.new(hair.outputs[0], o.inputs["Surface"])
skin = bpy.data.materials.new("skin")
skin.use_nodes = True
skin.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.03, 0.02, 0.015, 1)
body.data.materials.clear()
body.data.materials.append(skin)
body.data.materials.append(fur)

ps = body.modifiers.new("fur", "PARTICLE_SYSTEM").particle_system
st = ps.settings
st.type = "HAIR"
st.count = 30000
st.hair_length = 0.09
st.child_type = "INTERPOLATED"
st.rendered_child_count = 12
st.child_length = 1.0
st.clump_factor = 0.4
st.roughness_2 = 0.03
st.material = 2
st.root_radius = 0.6
st.tip_radius = 0.1
st.radius_scale = 0.004

# three of them around a fire, in a loose triangle
others = []
for k, (x, y, rz) in enumerate([(0, 0, 0), (1.9, 1.5, 200), (-1.7, 1.7, 150)]):
    if k == 0:
        root.location = (x, y, 0)
        root.rotation_euler = (0, 0, math.radians(rz))
        continue
    dup = root.copy()
    if root.type == "ARMATURE":
        bpy.context.scene.collection.objects.link(dup)
        b2 = body.copy(); b2.data = body.data; b2.parent = dup
        bpy.context.scene.collection.objects.link(b2)
    else:
        bpy.context.scene.collection.objects.link(dup)
    dup.location = (x, y, 0)
    dup.rotation_euler = (0, 0, math.radians(rz))

ground = bpy.data.meshes.new("ground")
import bmesh
bm = bmesh.new()
bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=40)
bm.to_mesh(ground)
g = bpy.data.objects.new("ground", ground)
scene.collection.objects.link(g)
gm = bpy.data.materials.new("sand")
gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.25, 0.12, 0.07, 1)
g.data.materials.append(gm)

fire = bpy.data.objects.new("fire", bpy.data.lights.new("fire", "POINT"))
fire.data.energy = 900
fire.data.color = (1.0, 0.45, 0.15)
fire.data.shadow_soft_size = 0.4
fire.location = (0.0, 1.05, 0.35)
scene.collection.objects.link(fire)
moon = bpy.data.objects.new("moon", bpy.data.lights.new("moon", "SUN"))
moon.data.energy = 0.08
moon.data.color = (0.5, 0.6, 1.0)
moon.rotation_euler = (math.radians(40), 0, math.radians(120))
scene.collection.objects.link(moon)
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.004, 0.006, 0.015, 1)
scene.world = world
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 85
cam.location = (2.0, -16.0, 4.0)
cam.rotation_euler = (Vector((0, 0.9, 1.1)) - cam.location).to_track_quat("-Z", "Y").to_euler()
scene.collection.objects.link(cam)
scene.camera = cam
scene.render.engine = "CYCLES"
scene.cycles.samples = 48
scene.cycles.device = "CPU"
scene.render.resolution_x, scene.render.resolution_y = 960, 540
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print("FUR", out)
