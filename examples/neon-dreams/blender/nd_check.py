"""Neon Dreams: check renders of kit assets, loaded from the exported GLBs (so the check sees what the engine gets).

  Blender -b --factory-startup -P nd_check.py -- --kit DIR --out DIR --jobs name:asset:az:el:zoom[:tz],... [--mode day|night]
                                                  [--res 800] [--spp 24] [--palette art/palette.json]

az = degrees round from the front (0 = straight at the shop side, positive = from the right), el = elevation, zoom = how
much of the asset fills the frame (1 = all of it; 3 = a close look), tz = height of the target as a fraction of the
asset's height. day = flat neutral light for auditing geometry (window openings, frames, pipes); night = the kit lit by
its own lights in Ultraviolet Dream. Cycles on the CPU: no GPU needed.
"""
import bpy, sys, os, math, json
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nd_assemble as AS

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
KIT, OUT, MODE = arg("--kit", ""), arg("--out", ""), arg("--mode", "day")
RES, SPP = int(arg("--res", 800)), int(arg("--spp", 24))
PAL = AS.load_palette(arg("--palette", os.path.join(HERE, "..", "art", "palette.json")), arg("--state", "uv"))
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
if MODE == "day":
    AS.GAIN.update({k: 0.12 for k in AS.DEFAULT_KEY}); AS.GAIN["neon_a"] = 0.05; AS.GAIN["beacon"] = 0.03; AS.GAIN["garden"] = 0.05; AS.GAIN["lantern"] = 0.05
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
bg = w.node_tree.nodes["Background"]
sun = bpy.data.lights.new("sun", 'SUN'); so = bpy.data.objects.new("sun", sun); sc.collection.objects.link(so)
if MODE == "day":
    bg.inputs[0].default_value = (0.55, 0.6, 0.75, 1); bg.inputs[1].default_value = 1.0
    sun.energy = 3.0; sun.angle = math.radians(8); sun.color = (1.0, 0.96, 0.9)
else:
    bg.inputs[0].default_value = (*PAL["sky_horizon"], 1); bg.inputs[1].default_value = 1.6
    sun.energy = 0.35; sun.angle = math.radians(3); sun.color = PAL["moon_light"]
cam = bpy.data.cameras.new("c"); co = bpy.data.objects.new("c", cam); sc.collection.objects.link(co); sc.camera = co
cam.lens = 50; cam.sensor_width = 36; cam.clip_start = 0.1; cam.clip_end = 5000
bpy.ops.mesh.primitive_plane_add(size=4000, location=(0, 0, -30)); floor = bpy.context.object
fm = bpy.data.materials.new("floor"); fm.use_nodes = True
fm.node_tree.nodes["Principled BSDF"].inputs['Base Color'].default_value = (0.12, 0.12, 0.16, 1) if MODE == "day" else (0.02, 0.02, 0.04, 1)
floor.data.materials.append(fm)
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = SPP; sc.cycles.use_denoising = True
sc.cycles.max_bounces = 3; sc.cycles.diffuse_bounces = 2
sc.render.resolution_x = RES; sc.render.resolution_y = int(arg("--resy", RES))
try:
    sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast' if MODE == "night" else 'None'
except Exception:
    pass
sc.view_settings.exposure = float(arg("--ev", 2.6 if MODE == "day" else 1.0))
sc.render.image_settings.file_format = 'PNG'

placed = {}
for job in arg("--jobs", "").split(","):
    f = job.split(":")
    name, asset, az, el, zoom = f[0], f[1], float(f[2]), float(f[3]), float(f[4])
    tz = float(f[5]) if len(f) > 5 else 0.5
    for o in placed.values():
        o.hide_render = True
    if asset not in placed:
        placed[asset] = AS.place(KIT, asset, (0, 0, 0))
        AS.apply_palette(PAL)
    ob = placed[asset]; ob.hide_render = False
    bb = [Vector(c) for c in ob.bound_box]
    lo = Vector((min(c.x for c in bb), min(c.y for c in bb), max(min(c.z for c in bb), -3.0))); hi = Vector((max(c.x for c in bb), max(c.y for c in bb), max(c.z for c in bb)))
    size = hi - lo; tgt = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z + size.z * tz))
    span = max(size.z, math.hypot(size.x, size.y) * 0.85) / zoom
    aspect = sc.render.resolution_x / sc.render.resolution_y
    fov = 2 * math.atan(18 / cam.lens / max(aspect, 1.0 / aspect))                           # the tighter of the two fields of view
    dist = span / (2 * math.tan(fov / 2)) * 1.12 + max(size.x, size.y) / 2
    a, e = math.radians(az), math.radians(el)
    dirv = Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e)))      # front of a kit building is -y in Blender
    co.location = tgt + dirv * dist
    co.rotation_euler = (-dirv).to_track_quat('-Z', 'Y').to_euler()
    sd = Vector((math.sin(a - 0.7), -math.cos(a - 0.7), 0.75)).normalized()                   # key from the camera's left
    so.rotation_euler = (-sd).to_track_quat('-Z', 'Y').to_euler()
    floor.location.z = lo.z - (26.0 if MODE == "night" else 0.02) if lo.z > -2.9 else -30
    sc.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)
    print("CHECK", name, asset, "dist %.1f" % dist, flush=True)
