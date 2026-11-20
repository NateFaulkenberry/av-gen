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
The plateau under the Mooring follows nd_world.town_top: about 9 m under every deck, so stilts vanish into it.

Output: lobes in the ADR-1289 shape {c, r, scale, density, soft}, ENGINE coordinates, plus a ready `clouds` block for
the engine (sea, glows with palette keys, light direction, noise) and a replica of the renderer's lobe binning so the
file can be checked against its limits (64 lobes per cell, cells of half the median radius or span / 64).
"""
import json, math, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nd_world import *  # noqa

WIND = (0.86, -0.5)            # plan direction the wind blows toward (east-south-east): leans, shelves and the plume follow it
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


def town():
    """The Mooring's decks and buildings as plan cells of 4 m, from art/layout.json and the kit manifest:
    {(i, j): (deck height, True under a building)}. The plateau is built under them, the heads and wisps beside them."""
    lay = json.load(open(os.path.join(ROOT, "art", "layout.json")))
    man = {a_["asset"]: a_ for a_ in json.load(open(os.path.join(ROOT, "meshes", "kit_manifest.json")))["assets"]}
    cells = {}
    for i in lay["instances"]:
        fam = i["asset"].split("/")[0]
        if fam not in ("decks", "buildings") or i["zone"] in ("condensery", "lone_lamp", "lighthouse", "gangway") or "rail" in i["asset"] or "stair" in i["asset"]:
            continue
        b = man[i["asset"]]["bounds_m"]; sx, sy, sz = i["scale"]; yaw = math.radians(i["rot_y_deg"])
        px, py, pz = i["pos"][0], -i["pos"][2], i["pos"][1]
        x0, x1, z0, z1 = b[0] * sx, b[3] * sx, b[2] * sz, b[5] * sz
        nx, nz = max(2, int((x1 - x0) / 2.0) + 1), max(2, int((z1 - z0) / 2.0) + 1)
        for a_ in range(nx + 1):
            for c_ in range(nz + 1):
                lx, ly = x0 + (x1 - x0) * a_ / nx, -(z0 + (z1 - z0) * c_ / nz)          # GLB (x, z) -> building-local plan (x, -z)
                wx, wy = px + lx * math.cos(yaw) - ly * math.sin(yaw), py + lx * math.sin(yaw) + ly * math.cos(yaw)
                k = (int(math.floor(wx / CELL)), int(math.floor(wy / CELL)))
                z0_, b0_ = cells.get(k, (-1e9, False))
                cells[k] = (max(z0_, pz), b0_ or fam == "buildings")
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
    street, a terrace or a garden."""
    cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; rx, rz, ry = (l["r"] * v for v in l["scale"])
    for i in range(int(math.floor((cx - rx) / CELL)), int(math.floor((cx + rx) / CELL)) + 1):
        for j in range(int(math.floor((cy - rz) / CELL)), int(math.floor((cy + rz) / CELL)) + 1):
            c = cells.get((i, j))
            if c is None or c[1]:
                continue
            q = 1.0 - (((i + 0.5) * CELL - cx) / rx) ** 2 - (((j + 0.5) * CELL - cy) / rz) ** 2
            if q <= 0.0:
                continue
            h = ry * math.sqrt(q)
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
            north = py > qy(px); ref = town_top(px, py) + 5.0
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
            r = rr.uniform(17, 32); top = town_top(px, py) - hollow(px, py) + rr.uniform(1, 13) - (10.0 * max(0.0, e - 0.85) / 0.27)
            grp = []; heads(rr, grp, px, py, top, r, soft=0.22, kids=(2, 4))
            grp = [l for l in grp if path_gap(l, 0.0, 47.4) >= 14.0 and not on_open_deck(cells, l)]
            if grp and grp[0]["r"] >= 15.0:
                grp[0]["_keep"] = True; out += grp; n_p += 1
        gy += 27.0
    STATS["plain_heads"] = n_p
    return out, n_h, n_w


def bins_grid(lobes):
    return bins_report(lobes)


_PATH = [cam_at(t / 5.0) for t in range(0, 352)]          # the journey every 0.2 s (plan coordinates)


def path_gap(l, t0=0.0, t1=70.2):
    """Metres of clear air between a lobe (engine coordinates) and the journey between t0 and t1 (negative: inside)."""
    cx, cy, cz = l["c"][0], -l["c"][2], l["c"][1]; rx, rz, ry = (l["r"] * v for v in l["scale"]); best = 1e9
    for k in range(int(t0 * 5), min(len(_PATH), int(t1 * 5) + 1)):
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
        r = rr.uniform(20, 46) if e > 0.8 else rr.uniform(26, 44)
        if e < 0.85:
            t = top - rr.uniform(3, 11)                                                     # under the yard
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
              (310, 20, 0, 1, "lantern_room"), (328, 30, 0, 3, "lantern_room")]
# The PLUG: from the lantern room's far wall (about 350 m) to 448 m the bank is solid at full density. It is the only
# cloud between the rooms and the Condensery, which stands 3 degrees off the camera's own heading, so nothing beside
# the path can hide it: about 100 m at an extinction of 0.15 is an optical depth near 10. The camera is blind from
# 54.9 s to the crash at 56.85 s and comes out with it. After it the air is kept clear for the reveal.
VEIL_PLUG = (376, 388, 400, 412)
VEIL_EXIT = [(474, 30, 0, 0), (492, 44, 0, 0), (520, 60, 0, 0), (552, 76, 0, 0), (592, 92, 0, 0), (640, 112, 0, 0)]


def _veil_axis():
    ts = [VEIL_T0 + k * 0.05 for k in range(0, 300)]; P = [cam_at(t) for t in ts]; cum = [0.0]
    for k in range(1, len(P)):
        cum.append(cum[-1] + math.dist(P[k - 1], P[k]))

    def at(sm):
        k = max(1, min(len(cum) - 1, next((i for i in range(1, len(cum)) if cum[i] >= sm), len(cum) - 1)))
        u = (sm - cum[k - 1]) / max(cum[k] - cum[k - 1], 1e-6); u = max(-2.0, min(1.0, u))
        p = [P[k - 1][j] + (P[k][j] - P[k - 1][j]) * u for j in range(3)]
        d = [P[k][j] - P[k - 1][j] for j in range(3)]; n = math.sqrt(sum(v * v for v in d)); d = [v / n for v in d]
        right = (d[1], -d[0], 0.0); rn = math.hypot(right[0], right[1]); right = (right[0] / rn, right[1] / rn, 0.0)
        return p, d, right, ts[k - 1] + (ts[k] - ts[k - 1]) * u
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


def veil():
    """The bank: three turrets over a chain of rooms (gate, antechamber, nave, lantern room), a plug, and tatters."""
    rr = random.Random(47); out = []; at = _veil_axis()
    rooms, exits = veil_voids(); voids = rooms + exits
    # 1. shells: billows round every bead, bulging into it by 0.28 of their radius
    for v in rooms:
        rb = max(18.0, min(40.0, 0.78 * v["rho"])); n = max(12, int(5.5 * ((v["rho"] + 0.6 * rb) / rb) ** 2))
        for k in range(n):
            zf = 1.0 - 2.0 * (k + 0.5) / n; ph = k * 2.39996 + rr.uniform(-0.25, 0.25); sf = math.sqrt(max(0.0, 1.0 - zf * zf))
            u = (sf * math.cos(ph), sf * math.sin(ph), zf); r = rb * (rr.uniform(1.05, 1.5) if k % 3 else rr.uniform(0.6, 0.85))
            c = [v["c"][j] + u[j] * (v["rho"] + r * 0.72) for j in range(3)]
            l = L(c[0], c[1], c[2], r, rr.uniform(0.8, 1.0), 1.0, 0.34, sx=rr.uniform(0.9, 1.2), sz=rr.uniform(0.9, 1.2))
            if _admit(l, voids, 0.32):
                out.append(l)
                l["_keep"] = True
                for j_ in range(rr.randint(0, 2)):                            # small billows on its face, toward the room
                    a1, a2 = rr.uniform(-0.9, 0.9), rr.uniform(-0.9, 0.9); cr = r * rr.uniform(0.32, 0.46)
                    w = (-u[0] + a1 * u[1] + a2 * u[2] * 0.3, -u[1] - a1 * u[0], -u[2] + a2 * (1.0 - abs(u[2])))
                    wn = math.sqrt(sum(q * q for q in w)) or 1.0
                    cc = [c[j] + w[j] / wn * (r * 0.82) for j in range(3)]
                    lc = L(cc[0], cc[1], cc[2], cr, rr.uniform(0.8, 1.0), 1.0, 0.3)
                    if cr >= 7.0 and _admit(lc, voids, 0.5):
                        out.append(lc)
                if rr.random() < 0.34:                                      # a second, bigger billow behind it: the wall's thickness
                    r2 = rr.uniform(42, 64); c2 = [v["c"][j] + u[j] * (v["rho"] + r * 1.3 + r2 * 0.7) for j in range(3)]
                    l2 = L(c2[0], c2[1], c2[2], r2, rr.uniform(0.85, 1.0), 1.0, 0.26)
                    if _admit(l2, voids, 0.05):
                        out.append(l2)
    n_shell = len(out)
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
    # 4. the plug: solid, full density, on the axis and round it
    for sm in VEIL_PLUG:
        p, d, right, t = at(sm)
        l = L(p[0], p[1], p[2] - 3.0, rr.uniform(38, 42), 1.0, 1.0, 0.22); l["_keep"] = True; out.append(l)
        for k in range(5):
            a = k / 5.0 * 6.283 + rr.uniform(-0.4, 0.4); off = rr.uniform(34, 52); r = rr.uniform(34, 48)
            l = L(p[0] + right[0] * off * math.cos(a), p[1] + right[1] * off * math.cos(a), p[2] + off * math.sin(a) - 3.0, r, rr.uniform(0.9, 1.0), 1.0, 0.24)
            if _admit(l, rooms, 0.25):
                out.append(l)
    # 5. tatters in the clear air after it: a few thin scraps beside the path until the reveal at 58.95 s
    for sm in (462, 472, 484, 498, 514, 532):
        p, d, right, t = at(sm)
        for k in range(2):
            side = rr.choice((-1, 1)); off = rr.uniform(16, 46) * (1 + (sm - 450) / 90.0)
            out.append(L(p[0] + right[0] * side * off, p[1] + right[1] * side * off, p[2] + rr.uniform(-26, 22), rr.uniform(9, 17), rr.uniform(0.4, 0.6), rr.uniform(0.35, 0.5), 0.65, sx=rr.uniform(1.4, 2.2)))
    ragged([l for l in out if not l.get("_keep")])              # the bank's big lobes, not the rooms' walls
    # a curtain across the second throat: one thin scrap the camera passes through at about 53.6 s
    p, d, right, t = at(294)
    out.append(L(p[0], p[1], p[2] + 3, 13.0, 0.9, 0.3, 0.8))
    return out, n_shell, rooms, exits


def veil_glows():
    """The light in the rooms, under the names the engine's routes already use (veil_rose, veil_amber, veil_pocket0..5)."""
    rooms, exits = veil_voids(); at = _veil_axis(); by = {}
    for v in rooms:
        by.setdefault(v["room"], []).append(v)
    g = []

    def add(name, c, radius, gain, key, note):
        g.append({"name": name, "position": E(c), "radius": radius, "intensity": gain, "key": key, "note": note})
    a = max(by["antechamber"], key=lambda v: v["rho"]); n = max(by["nave"], key=lambda v: v["rho"]); lr = max(by["lantern_room"], key=lambda v: v["rho"])
    add("veil_rose", (a["c"][0], a["c"][1], a["c"][2] + 8), 50.0, 1.6, "garden_a", "the antechamber: the rose light seen in the gate's mouth from the garden")
    add("veil_amber", at(360)[0], 62.0, 3.0, "cond_a", "in the plug's near face: the first amber, on the lantern room's far wall at 54.4 s, then all round the camera")
    ns = sorted(by["nave"], key=lambda v: v["s"])
    add("veil_pocket0", (a["c"][0], a["c"][1], a["c"][2] - a["rho"] + 6), 34.0, 0.7, "garden_a", "the antechamber's floor")
    add("veil_pocket1", (ns[1]["c"][0], ns[1]["c"][1], ns[1]["c"][2] - ns[1]["rho"] + 8), 46.0, 0.9, "garden_b", "the nave's floor, west")
    add("veil_pocket2", (n["c"][0], n["c"][1], n["c"][2] + 62), 44.0, 0.8, "garden_a", "the nave's clerestory")
    add("veil_pocket3", (ns[4]["c"][0], ns[4]["c"][1], ns[4]["c"][2] - ns[4]["rho"] + 8), 46.0, 0.9, "garden_b", "the nave's floor, east")
    p, d, right, t = at(328)
    add("veil_pocket4", (lr["c"][0] + right[0] * 20, lr["c"][1] + right[1] * 20, lr["c"][2] - 8), 30.0, 0.7, "garden_a", "the lantern room, right")
    add("veil_pocket5", (lr["c"][0] - right[0] * 20, lr["c"][1] - right[1] * 20, lr["c"][2] + 12), 30.0, 0.7, "garden_b", "the lantern room, left")
    return g


STATS = {}


def fit_bins(masses, limit=62):
    """The renderer keeps the 64 biggest lobes of a bin cell and drops the rest, which in a bank are the billows that
    make its surface and, in the Veil, the walls of its rooms. So the design drops lobes itself, by the same grid: in
    every cell over the limit the smallest lobe that is not marked `_keep` goes (from every cell it touches), until all
    fit. Returns how many were dropped per mass."""
    items = [(k, l) for k, m in masses.items() for l in m]
    ext = [l["r"] * max(l["scale"][0], l["scale"][2]) for k, l in items]
    lo = (min(l["c"][0] - e for (k, l), e in zip(items, ext)), min(l["c"][2] - e for (k, l), e in zip(items, ext)))
    hi = (max(l["c"][0] + e for (k, l), e in zip(items, ext)), max(l["c"][2] + e for (k, l), e in zip(items, ext)))
    rs = sorted(ext); cell = max(0.5 * rs[len(rs) // 2], max(hi[0] - lo[0], hi[1] - lo[1]) / 64.0, 1.0)
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
    span / 64."""
    if not lobes:
        return {}
    ext = [l["r"] * max(l["scale"][0], l["scale"][2]) for l in lobes]
    lo = (min(l["c"][0] - e for l, e in zip(lobes, ext)), min(l["c"][2] - e for l, e in zip(lobes, ext)))
    hi = (max(l["c"][0] + e for l, e in zip(lobes, ext)), max(l["c"][2] + e for l, e in zip(lobes, ext)))
    span = max(hi[0] - lo[0], hi[1] - lo[1]); rs = sorted(ext)
    cell = max(0.5 * rs[len(rs) // 2], span / 64.0, 1.0)
    nx, nz = max(1, math.ceil((hi[0] - lo[0]) / cell)), max(1, math.ceil((hi[1] - lo[1]) / cell)); cells = {}
    for l, e in zip(lobes, ext):
        for z in range(max(0, int((l["c"][2] - e - lo[1]) // cell)), min(nz - 1, int((l["c"][2] + e - lo[1]) // cell)) + 1):
            for x in range(max(0, int((l["c"][0] - e - lo[0]) // cell)), min(nx - 1, int((l["c"][0] + e - lo[0]) // cell)) + 1):
                cells[(x, z)] = cells.get((x, z), 0) + 1
    worst = max(cells.values()); wk = max(cells, key=cells.get)
    return {"lobes": len(lobes), "span_m": round(span), "median_extent_m": round(rs[len(rs) // 2], 1), "cell_m": round(cell, 1), "grid": [nx, nz], "worst_cell": worst,
            "worst_cell_at": [round(lo[0] + (wk[0] + 0.5) * cell), round(lo[1] + (wk[1] + 0.5) * cell)], "cells_over_64": sum(1 for v in cells.values() if v > 64)}


def build():
    cells = town()
    dress, n_heads, n_wisps = mound_dressing(cells)
    masses = {"mooring_mound": mooring_mound() + dress}
    STATS.update(town_heads=n_heads, town_wisps=n_wisps)
    masses["head"] = cumulus(23, HEAD[0], HEAD[1], SEA_TOP - 20, 365, 215, turrets=4, lean=0.2, shelves=((0.36, 1.1), (0.62, 0.8)), kids=(3, 4))
    # the Veil (iteration 5): a bank on the journey's own line with rooms in it (see veil()); nothing of it stands on
    # the garden's decks
    v_lobes, n_shell, rooms, exits = veil()
    masses["veil"] = [l for l in v_lobes if not on_open_deck(cells, l, under=1.0, over=160.0)]       # nothing of it hangs over the garden either: the gate opens beyond the terrace's edge
    STATS.update(veil_shell_lobes=n_shell, veil_dropped_over_decks=len(v_lobes) - len(masses["veil"]))
    masses["fore"] = cumulus(23, FORE[0], FORE[1], SEA_TOP - 20, 128, 105, turrets=2, lean=0.3, shelves=((0.6, 1.2),))
    for i, (name, asset, x, y, z, yawd, rpm, ph, bl) in enumerate(LIGHTHOUSES):
        masses["rock_" + name] = rock(31 + i, x, y, z - 8.0, (54, 60, 50)[i])
    cx, cy, cz = CONDENSERY
    col = mesa(40, cx, cy, cz - 5.0, 205)
    col += cumulus(42, cx - 190, cy + 140, SEA_TOP - 20, 300, 150, turrets=3, shelves=((0.45, 1.1),), detail=1, kids=(3, 5), rich=False) + cumulus(43, cx + 180, cy - 80, SEA_TOP - 20, 190, 130, turrets=2, shelves=((0.55, 0.9),), detail=1, kids=(3, 5), rich=False)
    masses["condensery_column"] = col
    masses["condensery_plume"] = plume()
    lx, ly, lz = LONE_LAMP
    masses["lone_lamp"] = rock(50, lx, ly, lz - 9.0, 48)
    mid = []
    for i, (x, y, top, rad) in enumerate(((760, 430, 270, 190), (1250, 620, 220, 170), (1180, -520, 190, 160), (520, -620, 160, 130), (-560, 420, 210, 170), (1560, -260, 140, 125))):
        mid += cumulus(60 + i, x, y, SEA_TOP - 20, top, rad, turrets=3, shelves=((0.45, 1.2), (0.7, 0.6)), detail=1, kids=(3, 5), rich=False)
    masses["mid_field"] = mid
    for k_ in ("head", "fore"):
        masses[k_] = [l for l in masses[k_] if not on_open_deck(cells, l)]
    n0 = sum(len(m) for m in masses.values())
    for k_ in masses:
        masses[k_] = prune_hidden(masses[k_])
    STATS.update(hidden_lobes_dropped=n0 - sum(len(m) for m in masses.values()))
    STATS.update(dropped_to_fit_bins=fit_bins(masses))
    far = []
    for i, (fx, fy, fh, fr) in enumerate(((-900, 1500, 500, 420), (1200, 2600, 700, 520), (3400, 2200, 900, 600), (4200, -300, 600, 520), (2600, -1500, 450, 420), (5200, 3800, 1300, 800),
                                          (7600, 900, 1100, 900), (300, 4600, 1000, 800), (8200, 4800, 1600, 1000), (-2800, 3200, 800, 700), (6200, -2600, 900, 800), (9800, 2000, 1400, 1100))):
        rr = random.Random(80 + i); f = []
        far += [l for l in cumulus(80 + i, fx, fy, SEA_TOP - 40, fh * 0.85, fr * 0.75, turrets=4, shelves=((0.5, 1.2),), mist=False, detail=1, kids=(3, 4), rich=False) if l["r"] > fr * 0.11]
    return masses, far


def glows():
    g = lambda name, p, radius, key, gain: {"name": name, "position": E(p), "radius": radius, "intensity": gain, "key": key}
    cx, cy, cz = CONDENSERY; lh = LIGHTHOUSES
    return [g("quay_west", (-55, qy(-55), qz(-55) + 4.5), 46, "bounce_b", 1.0), g("quay_mid", (10, qy(10), qz(10) + 4.5), 48, "bounce_b", 1.0), g("quay_east", (80, qy(80), qz(80) + 4.5), 46, "bounce_b", 0.9),
            g("keel_west", (-50, -12, 116), 70, "bounce_a", 1.2), g("keel_east", (50, -18, 110), 75, "bounce_a", 1.2),
            g("great_lantern", (GREAT_LANTERN[0], GREAT_LANTERN[1], GREAT_LANTERN[2] + 36), 105, "landmark", 1.6),
            g("gardens", (112, -30, 127), 60, "garden_a", 0.9), g("stacks", (10, 60, 150), 110, "bounce_b", 0.4), g("station_pier", (-150, -45, 134), 65, "win_a", 0.6),
            g("harbour_light", (lh[0][2], lh[0][3], lh[0][4] + 8), 70, "moon_disc", 0.7), g("outer_light", (lh[1][2], lh[1][3], lh[1][4] + 8), 80, "moon_disc", 0.7),
            g("break_light", (lh[2][2], lh[2][3], lh[2][4] + 8), 80, "moon_disc", 0.7),
            g("condensery_throats", (cx, cy, cz + 150), 150, "cond_a", 2.2), g("condensery_yard", (cx, cy, cz + 14), 85, "cond_a", 0.8),       # the yard lit the old cap into a white plate at 130 m / 1.2
            g("condensery_keel", (cx, cy, cz - 40), 170, "cond_b", 1.0), g("ocean_break", (BREAK["centre"][0], BREAK["centre"][1], -210), 420, "ocean_b", 1.8)]


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
    allhero = [l for m in masses.values() for l in m]
    out = {
        "version": 3,
        "units": "metres, Y-up (x east, y up, z = -north): the frame of art/layout.json; the journey runs toward +x, the Mooring is at the origin",
        "masses": masses,
        "far_masses": {"far_towers": far},
        "clouds_block": {
            "note": "a starting point for the scene's `clouds` block (ADR-2100); colours are palette keys, to be driven by the palette moods (values shown are Ultraviolet Dream)",
            "source_masses": list(masses.keys()),
            "sea": {"base": SEA_BASE, "top": SEA_TOP, "coverage": 1.0, "coverageScale": 0.0003, "density": 1.6,
                    "relief": 62.0, "reliefScale": 2400.0, "billow": 14.0, "farFade": 0.35,       # ADR-2101: swells of 2.4 km, crests about 60 m, cauliflower of 14 m
                    "breaks": [{"centre": [BREAK["centre"][0], -BREAK["centre"][1]], "radius": BREAK["radius"], "elongation": BREAK["elongation"], "yaw": -BREAK["yaw"]}]},
            "lightDirection": ldir, "lightColor_key": "moon_light", "lightIntensity": 0.16, "anisotropy": 0.5,
            "ambientTop_key": "sky_horizon", "ambientBottom_key": "haze", "hazeColor_key": "haze", "warmColor_key": "horizon_glow",
            "warmDirection": [round(math.cos(math.radians(19)), 3), 0.0, round(-math.sin(math.radians(19)), 3)],
            "noiseScale": 0.0034, "noiseAmount": 0.42, "detailScale": 0.02, "detailAmount": 0.45, "extinction": 0.012,
            "glowGain": 1.0, "glowOcclusion": 2.5, "drift": [round(WIND[0] * 3.5, 2), 0.0, round(-WIND[1] * 3.5, 2)],
            "glows": gl},
        # iteration 5: what these lobes are designed for. Measured in AV Gen (art test stills, work/art/it5/av): at the
        # scene's extinction of 0.02 a 100 m lobe passes half the light behind it, so every formation is fog, stilts show
        # through the plateau and the Condensery shows through the Veil. At 0.08 the same lobes have surfaces; at 0.15 the Veil's last wall hides the Condensery.
        "designed_for": {"extinction": 0.15, "noiseAmount": 0.72, "noiseScale": 0.018, "note": "tested in AV Gen with the scene's other values as they are (art test stills, work/art/it5/av); see notes/00-coordination.md, 'Art: iteration 5'"},
        "veil": {"note": "the Veil's rooms are built as absences (masses.veil holds the walls). The engine's generated passage lobes (build.py veil_passage) would fill them: drop them, and take the Veil's glows from here (same names as the routes use).",
                 "t0": VEIL_T0, "rooms": [{"room": v["room"], "t": v["t"], "centre": E(v["c"]), "radius": v["rho"]} for v in veil_voids()[0]],
                 "plug": {"from_t": 54.9, "to_t": 56.9, "note": "solid: the camera is blind from the lantern room's far wall to the crash at 56.85 s"},
                 "clear_after_t": 56.9, "glows": vg},
        "sea_design": {"top_mean": SEA_TOP, "top_range": [-85, 60], "base": SEA_BASE,
                       "break": {"centre": [BREAK["centre"][0], -BREAK["centre"][1]], "radius": BREAK["radius"], "elongation": BREAK["elongation"], "yaw_rad": -BREAK["yaw"],
                                 "note": "placed by design beside the Condensery, under the reveal; ADR-2101 sea.breaks takes it as it is (clouds_block.sea.breaks)"}},
        "wind": {"toward": [WIND[0], 0.0, -WIND[1]], "note": "leans, shelves, the plume and `drift` share it"},
        "moon": MOON,
        "bins": {"hero_masses": bins_report(allhero), "far_towers": bins_report(far),
                 "note": "replica of binCloudLobes, one grid per set (ADR-2104: clouds.lobes and clouds.farLobes): a cell holds at most 64 lobes and keeps the biggest."},
        "counts": {k: len(v) for k, v in masses.items()},
        "stats": STATS,
    }
    dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "art", "cloud_design.json")
    json.dump(out, open(dst, "w"), indent=None, separators=(",", ":"))
    print("cloud design:", out["counts"], "total", len(allhero), "far", len(far))
    print("bins hero:", out["bins"]["hero_masses"]); print("bins far:", out["bins"]["far_towers"]); print("stats:", STATS)
