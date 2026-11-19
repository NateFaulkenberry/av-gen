"""Neon Dreams kit: the planting vocabulary of the Moorings and the garden pieces.

Plants are bioluminescent and original: the umbel tree is the signature; around it grow cushion shrubs with glow buds,
tidevine (hanging strands with lantern buds), sprigs (thin stems with a bud) and fan ferns. Dark foliage is the
`foliage` material; every light point is on `garden` with collapsed UVs (u = level, v = hash), so the scene supplies the
colour (Garden umbels A or B) and gain per instance.

Rule that keeps gardens and windows apart: on a facade, vines hang only at CELL BOUNDARIES (the piers between
windows) or stay shorter than the gap above the next window head; troughs on ledges stay below the sills.
"""
import math, random
from mathutils import Vector
from nd_kitcore import *  # noqa


def bud(A, c, size, r, lo=0.25):
    octa(A.m("garden"), tuple(c), size, lvl_uv(r, lo))


def shrub(A, r, c, rad=0.5, buds=8, h=1.0):
    """Cushion shrub: a few squashed low-poly balls of dark foliage, glow buds on top."""
    c = Vector(c)
    for k in range(r.choice([3, 4, 5])):
        a = r.uniform(0, 6.283); d = rad * r.uniform(0.0, 0.55)
        rr = rad * r.uniform(0.55, 0.85)
        A.m("foliage").sphere((c.x + d * math.cos(a), c.y + d * math.sin(a), c.z + rr * 0.55 * h), rr, n=7, m=4, squash=0.7 * h)
    for k in range(buds):
        a = r.uniform(0, 6.283); d = rad * math.sqrt(r.random()) * 0.9
        bud(A, (c.x + d * math.cos(a), c.y + d * math.sin(a), c.z + rad * h * r.uniform(0.75, 1.15)), r.uniform(0.035, 0.06), r)


def fern(A, r, c, h=0.8, blades=7):
    """Fan fern: narrow blades arching up and out."""
    c = Vector(c)
    for k in range(blades):
        a = k / blades * 6.283 + r.uniform(-0.3, 0.3); out = h * r.uniform(0.5, 0.9); up = h * r.uniform(0.7, 1.1)
        d = Vector((math.cos(a), math.sin(a), 0)); s = Vector((-d.y, d.x, 0)) * 0.07
        p0, p1, p2 = c, c + d * out * 0.5 + Vector((0, 0, up)), c + d * out + Vector((0, 0, up * 0.72))
        A.m("foliage").quad(tuple(p0 - s), tuple(p0 + s), tuple(p1 + s * 1.3), tuple(p1 - s * 1.3), double=True)
        A.m("foliage")._add([tuple(p1 - s * 1.3), tuple(p1 + s * 1.3), tuple(p2)], [(0, 1, 2)])


def sprigs(A, r, c, rad, n=12, h=(0.3, 0.9)):
    """Glow sprigs: thin stems with a bud. The ground cover of every garden."""
    c = Vector(c)
    for k in range(n):
        a = r.uniform(0, 6.283); d = rad * math.sqrt(r.random()); hh = r.uniform(*h)
        b = Vector((c.x + d * math.cos(a), c.y + d * math.sin(a), c.z))
        t = b + Vector((r.uniform(-0.08, 0.08), r.uniform(-0.08, 0.08), hh))
        A.m("foliage").beam(b, t, 0.018)
        bud(A, t, r.uniform(0.035, 0.065), r, 0.15)


def vine(A, r, top, length, sway=0.25, leaves=True):
    """One tidevine strand hanging from `top`: a drooping chain, leaf pairs, lantern buds, a larger bud at the tip."""
    p = Vector(top); n = max(3, int(length / 0.55)); dx, dy = r.uniform(-sway, sway), r.uniform(-sway, sway)
    for k in range(n):
        t = (k + 1) / n
        q = Vector(top) + Vector((dx * math.sin(t * 2.2), dy * math.sin(t * 1.7 + 0.5), -length * t))
        A.m("foliage").beam(p, q, 0.022)
        if leaves and k % 1 == 0:
            a = r.uniform(0, 6.283); s = Vector((math.cos(a), math.sin(a), 0)) * r.uniform(0.12, 0.2)
            A.m("foliage").quad(tuple(q), tuple(q + s + Vector((0, 0, -0.03))), tuple(q + s * 0.9 + Vector((0, 0, -0.2))), tuple(q + Vector((0, 0, -0.1))), double=True)
        if r.random() < 0.3:
            bud(A, q + Vector((r.uniform(-0.05, 0.05), r.uniform(-0.05, 0.05), -0.05)), r.uniform(0.03, 0.05), r, 0.2)
        p = q
    bud(A, p + Vector((0, 0, -0.08)), r.uniform(0.06, 0.09), r, 0.55)


def trough(A, r, c, length, yaw=0.0, depth=0.42, height=0.36, plant=True, vines=0, vine_len=(1.0, 2.6), low=True):
    """A planter trough along `yaw` with low shrubs (top stays under 0.8 m so it never rises into a window above)."""
    c = Vector(c); d = Vector((math.cos(yaw), math.sin(yaw), 0)); s = Vector((-d.y, d.x, 0))
    A.m("trim").box((c.x, c.y, c.z + height / 2), (length, depth, height), yaw)
    A.m("facade").box((c.x, c.y, c.z + height - 0.01), (length - 0.08, depth - 0.08, 0.04), yaw)
    if plant:
        k = -length / 2 + 0.3
        while k < length / 2 - 0.2:
            q = c + d * k + Vector((0, 0, height))
            if r.random() < 0.6:
                shrub(A, r, q, r.uniform(0.2, 0.27), buds=r.choice([3, 4, 6]), h=0.9 if low else 1.3)
            else:
                fern(A, r, q, r.uniform(0.3, 0.42), blades=6); sprigs(A, r, q, 0.15, n=2, h=(0.2, 0.4))
            k += r.uniform(0.45, 0.7)
    for k in range(vines):
        q = c + d * r.uniform(-length / 2 + 0.1, length / 2 - 0.1) + s * (depth / 2 + 0.02) * r.choice([-1, 1]) + Vector((0, 0, height * 0.6))
        vine(A, r, q, r.uniform(*vine_len), 0.12)


def umbel_tree(name, seed, height=9.0, radius=4.6, n_pts=120):
    """Garden tree of the Moorings: a thin trunk and a shallow dome of light points on fine rays."""
    r = random.Random(seed); A = Asset(name)
    lean = (r.uniform(-0.6, 0.6), r.uniform(-0.6, 0.6)); seg = 6; pts = []
    for i in range(seg + 1):
        t = i / seg
        pts.append(Vector((lean[0] * t * t, lean[1] * t * t, height * t)))
    for i in range(seg):
        A.m("trim").beam(pts[i], pts[i + 1], (0.3 - 0.16 * (i / seg)) * min(1.0, height / 7.0 + 0.2))
    top = pts[-1]
    for k in range(n_pts):
        u_ = (k + 0.5) / n_pts; th = math.acos(1 - u_ * 0.74); ph = k * 2.39996 + r.uniform(-0.1, 0.1)
        rr_ = radius * r.uniform(0.86, 1.06)
        q = Vector((top.x + rr_ * math.sin(th) * math.cos(ph), top.y + rr_ * math.sin(th) * math.sin(ph), top.z + rr_ * 0.55 * math.cos(th) - radius * 0.2))
        octa(A.m("garden"), tuple(q), r.uniform(0.07, 0.13) * min(1.0, radius / 4.0 + 0.25), lvl_uv(r, 0.15))
        if k % 4 == 0:
            A.m("trim").beam(top, q, 0.03)
    for k in range(5):                               # a few low buds on the trunk
        t = r.uniform(0.35, 0.8); a = r.uniform(0, 6.283); pt = Vector((lean[0] * t * t, lean[1] * t * t, height * t))
        q = pt + Vector((math.cos(a) * 0.9, math.sin(a) * 0.9, 0.5)) if height > 5 else pt + Vector((math.cos(a) * 0.4, math.sin(a) * 0.4, 0.25))
        A.m("trim").beam(pt, q, 0.03); octa(A.m("garden"), tuple(q), 0.09, lvl_uv(r, 0.1))
    A.finish()
    return A


def pergola(A, r, c, w, d, h=2.5, vines=6):
    """Open timber frame with tidevine and lantern buds: the roof-terrace and pocket-garden shelter."""
    c = Vector(c)
    for sx in (-1, 1):
        for sy in (-1, 1):
            A.m("trim").box((c.x + sx * (w / 2 - 0.08), c.y + sy * (d / 2 - 0.08), c.z + h / 2), (0.12, 0.12, h))
    for sy in (-1, 1):
        A.m("trim").box((c.x, c.y + sy * (d / 2 - 0.08), c.z + h), (w + 0.5, 0.1, 0.16))
    k = -w / 2 + 0.2
    while k <= w / 2 - 0.1:
        A.m("trim").box((c.x + k, c.y, c.z + h + 0.12), (0.07, d + 0.5, 0.1)); k += 0.55
    for k in range(vines):
        vine(A, r, (c.x + r.uniform(-w / 2, w / 2), c.y + r.uniform(-d / 2, d / 2), c.z + h + 0.05), r.uniform(0.5, 1.3), 0.1)


def bench(A, c, yaw=0.0, length=1.8):
    c = Vector(c); d = Vector((math.cos(yaw), math.sin(yaw), 0)); s = Vector((-d.y, d.x, 0))
    A.m("trim").box(tuple(c + Vector((0, 0, 0.43))), (length, 0.42, 0.06), yaw)
    A.m("trim").box(tuple(c + s * 0.2 + Vector((0, 0, 0.72))), (length, 0.05, 0.36), yaw)
    for k in (-1, 1):
        A.m("metal").box(tuple(c + d * k * (length / 2 - 0.12) + Vector((0, 0, 0.2))), (0.06, 0.4, 0.4), yaw)
        A.m("metal").box(tuple(c + d * k * (length / 2 - 0.12) + s * 0.2 + Vector((0, 0, 0.65))), (0.05, 0.05, 0.5), yaw)


def garden_kit():
    """Loose garden pieces for the layout: trees, planters, vines, beds, the stepped-terrace module, the pavilion."""
    out = []
    for nm, seed, hgt, rad, n in (("umbel_tree_a", 3, 9.5, 5.0, 130), ("umbel_tree_b", 4, 7.0, 4.0, 100), ("umbel_tree_c", 6, 11.0, 5.6, 150),
                                  ("umbel_tree_s", 9, 4.6, 2.3, 70), ("umbel_sapling", 12, 2.7, 1.25, 40)):
        out.append(umbel_tree(nm, seed, hgt, rad, n))
    for i, (L, vines) in enumerate(((2.2, 0), (2.2, 3), (4.4, 2))):
        A = Asset("planter_trough_%s" % "abc"[i]); r = random.Random(40 + i)
        trough(A, r, (0, 0, 0), L, vines=vines, low=False); A.finish(); out.append(A)
    A = Asset("planter_round"); r = random.Random(50)
    A.m("trim").cyl((0, 0, 0), 0.42, 0.5, n=10, r_top=0.52); shrub(A, r, (0, 0, 0.5), 0.5, buds=9, h=1.1); fern(A, r, (0.1, 0.05, 0.5), 0.7); A.finish(); out.append(A)
    for i, rad in enumerate((0.6, 0.9)):
        A = Asset("shrub_%s" % "ab"[i]); r = random.Random(60 + i); shrub(A, r, (0, 0, 0), rad, buds=10 + 6 * i, h=1.0); fern(A, r, (rad * 0.5, -rad * 0.3, 0), rad); A.finish(); out.append(A)
    A = Asset("sprig_bed_3m"); r = random.Random(70)        # glow ground cover: a low edging and ~40 sprigs with ferns
    A.m("trim").cyl((0, 0, 0), 1.5, 0.16, n=12); A.m("facade").cyl((0, 0, 0.13), 1.4, 0.05, n=12)
    sprigs(A, r, (0, 0, 0.17), 1.3, n=40); [fern(A, r, (r.uniform(-0.9, 0.9), r.uniform(-0.9, 0.9), 0.17), r.uniform(0.35, 0.6), 6) for _ in range(4)]
    A.finish(); out.append(A)
    for i, (w_, n_, ln) in enumerate(((2.4, 7, (1.2, 3.2)), (2.4, 9, (2.5, 6.0)), (1.0, 4, (0.6, 1.6)))):
        A = Asset("vine_curtain_%s" % "abc"[i]); r = random.Random(80 + i)    # hangs from an edge: origin on the edge, strands along x
        A.m("metal").box((0, 0, -0.02), (w_, 0.05, 0.04))
        for k in range(n_):
            vine(A, r, (-w_ / 2 + (k + 0.5) * w_ / n_ + r.uniform(-0.08, 0.08), 0, -0.03), r.uniform(*ln), 0.14)
        A.finish(); out.append(A)
    A = Asset("pergola_4m"); r = random.Random(90); pergola(A, r, (0, 0, 0), 4.0, 2.8, 2.6, vines=9); bench(A, (0, 0.6, 0), 0.0); A.finish(); out.append(A)
    A = Asset("garden_bench"); bench(A, (0, 0, 0)); A.finish(); out.append(A)
    A = Asset("tree_pit"); r = random.Random(95)             # a round raised bed for an umbel tree, with sprigs
    A.m("trim").cyl((0, 0, 0), 1.25, 0.4, n=12); A.m("facade").cyl((0, 0, 0.37), 1.12, 0.05, n=12); sprigs(A, r, (0, 0, 0.42), 1.0, n=14, h=(0.2, 0.5)); A.finish(); out.append(A)
    A = Asset("terrace_step_6m"); r = random.Random(97)      # the Steps: a planted tray, one of a flight of terraces 0.8 m apart
    A.m("deck").box((0, 0, -0.2), (6.0, 6.0, 0.4)); A.m("trim").box((0, -2.95, -0.6), (6.0, 0.1, 1.2))
    for sx in (-2.9, 2.9):
        A.m("metal").cyl((sx * 0.9, 0, -14), 0.24, 13.6, n=8, cap=False)
    A.m("trim").box((0, -2.55, 0.2), (6.0, 0.7, 0.4)); A.m("facade").box((0, -2.55, 0.39), (5.9, 0.6, 0.04))        # the downhill raised bed
    k = -2.6
    while k < 2.7:
        (shrub(A, r, (k, -2.55, 0.4), r.uniform(0.3, 0.42), buds=5) if r.random() < 0.55 else (fern(A, r, (k, -2.55, 0.4), 0.5), sprigs(A, r, (k, -2.55, 0.4), 0.25, n=4)))
        k += r.uniform(0.6, 0.95)
    for i in range(4):                                       # four risers down to the next terrace, on the +x side
        A.m("deck").box((2.3, -3.15 - i * 0.3, -0.1 - i * 0.2), (1.4, 0.3, 0.2))
    for kx in (-2.2, -0.6):
        vine(A, r, (kx, -3.01, -0.1), r.uniform(0.5, 0.75), 0.08)
    A.finish(); out.append(A)
    A = Asset("pavilion"); r = random.Random(99)             # the garden shelter: paper screens are panes in framed openings
    w_, d_, h_ = 7.2, 4.8, 3.0
    for sx in (-1, 1):
        for sy in (-1, 1):
            A.m("trim").box((sx * (w_ / 2 - 0.1), sy * (d_ / 2 - 0.1), h_ / 2), (0.16, 0.16, h_))
    A.m("trim").box((0, 0, h_ + 0.1), (w_ + 1.6, d_ + 1.6, 0.2)); A.m("strip").box((0, -d_ / 2 - 0.66, h_ - 0.05), (w_ + 1.0, 0.06, 0.05))
    A.m("deck").box((0, 0, 0.06), (w_, d_, 0.12))
    O, T, Nn = wall((-w_ / 2, d_ / 2 - 0.2, 0), (1, 0, 0))                     # back wall of three screens, facing the garden
    for b in range(3):
        u = b * BAY
        pane_opening(A, O, T, Nn, (u + 0.2, u + BAY - 0.2, 1.1, 2.6), (u, u + BAY, 0.12, h_), 0.07, "win_warm", 0.5 + 0.2 * r.random(), 0.2 + 0.3 * b, sill=False,
                     mullions=2, transom=1.85, kind="screen", clear=2.5)
    A.m("facade").box((0, d_ / 2 - 0.2 + 0.125, (h_ + 0.12) / 2), (w_, 0.08, h_ - 0.12))                                   # its back
    bench(A, (-1.6, 0.9, 0.12), 0.0); bench(A, (1.6, 0.9, 0.12), 0.0)
    A.finish(); out.append(A)
    return out
