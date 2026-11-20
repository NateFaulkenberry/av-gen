"""Neon Dreams iteration 10 (owner msgs 121, 123): are the jet-pack flights clear of the town?

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_flight_check.py -- <in.json> <out.json>

build.py --check-flights writes <in.json> (every flight's take-off and landing point, its lift height, and its path
sampled every 0.1 s in the engine frame, before any formation bob) and runs this. CPU only, no GPU lock.

For each flight:
  * OPEN SKY (msg 123): rays straight up on a 0.15 m grid over a 0.6 m disc, from 0.3 m over the take-off and landing floor, to the lift height plus 3 m. Any hit -- a pergola, an eave, an awning, a gangway, a lantern
    string, a cable -- fails the end.
  * CLEARANCE (msgs 121, 123): every path sample more than 1.2 m from both ends (where the feet are on or near the deck)
    is tested at the body's hips, chest and head (node + 0.5 / 1.0 / 1.5 m): the nearest surface must be at least
    0.6 m away.
  * SEARCH: for an `open_near` request, the nearest floor point within `radius` that passes the open-sky test.
Writes <out.json> {flights: [{id, takeoff_open, landing_open, blocked_up: [...], min_clearance_m, worst_t, ok}], open: {...}}.
"""
import bpy, sys, os, json, math, time
from mathutils import Vector, Quaternion, Matrix
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
LAY = json.load(open(os.path.join(ROOT, "art", "layout.json")))
KIT = os.path.join(ROOT, "meshes")
argv = sys.argv[sys.argv.index("--") + 1:]
IN, OUT = argv[0], argv[1]
REQ = json.load(open(IN))
SKIP_ZONES = {"keep", "keep_deck", "keep_isle", "highwater", "highwater_isle", "anomaly"}
bpy.ops.wm.read_factory_settings(use_empty=True)
T0 = time.time()


def P(e):
    """engine (x, y up, z) -> Blender (x, -z, y)"""
    return Vector((e[0], -e[2], e[1]))


TEMPL = {}


def template(asset):
    if asset in TEMPL:
        return TEMPL[asset]
    before = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.gltf(filepath=os.path.join(KIT, asset))
    except Exception as e:  # noqa: BLE001 (a missing gitignored isle is skipped)
        print("skip", asset, e)
        TEMPL[asset] = None
        return None
    new = [o for o in bpy.data.objects if o not in before]
    verts, polys = [], []
    dg = bpy.context.evaluated_depsgraph_get()
    for o in new:
        if o.type != 'MESH':
            continue
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        mw = o.matrix_world
        base = len(verts)
        verts += [tuple(mw @ v.co) for v in me.vertices]
        polys += [tuple(base + i for i in p.vertices) for p in me.polygons]
        ev.to_mesh_clear()
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    TEMPL[asset] = (verts, polys) if polys else None
    return TEMPL[asset]


def inst_matrix(i):
    q = i["quat"]
    rot = Quaternion((q[3], q[0], -q[2], q[1])).to_matrix().to_4x4()
    s = i["scale"]
    return Matrix.Translation((i["pos"][0], -i["pos"][2], i["pos"][1])) @ rot @ Matrix.Diagonal((s[0], s[2], s[1], 1.0))


V, PL = [], []
for i in LAY["instances"]:
    if i["zone"] in SKIP_ZONES or i["asset"].startswith("far/") or i["asset"].startswith("vehicles/"):
        continue
    T = template(i["asset"])
    if T is None:
        continue
    M = inst_matrix(i)
    base = len(V)
    V += [tuple(M @ Vector(v)) for v in T[0]]
    PL += [tuple(base + j for j in p) for p in T[1]]
BVH = BVHTree.FromPolygons(V, PL, all_triangles=False, epsilon=0.0)
print("world: %d polygons, %.1f s" % (len(PL), time.time() - T0))
UP = Vector((0, 0, 1))
# a 0.15 m grid over a 0.6 m disc (49 rays): slatted pergolas and lantern strings have gaps a sparse ring slips through
RING = [(0.15 * i, 0.15 * j) for i in range(-4, 5) for j in range(-4, 5) if math.hypot(i, j) * 0.15 <= 0.6]


def blocked_up(e, height):
    hits = []
    o = P(e)
    for dx, dy in RING:
        loc, _, _, d = BVH.ray_cast(o + Vector((dx, dy, 0.3)), UP, height + 3.0)
        if loc is not None:
            hits.append([round(loc.x, 2), round(loc.z, 2), round(-loc.y, 2), round(d, 2)])
    return hits


def floor_at(x, z, y, reach=2.0):
    loc, nrm, _, _ = BVH.ray_cast(Vector((x, -z, y + reach)), Vector((0, 0, -1)), 2 * reach)
    return loc.z if loc is not None and nrm.z > 0.85 else None


out = {"flights": [], "open": {}}
for f in REQ.get("flights", []):
    a, b = f["takeoff"], f["landing"]
    ua, ub = blocked_up(a, f["lift_a"]), blocked_up(b, f["lift_b"])
    worst, worst_t, worst_at = 1e9, None, None
    viol = []
    for t, p in f["samples"]:
        if math.dist(p, a) < 1.2 or math.dist(p, b) < 1.2:
            continue
        for h in (0.5, 1.0, 1.5):
            loc, _, _, d = BVH.find_nearest(P([p[0], p[1] + h, p[2]]), 5.0)
            if loc is not None and d < 0.6:
                viol.append([t, [p[0], p[1] + h, p[2]], [round(loc.x, 3), round(loc.z, 3), round(-loc.y, 3)], round(d, 3)])
            if loc is not None and d < worst:
                worst, worst_t, worst_at = d, t, [round(loc.x, 2), round(loc.z, 2), round(-loc.y, 2)]
    rec = {"id": f["id"], "takeoff_open": not ua, "landing_open": not ub, "blocked_up_takeoff": ua[:3],
           "blocked_up_landing": ub[:3], "min_clearance_m": round(worst, 3) if worst < 1e9 else None,
           "worst_t": worst_t, "worst_at": worst_at, "violations": viol}
    rec["ok"] = rec["takeoff_open"] and rec["landing_open"] and (worst >= 0.6)
    out["flights"].append(rec)
    print("flight %-24s takeoff %s landing %s clearance %s at %s s %s -> %s" % (
        f["id"], "open" if not ua else "BLOCKED", "open" if not ub else "BLOCKED", rec["min_clearance_m"], worst_t, worst_at,
        "ok" if rec["ok"] else "FAIL"))
for q in REQ.get("open_near", []):
    c, r, h = q["near"], q.get("radius", 8.0), q.get("lift", 10.0)
    best = None
    steps = int(r / 0.5)
    for i in range(-steps, steps + 1):
        for j in range(-steps, steps + 1):
            x, z = c[0] + 0.5 * i, c[2] + 0.5 * j
            d = math.hypot(x - c[0], z - c[2])
            if d > r or (best is not None and d >= best[0]):
                continue
            y = floor_at(x, z, c[1])
            if y is None or abs(y - c[1]) > 1.0:
                continue
            if blocked_up([x, y, z], h):
                continue
            # room to stand and to come down: nothing (a wall, a lamp post) within 0.7 m from 1 m (clear of the floor) to 3.5 m up
            if any(BVH.find_nearest(P([x, y + h, z]), 0.7)[0] is not None for h in (1.0, 1.5, 2.5, 3.5)):
                continue
            best = (d, [round(x, 3), round(y, 3), round(z, 3)])
    out["open"][q["id"]] = best[1] if best else None
    print("open near %s: %s" % (q["id"], best))
json.dump(out, open(OUT, "w"), indent=1)
print("done %.1f s" % (time.time() - T0))
