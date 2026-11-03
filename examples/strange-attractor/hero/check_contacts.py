"""Clipping check over a whole idle loop: a skinned glb (body, shells, fur cards) against rocks and the ground, both
placed with the scene's node transforms. Every sampled frame, every vertex of every mesh.

    Blender -b --factory-startup --python examples/strange-attractor/hero/check_contacts.py -- <spec.json> <report.json>

spec: {"subject": {"asset": path, "position": [x,y,z], "rotation": [0,yaw,0], "scale": [s,s,s]},
       "rocks": [{"asset":..., "position":..., "rotation":..., "scale":...}, ...], "ground_y": 1.76, "step": 3,
       "search": {"rock": 0, "offsets": [[dx, dz], ..], "sy": [..]}}   # optional: rock offsets (scene x/z), y-scales
Positions are the scene's (glTF, y up). Reports, per frame, the count and the deepest penetration into each rock and
below the ground, and (with "search") the best rock offset/scale: no penetration, the seat touching (the lowest
body point over the rock's footprint within 3 cm of its top).
"""
import json
import math
import sys

import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector
from mathutils.bvhtree import BVHTree

spec_path, report_path = sys.argv[sys.argv.index("--") + 1:][:2]
spec = json.load(open(spec_path))


def node_matrix(n):
    """A glTF scene node's transform as a Blender (z-up) world matrix."""
    x, y, z = n["position"]
    yaw = math.radians(n.get("rotation", [0, 0, 0])[1])
    sx, sy, sz = n.get("scale", [1, 1, 1])
    return Matrix.Translation((x, -z, y)) @ Matrix.Rotation(yaw, 4, "Z") @ Matrix.Diagonal((sx, sz, sy, 1.0))


def load(asset, M):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=asset)
    new = [o for o in bpy.data.objects if o not in before]
    root = bpy.data.objects.new("root", None)
    bpy.context.scene.collection.objects.link(root)
    for o in new:
        if o.parent is None:
            o.parent = root
    root.matrix_world = M
    return new


bpy.ops.wm.read_factory_settings(use_empty=True)
subj = load(spec["subject"]["asset"], node_matrix(spec["subject"]))
rocks = []
for r in spec["rocks"]:
    objs = load(r["asset"], node_matrix(r))
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    vs, fs = [], []
    for o in objs:
        if o.type != "MESH":
            continue
        ev = o.evaluated_get(dg)
        m = ev.to_mesh()
        off = len(vs)
        vs += [ev.matrix_world @ v.co for v in m.vertices]
        fs += [[off + i for i in p.vertices] for p in m.polygons]
        ev.to_mesh_clear()
    rocks.append(dict(spec=r, verts=np.array([v[:] for v in vs]), faces=fs))
arm = next(o for o in subj if o.type == "ARMATURE")
act = arm.animation_data.action
f0, f1 = (int(x) for x in act.frame_range)
gy = spec.get("ground_y")


def rock_bvh(rk, dx=0.0, dz=0.0, sy=1.0):
    V = rk["verts"].copy()
    base = V[:, 2].min()
    V[:, 2] = base + (V[:, 2] - base) * sy
    V[:, 0] += dx
    V[:, 1] -= dz                 # scene z -> Blender -y
    return BVHTree.FromPolygons([Vector(v) for v in V], rk["faces"]), V


def inside(bvh, pts):
    """Depth inside a closed mesh by the nearest face's normal (positive = inside)."""
    d = np.zeros(len(pts))
    for i, p in enumerate(pts):
        hit = bvh.find_nearest(Vector(p), 0.5)
        if hit[0] is not None:
            v = Vector(p) - hit[0]
            if v.dot(hit[1]) < 0:
                d[i] = v.length
    return d


frames = list(range(f0, f1 + 1, spec.get("step", 3)))
samples = []
for fr in frames:
    bpy.context.scene.frame_set(fr)
    dg = bpy.context.evaluated_depsgraph_get()
    pts = {}
    for o in subj:
        if o.type != "MESH" or o.hide_render or any(c.name.startswith("glTF_not_exported") for c in o.users_collection):
            continue
        ev = o.evaluated_get(dg)
        m = ev.to_mesh()
        a = np.empty(len(m.vertices) * 3, np.float32)
        m.vertices.foreach_get("co", a)
        a = a.reshape(-1, 3)
        mw = np.array(ev.matrix_world)
        pts[o.name] = a @ mw[:3, :3].T + mw[:3, 3]
        ev.to_mesh_clear()
    samples.append((fr, pts))

report = {"frames": len(frames), "range": [f0, f1], "per_rock": [], "ground": None}
for ri, rk in enumerate(rocks):
    bvh, V = rock_bvh(rk)
    lo, hi = V.min(0) - 0.05, V.max(0) + 0.05
    worst = {}
    for fr, pts in samples:
        for name, P in pts.items():
            near = np.all((P >= lo) & (P <= hi), axis=1)
            if not near.any():
                continue
            d = inside(bvh, P[near])
            n = int((d > 0.003).sum())
            if n:
                w = worst.setdefault(name, dict(frames=0, max_depth=0.0, max_count=0, z_above_ground=[9, -9]))
                w["frames"] += 1
                w["max_depth"] = max(w["max_depth"], float(d.max()))
                w["max_count"] = max(w["max_count"], n)
                zz = P[near][d > 0.003][:, 2] - (gy or 0.0)
                w["z_above_ground"] = [min(w["z_above_ground"][0], float(zz.min())), max(w["z_above_ground"][1], float(zz.max()))]
    report["per_rock"].append(dict(asset=rk["spec"]["asset"].split("/")[-1], penetration=worst))
if gy is not None:
    g = {}
    for fr, pts in samples:
        for name, P in pts.items():
            below = gy - P[:, 2]
            n = int((below > 0.01).sum())
            if n:
                w = g.setdefault(name, dict(frames=0, max_depth=0.0, max_count=0))
                w["frames"] += 1
                w["max_depth"] = max(w["max_depth"], float(below.max()))
                w["max_count"] = max(w["max_count"], n)
    report["ground"] = g
s = spec.get("search")
if s:
    rk = rocks[s.get("rock", 0)]
    best = None
    offs = s.get("offsets") or [(dx, dz) for dx in s["dx"] for dz in s["dz"]]
    for sy in s["sy"]:
        for dx, dz in offs:
            if True:
                bvh, V = rock_bvh(rk, dx, dz, sy)
                lo, hi = V.min(0) - 0.02, V.max(0) + 0.02
                pen, gap = 0, 9.0
                top = V[:, 2].max()
                for fr, pts in samples:
                    for name, P in pts.items():
                        near = np.all((P >= lo) & (P <= hi), axis=1)
                        if near.any():
                            d = inside(bvh, P[near])
                            pen += int((d > 0.003).sum())
                    body = np.concatenate(list(pts.values()))
                    over = np.all((body[:, :2] >= V.min(0)[:2]) & (body[:, :2] <= V.max(0)[:2]), axis=1)
                    if over.any():
                        gap = min(gap, float(body[over, 2].min() - top))
                cand = dict(dx=dx, dz=dz, sy=sy, penetrating_vertex_frames=pen, seat_gap=gap)
                print("TRY", cand)
                if pen == 0 and (best is None or abs(gap) < abs(best["seat_gap"])):
                    best = cand
    report["search_best"] = best
json.dump(report, open(report_path, "w"), indent=1)
print("CONTACTS", json.dumps(report)[:2000])
