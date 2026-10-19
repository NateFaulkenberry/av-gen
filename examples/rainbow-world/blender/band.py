"""Rainbow World, Stage 2 prototype: one section of the knot band (about 420 m of it), with its rims.

The band is a closed cross-section swept along a centreline with a rotation-minimising frame and an authored
twist, so the section already shows gravity turning. Both broad faces are meadow; the rims are landforms: a
grassy crest that rolls over into a terraced cliff and back into the other face. Every form is authored -- a
few designed hills, a dell, a rolling swell, a ridge spine, terraces -- and no noise makes shape.

Outputs (args: <outdir> [w h samples]):
  <outdir>/band_*.png               the review views
  <outdir>/band_section.glb         the section mesh (Y-up), UV = (perimeter fraction, s / L)
  <outdir>/band_section.blend       the milestone file
Run: Blender -b --factory-startup -P band.py -- <outdir> [w h samples]
"""
import math
import os
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw_common as rw  # noqa: E402

L = 420.0          # section length (m)
HALF_W = 130.0     # half width of the band
FACE = 20.0        # half thickness at the faces (the meadow sits at +/- FACE)
TWIST = math.radians(55.0)
NS = 300           # rings along s
NP = 560           # samples round the cross-section


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ---- the centreline and its frames ---------------------------------------------------------------------
def centre(s):
    u = s / L
    return Vector((28.0 * math.sin(2 * math.pi * u * 0.7), s, 18.0 * math.sin(math.pi * u)))


KNOT = None        # a knot_path.Path when the band is the closed knot (configure_knot); None = the 420 m section


def frames():
    out = []
    if KNOT is not None:
        for i in range(NS + 1):
            s = L * i / NS
            p, t, n, b = KNOT.at(s)
            t, u = Vector(t), Vector(n)
            out.append((s, Vector(p), t, u, t.cross(u).normalized()))
        return out
    up = Vector((0, 0, 1))
    for i in range(NS + 1):
        s = L * i / NS
        p = centre(s)
        t = (centre(min(L, s + 0.5)) - centre(max(0.0, s - 0.5))).normalized()
        up = (up - t * up.dot(t)).normalized()
        tw = TWIST * smoothstep(0.0, 1.0, s / L)
        u = (Matrix.Rotation(tw, 3, t) @ up).normalized()
        side = t.cross(u).normalized()
        out.append((s, p, t, u, side))
    return out


# ---- the cross-section (x along the side, y along the band's up) --------------------------------------
def profile_points():
    """Closed control polygon, clockwise from the top face's centre. The right rim is a high crest
    over a tall terraced cliff; the left rim is lower, with a broad ledge (asymmetry is deliberate)."""
    W = HALF_W
    # v2 (Stage 3): a crisper break at both cliff tops -- the meadow runs nearly flat to the edge and turns down
    # over a short shoulder, instead of v1's rounded bulge that read as a lip of slime over the rocks.
    top = [(0, FACE), (60, FACE), (100, FACE + 1.0), (114, FACE + 1.6)]
    right = [(121, FACE + 1.2), (125.5, FACE - 1.5), (128.5, FACE - 6.0), (130.5, 0.0), (129.0, -FACE + 6.0),
             (125.5, -FACE + 1.5), (120, -FACE - 1.4)]
    bottom = [(100, -FACE - 1.0), (40, -FACE), (-40, -FACE), (-100, -FACE - 1.0)]
    left = [(-117, -FACE - 1.2), (-123, -FACE + 2.0), (-127, -8.0), (-134, -5.5), (-136.5, 1.0),
            (-131, FACE - 5.0), (-124.5, FACE - 1.0), (-115, FACE + 1.4)]
    ret = top + right + bottom + left + [(-60, FACE)]
    return [(x * W / 130.0, y) for x, y in ret]


def catmull(pts, per=24):
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2, p3 = (Vector(pts[(i + k) % n]) for k in (-1, 0, 1, 2))
        for k in range(per):
            t = k / per
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                              (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    return out


def resample(poly, n):
    seg = [(poly[(i + 1) % len(poly)] - poly[i]).length for i in range(len(poly))]
    total = sum(seg)
    out, acc, j = [], 0.0, 0
    for k in range(n):
        target = total * k / n
        while acc + seg[j] < target:
            acc += seg[j]
            j += 1
        f = (target - acc) / max(seg[j], 1e-9)
        out.append(poly[j].lerp(poly[(j + 1) % len(poly)], f))
    return out, total


PROFILE, PERIM = resample(catmull(profile_points()), NP)


def profile_normal(k):
    a, b = PROFILE[(k - 1) % NP], PROFILE[(k + 1) % NP]
    t = (b - a).normalized()
    return Vector((-t.y, t.x))  # the polygon runs clockwise (top face toward +x first) -> outward normal


# ---- authored surface features --------------------------------------------------------------------------
def hill(s, x, s0, x0, h, rs, rx, skew=1.0, rot=0.0):
    ds, dx = s - s0, x - x0
    if KNOT is not None:  # the closed loop: the shorter way round
        ds = (ds + 0.5 * L) % L - 0.5 * L
    c, sn = math.cos(rot), math.sin(rot)
    a, b = c * ds - sn * dx, sn * ds + c * dx
    ra = rs * (skew if a > 0 else 1.0 / skew)
    return h * math.exp(-(a * a) / (2 * ra * ra) - (b * b) / (2 * rx * rx))


TOP_HILLS = [  # s0, x0, height, radius along, radius across, skew, rotation (rad)
    (105, -42, 21, 42, 34, 1.5, 0.4), (250, 38, 15, 58, 40, 0.7, -0.3), (365, -62, 11, 30, 26, 1.3, 0.9),
    (190, 12, -6.5, 38, 30, 1.0, 0.0), (40, 60, 5, 22, 18, 1.0, 0.0), (300, -10, 4, 18, 22, 1.0, 0.0),
    (150, 70, 6, 26, 20, 1.2, 0.2), (330, 75, 3.5, 14, 14, 1.0, 0.0)]
BOTTOM_HILLS = [
    (80, 30, 16, 50, 36, 1.4, -0.5), (220, -50, 22, 40, 34, 0.8, 0.3), (340, 40, 9, 30, 30, 1.0, 0.0),
    (160, 60, -5, 30, 26, 1.0, 0.0), (280, 0, 5, 24, 30, 1.0, 0.6)]


_WINDOWS = {}


def _hill_window(hills, s, reach=320.0):
    """The hills within `reach` of s along the loop (hills are sorted by s0 once)."""
    key = id(hills)
    if key not in _WINDOWS:
        _WINDOWS[key] = sorted(hills, key=lambda h: h[0])
    hs = _WINDOWS[key]
    return [h for h in hs if abs((s - h[0] + 0.5 * L) % L - 0.5 * L) < reach]


SECTIONS = {  # authored terrain movements (s0 relative to the section, x0, height, rs, rx, skew, rotation)
    "rolling": (380.0, [(90, 40, 5, 30, 26, 1.2, 0.3), (250, -50, 4, 26, 22, 1.0, 0.0), (320, 70, 3, 18, 16, 1.0, 0.0)]),
    "greathill": (420.0, [(120, -42, 22, 44, 34, 1.5, 0.4), (260, 20, -7, 40, 30, 1.0, 0.0), (340, 60, 6, 24, 20, 1.0, 0.0),
                          (40, 70, 4, 18, 16, 1.0, 0.0)]),
    "ridge": (400.0, [(200, 72, 14, 130, 18, 1.0, 0.0), (150, -60, 10, 34, 30, 1.3, -0.5), (330, -20, 5, 22, 22, 1.0, 0.0)]),
    "twins": (360.0, [(110, -30, 15, 38, 32, 0.8, 0.2), (220, 35, 12, 34, 30, 1.2, -0.3), (300, -70, 4, 18, 18, 1.0, 0.0)]),
}


def layout_hills(seed, total):
    """Lay the authored sections end to end round a loop of length `total`, mirrored at random."""
    import random as _r
    rng = _r.Random(seed)
    order = ["greathill", "rolling", "ridge", "twins"]
    out, s0 = [], 0.0
    while s0 < total - 200:
        name = order[rng.randrange(len(order))] if out else "rolling"
        length, hills = SECTIONS[name]
        mirror = -1.0 if rng.random() < 0.5 else 1.0
        scale = rng.uniform(0.85, 1.15)
        for hs, hx, h, rs, rx, sk, rot in hills:
            out.append((s0 + hs * scale, hx * mirror, h * rng.uniform(0.85, 1.15), rs * scale, rx, sk, rot * mirror))
        s0 += length * scale
    return out


def per(period):
    """On the closed knot a periodic term must close too: the nearest period that divides the loop's length."""
    if KNOT is None:
        return period
    return L / max(1, round(L / period))


def face_height(s, x, hills, phase):
    edge = 1.0 - smoothstep(0.80, 0.95, abs(x) / HALF_W)
    h = 4.5 * math.sin(2 * math.pi * s / per(150.0) + 0.013 * x + phase) * (0.6 + 0.4 * math.sin(2 * math.pi * s / per(330.0)))
    if KNOT is not None and len(hills) > 24:
        h += sum(hill(s, x, *p) for p in _hill_window(hills, s))
    else:
        h += sum(hill(s, x, *p) for p in hills)
    # a ridge spine running toward the right rim, rising and falling along the band
    h += 7.0 * math.exp(-((x - 0.73 * HALF_W) ** 2) / (2 * 14.0 ** 2)) * (0.55 + 0.45 * math.sin(2 * math.pi * s / per(210.0) + phase))
    return h * edge


# The rims' rock: chunky stylised blocks, not thin strata (v4's strata read as a layer cake). Columns of
# irregular width along the band, each split into three tiers; every block is a slightly tilted plane, and
# blocks meet in narrow bevels, so the cliff reads as big carved masses with clean light and shadow planes.
import random as _random


def _columns(seed):
    rng = _random.Random(seed)
    cols, s0 = [], -10.0
    while s0 < L + 20:
        w = rng.uniform(6.0, 17.0)
        tiers = [(rng.uniform(-0.5, 3.0), rng.uniform(-1.6, 1.6), rng.uniform(-1.2, 1.2)) for _ in range(3)]
        gully = rng.random() < 0.18
        cols.append((s0, s0 + w, tiers, rng.random(), gully))
        s0 += w
    return cols


COLS = {True: _columns(11), False: _columns(23)}


def configure_knot(path, ring_spacing=3.0, seed=1):
    """Make this module sweep the closed knot instead of the 420 m section: frames from `path` (knot_path.Path,
    the engine's own spline frames), the loop's length, rings every `ring_spacing` metres, authored terrain sections
    laid round the loop, and the rim columns re-cut for the loop's length."""
    global KNOT, L, NS, TOP_HILLS, BOTTOM_HILLS, COLS
    KNOT = path
    L = float(path.length)
    NS = int(round(L / ring_spacing))
    TOP_HILLS = layout_hills(seed, L)
    BOTTOM_HILLS = layout_hills(seed + 100, L)
    COLS = {True: _columns(11), False: _columns(23)}
    _WINDOWS.clear()
TIER_Y = (-7.0, 6.0)  # tier boundaries (m, before the per-column wobble)


def _block(s, y, right):
    cols = COLS[right]
    lo, hi = 0, len(cols) - 1
    while lo < hi:  # binary search for the column containing s
        mid = (lo + hi) // 2
        if cols[mid][1] < s:
            lo = mid + 1
        else:
            hi = mid
    return lo


def _block_depth(ci, s, y, right):
    s0, s1, tiers, h, gully = COLS[right][ci]
    wob = 2.5 * math.sin(h * 40.0)
    t = 0 if y < TIER_Y[0] + wob else (1 if y < TIER_Y[1] + wob else 2)
    base, tilt_y, tilt_s = tiers[t]
    c = 0.5 * (s0 + s1)
    d = base + tilt_y * (y / FACE) + tilt_s * ((s - c) / max(1.0, 0.5 * (s1 - s0)))
    if gully:
        d -= 3.5
    return d


def rim_offset(s, y, right):
    ph = 0.0 if right else 2.0
    wander = 6.0 * math.sin(2 * math.pi * s / per(97.0) + ph) + 3.5 * math.sin(2 * math.pi * s / per(41.0) + ph)
    ci = _block(s, y, right)
    s0, s1, *_ = COLS[right][ci]
    d = _block_depth(ci, s, y, right)
    # bevel toward the neighbouring column over 1.3 m at each side
    bev = 1.3
    if s - s0 < bev and ci > 0:
        d = d + (_block_depth(ci - 1, s, y, right) - d) * 0.5 * (1 - smoothstep(0, bev, s - s0))
    if s1 - s < bev and ci + 1 < len(COLS[right]):
        d = d + (_block_depth(ci + 1, s, y, right) - d) * 0.5 * (1 - smoothstep(0, bev, s1 - s))
    expose = smoothstep(FACE + 3, FACE - 4, y) * smoothstep(-FACE - 3, -FACE + 4, y)
    return wander + 0.3 * d * expose - 14.0 * expose


def ledge_moss(s, y, right):
    ci = _block(s, y, right)
    h = COLS[right][ci][3]
    wob = 2.5 * math.sin(h * 40.0)
    m = 0.0
    for ty in TIER_Y:
        m = max(m, smoothstep(1.6, 0.0, abs(y - (ty + wob))))
    return m


def rim_tone(s, y, right):
    ci = _block(s, y, right)
    return COLS[right][ci][3]


def build_band(name="band_section", rings=None):
    """The band mesh. `rings` = (first, last) ring indices for one chunk of the knot (inclusive); None = all."""
    fr = frames()
    i0, i1 = rings if rings is not None else (0, NS)
    verts, uvs, zones, moss = [], [], [], []
    for (s, p, t, u, side) in fr[i0:i1 + 1]:
        for k in range(NP):
            q = PROFILE[k]
            n2 = profile_normal(k)
            x, y = q.x, q.y
            # how much of a face this point is: |n.y| near 1 on the faces, near 0 on the cliffs
            faceness = smoothstep(0.75, 0.97, abs(n2.y)) * (1.0 - smoothstep(0.86, 0.98, abs(x) / HALF_W))
            d = 0.0
            if n2.y > 0:
                d += faceness * face_height(s, x, TOP_HILLS, 0.0)
            else:
                d += faceness * face_height(s, -x, BOTTOM_HILLS, 1.3)
            rimw = 1.0 - faceness
            d += rimw * rim_offset(s, y, x > 0) * (1.0 if abs(x) > 0.75 * HALF_W else 0.0)
            pos = p + side * (x + n2.x * d) + u * (y + n2.y * d)
            verts.append(pos)
            uvs.append((k / NP, s / L))
            cap = 1.0 - smoothstep(FACE - 4, FACE + 2, abs(y))   # grass caps roll over both cliff tops
            stonew = rimw * cap
            zones.append((faceness if n2.y > 0 else 0.0, stonew, faceness if n2.y <= 0 else 0.0,
                          rim_tone(s, y, x > 0)))
            moss.append((ledge_moss(s, y, x > 0) * stonew, 0.0, 0.0, 1.0))
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bv = [bm.verts.new(v) for v in verts]
    bm.verts.ensure_lookup_table()
    uvl = bm.loops.layers.uv.new("UVMap")
    col = bm.loops.layers.color.new("zone")
    col2 = bm.loops.layers.color.new("rim2")
    faces = []
    for i in range(i1 - i0):
        for k in range(NP):
            a = i * NP + k
            b = i * NP + (k + 1) % NP
            c = (i + 1) * NP + (k + 1) % NP
            dd = (i + 1) * NP + k
            f = bm.faces.new((bv[a], bv[b], bv[c], bv[dd]))
            for loop, vi in zip(f.loops, (a, b, c, dd)):
                uu = uvs[vi][0]
                if k == NP - 1 and vi in (b, c):
                    uu = 1.0
                loop[uvl].uv = (uu, uvs[vi][1])
                z = zones[vi]
                loop[col] = (z[0], z[1], z[2], z[3])
                loop[col2] = moss[vi]
            faces.append(f)
    # end caps (a prototype section is open at its two ends; cap them so it renders as a solid). The knot is
    # closed: its chunks meet ring to ring, so they get none.
    if KNOT is None:
        last = i1 - i0
        for ring in (0, last):
            cap = bm.faces.new([bv[ring * NP + k] for k in (reversed(range(NP)) if ring == last else range(NP))])
            cap.smooth = False
    bm.to_mesh(me)
    bm.free()
    for poly in me.polygons:
        poly.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob, fr


def ground_material():
    m = bpy.data.materials.new("rw_ground")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = 0.92
    bsdf.inputs["Specular IOR Level"].default_value = 0.2
    attr = nt.nodes.new("ShaderNodeAttribute")
    attr.attribute_name = "zone"
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(attr.outputs["Color"], sep.inputs[0])
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 18.0
    ao.samples = 8
    tc = nt.nodes.new("ShaderNodeTexCoord")
    # broad colour patches (designed scale: 60-90 m), not shape
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 0.012
    nz.inputs["Detail"].default_value = 2.0
    nz.inputs["Roughness"].default_value = 0.45
    nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
    nz2 = nt.nodes.new("ShaderNodeTexNoise")
    nz2.inputs["Scale"].default_value = 0.09
    nz2.inputs["Detail"].default_value = 3.0
    nt.links.new(tc.outputs["Object"], nz2.inputs["Vector"])
    patch = rw.ramp(nt, [(0.30, rw.P["grass_deep"]), (0.48, rw.P["meadow"]), (0.62, rw.hexlin("#79AE34")),
                         (0.74, rw.P["olive"])])
    nt.links.new(nz.outputs["Fac"], patch.inputs[0])
    # convex crests lighter and warmer, concavities into teal shade (curvature from pointiness, cavity from AO)
    pt = rw.math_node(nt, "MULTIPLY_ADD", geo.outputs["Pointiness"], 6.0)
    pt.node.inputs[2].default_value = -2.6
    pt = rw.math_node(nt, "MAXIMUM", pt, 0.0)
    pt = rw.math_node(nt, "MINIMUM", pt, 1.0)
    meadow = rw.mix_rgb(nt, rw.math_node(nt, "MULTIPLY", pt, 0.3), patch.outputs[0], rw.P["meadow_sun"])
    cav = rw.math_node(nt, "SUBTRACT", 1.0, ao.outputs["AO"])
    cav = rw.math_node(nt, "MULTIPLY", cav, 1.4, clamp=True)
    meadow = rw.mix_rgb(nt, cav, meadow, rw.P["shadow_green"])
    fine = rw.math_node(nt, "MULTIPLY_ADD", nz2.outputs["Fac"], 0.25)
    fine.node.inputs[2].default_value = 0.87
    meadow = rw.mix_rgb(nt, 1.0, meadow, fine, "MULTIPLY")
    # stone for the cliffs: warm in light, cool in cavities, terrace lines from the object's Z
    tone = rw.ramp(nt, [(0.0, rw.hexlin("#B08E6E")), (0.35, rw.hexlin("#A88A72")), (0.6, rw.hexlin("#8E8A8E")),
                        (1.0, rw.hexlin("#9A8268"))])
    nt.links.new(attr.outputs["Alpha"], tone.inputs[0])
    stone = rw.mix_rgb(nt, rw.math_node(nt, "MULTIPLY", nz2.outputs["Fac"], 0.35), tone.outputs[0], rw.P["stone_cool"])
    stone = rw.mix_rgb(nt, cav, stone, rw.P["stone_shadow"])
    # ledges on the cliff that face 'out and up' carry moss: approximate with low pointiness + AO open
    moss_mask = rw.math_node(nt, "MULTIPLY", ao.outputs["AO"], 1.0)
    moss_mask = rw.math_node(nt, "SUBTRACT", moss_mask, 0.55)
    moss_mask = rw.math_node(nt, "MULTIPLY", moss_mask, 3.0, clamp=True)
    stone = rw.mix_rgb(nt, rw.math_node(nt, "MULTIPLY", moss_mask, 0.25), stone, rw.P["olive"])
    a2 = nt.nodes.new("ShaderNodeAttribute")
    a2.attribute_name = "rim2"
    s2 = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(a2.outputs["Color"], s2.inputs[0])
    stone = rw.mix_rgb(nt, rw.math_node(nt, "MULTIPLY", s2.outputs[0], 1.6, clamp=True), stone, rw.P["meadow"])
    rimw = rw.math_node(nt, "POWER", sep.outputs[1], 1.2)
    stone = rw.mix_rgb(nt, 0.55, stone, rw.P["stone_shadow"])  # the rim core is the crevice between rocks
    col = rw.mix_rgb(nt, rimw, meadow, stone)
    nt.links.new(col, bsdf.inputs["Base Color"])
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    return m


def grass_clump(name="grass_clump", blades=22, seed=3, segs=4):
    """A tuft of tapered, curved blades whose normals are bent to the tuft's dome (the painterly-shading
    trick of 02-research.md A/F), so a field of them shades as soft rolling volume."""
    import random
    rng = random.Random(seed)
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    normals = []
    for b in range(blades):
        a = 2 * math.pi * b / blades + rng.uniform(-0.3, 0.3)
        r0 = rng.uniform(0.02, 0.16)
        hgt = rng.uniform(0.25, 0.55)
        lean = rng.uniform(0.08, 0.28)
        wid = rng.uniform(0.03, 0.05)
        base = Vector((math.cos(a) * r0, math.sin(a) * r0, 0))
        out = Vector((math.cos(a), math.sin(a), 0))
        side = Vector((-math.sin(a), math.cos(a), 0))
        prev = None
        for k in range(segs + 1):
            t = k / segs
            c = base + out * (lean * hgt * t * t) + Vector((0, 0, hgt * t))
            w = wid * (1 - 0.65 * t * t)
            v1 = bm.verts.new(c - side * w)
            v2 = bm.verts.new(c + side * w)
            for v in (v1, v2):
                dome = (v.co - Vector((0, 0, -0.35))).normalized()
                normals.append((v, dome))
            if prev:
                f = bm.faces.new((prev[0], prev[1], v2, v1))
                for loop, (uu, vv) in zip(f.loops, ((0, (k - 1) / segs), (1, (k - 1) / segs), (1, t), (0, t))):
                    loop[uvl].uv = (uu, vv)
            prev = (v1, v2)
    bm.to_mesh(me)
    nmap = {v.index: n for v, n in normals}
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    me.normals_split_custom_set_from_vertices([nmap[i] for i in range(len(me.vertices))])
    ob = bpy.data.objects.new(name, me)
    m = bpy.data.materials.new("rw_grass")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.95
    bsdf.inputs["Specular IOR Level"].default_value = 0.12
    uvn = nt.nodes.new("ShaderNodeUVMap")
    sp = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(uvn.outputs[0], sp.inputs[0])
    gr = rw.ramp(nt, [(0.0, rw.P["grass_deep"]), (0.45, rw.P["meadow"]), (1.0, rw.hexlin("#8CC63F"))])
    nt.links.new(sp.outputs[1], gr.inputs[0])
    nt.links.new(gr.outputs[0], bsdf.inputs["Base Color"])
    # a little light through the blades
    nt.links.new(gr.outputs[0], bsdf.inputs["Subsurface Weight"] if False else bsdf.inputs["Base Color"])
    try:
        bsdf.inputs["Transmission Weight"].default_value = 0.0
        bsdf.inputs["Sheen Weight"].default_value = 0.0
    except KeyError:
        pass
    me.materials.append(m)
    return ob


def scatter_grass(band, clump, centre_pt, radius, density):
    """Geometry-nodes scatter of the clump on the meadow faces (zone R or B), within radius of a point."""
    mod = band.modifiers.new("grass", "NODES")
    ng = bpy.data.node_groups.new("rw_grass_scatter", "GeometryNodeTree")
    mod.node_group = ng
    ng.interface.new_socket("Geometry", in_out="INPUT", socket_type="NodeSocketGeometry")
    ng.interface.new_socket("Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
    n = ng.nodes
    gi, go = n.new("NodeGroupInput"), n.new("NodeGroupOutput")
    dist = n.new("GeometryNodeDistributePointsOnFaces")
    dist.distribute_method = "POISSON"
    dist.inputs["Distance Min"].default_value = 0.16
    dist.inputs["Density Max"].default_value = density
    # density mask: meadow (zone R + B) times a disc round the focus point
    zone = n.new("GeometryNodeInputNamedAttribute")
    zone.data_type = "FLOAT_COLOR"
    zone.inputs["Name"].default_value = "zone"
    sepc = n.new("FunctionNodeSeparateColor")
    ng.links.new(zone.outputs["Attribute"], sepc.inputs[0])
    addz = n.new("ShaderNodeMath")
    addz.operation = "ADD"
    ng.links.new(sepc.outputs[0], addz.inputs[0])
    ng.links.new(sepc.outputs[2], addz.inputs[1])
    pos = n.new("GeometryNodeInputPosition")
    d = n.new("ShaderNodeVectorMath")
    d.operation = "DISTANCE"
    ng.links.new(pos.outputs[0], d.inputs[0])
    d.inputs[1].default_value = tuple(centre_pt)
    disc = n.new("ShaderNodeMath")
    disc.operation = "MULTIPLY_ADD"
    disc.use_clamp = True
    ng.links.new(d.outputs["Value"], disc.inputs[0])
    disc.inputs[1].default_value = -1.0 / (0.4 * radius)
    disc.inputs[2].default_value = 1.0 / 0.4
    mul = n.new("ShaderNodeMath")
    mul.operation = "MULTIPLY"
    ng.links.new(addz.outputs[0], mul.inputs[0])
    ng.links.new(disc.outputs[0], mul.inputs[1])
    thr = n.new("ShaderNodeMath")
    thr.operation = "GREATER_THAN"
    thr.inputs[1].default_value = 0.6
    ng.links.new(mul.outputs[0], thr.inputs[0])
    ng.links.new(gi.outputs[0], dist.inputs["Mesh"])
    ng.links.new(thr.outputs[0], dist.inputs["Selection"])
    inst = n.new("GeometryNodeInstanceOnPoints")
    oi = n.new("GeometryNodeObjectInfo")
    oi.inputs["Object"].default_value = clump
    rv = n.new("FunctionNodeRandomValue")
    rv.data_type = "FLOAT"
    rv.inputs["Min"].default_value = 0.7
    rv.inputs["Max"].default_value = 1.35
    rr = n.new("FunctionNodeRandomValue")
    rr.data_type = "FLOAT"
    rr.inputs["Min"].default_value = 0.0
    rr.inputs["Max"].default_value = 6.283
    rot = n.new("FunctionNodeAlignRotationToVector")
    rot.axis = "Z"
    ng.links.new(dist.outputs["Normal"], rot.inputs["Vector"])
    spin = n.new("FunctionNodeRotateRotation")
    spin.rotation_space = "LOCAL"
    eul = n.new("FunctionNodeEulerToRotation") if hasattr(bpy.types, "FunctionNodeEulerToRotation") else None
    comb = n.new("ShaderNodeCombineXYZ")
    ng.links.new(rr.outputs["Value"], comb.inputs[2])
    if eul is not None:
        ng.links.new(comb.outputs[0], eul.inputs[0])
        ng.links.new(rot.outputs[0], spin.inputs[0])
        ng.links.new(eul.outputs[0], spin.inputs[1])
        ng.links.new(spin.outputs[0], inst.inputs["Rotation"])
    else:
        ng.links.new(rot.outputs[0], inst.inputs["Rotation"])
    ng.links.new(dist.outputs["Points"], inst.inputs["Points"])
    ng.links.new(oi.outputs["Geometry"], inst.inputs["Instance"])
    ng.links.new(rv.outputs["Value"], inst.inputs["Scale"])
    join = n.new("GeometryNodeJoinGeometry")
    ng.links.new(gi.outputs[0], join.inputs[0])
    ng.links.new(inst.outputs[0], join.inputs[0])
    ng.links.new(join.outputs[0], go.inputs[0])
    return mod


def append_rocks(blend_path):
    names = []
    with bpy.data.libraries.load(blend_path, link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n.startswith("rock_")]
    for o in dst.objects:
        if o is not None:
            names.append(o)
    return names


def rim_point(fr, s, y, right):
    """Where the rim surface is at station s and band height y (searching the profile), and its outward
    direction."""
    i = max(0, min(NS, int(round(s / L * NS))))
    _, p, t, u, side = fr[i]
    best, bk = 1e9, 0
    for k in range(NP):
        q = PROFILE[k]
        if (q.x > 0) != right or abs(q.x) < 0.8 * HALF_W:
            continue
        dy = abs(q.y - y)
        if dy < best:
            best, bk = dy, k
    q = PROFILE[bk]
    n2 = profile_normal(bk)
    d = rim_offset(s, q.y, right)
    pos = p + side * (q.x + n2.x * d) + u * (q.y + n2.y * d)
    nrm = (side * n2.x + u * n2.y).normalized()
    return pos, nrm, t, u


def place_rim_rocks(fr, protos, seed=5):
    """Dress both rims with the adapted rock kit as the cliff itself: per column, two tall masses grow from
    the cliff's mid-line toward each face (the upper one's moss faces the band's up, the lower one's its
    down -- each face has its own gravity), overlapping their neighbours; a few smaller blocks break the line."""
    import random
    rng = random.Random(seed)
    placed = []

    def put(proto, pos, nrm, up, sx, sy, sz, yaw_deg):
        o = proto.copy()
        o.hide_render = False
        o.hide_viewport = False
        bpy.context.scene.collection.objects.link(o)
        outward = (nrm - up * nrm.dot(up)).normalized()
        ax = (Matrix.Rotation(math.radians(yaw_deg), 3, up) @ outward).normalized()
        xx = ax.cross(up).normalized()
        o.rotation_mode = "QUATERNION"
        o.rotation_quaternion = Matrix((xx, ax, up)).transposed().to_quaternion()
        o.scale = (sx, sy, sz)
        o.location = pos
        placed.append(o)

    for right in (True, False):
        s = rng.uniform(0, 8)
        while s < L - 6:
            xs = rng.uniform(26.0, 40.0)
            for half in (1, -1):
                y0 = half * rng.uniform(-3.0, 2.0)
                pos, nrm, t, u = rim_point(fr, s + rng.uniform(-3, 3), y0, right)
                up = u * half
                sz = rng.uniform(34.0, 46.0)
                put(rng.choice(protos), pos + nrm * rng.uniform(1.0, 4.0), nrm, up,
                    xs * rng.uniform(0.85, 1.15), rng.uniform(20.0, 28.0), sz, rng.uniform(-25, 25))
            if rng.random() < 0.5:  # a smaller fallen block at a cliff edge
                half = rng.choice((1, -1))
                pos, nrm, t, u = rim_point(fr, s + rng.uniform(-6, 6), half * rng.uniform(FACE - 6, FACE - 1), right)
                sz = rng.uniform(8.0, 14.0)
                put(rng.choice(protos), pos - nrm * 1.5, nrm, u * half, sz, sz, sz * 0.8, rng.uniform(-60, 60))
            s += 0.68 * xs * rng.uniform(0.55, 0.75)
    return placed


def surface_point(fr, s, x, lift):
    i = max(0, min(NS, int(round(s / L * NS))))
    _, p, t, u, side = fr[i]
    return p + side * x + u * (FACE + face_height(s, x, TOP_HILLS, 0.0) + lift), t, u, side


def main():
    args = rw.argv_after_dashes()
    out = args[0] if args else "build/rw/stage2/band"
    W = int(args[1]) if len(args) > 1 else 960
    H = int(args[2]) if len(args) > 2 else 540
    S = int(args[3]) if len(args) > 3 else 48
    sc = rw.reset()
    band, fr = build_band()
    band.data.materials.append(ground_material())
    os.makedirs(out, exist_ok=True)
    rw.export_glb([band], os.path.join(out, "band_section.glb"))

    _, pm, tm, um, sm = fr[NS // 2]
    sun = (0.45 * um + 0.75 * sm - 0.45 * tm).normalized()
    rw.sky_world(sc, sun, strength=0.95)
    rw.sun_light(sun, strength=5.0)
    fill = rw.sun_light((-0.3 * sun + -0.9 * um).normalized(), strength=1.4, angle_deg=40, color=rw.P["cloud_bounce"])
    fill.name = "cloud_sea_bounce"
    fill.data.use_shadow = False
    rw.setup_render(sc, W, H, samples=S, exposure=-0.2)

    clump = grass_clump()
    sc.collection.objects.link(clump)
    clump.location = (0, 0, -5000)
    eye, t0, u0, s0 = surface_point(fr, 40.0, -30.0, 2.2)
    scatter_grass(band, clump, eye, 70.0, 14.0)

    rocks_blend = os.path.join(os.path.dirname(os.path.abspath(out)), "rocks", "rocks.blend")
    if os.path.exists(rocks_blend):
        protos = [o for o in append_rocks(rocks_blend)]
        for o in protos:
            o.location = (0, 0, -9000)
        placed = place_rim_rocks(fr, protos)
        print("RIM ROCKS", len(placed))
    cam = rw.camera()
    # 1 overview (rectilinear), from outside the right rim, high
    rw.rectilinear(cam, 30)
    rw.aim(cam, pm + sm * 420 + um * 260 - tm * 240, pm + tm * 20, up=um)
    rw.render(sc, os.path.join(out, "band_1_overview.png"))
    # 2 on the surface, 150-degree fisheye, along the band toward the big hill and the twist
    rw.fisheye(cam, 150, W, H)
    tgt, _, _, _ = surface_point(fr, 140.0, -10.0, 14.0)
    rw.aim(cam, eye, tgt, up=u0)
    rw.render(sc, os.path.join(out, "band_2_surface_fisheye150.png"))
    # 3 the right rim from outside, at cliff height
    rw.rectilinear(cam, 24)
    _, p2, t2, u2, s2 = fr[int(NS * 0.55)]
    rw.aim(cam, p2 + s2 * 260 + u2 * 30 - t2 * 120, p2 + s2 * 120 + t2 * 30, up=u2)
    rw.render(sc, os.path.join(out, "band_3_rim.png"))
    # 4 under the band, on the other meadow face, looking along it (its gravity is opposite)
    rw.fisheye(cam, 150, W, H)
    _, p3, t3, u3, s3 = fr[int(NS * 0.3)]
    x4 = 20.0
    e4 = p3 + s3 * x4 - u3 * (FACE + face_height(L * 0.3, -x4, BOTTOM_HILLS, 1.3) + 4.0)
    rw.aim(cam, e4, e4 + t3 * 100 - u3 * 5, up=-u3)
    rw.render(sc, os.path.join(out, "band_4_underside_fisheye150.png"))
    rw.save_blend(os.path.join(out, "band_section.blend"))
    print("done")


if __name__ == "__main__":
    main()
