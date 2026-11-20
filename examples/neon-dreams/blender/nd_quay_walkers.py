"""Neon Dreams iteration 10 (owner msg 125): "make more of the aliens who are at ground level on the Quay walk rather
than just standing around". Edits art/population.json in place (deterministic; run once):

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_quay_walkers.py

* Who walks: the ground-level standers of the Quay, the Low Walk and the plaza -- `stand`, `look_lantern`,
  `look_sign` (all but every fourth: those stay as shopfront browsers), and the members of gathering groups of four or
  more (two of each group stay talking). Rail leaners (`look_sea`), fishers, `browse` and `use`, and groups of three
  stay put.
* Where: each joins the nearest walking path on its own level (the art's ray-checked Quay/plaza/Low Walk paths), on a
  lane 0.5 m to one side of it or the other (alternating), or on the path itself when the lane is not clear. A lane is
  clear when there is floor under every 0.6 m of it within 0.3 m of the level, and nothing within 0.35 m at 1.0 m
  and 1.6 m up (rails, lamp posts, planters, stall counters).
* How: pingpong at 0.9-1.4 m/s (both directions), its own phase; a `look_sign` walker stops for 2-3 s at the point of
  the path nearest its old spot, facing where it used to look (the shopfront), then walks on.
Writes `paths[]` entries `id: "qw_<spot>"`, tier A, `stops: [[fraction of the out leg, seconds, yaw]]`, removes the
converted spots (and their group memberships), and records it under `edits` in the file.
"""
import bpy, sys, os, json, math, random
from mathutils import Vector, Quaternion, Matrix
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
POP_PATH = os.path.join(ROOT, "art", "population.json")
POP = json.load(open(POP_PATH))
LAY = json.load(open(os.path.join(ROOT, "art", "layout.json")))
KIT = os.path.join(ROOT, "meshes")
SKIP_ZONES = {"keep", "keep_deck", "keep_isle", "highwater", "highwater_isle", "anomaly"}
bpy.ops.wm.read_factory_settings(use_empty=True)
rng = random.Random(125)
if any(e.get("msg") == 125 for e in POP.get("edits", [])):
    print("already applied"); sys.exit(0)


def P(e):
    return Vector((e[0], -e[2], e[1]))


TEMPL = {}


def template(asset):
    if asset in TEMPL:
        return TEMPL[asset]
    before = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.gltf(filepath=os.path.join(KIT, asset))
    except Exception:  # noqa: BLE001
        TEMPL[asset] = None
        return None
    new = [o for o in bpy.data.objects if o not in before]
    verts, polys = [], []
    dg = bpy.context.evaluated_depsgraph_get()
    for o in new:
        if o.type != 'MESH':
            continue
        ev = o.evaluated_get(dg); me = ev.to_mesh(); mw = o.matrix_world; base = len(verts)
        verts += [tuple(mw @ v.co) for v in me.vertices]
        polys += [tuple(base + i for i in p.vertices) for p in me.polygons]
        ev.to_mesh_clear()
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    TEMPL[asset] = (verts, polys) if polys else None
    return TEMPL[asset]


def inst_matrix(i):
    q = i["quat"]; rot = Quaternion((q[3], q[0], -q[2], q[1])).to_matrix().to_4x4(); s = i["scale"]
    return Matrix.Translation((i["pos"][0], -i["pos"][2], i["pos"][1])) @ rot @ Matrix.Diagonal((s[0], s[2], s[1], 1.0))


V, PL = [], []
for i in LAY["instances"]:
    if i["zone"] in SKIP_ZONES or i["asset"].startswith("far/") or i["asset"].startswith("vehicles/"):
        continue
    T = template(i["asset"])
    if T is None:
        continue
    M = inst_matrix(i); base = len(V)
    V += [tuple(M @ Vector(v)) for v in T[0]]
    PL += [tuple(base + j for j in p) for p in T[1]]
BVH = BVHTree.FromPolygons(V, PL, all_triangles=False, epsilon=0.0)


def lane_clear(pts):
    for a, b in zip(pts[:-1], pts[1:]):
        n = max(1, int(math.dist(a, b) / 0.6))
        for k in range(n + 1):
            u = k / n
            p = [a[i] + (b[i] - a[i]) * u for i in range(3)]
            loc, nrm, _, _ = BVH.ray_cast(P([p[0], p[1] + 0.5, p[2]]), Vector((0, 0, -1)), 1.0)
            if loc is None or nrm.z < 0.85 or abs(loc.z - p[1]) > 0.3:
                return False
            for h in (1.0, 1.6):
                if BVH.find_nearest(P([p[0], p[1] + h, p[2]]), 0.35)[0] is not None:
                    return False
    return True


def offset(pts, d):
    out = []
    for k, p in enumerate(pts):
        a, b = pts[max(0, k - 1)], pts[min(len(pts) - 1, k + 1)]
        t = [b[0] - a[0], b[2] - a[2]]
        ln = math.hypot(*t) or 1.0
        out.append([round(p[0] - t[1] / ln * d, 3), p[1], round(p[2] + t[0] / ln * d, 3)])
    return out


def project(pts, q):
    """fraction along the polyline of its point nearest q, and that distance"""
    cum, best = 0.0, (1e9, 0.0)
    total = sum(math.dist(a, b) for a, b in zip(pts[:-1], pts[1:]))
    for a, b in zip(pts[:-1], pts[1:]):
        L = math.dist(a, b)
        ab = [b[0] - a[0], b[2] - a[2]]
        u = max(0.0, min(1.0, ((q[0] - a[0]) * ab[0] + (q[2] - a[2]) * ab[1]) / max(L * L, 1e-9)))
        x = [a[0] + ab[0] * u, a[2] + ab[1] * u]
        d = math.hypot(q[0] - x[0], q[2] - x[1])
        if d < best[0]:
            best = (d, (cum + u * L) / max(total, 1e-9))
        cum += L
    return best[1], best[0]


DISTRICTS = ("quay", "low_walk", "plaza")
groups = {g["id"]: g for g in POP["groups"]}
paths = [p for p in POP["paths"] if p["district"] in DISTRICTS]
convert = []
signs = 0
for s in POP["spots"]:
    if s["district"] not in DISTRICTS:
        continue
    act = s["activity"]
    if act in ("stand", "look_lantern"):
        convert.append(s)
    elif act == "look_sign":
        signs += 1
        if signs % 4 != 0:
            convert.append(s)
    elif act == "gather" and s.get("group") in groups and len(groups[s["group"]]["members"]) >= 4:
        g = groups[s["group"]]
        if g["members"].index(s["id"]) >= 2:
            convert.append(s)
new_paths, gone, lane_k = [], set(), 0
for s in convert:
    level = [p for p in paths if abs(p["points"][0][1] - s["pos"][1]) < 0.5]
    if not level:
        continue
    best = min(level, key=lambda p: project(p["points"], s["pos"])[1])
    frac, dist = project(best["points"], s["pos"])
    if dist > 12.0:
        continue
    lane_k += 1
    side = 0.5 if lane_k % 2 else -0.5
    pts = None
    for d in (side, -side, 0.0):
        cand = offset(best["points"], d) if d else best["points"]
        if lane_clear(cand):
            pts = cand
            break
    if pts is None:
        continue
    speed = round(rng.uniform(0.9, 1.4), 2)
    length = sum(math.dist(a, b) for a, b in zip(pts[:-1], pts[1:]))
    stops = []
    if s["activity"] == "look_sign":
        stops = [[round(frac, 3), round(rng.uniform(2.0, 3.0), 2), s["facing_deg"]]]
    lap = 2 * (length / speed + 1.2) + sum(st[1] for st in stops)
    new_paths.append({"id": "qw_" + s["id"], "district": s["district"], "points": pts, "speed_mps": speed,
                      "mode": "pingpong", "clip": "Walking", "walkers": 1, "pause_s": 1.2,
                      "phases_s": [round(rng.uniform(0, lap), 2)], "cast": [s["cast"]], "bob": best.get("bob"),
                      "length_m": round(length, 1), "tier": "A", "react": ["sway"], "stops": stops,
                      "note": "msg 125: was %s (%s); on %s, lane %+.1f m" % (s["id"], s["activity"], best["id"],
                                                                         0.0 if pts is best["points"] else side)})
    gone.add(s["id"])
POP["spots"] = [s for s in POP["spots"] if s["id"] not in gone]
for g in POP["groups"]:
    g["members"] = [m for m in g["members"] if m not in gone]
POP["groups"] = [g for g in POP["groups"] if g["members"]]
POP["paths"] += new_paths
POP.setdefault("edits", []).append({"msg": 125, "by": "engine", "script": "blender/nd_quay_walkers.py",
                                    "converted": len(new_paths), "candidates": len(convert)})
json.dump(POP, open(POP_PATH, "w"), indent=1)
print("msg 125: %d of %d standers now walk (%d new paths); %d spots left" % (len(new_paths), len(convert), len(new_paths),
                                                                             len(POP["spots"])))
