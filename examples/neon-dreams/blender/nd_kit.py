"""Neon Dreams -- Mooring building kit v1 (Lanternwright vernacular), engine export.

Builds original architecture as GLB for AV Gen: metres, origin at the base centre of the footprint at deck level,
Blender Z-up exported Y-up. One mesh per asset, materials named by light role so the engine can drive them:

  facade   dark wall                         trim     slabs, frames, sills (slightly lighter)
  metal    pipes, rails, tanks, stilts       glass    window panes (dark; UV = window id, see below)
  win_warm shop panes and some windows       win_cool some windows
  strip    eave lines                        beacon   mast light
  landmark the Great Lantern's bell

Window panes: every pane's four UVs are collapsed to ONE point (u = brightness level 0..1 in 16 steps, v = a hash
in 0..1 unique to the window). An emissive texture win_levels.png (16x1 grey ramp) therefore gives each lit pane
its own brightness with plain glTF, and a material program can hash uv for per-window, per-instance lit/dark.

  Blender -b --factory-startup -P nd_kit.py -- --out <dir> [--preview <png>]
"""
import bpy, math, random, sys, os, json
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "blockout"))   # studio work folder layout
sys.path.insert(0, HERE)                                    # worktree layout: nd_lib.py sits beside this file
from nd_lib import MB, new_mat, node, hex2lin, link  # noqa

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
OUT = arg("--out", os.path.join(HERE, "out"))
PREVIEW = arg("--preview", "")
os.makedirs(OUT, exist_ok=True)
FLOOR, BAY = 3.2, 2.4
LEVELS = os.path.join(OUT, "win_levels.png")


def make_levels():
    img = bpy.data.images.new("win_levels", 16, 1, alpha=False)
    px = []
    for i in range(16):
        v = 0.12 + 0.88 * (i / 15.0) ** 1.6
        px += [v, v, v, 1.0]
    img.pixels = px
    img.filepath_raw = LEVELS
    img.file_format = 'PNG'
    img.save()
    return img


MATS = {}


def mats():
    lv = make_levels()

    def pbr(name, col, rough, metal=0.0, emit=None, strength=0.0, tex=None):
        m, nt = new_mat(name)
        out = node(nt, 'ShaderNodeOutputMaterial'); b = node(nt, 'ShaderNodeBsdfPrincipled')
        nt.links.new(b.outputs[0], out.inputs[0])
        b.inputs['Base Color'].default_value = (*col, 1); b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
        if emit:
            b.inputs['Emission Color'].default_value = (*emit, 1); b.inputs['Emission Strength'].default_value = strength
            if tex:
                t = node(nt, 'ShaderNodeTexImage'); t.image = tex; t.interpolation = 'Closest'
                nt.links.new(t.outputs[0], b.inputs['Emission Color'])
        MATS[name] = m

    pbr("facade", (0.036, 0.033, 0.046), 0.72)
    pbr("trim", (0.070, 0.064, 0.080), 0.55)
    pbr("metal", (0.045, 0.047, 0.055), 0.42, 0.8)
    pbr("glass", (0.008, 0.009, 0.014), 0.28)
    pbr("win_warm", (0.01, 0.008, 0.006), 0.3, emit=hex2lin("#ffb86b"), strength=8.0, tex=lv)
    pbr("win_cool", (0.006, 0.007, 0.012), 0.3, emit=hex2lin("#d9ccff"), strength=8.0, tex=lv)
    pbr("strip", (0.01, 0.01, 0.01), 0.4, emit=hex2lin("#c06bff"), strength=12.0)
    pbr("beacon", (0.01, 0.0, 0.0), 0.4, emit=hex2lin("#ff2030"), strength=40.0)
    pbr("landmark", (0.02, 0.012, 0.018), 0.5, emit=hex2lin("#ff9be6"), strength=6.0, tex=lv)


class Asset:
    def __init__(self, name):
        self.name = name
        self.mb = {}

    def m(self, mat):
        return self.mb.setdefault(mat, MB())

    def finish(self):
        objs = []
        for mat, mb in self.mb.items():
            o = mb.build(self.name + "_" + mat, MATS[mat])
            if o:
                objs.append(o)
        bpy.ops.object.select_all(action='DESELECT')
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        bpy.ops.object.join()
        ob = bpy.context.view_layer.objects.active
        ob.name = self.name; ob.data.name = self.name
        tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        return ob, tris


def wq(A, mat, O, T, Nn, u0, u1, v0, v1, n=0.0, uv=None, flip=False):
    """Quad on a wall plane: origin O, tangent T, outward normal Nn; (u along wall, v up, n out)."""
    Z = Vector((0, 0, 1))
    p = [O + T * u0 + Z * v0 + Nn * n, O + T * u1 + Z * v0 + Nn * n, O + T * u1 + Z * v1 + Nn * n, O + T * u0 + Z * v1 + Nn * n]
    if flip:
        p.reverse()
    A.m(mat).quad(*[tuple(q) for q in p], uv=uv or (u0 / 4, v0 / 4, u1 / 4, v1 / 4), double=False)


def wbox(A, mat, O, T, Nn, u0, u1, v0, v1, n0, n1):
    """Box in wall coordinates (n0..n1 out of the wall)."""
    Z = Vector((0, 0, 1))
    c = O + T * ((u0 + u1) / 2) + Z * ((v0 + v1) / 2) + Nn * ((n0 + n1) / 2)
    yaw = math.atan2(T.y, T.x)
    A.m(mat).box(tuple(c), (abs(u1 - u0), abs(n1 - n0), abs(v1 - v0)), yaw)


def window_cell(A, r, O, T, Nn, u, v, lit_p, warm_p, wid, ac_p=0.18, balcony=False):
    """One 2.4 x 3.2 m facade cell with a recessed, mullioned window. u, v = cell origin on the wall."""
    a0, a1, b0, b1, dep = u + 0.575, u + 1.825, v + 0.95, v + 2.30, 0.14
    wq(A, "facade", O, T, Nn, u, a0, v, v + FLOOR); wq(A, "facade", O, T, Nn, a1, u + BAY, v, v + FLOOR)
    wq(A, "facade", O, T, Nn, a0, a1, v, b0); wq(A, "facade", O, T, Nn, a0, a1, b1, v + FLOOR)
    Z = Vector((0, 0, 1))
    # reveals (four inward faces)
    for (p0, p1) in (((a0, b0), (a1, b0)), ((a1, b0), (a1, b1)), ((a1, b1), (a0, b1)), ((a0, b1), (a0, b0))):
        q = [O + T * p0[0] + Z * p0[1], O + T * p1[0] + Z * p1[1], O + T * p1[0] + Z * p1[1] - Nn * dep, O + T * p0[0] + Z * p0[1] - Nn * dep]
        A.m("trim").quad(*[tuple(x) for x in q], uv=(0, 0, 0.1, 0.1), double=False)
    lit = r.random() < lit_p
    mat = ("win_warm" if r.random() < warm_p else "win_cool") if lit else "glass"
    lvl = (int(r.random() ** 1.5 * 16) + 0.5) / 16.0
    wq(A, mat, O, T, Nn, a0, a1, b0, b1, n=-dep, uv=(lvl, wid, lvl, wid))
    wbox(A, "trim", O, T, Nn, (a0 + a1) / 2 - 0.025, (a0 + a1) / 2 + 0.025, b0, b1, -dep, -dep + 0.05)      # mullion
    wbox(A, "trim", O, T, Nn, a0, a1, b0 + 0.82, b0 + 0.86, -dep, -dep + 0.04)                                # transom
    wbox(A, "trim", O, T, Nn, a0 - 0.08, a1 + 0.08, b0 - 0.07, b0, -0.02, 0.09)                               # sill
    if balcony:
        wbox(A, "trim", O, T, Nn, u + 0.2, u + BAY - 0.2, v + 0.0, v + 0.12, 0.0, 1.05)
        for k in range(9):
            uu = u + 0.25 + k * (BAY - 0.5) / 8
            wbox(A, "metal", O, T, Nn, uu - 0.015, uu + 0.015, v + 0.12, v + 1.05, 1.0, 1.03)
        wbox(A, "metal", O, T, Nn, u + 0.2, u + BAY - 0.2, v + 1.02, v + 1.07, 0.98, 1.05)
        for uu in (u + 0.2, u + BAY - 0.23):
            wbox(A, "metal", O, T, Nn, uu, uu + 0.03, v + 0.12, v + 1.05, 0.0, 1.03)
    elif r.random() < ac_p:
        wbox(A, "metal", O, T, Nn, a0 + 0.2, a0 + 1.0, v + 0.3, v + 0.78, 0.0, 0.34)
        wbox(A, "metal", O, T, Nn, a0 + 0.25, a0 + 0.95, v + 0.24, v + 0.3, 0.02, 0.3)


def block(A, r, cx, cy, z0, w, d, floors, lit_p, warm_p, wbase, balcony_side=None, shop=False, blank_p=0.1):
    """One stacked block: four facades of window cells with corner piers; returns nothing."""
    sides = [(Vector((cx - w / 2, cy - d / 2, 0)), Vector((1, 0, 0)), w), (Vector((cx + w / 2, cy - d / 2, 0)), Vector((0, 1, 0)), d),
             (Vector((cx + w / 2, cy + d / 2, 0)), Vector((-1, 0, 0)), w), (Vector((cx - w / 2, cy + d / 2, 0)), Vector((0, -1, 0)), d)]
    for si, (O, T, L) in enumerate(sides):
        Nn = T.cross(Vector((0, 0, 1)))
        nb = int(L / BAY + 1e-6); mg = (L - nb * BAY) / 2
        bal_col = r.randrange(nb) if (balcony_side == si and nb > 0) else -1
        for fl in range(floors):
            v = z0 + fl * FLOOR
            if mg > 1e-4:
                wq(A, "facade", O, T, Nn, 0, mg, v, v + FLOOR); wq(A, "facade", O, T, Nn, L - mg, L, v, v + FLOOR)
            for b in range(nb):
                u = mg + b * BAY
                if shop and si == 0 and fl == 0:
                    shop_bay(A, r, O, T, Nn, u, v, door=(b == nb // 2))
                elif r.random() < blank_p or (si in (1, 3) and r.random() < 0.25):
                    wq(A, "facade", O, T, Nn, u, u + BAY, v, v + FLOOR)
                else:
                    wid = (wbase * 0.137 + si * 0.251 + fl * 0.0613 + b * 0.0171) % 1.0
                    window_cell(A, r, O, T, Nn, u, v, lit_p, warm_p, wid, balcony=(b == bal_col and fl > 0))
    A.m("facade").box((cx, cy, z0 + floors * FLOOR - 0.01), (w - 0.02, d - 0.02, 0.02))       # top cap
    A.m("facade").box((cx, cy, z0 + 0.01), (w - 0.02, d - 0.02, 0.02))                        # bottom cap


def shop_bay(A, r, O, T, Nn, u, v, door=False):
    a0, a1 = u + 0.16, u + BAY - 0.16
    b0, b1 = (v + 0.06, v + 2.5) if door else (v + 0.7, v + 2.5)
    wq(A, "facade", O, T, Nn, u, a0, v, v + FLOOR); wq(A, "facade", O, T, Nn, a1, u + BAY, v, v + FLOOR)
    wq(A, "facade", O, T, Nn, a0, a1, v, b0); wq(A, "facade", O, T, Nn, a0, a1, b1, v + FLOOR)
    lvl = (int((0.45 + 0.55 * r.random()) * 15.99) + 0.5) / 16.0
    wq(A, "win_warm", O, T, Nn, a0, a1, b0, b1, n=-0.1, uv=(lvl, 0.5, lvl, 0.5))
    Z = Vector((0, 0, 1))
    for (p0, p1) in (((a0, b0), (a1, b0)), ((a1, b0), (a1, b1)), ((a1, b1), (a0, b1)), ((a0, b1), (a0, b0))):
        q = [O + T * p0[0] + Z * p0[1], O + T * p1[0] + Z * p1[1], O + T * p1[0] + Z * p1[1] - Nn * 0.1, O + T * p0[0] + Z * p0[1] - Nn * 0.1]
        A.m("trim").quad(*[tuple(x) for x in q], uv=(0, 0, 0.1, 0.1), double=False)
    n_m = 1 if door else 2
    for k in range(1, n_m + 1):
        uu = a0 + (a1 - a0) * k / (n_m + 1)
        wbox(A, "trim", O, T, Nn, uu - 0.025, uu + 0.025, b0, b1, -0.1, -0.04)
    wbox(A, "trim", O, T, Nn, a0, a1, v + 2.02, v + 2.07, -0.1, -0.04)


def roof_kit(A, r, cx, cy, z, w, d):
    for (sx, sy, lw, ld) in ((0, -d / 2 + 0.1, w, 0.2), (0, d / 2 - 0.1, w, 0.2), (-w / 2 + 0.1, 0, 0.2, d), (w / 2 - 0.1, 0, 0.2, d)):
        A.m("trim").box((cx + sx, cy + sy, z + 0.45), (lw, ld, 0.9))
    tx, ty = cx - w * 0.2, cy + d * 0.18                                  # water tank on legs
    for sx in (-0.85, 0.85):
        for sy in (-0.85, 0.85):
            A.m("metal").box((tx + sx, ty + sy, z + 0.8), (0.12, 0.12, 1.6))
    A.m("metal").cyl((tx, ty, z + 1.6), 1.3, 2.3, n=14); A.m("metal").cyl((tx, ty, z + 3.9), 1.3, 0.55, n=14, r_top=0.12)
    for k in range(4):
        a = k / 4 * 6.283 + 0.4
        A.m("metal").beam((tx + 1.31 * math.cos(a), ty + 1.31 * math.sin(a), z + 1.7), (tx + 1.31 * math.cos(a), ty + 1.31 * math.sin(a), z + 3.8), 0.05)
    bx, by = cx + w * 0.22, cy - d * 0.2                                  # stair bulkhead with a lit door
    A.m("facade").box((bx, by, z + 1.35), (2.6, 3.0, 2.7)); A.m("trim").box((bx, by, z + 2.76), (2.9, 3.3, 0.12))
    A.m("win_warm").quad((bx - 1.31, by + 0.5, z + 0.1), (bx - 1.31, by - 0.5, z + 0.1), (bx - 1.31, by - 0.5, z + 2.1), (bx - 1.31, by + 0.5, z + 2.1), uv=(0.6, 0.3, 0.6, 0.3), double=False)
    for k in range(3):                                                    # condenser units
        A.m("metal").box((cx - w * 0.3 + k * 1.3, cy - d * 0.32, z + 0.55), (1.0, 0.75, 0.9))
        A.m("metal").cyl((cx - w * 0.3 + k * 1.3, cy - d * 0.32, z + 1.0), 0.32, 0.06, n=10)
    mx, my, mh = cx + w * 0.3, cy + d * 0.3, 9.0                           # mast with beacon
    A.m("metal").cyl((mx, my, z), 0.07, mh, n=6)
    A.m("metal").box((mx, my, z + mh * 0.7), (1.5, 0.05, 0.05)); A.m("metal").box((mx, my, z + mh * 0.84), (0.05, 1.0, 0.05))
    A.m("beacon").sphere((mx, my, z + mh + 0.12), 0.14, n=8, m=5)


def stilts(A, cx, cy, w, d, depth=14.0):
    pts = [(cx + sx * (w / 2 - 0.5), cy + sy * (d / 2 - 0.5)) for sx in (-1, 0, 1) for sy in (-1, 1)]
    for (x, y) in pts:
        A.m("metal").cyl((x, y, -depth), 0.26, depth, n=8, cap=False)
        A.m("metal").cyl((x, y, -0.5), 0.45, 0.5, n=8)
    for sy in (-1, 1):
        y = cy + sy * (d / 2 - 0.5)
        for (xa, xb) in ((-1, 0), (0, 1)):
            xa_, xb_ = cx + xa * (w / 2 - 0.5), cx + xb * (w / 2 - 0.5)
            A.m("metal").beam((xa_, y, -1.0), (xb_, y, -6.5), 0.1); A.m("metal").beam((xb_, y, -1.0), (xa_, y, -6.5), 0.1)
            A.m("metal").beam((xa_, y, -6.8), (xb_, y, -6.8), 0.14)
    A.m("trim").box((cx, cy, -0.25), (w + 0.6, d + 0.6, 0.5))            # deck plate under the building


def shophouse(name, seed, floors=6, w=8.4, d=10.8, lit=0.34, warm=0.3):
    """Shophouse stack: shop floor, then offset blocks separated by eave slabs with an eave line."""
    r = random.Random(seed); A = Asset(name)
    z, left, first, top = 0.0, floors, True, None
    while left > 0:
        n = 1 if first else min(left, r.choice([2, 2, 3, 3]))
        ox, oy = (0, 0) if first else (r.choice([-1.2, 0, 0, 1.2]) * (1 if w > 7.2 else 0), r.choice([-0.6, 0, 0.6]))
        ww = w if first else w - r.choice([0, 0, 2.4]) if w > 7.2 else w
        dd = d if first else d - r.choice([0, 0, 0, 2.4])
        cx, cy = ox, oy + (d - dd) / 2 * (1 if r.random() < 0.7 else -1) * (0 if first else 1)
        block(A, r, cx, cy, z, ww, dd, n, lit, warm, seed + z, balcony_side=None if first else r.choice([0, 0, 2, None]), shop=first)
        z += n * FLOOR; left -= n
        if left > 0 or True:
            A.m("trim").box((cx, cy, z), (ww + 2.2, dd + 2.2, 0.22))                                   # the eave slab
            A.m("trim").box((cx, cy - dd / 2 - 1.0, z - 0.2), (ww + 2.2, 0.12, 0.18))                   # fascia lip
            if first or r.random() < 0.6:                                                               # the eave line
                A.m("strip").box((cx, cy - dd / 2 - 0.86, z - 0.15), (ww + 1.6, 0.07, 0.06))
                A.m("strip").box((cx + ww / 2 + 0.86, cy, z - 0.15), (0.07, dd + 1.6, 0.06))
        top = (cx, cy, z, ww, dd); first = False
    roof_kit(A, r, top[0], top[1], top[2] + 0.11, top[3], top[4])
    for (px, py) in ((-w / 2 - 0.09, -d / 2 + 0.5), (w / 2 + 0.09, d / 2 - 0.6)):                        # drain pipes
        A.m("metal").cyl((px, py, 0), 0.06, top[2], n=6, cap=False)
    stilts(A, 0, 0, w, d)
    return A.finish()


def great_lantern():
    """Landmark: gasholder guide frame, 92 m x 38 m. The bell is a second asset so the engine can ride it on the bass."""
    R_, H_, N_, tiers = 19.0, 92.0, 14, 7
    F_ = Asset("great_lantern_frame")
    cols = [Vector((R_ * math.cos(i / N_ * 2 * math.pi), R_ * math.sin(i / N_ * 2 * math.pi), 0)) for i in range(N_)]
    for i in range(N_):
        a, b = cols[i], cols[(i + 1) % N_]
        F_.m("metal").cyl(tuple(a), 0.42, H_, n=8, cap=True)
        F_.m("metal").cyl(tuple(a), 0.75, 1.2, n=8); F_.m("metal").cyl(tuple(a + Vector((0, 0, H_ - 0.6))), 0.62, 0.9, n=8)
        ain = a * ((R_ - 1.5) / R_)                                                   # guide rail on the inside of each standard
        F_.m("trim").beam(ain + Vector((0, 0, 3)), ain + Vector((0, 0, H_ - 2)), 0.16)
        for t in range(tiers + 1):
            z = H_ * t / tiers
            big = t in (0, tiers)
            F_.m("metal").beam(a + Vector((0, 0, z)), b + Vector((0, 0, z)), 0.5 if big else 0.3, t2=0.7 if big else 0.42)
            if t < tiers:
                z2 = H_ * (t + 1) / tiers
                F_.m("metal").beam(a + Vector((0, 0, z + 0.3)), b + Vector((0, 0, z2 - 0.3)), 0.11)
                F_.m("metal").beam(b + Vector((0, 0, z + 0.3)), a + Vector((0, 0, z2 - 0.3)), 0.11)
        F_.m("strip").beam(a + Vector((0, 0, H_ + 0.55)), b + Vector((0, 0, H_ + 0.55)), 0.14)        # crown ring light
        # walkway at tier 2 with rail
        zc = H_ * 2 / tiers
        F_.m("trim").beam(a * 1.06 + Vector((0, 0, zc)), b * 1.06 + Vector((0, 0, zc)), 1.3, t2=0.1)
        F_.m("metal").beam(a * 1.095 + Vector((0, 0, zc + 1.05)), b * 1.095 + Vector((0, 0, zc + 1.05)), 0.05)
    F_.m("trim").cyl((0, 0, 0), 16.2, 3.2, n=28); F_.m("trim").cyl((0, 0, -0.6), 27.0, 0.6, n=28)        # tank wall and round deck
    for k in range(10):
        a = k / 10 * 6.283
        F_.m("metal").cyl((24 * math.cos(a), 24 * math.sin(a), -16), 0.4, 15.4, n=8, cap=False)
    F_.m("metal").cyl((0, 0, H_), 0.16, 18.0, n=6); F_.m("beacon").sphere((0, 0, H_ + 18.2), 0.3, n=8, m=5)
    for k in range(3):
        a = k / 3 * 6.283
        F_.m("metal").beam((0, 0, H_ + 8), (R_ * 0.55 * math.cos(a), R_ * 0.55 * math.sin(a), H_ + 0.4), 0.06)
        F_.m("metal").beam((0, 0, H_ + 0.4), (R_ * math.cos(a), R_ * math.sin(a), H_ + 0.4), 0.2)
    frame = F_.finish()
    B_ = Asset("great_lantern_bell")   # origin at the bell's base centre; rest height 16 m above the deck
    lifts, hh = 6, 50.0 / 6
    for b_ in range(lifts):
        rad = 15.4 - b_ * 0.22
        lvl = [0.97, 0.84, 0.72, 0.6, 0.5, 0.42][b_]
        mb = B_.m("landmark")
        n = 36
        for i in range(n):                                                 # each lift: a ring of lit panels with u = level
            a0, a1 = i / n * 6.283, (i + 1) / n * 6.283
            z0, z1 = b_ * hh + 0.3, (b_ + 1) * hh - 0.3
            p = [(rad * math.cos(a0), rad * math.sin(a0), z0), (rad * math.cos(a1), rad * math.sin(a1), z0),
                 (rad * math.cos(a1), rad * math.sin(a1), z1), (rad * math.cos(a0), rad * math.sin(a0), z1)]
            mb.quad(*p, uv=(lvl, 0.5, lvl, 0.5), double=False)
            if i % 3 == 0:
                B_.m("trim").beam((rad * 1.004 * math.cos(a0), rad * 1.004 * math.sin(a0), z0), (rad * 1.004 * math.cos(a0), rad * 1.004 * math.sin(a0), z1), 0.09)
        B_.m("trim").cyl((0, 0, b_ * hh - 0.3), rad + 0.25, 0.6, n=36)
    B_.m("landmark").cyl((0, 0, 50.0), 14.1, 4.6, n=36, r_top=3.5, cap=False)
    B_.m("trim").cyl((0, 0, 54.6), 3.5, 0.5, n=18)
    bell = B_.finish()
    return frame, bell


def export(ob, sub):
    d = os.path.join(OUT, sub); os.makedirs(d, exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    path = os.path.join(d, ob.name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True)
    return path


for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
mats()
manifest = []
specs = [("mooring_shophouse_a", 11, dict(floors=6, w=8.4, d=10.8, lit=0.34, warm=0.3)),
         ("mooring_shophouse_b", 23, dict(floors=8, w=9.6, d=10.8, lit=0.26, warm=0.22)),
         ("mooring_shophouse_c", 37, dict(floors=4, w=7.2, d=8.4, lit=0.42, warm=0.4)),
         ("mooring_stack_a", 51, dict(floors=13, w=9.6, d=9.6, lit=0.22, warm=0.18)),
         ("mooring_stack_b", 68, dict(floors=17, w=8.4, d=9.6, lit=0.16, warm=0.2))]
placed = []
for i, (name, seed, kw) in enumerate(specs):
    ob, tris = shophouse(name, seed, **kw)
    path = export(ob, "buildings")
    manifest.append({"asset": "buildings/" + name + ".glb", "tris": tris, "floors": kw["floors"], "footprint_m": [kw["w"], kw["d"]], "height_m": round(max(v.co.z for v in ob.data.vertices), 1)})
    ob.location = (-34 + i * 15.5, 0, 0); placed.append(ob)
(frame, ftris), (bell, btris) = great_lantern()
manifest.append({"asset": "landmarks/great_lantern_frame.glb", "tris": ftris, "height_m": 110.2, "diameter_m": 38})
manifest.append({"asset": "landmarks/great_lantern_bell.glb", "tris": btris, "height_m": 55.1, "rest_offset_y_m": 16.0, "ride_m": [14.5, 34.0]})
export(frame, "landmarks"); export(bell, "landmarks")
frame.location = (62, 30, 0); bell.location = (62, 30, 16.0)
json.dump({"units": "metres, Y-up in the GLB, origin at base centre at deck level",
           "materials": {"facade": "dark wall", "trim": "slabs, frames", "metal": "pipes, rails, stilts", "glass": "unlit panes; uv = (level, window hash)",
                         "win_warm": "lit panes and shop bays, emissive texture win_levels.png, strength 8", "win_cool": "lit panes, strength 8",
                         "strip": "eave lines, strength 12", "beacon": "mast light, strength 40", "landmark": "Great Lantern bell, strength 6"},
           "grid": {"floor_m": FLOOR, "bay_m": BAY}, "assets": manifest}, open(os.path.join(OUT, "kit_manifest.json"), "w"), indent=1)
print("KIT", json.dumps(manifest))

if PREVIEW:
    sc = bpy.context.scene
    for nm_, hexc in (("win_warm", "#ffb86b"), ("win_cool", "#d9ccff"), ("landmark", "#ff9be6")):   # preview only: tint the lights
        nt_ = MATS[nm_].node_tree; b_ = [n for n in nt_.nodes if n.type == 'BSDF_PRINCIPLED'][0]
        for l_ in list(b_.inputs['Emission Color'].links):
            nt_.links.remove(l_)
        b_.inputs['Emission Color'].default_value = (*hex2lin(hexc), 1); b_.inputs['Emission Strength'].default_value = 3.0
    w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.02, 0.024, 0.075, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    sun = bpy.data.lights.new("moon", 'SUN'); sun.energy = 0.6; sun.color = (0.53, 0.57, 1.0); so = link(bpy.data.objects.new("moon", sun))
    so.rotation_euler = (math.radians(58), 0, math.radians(140))
    bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, -14)); bpy.context.object.data.materials.append(MATS["facade"])
    for (lx, col) in ((-20, (1.0, 0.05, 0.45)), (10, (0.43, 0.03, 1.0)), (40, (1.0, 0.43, 0.12))):
        l = bpy.data.lights.new("p", 'POINT'); l.energy = 2500; l.color = col; l.shadow_soft_size = 1.0
        o = link(bpy.data.objects.new("p", l)); o.location = (lx, -9, 4)
    cam = bpy.data.cameras.new("c"); cam.lens = float(arg("--lens", 38)); co = link(bpy.data.objects.new("c", cam))
    cp = [float(x) for x in arg("--cam", "-6,-78,30").split(",")]; ct = [float(x) for x in arg("--tgt", "6,0,24").split(",")]
    co.location = cp
    co.rotation_euler = (Vector(ct) - Vector(cp)).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = co
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = int(arg("--spp", 48)); sc.cycles.use_denoising = True
    sc.render.resolution_x = 1280; sc.render.resolution_y = 720
    try:
        sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
    except Exception:
        pass
    sc.view_settings.exposure = float(arg("--ev", 0.8))
    sc.render.image_settings.file_format = 'PNG'; sc.render.filepath = PREVIEW
    bpy.ops.render.render(write_still=True)
