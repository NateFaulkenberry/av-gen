"""Neon Dreams iteration 11 (owner msg 130: "we still haven't fixed this guy floating / standing on nothing"): is every
figure standing on something?

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_ground_check.py -- <in.json> <out.json>

build.py --check-ground writes <in.json>: every figure as the scene places it -- spots (one rest-frame point), walkers
(their position keys over the whole song, resampled every 0.3 m along each leg), flyers at their take-off and landing
decks, riders in their cabin's local frame -- each with the formation it rides (`bob`). CPU only, no GPU lock.

The town is the layout's instances (all but far/ and vehicles/), each polygon tagged with the formation it rides. For
every sample:
  * SUPPORT: rays straight down from five foot points (the root and +-0.12 m to the sides, +-0.1 m ahead/behind, turned
    by the figure's yaw) from 0.25 m over the root. The nearest upward-facing hit under a foot is its floor; the figure
    is supported when some foot's floor is within GAP (5 cm) of the root. No floor within GAP -> FLOAT (the gap is the
    distance to the first floor below, or "nothing" when there is none within 30 m). A floor 8-25 cm over the root ->
    SUNK.
  * RIDES: the supporting polygon's formation must be the figure's own (a figure with no bob on a bobbing isle, or the
    other way round, floats or sinks by up to the formation's amplitude as it bobs).
  * INSIDE: the nearest surface to the shins, hips, chest and head (0.3 / 0.9 / 1.3 / 1.7 m x the figure's height
    factor), when within 0.3 m and the point lies behind it (the face looks away from the point) by more than 4 cm ->
    the body is in a wall or a mesh.
For each spot that fails, a SEARCH: the nearest point within 4 m on a floor within 0.5 m of its level that passes all
three (the spot's move); none -> the spot is dropped. A walker leg that fails is reported with its failing samples.
Writes <out.json> {figures: [{id, kind, ok, reasons, worst: {...}, n_samples, n_fail, fix}], summary}.
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
GAP, SUNK, INSIDE, LOOK_UP = 0.05, 0.08, 0.04, 0.5
bpy.ops.wm.read_factory_settings(use_empty=True)
T0 = time.time()


def P(e):
    """engine (x, y up, z) -> Blender (x, -z, y)"""
    return Vector((e[0], -e[2], e[1]))


def E(v):
    return [round(v.x, 3), round(v.z, 3), round(-v.y, 3)]


TEMPL = {}


def template(path):
    if path in TEMPL:
        return TEMPL[path]
    before = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.gltf(filepath=path)
    except Exception as e:  # noqa: BLE001
        print("skip", path, e)
        TEMPL[path] = None
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
    TEMPL[path] = (verts, polys) if polys else None
    return TEMPL[path]


def inst_matrix(i):
    q = i["quat"]
    rot = Quaternion((q[3], q[0], -q[2], q[1])).to_matrix().to_4x4()
    s = i["scale"]
    return Matrix.Translation((i["pos"][0], -i["pos"][2], i["pos"][1])) @ rot @ Matrix.Diagonal((s[0], s[2], s[1], 1.0))


def world(instances):
    V, PL, OWN = [], [], []
    for i in instances:
        T = template(os.path.join(KIT, i["asset"]))
        if T is None:
            continue
        M = inst_matrix(i)
        base = len(V)
        V += [tuple(M @ Vector(v)) for v in T[0]]
        PL += [tuple(base + j for j in p) for p in T[1]]
        OWN += [(i.get("bob") or "", i["id"])] * len(T[1])
    return BVHTree.FromPolygons(V, PL, all_triangles=False, epsilon=0.0), OWN


TOWN, OWN = world([i for i in LAY["instances"] if not i["asset"].startswith(("far/", "vehicles/"))])
print("world: %d polygons, %.1f s" % (len(OWN), time.time() - T0))
CABINS = {}
DOWN = Vector((0, 0, -1))
FEET = [(0.0, 0.0), (0.12, 0.0), (-0.12, 0.0), (0.0, 0.1), (0.0, -0.1)]


def first_floor(bvh, o, reach):
    """The first upward-facing surface under o (skipping steep faces: a wall a ray grazes)."""
    start, left = o.copy(), reach
    while left > 0:
        loc, nrm, idx, d = bvh.ray_cast(start, DOWN, left)
        if loc is None:
            return None
        if nrm.z > 0.5:
            return loc, idx
        start, left = loc + DOWN * 0.005, left - d - 0.005
    return None


def test(bvh, own, pos, yaw, bob, hf=1.0, check_bob=True):
    """-> (reasons, detail). pos engine frame, yaw degrees (engine: about +y, 0 = facing +z)."""
    root = P(pos)
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))

    def off(dx, dz):   # engine lateral (x) and forward (z) turned by yaw, as a Blender offset
        ex, ez = dx * c + dz * s, -dx * s + dz * c
        return Vector((ex, -ez, 0.0))
    reasons, det = [], {}
    best = None
    for dx, dz in FEET:
        # from knee height: a floor over the root (the feet in it) is found as well as one under
        hit = first_floor(bvh, root + off(dx, dz) + Vector((0, 0, LOOK_UP)), 30.0 + LOOK_UP)
        if hit is None:
            continue
        g = root.z - hit[0].z
        if best is None or abs(g) < abs(best[0]):
            best = (g, hit[1])
    det["gap_m"] = None if best is None else round(best[0], 3)
    if best is None or best[0] > GAP:
        reasons.append("float")
    elif best[0] < -SUNK:
        reasons.append("sunk")
    if best is not None and own is not None and "float" not in reasons:
        sb, sid = own[best[1]]
        det["on"], det["support_bob"] = sid, sb
        if check_bob and sb != (bob or ""):
            reasons.append("rides")
    for h, dx in ((0.3, 0.0), (0.9, 0.0), (1.3, 0.0), (1.6, 0.0), (1.2, 0.18), (1.2, -0.18)):
        q = root + off(dx, 0.0) + Vector((0, 0, h * hf))
        loc, nrm, idx, d = bvh.find_nearest(q, 0.3)
        if loc is None:
            continue
        behind = (q - loc).dot(nrm)
        if behind < -INSIDE:
            reasons.append("inside")
            det["inside_at_m"] = h
            det["inside_by_m"] = round(-behind, 3)
            if own is not None:
                det["inside"] = own[idx][1]
            break
    return reasons, det


def candidates(bvh, x0, y0, z0, radius, step=0.25, tol=0.4):
    """Floor points round (x0, z0) within `tol` of the level y0, nearest first."""
    n = int(radius / step)
    pts = sorted(((math.hypot(i, j) * step, i, j) for i in range(-n, n + 1) for j in range(-n, n + 1)
                  if math.hypot(i, j) * step <= radius))
    for d, i, j in pts:
        x, z = x0 + i * step, z0 + j * step
        hit = first_floor(bvh, P([x, y0 + LOOK_UP, z]), LOOK_UP + 0.6)
        if hit is None or abs(hit[0].z - y0) > tol:
            continue
        yield d, [round(x, 3), round(hit[0].z, 3), round(z, 3)]


def search(bvh, own, pos, yaw, hf=1.0, radius=4.0, room=0.25):
    """The nearest standing point (and the formation it rides)."""
    for d, q in candidates(bvh, pos[0], pos[1], pos[2], radius):
        rs, det = test(bvh, own, q, yaw, None, hf, check_bob=False)
        if rs:
            continue
        # a little room round the body: nothing within `room` at the hips and chest
        if any(bvh.find_nearest(P([q[0], q[1] + h * hf, q[2]]), room)[0] is not None for h in (0.9, 1.3)):
            continue
        return d, q, det.get("support_bob", "")
    return None


def room(bvh, q, hf=1.0, r=0.28):
    """Nothing but floor within r of the shins, hips and chest: a walker passing between two such points clears it."""
    for h in (0.35, 0.9, 1.3):
        loc, nrm, _, _ = bvh.find_nearest(P([q[0], q[1] + h * hf, q[2]]), r)
        if loc is not None and nrm.z < 0.7:
            return False
    return True


def walk_fails(bvh, own, path, hf=1.0, step=0.1):
    """Samples every `step` along the re-laid path that fail the test: [(segment index, point)]."""
    bad = []
    for k, (a, b) in enumerate(zip(path, path[1:])):
        L = math.dist(a, b)
        yaw = math.degrees(math.atan2(b[0] - a[0], b[2] - a[2]))
        n = max(1, int(math.ceil(L / step)))
        for j in range(n + 1):
            q = [a[i] + (b[i] - a[i]) * j / n for i in range(3)]
            if test(bvh, own, q, yaw, None, hf, check_bob=False)[0]:
                bad.append((k, q, yaw))
                break
    return bad


def drape(bvh, own, pts, hf=1.0):
    path, lost, n = drape0(bvh, own, pts, hf)
    for rnd in range(40):   # validate every 0.1 m; where a sample fails, a standing point near it goes into the path
        bad = walk_fails(bvh, own, path, hf)
        if not bad:
            return path, lost, n
        if rnd >= 30:   # no convergence: cut at the first failure that is left
            bad = bad[:1]
            k = bad[0][0]
            left, right = path[:k + 1], path[k + 1:]
            path = left if sum(math.dist(a, b) for a, b in zip(left, left[1:])) >= \
                sum(math.dist(a, b) for a, b in zip(right, right[1:])) else right
            lost += 1
            continue
        for k, q, yaw in sorted(bad, key=lambda b: -b[0]):
            fix = next((c for d, c in candidates(bvh, q[0], q[1], q[2], 1.0, step=0.1, tol=0.25)
                        if not test(bvh, own, c, yaw, None, hf, check_bob=False)[0] and room(bvh, c, hf)), None)
            if fix is None:   # cut the path there: keep the longer side
                left, right = path[:k + 1], path[k + 1:]
                Ll = sum(math.dist(a, b) for a, b in zip(left, left[1:]))
                Lr = sum(math.dist(a, b) for a, b in zip(right, right[1:]))
                path = left if Ll >= Lr else right
                lost += 1
                break
            path.insert(k + 1, [round(v, 3) for v in fix])
    return path, lost, n


def drape0(bvh, own, pts, hf=1.0):
    """A walker's path, re-laid: every 0.5 m, the nearest standing point within 1.5 m on the level of the last one,
    the floor's own height. -> (points, runs) with None where no point stands; the caller keeps the longest run."""
    dense = []
    for a, b in zip(pts, pts[1:]):
        L = math.dist(a, b)
        n = max(1, int(math.ceil(L / 0.3)))
        dense += [[a[i] + (b[i] - a[i]) * k / n for i in range(3)] for k in range(n)]
    dense.append(list(pts[-1]))
    out = []
    for k, p in enumerate(dense):
        nxt = dense[min(k + 1, len(dense) - 1)]
        pv = dense[max(k - 1, 0)]
        yaw = math.degrees(math.atan2(nxt[0] - pv[0], nxt[2] - pv[2]))
        hit = None
        for d, q in candidates(bvh, p[0], p[1], p[2], 1.5, tol=0.25):
            if not test(bvh, own, q, yaw, None, hf, check_bob=False)[0] and room(bvh, q, hf):
                hit = q
                break
        out.append(hit)
    # the longest run of standing points
    runs, cur = [], []
    for q in out:
        if q is None:
            if cur:
                runs.append(cur)
            cur = []
        else:
            cur.append(q)
    if cur:
        runs.append(cur)
    best = max(runs, key=lambda r: sum(math.dist(a, b) for a, b in zip(r, r[1:])), default=[])
    # light smoothing of the lateral shifts (not the heights: they are the floor's), then drop collinear points
    sm = list(best)
    for k in range(1, len(best) - 1):
        q = [(best[k - 1][0] + 2 * best[k][0] + best[k + 1][0]) / 4, best[k][1],
             (best[k - 1][2] + 2 * best[k][2] + best[k + 1][2]) / 4]
        yaw = math.degrees(math.atan2(best[k + 1][0] - best[k - 1][0], best[k + 1][2] - best[k - 1][2]))
        if not test(bvh, own, q, yaw, None, hf, check_bob=False)[0]:
            sm[k] = q   # smoothed only where the smoothed point still stands
    keep = sm[:1]
    for k in range(1, len(sm) - 1):
        a, b, c2 = keep[-1], sm[k], sm[k + 1]
        u = [c2[i] - a[i] for i in range(3)]
        L = math.sqrt(sum(v * v for v in u)) or 1.0
        t = sum((b[i] - a[i]) * u[i] for i in range(3)) / L
        dev = math.sqrt(max(0.0, sum((b[i] - a[i]) ** 2 for i in range(3)) - t * t))
        if dev > 0.01:
            keep.append(b)
    if len(sm) > 1:
        keep.append(sm[-1])
    return [[round(v, 3) for v in q] for q in keep], sum(1 for q in out if q is None), len(out)


out, summ = [], {"figures": 0, "samples": 0, "fail": 0, "by_reason": {}, "by_kind": {}}
for f in REQ["figures"]:
    if f["kind"] == "rider":
        cab = CABINS.get(f["cabin"])
        if cab is None:
            cab = CABINS[f["cabin"]] = world([{"id": "cabin", "asset": f["cabin"], "pos": [0, 0, 0], "quat": [0, 0, 0, 1],
                                               "scale": [1, 1, 1]}])
        bvh, own = cab
    else:
        bvh, own = TOWN, OWN
    hf = f.get("height_factor", 1.0)
    fails, worst = [], None
    for k, (p, yaw) in enumerate(f["samples"]):
        rs, det = test(bvh, own, p, yaw, f.get("bob"), hf)
        if rs:
            fails.append({"i": k, "pos": p, "reasons": rs, **det})
            g = det.get("gap_m")
            sev = 99 if g is None else abs(g)
            if worst is None or sev > worst[0]:
                worst = (sev, fails[-1])
    rec = {"id": f["id"], "kind": f["kind"], "n_samples": len(f["samples"]), "n_fail": len(fails),
           "ok": not fails, "reasons": sorted({r for x in fails for r in x["reasons"]}),
           "worst": worst[1] if worst else None, "fails": fails[:6]}
    if fails and f["kind"] in ("spot", "rider", "flyer"):
        b = search(bvh, own, f["samples"][0][0], f["samples"][0][1], hf=hf, radius=4.0 if f["kind"] == "spot" else 0.6)
        rec["fix"] = {"move": b[1], "by_m": round(b[0], 2), "bob": b[2] or None} if b else {"drop": True}
    if fails and f["kind"] == "walker" and f.get("path"):
        pts, lost, n = drape(bvh, own, f["path"], hf)
        rec["fix"] = {"path": pts, "lost": lost, "of": n}
    out.append(rec)
    summ["figures"] += 1
    summ["samples"] += len(f["samples"])
    if fails:
        summ["fail"] += 1
        summ["by_kind"][f["kind"]] = summ["by_kind"].get(f["kind"], 0) + 1
        for r in rec["reasons"]:
            summ["by_reason"][r] = summ["by_reason"].get(r, 0) + 1
        print("FAIL %-6s %-28s %s %s" % (f["kind"], f["id"], ",".join(rec["reasons"]),
                                          json.dumps({k: v for k, v in worst[1].items() if k not in ("i",)})))
json.dump({"figures": out, "summary": summ}, open(OUT, "w"), indent=1)
print("summary", json.dumps(summ))
print("done %.1f s" % (time.time() - T0))
