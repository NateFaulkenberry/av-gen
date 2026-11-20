"""Neon Dreams, iteration 10 (owner msgs 112 and 117): the Moorings populated. Writes art/population.json.

    Blender -b --factory-startup -P nd_population.py -- [--out art/population.json] [--plot DIR] [--seed 1012]

What it does (CPU only, about a minute; no GPU lock):
  1. Builds the town as one BVH from art/layout.json (the kit's GLBs and the isles at rest; a formation's bob is the
     engine's offset, so everything standing on one carries its "bob" name).
  2. SURVEY: drops rays on a jittered 1.2 m grid over every populated zone and keeps each upward surface with 2.3 m of
     headroom, flat over a 1 m disc, 0.45 m clear round a body, open to the outside in at least two of eight directions
     (so never inside a building). It notes the drop-off edges within 1.2 m (rails and island rims) and what was hit
     (material role, instance zone, asset, bob).
  3. VISIBILITY: the journey camera (the layout's waypoints, the same monotone arc-length timing and lens as build.py)
     every 0.5 s; a place is seen when it is in frame, at least 4 px tall at 1080p and not hidden by geometry (clouds
     are not tested: the Veil passage hides more than this says).
  4. CASTING, district by district (DISTRICTS below): walkers on paths found on the surface graph, groups, rail
     leaners and fishers, sign and Lantern watchers, stall browsers, gardeners, edge sitters, keepers and workers,
     gondola riders, and the jet-pack flyers of msg 117 (take-off and landing timed for the camera).
  5. Each figure gets the LOD tier the engine can use: A (seen within 40 m), B (40-150 m), C (only beyond 150 m).

The schema is in notes/00-coordination.md ("population.json"). Coordinates out are ENGINE (Y-up); in here, PLAN.
"""
import bpy, sys, os, json, math, random, time, heapq, collections
from mathutils import Vector, Quaternion, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import nd_world as W

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
ROOT = os.path.join(HERE, "..")
LAY = json.load(open(os.path.join(ROOT, "art", "layout.json")))
KIT = os.path.join(ROOT, "meshes")
OUT = arg("--out", os.path.join(ROOT, "art", "population.json"))
PLOT = arg("--plot", None)
SEED = int(arg("--seed", 1012))
STEP = 1.2
SKIP_ZONES = {"keep", "keep_deck", "keep_isle", "highwater", "highwater_isle", "anomaly"}
NOT_GROUND = {"vines", "signs", "lantern_strings", "keel", "gondola"}
SONG = 70.2
CAST = ["alien-scout", "alien-elder", "alien-diver", "alien-pilot", "alien-ranger", "alien-trooper"]
FLYER_CAST = ["alien-pilot", "alien-ranger"]          # the two variants that carry the Backpack_Jet mesh
BODY = 1.72

bpy.ops.wm.read_factory_settings(use_empty=True)
T0 = time.time()
rng = random.Random(SEED)


def E(p):
    return [round(p[0], 3), round(p[2], 3), round(-p[1], 3)]


def P(e):
    return (e[0], -e[2], e[1])


def heading_to_engine(h_deg):
    """Plan heading (0 = east, 90 = north) -> (engine forward, engine yaw in degrees as build.py writes it)."""
    f = [math.cos(math.radians(h_deg)), 0.0, -math.sin(math.radians(h_deg))]
    return [round(v, 4) for v in f], round(math.degrees(math.atan2(f[0], f[2])), 1)


def heading(a, b):
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


# ------------------------------------------------------------------------------------------------ 1. the world
TEMPL = {}


def template(asset):
    if asset in TEMPL:
        return TEMPL[asset]
    before = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.gltf(filepath=os.path.join(KIT, asset))
    except Exception as e:                                   # noqa: BLE001  (a missing gitignored isle is skipped)
        print("skip", asset, e); TEMPL[asset] = None; return None
    new = [o for o in bpy.data.objects if o not in before]
    verts, polys, roles = [], [], []
    dg = bpy.context.evaluated_depsgraph_get()
    for o in new:
        if o.type != 'MESH':
            continue
        ev = o.evaluated_get(dg); me = ev.to_mesh(); mw = o.matrix_world; base = len(verts)
        verts += [tuple(mw @ v.co) for v in me.vertices]
        for p in me.polygons:
            polys.append(tuple(base + i for i in p.vertices))
            m = o.material_slots[p.material_index].material if o.material_slots else None
            roles.append(m.name.split(".")[0] if m else "none")
        ev.to_mesh_clear()
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    TEMPL[asset] = (verts, polys, roles) if polys else None
    return TEMPL[asset]


def inst_matrix(i):
    q = i["quat"]; rot = Quaternion((q[3], q[0], -q[2], q[1])).to_matrix().to_4x4()
    s = i["scale"]
    return Matrix.Translation((i["pos"][0], -i["pos"][2], i["pos"][1])) @ rot @ Matrix.Diagonal((s[0], s[2], s[1], 1.0))


V, PL, REC, INST = [], [], [], []
for i in LAY["instances"]:
    if i["zone"] in SKIP_ZONES or i["asset"].startswith("far/") or i["asset"].startswith("vehicles/"):
        continue
    T = template(i["asset"])
    if T is None:
        continue
    verts, polys, roles = T
    M = inst_matrix(i); base = len(V); k = len(INST)
    INST.append({"id": i["id"], "asset": i["asset"], "zone": i["zone"], "bob": i.get("bob"), "pos": P(i["pos"]), "M": M})
    V += [tuple(M @ Vector(v)) for v in verts]
    for p, r in zip(polys, roles):
        PL.append(tuple(base + j for j in p)); REC.append((k, r))
BVH = BVHTree.FromPolygons(V, PL, all_triangles=False, epsilon=0.0)
del V, PL
print("world: %d instances, %.1f s" % (len(INST), time.time() - T0))
UP, DOWN = Vector((0, 0, 1)), Vector((0, 0, -1))
DIRS8 = [Vector((math.cos(k * math.pi / 4), math.sin(k * math.pi / 4), 0)) for k in range(8)]


def cast(o, d, dist=1e4):
    loc, nrm, idx, dd = BVH.ray_cast(Vector(o), Vector(d), dist)
    return (loc, nrm, idx, dd) if loc is not None else None


def ground(x, y, z, reach=1.0):
    """The walkable height under (x, y) within `reach` of z, or None."""
    g = cast((x, y, z + reach), DOWN, 2 * reach)
    return g[0].z if g is not None and g[1].z > 0.85 else None


def seen_between(a, b):
    d = Vector(b) - Vector(a); L = d.length
    return cast(a, d / L, L - 0.05) is None


# ------------------------------------------------------------------------------------------------ 2. survey
GARDEN_PTS = [I["pos"] for I in INST if I["zone"] in ("roof_garden", "lanternhill_garden") or I["asset"].startswith("garden/")]
GKD = KDTree(len(GARDEN_PTS))
for k, p in enumerate(GARDEN_PTS):
    GKD.insert(p, k)
GKD.balance()

boxes = {}
for I in INST:
    z = I["zone"]
    if z in NOT_GROUND or z in ("roof_garden",):
        continue
    x, y = I["pos"][0], I["pos"][1]
    b = boxes.setdefault(z, [x, y, x, y]); b[0] = min(b[0], x); b[1] = min(b[1], y); b[2] = max(b[2], x); b[3] = max(b[3], y)
for isl in LAY["islands"]:
    if isl["zone"] in SKIP_ZONES:
        continue
    c = P(isl["centre"]); r = max(isl["width_m"], isl["depth_m"]) * 0.6
    if isl["zone"] == "condensery_isle":
        continue                                            # the works' own deck is enough out there
    boxes.setdefault(isl["zone"], [c[0] - r, c[1] - r, c[0] + r, c[1] + r])
cells = set()
for z, b in boxes.items():
    pad = 14.0
    for gx in range(int((b[0] - pad) // STEP), int((b[2] + pad) // STEP) + 1):
        for gy in range(int((b[1] - pad) // STEP), int((b[3] + pad) // STEP) + 1):
            cells.add((gx, gy))

SURF = []
for (gx, gy) in sorted(cells):
    x = (gx + 0.15 + 0.7 * rng.random()) * STEP; y = (gy + 0.15 + 0.7 * rng.random()) * STEP
    z, above = 700.0, None
    for _ in range(8):
        h = cast((x, y, z), DOWN)
        if h is None:
            break
        loc, nrm, idx, d = h
        k, role = REC[idx]; I = INST[k]
        head = (above - loc.z) if above is not None else 99.0
        if nrm.z > 0.93 and head >= 2.3 and I["zone"] not in NOT_GROUND and role not in ("foliage", "pool", "glass", "neon_a"):
            ok = True
            for dx, dy in ((0.5, 0), (-0.5, 0), (0, 0.5), (0, -0.5)):
                g = cast((x + dx, y + dy, loc.z + 1.0), DOWN, 1.4)
                if g is None or abs(g[0].z - loc.z) > 0.12:
                    ok = False; break
            clear, openn = 99.0, 0
            if ok:
                for hz in (0.5, 1.4):
                    for dv in DIRS8:
                        g = cast((x, y, loc.z + hz), dv, 8.0)
                        if g is not None:
                            clear = min(clear, g[3])
                        elif hz == 1.4:
                            openn += 1
                ok = clear >= 0.45 and openn >= 2
            if ok:
                edges = []
                for a, dv in enumerate(DIRS8):
                    gz = ground(x + 1.2 * dv.x, y + 1.2 * dv.y, loc.z, 0.6)
                    if gz is None or abs(gz - loc.z) > 0.5:
                        edges.append(a * 45)
                SURF.append({"p": (x, y, loc.z), "role": role, "zone": I["zone"], "asset": I["asset"], "inst": I["id"], "bob": I["bob"],
                             "clear": min(clear, 3.0), "open": openn, "sky": cast((x, y, loc.z + 2.0), UP, 400.0) is None, "edges": edges,
                             "garden": GKD.find((x, y, loc.z))[2] < 6.0 if GARDEN_PTS else False})
        above = loc.z
        z = loc.z - 0.05
print("survey: %d surfaces, %.1f s" % (len(SURF), time.time() - T0))

BUILDING_ZONES = {"quay_north", "quay_south", "stacks", "lampmarket", "lanternhill", "eastreach", "lampwick", "watch", "station",
                  "isle_west", "isle_west2", "isle_inner", "isle_inner2", "isle_south", "isle_south2", "isle_east", "isle_exit", "lone_lamp"}


def district(s):
    z, role = s["zone"], s["role"]
    if z in ("pier", "plaza", "station", "low_walk", "steps", "garden", "promenade", "gangway"):
        return z
    if z == "quay_deck" or z == "street":
        return "quay"
    if z.startswith("pocket"):
        return "pockets"
    if z == "great_lantern":
        return "lantern_deck"
    if z == "condensery":
        return "condensery" if role == "deck" and s["sky"] else None
    if z in ("lighthouse", "lighthouse_isle"):
        return "lighthouse:" + (s["bob"] or "?")
    if z.endswith("_isle"):
        base = z[:-5]
        return ("isle:" + base) if role == "isle_stone" else None
    if z.endswith("_deck"):
        base = z[:-5]
        return base if base in ("lampmarket", "lanternhill", "stacks", "eastreach") else "isle:" + base
    if z == "lanternhill_garden":
        return "lanternhill"
    if z in BUILDING_ZONES:
        if s["sky"]:
            return ("roof:" + z) if s["garden"] else None
        return ("balcony:" + z) if s["open"] >= 2 and s["open"] <= 5 else None
    return None


for s in SURF:
    s["district"] = district(s)
SURF = [s for s in SURF if s["district"]]
print(collections.Counter(s["district"].split(":")[0] for s in SURF).most_common())

# ------------------------------------------------------------------------------------------------ 3. visibility
WP = [(w["t"],) + tuple(P(w["pos"])) for w in LAY["camera"]["waypoints"]]
J = W.Journey(WP)
FOV = [(0.0, 52.0), (54.0, 52.0), (55.5, 50.5), (57.0, 45.0), (58.95, 39.0), (62.0, 36.5), (SONG, 35.0)]


def fov_at(t):
    for (a, fa), (b, fb) in zip(FOV, FOV[1:]):
        if t <= b:
            return fa + (fb - fa) * (t - a) / (b - a)
    return FOV[-1][1]


TIMES = [round(0.5 * k, 2) for k in range(int(SONG / 0.5) + 1)]
CAMS = []
for t in TIMES:
    c = Vector(J.at(t)); f = Vector(J.fwd(t)).normalized()
    r = f.cross(UP).normalized(); u = r.cross(f).normalized()
    foc = 540.0 / math.tan(math.radians(fov_at(t)) / 2)
    CAMS.append((t, c, f, r, u, foc))


def px_at(cam, p, h=BODY):
    """Projected height in px of a figure standing at p (plan), or 0 when out of frame."""
    t, c, f, r, u, foc = cam
    v = Vector(p) + Vector((0, 0, h * 0.5)) - c
    d = v.dot(f)
    if d < 0.8:
        return 0.0, d
    sx, sy = v.dot(r) / d * foc, v.dot(u) / d * foc
    if abs(sx) > 960 + 20 or abs(sy) > 540 + 20:
        return 0.0, d
    return h * foc / d, d


def visibility(p, occl=True):
    """{'max_px', 'min_dist', 'windows': [[t0, t1]], 'n'} of a figure at plan point p."""
    best, mind, wins, n = 0.0, 1e9, [], 0
    for cam in CAMS:
        px, d = px_at(cam, p)
        if px < 4.0:
            continue
        if occl:
            head = Vector(p) + Vector((0, 0, 1.5)); mid = Vector(p) + Vector((0, 0, 0.9))
            if not (seen_between(cam[1], head) or seen_between(cam[1], mid)):
                continue
        n += 1; best = max(best, px); mind = min(mind, d)
        if wins and abs(wins[-1][1] - cam[0]) <= 0.51:
            wins[-1][1] = cam[0]
        else:
            wins.append([cam[0], cam[0]])
    return {"max_px": round(best, 1), "min_dist_m": round(mind, 1) if n else None, "windows": wins, "samples": n}


def tier(v):
    if not v["samples"]:
        return "C"
    return "A" if v["min_dist_m"] < 40 else ("B" if v["min_dist_m"] < 150 else "C")


# cheap pre-pass: best projected size without occlusion, for every surface
for s in SURF:
    s["px0"] = max(px_at(cam, s["p"])[0] for cam in CAMS)
print("visibility pre-pass %.1f s" % (time.time() - T0))

# ------------------------------------------------------------------------------------------------ 4. look targets
BELL = P(next(i["pos"] for i in LAY["instances"] if i["asset"].endswith("great_lantern_bell.glb")))
BELL = (BELL[0], BELL[1], BELL[2] + 10.0)
MAST = P(next(i["pos"] for i in LAY["instances"] if "mooring_mast" in i["asset"]))
MOON_H = W.MOON["disc_bearing_deg"]
SIGNS = [I for I in INST if I["zone"] == "signs"]
SKD = KDTree(len(SIGNS))
for k, I in enumerate(SIGNS):
    SKD.insert(I["pos"], k)
SKD.balance()


def look_target(kind, p):
    """(plan heading, look_at plan point or None) for an activity at p."""
    if kind == "look_lantern":
        return heading(p, BELL), BELL
    if kind == "look_mast":
        return heading(p, MAST), (MAST[0], MAST[1], MAST[2] + 30)
    if kind == "look_moon":
        return MOON_H + rng.uniform(-12, 12), None
    if kind == "look_sign":
        for (co, k, d) in SKD.find_n((p[0], p[1], p[2] + 3), 6):
            if d < 16 and seen_between((p[0], p[1], p[2] + 1.6), co):
                return heading(p, co), tuple(co)
        return None, None
    return None, None


# ------------------------------------------------------------------------------------------------ 5. casting
PLACED_LIST = []


def free(p, r):
    if not PLACED_LIST:
        return True
    return all(math.dist(q[:2], p[:2]) >= r or abs(q[2] - p[2]) > 2.0 for q in PLACED_LIST if abs(q[0] - p[0]) < r and abs(q[1] - p[1]) < r)


def mark(p):
    PLACED_LIST.append(tuple(p))


CLIPS = {
    "stand": ["Idle", "Idle_turn"], "look_lantern": ["Idle_turn", "Idle"], "look_mast": ["Idle_turn", "Idle"], "look_moon": ["Idle", "Idle_turn"],
    "look_sign": ["Idle_turn", "Idle"], "look_sea": ["Idle", "Idle_turn"], "fish": ["Idle", "Take_from_floor"], "gather": ["Idle", "Idle_turn"],
    "tend": ["Take_from_floor", "Idle"], "browse": ["Take_from_table", "Idle_turn", "Button_push"], "wait": ["Idle", "Idle_turn"],
    "use": ["Button_push", "Take_from_table"], "sit_edge": ["Idle"], "keep": ["Idle_turn", "Idle"], "work": ["Button_push", "Take_from_floor", "Idle"],
    "ride": ["Idle", "Idle_turn"]}
# A sitter has no sit clip in the cast: hold a crouched frame (the engine's call; Idle if it cannot hold a pose).
POSE_HOLD = {"sit_edge": {"clip": "Walking_crouch", "phase": 0.3, "note": "a crouch held as a perch on the rim; Idle facing out if a pose cannot be held"}}
# Audio-behaviour tags (notes/audio-ideas-it10.md): which musical behaviours a figure may take part in.
REACT = {"look_lantern": ["turn_to_lantern"], "gather": ["sway", "gather_crash"], "stand": ["turn_to_lantern", "sway"], "look_sea": ["turn_to_lantern"],
         "fish": [], "browse": ["sway"], "wait": ["turn_to_lantern"], "tend": [], "sit_edge": ["turn_to_lantern"], "look_sign": ["sway"],
         "look_moon": ["turn_to_lantern"], "keep": [], "work": [], "ride": ["turn_to_lantern"], "use": []}

SPOTS, GROUPS, PATHS = [], [], []


def cast_of(k, flyer=False):
    return (FLYER_CAST if flyer else CAST)[k % (2 if flyer else len(CAST))]


def add_spot(kind, p, h_deg, dist, look=None, group=None, note=None, src="gen", bob=None, zone=None):
    f, yaw = heading_to_engine(h_deg)
    v = visibility(p)
    d = {"id": "pop_%04d" % len(SPOTS), "activity": kind, "pos": E(p), "facing_deg": yaw, "forward": f, "district": dist,
         "cast": cast_of(len(SPOTS) * 7 + 3), "clips": CLIPS[kind], "tier": tier(v), "visible": v, "react": REACT.get(kind, [])}
    if look:
        d["look_at"] = E(look)
    if kind in POSE_HOLD:
        d["pose_hold"] = POSE_HOLD[kind]
    if group:
        d["group"] = group
    if bob:
        d["bob"] = bob
    if zone:
        d["zone"] = zone
    if note:
        d["note"] = note
    if src != "gen":
        d["source"] = src
    SPOTS.append(d); mark(p)
    return d


# ---- the layout's own spots stay (hand placed in iterations 2-9), re-tagged into the new vocabulary
KIND_MAP = {"stand": "stand", "look": "look_sign", "rail": "look_sea", "gather": "gather", "wait": "wait", "use": "use", "tend": "tend"}
for sp in LAY["inhabitants"]["spots"]:
    p = P(sp["pos"]); kind = KIND_MAP.get(sp["kind"], "stand")
    if sp["zone"] in ("great_lantern", "plaza") and sp["kind"] == "look":
        kind = "look_lantern"
    h = math.degrees(math.atan2(-sp["forward"][2], sp["forward"][0]))
    dist = {"quay": "quay", "roof_terrace": "roof", "balcony": "balcony"}.get(sp["zone"], sp["zone"])
    add_spot(kind, p, h, dist, look=P(sp["look_at"]) if sp.get("look_at") else None, src=sp["id"], bob=sp.get("bob"), zone=sp["zone"])
N_LAYOUT_SPOTS = len(SPOTS)

# ---- per district: how many, doing what (weights), how far apart, and whether they need to be seen
DISTRICTS = {
    # name: (target, {activity: weight}, spacing m, min px to bother, walkers on paths, groups (n, size range))
    "pier":         (40, {"look_sea": 4, "fish": 5, "stand": 1, "look_lantern": 1}, 4.5, 3, (6, 2), (3, (3, 5))),
    "plaza":        (10, {"look_lantern": 4, "stand": 1, "wait": 1}, 1.6, 5, (3, 1), (2, (3, 4))),
    "station":      (8, {"wait": 5, "use": 1, "stand": 1}, 1.4, 5, (1, 1), (1, (3, 3))),
    "lantern_deck": (8, {"look_lantern": 5, "look_sea": 1}, 1.8, 5, (1, 1), (1, (3, 4))),
    "quay":         (22, {"look_sign": 4, "stand": 2, "look_lantern": 1}, 1.6, 5, (10, 2), (4, (3, 4))),
    "low_walk":     (8, {"look_sea": 4, "fish": 3, "stand": 1}, 2.0, 5, (3, 1), (1, (3, 3))),
    "promenade":    (6, {"look_sea": 4, "look_moon": 2}, 2.0, 5, (2, 1), (1, (3, 3))),
    "steps":        (8, {"tend": 2, "stand": 2, "look_sea": 2}, 1.8, 5, (2, 1), (2, (3, 4))),
    "garden":       (14, {"tend": 4, "stand": 2, "look_moon": 2}, 2.0, 5, (3, 1), (2, (3, 5))),
    "pockets":      (8, {"tend": 3, "stand": 1}, 1.6, 5, (0, 0), (1, (3, 3))),
    "gangway":      (2, {"look_sea": 1}, 3.0, 5, (1, 1), (0, (0, 0))),
    "lampmarket":   (26, {"browse": 0, "look_sign": 3, "stand": 2, "look_lantern": 2}, 1.5, 4, (8, 2), (4, (3, 5))),
    "lanternhill":  (24, {"look_lantern": 3, "look_moon": 2, "stand": 2, "tend": 2}, 2.2, 4, (5, 1), (3, (3, 4))),
    "stacks":       (14, {"look_lantern": 2, "stand": 2, "look_sign": 1}, 2.0, 4, (4, 1), (2, (3, 4))),
    "eastreach":    (14, {"tend": 4, "look_moon": 2, "stand": 1}, 2.0, 4, (3, 1), (2, (3, 4))),
    "roof":         (34, {"tend": 3, "look_lantern": 3, "look_moon": 2, "stand": 1}, 2.0, 4, (0, 0), (5, (3, 4))),
    "balcony":      (22, {"look_sea": 3, "stand": 2, "look_lantern": 1}, 2.0, 4, (0, 0), (0, (0, 0))),
    "isle":         (52, {"tend": 4, "sit_edge": 3, "look_moon": 2, "stand": 1, "look_lantern": 1}, 2.0, 3, (6, 1), (7, (3, 4))),
    "lighthouse":   (9, {"keep": 1, "look_sea": 2, "sit_edge": 1}, 2.0, 2, (0, 0), (0, (0, 0))),
    "condensery":   (12, {"work": 3, "look_moon": 1}, 4.0, 0, (2, 1), (1, (3, 3))),
}


def pool(dname):
    return [s for s in SURF if s["district"] == dname or s["district"].split(":")[0] == dname]


def pick(cands, n, spacing, minpx, weight=lambda s: 1.0):
    cands = [s for s in cands if s["px0"] >= minpx]
    rng.shuffle(cands)
    cands.sort(key=lambda s: -(math.log(1 + s["px0"]) * weight(s) + rng.random() * 1.5))
    out = []
    for s in cands:
        if len(out) >= n:
            break
        if free(s["p"], spacing) and all(math.dist(s["p"], o["p"]) >= spacing for o in out):
            out.append(s)
    return out


# ---- 5a. the surface graph and walk paths
GRAPH_KD = KDTree(len(SURF))
for k, s in enumerate(SURF):
    GRAPH_KD.insert(s["p"], k)
GRAPH_KD.balance()


def neighbours(k):
    s = SURF[k]; out = []
    for (co, j, d) in GRAPH_KD.find_range(s["p"], 2.2 * STEP):
        if j == k:
            continue
        o = SURF[j]
        if abs(o["p"][2] - s["p"][2]) > 0.25:
            continue
        mx, my = (s["p"][0] + o["p"][0]) / 2, (s["p"][1] + o["p"][1]) / 2
        g = ground(mx, my, s["p"][2], 0.4)
        if g is None or abs(g - s["p"][2]) > 0.15:
            continue
        out.append((j, d))
    return out


def walkable_segment(a, b, h0):
    n = max(2, int(math.dist(a[:2], b[:2]) / 0.6))
    for i in range(n + 1):
        u = i / n; x, y = a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u
        g = ground(x, y, h0 + (b[2] - a[2]) * u, 0.35)
        if g is None:
            return False
        for dv in DIRS8[::2]:
            if cast((x, y, g + 0.9), dv, 0.4) is not None:
                return False
    return True


def find_path(k0, k1, limit=4000):
    goal = SURF[k1]["p"]
    openq = [(0.0, k0)]; came = {k0: None}; cost = {k0: 0.0}; it = 0
    while openq and it < limit:
        it += 1
        _, k = heapq.heappop(openq)
        if k == k1:
            break
        for j, d in neighbours(k):
            c = cost[k] + d + (0.6 if SURF[j]["edges"] else 0.0)
            if j not in cost or c < cost[j]:
                cost[j] = c; came[j] = k
                heapq.heappush(openq, (c + math.dist(SURF[j]["p"], goal), j))
    if k1 not in came:
        return None
    pts, k = [], k1
    while k is not None:
        pts.append(SURF[k]["p"]); k = came[k]
    pts.reverse()
    # simplify: keep a point only when the straight walk past it is blocked
    out = [pts[0]]; i = 0
    while i < len(pts) - 1:
        j = len(pts) - 1
        while j > i + 1 and not (abs(pts[j][2] - pts[i][2]) < 0.2 and walkable_segment(pts[i], pts[j], pts[i][2])):
            j -= 1
        out.append(pts[j]); i = j
    return out


def make_paths(dname, n, walkers, cands):
    made = 0
    idx = {id(s): k for k, s in enumerate(SURF)}
    inner = [s for s in cands if not s["edges"] and s["px0"] >= 5]
    tries = 0
    while made < n and tries < n * 12 and len(inner) > 10:
        tries += 1
        a = rng.choice(inner)
        far = [s for s in inner if 14 < math.dist(s["p"][:2], a["p"][:2]) < 70 and abs(s["p"][2] - a["p"][2]) < 0.2]
        if not far:
            continue
        b = max(rng.sample(far, min(6, len(far))), key=lambda s: s["px0"])
        pts = find_path(idx[id(a)], idx[id(b)])
        if not pts or len(pts) < 2:
            continue
        length = sum(math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
        if length < 12:
            continue
        if any(math.dist(pts[0][:2], q["points_plan"][0][:2]) < 4 and math.dist(pts[-1][:2], q["points_plan"][-1][:2]) < 4 for q in PATHS):
            continue
        add_path(dname, pts, walkers, a)
        made += 1
    return made


def add_path(dname, pts, walkers, s, clip=None, speed=None, mode="pingpong", note=None):
    slow = dname in ("garden", "steps", "isle", "promenade", "lanternhill", "eastreach", "pockets", "lighthouse", "pier")
    clip = clip or ("Walking_low_grav" if slow and rng.random() < 0.5 else "Walking")
    speed = speed or (round(rng.uniform(0.75, 0.95), 2) if clip == "Walking_low_grav" else round(rng.uniform(1.0, 1.3), 2))
    length = sum(math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
    period = 2 * length / speed + 2 * 2.5
    vis = [visibility(pts[k]) for k in range(0, len(pts))]
    best = max(vis, key=lambda v: v["max_px"])
    mind = min([v["min_dist_m"] for v in vis if v["min_dist_m"] is not None] or [1e9])
    d = {"id": "walk_%03d" % len(PATHS), "district": dname, "points": [E(p) for p in pts], "points_plan": pts, "speed_mps": speed, "mode": mode,
         "clip": clip, "walkers": walkers, "pause_s": 2.5, "phases_s": [round(rng.uniform(0, period), 2) for _ in range(walkers)],
         "cast": [cast_of(len(PATHS) * 5 + w) for w in range(walkers)], "bob": s.get("bob"), "length_m": round(length, 1),
         "tier": "A" if mind < 40 else ("B" if mind < 150 else "C"), "visible": {"max_px": best["max_px"], "min_dist_m": None if mind > 1e8 else mind},
         "react": ["sway"]}
    if note:
        d["note"] = note
    PATHS.append(d)
    for p in pts:
        mark(p)


# ---- the layout's own paths are kept (priority 1 and 2), re-expressed
DROPPED_LAYOUT_PATHS = []
for p in LAY["inhabitants"]["paths"]:
    pts = [P(q) for q in p["points"]]
    s = {"bob": p.get("bob")}
    n_ok = n_all = 0
    for a, b in zip(pts, pts[1:]):
        n = max(2, int(math.dist(a[:2], b[:2]) / 1.0))
        for i in range(n + 1):
            u = i / n; x, y, z = (a[j] + (b[j] - a[j]) * u for j in range(3))
            g = ground(x, y, z, 1.2); n_all += 1                  # 1.2 m: a straight key line over a flight of steps is fine
            n_ok += g is not None and not any(cast((x, y, g + 0.9), dv, 0.35) is not None for dv in DIRS8[::2])
    if p["id"] != "pier_walk" and n_ok < 0.9 * n_all:      # the pier walk is checked by eye: its lamps stand on the line
        DROPPED_LAYOUT_PATHS.append((p["id"], round(n_ok / n_all, 2))); continue
    add_path(p["zone"], pts, max(1, min(2, p.get("walkers", 1))), s, clip=p.get("clip", "Walking"), speed=p.get("speed_mps", 1.2),
             note="layout path %s" % p["id"])
N_LAYOUT_PATHS = len(PATHS)


def place_group(dname, s, size, gid, rad=None):
    """A ring of `size` facing its centre, every member on the same level and clear of walls."""
    c = s["p"]; rad = rad or rng.uniform(0.85, 1.25); a0 = rng.uniform(0, 360)
    mem = []
    for m in range(size):
        a = math.radians(a0 + 360.0 * m / size + rng.uniform(-14, 14))
        x, y = c[0] + rad * math.cos(a), c[1] + rad * math.sin(a)
        g = ground(x, y, c[2], 0.3)
        if g is None or abs(g - c[2]) > 0.12:
            return None
        if any(cast((x, y, g + h), dv, 0.35) is not None for h in (0.5, 1.4) for dv in DIRS8[::2]):
            return None
        mem.append((x, y, g))
    if not all(free(p, 1.0) for p in mem):
        return None
    ids = []
    for m, p in enumerate(mem):
        kind = "gather"
        sp = add_spot(kind, p, heading(p, c) + rng.uniform(-10, 10), dname, group=gid, bob=s.get("bob"), zone=s["zone"])
        ids.append(sp["id"])
    GROUPS.append({"id": gid, "district": dname, "centre": E(c), "members": ids, "bob": s.get("bob"),
                   "note": "a ring facing its centre; one member may Idle_turn to the Lantern on the bass swell (turn_to_lantern)"})
    return ids


ACT_WEIGHTS = {}
for dname, (target, acts, spacing, minpx, (npaths, wk), (ngroups, gsize)) in DISTRICTS.items():
    cands = pool(dname)
    if not cands:
        print("district", dname, "has no surfaces"); continue
    # sub-districts (isles, lighthouses, roofs, balconies): spread the target over them by how much the camera sees
    subs = collections.defaultdict(list)
    for s in cands:
        subs[s["district"]].append(s)
    shares = {k: sum(min(s["px0"], 60) for s in v) ** 0.5 for k, v in subs.items()}
    tot = sum(shares.values()) or 1.0
    for sub, sc in sorted(subs.items()):
        share = shares[sub] / tot
        n_paths = int(round(npaths * share)) if len(subs) > 1 else npaths
        if dname == "isle":
            n_paths = 1 if share > 0.08 else 0
        if n_paths:
            make_paths(sub, n_paths, wk, sc)
        n_groups = int(round(ngroups * share + (0.3 if len(subs) > 1 else 0)))
        for g in range(n_groups):
            gs = pick([s for s in sc if not s["edges"]], 1, 4.0, minpx)
            if gs:
                place_group(sub, gs[0], rng.randint(*gsize), "grp_%03d" % len(GROUPS))
        n_single = max(0, int(round(target * share)) - n_groups * 3 - n_paths * wk)
        if dname in ("lighthouse",):
            n_single = 3
        if dname == "isle":
            n_single = max(2, n_single)
        # rail / rim activities need an edge; the rest keep off it
        for kind, w in acts.items():
            if w <= 0:
                continue
            k_n = int(round(n_single * w / sum(acts.values())))
            if k_n <= 0:
                continue
            need_edge = kind in ("look_sea", "fish", "sit_edge")
            cc = [s for s in sc if bool(s["edges"]) == need_edge]
            if kind == "tend":
                cc = [s for s in cc if s["garden"]] or cc
            for s in pick(cc, k_n, spacing, minpx):
                p = s["p"]
                if need_edge:
                    ex = [math.cos(math.radians(a)) for a in s["edges"]]; ey = [math.sin(math.radians(a)) for a in s["edges"]]
                    h = math.degrees(math.atan2(sum(ey), sum(ex))) + rng.uniform(-15, 15)
                    look = None
                    if kind == "sit_edge":                     # perch on the rim itself: 0.35 m further out, if the rock is there
                        x2, y2 = p[0] + 0.35 * math.cos(math.radians(h)), p[1] + 0.35 * math.sin(math.radians(h))
                        g = ground(x2, y2, p[2], 0.3)
                        if g is not None and abs(g - p[2]) < 0.15:
                            p = (x2, y2, g)
                else:
                    h, look = look_target(kind, p)
                    if h is None:
                        if kind == "look_sign":
                            kind2 = "stand"; h = rng.uniform(0, 360); look = None
                            add_spot(kind2, p, h, s["district"], bob=s.get("bob"), zone=s["zone"]); continue
                        h = rng.uniform(0, 360)
                add_spot(kind, p, h, s["district"], look=look, bob=s.get("bob"), zone=s["zone"])
print("layout paths dropped (blocked or off the deck):", DROPPED_LAYOUT_PATHS)
print("cast placed: %d spots, %d paths, %d groups, %.1f s" % (len(SPOTS), len(PATHS), len(GROUPS), time.time() - T0))

# ---- 5b. stall browsers in the Lampmarket: 1.2 m in front of each kiosk, facing it
for i in LAY["instances"]:
    if not i["asset"].endswith("kiosk.glb") or i["zone"] in SKIP_ZONES:
        continue
    M = inst_matrix(i); c = M.translation
    front = (M.to_3x3() @ Vector((0, -1, 0))).normalized()          # GLB +z (front) is Blender -y
    for side in (-0.7, 0.7) if rng.random() < 0.6 else (0.0,):
        lat = front.cross(UP).normalized()
        p0 = c + front * 1.25 + lat * side
        g = ground(p0.x, p0.y, c.z, 0.5)
        if g is None:
            continue
        p = (p0.x, p0.y, g)
        if free(p, 0.9) and visibility(p, occl=False)["max_px"] >= 4:
            add_spot("browse", p, heading(p, (c.x, c.y)), "lampmarket" if i["zone"].startswith("lamp") else i["zone"], look=(c.x, c.y, c.z + 1.3),
                     bob=i.get("bob"), zone=i["zone"])

# ---- 5c. gondola riders: two or three standing in each cabin, children of the cabin node
cab = template("vehicles/gondola_cabin.glb")
floor = 0.0
if cab:
    vs = cab[0]
    zs = sorted(v[2] for v in vs if abs(v[0]) < 0.6 and abs(v[1]) < 0.4)
    floor = next((z for z in zs if z > -0.5), 0.0)
RIDERS = []
for v in LAY["vehicles"]:
    if "gondola" not in v["asset"]:
        continue
    n = 3 if "lampwick" not in v["spline"] else 2
    slots = [(-1.1, 0.35), (0.2, -0.45), (1.2, 0.3)][:n]
    for k, (lx, lz) in enumerate(slots):
        RIDERS.append({"id": "ride_%02d" % len(RIDERS), "vehicle_spline": v["spline"], "vehicle_instance": v.get("instance"),
                       "local_pos": [lx, round(floor + 0.02, 3), lz], "local_yaw_deg": rng.choice([0.0, 180.0]) + rng.uniform(-30, 30),
                       "activity": "ride", "clips": CLIPS["ride"], "cast": cast_of(k + len(RIDERS) * 3), "tier": "B",
                       "note": "stands in the cabin (GLB local frame, floor measured); faces out of a window; parent to the cabin node",
                       "react": ["turn_to_lantern"]})

# ------------------------------------------------------------------------------------------------ 6. flyers (msg 117)
# Each: a take-off spot, a landing spot, a cruise height clear of the camera path, and the start time chosen so the
# camera sees the lift-off and/or the touchdown. Phases: Idle -> Jumping (the push-off, first 0.8 s) -> Floating (rise,
# hover) -> Flying_jet (cruise, the body horizontal and the node yawed along the travel) -> Floating (descent) ->
# Landing -> Idle. Position keys every 0.25 s; the engine owns the blend and may replace the curve with its behaviour.
FLY_SPEC = [
    # id, from (district or (x, y)), to, cruise over the higher end (m), cruise speed, note
    ("fly_pier_isle", "pier_end", "isle:isle_west2", 9.0, 9.0, "pier end -> the fill isle west2: off the last span, over the gap"),
    ("fly_hill_mast", "lanternhill_roof", "mast", 7.0, 7.0, "a Lanternhill roof garden -> the Mooring Mast's crown deck"),
    ("fly_lampwick_garden", "isle:lampwick", "garden", 10.0, 9.0, "Lampwick -> the east garden (the station is behind the Quay's towers from Lampwick: a direct line hits the corner tower)"),
    ("fly_isle_isle", "isle:isle_south", "isle:isle_south2", 10.0, 8.0, "island garden to island garden, south of the Quay"),
    ("fly_market_lantern", "lampmarket", "lantern_deck", 0.0, 10.0, "the Lampmarket -> a hover round the Great Lantern's bell -> down on its deck"),
    ("fly_reach_east", "eastreach", "isle:isle_east", 9.0, 8.0, "an Eastreach roof -> the east isle, crossing the journey high ahead of the camera"),
]


def end_surface(key, near=None):
    if key == "pier_end":
        c = [s for s in pool("pier") if abs(s["p"][2] - 131.2) < 0.3]          # the last intact span, not the dropped ones
        root = W.PIER_ROOT
        return max(c, key=lambda s: math.dist(s["p"][:2], root[:2]) - (3 if s["edges"] else 0) + rng.random() * 3) if c else None
    if key == "mast":
        c = [s for s in SURF if math.dist(s["p"][:2], MAST[:2]) < 14 and s["p"][2] > MAST[2] - 1]
        return max(c, key=lambda s: s["p"][2]) if c else None
    if key == "lanternhill_roof":
        c = [s for s in SURF if s["district"] == "roof:lanternhill"] or pool("lanternhill")
        return max(c, key=lambda s: min(s["px0"], 40) - math.dist(s["p"][:2], MAST[:2]) / 3 + rng.random() * 5) if c else None
    if key == "station":
        c = pool("station") or pool("plaza")
        return max(c, key=lambda s: s["px0"] + rng.random()) if c else None
    c = [s for s in SURF if s["district"] == key and not s["edges"]] or pool(key)
    if not c:
        return None
    if near is not None:
        return min(c, key=lambda s: math.dist(s["p"][:2], near[:2]) + rng.random() * 4)
    return max(c, key=lambda s: s["px0"] + rng.random() * 4)


def smooth(u):
    return u * u * (3 - 2 * u)


def flight_curve(a, b, cruise, speed, orbit=None):
    """Plan keyframes [(dt, pos, clip, yaw_heading)] from take-off at a to touchdown at b."""
    keys = []
    hold, push, rise = 1.2, 0.8, 2.6
    top_a = (a[0], a[1], a[2] + 6.0)
    hc = max(a[2], b[2]) + cruise
    keys.append((0.0, a, "Idle"))
    keys.append((hold, a, "Jumping"))
    keys.append((hold + push, (a[0], a[1], a[2] + 0.6), "Floating"))
    keys.append((hold + push + rise, top_a, "Floating"))
    t = hold + push + rise
    # cruise: a cubic from the top of the rise to 7 m over the landing, peaking at the cruise height
    over_b = (b[0], b[1], b[2] + 7.0)
    c1 = (top_a[0] + (b[0] - a[0]) * 0.25, top_a[1] + (b[1] - a[1]) * 0.25, hc)
    c2 = (over_b[0] - (b[0] - a[0]) * 0.25, over_b[1] - (b[1] - a[1]) * 0.25, hc)
    path = []
    if orbit is not None:                                 # a loop round a point before coming down (the Lantern)
        oc, orad, oh = orbit
        a0 = math.atan2(top_a[1] - oc[1], top_a[0] - oc[0])
        start = (oc[0] + orad * math.cos(a0), oc[1] + orad * math.sin(a0), oh)
        for k in range(1, 21):
            u = k / 20; path.append(tuple(top_a[i] + (start[i] - top_a[i]) * smooth(u) for i in range(3)))
        for k in range(1, 49):
            ang = a0 + 2 * math.pi * 0.8 * k / 48
            path.append((oc[0] + orad * math.cos(ang), oc[1] + orad * math.sin(ang), oh + 2.5 * math.sin(3 * ang)))
        last = path[-1]
        for k in range(1, 21):
            u = k / 20; path.append(tuple(last[i] + (over_b[i] - last[i]) * smooth(u) for i in range(3)))
    else:
        for k in range(1, 61):
            u = k / 60
            path.append(tuple((1 - u) ** 3 * top_a[i] + 3 * (1 - u) ** 2 * u * c1[i] + 3 * (1 - u) * u * u * c2[i] + u ** 3 * over_b[i] for i in range(3)))
    prev = top_a
    for q in path:
        d = math.dist(prev, q); t += d / speed
        keys.append((t, q, "Flying_jet")); prev = q
    keys[-1] = (keys[-1][0], keys[-1][1], "Floating")
    t += 2.8
    keys.append((t, (b[0], b[1], b[2] + 0.4), "Landing"))
    t += 0.9
    keys.append((t, b, "Idle"))
    return keys


def sample(keys, dt):
    for (t0, p0, c0), (t1, p1, c1) in zip(keys, keys[1:]):
        if dt <= t1:
            u = (dt - t0) / max(t1 - t0, 1e-6)
            return tuple(p0[i] + (p1[i] - p0[i]) * u for i in range(3)), c0
    return keys[-1][1], keys[-1][2]


def path_clear(keys):
    """No segment of the flight passes through geometry (a 0.6 m body)."""
    for (t0, p0, c0), (t1, p1, c1) in zip(keys[3:-2], keys[4:-2]):
        a = Vector(p0) + Vector((0, 0, 1.0)); b = Vector(p1) + Vector((0, 0, 1.0))
        d = b - a
        if d.length > 0.01 and cast(a, d.normalized(), d.length + 0.6) is not None:
            return False
    return True


KICKS = [t for t, _ in json.load(open(os.path.join(ROOT, "audio", "sabertooth_events.json")))["stems"]["kick"]["onsets"]]
FLYERS = []
SURF_ALL = SURF
for fid, ka, kb, cruise, speed, note in FLY_SPEC:
    best = None
    found = None
    SURF = [s for s in SURF_ALL if s["sky"]]                 # a flyer takes off and lands under open sky
    for attempt in range(16):
        sa = end_surface(ka)
        if sa is None:
            break
        if kb == "lantern_deck":
            sb = end_surface("lantern_deck"); orbit = (BELL[:2], 36.0 + 4 * (attempt % 3), BELL[2] - 4.0 + 6 * (attempt // 3 % 3))
        else:
            sb = end_surface(kb, near=sa["p"]); orbit = None
        if sb is None:
            break
        keys = flight_curve(sa["p"], sb["p"], cruise + 4 * (attempt % 4), speed, orbit)
        if path_clear(keys):
            found = (sa, sb, keys); break
    SURF = SURF_ALL
    if not found:
        print("flyer", fid, "blocked"); continue
    sa, sb, keys = found
    dur = keys[-1][0]
    # choose the start so the camera sees lift-off and touchdown (score: px of each, summed over their 3 s windows), and
    # the flyer never comes within 15 m of the lens
    for t_start in [x * 0.5 for x in range(int((SONG - 4) / 0.5))]:
        score, near = 0.0, 1e9
        for cam in CAMS:
            dt = cam[0] - t_start
            if dt < 0 or dt > dur + 3:
                continue
            p, clip = sample(keys, dt)
            near = min(near, (Vector(p) - cam[1]).length)
            px, d = px_at(cam, p)
            if px >= 4 and clip in ("Jumping", "Floating", "Landing") and seen_between(cam[1], Vector(p) + Vector((0, 0, 1.0))):
                score += min(px, 80.0)
            elif px >= 4 and clip == "Flying_jet":
                score += 0.3 * min(px, 60.0)
        if near < 15.0:
            continue
        if best is None or score > best[0]:
            best = (score, t_start, near)
    if best is None or best[0] <= 0:
        print("flyer", fid, "never seen; start 0"); best = (0.0, 0.0, None)
    score, t_start, near = best
    # On the beat (notes/audio-ideas-it10.md, idea 1): the feet leave the deck on a kick (0.45 s into Jumping, the
    # push-off) and touch down on a kick (the Landing key), the cruise stretched or squeezed by under 6 % to get there
    lift = t_start + 1.2 + 0.45
    k_lift = min(KICKS, key=lambda k: abs(k - lift)); t_start += k_lift - lift
    i_land = next(i for i, k in enumerate(keys) if k[2] == "Landing")
    i_cr0 = next(i for i, k in enumerate(keys) if k[2] == "Flying_jet")
    land = t_start + keys[i_land][0]
    cruise = keys[i_land - 1][0] - keys[i_cr0 - 1][0]
    opts = [k for k in KICKS if abs(k - land) <= 0.06 * cruise] or [land]
    k_land = min(opts, key=lambda k: abs(k - land)); stretch = 1.0 + (k_land - land) / cruise
    t0c = keys[i_cr0 - 1][0]
    keys = [(t if i < i_cr0 else (t0c + (t - t0c) * stretch if i < i_land else t + (k_land - land)), p_, c_) for i, (t, p_, c_) in enumerate(keys)]
    dur = keys[-1][0]
    out_keys, prev = [], None
    for k, (t, p, clip) in enumerate(keys):
        nxt = keys[min(k + 1, len(keys) - 1)][1]
        hdg = heading(p, nxt) if math.dist(p[:2], nxt[:2]) > 0.05 else (out_keys[-1]["heading_deg"] if out_keys else heading(sa["p"], sb["p"]))
        f, yaw = heading_to_engine(hdg)
        out_keys.append({"t": round(t_start + t, 3), "pos": E(p), "clip": clip, "yaw_deg": yaw, "heading_deg": round(hdg, 1)})
    phases, last = [], None
    for k in out_keys:
        if k["clip"] != last:
            phases.append({"t": k["t"], "clip": k["clip"]}); last = k["clip"]
    va = visibility(sa["p"]); vb = visibility(sb["p"])
    FLYERS.append({"id": fid, "cast": FLYER_CAST[len(FLYERS) % 2], "note": note, "t_start": t_start, "t_takeoff": round(t_start + 1.2, 2),
                   "t_land": round(t_start + dur - 0.9, 2), "duration_s": round(dur, 2),
                   "on_beat": {"liftoff_kick_s": round(k_lift, 2), "touchdown_kick_s": round(k_land, 2), "cruise_stretch": round(stretch, 4)},
                   "takeoff": {"pos": E(sa["p"]), "district": sa["district"], "bob": sa.get("bob")},
                   "landing": {"pos": E(sb["p"]), "district": sb["district"], "bob": sb.get("bob")},
                   "cruise_height_m": round(max(sa["p"][2], sb["p"][2]) + cruise, 1), "speed_mps": speed, "camera_min_dist_m": round(near, 1) if near else None,
                   "seen_score": round(score, 1), "phases": phases, "keys": [{kk: v for kk, v in k.items() if kk != "heading_deg"} for k in out_keys],
                   "plume": "props/jet_plume.glb on socket Backpack (see 00-coordination.md)",
                   "after_landing": "Idle at the landing spot to the end of the song (it does not fly back)"})
print("flyers", [(f["id"], f["t_takeoff"], f["t_land"], f["seen_score"]) for f in FLYERS])

# ------------------------------------------------------------------------------------------------ 7. stats per shot
SHOTS = [("approach", 0, 12), ("harbour_pier", 12, 22), ("station_plaza", 22, 27), ("quay", 27, 38), ("steps_garden", 38, 47),
         ("veil_dive", 47, 56), ("reveal", 56, 70.2)]


def walker_pos(pth, w, t):
    pts = pth["points_plan"]; seg = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]; L = sum(seg)
    sp, pause = pth["speed_mps"], pth["pause_s"]
    per = 2 * L / sp + 2 * pause
    u = (t + pth["phases_s"][w]) % per
    if u < L / sp:
        s = u * sp
    elif u < L / sp + pause:
        s = L
    elif u < 2 * L / sp + pause:
        s = L - (u - L / sp - pause) * sp
    else:
        s = 0.0
    for i, d in enumerate(seg):
        if s <= d or i == len(seg) - 1:
            a, b = pts[i], pts[i + 1]; v = min(s / d, 1.0) if d else 0.0
            return tuple(a[j] + (b[j] - a[j]) * v for j in range(3))
        s -= d


counts = {}
for name, a, b in SHOTS:
    per_t = []
    for cam in CAMS:
        if not (a <= cam[0] < b):
            continue
        n4 = n20 = 0
        figs = [P(s["pos"]) for s in SPOTS] + [walker_pos(p, w, cam[0]) for p in PATHS for w in range(p["walkers"])]
        for f in FLYERS:
            if f["keys"][0]["t"] <= cam[0] <= f["keys"][-1]["t"]:
                figs.append(P(min(f["keys"], key=lambda k: abs(k["t"] - cam[0]))["pos"]))
        for p in figs:
            px, d = px_at(cam, p)
            if px >= 4 and (seen_between(cam[1], Vector(p) + Vector((0, 0, 1.5))) or seen_between(cam[1], Vector(p) + Vector((0, 0, 0.9)))):
                n4 += 1
                n20 += px >= 20
        per_t.append((n4, n20))
    counts[name] = {"t": [a, b], "visible_max": max(x[0] for x in per_t), "visible_mean": round(sum(x[0] for x in per_t) / len(per_t), 1),
                    "readable_20px_max": max(x[1] for x in per_t), "readable_20px_mean": round(sum(x[1] for x in per_t) / len(per_t), 1)}
print(json.dumps(counts, indent=1))

n_walkers = sum(p["walkers"] for p in PATHS)
tiers = collections.Counter([s["tier"] for s in SPOTS] + [p["tier"] for p in PATHS for _ in range(p["walkers"])] + ["A"] * len(FLYERS) + ["B"] * len(RIDERS))
by_d = collections.Counter([s["district"].split(":")[0] for s in SPOTS] + [p["district"].split(":")[0] for p in PATHS for _ in range(p["walkers"])])
doc = {
    "version": 1,
    "_note": "Written by blender/nd_population.py (iteration 10, owner msgs 112/117). Engine frame (Y-up, metres). Schema: notes/00-coordination.md.",
    "scale": 1.0, "cast": CAST, "flyer_cast": FLYER_CAST,
    "lod": {"A": "seen within 40 m: posed every frame, full clip", "B": "seen at 40-150 m: posed at 10-15 Hz", "C": "only seen beyond 150 m (or never): one held pose, re-posed at 2-5 Hz or frozen"},
    "spots": SPOTS, "groups": GROUPS, "paths": [{k: v for k, v in p.items() if k != "points_plan"} for p in PATHS], "riders": RIDERS, "flyers": FLYERS,
    "stats": {"spots": len(SPOTS), "from_layout_spots": N_LAYOUT_SPOTS, "paths": len(PATHS), "from_layout_paths": N_LAYOUT_PATHS, "walkers": n_walkers,
              "groups": len(GROUPS), "riders": len(RIDERS), "flyers": len(FLYERS), "total_figures": len(SPOTS) + n_walkers + len(RIDERS) + len(FLYERS),
              "tiers": dict(tiers), "by_district": dict(by_d), "per_shot": counts},
}
json.dump(doc, open(OUT, "w"), indent=None, separators=(",", ":"))
print("wrote", OUT, json.dumps(doc["stats"], indent=None)[:1200])
print("done %.1f s" % (time.time() - T0))
if PLOT:
    os.makedirs(PLOT, exist_ok=True)
    json.dump({"surf": [[round(s["p"][0], 1), round(s["p"][1], 1), s["district"]] for s in SURF[::2]],
               "spots": [[P(s["pos"])[0], P(s["pos"])[1], s["activity"], s["tier"]] for s in SPOTS],
               "paths": [[[q[0], q[1]] for q in p["points_plan"]] for p in PATHS],
               "flyers": [[[P(k["pos"])[0], P(k["pos"])[1], k["clip"]] for k in f["keys"]] for f in FLYERS],
               "journey": [list(J.at(t / 4))[:2] for t in range(0, int(SONG * 4))]}, open(os.path.join(PLOT, "plot.json"), "w"))
