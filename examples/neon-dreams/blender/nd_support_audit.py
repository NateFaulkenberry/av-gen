"""Neon Dreams, iteration 12 (owner msg 149: "extend or remove this final pier support so it doesn't look cut off, make
sure none of the others are"): every leg, stilt, mast and pylon foot that ends in the air, and whether the journey sees
that end.

    Blender -b --factory-startup -P nd_support_audit.py -- CAMS.json OUT.json [--layout art/layout.json]
                                                           [--design art/cloud_design.json] [--cache DIR]

CAMS: [[t, lens, forward, fov_deg], ...] (the journey as the engine rides it). For every kit asset, its loose parts that
are thin (under 1.2 m across) and tall (over 4 m) and reach under the asset's base (a support) give a foot: the
part's lowest point. For every instance, the foot in the world is
  SEATED  when a surface of the town lies within 1.5 m under it (it stands on something),
  CLOUDED when it is inside a cloud lobe (at 0.8 of its size: the eroded edge) or under the sea's top,
  OPEN    otherwise: it ends in clear air.
An OPEN foot is SEEN when some camera sample has it in the frame (16:9), within 450 m, with no town surface between.
Writes every open foot with its nearest sighting (t, metres, px of the part's width). CPU only.
"""
import bpy, sys, os, json, math
import numpy as np
from mathutils import Vector, Quaternion
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[sys.argv.index("--") + 1:]
CAMS, OUT = argv[0], argv[1]
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
LAYP = arg("--layout", os.path.join(HERE, "..", "art", "layout.json"))
DESP = arg("--design", os.path.join(HERE, "..", "art", "cloud_design.json"))
CACHE = arg("--cache", os.path.join(HERE, "..", "scene", "camplan_cache"))
KIT = os.path.join(os.path.dirname(os.path.abspath(LAYP)), "..", "meshes")
LAY = json.load(open(LAYP)); DES = json.load(open(DESP)); cams = json.load(open(CAMS))


def E2P(e):
    return Vector((e[0], -e[2], e[1]))


# ---- the town (nd_camera_plan's cache: run that first)
cf = [os.path.join(CACHE, f) for f in os.listdir(CACHE) if f.endswith(".npz")]
d = np.load(max(cf, key=os.path.getmtime))
bvh = BVHTree.FromPolygons([tuple(v) for v in d["v"]], [tuple(t) for t in d["t"]])

# ---- the supports of each asset (asset frame, Blender Z-up after import)
bpy.ops.wm.read_factory_settings(use_empty=True)
FEET = {}


def feet_of(asset):
    if asset in FEET:
        return FEET[asset]
    before = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.gltf(filepath=os.path.join(KIT, asset))
    except Exception:
        FEET[asset] = []; return []
    new = [o for o in bpy.data.objects if o not in before]
    out = []
    import bmesh
    dg = bpy.context.evaluated_depsgraph_get()
    for o in new:
        if o.type != 'MESH':
            continue
        bm = bmesh.new(); bm.from_object(o, dg); bm.transform(o.matrix_world); bm.verts.ensure_lookup_table()
        seen = set()
        for v in bm.verts:
            if v.index in seen:
                continue
            st, comp = [v], []
            seen.add(v.index)
            while st:
                x = st.pop(); comp.append(x.co.copy())
                for e in x.link_edges:
                    y = e.other_vert(x)
                    if y.index not in seen:
                        seen.add(y.index); st.append(y)
            xs = [c.x for c in comp]; ys = [c.y for c in comp]; zs = [c.z for c in comp]
            w = max(max(xs) - min(xs), max(ys) - min(ys)); h = max(zs) - min(zs)
            if h > 4.0 and w < 1.2 and min(zs) < -2.0:
                low = min(comp, key=lambda c: c.z)
                # the foot: the lowest point's ring centre (mean of the points within 5 cm of the bottom)
                ring = [c for c in comp if c.z < low.z + 0.05]
                fx = sum(c.x for c in ring) / len(ring); fy = sum(c.y for c in ring) / len(ring)
                out.append((fx, fy, low.z, w))
        bm.free()
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    FEET[asset] = out
    return out


lobes = []
for name, ls in DES["masses"].items():
    if isinstance(ls, list):
        lobes += [l for l in ls if isinstance(l, dict)]
for name, ls in DES.get("far_masses", {}).items():
    lobes += [l for l in ls if isinstance(l, dict)]
L = np.array([[l["c"][0], l["c"][1], l["c"][2], l["r"] * l.get("scale", [1, 1, 1])[0], l["r"] * l.get("scale", [1, 1, 1])[1],
               l["r"] * l.get("scale", [1, 1, 1])[2]] for l in lobes])
SEA_TOP = DES["sea_design"]["top_mean"]


def clouded(p):
    q = (np.array(p)[None, :] - L[:, :3]) / (0.8 * L[:, 3:])
    return bool(np.any(np.sum(q * q, axis=1) < 1.0)) or p[1] < SEA_TOP


res = []
for inst in LAY["instances"]:
    a = inst["asset"]
    if a.startswith(("far/", "vehicles/", "signs/")) or "keel_lantern" in a:   # keel lanterns end in a lantern by design
        continue
    fs = feet_of(a)
    if not fs:
        continue
    qx, qy, qz, qw = inst["quat"]
    sc = inst.get("scale", [1, 1, 1])
    for fx, fy, fz, w in fs:
        # asset (Blender Z-up) -> engine local (x, z, -y) -> scaled, rotated, placed (engine) -> plan frame
        loc = Vector((fx * sc[0], fz * sc[1], -fy * sc[2]))
        qe = Quaternion((qw, qx, qy, qz))
        e = qe @ loc
        world = [inst["pos"][0] + e.x, inst["pos"][1] + e.y, inst["pos"][2] + e.z]
        P = E2P(world)
        # seated: something carries on under the foot -- a floor within 1.5 m under it, or the wall of a continuation
        # (a leg extension, a pier leg) within 0.6 m of the point a metre under it
        hit = bvh.ray_cast(P - Vector((0, 0, 0.02)), Vector((0, 0, -1)), 1.5)
        near = bvh.find_nearest(P - Vector((0, 0, 1.0)), 0.6)
        if hit[0] is not None or near[0] is not None:
            state = "seated"
        elif clouded(world):
            state = "clouded"
        else:
            state = "open"
        rec = {"instance": inst["id"], "asset": a, "foot": [round(v, 2) for v in world], "state": state, "width_m": round(w, 2)}
        if state == "open":
            best = None
            for t, lens, fwd, fov in cams:
                v = [world[k] - lens[k] for k in range(3)]; dist = math.sqrt(sum(x * x for x in v))
                if dist > 450 or dist < 0.5:
                    continue
                f = Vector(fwd).normalized(); right = Vector((-f.z, 0, f.x)).normalized(); up = right.cross(f) * -1.0
                vv = Vector(v); z = vv.dot(f)
                if z <= 0:
                    continue
                th = math.tan(math.radians(fov / 2))
                if abs(vv.dot(right) / (z * th * 16 / 9)) > 1 or abs(vv.dot(up) / (z * th)) > 1:
                    continue
                Lp, Pp = E2P(lens), E2P(world)
                dv = Pp - Lp
                h = bvh.ray_cast(Lp, dv.normalized(), dv.length - 0.3)
                if h[0] is not None:
                    continue
                px = 1080 * w / (2 * z * th)
                if best is None or px > best[2]:
                    best = (t, round(dist, 1), round(px, 1))
            rec["seen"] = best
        res.append(rec)
summ = {s: sum(1 for r in res if r["state"] == s) for s in ("seated", "clouded", "open")}
summ["open_seen"] = sum(1 for r in res if r["state"] == "open" and r.get("seen"))
json.dump({"summary": summ, "feet": res}, open(OUT, "w"), indent=1)
FIX = arg("--write-fixes", "")
if FIX:   # the feet the journey sees ending in clear air: nd_band.py continues each 55 m down into the cloud
    fx = [{"instance": r["instance"], "asset": r["asset"], "foot": r["foot"], "width_m": r["width_m"], "seen": r["seen"]}
          for r in res if r.get("seen")]
    json.dump({"_note": "it12 (msg 149): written by blender/nd_support_audit.py --write-fixes; supports whose foot the "
                        "journey sees ending in clear air (not on a surface, not in cloud); nd_band.py continues each",
               "feet": fx}, open(FIX, "w"), indent=1)
print("SUPPORT AUDIT", json.dumps(summ))
for r in sorted([r for r in res if r.get("seen")], key=lambda r: -r["seen"][2])[:40]:
    print("OPEN SEEN", r["instance"], r["asset"], r["foot"], "seen", r["seen"])
