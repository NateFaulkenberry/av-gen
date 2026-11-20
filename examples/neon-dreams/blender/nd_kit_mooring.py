"""Neon Dreams kit: Lowlight Mooring. Buildings (shophouse and residential stacks, gallery slab, corner tower, capsule
tower, station, kiosk, the Lone Lamp house), the Great Lantern, decks, craft and the lighthouses."""
import math, random
from mathutils import Vector
from nd_kitcore import *  # noqa
from nd_kit_garden import shrub, fern, sprigs, vine, trough, pergola, bench, bud


# ------------------------------------------------------------------------------------------- gardens in the architecture
def ledge_garden(A, r2, cx, cy, z, ww, dd, mg_front, nb_front, p=0.5, upper_front=None):
    """Troughs on the projecting lip of an eave slab (front) with vines at the cell boundaries below. Skipped where the
    block above overhangs the lip (upper_front = the y of its front wall)."""
    if r2.random() > p or (upper_front is not None and upper_front < cy - dd / 2 - 0.3):
        return
    y = cy - dd / 2 - 0.82
    for b in range(nb_front):
        if r2.random() < 0.7:
            x = cx - ww / 2 + mg_front + (b + 0.5) * BAY
            trough(A, r2, (x, y, z + 0.11), 1.9, 0.0, depth=0.36, height=0.3)
    for b in range(nb_front + 1):                       # vines over the piers only: never in front of a window
        if r2.random() < 0.55:
            x = cx - ww / 2 + mg_front + b * BAY
            for k in range(r2.choice([1, 2, 2])):
                vine(A, r2, (x + r2.uniform(-0.08, 0.08), cy - dd / 2 - 1.12, z - 0.1), r2.uniform(1.2, 4.2), 0.05)


def roof_garden(A, r2, cx, cy, z, w, d, mast_h=7.0):
    """A planted roof terrace: parapet, troughs on three sides, a pergola with tidevine, an umbel sapling in a pit, the
    stair bulkhead with its lit door, the mast. Residents gather here; the layout puts inhabitants on these roofs."""
    for (sx, sy, lw, ld) in ((0, -d / 2 + 0.1, w, 0.2), (0, d / 2 - 0.1, w, 0.2), (-w / 2 + 0.1, 0, 0.2, d - 0.4), (w / 2 - 0.1, 0, 0.2, d - 0.4)):
        A.m("trim").box((cx + sx, cy + sy, z + 0.5), (lw, ld, 1.0))
    A.m("metal").box((cx, cy - d / 2 + 0.1, z + 1.06), (w, 0.06, 0.05))
    bx, by = cx + w / 2 - 1.5, cy + d / 2 - 1.7                              # bulkhead, back right
    bw, bd, bh = 2.4, 2.8, 2.7
    O, T, Nn = wall((bx - bw / 2, by + bd / 2, 0), (0, -1, 0))
    pane_opening(A, O, T, Nn, (0.95, 1.85, z + 0.06, z + 2.1), (0, bd, z, z + bh), 0.1, "win_warm", 0.6, 0.31, sill=False, mullions=0,
                 transom=z + 1.75, kind="door", clear=0.7)
    for (o_, t_, L_) in (((bx - bw / 2, by - bd / 2, 0), (1, 0, 0), bw), ((bx + bw / 2, by - bd / 2, 0), (0, 1, 0), bd), ((bx + bw / 2, by + bd / 2, 0), (-1, 0, 0), bw)):
        O2, T2, N2 = wall(o_, t_)
        wq(A, "facade", O2, T2, N2, 0, L_, z, z + bh)
    A.m("trim").box((bx, by, z + bh + 0.06), (bw + 0.3, bd + 0.3, 0.12))
    trough(A, r2, (cx - 0.2, cy - d / 2 + 0.45, z), max(1.6, w - 1.2), 0.0, low=False)                                   # front edge
    trough(A, r2, (cx - w / 2 + 0.45, cy + 0.2, z), max(1.6, d - 2.4), math.pi / 2, low=False)                            # left edge
    nbf = int(w / BAY + 1e-6)
    for b in range(nbf + 1):                                                  # vines spill over the eave lip, at the piers only
        if r2.random() < 0.6:
            x = cx - w / 2 + (w - nbf * BAY) / 2 + b * BAY
            vine(A, r2, (x + r2.uniform(-0.08, 0.08), cy - d / 2 - 1.12, z - 0.2), r2.uniform(1.5, 4.5), 0.05)
    px, py = cx - w / 2 + 2.3, cy + d / 2 - 1.9                              # pergola, back left
    if w >= 7.0:
        pergola(A, r2, (px, py, z), 2.8, 2.2, 2.4, vines=6)
    bench(A, (px, py + 0.5, z), 0.0, 1.5)
    tx, ty = cx + w / 2 - 1.6, cy - d / 2 + 1.9                              # an umbel sapling in a pit, front right
    if w >= 7.0:
        A.m("trim").cyl((tx, ty, z), 0.7, 0.4, n=10); sprigs(A, r2, (tx, ty, z + 0.4), 0.55, n=7, h=(0.15, 0.4))
        top = Vector((tx + 0.15, ty - 0.1, z + 3.0))
        A.m("trim").beam((tx, ty, z + 0.3), top, 0.09)
        for k in range(36):
            u_ = (k + 0.5) / 36; th = math.acos(1 - u_ * 0.74); ph = k * 2.39996
            q = Vector((top.x + 1.2 * math.sin(th) * math.cos(ph), top.y + 1.2 * math.sin(th) * math.sin(ph), top.z + 0.66 * math.cos(th) - 0.24))
            bud(A, q, r2.uniform(0.04, 0.06), r2)
            if k % 4 == 0:
                A.m("trim").beam(top, q, 0.018)
    mx, my = cx - w / 2 + 0.6, cy + d / 2 - 0.6
    A.m("metal").cyl((mx, my, z), 0.06, mast_h, n=6); A.m("beacon").sphere((mx, my, z + mast_h + 0.12), 0.13, n=8, m=5)


# ------------------------------------------------------------------------------------------- buildings
def shophouse(name, seed, floors=6, w=8.4, d=10.8, lit=0.34, warm=0.3, roof="kit", garden=0.35, shop=True, special=None):
    """Shophouse / residential stack: a shop floor, then offset blocks separated by eave slabs with an eave line.
    The `r` stream is drawn exactly as in kit v1, so the approved modules keep their massing and lit pattern."""
    r = random.Random(seed); r2 = random.Random(seed * 7919 + 13); A = Asset(name)
    z, left, first, top = 0.0, floors, True, None
    blocks = []; meta_blocks = []
    while left > 0:
        n = 1 if first else min(left, r.choice([2, 2, 3, 3]))
        ox, oy = (0, 0) if first else (r.choice([-1.2, 0, 0, 1.2]) * (1 if w > 7.2 else 0), r.choice([-0.6, 0, 0.6]))
        ww = w if first else w - r.choice([0, 0, 2.4]) if w > 7.2 else w
        dd = d if first else d - r.choice([0, 0, 0, 2.4])
        cx, cy = ox, oy + (d - dd) / 2 * (1 if r.random() < 0.7 else -1) * (0 if first else 1)
        bal = block(A, r, r2, cx, cy, z, ww, dd, n, lit, warm, seed + z, balcony_side=None if first else r.choice([0, 0, 2, None]), shop=first and shop, special=special, stair=3)
        meta_blocks.append(dict(c=[round(cx, 2), round(cy, 2)], z0=round(z, 2), floors=n, w=ww, d=dd, shop=bool(first and shop), front_balcony_bay=bal.get(0, -1)))
        z += n * FLOOR; left -= n
        A.m("trim").box((cx, cy, z), (ww + 2.2, dd + 2.2, 0.22))                                   # the eave slab
        A.m("trim").box((cx, cy - dd / 2 - 1.0, z - 0.2), (ww + 2.2, 0.12, 0.18))                   # fascia lip
        if first or r.random() < 0.6:                                                               # the eave line
            A.m("strip").box((cx, cy - dd / 2 - 0.86, z - 0.18), (ww + 1.6, 0.09, 0.12))               # 12 cm high: still a line at 5 m, no longer lost at 500 m
            A.m("strip").box((cx + ww / 2 + 0.86, cy, z - 0.18), (0.09, dd + 1.6, 0.12))
            A.m("strip").box((cx - ww / 2 - 0.86, cy, z - 0.18), (0.09, dd + 1.6, 0.12))                # the west side too: it faces the approach
        blocks.append((cx, cy, z, ww, dd))
        top = (cx, cy, z, ww, dd); first = False
    for i in range(1, len(blocks) - 1):                                       # planted ledges where the block above stands back
        cx, cy, zz, ww, dd = blocks[i]; nb = int(ww / BAY + 1e-6)
        ledge_garden(A, r2, cx, cy, zz, ww, dd, (ww - nb * BAY) / 2, nb, p=garden, upper_front=blocks[i + 1][1] - blocks[i + 1][4] / 2)
    if roof == "garden":
        roof_garden(A, r2, top[0], top[1], top[2] + 0.11, top[3], top[4])
    else:
        roof_kit(A, r2, top[0], top[1], top[2] + 0.11, top[3], top[4])
    if floors >= 10:                                                                              # tall stacks wear a lit crown on the sides the approach sees
        cx, cy, z, ww, dd = top
        A.m("strip").box((cx, cy - dd / 2 - 1.12, z + 0.02), (ww + 2.2, 0.08, 0.14)); A.m("strip").box((cx - ww / 2 - 1.12, cy, z + 0.02), (0.08, dd + 2.2, 0.14))
    stilts(A, 0, 0, w, d)
    A.meta = dict(floors=floors, footprint_m=[w, d], roof=roof, roof_y_m=round(top[2] + 0.11, 2), roof_centre_m=[round(top[0], 2), round(top[1], 2)],
                  roof_size_m=[top[3], top[4]], blocks=meta_blocks,
                  blocks_note="building-local plan coordinates: x along the front, y = depth (the front faces -y), z up; GLB = (x, z, -y)")
    A.finish()
    return A


def lone_lamp_house():
    """The Lone Lamp: four dark floors and one lit window."""
    def one(si, fl, b, O, T, Nn, u, v):
        if si == 0 and fl == 2 and b == 1:
            pane_opening(A_[0], O, T, Nn, (u + 0.575, u + 1.825, v + 0.95, v + 2.30), (u, u + BAY, v, v + FLOOR), 0.14, "win_warm", 1.0, 0.77, transom=v + 1.79,
                         dress=("curtain", 0.2))
            return True
        return False
    A_ = [None]
    r = random.Random(4242); r2 = random.Random(4243); A = Asset("lone_lamp_house"); A_[0] = A
    block(A, r, r2, 0, 0, 0.0, 7.2, 8.4, 2, 0.0, 0.0, 1.0, special=one, shop=False)
    A.m("trim").box((0, 0, 6.4), (9.4, 10.6, 0.22))
    block(A, r, r2, 0.0, 0.6, 6.4, 7.2, 6.0, 2, 0.0, 0.0, 2.0, special=one, shop=False)
    A.m("trim").box((0, 0.6, 12.8), (9.4, 8.2, 0.22))
    roof_kit(A, r2, 0, 0.6, 12.91, 7.2, 6.0, mast_h=14.0)
    stilts(A, 0, 0, 7.2, 8.4)
    A.finish()
    return A


def gallery_slab():
    """Gallery-access slab block, 31.2 x 8.4 m, 8 floors in two shifted blocks: open access galleries with a soffit
    light line on every floor (its signature at distance), a lit stair tower, balconies on the back."""
    r = random.Random(808); r2 = random.Random(809); A = Asset("mooring_gallery_slab")
    L1, L2, D = 31.2, 28.8, 8.4

    def cells(lit):
        def f(si, fl, b, O, T, Nn, u, v):
            if si == 0:                                                     # the gallery wall: door + kitchen window, or a high window
                wid = (0.31 + fl * 0.0613 + b * 0.0171) % 1.0
                on = r.random() < lit; mat = ("win_warm" if r.random() < 0.3 else "win_cool") if on else "glass"; lv = r.random() ** 1.3
                if b % 2 == 0:
                    D_, W_ = (u + 0.3, u + 1.2, v + 0.06, v + 2.1), (u + 1.5, u + 2.1, v + 1.1, v + 2.1)
                    fill_cell(A, O, T, Nn, (u, u + BAY, v, v + FLOOR), [D_, W_])
                    door_leaf(A, O, T, Nn, D_)
                    pane_opening(A, O, T, Nn, W_, None, 0.12, mat, lv, wid, mullions=0, kind="gallery", clear=1.3, reg_cell=(u, u + BAY, v, v + FLOOR))
                else:
                    pane_opening(A, O, T, Nn, (u + 0.5, u + 1.9, v + 1.35, v + 2.2), (u, u + BAY, v, v + FLOOR), 0.12, mat, lv, wid, mullions=1, kind="gallery", clear=1.3)
                return True
            if si == 2 and fl > 0 and b % 2 == 1:                           # back: a balcony on every other bay
                window_cell(A, r, r2, O, T, Nn, u, v, lit, 0.25, (0.7 + fl * 0.0613 + b * 0.0171) % 1.0, balcony=True)
                if r2.random() < 0.5:                                       # a trough hung outside the rail
                    c = O + T * (u + 1.2) + Z * (v + 0.55) + Nn * 1.24
                    trough(A, r2, tuple(c), 1.6, math.atan2(T.y, T.x), depth=0.3, height=0.28)
                for uu in (u + 0.24, u + BAY - 0.24):
                    if r2.random() < 0.35:
                        vine(A, r2, tuple(O + T * uu + Z * (v - 0.02) + Nn * 1.06), r2.uniform(1.0, 3.0), 0.04)
                return True
            if si == 3 and b >= 1:                                          # the stair tower stands against this part of the west wall
                wq(A, "facade", O, T, Nn, u, u + BAY, v, v + FLOOR)
                return True
            return False
        return f

    def galleries(cx, cy, z0, L, floors, top):
        O, T, Nn = wall((cx - L / 2, cy - D / 2, 0), (1, 0, 0))
        for fl in range(floors + (1 if top else 0)):
            v = z0 + fl * FLOOR
            wbox(A, "trim", O, T, Nn, 0, L, v - 0.14, v, 0.0, 1.5)                                    # gallery slab (the top one is a canopy)
            if fl > 0:
                wbox(A, "strip", O, T, Nn, 0.5, L - 0.5, v - 0.2, v - 0.14, 0.32, 0.4)                # the soffit light line of the floor below
            if fl < floors:
                wbox(A, "trim", O, T, Nn, 0, L, v, v + 1.0, 1.42, 1.48)                               # solid parapet
                wbox(A, "metal", O, T, Nn, 0, L, v + 1.0, v + 1.06, 1.38, 1.5)
                k = 0.0
                while k <= L + 0.01 and (fl < floors - 1 or top):
                    wbox(A, "metal", O, T, Nn, min(k, L - 0.1), min(k, L - 0.1) + 0.1, v, v + FLOOR - 0.14, 1.4, 1.5)
                    k += 4.8
                if r2.random() < 0.7:                                                                  # troughs hung outside the parapet, with vines
                    for q in range(r2.choice([1, 2, 3])):
                        uu = r2.uniform(1.5, L - 1.5)
                        c = O + T * uu + Z * (v + 0.62) + Nn * 1.66
                        trough(A, r2, tuple(c), 1.8, 0.0, depth=0.3, height=0.3, vines=r2.choice([1, 2, 3]), vine_len=(0.8, 2.4))

    block(A, r, r2, 0, 0, 0.0, L1, D, 5, 0.3, 0.25, 3.0, special=cells(0.34), blank_p=0.06, pipes=False)
    galleries(0, 0, 0.0, L1, 5, True)
    A.m("trim").box((0, -0.2, 16.0), (L1 + 2.2, D + 2.6, 0.22))
    block(A, r, r2, -1.2, 0.6, 16.0, L2, D, 3, 0.24, 0.25, 5.0, special=cells(0.26), blank_p=0.06, pipes=False)
    galleries(-1.2, 0.6, 16.0, L2, 3, True)
    for (px, py, L_, yw) in ((14.9, 0.3, 6.0, math.pi / 2), (14.2, -3.6, 1.8, 0.0)):       # the east setback at floor 5 is a planted terrace
        trough(A, r2, (px, py, 16.11), L_, yw, low=False, vines=0)
    shrub(A, r2, (14.3, 2.8, 16.11), 0.6, buds=10); fern(A, r2, (14.0, 1.2, 16.11), 0.8)
    z = 25.6
    A.m("trim").box((-1.2, 0.6, z), (L2 + 2.2, D + 2.2, 0.22))
    roof_kit(A, r2, -1.2 - 7.2, 0.6, z + 0.11, 14.4, D, mast_h=11.0, tanks=2); roof_garden(A, r2, -1.2 + 7.2, 0.6, z + 0.11, 14.4, D)
    # the stair tower at the west end: a slot window per floor, always lit
    tw, td, tx, ty = 3.6, 4.8, -L1 / 2 - 1.8, -D / 2 + 2.4 - 1.0

    def slot(si, fl, b, O, T, Nn, u, v):
        if si == 0:
            pane_opening(A, O, T, Nn, (u + 0.85, u + 1.55, v + 0.5, v + 2.75), (u, u + BAY, v, v + FLOOR), 0.14, "win_stair", 0.72 if fl % 3 else 0.9, (0.9 + fl * 0.03) % 1.0,
                         sill=False, mullions=0, transom=v + 1.6, kind="stair")
            return True
        wq(A, "facade", O, T, Nn, u, u + BAY, v, v + FLOOR)
        return True
    block(A, r, r2, tx, ty, 0.0, tw, td, 9, 0.0, 0.0, 9.0, special=slot, pipes=False)
    A.m("trim").box((tx, ty, 28.8), (tw + 0.6, td + 0.6, 0.22)); A.m("beacon").sphere((tx, ty, 29.2), 0.14, n=8, m=5)
    stilts(A, 0, 0, L1, D, nx=7)
    for sy in (-1, 1):
        A.m("metal").cyl((tx, ty + sy * 1.8, -14.0), 0.26, 14.0, n=8, cap=False)
    A.meta = dict(floors=8, footprint_m=[L1 + tw, D + 1.5], gallery_side="front (-z in Blender y, +z in the GLB)", roof_y_m=25.71)
    A.finish()
    return A


def corner_tower():
    """The tallest ordinary building: 21 floors in five blocks, setbacks at 14 and 18, a lit stair slot up the front,
    a lit crown. Rooftop letters are a separate sign asset placed on its roof by the layout."""
    r = random.Random(777); r2 = random.Random(778); A = Asset("mooring_corner_tower")

    def slot_col(nb):
        def f(si, fl, b, O, T, Nn, u, v):
            if si == 0 and b == nb // 2 and not (v < 0.1):
                pane_opening(A, O, T, Nn, (u + 0.575, u + 1.825, v + 0.3, v + 2.85), (u, u + BAY, v, v + FLOOR), 0.16, "win_stair", 0.66 + 0.2 * ((int(v / FLOOR)) % 2), 0.05,
                             sill=False, mullions=1, transom=v + 1.6, kind="stair")
                return True
            return False
        return f
    plan = [(5, 12.0, 12.0, 0.0, 0.0), (5, 12.0, 12.0, 0.6, -0.3), (4, 12.0, 12.0, -0.6, 0.3), (4, 9.6, 9.6, 0.6, 0.6), (3, 9.6, 9.6, -0.3, 1.2)]
    z = 0.0; meta_blocks = []
    for i, (n, ww, dd, ox, oy) in enumerate(plan):
        block(A, r, r2, ox, oy, z, ww, dd, n, 0.3 if i < 3 else 0.42, 0.25, 7.0 + z, balcony_side=r.choice([1, 2, 3]) if i else None, shop=(i == 0),
              special=slot_col(int(ww / BAY + 1e-6)), blank_p=0.08)
        meta_blocks.append(dict(c=[ox, oy], z0=round(z, 2), floors=n, w=ww, d=dd, shop=(i == 0), front_balcony_bay=-1, stair_slot_bay=int(ww / BAY + 1e-6) // 2))
        z += n * FLOOR
        A.m("trim").box((ox, oy, z), (ww + 2.2, dd + 2.2, 0.22)); A.m("trim").box((ox, oy - dd / 2 - 1.0, z - 0.2), (ww + 2.2, 0.12, 0.18))
        if i in (0, 2, 4):
            A.m("strip").box((ox, oy - dd / 2 - 0.86, z - 0.15), (ww + 1.6, 0.07, 0.06)); A.m("strip").box((ox + ww / 2 + 0.86, oy, z - 0.15), (0.07, dd + 1.6, 0.06))
        if i == 2:                                                            # the setback at floor 14 is a planted terrace
            for (px, py, L_, yw) in ((ox - 2.6, oy - 5.6, 5.0, 0.0), (ox - 5.6, oy, 7.0, math.pi / 2), (ox + 5.6, oy + 1.0, 6.0, math.pi / 2)):   # the front trough stops short of the stair slot
                trough(A, r2, (px, py, z + 0.11), L_, yw, low=False)
            for b in range(6):
                if r2.random() < 0.6:
                    vine(A, r2, (ox - 6.0 + b * BAY + r2.uniform(-0.1, 0.1), oy - 12.0 / 2 - 1.04, z - 0.1), r2.uniform(2.0, 6.0), 0.07)
        top = (ox, oy, z, ww, dd)
    cx, cy, z, ww, dd = top
    for (sx, sy, lw, ld) in ((0, -dd / 2 - 0.3, ww + 0.8, 0.2), (0, dd / 2 + 0.3, ww + 0.8, 0.2), (-ww / 2 - 0.3, 0, 0.2, dd + 0.4), (ww / 2 + 0.3, 0, 0.2, dd + 0.4)):
        A.m("trim").box((cx + sx, cy + sy, z + 0.75), (lw, ld, 1.3))                              # the crown parapet
        A.m("strip").box((cx + sx * 1.04, cy + sy * 1.04, z + 1.32), (lw + (0.1 if lw > 1 else 0.0), ld + (0.1 if ld > 1 else 0.0), 0.1))   # the lit crown
    roof_kit(A, r2, cx, cy + 1.2, z + 0.11, ww, dd - 2.4, mast_h=16.0)
    stilts(A, 0, 0, 12.0, 12.0, nx=4)
    A.meta = dict(floors=21, footprint_m=[12.0, 12.0], roof_y_m=round(z + 0.11, 2), roof_centre_m=[cx, cy], roof_size_m=[ww, dd], blocks=meta_blocks,
                  sign_mount="rooftop letters stand on the front strip of the roof (the roof kit is set 2.4 m back)")
    A.finish()
    return A


def capsule_tower(seed=5):
    """Metabolist plug-in tower: a service core with 34 capsules, each with one round window in a recessed opening."""
    r = random.Random(seed); A = Asset("mooring_capsule_tower"); H = 60.0
    A.m("facade").box((0, 0, H / 2), (5.0, 5.0, H)); A.m("trim").box((0, 0, H + 0.2), (5.6, 5.6, 0.4))
    A.m("metal").box((0, 0, H + 2.4), (3.0, 3.0, 4.0)); A.m("metal").cyl((0, 0, H + 4.4), 0.08, 9.0, n=6); A.m("beacon").sphere((0, 0, H + 13.5), 0.18, n=8, m=5)
    for k in range(34):
        side = k % 4; z = 2.0 + (k // 4) * 6.4 + (side % 2) * 3.2 + r.uniform(-0.25, 0.25)
        a = side * math.pi / 2; c_, s_ = math.cos(a), math.sin(a)
        ctr = (c_ * 4.7, s_ * 4.7, z + 1.4)
        A.m("trim").box(ctr, (4.2, 3.0, 2.8), a, skip=(3,)); A.m("facade").box((c_ * 4.7, s_ * 4.7, z + 2.86), (4.0, 2.8, 0.12), a)
        A.m("metal").box((c_ * 2.55, s_ * 2.55, z + 1.4), (0.3, 1.2, 1.2), a)
        lit = r.random() < 0.4
        mat = (r.choice(["win_warm", "win_cool", "win_cool"])) if lit else "glass"
        uv = lvl_uv(r, 0.2)
        Nn = Vector((c_, s_, 0)); T = Vector((-s_, c_, 0))
        porthole(A, (c_ * 6.8, s_ * 6.8, z + 1.4), T, Nn, 0.8, 1.5, 1.4, 0.16, mat, uv[0], uv[1])
    stilts(A, 0, 0, 5.6, 5.6)
    A.finish()
    return A


def station():
    """The Kite Line station: a ticket hall under a broad canopy on six columns, and the cable's wheel house on a mast.
    Origin at the canopy centre on the plaza; the cable leaves from meta.cable_anchor toward local -x."""
    r = random.Random(606); r2 = random.Random(607); A = Asset("mooring_station")
    block(A, r, r2, 1.2, 2.0, 0.0, 9.6, 4.8, 1, 0.5, 0.6, 6.0, shop=True, blank_p=0.3, pipes=False)
    A.m("trim").box((1.2, 2.0, 3.25), (10.0, 5.2, 0.14))
    cw, cd, ch = 16.8, 9.6, 5.2                                               # the canopy
    A.m("trim").box((0, 0, ch + 0.15), (cw, cd, 0.3)); A.m("trim").box((0, -cd / 2 + 0.1, ch + 0.5), (cw, 0.2, 0.5))
    A.m("strip").box((0, -cd / 2 + 0.3, ch - 0.04), (cw - 1.0, 0.08, 0.06)); A.m("strip").box((-cw / 2 + 0.3, 0, ch - 0.04), (0.08, cd - 1.0, 0.06))
    for sx in (-7.2, 0, 7.2):
        for sy in (-3.9, 3.9):
            if sy > 0 and abs(sx - 1.2) < 5.0:
                continue
            A.m("metal").cyl((sx, sy, 0), 0.16, ch, n=8); A.m("metal").cyl((sx, sy, 0), 0.3, 0.25, n=8)
            A.m("metal").beam((sx, sy, ch - 1.2), (sx + (0.9 if sx <= 0 else -0.9), sy, ch), 0.08)
    for k in range(5):                                                        # soffit lamps
        lamp(A.m("lantern"), (-6.0 + k * 3.0, -2.2, ch - 0.12), 0.13, 0.7, 0.1 + 0.17 * k, n=8, m=4, squash=0.6)
    for bx in (-5.6, -3.0):
        bench(A, (bx, -2.9, 0), 0.0, 2.0)
    A.m("metal").box((-6.6, 2.6, 1.4), (0.12, 2.0, 2.8)); A.m("trim").box((-6.6, 2.6, 2.0), (0.16, 1.7, 1.2))                # timetable board
    mx, my, mh = -5.0, 1.2, 14.6                                              # wheel house mast through the canopy
    for sx in (-0.9, 0.9):
        for sy in (-0.9, 0.9):
            A.m("metal").beam((mx + sx * 1.3, my + sy * 1.3, ch + 0.3), (mx + sx * 0.7, my + sy * 0.7, mh), 0.16)
    for zz in (7.4, 9.8, 12.2):
        s_ = 1.3 - 0.6 * (zz - ch) / (mh - ch)
        for (ax, ay, bx_, by_) in ((-1, -1, 1, -1), (1, -1, 1, 1), (1, 1, -1, 1), (-1, 1, -1, -1)):
            A.m("metal").beam((mx + ax * s_, my + ay * s_, zz), (mx + bx_ * s_, my + by_ * s_, zz + 1.0), 0.07)
    A.m("trim").box((mx, my, mh + 0.6), (3.4, 3.4, 1.2)); A.m("trim").box((mx, my, mh + 1.3), (4.0, 4.0, 0.14))
    A.m("metal").cyl((mx - 0.2, my, mh - 0.42), 1.9, 0.24, n=20)             # the bullwheel
    A.m("strip").box((mx, my - 1.72, mh + 0.1), (3.0, 0.05, 0.06)); A.m("beacon").sphere((mx, my, mh + 1.6), 0.16, n=8, m=5)
    stilts(A, 0, 0, cw, cd, nx=4)
    A.meta = dict(footprint_m=[cw, cd], cable_anchor_m=[mx - 2.1, mh - 0.3, -my], roundel_mount_m=[0.0, ch + 1.4, cd / 2 - 0.1])
    A.finish()
    return A


def gondola_pylon():
    A = Asset("gondola_pylon")                     # a T pylon for the cable: cable passes at y = 14 m, 2.2 m either side of the mast
    A.m("metal").cyl((0, 0, -12), 0.34, 26.6, n=8, r_top=0.2); A.m("metal").box((0, 0, 14.5), (0.3, 5.2, 0.34))
    for sy in (-2.2, 2.2):
        A.m("metal").box((0, sy, 14.2), (1.5, 0.12, 0.3))
        for sx in (-0.5, 0.5):
            A.m("metal").cyl((sx, sy - 0.09, 14.0), 0.17, 0.18, n=10)
        A.m("metal").beam((0, sy, 14.5), (0, 0, 13.2), 0.07)
    A.m("beacon").sphere((0, 0, 14.85), 0.13, n=8, m=5)
    A.m("metal").beam((0, -1.6, -1.0), (0, 0, 4.0), 0.1); A.m("metal").beam((0, 1.6, -1.0), (0, 0, 4.0), 0.1)
    A.meta = dict(cable_y_m=14.0, cable_offsets_m=[-2.2, 2.2]); A.finish()
    return A


def kiosk():
    """A street stall: a counter window in an opening, a small canopy with a light line."""
    r = random.Random(31); r2 = random.Random(32); A = Asset("kiosk")

    def counter(si, fl, b, O, T, Nn, u, v):
        if si == 0:
            pane_opening(A, O, T, Nn, (u + 0.25, u + BAY - 0.25, v + 1.0, v + 2.1), (u, u + BAY, v, v + 2.6), 0.1, "win_warm", 0.75, 0.42, sill=True, mullions=1,
                         kind="shop", clear=1.0)
        else:
            wq(A, "facade", O, T, Nn, u, u + BAY, v, v + 2.6)
        return True
    sides = [((-1.2, -0.9, 0), (1, 0, 0), 2.4), ((1.2, -0.9, 0), (0, 1, 0), 1.8), ((1.2, 0.9, 0), (-1, 0, 0), 2.4), ((-1.2, 0.9, 0), (0, -1, 0), 1.8)]
    for si, (o_, t_, L_) in enumerate(sides):
        O, T, Nn = wall(o_, t_)
        if si == 0:
            counter(0, 0, 0, O, T, Nn, 0.0, 0.0)
        else:
            wq(A, "facade", O, T, Nn, 0, L_, 0, 2.6)
    A.m("trim").box((0, -0.25, 2.68), (3.0, 2.9, 0.16)); A.m("strip").box((0, -1.62, 2.58), (2.6, 0.05, 0.05))
    A.m("trim").box((0, -1.05, 0.98), (2.2, 0.3, 0.05))                                              # the counter shelf, under the opening
    A.finish()
    return A


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
        zc = H_ * 2 / tiers                                                                             # walkway at tier 2 with rail
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
    F_.finish()
    # the bell: six lifts of lit panels. Levels fall toward the top and vary panel to panel, so it reads as a lantern
    # with a warm belly, not a white cylinder; the GLB strength is low (1.6) so the hue survives the tone curve.
    B_ = Asset("great_lantern_bell"); rr = random.Random(92)
    lifts, hh = 6, 50.0 / 6
    for b_ in range(lifts):
        rad = 15.4 - b_ * 0.22
        base = [0.78, 1.0, 0.9, 0.7, 0.52, 0.38][b_]
        mb = B_.m("landmark")
        n = 36
        for i in range(n):
            a0, a1 = i / n * 6.283, (i + 1) / n * 6.283
            z0, z1 = b_ * hh + 0.3, (b_ + 1) * hh - 0.3
            p = [(rad * math.cos(a0), rad * math.sin(a0), z0), (rad * math.cos(a1), rad * math.sin(a1), z0),
                 (rad * math.cos(a1), rad * math.sin(a1), z1), (rad * math.cos(a0), rad * math.sin(a0), z1)]
            u = lvl(base * (0.78 + 0.22 * rr.random()) * (0.55 if rr.random() < 0.06 else 1.0)); v = (b_ * 36 + i) / 216.0
            mb.quad(*p, uv=(u, v, u, v), double=False)
            if i % 3 == 0:
                B_.m("trim").beam((rad * 1.004 * math.cos(a0), rad * 1.004 * math.sin(a0), z0), (rad * 1.004 * math.cos(a0), rad * 1.004 * math.sin(a0), z1), 0.09)
        B_.m("trim").cyl((0, 0, b_ * hh - 0.3), rad + 0.25, 0.6, n=36)
    ucap = lvl(0.22)
    b0 = len(B_.m("landmark").uv)
    B_.m("landmark").cyl((0, 0, 50.0), 14.1, 4.6, n=36, r_top=3.5, cap=False)
    for i in range(b0, len(B_.m("landmark").uv)):
        B_.m("landmark").uv[i] = (ucap, 0.99)
    B_.m("trim").cyl((0, 0, 54.6), 3.5, 0.5, n=18)
    B_.finish()
    return F_, B_


def lighthouse(name, seed, height=46.0, house_floors=3, base_w=7.2, lattice=18.0):
    """A Lanternwright beacon tower for a cloud-rock outcrop: braced legs, a keeper's stack of shifted blocks, a gallery,
    and a lantern room whose glazing is eight panes in framed openings (material beam_lamp). The beams themselves are the
    scene's (two opposed volumetric spots turning about meta.lamp_m; see layout.json "beams")."""
    r = random.Random(seed); r2 = random.Random(seed + 1); A = Asset(name)
    # legs go far down into the cloud rock
    for sx in (-1, 1):
        for sy in (-1, 1):
            A.m("metal").beam((sx * (base_w / 2 + 2.2), sy * (base_w / 2 + 2.2), -26), (sx * (base_w / 2 - 0.4), sy * (base_w / 2 - 0.4), lattice), 0.36)
    for k in range(4):
        z0, z1 = -2 + k * (lattice + 2) / 4, -2 + (k + 1) * (lattice + 2) / 4
        f0 = (base_w / 2 + 2.2) + ((base_w / 2 - 0.4) - (base_w / 2 + 2.2)) * (z0 + 26) / (lattice + 26)
        f1 = (base_w / 2 + 2.2) + ((base_w / 2 - 0.4) - (base_w / 2 + 2.2)) * (z1 + 26) / (lattice + 26)
        for (ax, ay, bx_, by_) in ((-1, -1, 1, -1), (1, -1, 1, 1), (1, 1, -1, 1), (-1, 1, -1, -1)):
            A.m("metal").beam((ax * f0, ay * f0, z0), (bx_ * f1, by_ * f1, z1), 0.12); A.m("metal").beam((bx_ * f0, by_ * f0, z0), (ax * f1, ay * f1, z1), 0.12)
            A.m("metal").beam((ax * f1, ay * f1, z1), (bx_ * f1, by_ * f1, z1), 0.16)
    A.m("trim").box((0, 0, lattice + 0.11), (base_w + 2.6, base_w + 2.6, 0.22))                       # landing deck with rail
    for (sx, sy, lw, ld) in ((0, -1, base_w + 2.5, 0.05), (0, 1, base_w + 2.5, 0.05), (-1, 0, 0.05, base_w + 2.5), (1, 0, 0.05, base_w + 2.5)):
        A.m("metal").box((sx * (base_w / 2 + 1.25), sy * (base_w / 2 + 1.25), lattice + 1.25), (lw, ld, 0.05))
        A.m("metal").box((sx * (base_w / 2 + 1.25), sy * (base_w / 2 + 1.25), lattice + 0.75), (lw, ld, 0.03))
    for sx in (-1, 1):
        for sy in (-1, 1):
            A.m("metal").box((sx * (base_w / 2 + 1.25), sy * (base_w / 2 + 1.25), lattice + 0.75), (0.06, 0.06, 1.1))
    A.m("metal").beam((base_w / 2 + 1.0, -base_w / 2 - 1.0, lattice), (base_w / 2 + 4.5, -base_w / 2 - 1.0, -8.0), 0.5, t2=0.08)   # a stair down to a skiff
    z = lattice + 0.22; w = base_w; top = None; fl_left = house_floors; first = True
    while fl_left > 0:                                                         # the keeper's stack
        n = min(fl_left, 2 if first else r.choice([1, 2]))
        ox, oy = (0, 0) if first else (r.choice([-0.6, 0.6]), r.choice([-0.6, 0, 0.6]))
        ww = w if first else w - 2.4
        block(A, r, r2, ox, oy, z, ww, ww, n, 0.45, 0.7, seed + z, balcony_side=None, blank_p=0.2)
        z += n * FLOOR; fl_left -= n
        A.m("trim").box((ox, oy, z), (ww + 2.0, ww + 2.0, 0.22)); A.m("strip").box((ox, oy - ww / 2 - 0.8, z - 0.15), (ww + 1.4, 0.07, 0.06))
        top = (ox, oy, z, ww); first = False
    ox, oy, z, ww = top
    sh_r0, sh_r1 = 1.75, 1.3                                                    # the tower shaft: a tapering octagon with ring bands
    shaft_h = height - z - 6.2
    A.m("facade").cyl((ox, oy, z), sh_r0, shaft_h, n=8, r_top=sh_r1)
    k = z + 3.0
    while k < z + shaft_h - 1.0:
        f = (k - z) / shaft_h
        A.m("trim").cyl((ox, oy, k), sh_r0 + (sh_r1 - sh_r0) * f + 0.08, 0.16, n=8)
        k += 4.2
    zg = z + shaft_h                                                            # gallery deck, brackets, rail
    A.m("trim").cyl((ox, oy, zg), 3.3, 0.2, n=16)
    for i in range(8):
        a = i / 8 * 6.283
        A.m("metal").beam((ox + 1.3 * math.cos(a), oy + 1.3 * math.sin(a), zg - 2.2), (ox + 3.1 * math.cos(a), oy + 3.1 * math.sin(a), zg), 0.1)
    for i in range(16):
        a0, a1 = i / 16 * 6.283, (i + 1) / 16 * 6.283
        p0, p1 = Vector((ox + 3.2 * math.cos(a0), oy + 3.2 * math.sin(a0), zg + 1.25)), Vector((ox + 3.2 * math.cos(a1), oy + 3.2 * math.sin(a1), zg + 1.25))
        A.m("metal").beam(p0, p1, 0.05); A.m("metal").beam(p0 - Z * 0.5, p1 - Z * 0.5, 0.03); A.m("metal").box((p0.x, p0.y, zg + 0.72), (0.04, 0.04, 1.06))
    A.m("strip").cyl((ox, oy, zg - 0.08), 3.26, 0.06, n=16, cap=False)                               # a light line under the gallery
    # the lantern room: eight wall cells, each a pane in a framed opening
    lr, lh = 2.05, 3.6
    for i in range(8):
        a0, a1 = (i - 0.5) / 8 * 6.283, (i + 0.5) / 8 * 6.283
        P0 = Vector((ox + lr * math.cos(a0), oy + lr * math.sin(a0), 0)); P1 = Vector((ox + lr * math.cos(a1), oy + lr * math.sin(a1), 0))
        T = (P1 - P0).normalized(); Nn = T.cross(Z); L_ = (P1 - P0).length
        pane_opening(A, P0, T, Nn, (0.16, L_ - 0.16, zg + 0.2 + 0.75, zg + 0.2 + lh - 0.3), (0, L_, zg + 0.2, zg + 0.2 + lh), 0.07, "beam_lamp", 0.85, (i + 0.5) / 8.0, sill=False,
                     mullions=0, transom=None, kind="lantern_room", clear=1.0, frame_mat="metal", fill_mat="trim")
    zt = zg + 0.2 + lh
    A.m("trim").cyl((ox, oy, zt), 2.5, 0.22, n=8); A.m("metal").cyl((ox, oy, zt + 0.22), 2.3, 1.5, n=8, r_top=0.4)
    A.m("metal").cyl((ox, oy, zt + 1.7), 0.25, 0.5, n=8); A.m("metal").cyl((ox, oy, zt + 2.2), 0.05, 2.6, n=6); A.m("beacon").sphere((ox, oy, zt + 4.9), 0.16, n=8, m=5)
    A.meta = dict(height_m=round(zt + 5.0, 1), lamp_m=[round(ox, 2), round(zg + 0.2 + 0.75 + (lh - 1.05) / 2, 2), round(-oy, 2)], gallery_y_m=round(zg + 0.1, 2),
                  deck_y_m=round(lattice + 0.22, 2), panes="beam_lamp, uv.v = pane bearing / 360 (pane 0 faces +x), so a program can spin the lit pane with the beam")
    A.finish()
    return A


def beam_cone():
    """Fallback beam geometry if the fog-lit spot is not enough: a unit cone along +x, 1 m long, radius 0.02 at the lamp
    and 0.12 at the far end; scale x by the beam length and y/z with it. UV u runs along the beam (0 at the lamp), so
    an additive material can fade it with distance."""
    A = Asset("beam_cone_unit"); n = 12; mb = A.m("beam")
    for i in range(n):
        a0, a1 = i / n * 6.283, (i + 1) / n * 6.283
        p = [(0, 0.02 * math.cos(a0), 0.02 * math.sin(a0)), (0, 0.02 * math.cos(a1), 0.02 * math.sin(a1)), (1, 0.12 * math.cos(a1), 0.12 * math.sin(a1)), (1, 0.12 * math.cos(a0), 0.12 * math.sin(a0))]
        mb._add(p, [(0, 1, 2, 3)], None, [(0, i / n), (0, (i + 1) / n), (1, (i + 1) / n), (1, i / n)])
    A.finish()
    return A


# ------------------------------------------------------------------------------------------- decks and street furniture
def deck_kit():
    out = []
    A = Asset("deck_quay_6m")                       # one 6 m length of the Quay, 8.6 m wide; instance along the street spline
    A.m("deck").box((0, 0, -0.14), (6.0, 8.6, 0.28)); A.m("trim").box((0, 0, -0.42), (6.0, 8.2, 0.28))
    A.m("trim").box((3.0, 0, -0.17), (0.7, 8.6, 0.3))                                                  # joint plate under the seam (covers the gap on curves)
    for sy in (-4.2, 4.2):
        A.m("trim").box((0, sy, -0.3), (6.0, 0.24, 0.6)); A.m("metal").box((0, sy * 0.98, 0.07), (6.0, 0.1, 0.14))
    for sy in (-3.7, 3.7):
        A.m("metal").cyl((0, sy, -14), 0.28, 13.4, n=8, cap=False)
    A.m("metal").beam((0, -3.7, -1.2), (0, 3.7, -7.0), 0.1); A.m("metal").beam((0, 3.7, -1.2), (0, -3.7, -7.0), 0.1); A.m("metal").beam((0, -3.7, -7.3), (0, 3.7, -7.3), 0.16)
    A.finish(); out.append(A)
    A = Asset("deck_stair_3m2")                     # one floor (3.2 m) down over 4.8 m toward +x, full street width; origin at the foot centre
    for i in range(16):
        A.m("deck").box((-2.4 + (i + 0.5) * 0.3, 0, 3.2 - (i + 0.5) * 0.2 - 0.1), (0.3, 8.6, 0.2))
    for sy in (-4.2, 4.2):
        A.m("trim").beam((-2.4, sy, 3.2), (2.4, sy, 0.0), 0.2, t2=0.5); A.m("metal").beam((-2.4, sy, 4.25), (2.4, sy, 1.05), 0.05)
        for k in range(5):
            x = -2.4 + k * 1.2; z = 3.2 - k * 0.8
            A.m("metal").box((x, sy, z + 0.52), (0.04, 0.04, 1.05))
    A.m("metal").beam((-2.4, 0, 4.25), (2.4, 0, 1.05), 0.05)                                           # centre handrail
    for k in range(5):
        A.m("metal").box((-2.4 + k * 1.2, 0, 3.2 - k * 0.8 + 0.52), (0.04, 0.04, 1.05))
    A.m("strip").beam((-2.4, 0, 3.02), (2.4, 0, -0.18), 7.6, t2=0.05)
    A.finish(); out.append(A)
    for nm, garden in (("gangway_10m", False), ("gangway_garden_10m", True)):
        A = Asset(nm); r = random.Random(77)        # a footbridge between buildings at third-floor height (along y)
        A.m("deck").box((0, 0, -0.1), (2.0, 10.0, 0.2)); A.m("trim").box((0, 0, -0.32), (1.5, 10.0, 0.26))
        for sx in (-0.95, 0.95):
            A.m("metal").box((sx, 0, 1.05), (0.05, 10.0, 0.05)); A.m("metal").box((sx, 0, 0.55), (0.03, 10.0, 0.03))
            for k in range(11):
                A.m("metal").box((sx, -5 + k, 0.52), (0.035, 0.035, 1.05))
        A.m("strip").box((0, 0, -0.46), (0.08, 9.6, 0.05))
        if garden:                                  # the hanging garden: troughs outside both rails, tidevine below
            for sx in (-1.18, 1.18):
                for yy in (-2.9, 0.2, 3.1):
                    trough(A, r, (sx, yy + r.uniform(-0.3, 0.3), -0.05), 2.4, math.pi / 2, depth=0.34, height=0.3)
                for k in range(9):
                    vine(A, r, (sx * 1.1, -4.4 + k * 1.1 + r.uniform(-0.3, 0.3), -0.1), r.uniform(1.0, 3.6), 0.12)
        A.finish(); out.append(A)
    A = Asset("catwalk_stair")                      # a narrow flight: 3.2 m down over 4.8 m toward +x, 2.6 m wide; origin at the foot centre
    for i in range(16):
        A.m("deck").box((-2.4 + (i + 0.5) * 0.3, 0, 3.2 - (i + 0.5) * 0.2 - 0.1), (0.3, 2.6, 0.2))
    for sy in (-1.25, 1.25):
        A.m("trim").beam((-2.4, sy, 3.1), (2.4, sy, -0.1), 0.12, t2=0.4); A.m("metal").beam((-2.4, sy, 4.25), (2.4, sy, 1.05), 0.05)
        for k in range(5):
            A.m("metal").box((-2.4 + k * 1.2, sy, 3.2 - k * 0.8 + 0.52), (0.04, 0.04, 1.05))
    A.m("metal").cyl((0, 0, -12), 0.2, 13.0, n=8, cap=False)
    A.finish(); out.append(A)
    A = Asset("alley_stair")                        # a scaffold stair tower in an alley: 2.2 x 3.4 m, landings every 3.2 m up to 12.8 m (gangway level)
    for sx in (-1.0, 1.0):
        for sy in (-1.6, 1.6):
            A.m("metal").cyl((sx, sy, -10), 0.07, 24.0, n=6, cap=False)
    for k in range(4):
        z0 = k * 3.2
        A.m("deck").box((0, 1.25 if k % 2 == 0 else -1.25, z0 + 3.2 - 0.05), (2.1, 0.9, 0.1))                    # landing
        a, b = ((0, -1.3, z0), (0, 0.9, z0 + 3.2)) if k % 2 == 0 else ((0, 1.3, z0), (0, -0.9, z0 + 3.2))
        A.m("deck").beam(a, b, 0.9, t2=0.08)                                                                    # the flight (a ramp of treads at this scale)
        for sx in (-0.5, 0.5):
            A.m("metal").beam((a[0] + sx, a[1], a[2] + 1.0), (b[0] + sx, b[1], b[2] + 1.0), 0.04)
        for sy in (-1.6, 1.6):
            A.m("metal").beam((-1.0, sy, z0 + 0.3), (1.0, sy, z0 + 3.0), 0.035); A.m("metal").box((0, sy, z0 + 3.2), (2.0, 0.05, 0.05))
    A.m("deck").box((0, 0, 12.8 - 0.05), (2.2, 3.4, 0.1)); A.m("trim").box((0, 0, 15.3), (2.6, 3.8, 0.1))
    for sy in (-1.65, 1.65):
        A.m("metal").box((0, sy, 13.85), (2.2, 0.04, 0.04))
    lamp(A.m("lantern"), (0, 0, 15.05), 0.12, 0.8, 0.5)
    A.meta = dict(top_y_m=12.8, footprint_m=[2.2, 3.4]); A.finish(); out.append(A)
    A = Asset("pier_span_22m")                      # the pier: instance end to end; drop and tilt the last ones for the broken spans
    A.m("deck").box((0, 0, -0.2), (22.0, 5.5, 0.4)); A.m("trim").box((0, 0, -0.75), (22.0, 1.2, 0.7))
    for sy in (-2.65, 2.65):
        A.m("trim").box((0, sy, -0.35), (22.0, 0.2, 0.7)); A.m("metal").box((0, sy, 1.05), (22.0, 0.05, 0.05)); A.m("metal").box((0, sy, 0.55), (22.0, 0.03, 0.03))
        for k in range(12):
            A.m("metal").box((-10.9 + k * 1.98, sy, 0.52), (0.04, 0.04, 1.05))
        for k in range(3):
            x = -8.0 + k * 8.0
            A.m("metal").box((x, sy, 0.9), (0.06, 0.06, 1.8)); lamp(A.m("lantern"), (x, sy, 1.9), 0.14, 0.75, 0.2 + 0.25 * k + (0.1 if sy > 0 else 0.0))
    for sy in (-1, 1):                               # A-frame legs splay outward and are lost in the cloud
        A.m("metal").beam((0, sy * 2.2, -0.9), (0, sy * 6.5, -70.0), 0.42)
        A.m("metal").beam((0, sy * 2.6, -8.0), (0, -sy * 3.0, -22.0), 0.14)
    for z in (-12.0, -30.0, -52.0):
        w_ = 2.2 + (-z) / 70.0 * 4.3
        A.m("metal").beam((0, -w_, z), (0, w_, z), 0.18)
    A.finish(); out.append(A)
    A = Asset("deck_lowwalk_6m")                    # the Low Walk: 2.6 m wide, a rail on the cloud side (-y)
    A.m("deck").box((0, 0, -0.12), (6.0, 2.6, 0.24)); A.m("trim").box((0, 0, -0.36), (6.0, 2.2, 0.24))
    A.m("metal").box((0, -1.25, 1.05), (6.0, 0.05, 0.05)); A.m("metal").box((0, -1.25, 0.55), (6.0, 0.03, 0.03))
    for k in range(4):
        A.m("metal").box((-3.0 + k * 2.0, -1.25, 0.52), (0.04, 0.04, 1.05))
    A.m("metal").cyl((0, -1.0, -12), 0.2, 11.6, n=8, cap=False); A.m("metal").beam((0, -1.0, -1.0), (0, 1.2, 4.5), 0.08)
    A.finish(); out.append(A)
    A = Asset("deck_pad_6m")                        # a 6 x 6 m deck square: plazas, pads under the Stacks, pocket gardens
    A.m("deck").box((0, 0, -0.14), (6.0, 6.0, 0.28)); A.m("trim").box((0, 0, -0.42), (5.7, 5.7, 0.28))
    A.m("metal").cyl((0, 0, -14), 0.3, 13.4, n=8, cap=False)
    for (ax, ay) in ((2.6, 2.6), (-2.6, 2.6), (2.6, -2.6), (-2.6, -2.6)):
        A.m("metal").beam((0, 0, -5.0), (ax, ay, -0.5), 0.1)
    A.finish(); out.append(A)
    for nm, rad, legs in (("deck_round_27m", 27.0, 9), ("deck_round_14m", 14.0, 6)):
        A = Asset(nm)
        A.m("deck").cyl((0, 0, -0.3), rad, 0.3, n=36 if rad > 20 else 24); A.m("trim").cyl((0, 0, -0.75), rad - 0.6, 0.45, n=36 if rad > 20 else 24)
        for k in range(legs):
            a = k / legs * 6.283
            A.m("metal").cyl(((rad - 4) * math.cos(a), (rad - 4) * math.sin(a), -16), 0.32, 15.4, n=8, cap=False)
        A.finish(); out.append(A)
    A = Asset("rail_6m")                            # a deck-edge railing along x, 1.05 m high
    A.m("metal").box((0, 0, 1.05), (6.0, 0.05, 0.05)); A.m("metal").box((0, 0, 0.55), (6.0, 0.03, 0.03)); A.m("metal").box((0, 0, 0.12), (6.0, 0.03, 0.03))
    for k in range(5):
        A.m("metal").box((-3.0 + k * 1.5, 0, 0.52), (0.045, 0.045, 1.05))
    A.finish(); out.append(A)
    A = Asset("pool_15m")                           # the garden pool: a dark mirror with a low kerb
    A.m("pool").cyl((0, 0, 0.02), 7.5, 0.04, n=32); A.m("trim").cyl((0, 0, 0.0), 7.8, 0.14, n=32, cap=False)
    ring = [(7.5 * math.cos(i / 32 * 6.283), 7.5 * math.sin(i / 32 * 6.283), 0.14) for i in range(32)]
    ring2 = [(7.8 * math.cos(i / 32 * 6.283), 7.8 * math.sin(i / 32 * 6.283), 0.14) for i in range(32)]
    for i in range(32):
        A.m("trim")._add([ring[i], ring[(i + 1) % 32], ring2[(i + 1) % 32], ring2[i]], [(0, 1, 2, 3)])
    A.finish(); out.append(A)
    return out


def street_kit():
    """What the Quay close pass sees at 5 to 15 m: lantern strings, awnings, lamps, keel lanterns, clutter."""
    out = []
    A = Asset("lantern_string_9m"); r = random.Random(21)      # anchors at (+-4.5, 0, 0); sags 0.9 m; scale x to fit a span
    prev = None
    for k in range(25):
        t = k / 24; x = -4.5 + 9.0 * t; z = -0.9 * math.sin(t * math.pi)
        if prev:
            A.m("metal").beam(prev, (x, 0, z), 0.018)
        prev = (x, 0, z)
    for k in range(9):
        t = (k + 0.5) / 9; x = -4.5 + 9.0 * t; z = -0.9 * math.sin(t * math.pi); big = k % 3 == 1
        hh, rr_ = (0.34, 0.17) if big else (0.24, 0.12)
        A.m("metal").box((x, 0, z - 0.06), (0.012, 0.012, 0.12))
        mb = A.m("lantern"); b0 = len(mb.uv)
        mb.cyl((x, 0, z - 0.12 - hh), rr_ * 0.7, hh / 2, n=8, r_top=rr_, cap=True); mb.cyl((x, 0, z - 0.12 - hh / 2), rr_, hh / 2, n=8, r_top=rr_ * 0.7, cap=True)
        u = (lvl(r.uniform(0.5, 1.0)), (k + 0.5) / 9.0)
        for i in range(b0, len(mb.uv)):
            mb.uv[i] = u
        A.m("metal").cyl((x, 0, z - 0.13), rr_ * 0.72, 0.03, n=8)
    A.meta = dict(span_m=9.0, sag_m=0.9, bulbs=9); A.finish(); out.append(A)
    for nm, w_ in (("awning_1bay", 2.4), ("awning_2bay", 4.8), ("awning_3bay", 7.2)):
        A = Asset(nm); r = random.Random(int(w_ * 10))          # mounts on the wall above a shop bay: origin at the wall, 2.62 m above the deck
        dep, drop = 1.7, 0.5
        p = [(-w_ / 2 + 0.1, 0, 0.3), (w_ / 2 - 0.1, 0, 0.3), (w_ / 2 - 0.1, -dep, 0.3 - drop), (-w_ / 2 + 0.1, -dep, 0.3 - drop)]
        A.m("cloth").quad(p[3], p[2], p[1], p[0], uv=(0, 0, w_ / 2.4, 1), double=True)
        A.m("cloth").quad((-w_ / 2 + 0.1, -dep, 0.3 - drop), (w_ / 2 - 0.1, -dep, 0.3 - drop), (w_ / 2 - 0.1, -dep, 0.3 - drop - 0.26), (-w_ / 2 + 0.1, -dep, 0.3 - drop - 0.26), uv=(0, 0, w_ / 2.4, 0.2), double=True)
        for sx in (-w_ / 2 + 0.1, w_ / 2 - 0.1):
            A.m("metal").beam((sx, 0, 0.3), (sx, -dep, 0.3 - drop), 0.04); A.m("metal").beam((sx, 0, -0.45), (sx, -dep, 0.3 - drop), 0.03)
        A.m("metal").beam((-w_ / 2 + 0.1, -dep, 0.3 - drop), (w_ / 2 - 0.1, -dep, 0.3 - drop), 0.04)
        n_l = int(w_ / 1.2)
        for k in range(n_l):                                    # lamps under the awning light the shop front and whoever stands at it
            lamp(A.m("lantern"), (-w_ / 2 + (k + 0.5) * w_ / n_l, -dep * 0.55, 0.3 - drop * 0.55 - 0.16), 0.07, r.uniform(0.5, 0.9), (k + 0.5) / n_l)
        A.meta = dict(mount="origin on the wall above a shop bay, 2.62 m above the deck; projects 1.7 m out of the wall (the building's front)", width_m=w_); A.finish(); out.append(A)
    A = Asset("street_lamp")                                    # a deck lamp: 4.2 m post, a caged lantern on a short arm
    A.m("metal").cyl((0, 0, 0), 0.09, 0.5, n=8, r_top=0.06); A.m("metal").cyl((0, 0, 0.5), 0.05, 3.7, n=6)
    A.m("metal").beam((0, 0, 4.1), (0.55, 0, 4.25), 0.04); A.m("metal").box((0.55, 0, 4.12), (0.02, 0.02, 0.22))
    mb = A.m("lantern"); b0 = len(mb.uv); mb.cyl((0.55, 0, 3.66), 0.12, 0.36, n=8, r_top=0.16)
    for i in range(b0, len(mb.uv)):
        mb.uv[i] = (lvl(0.85), 0.5)
    A.m("metal").cyl((0.55, 0, 4.02), 0.2, 0.05, n=8, r_top=0.05)
    A.finish(); out.append(A)
    for nm, ln in (("keel_lantern_8m", 8.0), ("keel_lantern_16m", 16.0), ("keel_lantern_26m", 26.0)):
        A = Asset(nm)                                           # hangs under a deck into the cloud: origin at the deck underside
        A.m("metal").cyl((0, 0, -ln), 0.02, ln, n=4, cap=False)
        mb = A.m("keel"); b0 = len(mb.uv); mb.cyl((0, 0, -ln - 0.7), 0.16, 0.35, n=8, r_top=0.26); mb.cyl((0, 0, -ln - 0.35), 0.26, 0.35, n=8, r_top=0.16)
        for i in range(b0, len(mb.uv)):
            mb.uv[i] = (lvl(0.9), 0.5)
        A.m("metal").cyl((0, 0, -ln - 0.02), 0.2, 0.05, n=8); A.m("metal").cyl((0, 0, -ln - 0.76), 0.2, 0.05, n=8)
        A.finish(); out.append(A)
    A = Asset("crate_stack"); r = random.Random(5)
    for (x, y, z, s_, yw) in ((0, 0, 0.3, 0.6, 0.1), (0.7, 0.1, 0.3, 0.6, -0.2), (0.3, 0.05, 0.9, 0.6, 0.3), (-0.6, 0.5, 0.2, 0.4, 0.5)):
        A.m("trim").box((x, y, z), (s_, s_, s_), yw); A.m("metal").box((x, y, z + s_ / 2 - 0.02), (s_ + 0.03, s_ + 0.03, 0.03), yw)
    A.finish(); out.append(A)
    A = Asset("mooring_post")                                   # a bollard on the Low Walk with a rope coil
    A.m("metal").cyl((0, 0, 0), 0.13, 0.75, n=8, r_top=0.1); A.m("metal").cyl((0, 0, 0.75), 0.17, 0.1, n=8); A.m("trim").cyl((0.4, 0.1, 0), 0.26, 0.14, n=10)
    A.finish(); out.append(A)
    A = Asset("cable_unit")                                     # 1 m of cable along x: scale x to the span
    A.m("metal").cyl((0, 0, 0), 0.03, 1.0, n=6, cap=False)
    A.finish()
    for v in A.ob.data.vertices:                                # lay the cylinder along +x
        v.co = Vector((v.co.z, v.co.y, -v.co.x))
    out.append(A)
    return out


def craft():
    out = []
    A = Asset("gondola_cabin")                      # hangs from a cable 3.6 m above the cabin floor; origin at the floor centre
    A.m("facade").box((0, 0, 2.46), (4.3, 2.3, 0.12)); A.m("metal").box((0, 0, -0.08), (4.1, 2.1, 0.16))
    for (o_, t_, L_, nwin) in (((-2.0, -1.0, 0), (1, 0, 0), 4.0, 4), ((2.0, -1.0, 0), (0, 1, 0), 2.0, 1), ((2.0, 1.0, 0), (-1, 0, 0), 4.0, 4), ((-2.0, 1.0, 0), (0, -1, 0), 2.0, 1)):
        O, T, Nn = wall(o_, t_); cw_ = L_ / nwin
        for k in range(nwin):
            u = k * cw_; ins = 0.09 if nwin > 1 else 0.3
            pane_opening(A, O, T, Nn, (u + ins, u + cw_ - ins, 0.95, 1.95), (u, u + cw_, 0.0, 2.4), 0.06, "win_warm", 0.72, 0.5, sill=False, mullions=0,
                         kind="cabin", clear=1.0, fill_mat="trim", frame_mat="metal")
    A.m("metal").beam((0, 0, 2.5), (0, 0, 3.5), 0.1); A.m("metal").box((0, 0, 3.6), (1.6, 0.14, 0.2))
    for sx in (-0.7, 0.7):
        A.m("metal").cyl((sx, -0.1, 3.5), 0.18, 0.2, n=10)
    A.m("beacon").sphere((0, 0, -0.25), 0.1, n=8, m=5)
    A.meta = dict(cable_y_m=3.6); A.finish(); out.append(A)
    A = Asset("cloud_skiff")                        # a moored boat of the cloud sea; origin at the keel line amidships
    for (x, w_, l_) in ((-2.4, 1.3, 1.8), (-0.6, 2.0, 2.0), (1.4, 2.0, 2.0), (3.0, 1.2, 1.4)):
        A.m("trim").box((x, 0, 0.45), (l_, w_, 0.9))
    A.m("metal").box((0.4, 0, 0.94), (6.6, 2.1, 0.08)); A.m("trim").box((1.0, 0, 2.2), (3.0, 1.9, 0.1))
    for (o_, t_, L_, win) in (((-0.3, -0.8, 0), (1, 0, 0), 2.6, 2), ((2.3, -0.8, 0), (0, 1, 0), 1.6, 0), ((2.3, 0.8, 0), (-1, 0, 0), 2.6, 2), ((-0.3, 0.8, 0), (0, -1, 0), 1.6, 1)):
        O, T, Nn = wall(o_, t_)
        if win == 0:
            wq(A, "facade", O, T, Nn, 0, L_, 0.98, 2.15)
        for k in range(win):
            cw_ = L_ / win; u = k * cw_
            pane_opening(A, O, T, Nn, (u + 0.2, u + cw_ - 0.2, 1.3, 1.9), (u, u + cw_, 0.98, 2.15), 0.06, "win_warm", 0.6, 0.5, sill=False, mullions=0, kind="cabin",
                         clear=0.8, frame_mat="metal")
    A.m("metal").cyl((-2.2, 0, 0.9), 0.05, 3.4, n=6); lamp(A.m("lantern"), (-2.2, 0, 4.42), 0.16, 0.9, 0.5)
    A.m("metal").beam((-2.2, 0, 4.2), (2.3, 0, 2.3), 0.02)
    A.finish(); out.append(A)
    return out
