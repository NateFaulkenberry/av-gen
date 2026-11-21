"""Neon Dreams, iteration 11 (owner msgs 130, 132, 136): the journey camera judged by what it shows of the aliens.

    Blender -b --factory-startup -P nd_camera_plan.py -- REQUEST.json OUT.json [--layout art/layout.json] [--cache DIR]

REQUEST (written by the art's planner from a built scene: the camera as the engine rides it, every figure as the
scene places it, both in the engine frame):
    {"cams": [[t, [x,y,z] lens, [x,y,z] aim point, fov_deg], ...],       every 0.25 s
     "clear": [[t, [x,y,z]], ...],                                      every 0.1 s (the clearance samples)
     "figs": {"<t>": [[id, [x,y,z] feet, px], ...]}}                     only the figures in frame (px >= 12)
OUT: {"vis": {"<t>": [[id, px], ...]}  (figures not hidden by the town: a ray to the head or the chest is clear),
      "clear": [[t, metres to the nearest surface], ...]}

The town is the layout's instances at rest (the kit GLBs and the gitignored isles; vehicles and far props left out),
cached as triangles keyed by the instance list, so a second run starts in a few seconds. Clouds are not tested.
CPU only; no GPU lock.
"""
import bpy, sys, os, json, math, time, hashlib
import numpy as np
from mathutils import Vector, Quaternion, Matrix
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
REQ, OUT = argv[0], argv[1]
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
LAYP = arg("--layout", os.path.join(HERE, "..", "art", "layout.json"))
CACHE = arg("--cache", os.path.join(HERE, "..", "scene", "camplan_cache"))   # scene/ is gitignored
KIT = os.path.join(os.path.dirname(os.path.abspath(LAYP)), "..", "meshes")
SKIP = {"anomaly", "keep", "keep_deck", "keep_isle", "highwater", "highwater_isle"}
T0 = time.time()
LAY = json.load(open(LAYP))
insts = [i for i in LAY["instances"] if i["zone"] not in SKIP and not i["asset"].startswith(("far/", "vehicles/"))]
key = hashlib.sha1(json.dumps([(i["asset"], i["pos"], i["quat"], i["scale"]) for i in insts]).encode()).hexdigest()[:16]
os.makedirs(CACHE, exist_ok=True)
cf = os.path.join(CACHE, key + ".npz")


def E2P(e):            # engine (x, y up, z) -> Blender plan (x, -z, y)
    return Vector((e[0], -e[2], e[1]))


if os.path.exists(cf):
    d = np.load(cf)
    V, T = d["v"], d["t"]
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    TEMPL = {}

    def template(asset):
        if asset in TEMPL:
            return TEMPL[asset]
        before = set(bpy.data.objects)
        try:
            bpy.ops.import_scene.gltf(filepath=os.path.join(KIT, asset))
        except Exception as e:                       # noqa: BLE001  (a missing gitignored isle is skipped)
            print("skip", asset, e); TEMPL[asset] = None; return None
        new = [o for o in bpy.data.objects if o not in before]
        vs, ts = [], []
        dg = bpy.context.evaluated_depsgraph_get()
        for o in new:
            if o.type != 'MESH':
                continue
            ev = o.evaluated_get(dg); me = ev.to_mesh(); mw = o.matrix_world; base = len(vs)
            vs += [tuple(mw @ v.co) for v in me.vertices]
            for p in me.polygons:
                ids = list(p.vertices)
                for k in range(1, len(ids) - 1):
                    ts.append((base + ids[0], base + ids[k], base + ids[k + 1]))
            ev.to_mesh_clear()
        for o in new:
            bpy.data.objects.remove(o, do_unlink=True)
        TEMPL[asset] = (np.array(vs, dtype=np.float64), np.array(ts, dtype=np.int64)) if ts else None
        return TEMPL[asset]

    Vs, Ts, n = [], [], 0
    for i in insts:
        tp = template(i["asset"])
        if tp is None:
            continue
        q = i["quat"]; rot = np.array(Quaternion((q[3], q[0], -q[2], q[1])).to_matrix())
        s = i["scale"]; S = np.diag([s[0], s[2], s[1]])
        tr = np.array([i["pos"][0], -i["pos"][2], i["pos"][1]])
        Vs.append(tp[0] @ (rot @ S).T + tr); Ts.append(tp[1] + n); n += len(tp[0])
    V, T = np.concatenate(Vs), np.concatenate(Ts)
    np.savez(cf, v=V, t=T)
BVH = BVHTree.FromPolygons([tuple(v) for v in V], [tuple(t) for t in T], all_triangles=True, epsilon=0.0)
print("town: %d tris, %.1f s" % (len(T), time.time() - T0), flush=True)

R = json.load(open(REQ))
cams = {round(c[0], 3): c for c in R["cams"]}
vis = {}
for ts, figs in R["figs"].items():
    c = cams[round(float(ts), 3)]
    lens = E2P(c[1])
    out = []
    for fid, feet, px in figs:
        f = E2P(feet)
        ok = False
        for h in (1.5, 0.9):
            tgt = f + Vector((0, 0, h)); d = tgt - lens; L = d.length
            if L < 0.5:
                ok = True; break
            hit = BVH.ray_cast(lens, d / L, L - 0.15)
            if hit[0] is None:
                ok = True; break
        if ok:
            out.append([fid, px])
    vis[ts] = out
clear = []
for t, p in R["clear"]:
    loc, nrm, idx, dist = BVH.find_nearest(E2P(p), 60.0)
    clear.append([t, round(dist, 2) if loc is not None else 60.0])
json.dump({"vis": vis, "clear": clear}, open(OUT, "w"))
print("done %.1f s" % (time.time() - T0))
