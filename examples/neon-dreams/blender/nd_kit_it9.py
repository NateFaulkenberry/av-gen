"""Neon Dreams kit, iteration 9 (owner msgs 73, 78): what the built-out world needs that the kit did not have.

  garden/root_curtain_a, _b: hanging roots and tidevine under a floating rock island (owner msg 78: "sculpted, eroded
      undersides with hanging roots/vines"). Built at the size of a 60 m island's underside: a patch about 34 m across
      of roots 6 to 30 m long, thick at the top and thinning, some forked; tidevine strands among them; bioluminescent
      buds on the `garden` role (the scene colours them, garden A or B) clustered toward the tips. Scale with the island.
  landmarks/mooring_mast: the observation landmark at Lanternhill's crest. A Lanternwright lattice mast 128 m high, four
      raked legs braced in bays, three galleries (each a ring deck with a rail and a light line under it), a lantern drum
      at the top with a mooring cone and a red beacon; tidevine climbing the lower legs. Lights on `strip` (gallery lines),
      `lantern` (bulbs on the ring rails and the drum), `beacon`. No panes (no window audit needed).
  far/drowned_mast: the enormous silhouette (brief: "an unidentified enormous structure far off"). Something like the
      mast of a ship drowned in the cloud sea, 3.2 km high, leaning: a tapering spar, three yards at odd angles (the low
      one broken and hanging), rigging falling from the yards into the sea, a crow's nest, a few dim lights. Its own
      materials (`drown_line` dim emission, `drown_light`), not light roles: the scene keeps them as baked.
"""
import math, random
from mathutils import Vector
from nd_kitcore import *  # noqa
import nd_kit_far as F


def _strand(A, r, top, length, thick, sway, mat="foliage", forks=True):
    """A root: a tapering bent tube of 5 to 7 segments hanging from top; sometimes a fork; returns the tip."""
    n = r.randint(5, 7); p = Vector(top); ph = r.uniform(0, 6.28); dx, dy = math.cos(ph) * sway, math.sin(ph) * sway
    pts = [p]
    for k in range(1, n + 1):
        t = k / n
        pts.append(Vector(top) + Vector((dx * math.sin(t * 2.0) + r.uniform(-0.3, 0.3), dy * math.sin(t * 1.6 + 0.4) + r.uniform(-0.3, 0.3), -length * t)))
    for k in range(n):
        t0, t1 = k / n, (k + 1) / n
        A.m(mat).beam(tuple(pts[k]), tuple(pts[k + 1]), thick * (1 - 0.8 * t0) * 2, t2=thick * (1 - 0.8 * t1) * 2)
    if forks and length > 10 and r.random() < 0.55:
        j = r.randint(2, n - 2); _strand(A, r, tuple(pts[j]), length * r.uniform(0.3, 0.5), thick * 0.45, sway * 1.4, mat, forks=False)
    return pts


def root_curtain(name, seed, n_roots=34, radius=17.0, lmin=6.0, lmax=30.0):
    A = Asset(name); r = random.Random(seed)
    for k in range(n_roots):
        a = r.uniform(0, 6.283); d = radius * math.sqrt(r.random())
        top = (d * math.cos(a), d * math.sin(a), r.uniform(-1.5, 0.5))
        L = r.uniform(lmin, lmax) * (1.0 - 0.45 * d / radius)                     # the long ones near the keel
        pts = _strand(A, r, top, L, r.uniform(0.18, 0.42), r.uniform(0.6, 2.4))
        nb = r.randint(2, 6)
        for b in range(nb):                                                     # buds toward the tip
            t = 1.0 - (b / nb) ** 1.3 * 0.45; i = min(len(pts) - 1, int(t * (len(pts) - 1)))
            c = pts[i] + Vector((r.uniform(-0.25, 0.25), r.uniform(-0.25, 0.25), 0))
            lamp(A.m("garden"), tuple(c), r.uniform(0.14, 0.3) * (1.6 if b == 0 else 1.0), r.uniform(0.55, 1.0), r.random(), n=6, m=4)
    for k in range(int(n_roots * 0.6)):                                        # tidevine among the roots: thin, many small buds
        a = r.uniform(0, 6.283); d = radius * 1.05 * math.sqrt(r.random())
        top = Vector((d * math.cos(a), d * math.sin(a), 0.0)); L = r.uniform(3.0, 12.0); m = max(3, int(L / 1.2))
        p = top
        for j in range(m):
            q = top + Vector((0.4 * math.sin(j * 0.9 + k), 0.4 * math.cos(j * 0.7 + k), -L * (j + 1) / m))
            A.m("foliage").beam(tuple(p), tuple(q), 0.07)
            if j % 2 == 1:
                lamp(A.m("garden"), tuple(q), 0.09, r.uniform(0.35, 0.8), r.random(), n=5, m=3)
            p = q
    A.meta = {"what": "hanging roots and tidevine for the underside of a floating rock island, sized for a 60 m island; scale with it", "patch_m": radius * 2, "depth_m": lmax}
    A.finish()
    return A


def mooring_mast():
    A = Asset("mooring_mast"); r = random.Random(128)
    H, B0, B1 = 128.0, 7.0, 2.2                                               # height, half-width at the foot and at the top
    leg = lambda i, z: Vector(((1 if i in (0, 3) else -1) * (B0 + (B1 - B0) * z / H), (1 if i in (0, 1) else -1) * (B0 + (B1 - B0) * z / H), z))
    bays = 12
    for i in range(4):
        A.m("metal").beam(tuple(leg(i, 0)), tuple(leg(i, H)), 0.55, t2=0.55)
        for b in range(bays):
            z0, z1 = H * b / bays, H * (b + 1) / bays
            j = (i + 1) % 4
            A.m("metal").beam(tuple(leg(i, z1)), tuple(leg(j, z1)), 0.22)
            A.m("metal").beam(tuple(leg(i, z0)), tuple(leg(j, z1)), 0.12)
            A.m("metal").beam(tuple(leg(j, z0)), tuple(leg(i, z1)), 0.12)
    A.m("metal").beam((0, 0, 0), (0, 0, H), 0.9)                              # the lift shaft
    for gz, gr in ((44.0, 9.5), (82.0, 7.5), (112.0, 6.0)):                    # galleries
        A.m("trim").cyl((0, 0, gz - 0.35), gr, 0.35, n=24)
        A.m("strip").cyl((0, 0, gz - 0.6), gr - 0.15, 0.18, n=24, cap=False)
        A.m("metal").cyl((0, 0, gz + 1.0), gr, 0.06, n=24, cap=False)
        for k in range(24):
            a = k / 24 * 6.283
            A.m("metal").beam((gr * math.cos(a), gr * math.sin(a), gz), (gr * math.cos(a), gr * math.sin(a), gz + 1.05), 0.05)
            if k % 3 == 0:
                lamp(A.m("lantern"), (gr * math.cos(a), gr * math.sin(a), gz + 1.15), 0.14, 0.9, r.random(), n=6, m=4)
        A.m("trim").cyl((0, 0, gz), 3.2, 3.0, n=12)                          # a small cabin round the shaft
        for k in range(8):
            a = k / 8 * 6.283; lamp(A.m("lantern"), (3.25 * math.cos(a), 3.25 * math.sin(a), gz + 1.6), 0.22, 0.75, r.random(), n=6, m=4)
    # the lantern drum and the mooring cone at the top
    A.m("trim").cyl((0, 0, H), 4.2, 0.5, n=20)
    for k in range(20):
        a0 = k / 20 * 6.283
        A.m("metal").beam((4.0 * math.cos(a0), 4.0 * math.sin(a0), H + 0.5), (4.0 * math.cos(a0), 4.0 * math.sin(a0), H + 6.0), 0.12)
    b0 = len(A.m("lantern").uv)
    A.m("lantern").cyl((0, 0, H + 0.6), 3.7, 5.2, n=20, cap=False)
    for i in range(b0, len(A.m("lantern").uv)):
        A.m("lantern").uv[i] = (lvl(0.62), 0.37)
    A.m("trim").cyl((0, 0, H + 6.0), 4.4, 0.6, n=20, r_top=1.2)
    A.m("metal").cyl((0, 0, H + 6.6), 0.9, 6.0, n=10, r_top=0.25)              # the mooring cone
    A.m("metal").beam((0, 0, H + 9.5), (6.5, 0, H + 8.0), 0.18)               # the arm a ship ties to
    lamp(A.m("beacon"), (6.6, 0, H + 8.0), 0.35, 1.0, 0.5, n=8, m=5)
    lamp(A.m("beacon"), (0, 0, H + 12.8), 0.4, 1.0, 0.5, n=8, m=5)
    # tidevine climbing the lower legs
    for i in range(4):
        for k in range(28):
            z = r.uniform(1, 40); p = leg(i, z); q = p + Vector((r.uniform(-0.6, 0.6), r.uniform(-0.6, 0.6), -r.uniform(1.0, 3.5)))
            A.m("foliage").beam(tuple(p), tuple(q), 0.08)
            if k % 2 == 0:
                lamp(A.m("garden"), tuple(q), 0.12, r.uniform(0.4, 0.9), r.random(), n=5, m=3)
    A.meta = {"what": "the Mooring Mast: Lanternhill's observation landmark", "height_m": H + 13, "galleries_m": [44, 82, 112]}
    A.finish()
    return A


def drowned_mast():
    F._mat("drown_line", (0.003, 0.003, 0.006), (0.30, 0.26, 0.60), 0.08)
    F._mat("drown_light", (0.01, 0.01, 0.01), (0.85, 0.70, 1.0), 0.6)
    A = Asset("drowned_mast"); r = random.Random(3200)
    H = 3200.0; lean = math.radians(5.5)
    spar = lambda z: Vector((math.sin(lean) * z, 0.0, math.cos(lean) * z - 260.0))
    seg = 16
    for k in range(seg):
        z0, z1 = H * k / seg, H * (k + 1) / seg; w0, w1 = 70.0 * (1 - 0.75 * z0 / H), 70.0 * (1 - 0.75 * z1 / H)
        A.m("drown_line").beam(tuple(spar(z0)), tuple(spar(z1)), w0, t2=w1)
        if k % 3 == 1:
            A.m("drown_line").cyl(tuple(spar(z0)), w0 * 0.75, 30.0, n=10)                       # bands round the spar
    # yards: (height, half-length, tilt deg, broken)
    tips = []
    for (z, half, tilt, broken) in ((2950.0, 520.0, -6.0, False), (2450.0, 860.0, 4.0, False), (1850.0, 1150.0, -2.0, True)):
        c = spar(z); t = math.radians(tilt); d = Vector((0, math.cos(t), math.sin(t)))
        a, b = c - d * half, c + d * half
        if broken:                                                              # the low yard has snapped on one side and hangs
            b = c + d * half * 0.35
            hang = b + Vector((80.0, 260.0, -820.0))
            A.m("drown_line").beam(tuple(b), tuple(hang), 34.0, t2=22.0)
            tips.append(hang)
        A.m("drown_line").beam(tuple(a), tuple(b), 38.0, t2=30.0)
        tips += [a, b]
        for e in (a, b):
            A.m("drown_light").box(tuple(e + Vector((0, 0, 30))), (22.0, 22.0, 22.0))
    A.m("drown_line").cyl(tuple(spar(3050.0)), 110.0, 40.0, n=12)              # the crow's nest
    A.m("drown_light").box(tuple(spar(3090.0)), (26.0, 26.0, 26.0))
    # rigging: lines from the yard ends and the top falling into the sea, sagging
    for tip in tips + [spar(H)]:
        for k in range(r.randint(1, 3)):
            end = Vector((tip.x + r.uniform(-600, 600), tip.y + r.uniform(-900, 900), -120.0))
            prev = tip
            for j in range(1, 9):
                t = j / 8.0
                q = tip.lerp(end, t) + Vector((0, 0, -math.sin(t * math.pi) * 220.0))
                A.m("drown_line").beam(tuple(prev), tuple(q), 9.0); prev = q
    A.meta = {"what": "the drowned mast: the enormous unidentified silhouette 9 km out", "height_m": H, "materials_note": "drown_line and drown_light keep their baked emission (0.08, 0.6): no scene role"}
    A.finish()
    return A


def garden_extra():
    return [root_curtain("root_curtain_a", 901), root_curtain("root_curtain_b", 902, n_roots=26, radius=12.0, lmin=4.0, lmax=18.0)]


# ============================================================================================ towers (owner msg 85)
# "Anything we can add for tower variety": the Stacks and the new districts read as many similar stacked-block columns.
# Seven tower types in the Lanternwright language, each built from the kit's own windowed blocks (nd_kitcore.block: every
# pane in a modelled opening; the audit runs on them like every other building) and the shared surface atlas, so they
# differ in massing, width, crown and roofline, not in material: a slender lantern spire, a setback tower with garden
# terraces, twin towers joined by sky-bridges, a leaning tower whose blocks step out over the street, a lighthouse-crowned
# tower (top-heavy: a gallery and a lantern drum), a mast tower with dishes and a beacon, and a hanging-garden tower.
import nd_kit_mooring as KM
from nd_kit_garden import vine, trough, pergola, bench, bud, sprigs


def _stack(A, r, r2, specs, lit, warm, seed, garden=0.6, eave=True, sides=(0, 1, 2, 3)):
    """specs: [(cx, cy, w, d, floors)] bottom to top; a shop floor first. Eave slabs, eave lines, planted ledges. Returns meta blocks and the top."""
    z = 0.0; metas = []; blocks = []
    for k, (cx, cy, w, d, n) in enumerate(specs):
        bal = block(A, r, r2, cx, cy, z, w, d, n, lit, warm, seed + z, balcony_side=None if k == 0 else r.choice([0, 0, 2, None]), shop=(k == 0), stair=3, sides_on=sides)
        metas.append(dict(c=[round(cx, 2), round(cy, 2)], z0=round(z, 2), floors=n, w=w, d=d, shop=k == 0, front_balcony_bay=bal.get(0, -1)))
        z += n * FLOOR
        A.m("trim").box((cx, cy, z), (w + 2.2, d + 2.2, 0.22)); A.m("trim").box((cx, cy - d / 2 - 1.0, z - 0.2), (w + 2.2, 0.12, 0.18))
        if eave:
            A.m("strip").box((cx, cy - d / 2 - 0.86, z - 0.18), (w + 1.6, 0.09, 0.12))
            A.m("strip").box((cx - w / 2 - 0.86, cy, z - 0.18), (0.09, d + 1.6, 0.12)); A.m("strip").box((cx + w / 2 + 0.86, cy, z - 0.18), (0.09, d + 1.6, 0.12))
        blocks.append((cx, cy, z, w, d))
    for i in range(len(blocks) - 1):
        cx, cy, zz, ww, dd = blocks[i]; nb = int(ww / BAY + 1e-6)
        KM.ledge_garden(A, r2, cx, cy, zz, ww, dd, (ww - nb * BAY) / 2, nb, p=garden, upper_front=blocks[i + 1][1] - blocks[i + 1][4] / 2)
    return metas, blocks


def _finish(A, metas, blocks, roof="kit", footprint=None, extra=None):
    cx, cy, z, w, d = blocks[-1]
    (KM.roof_garden if roof == "garden" else roof_kit)(A, random.Random(len(metas) * 31 + int(z)), cx, cy, z + 0.11, w, d) if roof else None
    fw, fd = footprint or (blocks[0][3], blocks[0][4])
    KM.stilts(A, 0, 0, fw, fd)
    A.meta = dict(floors=sum(m["floors"] for m in metas), footprint_m=[fw, fd], roof=roof or "crown", roof_y_m=round(z + 0.11, 2), roof_centre_m=[round(cx, 2), round(cy, 2)],
                  roof_size_m=[w, d], blocks=metas, tower=True, **(extra or {}),
                  blocks_note="building-local plan coordinates: x along the front, y = depth (the front faces -y), z up; GLB = (x, z, -y)")
    A.finish()
    return A


def _lantern_crown(A, r, cx, cy, z, rad, h, bands=3):
    """A lantern drum: a ring of lit bands between posts, a cap, a beacon. Not panes (no openings): `lantern` and `strip`."""
    A.m("trim").cyl((cx, cy, z), rad + 0.6, 0.35, n=16)
    for k in range(16):
        a = k / 16 * 6.283
        A.m("metal").beam((cx + rad * math.cos(a), cy + rad * math.sin(a), z + 0.35), (cx + rad * math.cos(a), cy + rad * math.sin(a), z + h), 0.1)
    for b in range(bands):
        zb = z + 0.6 + b * (h - 0.8) / bands; b0 = len(A.m("lantern").uv)
        A.m("lantern").cyl((cx, cy, zb), rad - 0.12, (h - 0.8) / bands * 0.7, n=16, cap=False)
        for i in range(b0, len(A.m("lantern").uv)):
            A.m("lantern").uv[i] = (lvl(0.45 + 0.4 * r.random()), r.random())
    A.m("trim").cyl((cx, cy, z + h), rad + 0.8, 0.5, n=16, r_top=0.3)
    A.m("strip").cyl((cx, cy, z + h - 0.1), rad + 0.85, 0.12, n=16, cap=False)
    A.m("metal").cyl((cx, cy, z + h + 0.5), 0.1, 4.0, n=6); A.m("beacon").sphere((cx, cy, z + h + 4.6), 0.2, n=8, m=5)


def tower_spire():
    r = random.Random(1201); r2 = random.Random(1202); A = Asset("tower_spire")
    specs = [(0, 0, 7.2, 7.2, 1), (0, 0.6, 4.8, 4.8, 5), (0.0, 0.0, 4.8, 4.8, 6), (0, -0.6, 4.8, 4.8, 5), (0, 0, 4.8, 4.8, 4)]
    metas, blocks = _stack(A, r, r2, specs, 0.24, 0.25, 1201, garden=0.5)
    cx, cy, z, w, d = blocks[-1]
    for k in range(4):                                                    # the spire: four raked ribs with light lines, rings, a needle
        a = k / 4 * 6.283 + 0.785; p0 = (cx + 2.6 * math.cos(a), cy + 2.6 * math.sin(a), z + 0.1); p1 = (cx, cy, z + 22.0)
        A.m("metal").beam(p0, p1, 0.32, t2=0.14); A.m("strip").beam((p0[0] * 0.98, p0[1] * 0.98, p0[2] + 0.3), (cx, cy, z + 21.0), 0.06)
    for k in range(5):
        zz = z + 2.5 + k * 3.8; rr_ = 2.6 * (1 - (zz - z) / 22.0)
        A.m("strip").cyl((cx, cy, zz), rr_ + 0.05, 0.1, n=12, cap=False)
    A.m("metal").cyl((cx, cy, z + 22.0), 0.08, 8.0, n=6); A.m("beacon").sphere((cx, cy, z + 30.2), 0.2, n=8, m=5)
    return _finish(A, metas, blocks, roof=None, extra=dict(kind="slender lantern spire", crown_m=30))


def tower_setback():
    r = random.Random(1211); r2 = random.Random(1212); A = Asset("tower_setback")
    specs = [(0, 0, 12.0, 12.0, 1), (0, 0, 12.0, 12.0, 4), (0, 1.2, 9.6, 9.6, 4), (0, 2.4, 7.2, 7.2, 4), (0, 3.0, 4.8, 4.8, 3)]
    metas, blocks = _stack(A, r, r2, specs, 0.28, 0.3, 1211, garden=1.0)
    for i in range(1, len(blocks) - 1):                                  # each setback a planted terrace: a pergola, a sapling, troughs on the side lips
        cx, cy, zz, ww, dd = blocks[i]; ncx, ncy, nz, nw, nd_ = blocks[i + 1]
        fy = cy - dd / 2 + 0.9
        if ncy - nd_ / 2 - fy > 1.6:
            pergola(A, r2, (cx - ww / 2 + 1.8, fy + 0.6, zz + 0.11), 2.2, 1.6, 2.3, vines=4)
            trough(A, r2, (cx + 1.0, fy, zz + 0.11), max(1.6, ww - 4.6), 0.0, low=False)
    cx, cy, z, w, d = blocks[-1]
    _lantern_crown(A, r, cx, cy, z + 0.11, 2.0, 4.2)
    return _finish(A, metas, blocks, roof=None, footprint=(12.0, 12.0), extra=dict(kind="setback tower with garden terraces"))


def tower_twin():
    r = random.Random(1221); r2 = random.Random(1222); A = Asset("tower_twin")
    left = [(-7.8, 0, 7.2, 7.2, 1), (-7.8, 0, 7.2, 7.2, 5), (-7.8, 0.6, 7.2, 6.0, 5), (-7.8, 0, 4.8, 6.0, 5)]
    right = [(7.8, 0, 7.2, 7.2, 1), (7.8, -0.6, 7.2, 7.2, 4), (7.8, 0, 7.2, 6.0, 4), (7.8, 0.6, 4.8, 4.8, 3)]
    mL, bL = _stack(A, r, r2, left, 0.26, 0.24, 1221, garden=0.6, sides=(0, 2, 3))      # the walls that face each other across the bridges are blank
    mR, bR = _stack(A, r, r2, right, 0.3, 0.28, 1231, garden=0.6, sides=(0, 1, 2))
    for zb in (6 * FLOOR + 0.0, 10 * FLOOR + 0.0):                       # sky-bridges at floors 6 and 10: a covered deck between the two, lit underneath
        A.m("trim").box((0, 0, zb), (8.8, 3.0, 0.3)); A.m("trim").box((0, 0, zb + 2.9), (8.8, 3.2, 0.25))
        for sx in (-1, 1):
            A.m("metal").box((0, sx * 1.45, zb + 1.1), (8.6, 0.06, 0.06))
            for k in range(4):
                A.m("metal").box((-3.6 + k * 2.4, sx * 1.45, zb + 1.45), (0.08, 0.08, 2.6))
        A.m("strip").box((0, -1.55, zb - 0.18), (8.6, 0.08, 0.1)); A.m("strip").box((0, 1.55, zb - 0.18), (8.6, 0.08, 0.1))
        for k in range(3):
            lamp(A.m("lantern"), (-2.0 + 2.0 * k, 0, zb + 2.6), 0.16, 0.85, r.random(), n=6, m=4)
    cxl, cyl, zl, wl, dl = bL[-1]; cxr, cyr, zr, wr, dr = bR[-1]
    roof_kit(A, r2, cxr, cyr, zr + 0.11, wr, dr, mast_h=6.0, tanks=0)          # a 4.8 m roof has no room for the tank beside the bulkhead door
    _lantern_crown(A, r, cxl, cyl, zl + 0.11, 1.8, 3.6)
    A_meta_blocks = mL + mR
    KM.stilts(A, -7.8, 0, 7.2, 7.2); KM.stilts(A, 7.8, 0, 7.2, 7.2)
    A.meta = dict(floors=16, footprint_m=[22.8, 7.2], roof="kit", roof_y_m=round(zr + 0.11, 2), roof_centre_m=[cxr, cyr], roof_size_m=[wr, dr], blocks=mL, tower=True, kind="twin towers with sky-bridges",
                  blocks_note="blocks of the left tower only (the right tower's piers are not listed)")
    A.finish()
    return A


def tower_lean():
    r = random.Random(1241); r2 = random.Random(1242); A = Asset("tower_lean")
    specs = [(0, 0, 7.2, 8.4, 1)] + [(0.6 + 1.2 * k, 0, 7.2, 8.4, 3) for k in range(5)]
    metas, blocks = _stack(A, r, r2, specs, 0.3, 0.3, 1241, garden=0.7)
    for i in range(1, len(blocks)):                                       # brackets under each block's overhang, on its side wall (no windows there: cell boundaries of the end wall)
        cx, cy, zz, ww, dd = blocks[i - 1]
        x_out = blocks[i][0] + blocks[i][3] / 2
        if x_out > cx + ww / 2 + 0.3:
            for sy in (-dd / 2 + 0.3, dd / 2 - 0.3):
                A.m("metal").beam((cx + ww / 2 - 0.1, cy + sy, zz - FLOOR + 0.3), (x_out - 0.2, cy + sy, zz - 0.15), 0.18)
    return _finish(A, metas, blocks, roof="kit", footprint=(7.2, 8.4), extra=dict(kind="leaning tower: each block steps 1.2 m out"))


def tower_crowned():
    r = random.Random(1251); r2 = random.Random(1252); A = Asset("tower_crowned")
    specs = [(0, 0, 8.4, 8.4, 1), (0, 0, 7.2, 7.2, 5), (0, 0.6, 7.2, 7.2, 5), (0, 0, 7.2, 7.2, 4)]
    metas, blocks = _stack(A, r, r2, specs, 0.22, 0.3, 1251, garden=0.5)
    cx, cy, z, w, d = blocks[-1]
    A.m("trim").cyl((cx, cy, z + 0.2), 8.0, 0.4, n=24)                   # the gallery: a ring deck wider than the tower, a rail, light under it
    A.m("strip").cyl((cx, cy, z - 0.05), 7.8, 0.12, n=24, cap=False)
    A.m("metal").cyl((cx, cy, z + 1.6), 8.0, 0.06, n=24, cap=False)
    for k in range(12):
        a = k / 12 * 6.283
        A.m("metal").beam((cx + 8.0 * math.cos(a), cy + 8.0 * math.sin(a), z + 0.6), (cx + 8.0 * math.cos(a), cy + 8.0 * math.sin(a), z + 1.65), 0.05)
        if k % 3 == 0:                                                     # struts from the block's corners only (never in front of a window)
            q = (cx + (w / 2 + 0.05) * (1 if math.cos(a + 0.785) > 0 else -1), cy + (d / 2 + 0.05) * (1 if math.sin(a + 0.785) > 0 else -1))
            A.m("metal").beam((q[0], q[1], z - 3.0), (cx + 7.6 * (q[0] - cx) / math.hypot(q[0] - cx, q[1] - cy), cy + 7.6 * (q[1] - cy) / math.hypot(q[0] - cx, q[1] - cy), z + 0.05), 0.16)
    _lantern_crown(A, r, cx, cy, z + 0.6, 4.2, 7.0, bands=4)
    return _finish(A, metas, blocks, roof=None, extra=dict(kind="lighthouse-crowned tower"))


def tower_mast():
    r = random.Random(1261); r2 = random.Random(1262); A = Asset("tower_mast")
    specs = [(0, 0, 9.6, 8.4, 1), (0, 0, 9.6, 8.4, 4), (-1.2, 0, 7.2, 8.4, 4), (0, 0.6, 7.2, 7.2, 3)]
    metas, blocks = _stack(A, r, r2, specs, 0.26, 0.22, 1261, garden=0.6)
    cx, cy, z, w, d = blocks[-1]; H = 26.0
    for (sx, sy) in ((-1.2, -1.2), (1.2, -1.2), (1.2, 1.2), (-1.2, 1.2)):
        A.m("metal").beam((cx + sx, cy + sy, z), (cx + sx * 0.3, cy + sy * 0.3, z + H), 0.16)
    for k in range(7):
        zz = z + 2 + k * 3.4; s_ = 1.2 * (1 - 0.7 * (zz - z) / H)
        A.m("metal").box((cx, cy, zz), (2 * s_, 2 * s_, 0.1))
        if k % 2 == 0:
            lamp(A.m("beacon"), (cx + s_, cy - s_, zz), 0.12, 1.0, 0.5, n=6, m=4)
    for (zz, a) in ((z + 9.0, 0.6), (z + 15.0, 3.6)):                      # dishes
        A.m("metal").cyl((cx + 1.6 * math.cos(a), cy + 1.6 * math.sin(a), zz), 0.2, 0.5, n=10, r_top=1.4)
    A.m("metal").cyl((cx, cy, z + H), 0.08, 6.0, n=6); A.m("beacon").sphere((cx, cy, z + H + 6.2), 0.25, n=8, m=5)
    A.m("strip").cyl((cx, cy, z + H - 0.4), 0.6, 0.1, n=10, cap=False)
    return _finish(A, metas, blocks, roof=None, extra=dict(kind="mast tower with dishes and a beacon"))


def tower_hanging():
    r = random.Random(1271); r2 = random.Random(1272); A = Asset("tower_hanging")
    specs = [(0, 0, 9.6, 9.6, 1)] + [(0, (k % 2) * 0.6 - 0.3, 9.6, 9.6 - (k % 2) * 1.2, 2) for k in range(6)]
    metas, blocks = _stack(A, r, r2, specs, 0.3, 0.32, 1271, garden=1.0)
    for i in range(len(blocks) - 1):                                      # every eave lip planted, vines longer than usual (at the piers only)
        cx, cy, zz, ww, dd = blocks[i]; nb = int(ww / BAY + 1e-6); mg = (ww - nb * BAY) / 2
        for b in range(nb + 1):
            x = cx - ww / 2 + mg + b * BAY
            for k in range(r2.choice([1, 2])):
                vine(A, r2, (x + r2.uniform(-0.08, 0.08), cy - dd / 2 - 1.12, zz - 0.1), r2.uniform(2.0, 5.5), 0.05)
    return _finish(A, metas, blocks, roof="garden", extra=dict(kind="hanging-garden tower"))


def towers():
    return [tower_spire(), tower_setback(), tower_twin(), tower_lean(), tower_crowned(), tower_mast(), tower_hanging()]
