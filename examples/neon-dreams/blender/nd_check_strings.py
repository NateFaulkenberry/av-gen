"""Neon Dreams: the lantern strings against the real meshes. Blender headless, CPU only.

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_check_strings.py -- [layout.json] [--reject]

nd_layout.py picks a string's two anchors from the kit manifest (piers of facade blocks, the masts of roof terraces).
The manifest does not know what a mesh carries at a pier (a vine, a blade sign's bracket, a parapet), so this reads the
exported GLBs: both ends of every string must touch geometry (within 8 cm), and no part of the span further than 0.45 m
from its ends may come within 0.15 m of anything else. With --reject the failing anchor pairs are added to
blender/string_rejects.json, which nd_layout.py reads: run the layout again and it takes its next candidate.
Exit code 3 while any string fails.
"""
import bpy, sys, os, json, math
from mathutils import Vector, Matrix, Quaternion
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nd_assemble as A

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
paths = [a for a in argv if not a.startswith("--")]
layout = json.load(open(paths[0] if paths else os.path.join(HERE, "..", "art", "layout.json")))
kit = os.path.join(HERE, "..", "meshes")
END_GAP, CLEAR = 0.08, 0.15
bpy.ops.wm.read_factory_settings(use_empty=True)
strings = [i for i in layout["instances"] if "lantern_string" in i["asset"]]
keys = layout["checks"].get("lantern_string_anchor_keys", [])


def world_matrix(i):
    """An instance's matrix in Blender's frame: engine (x, y up, z) -> blender (x, -z, y)."""
    q = i["quat"]; s = i["scale"]
    C = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    Me = Matrix.Translation(Vector(i["pos"])) @ Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4() @ Matrix.Diagonal((s[0], s[1], s[2], 1.0))
    return C @ Me @ C.inverted()


trees = []
for i in layout["instances"]:
    if "lantern_string" in i["asset"] or not any(math.dist(i["pos"], s["pos"]) < 45 for s in strings):
        continue
    t = A.load(kit, i["asset"]); M = world_matrix(i)
    trees.append((i["id"], BVHTree.FromPolygons([M @ v.co for v in t.data.vertices], [tuple(p.vertices) for p in t.data.polygons])))
bad = []
for n, s in enumerate(strings):
    M = world_matrix(s); t = A.load(kit, s["asset"])
    xs = [v.co.x for v in t.data.vertices]; half = (max(xs) - min(xs)) / 2
    ends = [M @ Vector((sx, 0.0, 0.0)) for sx in (-half, half)]
    gaps = []
    for p in ends:
        best = (1e9, None)
        for (iid, tr) in trees:
            loc, nor, idx, d = tr.find_nearest(p, 30.0)
            if loc is not None and d < best[0]:
                best = (d, iid)
        gaps.append(best)
    worst = (1e9, None)
    for v in t.data.vertices:
        p = M @ v.co
        if min((p - e).length for e in ends) < 0.45:
            continue
        for (iid, tr) in trees:
            loc, nor, idx, d = tr.find_nearest(p, 3.0)
            if loc is not None and d < worst[0]:
                worst = (d, iid)
    ok = all(g[0] <= END_GAP for g in gaps) and worst[0] >= CLEAR and not any("sign" in (g[1] or "") for g in gaps)
    print("STRING %s %-22s span %5.1f m  ends %.3f (%s) %.3f (%s)  clear %.2f of %s  %s" % (
        s["id"], s["asset"].split("/")[-1], 2 * half * s["scale"][0], gaps[0][0], gaps[0][1], gaps[1][0], gaps[1][1], worst[0], worst[1], "ok" if ok else "FAIL"))
    if not ok:
        bad.append(keys[n] if n < len(keys) else s["id"])
if bad and "--reject" in argv:
    rj = os.path.join(HERE, "string_rejects.json")
    cur = json.load(open(rj))["rejects"] if os.path.exists(rj) else []
    json.dump({"note": "lantern-string anchor pairs (north x, y, south x, y, height; plan metres) that nd_check_strings.py refused; nd_layout.py skips them",
               "rejects": sorted(set(cur) | set(bad))}, open(rj, "w"), indent=1)
print("STRINGS %d, failing %d" % (len(strings), len(bad)))
sys.exit(3 if bad else 0)
