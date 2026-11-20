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
    soft = 0.2 if dense else 0.34; dens = 1.0
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
    return out


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


def mooring_mound():
    """The plateau under the town: flattened lobes whose tops follow town_top, a rim of billows that grows away from the
    decks, and a hollow under the pier."""
    rr = random.Random(11); out = []

    def top(x, y):
        e = math.sqrt(((x - 15) / 250.0) ** 2 + ((y - 22) / 165.0) ** 2)             # 1 at the edge of the built area
        if e <= 1.0:
            return town_top(x, y), e
        f = min(1.0, (e - 1.0) / 0.9); f = f * f * (3 - 2 * f)
        return town_top(x, y) * (1 - f) + (SEA_TOP + 6) * f, e
    gy = -330
    while gy < 380:
        gx = -420
        while gx < 460:
            x, y = gx + rr.uniform(-12, 12), gy + rr.uniform(-12, 12)
            t, e = top(x, y)
            if e < 1.95 and math.hypot(x - HEAD[0], y - HEAD[1]) > 200 and math.hypot(x - VEIL[0], y - VEIL[1]) > 175:
                r = rr.uniform(34, 50) if e < 1.0 else rr.uniform(44, 70)
                hollow = 26 * math.exp(-(((x + 250) / 110.0) ** 2 + ((y + 125) / 60.0) ** 2))  # the pier reaches out over a hollow
                out.append(L(x, y, t - hollow - r * 0.5 * 0.8, r, 0.5, 1.0, 0.26 if e < 1.0 else 0.32))
                if e > 0.92:                                                      # the rim billows; calm under the decks
                    grow = min(1.0, (e - 0.92) / 0.5)
                    for k in range(rr.randint(1, 2)):
                        br = rr.uniform(16, 22 + 26 * grow)
                        bx, by = x + rr.uniform(-30, 30), y + rr.uniform(-30, 30)
                        bz = top(bx, by)[0] - hollow + br * rr.uniform(-0.1, 0.45) * grow
                        out.append(L(bx, by, bz, br, rr.uniform(0.75, 1.0), 1.0, 0.3))
                        if br > 32:
                            cauliflower(rr, out, bx, by, bz, br, 1, 0.3, 1.0, n=(2, 4), shrink=(0.38, 0.5))
            gx += 56.0
        gy += 56.0
    out += [L(185, -5, 108, 34, 0.8), L(204, -22, 112, 26, 0.85), L(170, 16, 106, 28, 0.8)]      # a swell that laps the garden terrace from behind
    # wisps: thin cloud standing a little above deck level round the edges of the town (never on the Quay), so the
    # outer stilts, the Low Walk and the backs of the Stacks are half lost in it
    for (x, y, z, r) in ((-96, -40, 126, 20), (-60, -30, 122, 18), (-20, -34, 118, 20), (24, -40, 116, 19), (150, -66, 118, 22), (176, -30, 120, 20), (-150, 20, 130, 22),
                         (-96, 84, 138, 22), (-20, 112, 142, 24), (40, 118, 142, 22), (96, 70, 134, 22), (132, 30, 124, 18), (-140, -60, 126, 20), (-190, -100, 124, 22),
                         (-250, -150, 120, 24), (70, -52, 114, 18)):
        out.append(L(x + rr.uniform(-6, 6), y + rr.uniform(-6, 6), z, r, rr.uniform(0.45, 0.6), 0.4, 0.6, sx=rr.uniform(1.2, 1.7)))
    return out


def plume():
    out = []; cx, cy, cz = CONDENSERY; rr = random.Random(41)
    for (lx, ly, hb) in ((-58, 14, 118), (-2, 30, 146), (52, 2, 100)):
        c, s = math.cos(CONDENSERY_YAW), math.sin(CONDENSERY_YAW); x, y = cx + lx * c - ly * s, cy + lx * s + ly * c; z = cz + hb + 10
        r = 22.0
        for k in range(7):
            out.append(L(x, y, z, r, rr.uniform(0.85, 1.0), 0.4 - 0.03 * k, 0.6))
            if k > 1:
                out.append(L(x + rr.uniform(-0.5, 0.5) * r, y + rr.uniform(-0.5, 0.5) * r, z + r * 0.5, r * 0.5, 0.9, 0.35, 0.5))
            x += WIND[0] * r * (0.35 + 0.22 * k); y += WIND[1] * r * (0.35 + 0.22 * k); z += r * max(0.2, 1.0 - 0.15 * k); r *= 1.2
    return out


def bins_report(lobes):
    """The renderer's binning (cloud_renderer.cpp binCloudLobes), replicated: cell size and the worst cell."""
    if not lobes:
        return {}
    ext = [(l["r"] * l["scale"][0], l["r"] * l["scale"][2]) for l in lobes]
    lo = (min(l["c"][0] - e[0] for l, e in zip(lobes, ext)), min(l["c"][2] - e[1] for l, e in zip(lobes, ext)))
    hi = (max(l["c"][0] + e[0] for l, e in zip(lobes, ext)), max(l["c"][2] + e[1] for l, e in zip(lobes, ext)))
    span = max(hi[0] - lo[0], hi[1] - lo[1]); rs = sorted(l["r"] for l in lobes)
    cell = max(0.5 * rs[len(rs) // 2], span / 64.0, 1.0)
    nx, nz = max(1, math.ceil((hi[0] - lo[0]) / cell)), max(1, math.ceil((hi[1] - lo[1]) / cell)); cells = {}
    for l, e in zip(lobes, ext):
        for z in range(max(0, int((l["c"][2] - e[1] - lo[1]) // cell)), min(nz - 1, int((l["c"][2] + e[1] - lo[1]) // cell)) + 1):
            for x in range(max(0, int((l["c"][0] - e[0] - lo[0]) // cell)), min(nx - 1, int((l["c"][0] + e[0] - lo[0]) // cell)) + 1):
                cells[(x, z)] = cells.get((x, z), 0) + 1
    worst = max(cells.values()); wk = max(cells, key=cells.get)
    return {"lobes": len(lobes), "span_m": round(span), "median_radius_m": rs[len(rs) // 2], "cell_m": round(cell, 1), "grid": [nx, nz], "worst_cell": worst, "worst_cell_at": [round(lo[0] + (wk[0] + 0.5) * cell), round(lo[1] + (wk[1] + 0.5) * cell)],
            "cells_over_64": sum(1 for v in cells.values() if v > 64)}


def build():
    masses = {"mooring_mound": mooring_mound()}
    masses["head"] = cumulus(23, HEAD[0], HEAD[1], SEA_TOP - 20, 365, 215, turrets=4, lean=0.2, shelves=((0.36, 1.1), (0.62, 0.8)), kids=(3, 4))
    # the Veil: upright, on the line from the approach to the Condensery, so it hides the flare from the Mooring, and
    # narrow enough above 190 m that the Drowned Moon's left limb shows beside it (seed and place chosen by projecting
    # its lobes from the approach camera: the flare covered, eight sample points on the limb free)
    masses["veil"] = cumulus(32, VEIL[0], VEIL[1], SEA_TOP - 20, 290, 150, turrets=3, lean=0.06, shelves=((0.5, 1.0), (0.74, 0.6)), kids=(3, 4))
    masses["fore"] = cumulus(23, FORE[0], FORE[1], SEA_TOP - 20, 128, 105, turrets=2, lean=0.3, shelves=((0.6, 1.2),))
    for i, (name, asset, x, y, z, yawd, rpm, ph, bl) in enumerate(LIGHTHOUSES):
        masses["rock_" + name] = rock(31 + i, x, y, z - 8.0, (54, 60, 50)[i])
    cx, cy, cz = CONDENSERY
    col = rock(40, cx, cy, cz - 12.0, 150, base=SEA_TOP - 40)
    col += cumulus(42, cx - 190, cy + 140, SEA_TOP - 20, 300, 150, turrets=3, shelves=((0.45, 1.1),), detail=1, kids=(3, 5), rich=False) + cumulus(43, cx + 180, cy - 80, SEA_TOP - 20, 190, 130, turrets=2, shelves=((0.55, 0.9),), detail=1, kids=(3, 5), rich=False)
    masses["condensery_column"] = col
    masses["condensery_plume"] = plume()
    lx, ly, lz = LONE_LAMP
    masses["lone_lamp"] = rock(50, lx, ly, lz - 9.0, 48)
    mid = []
    for i, (x, y, top, rad) in enumerate(((760, 430, 270, 190), (1250, 620, 220, 170), (1180, -520, 190, 160), (520, -620, 160, 130), (-560, 420, 210, 170), (1560, -260, 140, 125))):
        mid += cumulus(60 + i, x, y, SEA_TOP - 20, top, rad, turrets=3, shelves=((0.45, 1.2), (0.7, 0.6)), detail=1, kids=(3, 5), rich=False)
    masses["mid_field"] = mid
    far = []
    for i, (fx, fy, fh, fr) in enumerate(((-900, 1500, 500, 420), (1200, 2600, 700, 520), (3400, 2200, 900, 600), (4200, -300, 600, 520), (2600, -1500, 450, 420), (5200, 3800, 1300, 800),
                                          (7600, 900, 1100, 900), (300, 4600, 1000, 800), (8200, 4800, 1600, 1000), (-2800, 3200, 800, 700), (6200, -2600, 900, 800), (9800, 2000, 1400, 1100))):
        rr = random.Random(80 + i); f = []
        far += [l for l in cumulus(80 + i, fx, fy, SEA_TOP - 40, fh * 0.85, fr * 0.75, turrets=4, shelves=((0.5, 1.2),), mist=False, detail=1, kids=(3, 4), rich=False) if l["r"] > fr * 0.11]
    return masses, far


def glows():
    g = lambda name, p, radius, key, gain: {"name": name, "position": E(p), "radius": radius, "intensity": gain, "key": key}
    cx, cy, cz = CONDENSERY; lh = LIGHTHOUSES
    return [g("quay_west", (-55, qy(-55), qz(-55) + 4.5), 80, "bounce_b", 1.0), g("quay_mid", (10, qy(10), qz(10) + 4.5), 85, "bounce_b", 1.0), g("quay_east", (80, qy(80), qz(80) + 4.5), 80, "bounce_b", 0.9),
            g("keel_west", (-50, -12, 116), 120, "bounce_a", 1.4), g("keel_east", (50, -18, 110), 130, "bounce_a", 1.4),
            g("great_lantern", (GREAT_LANTERN[0], GREAT_LANTERN[1], GREAT_LANTERN[2] + 36), 190, "landmark", 2.0),
            g("gardens", (112, -30, 127), 100, "garden_a", 1.0), g("stacks", (10, 60, 150), 200, "bounce_b", 0.5), g("station_pier", (-150, -45, 134), 110, "win_a", 0.7),
            g("harbour_light", (lh[0][2], lh[0][3], lh[0][4] + 8), 90, "moon_disc", 0.8), g("outer_light", (lh[1][2], lh[1][3], lh[1][4] + 8), 110, "moon_disc", 0.8),
            g("break_light", (lh[2][2], lh[2][3], lh[2][4] + 8), 110, "moon_disc", 0.8),
            g("condensery_throats", (cx, cy - 12, cz + 130), 280, "cond_a", 2.0), g("condensery_flare", (cx - 112, cy + 40, cz + 166), 180, "cond_a", 1.6),
            g("condensery_keel", (cx - 20, cy, cz - 30), 220, "cond_b", 1.2), g("ocean_break", (BREAK["centre"][0], BREAK["centre"][1], -210), 420, "ocean_b", 1.8)]


if __name__ == "__main__":
    pal = json.load(open(os.path.join(HERE, "..", "art", "palette.json")))
    uv = {k: v["linear"] for k, v in pal["states"]["Ultraviolet Dream"].items()}; uv.update({k: v["linear"] for k, v in pal["fixed"].items()})
    masses, far = build()
    gl = glows()
    for g_ in gl:
        g_["color"] = uv[g_["key"]]
    mb = math.radians(MOON["light_from_bearing_deg"]); me = math.radians(MOON["light_elevation_deg"])
    ldir = [round(math.cos(mb) * math.cos(me), 4), round(math.sin(me), 4), round(-math.sin(mb) * math.cos(me), 4)]
    allhero = [l for m in masses.values() for l in m]
    out = {
        "version": 2,
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
        "sea_design": {"top_mean": SEA_TOP, "top_range": [-85, 60], "base": SEA_BASE,
                       "break": {"centre": [BREAK["centre"][0], -BREAK["centre"][1]], "radius": BREAK["radius"], "elongation": BREAK["elongation"], "yaw_rad": -BREAK["yaw"],
                                 "note": "placed by design beside the Condensery, under the reveal; ADR-2101 sea.breaks takes it as it is (clouds_block.sea.breaks)"}},
        "wind": {"toward": [WIND[0], 0.0, -WIND[1]], "note": "leans, shelves, the plume and `drift` share it"},
        "moon": MOON,
        "bins": {"hero_masses": bins_report(allhero), "with_far_towers": bins_report(allhero + far),
                 "note": "replica of binCloudLobes: a cell holds at most 64 lobes and keeps the biggest. The far towers stretch the grid (cells of span/64), which overfills the cells of the detailed formations: keep them as a second lobe set, or draw them another way."},
        "counts": {k: len(v) for k, v in masses.items()},
    }
    dst = os.path.join(HERE, "..", "art", "cloud_design.json")
    json.dump(out, open(dst, "w"), indent=None, separators=(",", ":"))
    print("cloud design:", out["counts"], "total", len(allhero), "far", len(far))
    print("bins hero:", out["bins"]["hero_masses"]); print("bins with far:", out["bins"]["with_far_towers"])
