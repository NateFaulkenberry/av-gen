"""Neon Dreams it12 (owner msg 170): which standing figures have a rail to lean on.

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_rail_probe.py [-- --cache DIR]

For every spot of art/population.json outside the band: rays forward from the figure at 0.6-1.5 m (and 20 degrees to
either side) against the town (the layout's instances at rest, the same triangle cache as nd_camera_plan.py). A rail
is something 0.15-0.85 m ahead whose top is 0.8-1.2 m over the figure's floor, with open air above it (a wall is
not a rail), and floor still under the figure 0.36 m short of it. Writes art/rails.json:
    {"<spot id>": {"dist": metres ahead, "top": metres over its floor, "yaw": degrees its facing should turn to meet it}}
nd_variety.py gives those figures ND_Lean, moved so the rail is 0.36 m ahead and sized so their forearms meet its top.
CPU only; no GPU lock; about 20 s with the cache.
"""
import bpy, sys, os, json, math, time, hashlib
import numpy as np
from mathutils import Vector, Quaternion
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
ART = os.path.join(HERE, "..", "art")
CACHE = arg("--cache", os.path.join(HERE, "..", "scene", "camplan_cache"))   # scene/ is gitignored
KIT = os.path.join(HERE, "..", "meshes")
SKIP = {"anomaly", "keep", "keep_deck", "keep_isle", "highwater", "highwater_isle"}
T0 = time.time()
LAY = json.load(open(os.path.join(ART, "layout.json")))
POP = json.load(open(os.path.join(ART, "population.json")))
insts = [i for i in LAY["instances"] if i["zone"] not in SKIP and not i["asset"].startswith(("far/", "vehicles/"))]
key = hashlib.sha1(json.dumps([(i["asset"], i["pos"], i["quat"], i["scale"]) for i in insts]).encode()).hexdigest()[:16]
os.makedirs(CACHE, exist_ok=True)
cf = os.path.join(CACHE, key + ".npz")


def E2P(e):            # engine (x, y up, z) -> Blender plan (x, -z, y)
    return Vector((e[0], -e[2], e[1]))


if os.path.exists(cf):
    d = np.load(cf)
    V, T = d["v"], d["t"]
else:   # (the same build as nd_camera_plan.py)
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

UP = Vector((0, 0, 1))
out = {}
for sp in POP["spots"]:
    if sp.get("district") == "band":
        continue
    yaw0 = sp.get("facing0_deg", sp["facing_deg"])
    feet = E2P(sp.get("pos0", sp["pos"]))
    best = None
    for dyaw in (0.0, -20.0, 20.0):
        a = math.radians(yaw0 + dyaw)
        f = Vector((math.sin(a), -math.cos(a), 0.0))        # engine forward (sin, 0, cos) in plan axes
        hits = {}
        for h in (0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.35, 1.5, 1.7):
            hit = BVH.ray_cast(feet + UP * h, f, 1.3)
            hits[h] = hit[3] if hit[0] is not None else None
        near = [(h, d) for h, d in hits.items() if d is not None and 0.15 <= d <= 0.85 and h <= 1.2]
        if not near:
            continue
        top = max(h for h, _ in near)
        dist = min(d for h, d in near if h >= top - 0.25)
        # a wall, a post or a prop is not a rail: open air over it
        if top < 0.8 or any(hits[h] is not None and hits[h] < dist + 0.5 for h in (1.35, 1.5, 1.7)):
            continue
        # the rail's true top, to 2 cm
        lo, hi = top, top + 0.1
        for _ in range(4):
            mid = 0.5 * (lo + hi)
            hit = BVH.ray_cast(feet + UP * mid, f, dist + 0.3)
            if hit[0] is not None:
                lo = mid
            else:
                hi = mid
        top = lo
        # floor under the figure where it will stand (0.36 m short of the rail)
        stand = feet + f * (dist - 0.36)
        g = BVH.ray_cast(stand + UP * 0.6, -UP, 1.0)
        if g[0] is None or abs(g[3] - 0.6) > 0.08:
            continue
        cand = {"dist": round(dist, 3), "top": round(top, 3), "yaw": dyaw}
        if best is None or abs(dyaw) < abs(best["yaw"]):
            best = cand
    if best and 0.82 <= best["top"] <= 1.22:
        out[sp["id"]] = best
json.dump({"_note": "Written by blender/nd_rail_probe.py (it12, owner msg 170): spots with a rail ahead (metres ahead, its top "
                    "over the spot's floor, the turn of its facing that meets it).", "rails": out},
          open(os.path.join(ART, "rails.json"), "w"), indent=1)
import collections
print("rails:", len(out), "of", len(POP["spots"]), collections.Counter(sp["activity"] for sp in POP["spots"] if sp["id"] in out))
print("tops:", sorted(v["top"] for v in out.values())[::max(1, len(out) // 12)])
print("done %.1f s" % (time.time() - T0))
