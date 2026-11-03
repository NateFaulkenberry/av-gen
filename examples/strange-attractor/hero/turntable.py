"""Hero-asset audit (brief 49, Phase 1.1): turntable and topology stills of one .glb, in Blender (EEVEE).

    Blender -b --factory-startup --python examples/strange-attractor/hero/turntable.py -- <asset.glb> <out_prefix> \
        [--frame F] [--front DEG] [--focus top|rim|all] [--focus-frac 0.2] [--focus-angle DEG] [--wire 0.004]

Writes <out_prefix>-{000,090,180,270}.png (lit, neutral grey key/fill/rim), <out_prefix>-closeup.png (the focus region,
textured) and <out_prefix>-wire.png (the same, clay with the mesh wire, to judge topology). Skinned assets are posed at
--frame of their first action. Prints the evaluated bounds.
"""
import math
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
asset, prefix = argv[:2]
opt = {argv[i][2:]: argv[i + 1] if i + 1 < len(argv) and not argv[i + 1].startswith("--") else "1"
       for i in range(2, len(argv)) if argv[i].startswith("--")}
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=asset)
scene = bpy.context.scene
if "frame" in opt:
    scene.frame_set(int(opt["frame"]))
dg = bpy.context.evaluated_depsgraph_get()
lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
meshes = [o for o in scene.objects if o.type == "MESH" and not any(c.name.startswith("glTF_not_exported") for c in o.users_collection)]
for o in meshes:
    ev = o.evaluated_get(dg)
    m = ev.to_mesh()
    for v in m.vertices:
        w = ev.matrix_world @ v.co
        lo, hi = Vector(map(min, lo, w)), Vector(map(max, hi, w))
    ev.to_mesh_clear()
size = max(hi - lo)
centre = (lo + hi) / 2
print("BOUNDS", tuple(round(x, 3) for x in lo), tuple(round(x, 3) for x in hi))
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x, scene.render.resolution_y = 900, 900
scene.view_settings.view_transform = "AgX"
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.02, 0.022, 0.026, 1)
world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
scene.world = world
for rot, e, col in (((50, 0, 35), 3.5, (1, 0.97, 0.92)), ((65, 0, -120), 1.0, (0.8, 0.88, 1)),
                    ((110, 0, 180), 2.5, (0.9, 0.95, 1))):
    l = bpy.data.objects.new("l", bpy.data.lights.new("l", "SUN"))
    l.data.energy = e
    l.data.color = col
    l.data.angle = math.radians(3)
    l.rotation_euler = tuple(math.radians(a) for a in rot)
    scene.collection.objects.link(l)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 70
scene.collection.objects.link(cam)
scene.camera = cam


def shoot(target, extent, ang, path, elev=0.12):
    a = math.radians(ang)
    d = extent * 2.25 * 70 / 50
    cam.location = target + Vector((math.sin(a) * -d, math.cos(a) * -d, extent * elev * 2))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    cam.data.clip_start, cam.data.clip_end = d * 0.01, d * 10
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


front = float(opt.get("front", "0"))
for ang in (0, 90, 180, 270):
    shoot(centre, size * 0.62, ang + front, f"{prefix}-{ang:03d}.png")
focus = opt.get("focus", "top")
frac = float(opt.get("focus-frac", "0.2"))
ang = float(opt.get("focus-angle", "20")) + front
if focus == "top":
    t = Vector((centre.x, centre.y, hi.z - (hi.z - lo.z) * frac * 0.5))
    ext = (hi.z - lo.z) * frac * 1.1
elif focus == "rim":
    t = Vector((centre.x, lo.y, centre.z))
    ext = size * frac
else:
    t, ext = centre, size * 0.62
shoot(t, ext, ang, f"{prefix}-closeup.png", elev=0.05)
# clay + wire
clay = bpy.data.materials.new("clay")
clay.use_nodes = True
clay.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.45, 0.45, 0.47, 1)
clay.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.6
wire = bpy.data.materials.new("wire")
wire.use_nodes = True
wn = wire.node_tree.nodes
wn["Principled BSDF"].inputs["Base Color"].default_value = (0.02, 0.02, 0.02, 1)
for o in meshes:
    o.data.materials.clear()
    o.data.materials.append(clay)
    o.data.materials.append(wire)
    w = o.modifiers.new("wire", "WIREFRAME")
    w.thickness = ext * float(opt.get("wire", "0.004")) / max(1e-6, o.matrix_world.to_scale().x)
    w.use_replace = False
    w.material_offset = 1
    w.use_even_offset = False
    w.offset = 1.0
shoot(t, ext, ang, f"{prefix}-wire.png", elev=0.05)
print("TURNTABLE done", prefix, "size", round(size, 3))
