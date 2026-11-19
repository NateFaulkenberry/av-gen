"""Neon Dreams kit: the Condensery, a cloudworks. Few forms, each very large; almost no windows; sodium amber, muted red
and a few blue-white work lights. Seen from 1.2 km or more in the POC, so the silhouettes carry it.

Light roles (always the Condensery's own colours, never the Mooring's palette state):
  cond_glow  throats, vent bands, the flare      cond_lamp  sodium lamps      cond_work  blue-white work lights
"""
import math, random
from mathutils import Vector
from nd_kitcore import *  # noqa


def _set_uv(mb, b0, level, hsh):
    u = (lvl(level), hsh)
    for i in range(b0, len(mb.uv)):
        mb.uv[i] = u


def _ring_quads(mb, r0, z0, r1, z1, n, level=None, hsh=0.5, inward=False):
    for i in range(n):
        a0, a1 = i / n * 6.2832, (i + 1) / n * 6.2832
        p = [(r0 * math.cos(a0), r0 * math.sin(a0), z0), (r0 * math.cos(a1), r0 * math.sin(a1), z0), (r1 * math.cos(a1), r1 * math.sin(a1), z1), (r1 * math.cos(a0), r1 * math.sin(a0), z1)]
        if inward:
            p.reverse()
        uv = None if level is None else [(lvl(level), hsh)] * 4
        mb._add(p, [(0, 1, 2, 3)], None, uv)


def cond_stack(name, H, R, seed):
    """Hyperboloid stack with a glowing throat. Origin at the base centre on the platform."""
    r = random.Random(seed); A = Asset(name); n = 24
    rw, zw = 0.6 * R, 0.66 * H; c = zw / math.sqrt((R / rw) ** 2 - 1)
    rad = lambda z: rw * math.sqrt(1 + ((z - zw) / c) ** 2)
    leg = 7.0
    for i in range(n):                                                       # the open lattice base: diagonal columns
        a0, a1 = i / n * 6.2832, (i + 1) / n * 6.2832
        A.m("metal").beam((R * 1.02 * math.cos(a0), R * 1.02 * math.sin(a0), 0), (rad(leg) * math.cos(a1), rad(leg) * math.sin(a1), leg), 0.5)
        A.m("metal").beam((R * 1.02 * math.cos(a1), R * 1.02 * math.sin(a1), 0), (rad(leg) * math.cos(a0), rad(leg) * math.sin(a0), leg), 0.5)
    b0 = len(A.m("cond_glow").uv); A.m("cond_glow").cyl((0, 0, 0.3), R * 0.7, 0.2, n=n); _set_uv(A.m("cond_glow"), b0, 0.3, 0.1)      # the basin glows through the legs
    segs = 14; zs = [leg + (H - 2.6 - leg) * k / segs for k in range(segs + 1)]
    for k in range(segs):
        _ring_quads(A.m("facade"), rad(zs[k]), zs[k], rad(zs[k + 1]), zs[k + 1], n)
    A.m("trim").cyl((0, 0, leg - 0.4), rad(leg) + 0.5, 0.8, n=n, cap=False)
    _ring_quads(A.m("cond_glow"), rad(H - 2.6), H - 2.6, rad(H), H, n, 0.55, 0.5)                    # the hot rim, seen from every side
    _ring_quads(A.m("cond_glow"), rad(H) - 0.6, H, rad(H - 9) - 1.2, H - 9, n, 0.8, 0.6, inward=True)   # the throat's inner wall
    b0 = len(A.m("cond_glow").uv); A.m("cond_glow").cyl((0, 0, H - 9.2), rad(H - 9) - 1.2, 0.2, n=n); _set_uv(A.m("cond_glow"), b0, 1.0, 0.7)
    _ring_quads(A.m("trim"), rad(H), H, rad(H) - 0.6, H, n)
    for zf in (0.44, 0.8):                                                   # ring walkways with rails
        z = H * zf; rr_ = rad(z)
        _ring_quads(A.m("trim"), rr_, z, rr_ + 2.0, z, n); _ring_quads(A.m("trim"), rr_ + 2.0, z - 0.3, rr_, z - 0.3, n); _ring_quads(A.m("trim"), rr_ + 2.0, z - 0.3, rr_ + 2.0, z, n)
        for i in range(n):
            a0, a1 = i / n * 6.2832, (i + 1) / n * 6.2832
            A.m("metal").beam(((rr_ + 1.95) * math.cos(a0), (rr_ + 1.95) * math.sin(a0), z + 1.1), ((rr_ + 1.95) * math.cos(a1), (rr_ + 1.95) * math.sin(a1), z + 1.1), 0.08)
            if i % 4 == 0:
                lamp(A.m("cond_lamp"), ((rr_ + 1.9) * math.cos(a0), (rr_ + 1.9) * math.sin(a0), z + 1.6), 0.28, 0.6 + 0.4 * r.random(), r.random(), n=6, m=4)
    zb = H * 0.56                                                            # a band of glowing louvres at the waist
    for i in range(n):
        if i % 3 == 2:
            continue
        a0, a1 = (i + 0.2) / n * 6.2832, (i + 0.8) / n * 6.2832; rr_ = rad(zb) + 0.06
        p = [(rr_ * math.cos(a0), rr_ * math.sin(a0), zb), (rr_ * math.cos(a1), rr_ * math.sin(a1), zb), (rr_ * math.cos(a1), rr_ * math.sin(a1), zb + 3.4), (rr_ * math.cos(a0), rr_ * math.sin(a0), zb + 3.4)]
        A.m("cond_glow")._add(p, [(0, 1, 2, 3)], None, [(lvl(0.3 + 0.35 * r.random()), r.random())] * 4)
    for k in range(8):                                                       # rim lamps and aviation beacons
        a = k / 8 * 6.2832
        lamp(A.m("cond_lamp"), ((rad(H) + 0.4) * math.cos(a), (rad(H) + 0.4) * math.sin(a), H + 0.5), 0.4, 0.9, k / 8, n=6, m=4)
    for k in range(4):
        a = k / 4 * 6.2832 + 0.4
        A.m("beacon").sphere(((rad(zw) + 0.3) * math.cos(a), (rad(zw) + 0.3) * math.sin(a), zw), 0.35, n=6, m=4)
    a = 0.9                                                                  # the ladder cage, a straight line up the shell
    for k in range(segs):
        A.m("metal").beam(((rad(zs[k]) + 0.5) * math.cos(a), (rad(zs[k]) + 0.5) * math.sin(a), zs[k]), ((rad(zs[k + 1]) + 0.5) * math.cos(a), (rad(zs[k + 1]) + 0.5) * math.sin(a), zs[k + 1]), 0.5, t2=0.5)
    A.meta = dict(height_m=H, base_radius_m=R, throat_radius_m=round(rad(H), 1), plume_origin_m=[0.0, H + 2.0, 0.0])
    A.finish()
    return A


def cond_sphere_tank():
    A = Asset("cond_sphere_tank")
    A.m("facade").sphere((0, 0, 17), 14.0, n=20, m=12)
    for k in range(8):
        a = k / 8 * 6.2832
        A.m("metal").cyl((10.5 * math.cos(a), 10.5 * math.sin(a), 0), 0.5, 13.0, n=6, cap=False)
        a1 = (k + 1) / 8 * 6.2832
        A.m("metal").beam((10.5 * math.cos(a), 10.5 * math.sin(a), 1.0), (10.5 * math.cos(a1), 10.5 * math.sin(a1), 9.0), 0.2)
    _ring_quads(A.m("trim"), 14.0, 17.0, 15.8, 17.0, 20); _ring_quads(A.m("trim"), 15.8, 16.7, 14.0, 16.7, 20); _ring_quads(A.m("trim"), 15.8, 16.7, 15.8, 17.0, 20)
    for k in range(20):
        a0, a1 = k / 20 * 6.2832, (k + 1) / 20 * 6.2832
        A.m("metal").beam((15.7 * math.cos(a0), 15.7 * math.sin(a0), 18.1), (15.7 * math.cos(a1), 15.7 * math.sin(a1), 18.1), 0.08)
    A.m("metal").cyl((0, 0, 31.0), 0.12, 2.6, n=6); lamp(A.m("cond_work"), (0, 0, 33.8), 0.5, 1.0, 0.5, n=6, m=4)
    A.m("metal").beam((14.5, 0, 17.0), (20.0, 0, 0.0), 1.0, t2=0.12)
    A.finish()
    return A


def cond_flare_mast(H=160.0):
    A = Asset("cond_flare_mast"); lv = 14
    for k in range(4):
        a = k / 4 * 6.2832 + 0.4
        A.m("metal").beam((5 * math.cos(a), 5 * math.sin(a), 0), (0.8 * math.cos(a), 0.8 * math.sin(a), H), 0.8)
    for k in range(lv):
        z0, z1 = k * H / lv, (k + 1) * H / lv
        r0, r1 = 5 - 4.2 * k / lv, 5 - 4.2 * (k + 1) / lv
        for j in range(4):
            a0, a1 = j / 4 * 6.2832 + 0.4, (j + 1) / 4 * 6.2832 + 0.4
            A.m("metal").beam((r0 * math.cos(a0), r0 * math.sin(a0), z0), (r1 * math.cos(a1), r1 * math.sin(a1), z1), 0.3)
            A.m("metal").beam((r1 * math.cos(a0), r1 * math.sin(a0), z1), (r1 * math.cos(a1), r1 * math.sin(a1), z1), 0.24)
        if k in (4, 9):
            for j in range(4):
                a0 = j / 4 * 6.2832 + 0.4
                A.m("beacon").sphere((r1 * 1.1 * math.cos(a0), r1 * 1.1 * math.sin(a0), z1), 0.4, n=6, m=4)
    A.m("metal").cyl((0, 0, H), 1.4, 2.5, n=10, r_top=1.9)
    mb = A.m("cond_glow"); b0 = len(mb.uv); mb.sphere((0.4, 0.2, H + 6.4), 3.2, n=10, m=7, squash=1.7); _set_uv(mb, b0, 1.0, 0.5)   # the pilot flame
    b0 = len(mb.uv); mb.sphere((1.3, 0.6, H + 11.5), 1.7, n=8, m=5, squash=1.6); _set_uv(mb, b0, 0.7, 0.6)
    A.meta = dict(height_m=H, flame_m=[0.4, H + 6.4, -0.2]); A.finish()
    return A


def cond_crane():
    """Gantry crane: two A-legs to 74 m and a 170 m boom that reaches out over the cloud. Boom along +x."""
    A = Asset("cond_crane")
    for sy in (-18, 18):
        for sx in (-9, 9):
            A.m("metal").beam((sx, sy, 0), (sx * 0.25, sy * 0.4, 74), 1.6)
        A.m("metal").beam((-9, sy, 0.5), (9, sy, 0.5), 1.0); A.m("metal").beam((-6.5, sy * 0.8, 25), (6.5, sy * 0.8, 25), 0.7)
        A.m("metal").beam((-4, sy * 0.6, 50), (4, sy * 0.6, 50), 0.6)
    A.m("metal").beam((0, -7.2, 74), (0, 7.2, 74), 2.0)
    a, b = Vector((-30, 0, 74)), Vector((140, 0, 80))
    for off in (-1.8, 1.8):                                                  # boom chords and lacing
        A.m("metal").beam(a + Vector((0, off, 0)), b + Vector((0, off, 0)), 0.7); A.m("metal").beam(a + Vector((0, off, 5)), b + Vector((0, off, 3)), 0.5)
    for k in range(17):
        t0, t1 = k / 17, (k + 1) / 17
        for off in (-1.8, 1.8):
            p0 = a.lerp(b, t0) + Vector((0, off, 0)); p1 = a.lerp(b, t1) + Vector((0, off, 5 - 2 * t1))
            A.m("metal").beam(p0, p1, 0.22)
        A.m("metal").beam(a.lerp(b, t1) + Vector((0, -1.8, 0)), a.lerp(b, t1) + Vector((0, 1.8, 0)), 0.2)
    A.m("metal").beam((0, 0, 104), (0, 0, 74), 1.0); A.m("metal").beam((0, 0, 104), tuple(b + Vector((0, 0, 3))), 0.16); A.m("metal").beam((0, 0, 104), tuple(a + Vector((0, 0, 5))), 0.16)
    A.m("beacon").sphere((0, 0, 104.6), 0.5, n=6, m=4)
    O, T, Nn = wall((-6.0, -3.0, 0), (1, 0, 0))                               # the machinery house on the boom root, one pane in an opening
    pane_opening(A, O, T, Nn, (1.2, 4.8, 80.6, 82.2), (0, 6.0, 79.4, 83.4), 0.14, "win_warm", 0.8, 0.5, sill=False, mullions=2, kind="cabin", clear=1.5, fill_mat="trim")
    for (o_, t_, L_) in (((0.0, -3.0, 0), (0, 1, 0), 6.0), ((0.0, 3.0, 0), (-1, 0, 0), 6.0), ((-6.0, 3.0, 0), (0, -1, 0), 6.0)):
        O2, T2, N2 = wall(o_, t_); wq(A, "trim", O2, T2, N2, 0, L_, 79.4, 83.4)
    A.m("trim").box((-3.0, 0, 83.5), (6.6, 6.6, 0.2)); A.m("trim").box((-3.0, 0, 79.3), (6.2, 6.2, 0.2))
    hx = 100.0; hz = a.lerp(b, (hx + 30) / 170).z                             # trolley, cable, hook block with a work light
    A.m("metal").box((hx, 0, hz - 0.6), (4.0, 4.4, 1.2)); A.m("metal").cyl((hx, 0, hz - 52), 0.12, 51.4, n=5, cap=False)
    A.m("metal").box((hx, 0, hz - 53), (1.6, 1.6, 2.0)); lamp(A.m("cond_work"), (hx, 0, hz - 54.6), 0.6, 1.0, 0.3, n=6, m=4)
    lamp(A.m("cond_work"), (140, 0, 77.2), 0.6, 1.0, 0.7, n=6, m=4)
    A.meta = dict(boom_tip_m=[140.0, 80.0, 0.0], height_m=104.0); A.finish()
    return A


def cond_shed(name, seed, L=33.6, W=14.4, floors=3):
    """A long shed: sparse amber windows in openings, a gabled roof with a ridge vent that glows a little."""
    r = random.Random(seed); r2 = random.Random(seed + 1); A = Asset(name)
    block(A, r, r2, 0, 0, 0.0, L, W, floors, 0.16, 1.0, seed, blank_p=0.55, pipes=False)
    z = floors * FLOOR; rise = 3.4
    for sy in (-1, 1):
        p = [(-L / 2 - 0.6, sy * (W / 2 + 0.6), z - 0.1), (L / 2 + 0.6, sy * (W / 2 + 0.6), z - 0.1), (L / 2 + 0.6, 0, z + rise), (-L / 2 - 0.6, 0, z + rise)]
        A.m("trim").quad(*(p if sy < 0 else list(reversed(p))), double=True)
    for sx in (-1, 1):
        p = [(sx * L / 2, -W / 2, z), (sx * L / 2, W / 2, z), (sx * L / 2, 0, z + rise)]
        A.m("facade")._add(p if sx > 0 else list(reversed(p)), [(0, 1, 2)])
    A.m("metal").box((0, 0, z + rise + 0.5), (L * 0.8, 1.6, 1.0))
    mb = A.m("cond_glow"); b0 = len(mb.uv); mb.box((0, 0, z + rise + 0.5), (L * 0.78, 1.66, 0.3)); _set_uv(mb, b0, 0.25, 0.4)
    for k in range(3):
        A.m("metal").cyl((-L / 2 + (k + 0.5) * L / 3, W * 0.2, z + rise * 0.5), 0.7, 5.0, n=8)
    A.m("trim").box((0, 0, -0.3), (L + 1.0, W + 1.0, 0.6))
    A.finish()
    return A


def cond_conveyor_span():
    """20 m of the conveyor pier that points back at the Mooring and stops. Along x."""
    A = Asset("cond_conveyor_span")
    A.m("deck").box((0, 0, -0.3), (20.0, 5.0, 0.6))
    for sy in (-2.3, 2.3):
        A.m("metal").beam((-10, sy, 0), (10, sy, 0), 0.3); A.m("metal").beam((-10, sy, 3.0), (10, sy, 3.0), 0.3)
        for k in range(4):
            x0 = -10 + k * 5.0
            A.m("metal").beam((x0, sy, 0), (x0 + 5.0, sy, 3.0), 0.16); A.m("metal").beam((x0, sy, 0), (x0, sy, 3.0), 0.16)
    A.m("trim").box((0, 0, 1.0), (20.0, 2.0, 0.5))
    lamp(A.m("cond_lamp"), (0, 2.3, 3.5), 0.32, 0.8, 0.5, n=6, m=4)
    A.m("metal").cyl((0, 0, -150), 0.55, 149.4, n=6, cap=False); A.m("metal").beam((0, -2.3, -0.6), (0, 0, -14), 0.3); A.m("metal").beam((0, 2.3, -0.6), (0, 0, -14), 0.3)
    A.finish()
    return A


def cond_platform():
    """The Condensery's deck, 240 x 130 m, on eighteen legs that go 190 m down into its cloud column."""
    A = Asset("cond_platform"); r = random.Random(3)
    A.m("deck").box((0, 0, -0.6), (240, 130, 1.2)); A.m("trim").box((0, 0, -2.0), (236, 126, 1.6))
    for (sx, sy, lw, ld) in ((0, -65, 240, 0.8), (0, 65, 240, 0.8), (-120, 0, 0.8, 130), (120, 0, 0.8, 130)):
        A.m("trim").box((sx, sy, -1.0), (lw, ld, 2.6))
    xs = [-110 + i * 44 for i in range(6)]; ys = [-55, 0, 55]
    for x in xs:
        for y in ys:
            A.m("metal").cyl((x, y, -190), 1.4, 187.5, n=8, cap=False)
    for y in ys:
        for x0, x1 in zip(xs[:-1], xs[1:]):
            for zz in (-12.0, -60.0):
                A.m("metal").beam((x0, y, zz), (x1, y, zz - 36), 0.5); A.m("metal").beam((x1, y, zz), (x0, y, zz - 36), 0.5)
            A.m("metal").beam((x0, y, -50), (x1, y, -50), 0.7)
    for k in range(22):                                                      # keel lamps under the deck light the column red
        lamp(A.m("keel"), (r.uniform(-105, 105), r.uniform(-55, 55), -r.uniform(8, 30)), 0.5, r.uniform(0.5, 1.0), r.random(), n=6, m=4)
    A.finish()
    return A


def cond_lamp_post():
    A = Asset("cond_lamp_post")
    A.m("metal").cyl((0, 0, 0), 0.16, 10.0, n=6, r_top=0.1); A.m("metal").beam((0, 0, 9.8), (1.4, 0, 10.3), 0.1)
    mb = A.m("cond_lamp"); b0 = len(mb.uv); mb.box((1.5, 0, 10.15), (0.9, 0.5, 0.3)); _set_uv(mb, b0, 1.0, 0.5)
    A.finish()
    return A


def condensery_kit():
    return [cond_stack("cond_stack_a", 118.0, 25.0, 1), cond_stack("cond_stack_b", 146.0, 29.0, 2), cond_stack("cond_stack_c", 100.0, 23.0, 3),
            cond_sphere_tank(), cond_flare_mast(), cond_crane(), cond_shed("cond_shed_a", 71), cond_shed("cond_shed_b", 73, L=26.4, W=12.0, floors=4),
            cond_conveyor_span(), cond_platform(), cond_lamp_post()]
