#!/usr/bin/env python3
"""Neon Dreams: write art/cloud_design.json for Cloud Environment v2 (ADR-2100). Pure Python, deterministic.

    python3 examples/neon-dreams/blender/nd_clouds.py

What a formation is here (the round-1 clouds were thumbs: a few big balls under a displacement):
  * a SKIRT of wide flattened lobes that meets the sea;
  * two to four TURRETS of unequal height, each a leaning chain of core lobes (the lean is the wind);
  * CAULIFLOWER: every core lobe carries children on its upper half and sides, and they carry grandchildren, so the
    silhouette is hemispheres at three scales (150 m, 50 m, 18 m), not one curve;
  * SHELVES: thin horizontal sheets at one or two levels, pushed out downwind, pierced by the turrets, with ragged ends
    and a few detached scraps;
  * ROCKS (the lighthouse outcrops, the Condensery's column): the same recipe, compact and dense, steep sides, crisp
    edges (soft 0.16 to 0.24), a calm top, mist lobes round the foot.
The plateau under the Mooring follows nd_world.town_top plus SEAT: level with the decks as authored, never over them.

Output: lobes in the ADR-1289 shape {c, r, scale, density, soft}, ENGINE coordinates, plus a ready `clouds` block for
the engine (sea, glows with palette keys, light direction, noise) and a replica of the renderer's lobe binning so the
file can be checked against its limits (64 lobes per cell, cells of half the median radius or span / 128).

Iteration 6 (version 4 of the file): the far field is a composed cloudscape (far_field); the Veil's walls are wall masses
only and its plug is one layer of big lobes (veil); the reveal has a foreground (reveal_fore); the ocean break lies under
the Condensery's pedestal; the cloud that decks stand in is lifted by SEAT. What the engine's ADR-2106 erosion does to a
lobe, and what that costs, is written where it bit: SEAT, VEIL_WALLS, veil().
"""
import json, math, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nd_world import *  # noqa

WIND = (0.86, -0.5)            # plan direction the wind blows toward (east-south-east): leans, shelves and the plume follow it
# Iteration 6, after the engine's ADR-2106 (erosion in metres): every lobe's surface is now eaten inward by up to 0.88 of
# its smallest semi-axis (about 0.3 to 0.45 on average), in every direction. Measured in AV Gen with erosion 1 and a 60 m
# wavelength: the plateau under the Mooring and the heads under the Condensery's deck, authored 5 m under the decks, sat
# 15 to 30 m under them, and every stilt stood in the open. SEAT lifts the authored tops of the cloud that decks stand
# in (the plateau's body and shelves, the Condensery's heads, the lighthouse rocks) by 3 m. The heads beside the
# buildings keep their heights of iteration 5: they stand beside decks, not under them.
# What that buys, and what it risks. The seat follows town_top, which is smooth, and the decks step, so "2 m under the
# decks" is an average: measured on this file, the authored cloud is above the deck in 118 of the 381 open cells of the
# upper Quay's level (by more than 1 m in 82, at most 4 m). The erosion only goes inward: with all of it (about 9 m off
# a shelf) the plateau is 8 to 15 m under the decks; with 0.3 of it, about 5 m under, and a few cells may carry a
# shallow mist; with NONE, cloud lies on parts of the Quay. So: lobes tagged "seat" want a share of the erosion near 0.3
# (the engine's LOBE_ERODE, ADR-2107), not 0. At 5 m with the heads lifted too (first try) 215 of 1,056 open deck cells
# had authored cloud more than 3 m over them.
SEAT = 3.0
_town_top0 = town_top


def town_top(x, y):          # noqa: F811
    return _town_top0(x, y) + SEAT


SEA_TOP, SEA_BASE = -38.0, -260.0


def L(x, y, z, r, sy=1.0, dens=1.0, soft=0.35, sx=1.0, sz=1.0):
    return {"c": [round(x, 1), round(z, 1), round(-y, 1)], "r": round(r, 1), "scale": [round(sx, 2), round(sy, 2), round(sz, 2)], "density": round(dens, 2), "soft": round(soft, 2)}


def cauliflower(rr, out, x, y, z, r, depth, soft, dens=1.0, up=0.25, n=(5, 7), shrink=(0.3, 0.46), rmin=(26.0, 12.0)):
    """Children on the upper half and sides of a lobe, then their children. Nothing under 12 m: the renderer's noise
    takes over below that, and the lobe budget (4,096 lobes, 64 to a bin cell) is spent on the silhouette."""
    if depth == 0 or r < rmin[0]:
        return
    for k in range(rr.randint(*n)):
        ph = rr.uniform(0, 6.283); th = math.acos(rr.uniform(-up, 1.0))            # polar angle from the zenith: mostly upper half
        d = r * rr.uniform(0.72, 0.95); cr = r * rr.uniform(*shrink)
        if cr < rmin[1]:
            continue
        cx, cy, cz = x + d * math.sin(th) * math.cos(ph), y + d * math.sin(th) * math.sin(ph), z + d * math.cos(th)
        out.append(L(cx, cy, cz, cr, rr.uniform(0.8, 1.0), dens, soft))
        cauliflower(rr, out, cx, cy, cz, cr, depth - 1, soft, dens, up, (2, 3), (0.36, 0.5), rmin)


def shelf(rr, out, x, y, z, length, width, thick, yaw, dens=0.6, soft=0.5, scraps=3):
    """A shelf reaching out along yaw from inside a turret: a ragged mat of small flattened lobes, two or three abreast,
    narrowing and thinning toward the end. (One wide flat lobe reads as a saucer; a mat of small ones reads as cloud.)"""
    cs, sn = math.cos(yaw), math.sin(yaw); rows = max(3, int(length / (width * 0.42)))
    for i in range(rows):
        t = (i + 0.2) / rows; w = width * (1.0 - 0.55 * t)
        for j in range(2):
            off = (j - 0.5) * w * rr.uniform(0.5, 0.9) + rr.uniform(-0.12, 0.12) * w; r = w * rr.uniform(0.24, 0.36)
            px, py = x + cs * length * t - sn * off, y + sn * length * t + cs * off
            out.append(L(px, py, z + rr.uniform(-0.5, 0.5) * thick, r, min(0.7, max(0.24, thick / r * rr.uniform(0.7, 1.2))), dens * (1.0 - 0.4 * t), soft, sx=rr.uniform(1.0, 1.5), sz=rr.uniform(0.9, 1.2)))
        if rr.random() < 0.4:                                                   # a small dome on the mat
            off = rr.uniform(-0.3, 0.3) * w
            out.append(L(x + cs * length * t - sn * off, y + sn * length * t + cs * off, z + thick * 0.8, max(12.0, w * rr.uniform(0.12, 0.2)), 0.8, dens + 0.2, 0.35))
    for k in range(scraps):                                                     # detached scraps just beyond the end
        t = 1.0 + rr.uniform(0.05, 0.25); off = rr.uniform(-0.4, 0.4) * width
        out.append(L(x + cs * length * t - sn * off, y + sn * length * t + cs * off, z + rr.uniform(-0.5, 0.5) * thick, max(12.0, width * rr.uniform(0.08, 0.14)), 0.5, 0.45, 0.6, sx=1.5))


def cumulus(seed, x, y, base, top, radius, turrets=3, lean=0.22, shelves=((0.42, 1.2), (0.68, 0.7)), dense=False, skirt=True, mist=True, detail=2, kids=(5, 7), rich=True):
    """One formation. base/top: heights of its foot and of its tallest turret; radius: the foot's radius."""
    rr = random.Random(seed); out = []; H = top - base
    soft = 0.2 if dense else 0.28; dens = 1.0                                      # crisp enough that cauliflower stays cauliflower under the engine's noise
    if skirt:
        for k in range(rr.randint(6, 7)):
            a = k / 6.5 * 6.283 + rr.uniform(-0.3, 0.3); d = radius * rr.uniform(0.45, 0.95); r = radius * rr.uniform(0.36, 0.52); sy_ = rr.uniform(0.42, 0.62)
            out.append(L(x + d * math.cos(a), y + d * math.sin(a), base + 15 + r * sy_ * 0.9, r, sy_, dens, soft + 0.08))        # the skirt shows above the sea as a low dome ring
        for k in range(4 if rich else 3):                        # shoulders: the mass that keeps a formation from being a pillar
            a = rr.uniform(0, 6.283); d = radius * rr.uniform(0.2, 0.6); r = radius * rr.uniform(0.28, 0.4)
            sx_, sy2 = x + d * math.cos(a), y + d * math.sin(a); sz_ = base + H * rr.uniform(0.2, 0.42)
            out.append(L(sx_, sy2, sz_, r, rr.uniform(0.8, 1.0), dens, soft))
            if rich:
                cauliflower(rr, out, sx_, sy2, sz_, r, 1, soft, dens, n=(2, 3))
    tops = sorted([1.0] + [rr.uniform(0.45, 0.85) for _ in range(turrets - 1)], reverse=True)
    spots = [(rr.uniform(-0.25, 0.1) * radius, rr.uniform(-0.2, 0.2) * radius)] + [(radius * rr.uniform(0.35, 0.7) * math.cos(a), radius * rr.uniform(0.35, 0.7) * math.sin(a))
                                                                                   for a in [rr.uniform(0, 6.283) for _ in range(turrets - 1)]]
    cores = []
    for ti, (frac, (ox, oy)) in enumerate(zip(tops, spots)):
        th = H * frac; r = radius * ((0.56 if rich else 0.5) if ti == 0 else rr.uniform(0.32, 0.44)); z = base + r * 0.5; px, py = x + ox, y + oy
        while True:
            out.append(L(px, py, z, r, rr.uniform(0.88, 1.0), dens, soft)); cores.append((px, py, z, r))
            cauliflower(rr, out, px, py, z, r, detail, soft, dens, n=kids)
            if r > radius * 0.16 and (z - base) > th * 0.42:                    # a sibling beside each core of the upper half: a crown is a cluster of heads, never a spire
                a = rr.uniform(0, 6.283); sr = r * rr.uniform(0.68, 0.85)
                qx, qy_, qz_ = px + math.cos(a) * r * 0.85, py + math.sin(a) * r * 0.85, z - r * rr.uniform(0.05, 0.3)
                out.append(L(qx, qy_, qz_, sr, rr.uniform(0.85, 1.0), dens, soft))
                if rich:
                    cauliflower(rr, out, qx, qy_, qz_, sr, 1, soft, dens, n=(2, 3))
            low = (z - base) < th * 0.55                                         # a tower keeps its girth and bulges on the way up, and narrows only near its top: not a cone
            nz = z + r * rr.uniform(0.55, 0.75)
            nr = r * ((rr.uniform(0.8, 0.94) if rich else rr.uniform(0.88, 1.08)) if low else rr.uniform(0.72, 0.86))   # hero formations step in a little: fat all the way up, the Head was one dome
            if nz + nr * 0.8 > base + th or nr < radius * 0.1:
                break
            px += WIND[0] * r * lean * rr.uniform(0.5, 1.5) + rr.uniform(-0.2, 0.2) * r; py += WIND[1] * r * lean * rr.uniform(0.5, 1.5) + rr.uniform(-0.2, 0.2) * r
            z, r = nz, nr
    wyaw = math.atan2(WIND[1], WIND[0])
    for (hf, lf) in shelves:                                                    # shelves reach out downwind (one may go the other way)
        zz = base + H * hf; yaw = wyaw + rr.uniform(-0.5, 0.5) + (math.pi * 0.8 if rr.random() < 0.3 else 0.0)
        near = min(cores, key=lambda c: abs(c[2] - zz))
        shelf(rr, out, near[0], near[1], zz, near[3] + radius * lf * rr.uniform(0.5, 0.75), radius * rr.uniform(0.5, 0.75), radius * 0.075, yaw, dens=0.8 if dense else 0.62)
    if mist:
        for k in range(5):
            a = rr.uniform(0, 6.283); d = radius * rr.uniform(0.9, 1.5)
            out.append(L(x + d * math.cos(a), y + d * math.sin(a), base + rr.uniform(-10, 25), radius * rr.uniform(0.25, 0.4), 0.3, 0.4, 0.7, sx=1.4))
    return ragged(out)


def ragged(lobes):
    """The renderer's shape noise only bites inside a lobe's soft edge (it is taken off the density, so where the lobe
    is solid it changes nothing). A lobe of 100 m with a soft edge of 0.28 kept a circle for an outline at any
    extinction that gave it a surface. So the soft edge grows with the lobe: small heads stay crisp (0.2 to 0.3), a
    lobe of 45 m or more has 0.45 to 0.6 and the noise takes bays of 20 to 40 m out of it."""
    for l in lobes:
        re = l["r"] * min(l["scale"])
        if re > 45.0 and l["density"] >= 0.9:
            l["soft"] = round(max(l["soft"], min(0.6, 0.2 + re / 180.0)), 2)
    return lobes


def rock(seed, x, y, top, radius, base=SEA_TOP - 30):
    """A cloud-rock outcrop: dense, steep, a calm top a few metres under the deck that stands on it."""
    rr = random.Random(seed); out = []; H = top - base
    out.append(L(x, y, base + H * 0.45, radius * 0.95, H * 0.6 / (radius * 0.95), 1.0, 0.18))                   # the stock
    out.append(L(x, y, top - radius * 0.42 * 0.5, radius * 0.6, 0.5, 1.0, 0.16))                                # the calm cap
    for k in range(rr.randint(8, 11)):                                           # buttresses stepping down, each a little cauliflower
        a = k / 9.0 * 6.283 + rr.uniform(-0.3, 0.3); d = radius * rr.uniform(0.55, 0.95); r = radius * rr.uniform(0.32, 0.5)
        z = base + H * rr.uniform(0.25, 0.8)
        out.append(L(x + d * math.cos(a), y + d * math.sin(a), z, r, rr.uniform(0.85, 1.25), 1.0, 0.2))
        cauliflower(rr, out, x + d * math.cos(a), y + d * math.sin(a), z, r, 2, 0.22, 1.0, up=0.5, n=(3, 5), shrink=(0.36, 0.5), rmin=(radius * 0.26, radius * 0.13))
    wyaw = math.atan2(WIND[1], WIND[0])
    shelf(rr, out, x, y, base + H * 0.5, radius * 1.7, radius * 0.9, radius * 0.11, wyaw + rr.uniform(-0.4, 0.4), dens=0.65, soft=0.45)
    for k in range(7):                                                           # mist round the foot
        a = rr.uniform(0, 6.283); d = radius * rr.uniform(1.0, 1.9)
        out.append(L(x + d * math.cos(a), y + d * math.sin(a), base + rr.uniform(15, 45), radius * rr.uniform(0.3, 0.55), 0.28, 0.35, 0.75, sx=1.5))
    return out


# ---------------------------------------------------------------------------------------------------------------
# Iteration 5: the cloud is designed against the town and the journey, and checked against both.
ROOT = os.path.join(HERE, "..")
CELL = 4.0


NEW_ZONES = ("lanternhill", "lampmarket", "eastreach", "roof")      # iteration 9's districts: their cells are marked, so the cloud designed before them is made exactly as before


def town():
    """The Mooring's decks and buildings as plan cells of 4 m, from art/layout.json and the kit manifest:
    {(i, j): (deck height, True under a building)}. The plateau is built under them, the heads and wisps beside them."""
    lay = json.load(open(os.environ.get("ND_LAYOUT", os.path.join(ROOT, "art", "layout.json"))))          # ND_LAYOUT / ND_MANIFEST: a scratch run
    man = {a_["asset"]: a_ for a_ in json.load(open(os.environ.get("ND_MANIFEST", os.path.join(ROOT, "meshes", "kit_manifest.json"))))["assets"]}
    cells = {}
    for i in lay["instances"]:
        fam = i["asset"].split("/")[0]
        if fam not in ("decks", "buildings") or i["zone"] in ("condensery", "lone_lamp", "lighthouse", "gangway", "highwater") or i.get("bob") or "rail" in i["asset"] or "stair" in i["asset"]:
            continue
        b = man[i["asset"]]["bounds_m"]; sx, sy, sz = i["scale"]; yaw = math.radians(i["rot_y_deg"])
        if "deck_round" in i["asset"]:                                    # the round decks count by their radius (iteration 8 hung crescents outside the large one: the cloud may lie on those)
            rd = 27.0 if "27m" in i["asset"] else 14.0; b = [-rd, b[1], -rd, rd, b[4], rd]
        px, py, pz = i["pos"][0], -i["pos"][2], i["pos"][1]
        x0, x1, z0, z1 = b[0] * sx, b[3] * sx, b[2] * sz, b[5] * sz
        nx, nz = max(2, int((x1 - x0) / 2.0) + 1), max(2, int((z1 - z0) / 2.0) + 1)
        for a_ in range(nx + 1):
            for c_ in range(nz + 1):
                lx, ly = x0 + (x1 - x0) * a_ / nx, -(z0 + (z1 - z0) * c_ / nz)          # GLB (x, z) -> building-local plan (x, -z)
                wx, wy = px + lx * math.cos(yaw) - ly * math.sin(yaw), py + lx * math.sin(yaw) + ly * math.cos(yaw)
                k = (int(math.floor(wx / CELL)), int(math.floor(wy / CELL)))
                z0_, b0_, n0_ = cells.get(k, (-1e9, False, True))
                cells[k] = (max(z0_, pz), b0_ or fam == "buildings", n0_ and i["zone"].split("_")[0] in NEW_ZONES)
    return cells


def town_near(cells, x, y, reach):
    """(distance to the nearest deck or building cell, its deck height), or (None, None) beyond `reach` metres."""
    ci, cj = int(math.floor(x / CELL)), int(math.floor(y / CELL)); n = int(reach / CELL) + 1; best = (None, None)
    for i in range(ci - n, ci + n + 1):
        for j in range(cj - n, cj + n + 1):
            if (i, j) in cells:
                d = math.hypot((i + 0.5) * CELL - x, (j + 0.5) * CELL - y)
                if d <= reach and (best[0] is None or d < best[0]):
                    best = (d, cells[(i, j)][0])
    return best


def on_open_deck(cells, l, under=1.5, over=40.0):
    """True when lobe l (engine coordinates) stands in the air over an open deck: somewhere inside its footprint a deck
    cell with no building on it lies between the lobe's bottom and top there (from `under` metres below the deck to
    `over` above it). Cloud may wrap a building's lower floors and fill the space under a deck; it may not lie on a
    street, a terrace or a garden.
    (Until iteration 6 this read a lobe's scale as (x, plan y, up); it is (x, up, plan y), as L() writes it. For a head
    0.72 high that made the footprint 30 % too short north to south and the height 45 % too tall.)"""
    cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; rx, rv, ry = (l["r"] * v for v in l["scale"])
    for i in range(int(math.floor((cx - rx) / CELL)), int(math.floor((cx + rx) / CELL)) + 1):
        for j in range(int(math.floor((cy - ry) / CELL)), int(math.floor((cy + ry) / CELL)) + 1):
            c = cells.get((i, j))
            if c is None or c[1]:
                continue
            q = 1.0 - (((i + 0.5) * CELL - cx) / rx) ** 2 - (((j + 0.5) * CELL - cy) / ry) ** 2
            if q <= 0.0:
                continue
            h = rv * math.sqrt(q)
            if cz + h > c[0] - under and cz - h < c[0] + over:
                return True
    return False


def mound_dressing(cells):
    """Iteration 5: what stood round the town in the engine was one flat sheet with stilts through it. This puts cloud
    at the scale of the buildings beside them: cauliflower HEADS in the gaps and behind the rows (their tops from 3 m
    under the nearest street to two or three floors over it on the north side, lower on the south side where the Low
    Walk hangs over the sea), and WISPS, thin tatters drawn out along the wind at deck level. Nothing stands on an open
    deck or within 16 m of the journey: the cloud comes up to walls and stilts, not onto the street."""
    rr = random.Random(57); out = []; n_h = n_w = 0
    xs = [k[0] * CELL for k in cells]; ys = [k[1] * CELL for k in cells]
    y = min(ys) - 44.0
    while y < max(ys) + 44.0:
        x = min(xs) - 44.0
        while x < max(xs) + 44.0:
            px, py = x + rr.uniform(-5, 5), y + rr.uniform(-5, 5); x += 13.0
            d, dz = town_near(cells, px, py, 44.0)
            if d is None or d < 4.0:
                continue
            north = py > qy(px); ref = _town_top0(px, py) + 5.0                       # deck level (not the lifted seat)
            u = rr.random()
            if u < (0.42 if d < 16 else (0.3 if d < 30 else 0.16)):                        # a head
                if d < 16:
                    top, r = ref + rr.uniform(-3, 7 if north else 2), rr.uniform(12, 20)
                elif d < 30:
                    top, r = ref + rr.uniform(-2, 12 if north else 4), rr.uniform(16, 28)
                else:
                    top, r = ref + rr.uniform(-8, 16 if north else 2), rr.uniform(20, 34)
                grp = []; heads(rr, grp, px, py, top, r, soft=0.2, kids=(2, 4))
                grp = [l for l in grp if path_gap(l, 0.0, 47.4) >= 16.0 and not on_open_deck(cells, l)]
                if grp and grp[0]["r"] >= 12.0:
                    grp[0]["_keep"] = True; out += grp; n_h += 1
            elif u < 0.56 and d < 22:                                                      # a wisp
                l = L(px, py, ref + rr.uniform(-2, 6), rr.uniform(9, 16), rr.uniform(0.32, 0.46), rr.uniform(0.4, 0.55), 0.7, sx=rr.uniform(1.6, 2.6))
                if path_gap(l, 0.0, 47.4) >= 14.0 and not on_open_deck(cells, l):
                    out.append(l); n_w += 1
        y += 13.0
    # the plain: in the engine the plateau between the pier and the Quay was one smooth lit sheet. Heads over all of
    # it, their tops a few metres either side of the shelves' own, so the sheet becomes a field of billows with dark
    # creases; lower in the bay under the pier.
    CX, CY, AX, AY = 15.0, 22.0, 250.0, 165.0; n_p = 0
    hollow = lambda x_, y_: 30 * math.exp(-(((x_ + 250) / 110.0) ** 2 + ((y_ + 125) / 60.0) ** 2))
    gy = CY - AY * 1.15
    while gy <= CY + AY * 1.15:
        gx = CX - AX * 1.15
        while gx <= CX + AX * 1.15:
            px, py = gx + rr.uniform(-9, 9), gy + rr.uniform(-9, 9); gx += 27.0
            e = math.sqrt(((px - CX) / AX) ** 2 + ((py - CY) / AY) ** 2)
            if e > 1.12 or math.hypot(px - VEIL[0], py - VEIL[1]) < 150 or rr.random() < 0.18:
                continue
            d, dz = town_near(cells, px, py, 14.0)
            if d is not None:
                continue                                                                   # the dressing above has the town's own edges
            r = rr.uniform(17, 32); top = _town_top0(px, py) - hollow(px, py) + rr.uniform(1, 13) - (10.0 * max(0.0, e - 0.85) / 0.27)
            grp = []; heads(rr, grp, px, py, top, r, soft=0.22, kids=(2, 4))
            grp = [l for l in grp if path_gap(l, 0.0, 47.4) >= 14.0 and not on_open_deck(cells, l)]
            if grp and grp[0]["r"] >= 15.0:
                grp[0]["_keep"] = True; out += grp; n_p += 1
        gy += 27.0
    STATS["plain_heads"] = n_p
    return out, n_h, n_w


def bins_grid(lobes):
    return bins_report(lobes)


_J = Journey()                                            # the journey as the engine flies it (nd_world.Journey)
_PATH = [_J.at(t / 10.0) for t in range(0, 703)]          # every 0.1 s (plan coordinates)


def path_gap(l, t0=0.0, t1=70.2):
    """Metres of clear air between a lobe (engine coordinates) and the journey between t0 and t1 (negative: inside)."""
    cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; rx, rz, ry = (l["r"] * v for v in l["scale"]); best = 1e9
    for k in range(int(t0 * 10), min(len(_PATH), int(t1 * 10) + 1)):
        px, py, pz = _PATH[k]
        q = math.sqrt(((px - cx) / rx) ** 2 + ((py - cy) / rz) ** 2 + ((pz - cz) / ry) ** 2)
        best = min(best, (q - 1.0) * min(rx, ry, rz))
    return best


def heads(rr, out, x, y, top, r, soft=0.22, kids=(3, 5), dens=1.0, sy=0.72):
    """A cauliflower head with a flattened base: one lobe whose top is at `top`, smaller ones on its upper half, and
    theirs. The unit of everything that should read as cumulus at the scale of a building."""
    out.append(L(x, y, top - r * sy, r, sy, dens, soft, sx=rr.uniform(1.0, 1.25), sz=rr.uniform(0.95, 1.15)))
    for j in range(rr.randint(*kids)):
        ph = rr.uniform(0, 6.283); d = r * rr.uniform(0.35, 0.85); cr = r * rr.uniform(0.38, 0.56)
        if cr < 6.5:
            continue
        cx, cy, cz = x + d * math.cos(ph), y + d * math.sin(ph), top - r * sy * 0.55 + cr * rr.uniform(-0.1, 0.45)
        out.append(L(cx, cy, cz, cr, rr.uniform(0.8, 0.95), dens, soft))
        if cr > 13.0 and rr.random() < 0.7:
            ph2 = rr.uniform(0, 6.283); g = cr * rr.uniform(0.42, 0.55)
            out.append(L(cx + cr * 0.6 * math.cos(ph2), cy + cr * 0.6 * math.sin(ph2), cz + cr * 0.5, g, 0.88, dens, soft))


def garden_seat():
    """Iteration 8: the garden terrace seated in cloud. In the engine's 44.5 s frame the terrace was a clean disc over
    a hollow: the plateau lies under it, and nothing stood beside it (on_open_deck clears the whole square of its
    bounds). Cauliflower heads stand all round its rim except on the town's side, their tops from two metres under the
    promenade to two over it: cloud up to the balustrade and over the lower crescents, never on the paving. Small
    lobes, so they are tagged "seat" by build() and keep their body (0.3 of the erosion)."""
    rr = random.Random(86); out = []; gx, gy, gz = GARDEN; n = 0
    small = ((gx - 22.0, gy + 16.0, 14.0), (gx - 42.0, gy, 14.0))

    def clear(l):
        cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; rh = l["r"] * max(l["scale"][0], l["scale"][2]); top = cz + l["r"] * l["scale"][1]
        if top < gz - 1.6:
            return True                                                    # under the decks: anywhere
        if math.hypot(cx - gx, cy - gy) - rh < 25.8:                        # the promenade's outer metre may be lapped, the paving not
            return False
        return all(math.hypot(cx - sx, cy - sy) - rh > sr - 0.5 for (sx, sy, sr) in small)
    for k in range(28):
        a = math.radians(-150 + k * 300 / 27 + rr.uniform(-4, 4))
        r = rr.uniform(9.0, 14.5); d = 27.0 + r * rr.uniform(0.95, 1.35)
        top = gz - 0.4 + rr.uniform(-2.2, 2.2)
        grp = []; heads(rr, grp, gx + d * math.cos(a), gy + d * math.sin(a), top, r, soft=0.22, kids=(1, 3))
        grp = [l for l in grp if clear(l) and path_gap(l, 0.0, 47.4) >= 11.0]
        if grp:
            out += grp; n += 1
    for k in range(10):                                                    # mist at the rail
        a = math.radians(-140 + k * 280 / 9 + rr.uniform(-8, 8)); d = 27.0 + rr.uniform(6, 12)
        l = L(gx + d * math.cos(a), gy + d * math.sin(a), gz + rr.uniform(-1.5, 1.0), rr.uniform(8, 12), rr.uniform(0.3, 0.42), 0.4, 0.7, sx=rr.uniform(1.4, 2.0))
        if clear(l) and path_gap(l, 0.0, 47.4) >= 11.0:
            out.append(l)
    for l in out:
        l["tag"] = "seat"
    STATS["garden_seat_heads"] = n
    return out


def mooring_mound():
    """The plateau under the town, built in four sizes so it never reads as a field of equal balls:
    a BODY of a few very large lobes down to the sea; flat SHELVES (terraces) whose tops follow town_top, about 5 m
    under the decks, stepping with them; a RIM of cauliflower heads (a medium lobe with a flattened base and small
    lobes on top) that rises a little above deck level away from the streets; an APRON of big low lobes falling to
    the sea. The pier reaches out over a hollow."""
    rr = random.Random(11); out = []
    CX, CY, AX, AY = 15.0, 22.0, 250.0, 165.0
    clear = lambda x, y: math.hypot(x - HEAD[0], y - HEAD[1]) > 190 and math.hypot(x - VEIL[0], y - VEIL[1]) > 170
    hollow = lambda x, y: 30 * math.exp(-(((x + 250) / 110.0) ** 2 + ((y + 125) / 60.0) ** 2))
    for (x, y, r) in ((-150, 10, 170), (-40, 40, 200), (80, 30, 190), (170, -10, 150), (-60, -40, 150), (60, -50, 150), (0, 110, 170), (-230, -110, 120)):       # body
        out.append(L(x, y, town_top(x, y) - 22 - r * 0.5, r, 0.5, 1.0, 0.45))
    gy = CY - AY
    while gy <= CY + AY:                                                           # shelves: wide, thin, crisp-topped
        gx = CX - AX
        while gx <= CX + AX:
            x, y = gx + rr.uniform(-18, 18), gy + rr.uniform(-14, 14)
            e = math.sqrt(((x - CX) / AX) ** 2 + ((y - CY) / AY) ** 2)
            if e < 1.08:
                r = rr.uniform(88, 122); sy = rr.uniform(0.17, 0.23)
                out.append(L(x, y, town_top(x, y) - hollow(x, y) - r * sy, r, sy, 1.0, 0.2, sx=rr.uniform(1.0, 1.3), sz=rr.uniform(0.9, 1.1)))
            gx += 92.0
        gy += 82.0
    n_rim = 30
    for k in range(n_rim):                                                         # rim: cauliflower heads, flattened bases
        a = k / n_rim * 6.283 + rr.uniform(-0.08, 0.08); e = rr.uniform(1.0, 1.28)
        x, y = CX + AX * e * math.cos(a), CY + AY * e * math.sin(a)
        if not clear(x, y):
            continue
        r = rr.uniform(34, 52); t = town_top(x, y) - hollow(x, y) + rr.uniform(-6, 5) + (8 if e > 1.15 else 0)
        out.append(L(x, y, t - r * 0.62 * 0.55, r, 0.62, 1.0, 0.26, sx=rr.uniform(1.0, 1.25)))
        for j in range(rr.randint(3, 5)):
            ph = rr.uniform(0, 6.283); d = r * rr.uniform(0.3, 0.8); cr = rr.uniform(12, 20)
            out.append(L(x + d * math.cos(ph), y + d * math.sin(ph), t + cr * rr.uniform(-0.2, 0.35), cr, rr.uniform(0.78, 0.95), 1.0, 0.28))
    for k in range(16):                                                            # apron: big and low, down to the sea
        a = k / 16 * 6.283 + rr.uniform(-0.15, 0.15); e = rr.uniform(1.45, 1.8)
        x, y = CX + AX * e * math.cos(a), CY + AY * e * math.sin(a)
        if not clear(x, y):
            continue
        r = rr.uniform(85, 130); t = SEA_TOP + rr.uniform(40, 95) - hollow(x, y)
        out.append(L(x, y, t - r * 0.36, r, 0.36, 1.0, 0.32, sx=rr.uniform(1.0, 1.3)))
        if rr.random() < 0.6:
            cr = rr.uniform(26, 40)
            out.append(L(x + rr.uniform(-40, 40), y + rr.uniform(-40, 40), t + cr * 0.2, cr, 0.7, 1.0, 0.28))
    out += [L(185, -5, 108, 34, 0.7), L(204, -22, 112, 26, 0.75), L(170, 16, 106, 28, 0.7)]      # a swell that laps the garden terrace from behind
    # wisps: thin cloud standing a little above deck level round the edges of the town (never on the Quay), so the
    # outer stilts, the Low Walk and the backs of the Stacks are half lost in it
    for (x, y, z, r) in ((-96, -40, 126, 20), (-60, -30, 122, 18), (-20, -34, 118, 20), (24, -40, 116, 19), (150, -66, 118, 22), (176, -30, 120, 20), (-150, 20, 130, 22),
                         (-96, 84, 138, 22), (-20, 112, 142, 24), (40, 118, 142, 22), (96, 70, 134, 22), (132, 30, 124, 18), (-140, -60, 126, 20), (-190, -100, 124, 22),
                         (-250, -150, 120, 24), (70, -52, 114, 18)):
        out.append(L(x + rr.uniform(-6, 6), y + rr.uniform(-6, 6), z, r, rr.uniform(0.4, 0.5), 0.4, 0.6, sx=rr.uniform(1.4, 1.9)))
    return out


def mesa(seed, x, y, top, radius, base=SEA_TOP - 40):
    """The Condensery's pedestal (iteration 5): a billowing cumulus the works is sunk into, not a table. In the engine's
    iteration 5 the old cap (five flat shelves just under the deck) was one smooth white plate on a stump. Now:
      * a broad BODY of unequal lobes down to the sea, wider on the downwind side;
      * the top is a field of cauliflower HEADS of three sizes at uneven heights: low (5 to 12 m under the deck) under
        the yard so the works stands on it, level with the deck or up to 30 m above it round the rim, so the platform's
        edge and its legs are lost in billows and nothing reads as a disc;
      * BUTTRESSES that step down the flanks, each a head with children, with dark gaps between them;
      * a ragged SHELF halfway up on the downwind side, tatters streaming off it, mist at the foot."""
    rr = random.Random(seed); out = []; H = top - base
    wyaw = math.atan2(WIND[1], WIND[0]); wx, wy = WIND
    for (dx, dy, f, hf) in ((0, 0, 1.0, 0.86), (0.5, 0.2, 0.8, 0.7), (-0.5, -0.2, 0.75, 0.74), (0.15, -0.55, 0.7, 0.62), (-0.2, 0.5, 0.72, 0.68), (0.75, -0.3, 0.55, 0.5)):      # body
        r = radius * f * 0.78; hz = H * hf
        out.append(L(x + dx * radius, y + dy * radius, base + hz * 0.5, r, min(1.3, hz * 0.52 / r), 1.0, 0.22))
    out.append(L(x + wx * radius * 0.2, y + wy * radius * 0.2, base + radius * 0.22, radius * 1.3, 0.38, 1.0, 0.3))               # foot
    # the top: heads at uneven heights. Under the platform (an ellipse about 150 x 95 m along the yard's axis) they stay
    # below the deck; round it they stand up.
    cy_, sy_ = math.cos(CONDENSERY_YAW), math.sin(CONDENSERY_YAW)
    n = 0
    for k in range(260):
        a = rr.uniform(0, 6.283); d = radius * math.sqrt(rr.uniform(0.0, 1.0)) * 1.02
        px, py = x + d * math.cos(a), y + d * math.sin(a)
        lx, ly = (px - x) * cy_ + (py - y) * sy_, -(px - x) * sy_ + (py - y) * cy_          # in the works' own frame
        e = math.sqrt((lx / 150.0) ** 2 + (ly / 95.0) ** 2)                                # 1 at the platform's edge
        if any(math.hypot(px - o["c"][0], py + o["c"][2]) < 0.75 * (o["r"] + 22) for o in out[7:] if o.get("_head")):
            continue
        if any(math.hypot(lx - sx_, ly - sy0_) < 30.0 for (sx_, sy0_) in ((-58, 14), (-2, 30), (52, 2))):
            continue                                                                        # a well under each stack: the throats' light comes up between the heads (engine: an amber glow in each)
        r = rr.uniform(20, 46) if e > 0.8 else rr.uniform(26, 44)
        if e < 0.85:
            t = top - rr.uniform(0, 6)                                                      # under the yard (iteration 6: was 3 to 11 m under; the erosion takes 5 to 15 m more)
        elif e < 1.35:
            t = top + rr.uniform(-6, 26) * (0.5 + 0.5 * rr.random())                        # the rim: level with the deck or over it
        else:
            t = top - rr.uniform(8, 46) * (e - 1.0)                                         # falling away
        h0 = len(out)
        heads(rr, out, px, py, t, r, soft=0.2, kids=(3, 5) if e > 0.8 else (2, 3))
        out[h0]["_head"] = True; n += 1
        if n >= 58:
            break
    for k in range(14):                                                            # buttresses stepping down the flanks
        a = k / 14 * 6.283 + rr.uniform(-0.2, 0.2); d = radius * rr.uniform(0.82, 1.12); r = radius * rr.uniform(0.2, 0.34)
        z = base + H * rr.uniform(0.28, 0.8)
        heads(rr, out, x + d * math.cos(a), y + d * math.sin(a), z + r * 0.5, r, soft=0.2, kids=(4, 6), sy=0.9)
    shelf(rr, out, x, y, base + H * 0.55, radius * 1.7, radius * 0.85, radius * 0.07, wyaw + 0.3, dens=0.75, soft=0.35, scraps=6)
    for k in range(9):                                                             # tatters streaming downwind off the top
        t_ = rr.uniform(0.9, 1.9); off = rr.uniform(-0.5, 0.5) * radius
        out.append(L(x + wx * radius * t_ - wy * off, y + wy * radius * t_ + wx * off, top + rr.uniform(-40, 8), rr.uniform(14, 26), rr.uniform(0.35, 0.5), rr.uniform(0.4, 0.6), 0.6, sx=rr.uniform(1.6, 2.4)))
    for k in range(8):
        a = rr.uniform(0, 6.283); d = radius * rr.uniform(1.1, 1.7)
        out.append(L(x + d * math.cos(a), y + d * math.sin(a), base + rr.uniform(20, 55), radius * rr.uniform(0.3, 0.5), 0.3, 0.4, 0.7, sx=1.5))
    for o in out:
        o.pop("_head", None)
    return out


def plume():
    """Three plumes that rise from the throats, lean with the wind and merge into one trail: dense at the throat (the
    amber glow lights them from below), thinning downwind. Dense enough to read from 1.5 km."""
    out = []; cx, cy, cz = CONDENSERY; rr = random.Random(41)
    for (lx, ly, hb) in ((-58, 14, 118), (-2, 30, 146), (52, 2, 100)):
        c, s = math.cos(CONDENSERY_YAW), math.sin(CONDENSERY_YAW); x, y = cx + lx * c - ly * s, cy + lx * s + ly * c; z = cz + hb + 6
        r = 17.0
        for k in range(9):
            out.append(L(x, y, z, r, rr.uniform(0.85, 1.0), max(0.3, 0.8 - 0.06 * k), 0.45))
            if k > 0:
                out.append(L(x + rr.uniform(-0.5, 0.5) * r, y + rr.uniform(-0.5, 0.5) * r, z + r * 0.45, r * 0.55, 0.9, max(0.3, 0.7 - 0.06 * k), 0.4))
            x += WIND[0] * r * (0.25 + 0.2 * k); y += WIND[1] * r * (0.25 + 0.2 * k); z += r * max(0.15, 1.0 - 0.12 * k); r *= 1.22
    return out


# ---------------------------------------------------------------------------------------------------------------
# The Veil (iteration 5): a bank the journey passes THROUGH, with rooms in it.
# The renderer unites lobes (max) and cannot subtract, so a room is built as an absence: a chain of VOID beads along
# the journey (centre, radius), and every lobe of the bank is admitted only if it does not reach into a bead by more
# than a set share of its own radius. Shell lobes are laid round each bead, so the walls are billows seen from inside.
VEIL_T0 = 47.0
# (metres along the journey from 47.0 s, bead radius, metres to the right of the path, metres up) and the room it belongs to
VEIL_BEADS = [(20, 22, 0, 0, "gate"), (40, 22, 0, 1, "gate"), (58, 28, 0, 2, "antechamber"), (82, 36, 0, 4, "antechamber"), (108, 26, 0, 2, "antechamber"),
              (128, 15, 0, 0, "throat1"),
              (152, 30, 0, 2, "nave"), (182, 46, 4, 6, "nave"), (216, 52, 0, 8, "nave"), (250, 44, -4, 6, "nave"), (200, 34, 6, 44, "nave"), (234, 32, -6, 46, "nave"),
              (278, 22, 0, 0, "nave"), (294, 14, 0, 0, "throat2"),
              (316, 18, 0, 1, "lantern_room")]
# The PLUG: the bank is solid from the lantern room's far wall to the mouth. It is the only cloud between the rooms and
# the Condensery and the moon, which stand 10 and 17 degrees to the left of the camera's own heading there.
# Iteration 6 tried two things here in AV Gen and kept neither:
#   * two walls of discs (lobes flattened along the journey) with a small "ember room" between them: without the
#     engine's erosion it broke the 2.5 s of one colour into wall, room, wall;
#   * the same as two layers of 30 m lobes on a 34 m grid.
# The engine's ADR-2106 (erosion in metres, strength 1, wavelength 60 m) opens both: the erosion is taken off a lobe's
# normalised radius by up to 0.88 of its smallest semi-axis for a lobe under 18 m and 0.8 for one of 30 m, so a disc
# loses its width with its thickness and a 30 m lobe is 6 m in the deepest bays. The Condensery and the moon stood in
# the gaps from 54.4 s, two and a half seconds before the crash. Only big lobes survive a bay: one of 60 m keeps 23 m.
# So the plug is ONE layer of 60 m lobes on a 40 m grid across the journey, centred 388 m from the 47.0 s point:
# 328 to 448 m as authored (54.25 to 56.95 s). The engine's build.py gives lobes tagged "plug" 0.3 of the erosion
# (ADR-2107, LOBE_ERODE): with that the far face stands at about 442 m, and the lens is out on the crash at 56.88 s.
# With the whole erosion it is about 347 to 429 m (out at 56.5 s, the works coming up through the last shreds), never
# less than 365 to 411 m; with none, out at 56.95 s.
# What it shows from inside is the engine's and the glows'. A glow inside solid cloud is occluded within about 20 m
# (glowOcclusion 0.8 at this extinction), so one that stands beside the journey lights nothing the lens can see: the
# first test was 1.3 s of flat dark blue. veil_pocket5 (372 m, 55.2 s) and veil_amber (414 m, 56.15 s) therefore stand
# ON the journey: the lens flies through each, a warm pulse of about a second, the second larger: dark, ember, dark,
# ember, and the works coming up through the last of the cloud. The Condensery's own light is held at a tenth until 56.03 s by the
# engine's timeline. (The toms rest from 48.3 to 69.1 s: there is no tom hit to hang a flash on in the plug.)
VEIL_WALLS = [(388.0, 60.0, 40.0, "plug")]          # (centre along the journey, lobe radius, grid spacing, name)
VEIL_EXIT_S = 444.0
VEIL_EXIT = [(474, 30, 0, 0), (492, 44, 0, 0), (520, 60, 0, 0), (552, 76, 0, 0), (592, 92, 0, 0), (640, 112, 0, 0)]
_S47 = _J.s_at(VEIL_T0)


def _veil_axis():
    """at(metres from the journey's place at 47.0 s) -> (position, unit tangent, unit right, time), on the engine's path."""
    def at(sm):
        s_ = _S47 + sm; p = _J.at_s(s_); d = _J.fwd_s(s_)
        rn = math.hypot(d[1], d[0]) or 1.0
        return p, d, (d[1] / rn, -d[0] / rn, 0.0), _J.t_at(s_)
    return at


def veil_voids():
    at = _veil_axis(); out = []
    for (sm, rho, off, up, room) in VEIL_BEADS:
        p, d, right, t = at(sm)
        out.append({"c": (p[0] + right[0] * off, p[1] + right[1] * off, p[2] + up), "rho": rho, "room": room, "s": sm, "t": round(t, 2)})
    ex = []
    for (sm, rho, off, up) in VEIL_EXIT:
        p, d, right, t = at(sm)
        ex.append({"c": (p[0], p[1], p[2]), "rho": rho, "room": "exit", "s": sm, "t": round(t, 2)})
    return out, ex


def _admit(l, voids, bulge):
    """True when lobe l (engine coordinates) reaches into no void by more than `bulge` of its own radius."""
    cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; rx, rz, ry = (l["r"] * v for v in l["scale"])
    for v in voids:
        dx, dy, dz = v["c"][0] - cx, v["c"][1] - cy, v["c"][2] - cz; dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        if dist < 1e-6:
            return False
        # the lobe's radius toward the void
        ux, uy, uz = dx / dist, dy / dist, dz / dist; reach = 1.0 / math.sqrt((ux / rx) ** 2 + (uy / rz) ** 2 + (uz / ry) ** 2)
        if dist < v["rho"] + reach * (1.0 - bulge):
            return False
    return True


def _tangents(u):
    """Two unit vectors square to u and to each other."""
    ref = (0.0, 0.0, 1.0) if abs(u[2]) < 0.9 else (1.0, 0.0, 0.0)
    t1 = (u[1] * ref[2] - u[2] * ref[1], u[2] * ref[0] - u[0] * ref[2], u[0] * ref[1] - u[1] * ref[0]); n = math.sqrt(sum(q * q for q in t1)); t1 = tuple(q / n for q in t1)
    return t1, (u[1] * t1[2] - u[2] * t1[1], u[2] * t1[0] - u[0] * t1[2], u[0] * t1[1] - u[1] * t1[0])


GARDEN_SEAT = []            # garden_seat()'s lobes, placed the same way


def add_where_room(masses, extra, limit=62):
    """Add lobes without pushing any other out of the renderer's bins: the grid of fit_bins, and a lobe goes in only
    if every cell it touches is still under the limit. extra: {mass: [lobe, ...]}. Returns how many went in per mass."""
    items = [l for m in masses.values() for l in m]
    ext = lambda l: l["r"] * max(l["scale"][0], l["scale"][2])
    lo = (min(l["c"][0] - ext(l) for l in items), min(l["c"][2] - ext(l) for l in items))
    hi = (max(l["c"][0] + ext(l) for l in items), max(l["c"][2] + ext(l) for l in items))
    rs = sorted(ext(l) for l in items); cell = max(0.5 * rs[len(rs) // 2], max(hi[0] - lo[0], hi[1] - lo[1]) / BIN_SIDE, 1.0)
    cs_of = lambda l: [(x, z) for z in range(int((l["c"][2] - ext(l) - lo[1]) // cell), int((l["c"][2] + ext(l) - lo[1]) // cell) + 1)
                       for x in range(int((l["c"][0] - ext(l) - lo[0]) // cell), int((l["c"][0] + ext(l) - lo[0]) // cell) + 1)]
    cells = {}
    for l in items:
        for c in cs_of(l):
            cells[c] = cells.get(c, 0) + 1
    added = {}
    for k, ls in extra.items():
        for l in ls:
            cs = cs_of(l)
            if all(cells.get(c, 0) < limit for c in cs):
                for c in cs:
                    cells[c] = cells.get(c, 0) + 1
                masses[k].append(l); added[k] = added.get(k, 0) + 1
    return added


def deball(lobes, voids):
    """Iteration 8: no single round lobe on the Veil's face. With the engine's erosion (ADR-2106) a lobe of 40 to 80 m
    seen from the garden at 60 to 250 m is still one ball: the erosion takes bays of metres out of a surface whose
    outline is one arc, and the gate's glow shades it as one sphere (the round pink spheres of the 44.5 and 46.8 s
    frames). Every near-round lobe of 30 m and more whose surface is within 150 m of the garden's edge is therefore
    SPLIT into two or three unlike bodies (0.56 to 0.84 of its radius) set round its centre in the plane the lens sees
    (wider than tall), at different depths: the outline becomes two or three arcs of different sizes with saddles
    between them, each body keeping a soft edge of 0.5 or more for the erosion to bite into. None reaches into a room
    (else the lobe stays whole) and none is under 24.5 m (the Veil drops smaller lobes within 110 m of the journey).
    Tried in AV Gen and dropped (iteration 8, stills in work/art/it8/av):
      * FLATTENING the lobe on its own footprint (0.66 as high, the largest as two flattened bodies): every one read
        as a puck with a crisp, sky-lit underside at 46.8 s (under the erosion a flattened lobe loses its width with
        its thickness);
      * DRESSING the bodies with cauliflower heads of 14 to 24 m on the side the lens sees (offered after the bins,
        where a cell had room): seen against the sky they were stacks of smooth flat discs at 44.5 and 46.8 s, and
        from the Quay at 35.75 s a row of balloons on the bank's crown;
      * a soft edge of 0.48 on the bank's lobes of 18 to 45 m: no visible change (the smooth white puffs left on the
        bank's crown at 44.5 s are bodies of 30 to 45 m seen at 140 to 220 m against the sky);
      * one smaller flattened billow beside each lobe and a cap on the largest (a lone lobe of 15 to 25 m with a soft
        edge is a smooth lozenge at 100 m), and, when the bins were 64 a side, three or four lobes per lobe (it pushed
        64 small heads beside the town out of the bins)."""
    rr = random.Random(88); ref = _J.at(46.0); out = []; n = 0; n_split = 0
    for l in lobes:
        cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; r = l["r"]; sx, sv, sz = l["scale"]
        dx, dy = cx - ref[0], cy - ref[1]; dist = math.hypot(dx, dy) or 1.0
        gm = (sx * sv * sz) ** (1.0 / 3.0)
        if l.get("tag") == "plug" or min(sx, sv, sz) / max(sx, sv, sz) < 0.74 or r * gm < 30.0 or dist - r * gm > 150.0:
            out.append(l); continue
        n += 1
        vx, vy, vz = cx - ref[0], cy - ref[1], cz - ref[2]; vn = math.sqrt(vx * vx + vy * vy + vz * vz) or 1.0
        fwd = (vx / vn, vy / vn, vz / vn)
        tx, ty = -fwd[1], fwd[0]; tn = math.hypot(tx, ty) or 1.0; tx, ty = tx / tn, ty / tn          # across the view, level
        ux, uy, uz = (fwd[1] * 0 - fwd[2] * ty, fwd[2] * tx - fwd[0] * 0, fwd[0] * ty - fwd[1] * tx)   # up in the view plane
        if uz < 0:
            ux, uy, uz = -ux, -uy, -uz
        k = 3 if r * gm >= 52.0 else 2
        sizes = [0.84, 0.66] if k == 2 else [0.74, 0.62, 0.56]
        rr.shuffle(sizes)
        ph0 = rr.uniform(0, 2 * math.pi); made = []
        for i in range(k):
            ph = ph0 + i * 2 * math.pi / k + rr.uniform(-0.4, 0.4)
            off = (0.5 if k == 2 else 0.56) * r * gm * rr.uniform(0.9, 1.1)
            a_, b_ = math.cos(ph) * off, math.sin(ph) * off * 0.7                                    # wider than tall
            dep = rr.uniform(-0.2, 0.2) * r * gm
            ri = max(24.5, r * sizes[i]); ri = min(ri, 0.88 * r)
            px = cx + tx * a_ + ux * b_ + fwd[0] * dep; py = cy + ty * a_ + uy * b_ + fwd[1] * dep; pz = cz + uz * b_ + fwd[2] * dep
            m = dict(l); m["r"] = round(ri, 1); m["scale"] = [round(sx * rr.uniform(0.95, 1.12), 2), round(sv * rr.uniform(0.84, 1.0), 2), round(sz * rr.uniform(0.95, 1.12), 2)]
            m["c"] = [round(px, 1), round(pz, 1), round(-py, 1)]; m["soft"] = round(max(l["soft"], rr.uniform(0.5, 0.6)), 2)
            if _admit(m, voids, 0.32):
                made.append(m)
        if len(made) < 2:                                                 # cannot split without reaching into a room: keep it whole
            out.append(l); continue
        n_split += 1
        out += made
    STATS["veil_lobes_round"] = n; STATS["veil_lobes_split"] = n_split
    return out


def veil():
    """The bank: three turrets over a chain of rooms (gate, antechamber, nave, lantern room), the plug and the mouth. Iteration 6: what a room's wall is made of. In iteration 5 every shell lobe was 12 to 60 m with a crisp
    edge, and from inside the walls read as clustered spheres. The cause, found in AV Gen: the renderer's noise has
    features of about 55 m (shape) and 14 m (detail) and only works inside a lobe's soft edge, so a lobe whose soft band
    (radius x soft) is under about 12 m stays a smooth ellipsoid however many of them there are, and a cluster of them
    is a cluster of balls. So within 110 m of the journey the Veil is built of WALL MASSES only: 30 to 85 m, in three
    sizes, soft 0.5 to 0.6 (a band of 15 to 45 m). The noise takes bays and shreds out of them: that is the billowing.
    Nothing small stands on them, and the bank's own lobes are held to the same rule there.
    (Tried in AV Gen and dropped this iteration: clusters of flattened billows of 14 to 26 m on the masses, crisp or
    soft: lit from inside the room they read as rimmed discs, like blood cells. Wisps of 7 to 13 m at low density:
    clean translucent ellipses.)"""
    rr = random.Random(47); out = []; at = _veil_axis()
    rooms, exits = veil_voids(); voids = rooms + exits
    n_mass = 0
    for v in rooms:
        rho = v["rho"]; c0 = v["c"]
        ra = max(28.0, min(58.0, 1.25 * rho)); n = max(10, int(4.4 * ((rho + 0.8 * ra) / ra) ** 2))
        for k in range(n):
            zf = 1.0 - 2.0 * (k + 0.5) / n; ph = k * 2.39996 + rr.uniform(-0.3, 0.3); sf = math.sqrt(max(0.0, 1.0 - zf * zf))
            u = (sf * math.cos(ph), sf * math.sin(ph), zf)
            r = ra * (rr.uniform(1.15, 1.45) if k % 3 == 0 else rr.uniform(0.75, 1.05))                     # one in three is a size bigger
            c = [c0[j] + u[j] * (rho + r * 0.8) for j in range(3)]
            l = L(c[0], c[1], c[2], r, rr.uniform(0.8, 1.0), 1.0, rr.uniform(0.5, 0.6), sx=rr.uniform(0.95, 1.3), sz=rr.uniform(0.95, 1.2))
            if not _admit(l, voids, 0.32):
                continue
            l["_keep"] = True; l["tag"] = "wall"; out.append(l); n_mass += 1
            if rr.random() < 0.3:                                         # a second mass behind it: the wall's thickness
                r2 = rr.uniform(46, 68); c2 = [c0[j] + u[j] * (rho + r * 1.3 + r2 * 0.7) for j in range(3)]
                l2 = L(c2[0], c2[1], c2[2], r2, rr.uniform(0.85, 1.0), 1.0, 0.3)
                if _admit(l2, voids, 0.05):
                    out.append(l2)
    n_shell = len(out)
    STATS.update(veil_wall_masses=n_mass)
    # 2. the bank outside: three cumulus masses on the axis, the widest and tallest over the nave; whatever would
    #    reach into a room is left out, so the turrets stand on the rooms' roofs
    #    (their centres stand 35 m to the left of the path, the Condensery's side: more cloud between the rooms and the
    #    works, and sky to the right of the wall as the camera leaves the Quay)
    for (sm, top, rad, seed, turrets) in ((88, 215, 95, 321, 2), (220, 345, 170, 322, 4), (385, 300, 150, 323, 3)):
        p, d, right, t = at(sm); p = (p[0] - right[0] * 35.0, p[1] - right[1] * 35.0, p[2])
        for l in cumulus(seed, p[0], p[1], SEA_TOP - 20, top, rad, turrets=turrets, lean=0.05, shelves=((0.46, 1.0),), kids=(2, 3), detail=1, mist=False, rich=False):
            if _admit(l, voids, 0.12):
                out.append(l)
    # 3. the floor and flanks: big low lobes under and beside the rooms, so no room is open to the sea or the sky
    for sm in range(20, 440, 46):
        p, d, right, t = at(sm)
        for (side, up_, r) in ((0, -122, 100), (-1, -24, 88), (1, -24, 88), (-1, 70, 84), (1, 70, 84)):
            lat = rr.uniform(82, 100) if side > 0 else rr.uniform(105, 140)
            l = L(p[0] + right[0] * side * lat + rr.uniform(-12, 12), p[1] + right[1] * side * lat + rr.uniform(-12, 12),
                  p[2] + up_ + rr.uniform(-14, 14), r * rr.uniform(0.85, 1.15), rr.uniform(0.85, 1.05), 1.0, 0.26)
            if l["c"][1] + l["r"] * l["scale"][1] < 372 and _admit(l, voids, 0.08):
                out.append(l)
    # 4. the plug: one layer of big lobes on a grid across the journey, corners left off
    for (sc, r0, sp, name) in VEIL_WALLS:
        p, d, right, t = at(sc)
        for i in range(-2, 3):
            for j in range(-2, 3):
                if abs(i) == 2 and abs(j) == 2:
                    continue
                r = r0 * rr.uniform(0.96, 1.1); js = rr.uniform(-4.0, 4.0)
                l = L(p[0] + right[0] * i * sp + d[0] * js, p[1] + right[1] * i * sp + d[1] * js, p[2] + j * sp - 2.0, r, 1.0, 1.0, 0.3)
                if _admit(l, rooms, 0.3):
                    l["_keep"] = True; l["tag"] = "plug"; out.append(l); STATS["plug_lobes"] = STATS.get("plug_lobes", 0) + 1
    out = deball(out, voids)
    ragged([l for l in out if not l.get("_keep")])              # the bank's big lobes, not the rooms' walls
    kept = []; n_small = 0
    for l in out:                                               # within 110 m of the journey: no small lobe, no crisp one
        if not l.get("_keep") and path_gap(l, 46.5, 58.5) < 110.0:
            if l["r"] * min(l["scale"]) < 24.0:
                n_small += 1; continue
            l["soft"] = max(l["soft"], 0.5)
        kept.append(l)
    out = kept; STATS.update(veil_small_lobes_dropped_near_journey=n_small)
    # a curtain across the second throat: one thin scrap the camera passes through at about 53.6 s
    p, d, right, t = at(294)
    out.append(L(p[0], p[1], p[2] + 3, 13.0, 0.9, 0.3, 0.8))
    return out, n_shell, rooms, exits


def _ahead(at, sm):
    """The journey's point at sm metres from 47.0 s, carried on in a straight line past its end (for cloud laid ahead of
    the last frame)."""
    end = _J.total - _S47
    if sm <= end:
        return at(sm)
    p, d, right, t = at(end)
    return tuple(p[i] + d[i] * (sm - end) for i in range(3)), d, right, t


def reveal_fore():
    """Iteration 6: the foreground of the reveal. The iteration-5 reveal had nothing between the lens and the Condensery
    1.4 km away: no parallax, and the whole view arrived at once.
      * THE SILL: the bank's east shoulder under the journey, from the plug's mouth (56.88 s, the first crash) to a lip
        under the lens at 58.95 s (the second crash): 10 m under the lens at the mouth, 6 m at the lip, lower on the
        left, with a SPUR on the right that rises above the lens and ends at the lip. From the mouth the Condensery and
        the moon stand clear on the left; the right of the frame is behind the spur. At the lip both fall away: the
        ocean break opens under the lens and the far field on the right. Two crashes, two reveals.
      * SKERRIES: a few low tufts over the sea after the lip, 60 to 140 m off the journey and 45 to 100 m under it,
        passing under the frame as the lens sinks toward the break.
    Close to the lens a crisp lobe is a smooth ball (see veil()), so these are few, 22 to 46 m, with soft 0.55 to 0.6:
    the noise makes their surface. Nothing comes within 5 m of the journey; nothing on the left stands above the lens."""
    rr = random.Random(59); out = []; at = _veil_axis()
    s_lip = _J.s_at(58.95) - _S47; s0 = VEIL_EXIT_S + 8.0

    def put(sm_, lat, top, r, sy_, gap, tag="sill", soft=0.58):
        p, d, right, t = _ahead(at, sm_)
        l = L(p[0] + right[0] * lat, p[1] + right[1] * lat, p[2] + top - r * sy_, r, sy_, 1.0, soft, sx=rr.uniform(1.0, 1.25), sz=rr.uniform(0.95, 1.15))
        if path_gap(l, 56.3, 61.0) >= gap:
            l["tag"] = tag; l["_keep"] = True; out.append(l)
    sm = s0
    while sm < s_lip - 6.0:                                       # the floor
        f = (sm - s0) / (s_lip - s0); clear = 10.0 - 4.0 * f
        for lat in (-44, -14, 16):
            la = lat + rr.uniform(-6, 6)
            put(sm + rr.uniform(-4, 4), la, -clear - abs(la) * 0.08 - rr.uniform(0, 3) - (8.0 if la < -26 else 0.0), rr.uniform(24, 32), rr.uniform(0.5, 0.62), 5.0)
        sm += rr.uniform(17, 21)
    for sm_ in (466, 496):                                        # its body: the floor is the top of a bank, not a raft
        put(sm_, 4, -14, rr.uniform(46, 52), 0.85, 5.0, soft=0.45)
    sm = 468.0
    while sm < s_lip - 2.0:                                       # the spur: rising to the right, its tip about 10 m from the lens
        for (lat, rise, r) in ((36, 6, 24), (62, 20, 32), (92, 30, 40)):
            put(sm + rr.uniform(-4, 4), lat + rr.uniform(-3, 5), rise * rr.uniform(0.75, 1.15), r * rr.uniform(0.9, 1.12), rr.uniform(0.7, 0.9), 7.0)
        sm += rr.uniform(14, 18)
    n_sill = len(out)
    for (sm_, lat, drop, r) in ((600, 96, 48, 26), (640, -110, 60, 30), (700, 140, 70, 28), (760, -84, 86, 30), (850, 120, 96, 34), (930, -140, 100, 36)):
        put(sm_, lat, -drop, r, 0.62, 30.0, tag="skerry", soft=0.6)
        put(sm_ + rr.uniform(-18, 18), lat + rr.uniform(-22, 22), -drop + rr.uniform(2, 10), r * 0.6, 0.75, 30.0, tag="skerry", soft=0.7)
    STATS.update(sill_lobes=n_sill, skerry_lobes=len(out) - n_sill, sill_lip_m_from_47s=round(s_lip, 1))
    return out


def veil_glows():
    """The light in the rooms, under the names the engine's routes already use (veil_rose, veil_amber, veil_pocket0..5).
    Iteration 6: pocket 5 and veil_amber stand inside the plug on the journey itself, so its two seconds are two warm
    pulses in the dark and not one colour."""
    rooms, exits = veil_voids(); at = _veil_axis(); by = {}
    for v in rooms:
        by.setdefault(v["room"], []).append(v)
    g = []

    def add(name, c, radius, gain, key, note):
        g.append({"name": name, "position": E(c), "radius": radius, "intensity": gain, "key": key, "note": note})
    a = max(by["antechamber"], key=lambda v: v["rho"]); n = max(by["nave"], key=lambda v: v["rho"]); lr = max(by["lantern_room"], key=lambda v: v["rho"])
    add("veil_rose", (a["c"][0], a["c"][1], a["c"][2] + 8), 50.0, 1.6, "garden_a", "the antechamber: the rose light seen in the gate's mouth from the garden")
    p, d, right, t = at(414.0)
    add("veil_amber", (p[0] - right[0] * 5, p[1] - right[1] * 5, p[2] - 3), 58.0, 2.2, "cond_a",
        "inside the plug, on the journey, 414 m from the 47.0 s point (56.15 s): the second and larger warm pulse; the mouth and the first of the sill are amber at the crash (56.88 s)")
    ns = sorted(by["nave"], key=lambda v: v["s"])
    add("veil_pocket0", (a["c"][0], a["c"][1], a["c"][2] - a["rho"] + 6), 34.0, 0.7, "garden_a", "the antechamber's floor")
    add("veil_pocket1", (ns[1]["c"][0], ns[1]["c"][1], ns[1]["c"][2] - ns[1]["rho"] + 8), 46.0, 0.9, "garden_b", "the nave's floor, west")
    add("veil_pocket2", (n["c"][0], n["c"][1], n["c"][2] + 62), 44.0, 0.8, "garden_a", "the nave's clerestory")
    add("veil_pocket3", (ns[4]["c"][0], ns[4]["c"][1], ns[4]["c"][2] - ns[4]["rho"] + 8), 46.0, 0.9, "garden_b", "the nave's floor, east")
    p, d, right, t = at(316)
    add("veil_pocket4", (lr["c"][0] + right[0] * 14, lr["c"][1] + right[1] * 14, lr["c"][2] - 6), 34.0, 0.8, "garden_a", "the lantern room: the last room before the plug")
    p, d, right, t = at(372.0)
    add("veil_pocket5", (p[0], p[1], p[2] - 4), 40.0, 1.6, "cond_a", "inside the plug, on the journey, 372 m from the 47.0 s point (55.2 s): the first amber, a pulse as the lens passes it")
    return g


# ---------------------------------------------------------------------------------------------------------------
# The far field (iteration 6). Until now it was twelve cumulus of one recipe, each about as tall as it was wide, at 2 to
# 10 km: in the engine's reveal, four similar puffs on an empty horizon. It is now composed for the two views that show
# it (the approach, 3 to 25 s, looking about 30 degrees north of east from the south-west; the reveal, 57 to 70 s,
# looking about 4 degrees north of east from (650, 50)):
#   * COLOSSAL BANKS: ranges kilometres long with a ridge line that rises and falls (far_bank);
#   * LONG LOW SWELLS: bands 2 to 4 km long and 150 to 250 m thick lying on the horizon at 5 to 9 km, one behind the
#     other, so the horizon is ridgelines in haze instead of a line (far_swell);
#   * ONE TALL TOWER with an anvil, 9 km out on the right of the reveal (far_tower): the only thing in the sky as tall
#     as the moon;
#   * a few true cumulus of unlike sizes between them;
#   * the cloud island of Highwater Mooring (the settlement glimpsed on the right of the reveal), with a taller cumulus
#     behind it and a low bank across its near side that veils part of its lights.
# The moon's sector as seen from the reveal (plan headings 10 to 33 degrees) holds nothing taller than 1.5 degrees over
# the horizon: whatever stood there would hide the disc, which is at infinity.
# Lobes are sized for the distance: the march steps about 2 % of the distance, so nothing under 55 m at 2 km, 80 m at
# 4 km or 150 m at 9 km.
def _axis_scale(dx, dy, k):
    """A stretch of k along the nearer world axis of a plan direction (lobes are axis-aligned ellipsoids)."""
    a = abs(math.degrees(math.atan2(dy, dx))) % 180.0
    if a < 30.0 or a > 150.0:
        return k, 1.0
    if 60.0 < a < 120.0:
        return 1.0, k
    return 1.0 + (k - 1.0) * 0.45, 1.0 + (k - 1.0) * 0.45


def far_bank(seed, a, b, heights, width, rmin=75.0, base=SEA_TOP - 40, shelves=2):
    """A range from a to b (plan). heights: altitudes of the ridge line along it; width: across.
    A BODY of big overlapping lobes carries a continuous ridge to about two thirds of the profile, DOMES of two sizes
    stand on it to the profile's height, one or two a core, and a few heads stand on the domes. (The first version
    stacked a column of lobes at every core: from 4 km the North Wall was a row of thumbs.)"""
    rr = random.Random(seed); out = []
    dx, dy = b[0] - a[0], b[1] - a[1]; length = math.hypot(dx, dy); nx, ny = -dy / length, dx / length
    ex, ey = _axis_scale(dx, dy, 1.5)

    def H(t):
        k = t * (len(heights) - 1); i = min(int(k), len(heights) - 2)
        return heights[i] + (heights[i + 1] - heights[i]) * (k - i)
    n = max(4, int(length / (width * 0.42))); domes = []
    for i in range(n + 1):
        t = i / n; top = H(t) * rr.uniform(0.9, 1.06); hb = (top - base) * rr.uniform(0.58, 0.7)
        off = rr.uniform(-0.2, 0.2) * width; x, y = a[0] + dx * t + nx * off, a[1] + dy * t + ny * off
        r = width * rr.uniform(0.56, 0.72)
        out.append(L(x, y, base + hb * 0.5, r, min(1.25, hb * 0.56 / r), 1.0, 0.4, sx=ex, sz=ey))            # the body
        if i % 2 == 0:
            out.append(L(x + nx * rr.uniform(-0.5, 0.5) * width, y + ny * rr.uniform(-0.5, 0.5) * width, base + 40 + r * 0.3, r * 1.2, 0.3, 1.0, 0.5, sx=ex, sz=ey))      # the foot
        for j in range(rr.randint(1, 2)):                                                              # domes on the ridge
            rd = width * (rr.uniform(0.34, 0.48) if j == 0 else rr.uniform(0.2, 0.3))
            if rd < rmin:
                continue
            sy_ = rr.uniform(0.75, 0.95); tj = top * (1.0 if j == 0 else rr.uniform(0.78, 0.92))
            px, py = x + nx * rr.uniform(-0.3, 0.3) * width + dx / length * rr.uniform(-0.2, 0.2) * width, y + ny * rr.uniform(-0.3, 0.3) * width + dy / length * rr.uniform(-0.2, 0.2) * width
            zc = max(base + hb * 0.8, tj - rd * sy_)
            out.append(L(px, py, zc, rd, sy_, 1.0, 0.3)); domes.append((px, py, zc, rd))
            if rd * 0.45 >= rmin and rr.random() < 0.6:
                ph = rr.uniform(0, 6.283); cr = rd * rr.uniform(0.4, 0.5)
                out.append(L(px + rd * 0.7 * math.cos(ph), py + rd * 0.7 * math.sin(ph), zc + rd * sy_ * rr.uniform(0.3, 0.6), cr, 0.9, 1.0, 0.28))
    wyaw = math.atan2(WIND[1], WIND[0])
    for k in range(shelves):                                                                          # a shelf out of its lee side
        c = rr.choice(domes)
        shelf(rr, out, c[0], c[1], c[2], width * rr.uniform(1.3, 1.9), width * rr.uniform(0.7, 1.0), max(40.0, width * 0.09), wyaw + rr.uniform(-0.4, 0.4), dens=0.7, soft=0.5, scraps=2)
    return ragged([l for l in out if l["r"] >= rmin * 0.8])


def far_swell(seed, a, b, top, thick, width, dens=0.9):
    """A long low band from a to b: flattened lobes two abreast, a few low domes on it, thinning at both ends."""
    rr = random.Random(seed); out = []
    dx, dy = b[0] - a[0], b[1] - a[1]; length = math.hypot(dx, dy); nx, ny = -dy / length, dx / length
    ex, ey = _axis_scale(dx, dy, 1.7); n = max(4, int(length / (width * 0.55)))
    for i in range(n + 1):
        t = i / n; taper = 0.55 + 0.45 * math.sin(math.pi * min(1.0, max(0.0, t * 0.9 + 0.05)))
        for j in (-1, 1):
            off = j * width * rr.uniform(0.15, 0.4); r = width * rr.uniform(0.5, 0.72) * taper; th = thick * taper * rr.uniform(0.75, 1.15)
            out.append(L(a[0] + dx * t + nx * off, a[1] + dy * t + ny * off, top - th * 0.5 - rr.uniform(0, 0.25) * thick, r, max(0.12, th * 0.5 / r), dens * taper, 0.55, sx=ex, sz=ey))
        if rr.random() < 0.45:
            r = width * rr.uniform(0.2, 0.32)
            out.append(L(a[0] + dx * t + nx * rr.uniform(-0.3, 0.3) * width, a[1] + dy * t + ny * rr.uniform(-0.3, 0.3) * width, top - r * 0.25, r, 0.6, 1.0, 0.35))
    return out


def far_tower(seed, x, y, top, radius, base=SEA_TOP - 40):
    """The one tall tower: a column that keeps its girth, heads on its flanks, and an anvil drawn out downwind."""
    rr = random.Random(seed); out = []
    for k in range(6):                                                                                # the foot
        a = k / 6.0 * 6.283 + rr.uniform(-0.3, 0.3)
        out.append(L(x + radius * 0.8 * math.cos(a), y + radius * 0.8 * math.sin(a), base + radius * 0.3, radius * rr.uniform(0.55, 0.75), 0.5, 1.0, 0.45))
    z = base + radius * 0.6; r = radius; px, py = x, y
    while z + r * 0.6 < top * 0.86:
        out.append(L(px, py, z, r, rr.uniform(0.9, 1.05), 1.0, 0.3))
        for j in range(rr.randint(3, 4)):
            ph = rr.uniform(0, 6.283); cr = r * rr.uniform(0.36, 0.5)
            out.append(L(px + r * 0.82 * math.cos(ph), py + r * 0.82 * math.sin(ph), z + r * rr.uniform(-0.2, 0.6), cr, rr.uniform(0.8, 1.0), 1.0, 0.28))
        z += r * rr.uniform(0.62, 0.78); r *= rr.uniform(0.9, 1.0); px += WIND[0] * r * 0.1; py += WIND[1] * r * 0.1
    za = top - radius * 0.2                                                                           # the anvil
    for k in range(9):
        t = k / 8.0; d = radius * (0.2 + 3.4 * t); w = radius * (1.25 + 0.5 * math.sin(t * 3.0)) * (1.0 - 0.3 * t)
        off = rr.uniform(-0.3, 0.3) * radius
        out.append(L(px + WIND[0] * d - WIND[1] * off, py + WIND[1] * d + WIND[0] * off, za + rr.uniform(-0.05, 0.05) * radius, w, rr.uniform(0.15, 0.2), 1.0 - 0.45 * t, 0.5 + 0.2 * t, sx=1.25))
    out.append(L(px - WIND[0] * radius * 0.5, py - WIND[1] * radius * 0.5, za, radius * 1.1, 0.2, 0.9, 0.5))       # a little upwind too
    out.append(L(px, py, top - radius * 0.3, radius * 0.5, 0.75, 1.0, 0.3))                                         # the overshooting top
    return out


def far_cumulus(seed, x, y, top, radius, rmin, turrets=3):
    return [l for l in cumulus(seed, x, y, SEA_TOP - 40, top, radius, turrets=turrets, shelves=((0.5, 1.2),), mist=False, detail=1, kids=(3, 4), rich=False) if l["r"] >= rmin]


def highwater_island():
    """The cloud Highwater Mooring stands in: a low island whose heads come up to the decks, a broad low cumulus behind it
    (north-east), and a bank across its west side, the side the reveal sees, that covers part of the town."""
    rr = random.Random(71); x, y, z = HIGHWATER; out = []
    out.append(L(x, y, z - 150, 270, 0.52, 1.0, 0.45, sx=1.15))
    for k in range(11):
        a = k / 11.0 * 6.283 + rr.uniform(-0.25, 0.25); d = rr.uniform(70, 230); r = rr.uniform(55, 82)
        east = math.cos(a) > 0.2
        heads(rr, out, x + d * math.cos(a), y + d * math.sin(a), z + SEAT + (rr.uniform(8, 44) if east else rr.uniform(-6, 12)), r * 1.15, soft=0.24, kids=(1, 2))      # the west heads stand 1 to 17 m over the decks as authored: the erosion takes about 15 m off a head this size
    out += [l for l in cumulus(72, x + 270, y + 210, SEA_TOP - 40, z + 105, 250, turrets=2, shelves=((0.5, 1.0),), mist=False, detail=1, kids=(2, 3), rich=False) if l["r"] >= 50.0]       # low and broad: at z + 250 it was a thumb beside the town
    for (dx, dy, dz, r, sy_) in ((-250, 60, 8, 95, 0.34), (-215, -70, 14, 80, 0.4), (-285, -10, -8, 110, 0.32)):       # the veil across its near side
        out.append(L(x + dx, y + dy, z + dz, r, sy_, 0.8, 0.6, sz=1.5))
    for l in out[1:]:
        if abs(l["c"][1] + l["r"] * l["scale"][1] - z) < 30.0:
            l["tag"] = "seat"
    return out


def far_field():
    f = {}
    f["far_banks"] = (far_bank(81, (600, 3950), (3900, 1800), (480, 820, 1000, 700, 880, 600, 430), 520)          # the North Wall: behind the Mooring from the approach; its east shoulder stops under the moon's left limb in the reveal
                      + far_bank(82, (4250, -1500), (4750, 900), (260, 380, 300, 240, 330, 260), 420, shelves=1)   # the South Reef: low, 4.4 km out, behind the break and Highwater in the reveal
                      + far_bank(83, (-2600, 2400), (-1300, 4300), (500, 900, 1250, 800), 600, rmin=90.0))         # north-west, for the turn at the arrival
    f["far_swells"] = (far_swell(91, (6400, -2900), (7300, -100), 420, 200, 700)
                       + far_swell(92, (8900, 200), (8500, 2000), 380, 180, 650)
                       + far_swell(93, (7500, 1500), (6500, 3800), 400, 190, 700)                                # under the moon: the disc sinks into it
                       + far_swell(94, (5300, 150), (4600, 2400), 320, 150, 520)
                       + far_swell(95, (2500, 5600), (-200, 6300), 450, 200, 700)
                       + far_swell(96, (3000, -4200), (5200, -3400), 400, 190, 650)
                       + far_swell(97, (600, -5200), (2400, -4800), 380, 180, 650)
                       + far_swell(98, (-3200, -1500), (-2600, 800), 360, 170, 600))
    f["far_tower"] = far_tower(101, 9200.0, -2730.0, 2100.0, 720.0)
    f["far_cumulus"] = (far_cumulus(111, -300, 2600, 760, 520, 95.0, turrets=4) + far_cumulus(112, 3100, -1500, 520, 380, 75.0)
                        + far_cumulus(113, 2300, -1150, 300, 200, 55.0, turrets=2)
                        + far_cumulus(115, 6000, -4300, 1100, 700, 150.0, turrets=4) + far_cumulus(116, -1900, -2600, 620, 430, 90.0))
    f["highwater"] = tag_bob(highwater_island()[1:], "highwater")           # iteration 9: no body lobe; the town stands on its rock island
    isl = islands_cloud()
    f["highwater"] += tag_bob(isl.get("highwater", []), "highwater")
    f["keep"] = tag_bob(keep_colossus() + isl.get("keep", []), "keep")
    return f


STATS = {}
BIN_SIDE = 128.0          # ADR-2105: the renderer's bin grid is at most 128 cells a side (was 64); a cell still holds 64 lobes


def fit_bins(masses, limit=62):
    """The renderer keeps the 64 biggest lobes of a bin cell and drops the rest, which in a bank are the billows that
    make its surface and, in the Veil, the walls of its rooms. So the design drops lobes itself, by the same grid: in
    every cell over the limit the smallest lobe that is not marked `_keep` goes (from every cell it touches), until all
    fit. Returns how many were dropped per mass."""
    items = [(k, l) for k, m in masses.items() for l in m]
    ext = [l["r"] * max(l["scale"][0], l["scale"][2]) for k, l in items]
    lo = (min(l["c"][0] - e for (k, l), e in zip(items, ext)), min(l["c"][2] - e for (k, l), e in zip(items, ext)))
    hi = (max(l["c"][0] + e for (k, l), e in zip(items, ext)), max(l["c"][2] + e for (k, l), e in zip(items, ext)))
    rs = sorted(ext); cell = max(0.5 * rs[len(rs) // 2], max(hi[0] - lo[0], hi[1] - lo[1]) / BIN_SIDE, 1.0)
    cells = {}; where = []
    for n, ((k, l), e) in enumerate(zip(items, ext)):
        cs = [(x, z) for z in range(int((l["c"][2] - e - lo[1]) // cell), int((l["c"][2] + e - lo[1]) // cell) + 1)
              for x in range(int((l["c"][0] - e - lo[0]) // cell), int((l["c"][0] + e - lo[0]) // cell) + 1)]
        where.append(cs)
        for c in cs:
            cells.setdefault(c, set()).add(n)
    gone = set(); dropped = {}
    while True:
        worst = max(cells.items(), key=lambda kv: len(kv[1]))
        if len(worst[1]) <= limit:
            break
        cand = sorted((n for n in worst[1] if not items[n][1].get("_keep")), key=lambda n: ext[n])
        if not cand:
            break
        n = cand[0]; gone.add(n); dropped[items[n][0]] = dropped.get(items[n][0], 0) + 1
        for c in where[n]:
            cells[c].discard(n)
    for k in masses:
        masses[k] = [l for l in masses[k] if not any(l is items[n][1] for n in gone)]
    for k, l in items:
        l.pop("_keep", None)
    return dropped


def prune_hidden(lobes):
    """Drop every lobe that lies wholly inside the solid core of a bigger one of at least its density: the renderer
    unites lobes by the greatest density, so it adds nothing, and it costs a place in its bin cells (64 a cell, the
    smallest dropped first: in a bank they would push out the billows that are the surface)."""
    big = sorted(lobes, key=lambda l: -l["r"] * min(l["scale"])); keep = []
    for l in big:
        cx, cy, cz = l["c"]; ex = [l["r"] * v for v in l["scale"]]; hidden = False
        for o in keep:
            if o["density"] < l["density"] or o["r"] * min(o["scale"]) <= l["r"] * max(l["scale"]):
                continue
            ox, oy, oz = o["c"]; rx, ry, rz = (o["r"] * v for v in o["scale"]); core = 1.0 - o["soft"]
            ok = True
            for (dx, dy, dz) in ((ex[0], 0, 0), (-ex[0], 0, 0), (0, ex[1], 0), (0, -ex[1], 0), (0, 0, ex[2]), (0, 0, -ex[2])):
                if math.sqrt(((cx + dx - ox) / rx) ** 2 + ((cy + dy - oy) / ry) ** 2 + ((cz + dz - oz) / rz) ** 2) > core:
                    ok = False; break
            if ok:
                hidden = True; break
        if not hidden:
            keep.append(l)
    return [l for l in lobes if any(l is k for k in keep)]


def bins_report(lobes):
    """The renderer's binning (cloud_renderer.cpp binCloudLobes), replicated: cell size and the worst cell. A lobe's
    footprint is the square of its larger horizontal radius; the cell is the larger of half the median of those and
    span / 128 (ADR-2105; span / 64 before)."""
    if not lobes:
        return {}
    ext = [l["r"] * max(l["scale"][0], l["scale"][2]) for l in lobes]
    lo = (min(l["c"][0] - e for l, e in zip(lobes, ext)), min(l["c"][2] - e for l, e in zip(lobes, ext)))
    hi = (max(l["c"][0] + e for l, e in zip(lobes, ext)), max(l["c"][2] + e for l, e in zip(lobes, ext)))
    span = max(hi[0] - lo[0], hi[1] - lo[1]); rs = sorted(ext)
    cell = max(0.5 * rs[len(rs) // 2], span / BIN_SIDE, 1.0)
    nx, nz = max(1, math.ceil((hi[0] - lo[0]) / cell)), max(1, math.ceil((hi[1] - lo[1]) / cell)); cells = {}
    for l, e in zip(lobes, ext):
        for z in range(max(0, int((l["c"][2] - e - lo[1]) // cell)), min(nz - 1, int((l["c"][2] + e - lo[1]) // cell)) + 1):
            for x in range(max(0, int((l["c"][0] - e - lo[0]) // cell)), min(nx - 1, int((l["c"][0] + e - lo[0]) // cell)) + 1):
                cells[(x, z)] = cells.get((x, z), 0) + 1
    worst = max(cells.values()); wk = max(cells, key=cells.get)
    return {"lobes": len(lobes), "span_m": round(span), "median_extent_m": round(rs[len(rs) // 2], 1), "cell_m": round(cell, 1), "grid": [nx, nz], "worst_cell": worst,
            "worst_cell_at": [round(lo[0] + (wk[0] + 0.5) * cell), round(lo[1] + (wk[1] + 0.5) * cell)], "cells_over_64": sum(1 for v in cells.values() if v > 64)}


# ---------------------------------------------------------------------------------------------------------------
# Iteration 9 (owner msgs 73, 78, 80). What floats stands on a rock island (meshes/_thirdparty/isles, placed by the layout,
# listed in layout.json "islands"): its cloud is no longer a body under it but a WRAP round the rim (heads level with the
# island's top to a few metres under it, so the decks' edges are lost in cloud while the sculpted underside and its roots
# show), a few tatters drawn out along the wind, and sparse mist far under the keel. Every lobe of a floating formation
# carries "bob": <name> (nd_world.BOBS; ADR-2112), and its mass has the same name.
def _layout():
    return json.load(open(os.environ.get("ND_LAYOUT", os.path.join(ROOT, "art", "layout.json"))))


def rim_wrap(seed, x, y, top, width, depth, n=None, rmin=8.0, inner=0.44):
    rr = random.Random(seed); out = []; R = width / 2.0; n = n or max(9, int(width / 7.0))
    for k in range(n):
        a = k / n * 6.283 + rr.uniform(-0.2, 0.2); d = R * rr.uniform(inner + 0.04, 1.05)
        r = max(rmin, width * rr.uniform(0.07, 0.13))
        heads(rr, out, x + d * math.cos(a), y + d * math.sin(a), top + rr.uniform(-4.0, 1.5), r, soft=0.24, kids=(1, 3))
    wx, wy = WIND
    for k in range(max(4, n // 3)):                                     # tatters at the rim, drawn out downwind
        a = rr.uniform(0, 6.283); d = R * rr.uniform(0.8, 1.3)
        out.append(L(x + d * math.cos(a) + wx * R * 0.3, y + d * math.sin(a) + wy * R * 0.3, top - rr.uniform(2, 10), max(rmin, width * rr.uniform(0.08, 0.14)),
                     rr.uniform(0.32, 0.45), rr.uniform(0.4, 0.6), 0.65, sx=rr.uniform(1.6, 2.4)))
    for k in range(max(3, n // 4)):                                     # sparse mist well under the keel: the island floats in cloud, it does not stand on it
        a = rr.uniform(0, 6.283); d = R * rr.uniform(0.3, 1.2)
        out.append(L(x + d * math.cos(a), y + d * math.sin(a), top - depth * rr.uniform(1.1, 1.6), max(rmin, width * rr.uniform(0.12, 0.2)), 0.3, 0.35, 0.75, sx=1.6))
    for l in out:
        l["tag"] = "seat"
    return out


def tag_bob(lobes, name):
    for l in lobes:
        l["bob"] = name
    return lobes


def district_seat():
    """The cloud the new districts stand in: Lanternhill climbing the Head, the Lampmarket west of the Lantern, Eastreach
    north of the Veil. A body of big flattened lobes under each terrace whose top is 6 m under its decks, and heads at the
    terraces' front edges whose tops reach from 4 m under to 2 m over the deck, so each terrace steps out of cloud."""
    lay = _layout(); rr = random.Random(909); out = []
    groups = {}
    for i in lay["instances"]:
        if i["zone"] in ("lanternhill", "lanternhill_deck", "lampmarket", "lampmarket_deck", "eastreach", "eastreach_deck") and (i["asset"].startswith("decks/") or i["asset"].startswith("buildings/")):
            z = round(i["pos"][1], 1); groups.setdefault((i["zone"].split("_")[0], z), []).append((i["pos"][0], -i["pos"][2]))
    for (zone, z), pts in sorted(groups.items()):
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        x0, x1, y0, y1 = min(xs) - 8, max(xs) + 8, min(ys) - 8, max(ys) + 8
        nx = max(1, int((x1 - x0) / 46.0) + 1); ny = max(1, int((y1 - y0) / 40.0) + 1)
        for a in range(nx):
            for b in range(ny):
                px, py = x0 + (x1 - x0) * (a + 0.5) / nx, y0 + (y1 - y0) * (b + 0.5) / ny
                r = rr.uniform(34, 46); sy_ = rr.uniform(0.32, 0.42)
                out.append(L(px + rr.uniform(-6, 6), py + rr.uniform(-6, 6), z - 6.0 - r * sy_, r, sy_, 1.0, 0.3, sx=rr.uniform(1.0, 1.3)))
        front = sorted(pts, key=lambda p: p[1])[:max(3, len(pts) // 3)]          # the south (front) edge of the terrace
        for (px, py) in front[::2]:
            heads(rr, out, px + rr.uniform(-4, 4), py - rr.uniform(6, 12), z + rr.uniform(-4.0, 2.0), rr.uniform(10, 16), soft=0.22, kids=(1, 3))
    for l in out:
        l["tag"] = "seat"
    return out


def islands_cloud():
    """Rim wraps for every rock island of the layout, by its bob name."""
    lay = _layout(); out = {}
    for k, isl in enumerate(lay.get("islands", [])):
        x, top, y = isl["centre"][0], isl["centre"][1], -isl["centre"][2]
        out.setdefault(isl["bob"], []).extend(rim_wrap(700 + k, x, y, top, isl["width_m"], isl["depth_m"], rmin=8.0 if isl["width_m"] < 120 else 14.0))
    return out


def keep_colossus():
    """The Keep (far set): a cumulus massif 1.1 km high, 2.7 km out on the left of the approach, with a bay on its south-west
    face where its town stands on a rock island (layout zone keep). Big lobes: at that range nothing under 60 m reads."""
    x, y, top, rad = KEEP; sx, sy, sz = KEEP_SHELF
    lobes = [l for l in cumulus(121, x, y, SEA_TOP - 40, top, rad, turrets=4, shelves=((0.3, 1.2), (0.62, 0.8)), mist=False, detail=1, kids=(3, 4), rich=False) if l["r"] >= 60.0]
    keep = []
    for l in lobes:                                                     # the bay: nothing within 120 m of the town, between 40 m under and 140 m over it
        cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; rh = l["r"] * max(l["scale"][0], l["scale"][2])
        if math.hypot(cx - sx, cy - sy) - rh < 125.0 and cz - l["r"] * l["scale"][1] < sz + 140.0 and cz + l["r"] * l["scale"][1] > sz - 40.0:
            continue
        keep.append(l)
    return keep


HEAD_TOP = 235.0           # owner msg 96 (was 365)
VEIL_CROWN = 250.0         # owner msg 96: the Veil's outer crown is cut to this height (was up to 290); the plug keeps its body


def crown(lobes, ceiling, keep_tags=("plug",)):
    """Lower a bank's crown to `ceiling` without touching its rooms: a lobe whose top is over the ceiling is flattened so
    its top sits on it (its base and centre stay), or dropped if its centre is already within 12 m of it. Lobes tagged in
    keep_tags (the plug that hides the Condensery until the crash) are left whole."""
    out = []
    for l in lobes:
        if l.get("tag") in keep_tags:
            out.append(l); continue
        cy_, ry = l["c"][1], l["r"] * l["scale"][1]
        if cy_ + ry <= ceiling:
            out.append(l)
        elif cy_ < ceiling - 12.0:
            l["scale"][1] = round((ceiling - cy_) / l["r"], 3); out.append(l)
    STATS["veil_crown_dropped"] = len(lobes) - len(out)
    return out


def build():
    cells = town()
    old = {k: v for k, v in cells.items() if not v[2]}               # the town before iteration 9: the near-city cloud is built from it, unchanged
    dress, n_heads, n_wisps = mound_dressing(old)
    masses = {"mooring_mound": mooring_mound() + dress}
    GARDEN_SEAT[:] = garden_seat()
    STATS.update(town_heads=n_heads, town_wisps=n_wisps)
    # owner msg 96: the Head and the Veil's crown towered over the skyline (to 365 and 290 m) and overpowered the town.
    # The Head is now lower and broader (top 235 m, about the height of Lanternhill's crowns; the Mooring Mast stands clear
    # of it), its shelves lower: the skyline reads against sky, with cloud at its shoulders.
    masses["head"] = cumulus(23, HEAD[0], HEAD[1], SEA_TOP - 20, HEAD_TOP, 250, turrets=5, lean=0.2, shelves=((0.3, 1.3), (0.5, 1.0)), kids=(3, 4))
    # the Veil (iteration 5): a bank on the journey's own line with rooms in it (see veil()); nothing of it stands on
    # the garden's decks
    v_lobes, n_shell, rooms, exits = veil()
    v_lobes = crown(v_lobes, VEIL_CROWN)
    masses["veil"] = [l for l in v_lobes if not on_open_deck(old, l, under=1.0, over=160.0)]       # nothing of it hangs over the garden either: the gate opens beyond the terrace's edge
    STATS.update(veil_shell_lobes=n_shell, veil_dropped_over_decks=len(v_lobes) - len(masses["veil"]))
    masses["fore"] = cumulus(23, FORE[0], FORE[1], SEA_TOP - 20, 128, 105, turrets=2, lean=0.3, shelves=((0.6, 1.2),))
    # iteration 9: the lighthouses stand on rock islands (their cloud: islands_cloud(), below); no cloud-rock under them
    cx, cy, cz = CONDENSERY
    col = mesa(40, cx, cy, cz - 5.0 + SEAT, 205)
    # its two flank cumulus (iteration 6: the left one stood 3 degrees over the horizon in front of the moon's lower
    # half and was one of the reveal's four similar puffs: it is lower and broader now, a shoulder of the pedestal)
    col += cumulus(42, cx - 190, cy + 140, SEA_TOP - 20, 262, 175, turrets=2, shelves=((0.45, 1.1),), detail=1, kids=(3, 5), rich=False) + cumulus(43, cx + 180, cy - 80, SEA_TOP - 20, 190, 130, turrets=2, shelves=((0.55, 0.9),), detail=1, kids=(3, 5), rich=False)
    # a tongue of the pedestal under the conveyor pier (iteration 6): the pier runs 260 m out from the platform toward the
    # Mooring and the mesa ends 170 m short of its tip, so from the reveal's lower path its stilts stood in the open as
    # a fence. Heads under it, their authored tops level with its deck to 7 m under it, a little lower toward the tip.
    rr_ = random.Random(44); c_, s_ = math.cos(CONDENSERY_YAW), math.sin(CONDENSERY_YAW)
    ax, ay = cx + (-112) * c_ - (-20) * s_, cy + (-112) * s_ + (-20) * c_; ux, uy = -0.95 * c_ + 0.3 * s_, -0.95 * s_ - 0.3 * c_
    for k in range(9):
        dd = 70 + 34 * k; off = rr_.uniform(-16, 16); r = rr_.uniform(34, 46) * (1.0 - 0.03 * k)
        heads(rr_, col, ax + ux * dd - uy * off, ay + uy * dd + ux * off, cz + 4.0 + SEAT - 0.3 * (dd / 20.0) - rr_.uniform(2, 7) - 0.5 * k, r, soft=0.24, kids=(2, 4))
    for k in range(3):
        dd = 110 + 100 * k
        col.append(L(ax + ux * dd, ay + uy * dd, cz - 95 - 14 * k, 92 - 9 * k, 0.95, 1.0, 0.4))
    # iteration 9: the works stands on a rock island 330 m across; the mesa's body and buttresses under its rim go, so the
    # sculpted underside shows. What is left of the pedestal near the works (the heads round the platform's edge and the
    # pier's tongue) floats with it; the flank cumulus stay where they are.
    ride, stay = [], []
    for l in col:
        cxl, cyl, czl = l["c"][0], -l["c"][2], l["c"][1]; topl = czl + l["r"] * l["scale"][1]; dd = math.hypot(cxl - cx, cyl - cy)
        if dd < 205.0 and topl < cz - 16.0:
            continue
        (ride if dd < 400.0 and topl > cz - 40.0 else stay).append(l)
    masses["condensery_column"] = stay
    masses["condensery"] = tag_bob(ride, "condensery")
    masses["condensery_plume"] = tag_bob(plume(), "condensery")
    lx, ly, lz = LONE_LAMP
    # the Lone Lamp (iteration 6): at 1.4 km, level with the sea's crests and 2 degrees from the Outer Light, one window was
    # nothing. It now stands 110 m higher and 11 degrees to the right of the Outer Light as the approach sees it, on a
    # cloudlet of its own, 100 m deep, that touches neither the sea nor anything else, with a small cumulus beside the
    # house rising 105 m over its deck for the lamp to light (glow `lone_lamp`): what reads from the approach is a
    # small warm cloud with a house on it.
    # Under the engine's erosion (ADR-2106) the first cloudlet, a 44 m rock with small buttresses, was eaten to a few
    # puffs and the house hung in the open air. It is now a core of 56 m whose authored top is 6 m over the deck, four
    # shoulders of 34 to 42 m and a cumulus of 70 m: lobes big enough to keep a body in the erosion's bays. All of it
    # is tagged "seat".
    rl = random.Random(50)
    ll = [L(lx, ly, lz - 44.0, 56.0, 0.9, 1.0, 0.3)]
    for k in range(4):
        a_ = k * 1.5708 + rl.uniform(-0.4, 0.4); d_ = rl.uniform(28, 40); r_ = rl.uniform(34, 42)
        ll.append(L(lx + d_ * math.cos(a_), ly + d_ * math.sin(a_), lz + rl.uniform(-6, 2) - r_ * 0.8, r_, 0.8, 1.0, 0.3))
    ll += [l for l in cumulus(51, lx + 44, ly - 36, lz - 80, lz + 110, 70, turrets=2, lean=0.1, shelves=(), skirt=False, mist=False, detail=1, kids=(2, 3), rich=False) if l["r"] >= 20.0]
    for k in range(4):                                             # mist under it: it touches nothing
        a_ = rl.uniform(0, 6.283); d_ = rl.uniform(30, 70)
        ll.append(L(lx + d_ * math.cos(a_), ly + d_ * math.sin(a_), lz - rl.uniform(70, 100), rl.uniform(30, 40), 0.4, 0.5, 0.6, sx=1.4))
    # iteration 9: the house stands on a rock island 40 m across now: no core under it; the cumulus beside the house (the
    # lamp's light-catcher) stays, lifted off the sea so it floats with the rock
    ll = [l for l in ll[5:] if l["c"][1] - l["r"] * l["scale"][1] > lz - 70.0 or l["density"] < 0.6]
    for l in ll:
        l["tag"] = "seat"
    masses["lone_lamp"] = tag_bob(ll, "lone_lamp")
    masses["reveal_fore"] = reveal_fore()
    # the mid field (iteration 6): it was six cumulus of one recipe (tops 140 to 270, radii 125 to 190). Now a long low
    # reef north-east of the Mooring (behind the town from the approach), and a tower and its satellite on the right of the
    # approach. (Heads along the rim of the ocean break were tried: seen from above, moonlit against the dark of the break,
    # they were cotton balls.)
    # iteration 9: the generic mid field (seven cumulus of one recipe, 507 lobes) becomes two floating reefs that bob
    reef_a = []
    for i, (x, y, top, rad, tur) in enumerate(((700, 520, 165, 170, 2), (930, 590, 180, 160, 3))):          # msg 96: lower, broader (were 200 and 245)
        reef_a += cumulus(60 + i, x, y, SEA_TOP - 20, top, rad, turrets=tur, shelves=((0.5, 1.1),), detail=1, kids=(3, 4), rich=False)
    reef_b = cumulus(65, 1180, -520, SEA_TOP - 20, 175, 125, turrets=2, shelves=((0.5, 1.0),), detail=1, kids=(3, 4), rich=False) + \
        cumulus(66, 1345, -630, SEA_TOP - 20, 110, 75, turrets=2, shelves=(), detail=1, kids=(2, 3), rich=False)
    masses["reef"] = tag_bob([l for l in reef_a + reef_b if l["r"] >= 14.0], "reef")
    masses["districts"] = [l for l in district_seat() if path_gap(l, 0.0, 47.4) >= 14.0 and not on_open_deck(cells, l, under=1.0, over=40.0)]
    for name, ls in islands_cloud().items():
        if name in ("highwater", "keep"):
            continue                                                   # far set (far_field)
        masses[name] = tag_bob(ls, name) if name not in masses else masses[name] + tag_bob(ls, name)
    for k_ in ("head", "fore"):
        masses[k_] = [l for l in masses[k_] if not on_open_deck(cells, l)]
    n_seat = 0                                                     # the cloud that decks stand in (see SEAT)
    for l in masses["mooring_mound"]:
        x_, y_, top_ = l["c"][0], -l["c"][2], l["c"][1] + l["r"] * l["scale"][1]
        if abs(top_ - (_town_top0(x_, y_) + 5.0)) < 13.0 and town_near(old, x_, y_, 60.0)[0] is not None:
            l["tag"] = "seat"; n_seat += 1
    for l in masses["condensery_column"]:
        x_, y_, top_ = l["c"][0], -l["c"][2], l["c"][1] + l["r"] * l["scale"][1]
        if math.hypot(x_ - cx, y_ - cy) < 230.0 and -16.0 < top_ - cz < 8.0:
            l["tag"] = "seat"; n_seat += 1
    STATS.update(seat_lobes=n_seat, seat_lift_m=SEAT)
    n0 = sum(len(m) for m in masses.values())
    for k_ in masses:
        masses[k_] = prune_hidden(masses[k_])
    STATS.update(hidden_lobes_dropped=n0 - sum(len(m) for m in masses.values()))
    late = {"districts": masses.pop("districts")}                  # iteration 9: the new districts' seat competes with nothing designed before it
    STATS.update(dropped_to_fit_bins=fit_bins(masses))
    # iteration 8: the garden's seat goes in last, and only where a bin cell has room
    STATS.update(added_where_room=add_where_room(masses, {"mooring_mound": GARDEN_SEAT}), offered={"mooring_mound": len(GARDEN_SEAT)})
    masses["districts"] = []
    STATS.update(districts_added_where_room=add_where_room(masses, late), districts_offered=len(late["districts"]))
    far = far_field()
    for k_ in far:
        far[k_] = prune_hidden(far[k_])
    STATS.update(far_dropped_to_fit_bins=fit_bins(far))
    return masses, far


def _wr(name, x, y, a, b, yaw_deg, floor, above, density, below=6.0):
    return {"name": name, "centre": [x, -y], "radii": [a, b], "yaw": round(-math.radians(yaw_deg), 4), "floor": floor, "below": below, "above": above, "density": density}


WISP_REGIONS = [
    _wr("quay_upper", -55, 4, 34, 13, 8, 131.2, 1.2, 0.8), _wr("quay_middle", 12, 3, 38, 13, -5, 128.0, 1.2, 0.8), _wr("quay_lower", 80, -6, 34, 13, -12, 124.8, 1.2, 0.8),
    _wr("plaza", -102, -6, 26, 26, 8, 131.2, 1.5, 1.0), _wr("low_walk", 0, -24, 70, 9, -8, 121.0, 2.0, 1.0),
    _wr("steps", 66, -26, 22, 14, -15, 122.4, 1.8, 0.9), _wr("garden", GARDEN[0], GARDEN[1], 32, 32, 0, GARDEN[2], 1.6, 0.9),
    _wr("plateau_gaps", 10, 30, 150, 70, 0, 129.0, 2.5, 0.6, below=10.0),
    _wr("lampmarket", LAMPMARKET["centre"][0], LAMPMARKET["centre"][1], 34, 26, 0, LAMPMARKET["deck"], 1.2, 0.8),
    _wr("lanternhill", 5, 155, 110, 50, -10, 153.0, 0.8, 0.4, below=8.0), _wr("eastreach", 215, 146, 70, 36, -5, 144.0, 0.8, 0.4, below=8.0),
]


def glows():
    """Iteration 6: `stacks` and `station_pier` (the two dimmest) make room for the Lone Lamp's cloud and Highwater's, so the
    count the engine receives is unchanged: 13 here once it drops the three lighthouse stand-ins, and 8 in the Veil."""
    g = lambda name, p, radius, key, gain: {"name": name, "position": E(p), "radius": radius, "intensity": gain, "key": key}
    cx, cy, cz = CONDENSERY; lh = LIGHTHOUSES; hx, hy, hz = HIGHWATER
    return [g("quay_west", (-55, qy(-55), qz(-55) + 4.5), 46, "bounce_b", 1.0), g("quay_mid", (10, qy(10), qz(10) + 4.5), 48, "bounce_b", 1.0), g("quay_east", (80, qy(80), qz(80) + 4.5), 46, "bounce_b", 0.9),
            g("keel_west", (-50, -12, 116), 70, "bounce_a", 1.2), g("keel_east", (50, -18, 110), 75, "bounce_a", 1.2),
            g("great_lantern", (GREAT_LANTERN[0], GREAT_LANTERN[1], GREAT_LANTERN[2] + 36), 105, "landmark", 1.6),
            g("gardens", (112, -30, 127), 60, "garden_a", 0.9),
            g("lone_lamp", (LONE_LAMP[0] + 10, LONE_LAMP[1] - 8, LONE_LAMP[2] + 16), 80, "amber", 6.0),                 # one house, and its lamp on its own cloud
            g("highwater", (hx - 20, hy, hz + 10), 240, "bounce_b", 5.0),                                     # the far town's light in its cloud: what reads at 2 km when its windows are points
            g("harbour_light", (lh[0][2], lh[0][3], lh[0][4] + 8), 70, "moon_disc", 0.7), g("outer_light", (lh[1][2], lh[1][3], lh[1][4] + 8), 80, "moon_disc", 0.7),
            g("break_light", (lh[2][2], lh[2][3], lh[2][4] + 8), 80, "moon_disc", 0.7),
            g("condensery_throats", (cx, cy, cz + 150), 150, "cond_a", 2.2), g("condensery_yard", (cx, cy, cz + 14), 85, "cond_a", 0.8),       # the yard lit the old cap into a white plate at 130 m / 1.2
            g("condensery_keel", (cx, cy, cz - 40), 170, "cond_b", 1.0),
            # iteration 9: the new districts' light in their cloud, and the floating settlements' (these ride their formation)
            dict(g("lampmarket", (LAMPMARKET["centre"][0], LAMPMARKET["centre"][1], LAMPMARKET["deck"] - 6), 75, "sign_a", 1.1)),
            g("lanternhill", (10, 150, 146), 95, "bounce_b", 0.9),
            dict(g("lampwick", (ISLE_LAMPWICK[0], ISLE_LAMPWICK[1], ISLE_LAMPWICK[2] - 4), 60, "amber", 2.0), bob="isle_lampwick"),
            # the works on its rock: the throats' light caught in the rim cloud and under the island's lip (it8's amber pedestal)
            dict(g("condensery_rim", (CONDENSERY[0], CONDENSERY[1], CONDENSERY[2] - 14), 190, "cond_b", 1.6), bob="condensery"),
            dict(g("keep_town", (KEEP_SHELF[0], KEEP_SHELF[1], KEEP_SHELF[2] + 10), 220, "amber", 4.0), bob="keep"), g("ocean_break", (BREAK["centre"][0], BREAK["centre"][1], -150), 600, "ocean_b", 3.0)]      # iteration 6: 420 m / 1.8 at -210 left the break's walls dark from the reveal


if __name__ == "__main__":
    pal = json.load(open(os.path.join(HERE, "..", "art", "palette.json")))
    uv = {k: v["linear"] for k, v in pal["states"]["Ultraviolet Dream"].items()}; uv.update({k: v["linear"] for k, v in pal["fixed"].items()})
    masses, far = build()
    gl = glows()
    vg = veil_glows()
    for g_ in gl + vg:
        g_["color"] = uv[g_["key"]]
    mb = math.radians(MOON["light_from_bearing_deg"]); me = math.radians(MOON["light_elevation_deg"])
    ldir = [round(math.cos(mb) * math.cos(me), 4), round(math.sin(me), 4), round(-math.sin(mb) * math.cos(me), 4)]
    allhero = [l for m in masses.values() for l in m]; allfar = [l for m in far.values() for l in m]
    out = {
        "version": 4,
        "units": "metres, Y-up (x east, y up, z = -north): the frame of art/layout.json; the journey runs toward +x, the Mooring is at the origin",
        "masses": masses,
        "far_masses": far,
        "clouds_block": {
            "note": "a starting point for the scene's `clouds` block (ADR-2100); colours are palette keys, to be driven by the palette moods (values shown are Ultraviolet Dream)",
            "source_masses": list(masses.keys()),
            "sea": {"base": SEA_BASE, "top": SEA_TOP, "coverage": 1.0, "coverageScale": 0.0003, "density": 1.6,
                    "relief": 62.0, "reliefScale": 2400.0, "billow": 14.0, "farFade": 0.35,       # ADR-2101: swells of 2.4 km, crests about 60 m, cauliflower of 14 m
                    "breaks": [{"centre": [BREAK["centre"][0], -BREAK["centre"][1]], "radius": BREAK["radius"], "elongation": BREAK["elongation"], "yaw": -BREAK["yaw"]}]},
            "lightDirection": ldir, "lightColor_key": "moon_light", "lightIntensity": 0.16, "anisotropy": 0.5,
            "ambientTop_key": "sky_horizon", "ambientBottom_key": "haze", "hazeColor_key": "haze", "warmColor_key": "horizon_glow",
            "warmDirection": [round(math.cos(math.radians(MOON["disc_bearing_deg"])), 3), 0.0, round(-math.sin(math.radians(MOON["disc_bearing_deg"])), 3)],
            "noiseScale": 0.0034, "noiseAmount": 0.42, "detailScale": 0.02, "detailAmount": 0.45, "extinction": 0.012,
            "glowGain": 1.0, "glowOcclusion": 2.5, "drift": [round(WIND[0] * 3.5, 2), 0.0, round(-WIND[1] * 3.5, 2)],
            "glows": gl},
        # iteration 5: what these lobes are designed for. Measured in AV Gen (art test stills, work/art/it5/av): at the
        # scene's extinction of 0.02 a 100 m lobe passes half the light behind it, so every formation is fog, stilts show
        # through the plateau and the Condensery shows through the Veil. At 0.08 the same lobes have surfaces; at 0.15 the Veil's last wall hides the Condensery.
        "designed_for": {"extinction": 0.15, "noiseAmount": 0.72, "noiseScale": 0.018, "note": "tested in AV Gen with the scene's other values as they are (art test stills, work/art/it5/av); see notes/00-coordination.md, 'Art: iteration 5'"},
        "veil": {"note": "the Veil's rooms are built as absences (masses.veil holds the walls). The engine's generated passage lobes (build.py veil_passage) would fill them: drop them, and take the Veil's glows from here (same names as the routes use).",
                 "t0": VEIL_T0, "rooms": [{"room": v["room"], "t": v["t"], "centre": E(v["c"]), "radius": v["rho"]} for v in veil_voids()[0]],
                 "tags": "lobes of masses.veil carry \"tag\": \"wall\" (a room's shell) or \"plug\" (the solid before the crash); if lobes may be denser than 1 (ADR-2105), the plug is where to use it",
                 "wells": "condensery_column leaves a well 60 m across under each stack (plan offsets in the works' frame (-58, 14), (-2, 30), (52, 2), turned by the Condensery's yaw): a glow in each lights the heads round it from below",
                 "plug": {"from_t": 54.9, "to_t": 56.88, "walls": [{"name": n_, "centre_m_from_47s": c_, "lobe_radius_m": r_, "grid_m": g_} for (c_, r_, g_, n_) in VEIL_WALLS],
                          "note": "iteration 6: one layer of 60 m lobes (tag plug), built to stay shut under ADR-2106's erosion at 1.0 / 60 m. Authored 328 to 448 m from the 47.0 s point (54.25 to 56.95 s); out at about 56.85 s with 0.3 of the erosion on tag plug (the engine's LOBE_ERODE), 56.5 s with all of it. Lobes under about 50 m open in the erosion's bays: do not thin it, and keep the Condensery's light held at a tenth until 56.03 s. veil_pocket5 and veil_amber stand inside it."},
                 "sill": "masses.reveal_fore: a floor under the journey from the mouth (56.88 s) to a lip at 58.95 s and a spur on its right; at the lip the ocean break opens below and the far field on the right. Keep the air clear of anything else from 56.88 s.",
                 "clear_after_t": 56.88, "glows": vg},
        "sea_design": {"top_mean": SEA_TOP, "top_range": [-85, 60], "base": SEA_BASE,
                       "break": {"centre": [BREAK["centre"][0], -BREAK["centre"][1]], "radius": BREAK["radius"], "elongation": BREAK["elongation"], "yaw_rad": -BREAK["yaw"],
                                 "ocean_plane": {"y": BREAK["ocean_y"], "semi_axes": BREAK["ocean_semi_axes"], "yaw_rad": -BREAK["yaw"],
                                                 "note": "iteration 6: where the cosmic ocean's surface has to be for the reveal to see it (the lens looks 9 to 20 degrees down at the break). At y = -900 with a radius of 900 the reveal sees only the break's dark far wall. Tested in AV Gen at -130 and -110 (art artifacts, iteration 6). An ellipse a little larger than the break, not a wider disc: at a radius of 1,400 m its rim showed through the troughs of the sea 800 m away."},
                                 "note": "placed by design beside the Condensery, under the reveal; ADR-2101 sea.breaks takes it as it is (clouds_block.sea.breaks)"}},
        "wind": {"toward": [WIND[0], 0.0, -WIND[1]], "note": "leans, shelves, the plume and `drift` share it"},
        "far_design": {"note": "iteration 6: far_masses is a composed cloudscape in five named groups (all of them belong in clouds.farLobes): far_banks (ranges kilometres long), far_swells (low bands on the horizon at 5 to 9 km), far_tower (one tower with an anvil 9 km out, on the right of the reveal), far_cumulus, highwater (the cloud island of Highwater Mooring).",
                       "moon_sector": "from the reveal the plan headings 10 to 33 degrees hold nothing taller than 1.5 degrees over the horizon: cloud there would hide the moon",
                       "needs": "the march must still be sampling at 9 km with hazeDistance near 9000: if the tower and the swells do not show, raise `steps` or `maxDistance` before changing the lobes",
                       "highwater": {"centre": E(HIGHWATER), "note": "a third settlement, never reached (layout zone `highwater`); glow `highwater` is its light in its cloud"}},
        "moon": MOON,
        # iteration 9 (owner msg 73): the formations that float, in the shape of the engine's ADR-2112 clouds.bobs. A lobe,
        # glow or mass with "bob": <name> moves by that formation's offset; so do the layout's instances with the same name.
        "bobs": [dict(name=k, liftMax=0.0, set="far" if k in far else "hero", **v) for k, v in BOBS.items()],
        # iteration 9 (owner msg 73): where the wispy low fog lies (the engine's ADR-2113 wisp layer, masked to these
        # soft-edged ellipses). Engine plan coordinates [x, z]; floor = the deck height (engine y); inside a region the
        # density peaks 1 m under the floor and is 0 at floor + above. See notes/00-coordination.md, "Art: iteration 9".
        "wisp_regions": WISP_REGIONS,
        "bins": {"hero_masses": bins_report(allhero), "far_masses": bins_report(allfar),
                 "note": "replica of binCloudLobes, one grid per set (ADR-2104: clouds.lobes and clouds.farLobes): a cell holds at most 64 lobes and keeps the biggest."},
        "counts": {k: len(v) for k, v in masses.items()}, "far_counts": {k: len(v) for k, v in far.items()},
        "stats": STATS,
    }
    dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "art", "cloud_design.json")
    json.dump(out, open(dst, "w"), indent=None, separators=(",", ":"))
    print("cloud design:", out["counts"], "total", len(allhero), "far", out["far_counts"], len(allfar), "all", len(allhero) + len(allfar))
    print("bins hero:", out["bins"]["hero_masses"]); print("bins far:", out["bins"]["far_masses"]); print("stats:", STATS)
