"""The Living Prism (aurora revision): the crystal formations, generated (Blender 5.x, background).

    Blender -b --factory-startup --python-exit-code 1 -P examples/living-prism-aurora/blender/aurora_assets.py --
        <out dir> [--preview <dir>]

Writes <out>/<family>.glb, one mesh per formation family, base at Blender z = 0 (AV Gen y = 0), its front facing
Blender -Y (AV Gen +Z). --preview composes the valley from layout.py (a height-field mesh) with the formations and
renders a Workbench still from the fixed camera: a composition check, not the look.

The shard (the reference's crystals, 3.1): a long, irregular, tabular prism of 4-6 sides with large flat faces and an
oblique termination (one or two apex points), not a hexagonal quartz. Families give each formation its own
silhouette (spec 4.4): a tall leaning cluster, a wide tilted pair, spires, small and distant groups.
"""
import json
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import layout as L  # noqa: E402


def argv():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return a[0], (a[a.index("--preview") + 1] if "--preview" in a else None)


def basis(axis):
    a = Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(a.z) < 0.9 else Vector((1, 0, 0))
    u = a.cross(ref).normalized()
    return u, a.cross(u).normalized(), a


def shard(bm, base, axis, width, length, rng, sides=None, flat=None, tip=None, chisel=None, embed=0.12, broken=False):
    """One crystal shard into bm. width: its cross-section's larger radius; flat: thickness / width."""
    sides = sides or 6                                      # phase 2c: hexagonal columns, always
    flat = flat if flat is not None else rng.uniform(0.7, 0.95)    # 3c: some tabular, never a slab
    tip = tip if tip is not None else rng.uniform(0.3, 0.45) * length  # 3c: long, sharp blade tips
    chisel = False if chisel is None else chisel   # pointed terminations only (a chisel read as a wedge)
    u, v, a = basis(axis)
    spin = rng.uniform(0, math.tau)
    # 3c: the faces differ in width (corner angles jittered well off the regular hexagon), the radii too
    angles = sorted(spin + math.tau * (i + rng.uniform(-0.22, 0.22)) / sides for i in range(sides))
    rs = [width * rng.uniform(0.8, 1.12) for _ in range(sides)]
    base = Vector(base)

    def ring(c, sc):
        return [bm.verts.new(c + u * (math.cos(t) * r * sc) + v * (math.sin(t) * r * sc * flat))
                for t, r in zip(angles, rs)]
    r0 = ring(base - a * (embed * length), 1.0)
    if broken:   # phase 2g: a broken stub -- the column snapped off on a slanted plane, no termination
        r1 = ring(base + a * length, 1.0)
        cut = (u * rng.uniform(-1, 1) + v * rng.uniform(-1, 1)).normalized()
        top = base + a * length
        for vv in r1:
            vv.co += a * ((vv.co - top).dot(cut) * rng.uniform(0.5, 0.9))
        for i in range(sides):
            j = (i + 1) % sides
            bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
        bm.faces.new(r1)
        bm.faces.new(list(reversed(r0)))
        return
    shard_body(bm, base, a, u, v, width, length, tip, flat, angles, rs, rng, embed)


def shard_body(bm, base, a, u, v, width, length, tip, flat, angles, rs, rng, embed):
    """Phase 3 (spec 5.1): the hero shard. Every corner of the hexagon is chamfered (two vertices a little apart),
    so each long primary face is flanked by a narrow secondary facet that catches light on its own; a mid ring kinks
    the column (the primary faces break into two planes); one or two corners of the upper ring are chipped; the
    termination is two-staged (a ring at about half the tip, its corners at irregular heights) to a skewed apex."""
    d = rng.uniform(0.07, 0.11)                  # the chamfer, radians either side of a corner
    corners = []
    for t, r in zip(angles, rs):
        corners += [(t - d, r * math.cos(d)), (t + d, r * math.cos(d))]
    n = len(corners)

    ci = bm.verts.layers.int.get("ci") or bm.verts.layers.int.new("ci")

    def ring(c, sc, jitter=0.0, chips=(), lift=None):
        out = []
        for k, (t, r) in enumerate(corners):
            rr = r * sc * (0.78 if k // 2 in chips else 1.0)
            p = c + u * (math.cos(t) * rr) + v * (math.sin(t) * rr * flat)
            if lift is not None:
                p = p + a * lift[k // 2]
            vv = bm.verts.new(p)
            vv[ci] = k + 1      # 1-based place in the chamfered ring (0: not a ring vertex)
            out.append(vv)
        return out
    kink = (u * rng.uniform(-1, 1) + v * rng.uniform(-1, 1)) * width * 0.07
    chips = set(rng.sample(range(len(angles)), rng.choice([0, 1, 1, 2])))
    off = (u * rng.uniform(-1, 1) + v * rng.uniform(-1, 1)) * width * 0.15
    lifts = [rng.uniform(-0.12, 0.12) * tip for _ in angles]
    rings = [ring(base - a * (embed * length), 1.0),
             ring(base + a * (length * rng.uniform(0.42, 0.6)) + kink, rng.uniform(0.96, 1.03)),
             ring(base + a * (length - tip), rng.uniform(0.88, 0.97), chips=chips),
             ring(base + a * (length - tip * 0.5) + off * 0.4, rng.uniform(0.3, 0.42), lift=lifts)]
    for r0, r1 in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    apex = bm.verts.new(base + a * length + off)
    for i in range(n):
        bm.faces.new((rings[-1][i], rings[-1][(i + 1) % n], apex))
    bm.faces.new(list(reversed(rings[0])))


def lean(rng, out_x, deg_lo, deg_hi, spread=25):
    t = math.radians(rng.uniform(deg_lo, deg_hi))
    az = math.radians(rng.uniform(-spread, spread)) + (0.0 if out_x > 0 else math.pi)
    return (math.sin(t) * math.cos(az), math.sin(t) * math.sin(az), math.cos(t))


FAMILIES = {
    # phase 2c: the left hero is a FAN -- three tall shards leaning outward (to the left, -x), shorter ones around
    # the base leaning every way; no single central spike. dy > 0 is behind (Blender +Y = AV Gen -Z).
    # phase 2h: no shard leans past ~25 deg (a 40 deg one lay almost flat toward the camera: a slab)
    # phase 2i (the owner: "remove the little crystal on the lefet side of the bank"): the three small front shards
    # are gone
    "fan": (13, [(0, 0, 4.0, 52, 10, 16), (-7, 4, 3.6, 46, 18, 26), (5, 6, 3.2, 40, 6, 12),
                 (-12, -3, 2.8, 26, 16, 24)], -1),
    # phase 2c: the right bank's near pair -- a big upright crystal leaning outward (+x) and a second one behind it,
    # small ones at their base (the reference's lower-right pair)
    # phase 2h: the base shards lean at most ~20 deg (a 44 deg one bent out sideways: the owner's "boot")
    "pairR": (29, [(0, 0, 5.2, 44, 12, 18), (-9, 14, 4.4, 36, 16, 24), (-6, -4, 2.4, 13, 12, 20),
                   (9, -3, 2.0, 10, 10, 18)], 1),

    # phase 2g (the owner: "vary the foreground crystals a bit more on the right hand side"): new silhouettes --
    # a single tall needle, a stub of broken columns, a hard-leaning pair, a right-hand fan. A 7th value of 1 in a
    # shard row makes it a broken stub.
    "needle": (53, [(0, 0, 3.0, 62, 2, 6), (4, 3, 1.8, 14, 20, 30)], 1),
    "stub": (59, [(0, 0, 5.6, 16, 4, 12, 1), (7, 4, 4.4, 11, 14, 24, 1), (-6, 3, 3.8, 9, 10, 22, 1),
                  (3, -6, 2.6, 18, 24, 34), (-4, -5, 2.0, 10, 30, 42)], 1),
    "leanPair": (61, [(0, 0, 4.6, 46, 30, 36), (7, 5, 3.6, 34, 38, 46), (-4, -4, 2.0, 12, 16, 26)], 1),
    "fanR": (67, [(0, 0, 3.8, 48, 8, 14), (7, 4, 3.4, 40, 18, 26), (-5, 6, 3.0, 34, 4, 10),
                  (11, -3, 2.6, 24, 30, 42), (-9, -4, 2.4, 18, 14, 24), (4, -8, 2.2, 14, 22, 34)], 1),
    # phase 2i (the owner: "it's not reading at distinct crystals in a group"): separate columns of different heights,
    # spaced so each pointed termination and silhouette stands apart (gaps of a column's width or more), each tilted
    # its own way
    "groupR": (73, [(0, 0, 3.2, 50, 3, 7), (14, 7, 2.6, 36, 16, 22), (-13, 5, 2.4, 30, 12, 18)], 1),
    # phase 2g: the left bank's dominant mass -- a big full cluster to stand on the wall
    "massL": (71, [(0, 0, 5.0, 58, 6, 12), (-8, 5, 4.4, 50, 14, 22), (7, 4, 4.0, 42, 4, 10), (-14, -2, 3.6, 36, 22, 30),
                   (12, -4, 3.2, 30, 16, 26), (-4, -9, 3.0, 24, 26, 38), (16, 6, 2.6, 20, 30, 44),
                   (-18, 8, 2.4, 16, 32, 46), (5, 11, 2.2, 14, 10, 24)], -1),
    # name: (seed, [(dx, dy, width, length, lean_lo, lean_hi)], outward +x)
    "cluster": (11, [(0, 0, 4.6, 40, 4, 10), (5, 3, 3.6, 30, 14, 24), (-4, 2, 3.2, 26, 8, 18), (3, -5, 3.0, 22, 20, 34),
                     (-6, -4, 2.6, 17, 24, 38), (8, -2, 2.4, 14, 30, 45), (-2, 6, 2.2, 12, 18, 30)], 1),
    "pair": (23, [(0, 0, 6.4, 28, 26, 34), (8, 3, 5.6, 24, 34, 42), (-5, 4, 3.0, 13, 14, 24), (10, -4, 2.6, 10, 40, 52)], 1),
    "spire": (31, [(0, 0, 3.8, 30, 4, 10), (5, 2, 2.8, 19, 14, 22)], 1),
    "spire2": (37, [(0, 0, 4.2, 34, 6, 12), (-5, 3, 2.6, 15, 18, 28)], 1),
    "small": (41, [(0, 0, 2.6, 16, 6, 14), (4, 3, 2.2, 12, 14, 24), (-3, 3, 1.8, 9, 18, 30)], 1),
    "small2": (43, [(0, 0, 2.8, 18, 6, 14), (-4, 2, 2.0, 11, 16, 26)], 1),
    "far": (47, [(0, 0, 4.0, 28, 2, 8), (9, 4, 3.4, 22, 8, 14), (-8, 2, 3.0, 18, 6, 14), (16, -3, 2.6, 15, 12, 20)], 1),
}


def family_view(name):
    """The direction to the camera in a formation's own frame (Blender axes). `name` is a formation (5e) or a family
    (the first formation that uses it)."""
    for f in L.FORMATIONS:
        if f[0] == name or f[5] == name:
            q = L.formation_quat(f)
            x, z = f[1], f[2]
            d = [L.CAMERA_POS[0] - x, L.CAMERA_POS[1] - L.height(x, z), L.CAMERA_POS[2] - z]
            qi = (-q[0], -q[1], -q[2], q[3])
            lv = L._qrot(qi, d)
            return Vector((lv[0], -lv[2], lv[1])).normalized()
    return None


def edges_for_formations(out, min_px=1.6, rr0=0.11):
    """5e: one edge mesh per formation (<formation>_edgeA.glb), its prisms widened so they are never under `min_px`
    pixels at the formation's distance from the camera at 1080p; the widening goes to edges.json (the scene dims the
    glow by it)."""
    for f in L.FORMATIONS:
        name, x, z, _yaw, s, fam = f[:6]
        d = math.dist(L.CAMERA_POS, (x, L.height(x, z), z))
        px = 2.0 * d * math.tan(math.radians(L.CAMERA_FOV) / 2) / 1080.0
        rr = max(rr0, 0.5 * min_px * px / s)
        EDGE_WIDEN[name] = round(rr / rr0, 3)
        bm, seed = FAMILY_BM[fam]
        build_edges(bm, name, out, random.Random(seed + 1000), rr=rr)
        build_stars(bm, name, out, random.Random(seed + 2000), size=max(0.07, 0.5 * 1.6 * px / s))
    with open(os.path.join(out, "edges.json"), "w") as fh:
        json.dump(EDGE_WIDEN, fh)


def build_stars(bm, name, out, rng, size, per_m2=0.35):
    """5f (the owner: "we loose all the speckles inside the crystals thats not acceptable"): the speckles as geometry --
    small octahedra just under the faces the camera sees, never under ~1.6 px (`size` comes from the formation's
    distance), so they read as stars without the stair-stepping of the old hashed cells. <name>_stars.glb."""
    view = family_view(name)
    tb = bmesh.new()
    n = 0
    for f in bm.faces:
        if view is not None and f.normal.dot(view) < 0.05:
            continue
        k = f.calc_area() * per_m2
        cnt = int(k) + (1 if rng.random() < k - int(k) else 0)
        vs = [v.co for v in f.verts]
        for _ in range(cnt):
            w = [rng.random() for _ in vs]
            t = sum(w)
            pnt = sum((v * (wi / t) for v, wi in zip(vs, w)), Vector((0, 0, 0))) - f.normal * size * 0.6
            r = size * rng.uniform(0.6, 1.3)
            pts = [tb.verts.new(pnt + Vector(d) * r) for d in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0),
                                                                 (0, 0, 1), (0, 0, -1))]
            for a_, b_, c_ in ((0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)):
                tb.faces.new((pts[a_], pts[b_], pts[c_]))
            n += 1
    me = bpy.data.meshes.new(name + "_stars")
    tb.to_mesh(me)
    tb.free()
    ob = bpy.data.objects.new(name + "_stars", me)
    bpy.context.scene.collection.objects.link(ob)
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + "_stars.glb"), export_format="GLB",
                              use_selection=True, export_yup=True, export_apply=True, export_normals=True,
                              export_materials="NONE")
    ob.hide_render = True
    print("EXPORTED", name + "_stars", n, "stars")


def build_edges(bm, name, out, rng, rr=0.11, sharp=18.0, ridges=0.15):
    """Phase 3 (spec 5.2, glowing edges), 3c selective: thin prisms along the SILHOUETTE edges as the camera sees the
    formation (one face toward the camera, the other away) and about 15% of the other sharp ridges, each tapered toward
    its ends (thinnest a third of the middle) so its strength varies along its length. Every other facet boundary
    reads by value contrast alone. Written to <name>_edgeA.glb. Geometry, not a shader: the renderer has no per-pixel
    edge distance."""
    tb = bmesh.new()
    ci = bm.verts.layers.int.get("ci")
    view = family_view(name)
    for e in bm.edges:
        if len(e.link_faces) != 2:
            continue
        n0, n1 = e.link_faces[0].normal, e.link_faces[1].normal
        if n0.length == 0 or n1.length == 0 or math.degrees(n0.angle(n1)) < sharp:
            continue
        k0, k1 = (e.verts[0][ci], e.verts[1][ci]) if ci is not None else (0, 0)
        if k0 and k0 == k1 and k0 % 2 == 0:
            continue
        if view is not None:
            d0, d1 = n0.dot(view), n1.dot(view)
            if d0 < 0.02 and d1 < 0.02:
                continue
            silhouette = (d0 > 0.02) != (d1 > 0.02)
            if not silhouette and rng.random() > ridges:
                continue
        a, b = e.verts[0].co.copy(), e.verts[1].co.copy()
        d = b - a
        if d.length < 1e-4:
            continue
        lift = (n0 + n1).normalized() * rr
        ref = Vector((0, 0, 1)) if abs(d.normalized().z) < 0.9 else Vector((1, 0, 0))
        p = d.cross(ref).normalized()
        q = d.normalized().cross(p)
        prof = [0.3, 1.0, 1.0, 0.3]                      # tapered: thin, full, full, thin
        rings = []
        for k, sc in enumerate(prof):
            c = a + d * (k / (len(prof) - 1)) + lift
            rings.append([tb.verts.new(c + (p * math.cos(math.tau * j / 3) + q * math.sin(math.tau * j / 3)) * rr * sc)
                          for j in range(3)])
        for r0, r1 in zip(rings, rings[1:]):
            for j in range(3):
                jj = (j + 1) % 3
                tb.faces.new((r0[j], r0[jj], r1[jj], r1[j]))
    bmesh.ops.recalc_face_normals(tb, faces=tb.faces)
    me = bpy.data.meshes.new(name + "_edgeA")
    tb.to_mesh(me)
    tb.free()
    ob = bpy.data.objects.new(name + "_edgeA", me)
    bpy.context.scene.collection.objects.link(ob)
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + "_edgeA.glb"), export_format="GLB",
                              use_selection=True, export_yup=True, export_apply=True, export_normals=True,
                              export_materials="NONE")
    ob.hide_render = True
    print("EXPORTED", name + "_edgeA", len(me.polygons), "faces")


def build_family(name, out):
    seed, spec, outward = FAMILIES[name]
    rng = random.Random(seed)
    bm = bmesh.new()
    for row in spec:
        dx, dy, w, ln, lo, hi = row[:6]
        shard._apex = None
        ax = lean(rng, outward, lo * 0.8, hi * 0.8)
        shard(bm, (dx, dy, 0.0), ax, w * 1.6, ln, rng, broken=len(row) > 6 and row[6] == 1)
        # phase 3 (spec 5.1, asymmetric branching): a long shard grows a side crystal from a third to half its height,
        # leaning 16-26 deg further out than its parent, on one side
        # (a strongly leaning parent grows none: its branch would stick out sideways, the owner's "boot")
        if ln >= 30 and hi <= 20 and rng.random() < 0.65:
            pa = Vector(ax).normalized()
            pu, pv, _ = basis(pa)
            ang = rng.uniform(0, math.tau)
            rad = (pu * math.cos(ang) + pv * math.sin(ang)).normalized()
            h = ln * rng.uniform(0.3, 0.5)
            b0 = Vector((dx, dy, 0.0)) + pa * h + rad * (w * 1.6 * 0.55)
            bax = (pa + rad * math.tan(math.radians(rng.uniform(16, 26)))).normalized()
            shard._apex = None
            shard(bm, b0, bax, w * 1.6 * rng.uniform(0.38, 0.5), ln * rng.uniform(0.32, 0.45), rng, embed=0.25)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    FAMILY_BM[name] = (bm.copy(), seed)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    m = bpy.data.materials.get("xtal") or bpy.data.materials.new("xtal")
    m.diffuse_color = (0.75, 0.6, 0.95, 1)
    me.materials.append(m)
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + ".glb"), export_format="GLB", use_selection=True,
                              export_yup=True, export_apply=True, export_normals=True)
    print("EXPORTED", name, len(me.polygons), "faces")
    # the footprint for placement and the river clearance check (layout.place / check_crystals): every vertex in AV
    # Gen's axes (Blender x, z, -y)
    FOOTPRINTS[name] = [[round(v.co.x, 3), round(v.co.z, 3), round(-v.co.y, 3)] for v in me.vertices]
    return ob


FOOTPRINTS = {}
FAMILY_BM = {}
EDGE_WIDEN = {}


# ---- phase 2e: Poly Haven CC0 scans for the rock (assets/aurora-valley.manifest.json) -------------------------
# Read from the asset library (tools/fetch_polyhaven.py), never modified there. Each is joined, decimated to a
# budget, rested on z = 0 and centred, and exported as geometry only: the scene shades it with the ground's own
# program (rock by slope, moss on what faces up), so scan and terrain read as one material.
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SCANS = {  # out name: (library path, target faces)
    "cliffA": ("terrain/formations/coastal_cliff_02/coastal_cliff_02_1k.gltf", 4000),   # phase 2f perf: 9k -> 4k
    "cliffB": ("terrain/formations/namaqualand_cliff_02/namaqualand_cliff_02_1k.gltf", 4000),
    "cliffC": ("terrain/formations/rock_face_01/rock_face_01_1k.gltf", 3000),
    "cliffD": ("terrain/formations/coastal_cliff_04/coastal_cliff_04_1k.gltf", 4000),
    "boulder": ("terrain/rocks/boulder_01/boulder_01_1k.gltf", 900),
    "rock": ("terrain/rocks/rock_07/rock_07_1k.gltf", 700),
    "mossrock": ("terrain/rocks/rock_moss_set_01/rock_moss_set_01_1k.gltf", 1400),
    "mossrock2": ("terrain/rocks/rock_moss_set_02/rock_moss_set_02_1k.gltf", 1400),   # 5e: moss-covered rock
    # 5f (the owner: "the bushes do not look good please use a more realistic texture"): Poly Haven CC0 plants with
    # modelled leaves and their own textures (not decimated: the leaves are the detail)
    "shrubA": ("nature/plants/shrub_02/shrub_02_1k.gltf", 100000),
    "fernA": ("nature/plants/fern_02/fern_02_1k.gltf", 100000),
    "shrubFar": ("nature/plants/shrub_02/shrub_02_1k.gltf", 5000),   # 5f: the same shrub, lighter, past ~250 m
    "stone": ("terrain/rocks/stone_01/stone_01_1k.gltf", 500),
}
SCAN_SIZES = {}

def build_scan(name, out):
    rel, faces = SCANS[name]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(REPO, "assets", rel))
    objs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for _ in range(4):   # a scan with seams can stop short of the ratio: decimate again until within budget
        n = len(ob.data.polygons)
        if n <= faces * 1.2:
            break
        mod = ob.modifiers.new("dec", "DECIMATE")
        mod.ratio = faces / n
        mod.use_collapse_triangulate = True
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.remove_doubles(threshold=0.002)
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.object.modifier_apply(modifier=mod.name)
    me = ob.data
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    off = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, min(zs)))
    for v in me.vertices:
        v.co -= off
    # phase 3c+ (17: textures on the rock): the scan keeps its own Poly Haven CC0 PBR textures (1k albedo, normal,
    # roughness), embedded in the GLB; the scene tints them rather than shading them with the ground program
    for p in me.polygons:
        p.use_smooth = True
    ob.name = name
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + ".glb"), export_format="GLB", use_selection=True,
                              export_yup=True, export_apply=True, export_normals=True, export_materials="EXPORT",
                              export_image_format="JPEG")
    # AV Gen extents (x, y up, z)
    SCAN_SIZES[name] = [round(max(xs) - min(xs), 3), round(max(zs) - min(zs), 3), round(max(ys) - min(ys), 3)]
    print("EXPORTED", name, len(me.polygons), "faces", SCAN_SIZES[name])
    return ob


# ---- the far mountains (phase 2b): three jagged ridgelines receding into the fog, built in place -----------------
RIDGES = [  # (name, distance m, half-span m, peak height m, centre dip (0..1), roughness, seed, dip x m, left lift)
    # phase 2e (the owner: "too symmetrical"): each ridge's saddle sits off-centre, at its own x, and each rises
    # toward one side by its own amount (the left, rocky side highest on the near ridge)
    # phase 2f: pushed back (the river now winds on to z = -1760), heights scaled to keep their angles; the near
    # saddle sits over the distant river
    ("ridgeNear", 1850.0, 2600.0, 470.0, 0.75, 0.70, 3, 60.0, 0.45),
    ("ridgeMid", 2350.0, 3000.0, 600.0, 0.45, 0.66, 5, -420.0, -0.25),
    ("ridgeFar", 2850.0, 3600.0, 700.0, 0.2, 0.62, 9, 600.0, 0.2),
]


def ridge_profile(n, rough, rng):
    """Midpoint displacement: a jagged skyline, 2^n + 1 samples in 0..1."""
    m = 2 ** n + 1
    h = [0.0] * m
    h[0], h[-1] = rng.uniform(0.3, 0.7), rng.uniform(0.3, 0.7)
    step, amp = m - 1, 0.6
    while step > 1:
        half = step // 2
        for i in range(half, m - 1, step):
            h[i] = 0.5 * (h[i - half] + h[i + half]) + rng.uniform(-amp, amp)
        step, amp = half, amp * rough
    lo, hi = min(h), max(h)
    return [(v - lo) / (hi - lo) for v in h]


def build_ridges(out):
    """Each ridge: a solid wall (front and back faces, 120 m deep) whose top is a jagged skyline; AV Gen coordinates
    are converted to Blender's on export. The near ridge dips in the middle (the reference's V), opening the
    horizon glow above the river's vanishing point."""
    for name, dist, half, peak, dip, rough, seed, dipx, lift in RIDGES:
        rng = random.Random(seed)
        prof = ridge_profile(9, rough, rng)
        bm = bmesh.new()
        n = len(prof)
        front, back, base_f, base_b = [], [], [], []
        for i, v in enumerate(prof):
            u = i / (n - 1)
            x = -half + 2 * half * u
            z = -dist - 0.18 * dist * (2 * u - 1) ** 2          # the ends curve toward the camera, framing
            centre = math.exp(-(((x - dipx) / (half * 0.28)) ** 2))   # the V, off-centre
            y = peak * (0.12 + 0.88 * v ** 1.6) * (1.0 - dip * centre) * (1.0 - lift * x / half)
            # 5f (the owner: "roll them off where the red line suggests ... get rid of the highlighted portion thats
            # jutting out"): right of the river every far ridge descends from ~45% of the half-view at its distance
            # to near nothing at the frame's right edge -- a diagonal roll-off, no peaks behind the right hero
            ro = min(max((x / dist - 0.42) / (0.88 - 0.42), 0.0), 1.0)
            y *= 1.0 - 0.88 * ro * ro * (3.0 - 2.0 * ro)
            front.append(bm.verts.new(av2bl((x, y, z))))
            back.append(bm.verts.new(av2bl((x, y * 0.95, z - 120.0))))
            base_f.append(bm.verts.new(av2bl((x, -80.0, z))))
            base_b.append(bm.verts.new(av2bl((x, -80.0, z - 120.0))))
        for i in range(n - 1):
            bm.faces.new((base_f[i], base_f[i + 1], front[i + 1], front[i]))
            bm.faces.new((front[i], front[i + 1], back[i + 1], back[i]))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        for p in me.polygons:
            p.use_smooth = False
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        for o in bpy.context.selected_objects:
            o.select_set(False)
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + ".glb"), export_format="GLB",
                                  use_selection=True, export_yup=True, export_apply=True, export_normals=True)
        print("EXPORTED", name, len(me.polygons), "faces")


def av2bl(v):
    return Vector((v[0], -v[2], v[1]))


def preview(objs, pdir):
    os.makedirs(pdir, exist_ok=True)
    sc = bpy.context.scene
    # the valley as a height field, coarse far away (the camera's view only)
    bm = bmesh.new()
    xs = [x for x in range(-800, 801, 10)]
    zs = list(range(260, -3000, -12))
    grid = [[bm.verts.new(av2bl((x, L.height(x, z), z))) for x in xs] for z in zs]
    for i in range(len(zs) - 1):
        for j in range(len(xs) - 1):
            bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
    me = bpy.data.meshes.new("valley")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("valley", me)
    sc.collection.objects.link(ob)
    m = bpy.data.materials.new("moss")
    m.diffuse_color = (0.05, 0.12, 0.06, 1)
    me.materials.append(m)
    # the water: a sheet at each river point's level, a coarse stand-in
    bm = bmesh.new()
    for rs, rr in L._RIVERS_S:
      for a, b in zip(rs, rs[1:]):
        d = Vector((b[0] - a[0], 0, b[2] - a[2])).normalized()
        n = Vector((-d.z, 0, d.x)) * rr["width"] * 0.5
        q = [bm.verts.new(av2bl((p[0] + s * n.x, p[1] + 0.05, p[2] + s * n.z))) for p, s in ((a, -1), (a, 1), (b, 1), (b, -1))]
        bm.faces.new(q)
    me = bpy.data.meshes.new("water")
    bm.to_mesh(me)
    bm.free()
    wo = bpy.data.objects.new("water", me)
    sc.collection.objects.link(wo)
    wm = bpy.data.materials.new("water")
    wm.diffuse_color = (0.55, 0.6, 0.8, 1)
    me.materials.append(wm)
    fam = {o.name: o for o in objs}
    conv = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0))).to_4x4()
    for ff in L.FORMATIONS:   # NB the valley here is features only (no noise): a composition check
        name, x, z, yaw, s, f = ff[:6]
        src = fam[f]
        o = src.copy()
        sc.collection.objects.link(o)
        q = L.formation_quat(ff)
        mw = Matrix.Translation((x, L.height(x, z) - 0.8 * s, z)) @ Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4() \
            @ Matrix.Diagonal((s, s, s, 1))
        o.matrix_world = conv @ mw @ conv.inverted()
    for o in objs:
        o.hide_render = True
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.background_type = "VIEWPORT"
    sc.display.shading.background_color = (0.09, 0.1, 0.18)
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    cam = bpy.data.cameras.new("cam")
    cam.sensor_fit = "VERTICAL"
    cam.angle_y = math.radians(L.CAMERA_FOV)
    cam.clip_end = 5000
    co = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    eye, tgt = av2bl(L.CAMERA_POS), av2bl(L.CAMERA_TARGET)
    co.location = eye
    co.rotation_euler = (tgt - eye).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = os.path.join(pdir, "camera.png")
    bpy.ops.render.render(write_still=True)


def main():
    out, pdir = argv()
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = [build_family(n, out) for n in FAMILIES]
    with open(os.path.join(out, "footprints.json"), "w") as f:
        json.dump(FOOTPRINTS, f)
    edges_for_formations(out)
    for n in SCANS:
        build_scan(n, out).hide_render = True
    with open(os.path.join(out, "scans.json"), "w") as f:
        json.dump(SCAN_SIZES, f)
    build_ridges(out)
    if pdir:
        preview(objs, pdir)


main()
