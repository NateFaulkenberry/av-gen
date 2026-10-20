"""Gate-1 experiment E2: does a closed, twisting band of land give the clip's changing gravity and a full frame?

Throwaway blockout (grey-box, not art). A trefoil torus knot carries a flattened, closed cross-section
(a long floating 'island' with no free edge): meadow on both broad faces, a rock rim at the sides. The
band twists one full turn per loop so the local 'up' rotates as the camera travels. The camera hugs
the surface (up = the band's local normal) and looks along the travel direction; a few hundred blob
clouds sit in the free space and a wide rainbow torus spans two strands.

Renders: overview.png (outside, rectilinear) and cam_<k>.png along the path (equisolid fisheye).
Run: Blender -b --factory-startup -P exp_knot.py -- <outdir> [w h fisheye_deg]
"""
import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
out = argv[0] if argv else "build/rw/exp_knot"
W = int(argv[1]) if len(argv) > 1 else 640
H = int(argv[2]) if len(argv) > 2 else 360
FISH = float(argv[3]) if len(argv) > 3 else 180.0
os.makedirs(out, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
rng = random.Random(7)

S = 160.0  # metres per knot unit
N = 900    # samples along the knot
M = 48     # samples round the cross-section
HALF_W, HALF_T = 45.0, 9.0
TWISTS = 1.0  # extra full turns of the band per loop


def knot(t):
    # trefoil (2,3) torus knot, z squashed
    a = 2 * math.pi * t
    r = 2.0 + math.cos(3 * a)
    return Vector((r * math.cos(2 * a), r * math.sin(2 * a), -0.8 * math.sin(3 * a))) * S


pts = [knot(i / N) for i in range(N)]
tans = [(pts[(i + 1) % N] - pts[i - 1]).normalized() for i in range(N)]
# parallel transport frame
ups = []
u = Vector((0, 0, 1))
u = (u - tans[0] * u.dot(tans[0])).normalized()
for i in range(N):
    t = tans[i]
    u = (u - t * u.dot(t)).normalized()
    ups.append(u.copy())
# holonomy correction so the frame closes, plus the authored twist
end = ups[-1]
t0 = tans[0]
cosang = max(-1.0, min(1.0, end.dot(ups[0])))
ang = math.acos(cosang)
if t0.cross(end).dot(ups[0]) < 0:
    ang = -ang
frames = []
for i in range(N):
    s = i / N
    tw = -ang * s + 2 * math.pi * TWISTS * s
    up = (Matrix.Rotation(tw, 3, tans[i]) @ ups[i]).normalized()
    side = tans[i].cross(up).normalized()
    frames.append((pts[i], tans[i], up, side))


def section(j):
    # superellipse: flat broad faces, rounded rim
    a = 2 * math.pi * j / M
    c, s_ = math.cos(a), math.sin(a)
    e = 0.35
    x = math.copysign(abs(c) ** e, c) * HALF_W
    y = math.copysign(abs(s_) ** e, s_) * HALF_T
    return x, y


verts, faces = [], []
for i, (p, t, up, side) in enumerate(frames):
    # gentle authored swell on the faces so it is not a flat plane
    swell = 6.0 * math.sin(i * 0.07) * math.sin(i * 0.031 + 1.0)
    for j in range(M):
        x, y = section(j)
        y2 = y + (swell if y > 0 else -swell) * (1 - (x / HALF_W) ** 2)
        verts.append(p + side * x + up * y2)
for i in range(N):
    for j in range(M):
        a = i * M + j
        b = i * M + (j + 1) % M
        c = ((i + 1) % N) * M + (j + 1) % M
        d = ((i + 1) % N) * M + j
        faces.append((a, b, c, d))
me = bpy.data.meshes.new("band")
me.from_pydata([tuple(v) for v in verts], [], faces)
me.update()
ob = bpy.data.objects.new("band", me)
sc.collection.objects.link(ob)
for poly in me.polygons:
    poly.use_smooth = True

# material: meadow on the faces, rock on the rim (by |x| of the section via a UV)
uv = me.uv_layers.new(name="uv")
for poly in me.polygons:
    for li in poly.loop_indices:
        vi = me.loops[li].vertex_index
        j = vi % M
        x, y = section(j)
        uv.data[li].uv = (abs(x) / HALF_W, 0.0)
m = bpy.data.materials.new("land")
m.use_nodes = True
nt = m.node_tree
b = nt.nodes["Principled BSDF"]
b.inputs["Roughness"].default_value = 0.9
tex = nt.nodes.new("ShaderNodeUVMap")
sep = nt.nodes.new("ShaderNodeSeparateXYZ")
nt.links.new(tex.outputs[0], sep.inputs[0])
ramp = nt.nodes.new("ShaderNodeValToRGB")
ramp.color_ramp.elements[0].position = 0.86
ramp.color_ramp.elements[0].color = (0.10, 0.40, 0.07, 1)
ramp.color_ramp.elements[1].position = 0.93
ramp.color_ramp.elements[1].color = (0.35, 0.28, 0.2, 1)
nt.links.new(sep.outputs[0], ramp.inputs[0])
nt.links.new(ramp.outputs[0], b.inputs["Base Color"])
me.materials.append(m)

# blob trees on both faces at regular stations
leaf = bpy.data.materials.new("leaf")
leaf.use_nodes = True
leaf.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.05, 0.22, 0.04, 1)
for i in range(0, N, 9):
    p, t, up, side = frames[i]
    for face in (1, -1):
        if rng.random() < 0.55:
            continue
        x = rng.uniform(-0.75, 0.75) * HALF_W
        base = p + side * x + up * face * (HALF_T + 0.5)
        for k in range(3):
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=rng.uniform(3, 5),
                                                  location=base + up * face * (7 + 3 * k) + side * rng.uniform(-2, 2))
            o = bpy.context.object
            o.data.materials.append(leaf)

# clouds: blob clusters in the free space
cl = bpy.data.materials.new("cloud")
cl.use_nodes = True
cb = cl.node_tree.nodes["Principled BSDF"]
cb.inputs["Base Color"].default_value = (0.95, 0.95, 1.0, 1)
cb.inputs["Roughness"].default_value = 1.0
for k in range(70):
    c = Vector((rng.uniform(-3.4, 3.4), rng.uniform(-3.4, 3.4), rng.uniform(-2.2, 2.2))) * S
    if min((c - p).length for p in pts[::15]) < 70:
        continue
    for q in range(6):
        bpy.ops.mesh.primitive_uv_sphere_add(radius=rng.uniform(15, 30),
                                             location=c + Vector((rng.uniform(-30, 30), rng.uniform(-30, 30), rng.uniform(-8, 8))))
        bpy.ops.object.shade_smooth()
        bpy.context.object.data.materials.append(cl)

# sky
w = bpy.data.worlds.new("sky")
sc.world = w
w.use_nodes = True
wn = w.node_tree
bg = wn.nodes["Background"]
tc = wn.nodes.new("ShaderNodeTexCoord")
sx = wn.nodes.new("ShaderNodeSeparateXYZ")
wn.links.new(tc.outputs["Generated"], sx.inputs[0])
mr = wn.nodes.new("ShaderNodeMapRange")
mr.inputs[1].default_value = -1.0
mr.inputs[2].default_value = 1.0
wn.links.new(sx.outputs[2], mr.inputs[0])
rr = wn.nodes.new("ShaderNodeValToRGB")
rr.color_ramp.elements[0].position = 0.3
rr.color_ramp.elements[0].color = (0.45, 0.7, 0.95, 1)
rr.color_ramp.elements[1].position = 0.9
rr.color_ramp.elements[1].color = (0.03, 0.18, 0.75, 1)
wn.links.new(mr.outputs[0], rr.inputs[0])
wn.links.new(rr.outputs[0], bg.inputs[0])
bpy.ops.object.light_add(type="SUN", rotation=(math.radians(40), math.radians(10), math.radians(30)))
bpy.context.object.data.energy = 4.0

sc.render.engine = "CYCLES"
sc.cycles.samples = 16
sc.cycles.use_denoising = True
try:
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "METAL"
    prefs.get_devices()
    for dv in prefs.devices:
        dv.use = True
    sc.cycles.device = "GPU"
except Exception as e:
    print("GPU setup failed:", e)
sc.view_settings.view_transform = "AgX"
sc.render.resolution_x, sc.render.resolution_y = W, H

bpy.ops.object.camera_add()
cam = bpy.context.object
sc.camera = cam


def shoot(name):
    sc.render.filepath = os.path.join(out, name)
    bpy.ops.render.render(write_still=True)


# overview
cam.data.type = "PERSP"
cam.data.lens = 24
cam.data.clip_end = 20000
cam.location = Vector((0, -9, 6)) * S
cam.rotation_mode = "QUATERNION"
cam.rotation_quaternion = (Vector((0, 0, 0)) - cam.location).to_track_quat("-Z", "Z")
shoot("overview.png")

cam.data.type = "PANO"
cam.data.panorama_type = "FISHEYE_EQUISOLID"
cam.data.fisheye_fov = math.radians(FISH)
cam.data.sensor_fit = "HORIZONTAL"
cam.data.sensor_width = 36.0
cam.data.fisheye_lens = 18.0 / (2.0 * math.sin(math.radians(FISH) / 4.0)) * (math.hypot(W, H) / W)
for k, s in enumerate([0.02, 0.09, 0.17, 0.25, 0.33, 0.41]):
    i = int(s * N)
    p, t, up, side = frames[i]
    pos = p + up * (HALF_T + 5.0)
    look = (t + up * 0.12).normalized()
    m3 = Matrix((look.cross(up).normalized(), up, -look)).transposed()
    cam.location = pos
    cam.rotation_quaternion = m3.to_quaternion()
    shoot("cam_%d.png" % k)
print("done", out)
