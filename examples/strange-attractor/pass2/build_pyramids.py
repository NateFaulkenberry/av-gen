"""The three Cydonia pyramids (Art Pass 2, M2): ancient, eroded, partly buried, each in its own state of ruin.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_pyramids.py -- assets/strange-attractor/build

Writes pyramid-a.glb, pyramid-b.glb and pyramid-c.glb: the Giza arrangement's three sizes. All geometry is made here
(no downloads), in metres, the base's centre at the origin on the ground (y up after export).

Each pyramid is a height field over a square grid that reaches past its base, so one surface carries the monument,
the sand banked against it and the ground it is sinking into:
- the casing: the true pyramid (51.8 degrees, Giza's), eroded by a noise, missing in patches where it has fallen;
- the core: courses of stone, stepped, showing wherever the casing is gone;
- the corners: broken back, more at the top;
- collapse: a slumped scar on one face (the oldest pyramid's is the largest);
- the sand: ramps against the base, deepest on the windward side, burying the lowest courses.

Three materials: the casing (pale ochre), the core (darker, redder: the courses' shadowed faces read on their own),
the sand (the plain's rust). The pyramid portal opens in pyramid-b's south face, where its casing has fallen.
"""
import bpy
import bmesh
import math
import random
import sys

from mathutils import Vector

out_dir = sys.argv[sys.argv.index("--") + 1]
_rest = sys.argv[sys.argv.index("--") + 2:]
bpy.ops.wm.read_factory_settings(use_empty=True)


def hash2(i, j, seed):
    h = (i * 374761393 + j * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)


def vnoise(x, y, seed):
    i, j = math.floor(x), math.floor(y)
    fx, fy = x - i, y - j
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash2(i, j, seed), hash2(i + 1, j, seed)
    c, d = hash2(i, j + 1, seed), hash2(i + 1, j + 1, seed)
    return (a + (b - a) * ux) * (1 - uy) + (c + (d - c) * ux) * uy


def fbm(x, y, seed, octaves=4):
    s, amp, f, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        s += amp * vnoise(x * f, y * f, seed + o * 17)
        norm += amp
        amp *= 0.5
        f *= 2.0
    return s / norm


def smooth(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


TEX = (sys.argv[sys.argv.index("--") + 2] if len(sys.argv) > sys.argv.index("--") + 2 else None)   # M3d: the photoscanned sets (Poly Haven, CC0; assets/strange-attractor/polyhaven/textures, never committed)


def material(name, color, rough, tex=None, tint=None):
    """A material; with `tex` (a Poly Haven set's id) its albedo is the photo, multiplied by `tint` (the stone's own
    colour: the variation between blocks survives), and its normal map, at the mesh's UVs (M3d)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    if tex and TEX:
        import os
        d = os.path.join(TEX, tex)
        img = nt.nodes.new("ShaderNodeTexImage")
        img.image = bpy.data.images.load(os.path.join(d, f"{tex}_diff_2k.jpg"))
        mul = nt.nodes.new("ShaderNodeMix")
        mul.data_type = "RGBA"
        mul.blend_type = "MULTIPLY"
        mul.inputs["Factor"].default_value = 1.0
        mul.inputs[7].default_value = (*(tint or (1.0, 1.0, 1.0)), 1.0)
        nt.links.new(img.outputs["Color"], mul.inputs[6])
        nt.links.new(mul.outputs[2], b.inputs["Base Color"])
        nimg = nt.nodes.new("ShaderNodeTexImage")
        nimg.image = bpy.data.images.load(os.path.join(d, f"{tex}_nor_gl_2k.jpg"))
        nimg.image.colorspace_settings.name = "Non-Color"
        nmap = nt.nodes.new("ShaderNodeNormalMap")
        nt.links.new(nimg.outputs["Color"], nmap.inputs["Color"])
        nt.links.new(nmap.outputs["Normal"], b.inputs["Normal"])
    return m


CASING = material("pyramid_casing", (0.50, 0.33, 0.21), 0.92)
CORE = material("pyramid_core", (0.33, 0.19, 0.12), 0.95)
SAND = material("pyramid_sand", (0.46, 0.24, 0.13), 1.0, tex="aerial_sand", tint=(1.0, 0.56, 0.36))

SLOPE = math.tan(math.radians(51.8))

# name, base (m), state: casing kept (0..1), corner loss, collapse (face, size), sand depth (share of height), seed
PYRAMIDS = [
    ("pyramid-a", 300.0, dict(casing=0.55, corners=0.10, collapse=("east", 0.10), sand=0.10, seed=11)),
    ("pyramid-b", 280.0, dict(casing=0.70, corners=0.07, collapse=(None, 0.0), sand=0.08, seed=23)),
    ("pyramid-c", 140.0, dict(casing=0.25, corners=0.18, collapse=("north", 0.22), sand=0.16, seed=37)),
]


def build(name, base, st):
    half = base * 0.5
    H = half * SLOPE
    course = max(base / 90.0, 1.6)          # stone courses (m)
    reach = half * 1.45                      # the grid runs past the base into the sand and the plain
    step = max(base / 320.0, 0.6)            # grid spacing (m)
    n = int(2 * reach / step) + 1
    seed = st["seed"]

    def height(x, y):
        m = max(abs(x), abs(y))              # the square pyramid's distance
        pyr = H * (1.0 - m / half)
        # the core: courses (terraces), each a step back from the casing
        core_h = max(0.0, H * (1.0 - (m + course * 0.9) / half))
        core = math.floor(core_h / course) * course + course * smooth(0.82, 1.0, (core_h / course) % 1.0)
        # the casing, eroded; missing in patches (more toward the bottom and the corners)
        rough = (fbm(x / 9.0, y / 9.0, seed) - 0.5) * course * 0.6
        corner = min(abs(abs(x) - abs(y)) / max(m, 1e-3), 1.0)   # 0 on a corner's arris, 1 mid-face
        keep = fbm(x / 38.0, y / 38.0, seed + 3) + 0.35 * (pyr / H) - 0.25 * (1.0 - corner)
        # M3: the casing's edge blends over a short band instead of switching per grid cell (the grid steps)
        cw = smooth(-0.035, 0.035, keep - (1.0 - st["casing"]))
        casing_on = cw > 0.5
        h = core + (pyr + rough - core) * cw
        # broken corners: the arrises chewed back, worse up high
        bite = st["corners"] * H * (1.0 - smooth(0.0, 0.18, corner)) * (0.4 + 0.6 * pyr / H) * fbm(x / 20.0, y / 20.0, seed + 5)
        h -= bite
        # collapse: a slumped scar on one face
        face, size = st["collapse"]
        if face:
            cx, cy = {"east": (half * 0.45, 0.0), "north": (0.0, half * 0.45)}[face]
            r = math.hypot(x - cx, y - cy) / (base * size)
            if r < 1.0:
                h -= H * 0.35 * size * 4.0 * (1.0 - r * r) ** 2 * (0.7 + 0.3 * fbm(x / 7.0, y / 7.0, seed + 9))
        h = max(h, 0.0) if m < half else 0.0
        # the sand: ramps against the base, deeper to the windward (west), rippled
        wind = 0.6 + 0.4 * smooth(-1.0, 1.0, -x / half)
        out = max(m - half * 0.92, 0.0)
        sand = st["sand"] * H * wind * math.exp(-out / (half * 0.16)) * (0.75 + 0.5 * fbm(x / 25.0, y / 25.0, seed + 13))
        sand += 0.25 * math.sin(x * 0.9 + 2.0 * fbm(x / 30.0, y / 30.0, seed + 15)) * smooth(half * 0.95, half * 1.2, m)
        # the plain: the grid's edge sinks a little so the node can sit into the ground without a lip
        sink = -2.0 * smooth(reach * 0.85, reach, m)
        if sand + sink > h:
            return sand + sink, 2, 1.0
        return h, (0 if casing_on else 1), cw

    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    for m_ in (CASING, CORE, SAND):
        mesh.materials.append(m_)
    bm = bmesh.new()
    verts, kinds, cws = [], [], []
    for j in range(n):
        y = -reach + j * step
        for i in range(n):
            x = -reach + i * step
            h, k, cw = height(x, y)
            verts.append(bm.verts.new((x, y, h)))
            kinds.append(k)
            cws.append(cw)
    # M3: uv.x carries the class to the stone program (0 casing .. 0.5 core, 1 sand), uv.y the height share
    uv_layer = bm.loops.layers.uv.new("UVMap")
    for j in range(n - 1):
        for i in range(n - 1):
            a = j * n + i
            f = bm.faces.new((verts[a], verts[a + 1], verts[a + n + 1], verts[a + n]))  # wound to face up (+z)
            ks = [kinds[a], kinds[a + 1], kinds[a + n], kinds[a + n + 1]]
            f.material_index = 2 if ks.count(2) >= 2 else (1 if 1 in ks else 0)
            for loop, vi in zip(f.loops, (a, a + 1, a + n + 1, a + n)):
                cls = 1.0 if kinds[vi] == 2 else 0.5 * (1.0 - cws[vi])
                loop[uv_layer].uv = (cls, max(0.0, loop.vert.co.z) / H)
    bm.to_mesh(mesh)
    bm.free()
    mesh.shade_flat()
    # decimate the flat plain and the smooth faces a little: the height field is dense everywhere
    dec = obj.modifiers.new("decimate", "DECIMATE")
    dec.decimate_type = "DISSOLVE"
    dec.angle_limit = math.radians(1.5)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier="decimate")
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    path = f"{out_dir}/{name}.glb"
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_apply=True)
    print("PYRAMID", path, "base", base, "height", round(H, 1), "triangles", tris)
    return obj




# ---- M3b (owner: chipped, coursed stone blocks like the Great Pyramid, stepped, weathered and broken edges, not
# crackle or cobblestone; much more buried in sand) -------------------------------------------------------------
# Each pyramid is built course by course from blocks: an outer ring of blocks of random length on each course, a
# hidden core box per course behind them. Each block is jittered, its top edge chamfered by a random amount, its
# corners chipped; blocks go missing more at the arrises and toward the top, and the upper courses of the middle
# pyramid keep a smooth casing cap (as Khafre's does). A sand mound buries the base -- deepest to windward (west) --
# and blocks wholly under it are not built. Five stone materials, one per block at random, plus dust on the treads.
STONES = [material(f"block_{k}", c, r, tex="old_sandstone_02", tint=tuple(min(1.0, 1.9 * v) for v in c)) for k, (c, r) in enumerate((
    ((0.43, 0.31, 0.2), 0.94), ((0.38, 0.26, 0.16), 0.95), ((0.48, 0.35, 0.23), 0.92), ((0.34, 0.22, 0.14), 0.96),
    ((0.45, 0.28, 0.16), 0.95)))]
DUST = material("block_dust", (0.5, 0.27, 0.14), 1.0, tex="aerial_sand", tint=(1.0, 0.56, 0.36))
CAP = material("casing_cap", (0.44, 0.31, 0.2), 0.95, tex="old_sandstone_02", tint=(0.85, 0.6, 0.42))
STAIN = material("block_stain", (0.38, 0.2, 0.11), 0.97, tex="old_sandstone_02", tint=(0.75, 0.42, 0.25))
DARK = material("block_dark", (0.24, 0.16, 0.11), 0.97, tex="old_sandstone_02", tint=(0.48, 0.32, 0.22))
STATES = {
    "pyramid-a": dict(burial=0.3, missing=0.07, chip=0.35, cap=0.0, seed=11),
    "pyramid-b": dict(burial=0.26, missing=0.045, chip=0.3, cap=0.12, seed=23),
    "pyramid-c": dict(burial=0.36, missing=0.11, chip=0.45, cap=0.0, seed=37),
}


def build_blocks(name, base, st):
    rnd = random.Random(st["seed"])
    half = base * 0.5
    H = half * SLOPE
    ch = max(base / 120.0, 1.15)        # course height (m)
    tread = ch / SLOPE                  # each course steps in by this much on every side
    depth = 2.2 * ch                    # how far a block runs into the pyramid
    seed = st["seed"]

    def sand(x, y):
        """M3c (owner: realistic, wind-banked sand; no streak banding, no cliff): the drift banked against the faces --
        deepest to windward (west), long and low there, shorter to leeward -- falling to the plain by a smooth cubic
        profile with no lip at the crest and none at the toe; broad, low undulations, no geometric ripples (the drift
        is smooth shaded; ripples belong to the texture's scale, not this grid's)."""
        m = max(abs(x), abs(y))
        westness = smooth(-1.0, 1.0, -x / half)
        # M3d (references: the Sphinx buried to the neck, Giza's faces with sand banked up them): the wind piles the
        # sand highest mid-face and scours the corners, so the burial line dips at every arris
        mid = 1.0 - min(abs(abs(x) - abs(y)) / max(m, 1e-3), 1.0)        # 1 on a corner's diagonal, 0 mid-face
        crest = st["burial"] * H * (0.7 + 0.45 * westness) * (0.78 + 0.44 * fbm(x / 90.0, y / 90.0, seed + 13))
        crest *= 1.0 - 0.4 * mid ** 2
        if m < half:
            # inside the footprint the drift keeps climbing a little into the courses (it fills the steps above the
            # line, where the stone face is lower than it)
            return crest + (half - m) * 0.18
        out = math.hypot(max(abs(x) - half, 0.0), max(abs(y) - half, 0.0))   # rounded around the corners
        run = crest * (2.4 + 2.2 * westness) * (0.85 + 0.3 * fbm(x / 70.0, y / 70.0, seed + 17))
        run = min(run, half * (0.75 + 0.9 * westness))
        t = min(out / max(run, 1.0), 1.0)
        prof = 1.0 - t * t * (3.0 - 2.0 * t)
        swell = (fbm(x / 45.0, y / 45.0, seed + 19) - 0.5) * 0.12 * crest * (1.0 - t)
        return crest * prof + swell

    bm = bmesh.new()
    mats_of = []
    ncourses = int(H / ch)
    cap_from = int(ncourses * (1.0 - st["cap"])) if st["cap"] > 0 else ncourses + 1

    def add_box(cx, cy, z0, lx, ly, lz, rot, chip, mat):
        hx, hy = lx / 2, ly / 2
        c, sn = math.cos(rot), math.sin(rot)
        pts = []
        for zz in (0.0, lz):
            for (ax, ay) in ((-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)):
                if zz > 0:   # the top chamfered (worn) and the corners chipped at random
                    k = 1.0 - chip * rnd.uniform(0.04, 0.16)
                    ax, ay = ax * k, ay * k
                    zt = zz - chip * rnd.uniform(0.0, 0.18) * lz
                else:
                    zt = zz
                pts.append(Vector((cx + ax * c - ay * sn, cy + ax * sn + ay * c, z0 + zt)))
        vs = [bm.verts.new(p) for p in pts]
        faces = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
        for f in faces:
            face = bm.faces.new([vs[i] for i in f])
            mats_of.append(mat)

    for k in range(ncourses):
        z0 = k * ch
        s_out = half - k * tread                  # the course's outer half-side
        if s_out < 1.0:
            break
        if k >= cap_from:                         # the casing cap: four smooth faces from here up
            break
        # the hidden core behind the blocks
        core = s_out - depth
        if core > 0.5:
            add_box(0.0, 0.0, z0, 2 * core, 2 * core, ch, 0.0, 0.0, 0)
        # the four sides: blocks along each, from corner to corner
        for side in range(4):
            ang = side * math.pi / 2
            ca, sa = math.cos(ang), math.sin(ang)
            t = -s_out
            while t < s_out:
                L = rnd.uniform(1.6, 3.0) * ch
                L = min(L, s_out - t)
                if L < 0.4 * ch:
                    break
                mid = t + L / 2
                # the block's centre on this side (the side's outward normal is (ca, sa))
                ox, oy = ca * (s_out - depth / 2), sa * (s_out - depth / 2)
                px, py = ox + -sa * mid, oy + ca * mid
                corner = 1.0 - min(1.0, (s_out - abs(mid)) / (3.0 * ch))     # 1 at an arris
                height_frac = z0 / H
                p_missing = st["missing"] * (1.0 + 4.0 * corner + 2.5 * height_frac)
                t += L
                if sand(px + ca * depth / 2, py + sa * depth / 2) > z0 + ch * 1.05:
                    continue                                                # under the sand: not built
                if rnd.random() < p_missing:
                    continue
                jitter = 0.04 * ch
                chip = st["chip"] * (1.0 + 1.5 * corner + height_frac)
                add_box(px + rnd.uniform(-jitter, jitter), py + rnd.uniform(-jitter, jitter), z0 + rnd.uniform(-0.02, 0.0),
                        L * rnd.uniform(0.93, 0.99), depth * rnd.uniform(0.96, 1.0), ch * rnd.uniform(0.94, 1.0),
                        ang + rnd.uniform(-0.012, 0.012), chip, 1 + rnd.randrange(5))
    # the casing cap
    if st["cap"] > 0:
        z0 = cap_from * ch
        s0 = half - cap_from * tread
        apex = Vector((0, 0, H))
        corners = [Vector((s0, s0, z0)), Vector((-s0, s0, z0)), Vector((-s0, -s0, z0)), Vector((s0, -s0, z0))]
        # M3c (owner: no smooth white capstone): the cap's four faces as a fine grid, eroded by a noise, its edges and
        # tip broken back, in a weathered casing stone
        NS = 24
        for i in range(4):
            a_, b_ = corners[i], corners[(i + 1) % 4]
            rows = []
            for r in range(NS + 1):
                fr = r / NS
                row = []
                for q in range(NS + 1 - r):
                    fq = q / max(NS - r, 1) if NS - r > 0 else 0.0
                    p_ = a_.lerp(b_, fq).lerp(apex, fr) if r < NS else apex.copy()
                    nrm = Vector((p_.x, p_.y, 0.0))
                    nrm = nrm.normalized() if nrm.length > 1e-6 else Vector((0, 0, 1))
                    edge = min(fq, 1.0 - fq) if NS - r > 0 else 0.0
                    bite = (0.9 - 0.6 * min(edge * 4.0, 1.0)) * (0.5 + fr)
                    d = (fbm(p_.x / 3.0 + i * 7, p_.y / 3.0 + p_.z / 3.0, seed + 61) - 0.55) * 1.4 - bite * 0.8
                    row.append(bm.verts.new(p_ + nrm * d - Vector((0, 0, 0.6 * fr * fr))))
                rows.append(row)
            for r in range(NS):
                for q in range(NS - r):
                    bm.faces.new((rows[r][q], rows[r][q + 1], rows[r + 1][q]))
                    mats_of.append(6)
                    if q + 1 < len(rows[r + 1]):
                        bm.faces.new((rows[r][q + 1], rows[r + 1][q + 1], rows[r + 1][q]))
                        mats_of.append(6)
    # the sand mound: a height field ring around the base (and over the buried courses)
    reach = half * 2.7
    stepg = max(base / 130.0, 1.6)
    n = int(2 * reach / stepg) + 1
    grid = []
    for j in range(n):
        y = -reach + j * stepg
        row = []
        for i in range(n):
            x = -reach + i * stepg
            m = max(abs(x), abs(y))
            h = sand(x, y)
            # inside the footprint the sand only shows where it is above the stone's face
            face = H * (1.0 - m / half) if m < half else 0.0
            h = max(h, 0.0) - 2.0 * smooth(reach * 0.9, reach, m)
            if m < half and h < face - ch * 0.2:
                h = face - ch * 0.6     # tucked under the stone: hidden
            row.append(bm.verts.new((x, y, h)))
        grid.append(row)
    for j in range(n - 1):
        for i in range(n - 1):
            bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
            mats_of.append(7)
    me = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    for m_ in [STONES[0]] + STONES + [CAP, SAND]:
        me.materials.append(m_)
    bm.faces.ensure_lookup_table()
    wrnd = random.Random(st["seed"] + 101)
    for f, mi in zip(bm.faces, mats_of):
        f.material_index = mi
        f.smooth = mi == 7                    # the drift smooth, the stone faceted
        if mi in (1, 2, 3, 4, 5):
            c = f.calc_center_median()
            sline = sand(c.x, c.y)
            if f.normal.z > 0.7 and (c.z < sline + 7.0 * ch or wrnd.random() < 0.35):
                f.material_index = 8          # sand spilled into the courses, dust on the treads
            elif c.z < sline + 3.0 * ch and wrnd.random() < 0.6:
                f.material_index = 9          # sand-scoured, rust-stained low courses
            elif wrnd.random() < 0.07:
                f.material_index = 10         # weathered dark stones
    me.materials.append(DUST)
    me.materials.append(STAIN)
    me.materials.append(DARK)
    # M3d: UVs for the photo textures -- each face projected on its dominant axis (a box projection), stone at 2.5 m
    # a tile, the drift at 7 m with a slow rotation of the projection across it (the tiling does not line up)
    uvl = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        tile = 7.0 if f.material_index == 7 else 2.5
        for loop in f.loops:
            c = loop.vert.co
            u, v = [(c.y, c.z), (c.x, c.z), (c.x, c.y)][ax]
            if f.material_index == 7:
                a = 0.6 * math.sin(c.x / 90.0) + 0.4 * math.cos(c.y / 70.0)
                u, v = u * math.cos(a) - v * math.sin(a), u * math.sin(a) + v * math.cos(a)
            loop[uvl].uv = (u / tile, v / tile)
    bm.to_mesh(me)
    bm.free()
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    # M3e (owner: no visible tiling): the drift is its own glb, <name>-sand.glb, so the scene can draw it through the
    # plain's own sand program (ADR-1572, ADR-1905) instead of this UV-mapped tile
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="DESELECT")
    obj.active_material_index = 7
    bpy.ops.object.material_slot_select()
    bpy.ops.mesh.separate(type="SELECTED")
    bpy.ops.object.mode_set(mode="OBJECT")
    drift = [o for o in bpy.context.selected_objects if o is not obj][0]
    drift.name = f"{name}-sand"
    bpy.ops.object.select_all(action="DESELECT")
    drift.select_set(True)
    bpy.ops.export_scene.gltf(filepath=f"{out_dir}/{name}-sand.glb", use_selection=True, export_apply=True)
    drift.hide_set(True)
    path = f"{out_dir}/{name}.glb"
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_apply=True)
    print("PYRAMID", path, "base", base, "height", round(H, 1), "courses", ncourses, "triangles", tris)
    return obj


for name, base, _ in PYRAMIDS:
    o = build_blocks(name, base, STATES[name])
    o.hide_set(True)
