"""Cursed Slammers POC 3: the face's cheap fixes, from the prepared scan (assets/cursed-slammers/cs-face.glb). Inputs and
outputs are gitignored (derived from the CC-BY scan); this script is ours.

    Blender -b --factory-startup -P tools/cursed_slammers/poc3_assets.py -- \
        --src assets/cursed-slammers/cs-face.glb --out-dir assets/cursed-slammers [--gain 2.2] [--muzzle 1.4]

Writes (GLB frame: +Y up, face toward +Z, width 1.0, key plane at z = 0):
- cs-face-p3.glb   the scan with B1's relative relief x gain, PLUS extra relief on the muzzle only: the depth gain
                   rises to gain + muzzle inside a soft ellipse round the muzzle (nose and lips), so the muzzle reaches
                   toward the viewer and nothing else moves. The scan's surface, UVs and textures are untouched
                   otherwise.
- cs-back.glb      a simple back volume: a dark ellipsoid behind the mask, so the mask's cut edges stop showing at
                   wide angles.
- cs-eye.glb       one eyeball (a glossy sphere with a dark iris cap facing +Z); placed twice by the harness.
- poc3_assets.json the key plane, the muzzle ellipse, and the two eye centres found in the carved sockets.

The eye centres: their frontal (x, y) is read off a frontal render (--eyes; the automatic "deepest point of the
socket" search picked the mask's receding sides instead), and their depth is the carved eye's own front surface
there; the eyeball is centred --eye-back radii behind it, so it sits in the socket where the carved eye was.
"""
import argparse
import json
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--src", required=True)
ap.add_argument("--out-dir", required=True)
ap.add_argument("--gain", type=float, default=2.2)
ap.add_argument("--muzzle", type=float, default=0.6, help="extra depth gain at the muzzle's centre")
ap.add_argument("--eye-r", type=float, default=0.036)
ap.add_argument("--eye-back", type=float, default=1.0, help="eye centre behind the carved surface, in radii")
ap.add_argument("--eyes", default="-0.147,0.205;0.171,0.181",
                help="the carved eyes' centres seen from the front (glTF x, y), read off a frontal render")
args = ap.parse_args(argv)
OUT = os.path.abspath(os.path.expanduser(args.out_dir))


def clear():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def load():
    clear()
    bpy.ops.import_scene.gltf(filepath=os.path.abspath(args.src))
    ob = [o for o in bpy.context.scene.objects if o.type == "MESH"][0]
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return ob


def export(ob, name):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    path = os.path.join(OUT, name)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_normals=True, export_texcoords=True,
                              export_materials="EXPORT", export_image_format="AUTO")
    print("[poc3] wrote", path, flush=True)


def material(o, name, rgb, rough, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    o.data.materials.append(m)
    return len(o.data.materials) - 1


# Blender frame: face toward -Y; depth toward the viewer d = -y; up = +z.
ob = load()
V = ob.data.vertices
ds = sorted(-v.co.y for v in V)
key = ds[len(ds) // 2]
# the muzzle: the most forward region of the lower face. Its centre is the depth-weighted centroid of the frontmost 3 %
front = sorted(V, key=lambda v: v.co.y)[: max(50, len(V) // 33)]
mx = sum(v.co.x for v in front) / len(front)
mz = sum(v.co.z for v in front) / len(front)
MRX, MRZ = 0.30, 0.26                          # the muzzle ellipse's radii (face widths)

# eyes: the frontmost carved surface within a small disc round each given frontal centre
eyes = []
for pair in args.eyes.split(";"):
    ex, ey = (float(c) for c in pair.split(","))
    near = [v for v in V if (v.co.x - ex) ** 2 + (v.co.z - ey) ** 2 < 0.02 ** 2]
    frontmost = min(near, key=lambda v: v.co.y)
    eyes.append(Vector((ex, frontmost.co.y, ey)))
print("[poc3] key", round(key, 4), "muzzle centre", round(mx, 3), round(mz, 3),
      "eyes", [tuple(round(c, 3) for c in e) for e in eyes], flush=True)


def gainf(x, z):
    r2 = ((x - mx) / MRX) ** 2 + ((z - mz) / MRZ) ** 2
    w = math.exp(-2.0 * r2)
    return args.gain + args.muzzle * w


for v in V:
    d = -v.co.y
    v.co.y = -((d - key) * gainf(v.co.x, v.co.z))
if ob.data.has_custom_normals:
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
ob.data.update()
export(ob, "cs-face-p3.glb")
# where the eyes landed after the remap (eye centre: one radius behind the carved surface)
eyes_out = []
for e in eyes:
    d = -e.y
    d2 = (d - key) * gainf(e.x, e.z)
    centre = (e.x, d2 - args.eye_r * args.eye_back, e.z)           # Blender (x, depth, up)
    eyes_out.append(centre)

# back volume: a dark ellipsoid behind the mask (the cranium the scan does not have)
clear()
bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=1.0, location=(0.0, 0.0, 0.0))
back = bpy.context.active_object
back.scale = (0.44, 0.60, 0.44)                # x half-width, y depth, z half-height
back.location = (0.0, 0.42, 0.02)              # behind the key plane (+y is back)
bpy.ops.object.transform_apply(location=True, scale=True)
bpy.ops.object.shade_smooth()
material(back, "back", (0.005, 0.0045, 0.004), 0.9)
export(back, "cs-back.glb")

# eyeball: a glossy sphere, a dark iris cap toward -y (glTF +Z)
clear()
bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=args.eye_r, location=(0, 0, 0))
eye = bpy.context.active_object
bpy.ops.object.shade_smooth()
sclera = material(eye, "eye_sclera", (0.06, 0.045, 0.035), 0.06)
iris = material(eye, "eye_iris", (0.012, 0.007, 0.004), 0.04)
for p in eye.data.polygons:
    c = p.center
    p.material_index = iris if c.y < -args.eye_r * 0.55 else sclera
export(eye, "cs-eye.glb")

to_gltf = lambda b: [b[0], b[2], b[1]]          # (x, depth toward viewer, up) -> glTF (x, up, z toward viewer)
info = {"keyPlane": key, "gain": args.gain, "muzzleExtra": args.muzzle, "muzzleCentre": [mx, mz],
        "muzzleRadii": [MRX, MRZ], "eyeRadius": args.eye_r,
        "eyesGltf": [to_gltf(c) for c in eyes_out]}
with open(os.path.join(OUT, "poc3_assets.json"), "w") as f:
    json.dump(info, f, indent=1)
print("[poc3] eyes (glTF)", info["eyesGltf"], flush=True)
