"""Cursed Slammers POC 2, Test B: depth variants of the prepared face (assets/cursed-slammers/cs-face.glb, from
prep_face.py). The input and outputs are gitignored (derived from the CC-BY scan); this script is ours.

    Blender -b --factory-startup -P tools/cursed_slammers/depth_variants.py -- \
        --src assets/cursed-slammers/cs-face.glb --out-dir assets/cursed-slammers [--gain 2.2] [--layers 5]

Writes, in the GLB frame (+Y up, face toward +Z, width 1.0), each with its own key plane at depth 0:
- cs-face-b0.glb  the scan's own relief, recentred so the mid-depth of the face is the key plane (z = 0);
- cs-face-b1.glb  RELATIVE relief exaggerated: depth about the key plane x gain (the muzzle comes forward, the sockets
                  go back; the silhouette's width and height do not change, the head does not move toward the camera);
- cs-face-b2.glb  the same range TERRACED into --layers plateaus with soft risers: the stacked-cut-out depth of a
                  classic 3D lenticular card (muzzle plane, brow plane, cheek plane, socket plane, rim);
- depth_variants.json: depth statistics (percentiles, which features sit where).

Depth is measured along the face's view axis; the key plane is the depth median of the front-facing half of the
mask, so the face straddles it (part in front of the print, part behind) as a lenticular card's key plane does.
"""
import argparse
import json
import math
import os
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--src", required=True)
ap.add_argument("--out-dir", required=True)
ap.add_argument("--gain", type=float, default=2.2)
ap.add_argument("--layers", type=int, default=5)
ap.add_argument("--riser", type=float, default=0.18, help="riser width as a fraction of one layer (0..0.5)")
args = ap.parse_args(argv)
OUT = os.path.abspath(os.path.expanduser(args.out_dir))
os.makedirs(OUT, exist_ok=True)


def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def load():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    bpy.ops.import_scene.gltf(filepath=os.path.abspath(args.src))
    ob = [o for o in bpy.context.scene.objects if o.type == "MESH"][0]
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return ob


def export(ob, name):
    me = ob.data
    if me.has_custom_normals:
        bpy.context.view_layer.objects.active = ob
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    me.update()
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    path = os.path.join(OUT, name)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_normals=True, export_texcoords=True,
                              export_materials="EXPORT", export_image_format="AUTO")
    print("[depth_variants] wrote", path, flush=True)
    return path


# Blender frame: the face looks down -Y, so depth toward the viewer is d = -y.
ob = load()
ds = sorted(-v.co.y for v in ob.data.vertices)
n = len(ds)
pct = {p: ds[min(n - 1, int(p / 100 * n))] for p in (1, 5, 25, 50, 75, 95, 99)}
key = pct[50]
lo, hi = pct[1], pct[99]
stats = {"vertices": n, "depthPercentiles": pct, "keyPlane": key, "range1to99": hi - lo,
         "gain": args.gain, "layers": args.layers, "riser": args.riser}
print("[depth_variants] depth percentiles", {k: round(v, 4) for k, v in pct.items()}, flush=True)


def remap(fn, name):
    o = load()
    for v in o.data.vertices:
        d = -v.co.y
        v.co.y = -fn(d)
    return export(o, name)


remap(lambda d: d - key, "cs-face-b0.glb")
remap(lambda d: (d - key) * args.gain, "cs-face-b1.glb")


def terrace(d):
    # normalised depth over the 1..99 % range, terraced into L plateaus with soft risers, then scaled like b1
    t = (d - lo) / max(1e-6, hi - lo)
    L = args.layers
    x = t * L
    k = math.floor(x)
    f = x - k
    w = args.riser
    step = smoothstep(0.5 - w, 0.5 + w, f)
    tq = (k + step) / L
    # keep a little of the original relief inside each plateau, so a plateau is a shallow surface, not a flat card
    tq = 0.8 * tq + 0.2 * t
    dq = lo + tq * (hi - lo)
    return (dq - key) * args.gain


remap(terrace, "cs-face-b2.glb")
with open(os.path.join(OUT, "depth_variants.json"), "w") as f:
    json.dump(stats, f, indent=1)
