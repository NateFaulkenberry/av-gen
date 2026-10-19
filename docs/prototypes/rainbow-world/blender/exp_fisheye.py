"""Gate-1 experiment E1: how wide must the lens be, and can a remapped rectilinear frame stand in for it?

Throwaway blockout (not art): rolling hills, a few blob trees, one rainbow band, a gradient sky.
Renders the SAME camera three ways with Cycles at low samples:
  rect_<fov>.png     rectilinear, horizontal FOV <fov> (the frame AV Gen can render today)
  fish_<fov>.png     true equisolid fisheye, <fov> degrees across the image circle (full-frame: the
                     circle is the frame DIAGONAL, so no black corners)
The remap (rect -> stereographic) is done afterwards by remap_fisheye.py.

Run: Blender -b --factory-startup -P exp_fisheye.py -- <outdir> [w h]
"""
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
out = argv[0] if argv else "build/rw/exp"
W = int(argv[1]) if len(argv) > 1 else 640
H = int(argv[2]) if len(argv) > 2 else 360
os.makedirs(out, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene


def mat(name, rgb, rough=0.8, emit=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Roughness"].default_value = rough
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1)
        b.inputs["Emission Strength"].default_value = strength
    return m


# Ground: a grid shaped by a handful of authored bumps (blockout only).
bpy.ops.mesh.primitive_grid_add(x_subdivisions=200, y_subdivisions=200, size=400)
g = bpy.context.object
bumps = [(-30, 60, 18, 40), (40, 90, 26, 55), (0, 150, 40, 70), (-90, 120, 35, 60), (80, 30, 10, 25),
         (-20, 25, 6, 15), (120, 160, 50, 80), (-140, 40, 30, 50)]
for v in g.data.vertices:
    x, y = v.co.x, v.co.y
    z = 0.0
    for bx, by, bh, br in bumps:
        z += bh * math.exp(-((x - bx) ** 2 + (y - by) ** 2) / (2 * br * br))
    v.co.z = z
bpy.ops.object.shade_smooth()
g.data.materials.append(mat("grass", (0.12, 0.42, 0.08)))

# Blob trees.
leaf = mat("leaf", (0.08, 0.3, 0.06))
bark = mat("bark", (0.25, 0.15, 0.08))
for i, (x, y) in enumerate([(-8, 18), (12, 30), (-25, 45), (30, 55), (5, 70), (-14, 2), (15, 4), (-10, -14), (11, -16), (-22, 12), (24, 14)]):
    z = sum(bh * math.exp(-((x - bx) ** 2 + (y - by) ** 2) / (2 * br * br)) for bx, by, bh, br in bumps)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.5, depth=6, location=(x, y, z + 3))
    bpy.context.object.data.materials.append(bark)
    for k in range(5):
        a = k * 1.3 + i
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=2.6 - 0.2 * k,
                                              location=(x + 2 * math.cos(a), y + 2 * math.sin(a), z + 7 + 0.6 * k))
        bpy.ops.object.shade_smooth()
        bpy.context.object.data.materials.append(leaf)

# Rainbow: a thick torus arc; colour by radius through a ramp, emissive and partly transparent.
bpy.ops.mesh.primitive_torus_add(major_radius=45, minor_radius=3, major_segments=160, minor_segments=24,
                                 location=(0, 70, 0), rotation=(math.radians(90), 0, 0))
rb = bpy.context.object
m = bpy.data.materials.new("rainbow")
m.use_nodes = True
nt = m.node_tree
nt.nodes.clear()
o = nt.nodes.new("ShaderNodeOutputMaterial")
geo = nt.nodes.new("ShaderNodeNewGeometry")
tc = nt.nodes.new("ShaderNodeTexCoord")
sep = nt.nodes.new("ShaderNodeSeparateXYZ")
nt.links.new(tc.outputs["Object"], sep.inputs[0])
# radial distance in the torus plane (x, y local), mapped 65..75 -> 0..1
vl = nt.nodes.new("ShaderNodeVectorMath")
vl.operation = "LENGTH"
cmb = nt.nodes.new("ShaderNodeCombineXYZ")
nt.links.new(sep.outputs[0], cmb.inputs[0])
nt.links.new(sep.outputs[1], cmb.inputs[1])
nt.links.new(cmb.outputs[0], vl.inputs[0])
mr = nt.nodes.new("ShaderNodeMapRange")
mr.inputs[1].default_value = 42.2
mr.inputs[2].default_value = 47.8
nt.links.new(vl.outputs["Value"], mr.inputs[0])
ramp = nt.nodes.new("ShaderNodeValToRGB")
cr = ramp.color_ramp
cols = [(0.0, (0.35, 0.05, 0.9)), (0.18, (0.05, 0.2, 1.0)), (0.36, (0.0, 0.8, 0.3)), (0.54, (1.0, 0.9, 0.0)),
        (0.72, (1.0, 0.4, 0.0)), (0.9, (1.0, 0.02, 0.05))]
cr.elements[0].position, cr.elements[0].color = cols[0][0], (*cols[0][1], 1)
cr.elements[1].position, cr.elements[1].color = cols[-1][0], (*cols[-1][1], 1)
for p, c in cols[1:-1]:
    e = cr.elements.new(p)
    e.color = (*c, 1)
nt.links.new(mr.outputs[0], ramp.inputs[0])
em = nt.nodes.new("ShaderNodeEmission")
em.inputs["Strength"].default_value = 3.0
nt.links.new(ramp.outputs[0], em.inputs[0])
tr = nt.nodes.new("ShaderNodeBsdfTransparent")
mix = nt.nodes.new("ShaderNodeMixShader")
mix.inputs[0].default_value = 0.55
nt.links.new(tr.outputs[0], mix.inputs[1])
nt.links.new(em.outputs[0], mix.inputs[2])
nt.links.new(mix.outputs[0], o.inputs[0])
rb.data.materials.append(m)

# Sky: vertical gradient + sun.
w = bpy.data.worlds.new("sky")
sc.world = w
w.use_nodes = True
wn = w.node_tree
bg = wn.nodes["Background"]
lp = wn.nodes.new("ShaderNodeTexCoord")
sx = wn.nodes.new("ShaderNodeSeparateXYZ")
wn.links.new(lp.outputs["Generated"], sx.inputs[0])
mrw = wn.nodes.new("ShaderNodeMapRange")
mrw.inputs[1].default_value = -0.1
mrw.inputs[2].default_value = 1.0
wn.links.new(sx.outputs[2], mrw.inputs[0])
rr = wn.nodes.new("ShaderNodeValToRGB")
rr.color_ramp.elements[0].position = 0.05
rr.color_ramp.elements[0].color = (0.55, 0.75, 0.95, 1)
rr.color_ramp.elements[1].position = 0.7
rr.color_ramp.elements[1].color = (0.05, 0.25, 0.8, 1)
wn.links.new(mrw.outputs[0], rr.inputs[0])
wn.links.new(rr.outputs[0], bg.inputs[0])
bg.inputs[1].default_value = 1.0
bpy.ops.object.light_add(type="SUN", rotation=(math.radians(50), 0, math.radians(30)))
bpy.context.object.data.energy = 4.0

# Camera: low over the grass, pitched up, rolled.
cz = sum(bh * math.exp(-((0 - bx) ** 2 + (-5 - by) ** 2) / (2 * br * br)) for bx, by, bh, br in bumps)
bpy.ops.object.camera_add(location=(0, -5, cz + 6.0))
cam = bpy.context.object
sc.camera = cam
d = Vector((0.0, 1.0, 0.05)).normalized()
cam.rotation_mode = "QUATERNION"
q = d.to_track_quat("-Z", "Y")
from mathutils import Quaternion
q = q @ Quaternion((0, 0, 1), math.radians(25))  # roll
cam.rotation_quaternion = q

sc.render.engine = "CYCLES"
sc.cycles.samples = 24
sc.cycles.use_denoising = True
try:
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "METAL"
    prefs.get_devices()
    for dv in prefs.devices:
        dv.use = True
    sc.cycles.device = "GPU"
except Exception as e:  # CPU fallback is fine for a 640 px test
    print("GPU setup failed:", e)
sc.view_settings.view_transform = "AgX"
sc.render.resolution_x, sc.render.resolution_y = W, H
sc.render.image_settings.file_format = "PNG"


def shoot(name):
    sc.render.filepath = os.path.join(out, name)
    bpy.ops.render.render(write_still=True)


for fov in (120, 140):
    cam.data.type = "PERSP"
    cam.data.sensor_fit = "HORIZONTAL"
    cam.data.angle = math.radians(fov)
    # 1.5x overscan for the remap test, so the centre is not upsampled
    sc.render.resolution_x, sc.render.resolution_y = int(W * 1.5), int(H * 1.5)
    shoot("rect_%d.png" % fov)
sc.render.resolution_x, sc.render.resolution_y = W, H
for fov in (150, 180, 220):
    cam.data.type = "PANO"
    cam.data.panorama_type = "FISHEYE_EQUISOLID"
    cam.data.fisheye_fov = math.radians(fov)
    cam.data.fisheye_lens = (36.0 * 0.5) / (2.0 * math.sin(math.radians(fov) / 4.0)) * \
        (math.hypot(W, H) / W)
    cam.data.sensor_fit = "HORIZONTAL"
    cam.data.sensor_width = 36.0
    shoot("fish_%d.png" % fov)
print("done", out)
