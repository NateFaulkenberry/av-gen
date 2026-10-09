"""CP3: the carpets and the light. Builds the close-range organisms (life.py) and their emitter templates, places them
by ecological rules around each shot, and writes the ecosystem block (ADR-1200 emitter layers, ADR-1281 reflections).

Placement, per place:
  meadow     a wall-to-wall carpet of tip-stalks in drifts, grass between, dense at the lens and thinning outward
  riverbank  moss cushions and stalks on both banks, reeds at the water's edge, small fungi
  macro      moss mounds at macro scale, a hero pair of caps, lattice fungi, bulbs on the lens (bokeh)
  floor      glades of luminous stalks across the valley floor, clustered toward the water and the forest's gaps:
             at the vista's range only their light is drawn (the stalks themselves stop at 70 m)
  forest     every grove carries crown specks (tier 1); a clustered third of them, toward the river, carries an
             understorey glow (tier 2), half green, half cyan
"""
from __future__ import annotations

import math

import numpy as np

import flora
import land
import life
import populate
from geom import fbm2, quat_y

rec = populate.rec

N_TUFT = 5          # colour per variant: lime, emerald, cyan, blue, violet (TUFT_COLORS)
N_FROND = 3
N_GRASS = 3
N_MOSS = 4
N_LATTICE = 2
N_BULBS = 3
N_REEDS = 3
WATER_BANK = None   # set by place(): the riverbank's calm water surface
LIBRARY = True      # the approved hero library replaces the native organisms at the riverbank (library.bank)


def build(out):
    tris = 0
    for k in range(N_TUFT):
        s, b, e = life.tuft(1100 + k)
        tris += s.write(out / f"tuft{k}_stalk.glb", f"tuft{k}_stalk") + b.write(out / f"tuft{k}_bulb.glb", "bulb")
        life.write_emit(out / f"tuft{k}.emit.json", e)
    for k in range(N_FROND):
        m, e = life.frond(1900 + k)
        tris += m.write(out / f"frond{k}.glb", f"frond{k}")
        life.write_emit(out / f"frond{k}.emit.json", e)
    for k in range(N_GRASS):
        tris += life.grass(1200 + k).write(out / f"grass{k}.glb", f"grass{k}")
    for k in range(N_MOSS):
        m, e = life.moss(1300 + k, 1.0, dots=160)
        tris += m.write(out / f"moss{k}.glb", f"moss{k}")
        life.write_emit(out / f"moss{k}.emit.json", e)
    for k in range(N_LATTICE):
        m, e = life.lattice(1400 + k)
        tris += m.write(out / f"lattice{k}.glb", f"lattice{k}")
        life.write_emit(out / f"lattice{k}.emit.json", e)
    for k in range(N_BULBS):
        s, h, em = life.bulbs(1500 + k)
        # the orbs float (out of focus, as bokeh): their host is a one-millimetre speck, never seen
        speck = life.Mesh()
        speck.add(np.array([[0, 0, 0], [0.001, 0, 0], [0, 0, 0.001]]), np.array([[0, 2, 1]]))
        tris += speck.write(out / f"bulbs{k}_stalk.glb", "s")
        life.write_emit(out / f"bulbs{k}.emit.json", em)
    for k in range(N_REEDS):
        m, e = life.reeds(1600 + k)
        tris += m.write(out / f"reeds{k}.glb", f"reeds{k}")
        life.write_emit(out / f"reeds{k}.emit.json", e)
    for name, (seed, h, cr, ruf) in {"hero": (1700, 0.3, 0.13, 0.09), "bank": (1701, 0.45, 0.22, 0.07),
                                     "small": (1702, 0.14, 0.055, 0.1)}.items():
        st, cap, e = life.fungus3d(seed, h, cr, ruf)
        tris += st.write(out / f"f3d_{name}_stalk.glb", "s") + cap.write(out / f"f3d_{name}_cap.glb", "c")
        life.write_emit(out / f"f3d_{name}.emit.json", e)
    # the veil fungus's beads
    st, cap, th = flora.fungus(*populate.FUNGI["veil"])
    life.write_emit(out / "veil.emit.json", th.emit)
    # forest templates: crown specks from the leaves; an understorey glow under the crowns
    r = np.random.default_rng(1800)
    for k in range(populate.N_GROVES):
        _, leaf = flora.grove(700 + k)
        P, F, UV = leaf.arrays()
        sel = r.choice(len(P), 260, replace=False)
        crown = [[*P[i], 0.9, float(r.random()), 0.05] for i in sel]
        life.write_emit(out / f"grove{k}_crown.emit.json", crown)
        sel = r.choice(len(P), 380, replace=False)
        under = []
        for i in sel:
            x, y, z = P[i]
            under.append([x + r.normal(0, 1.5), r.uniform(0.15, 2.5) if r.random() < 0.8 else r.uniform(2.5, y),
                          z + r.normal(0, 1.5), 0.3, float(r.random()), r.uniform(0.04, 0.12)])
        life.write_emit(out / f"grove{k}_under.emit.json", under)
    for k in range(populate.N_SHRUBS):
        t = flora.tree("shrub", 800 + k, 4.0)
        P, F, UV = t.leaf.arrays()
        sel = r.choice(len(P), min(60, len(P)), replace=False)
        life.write_emit(out / f"shrub{k}.emit.json", [[*P[i], 0.8, float(r.random()), 0.05] for i in sel])
    return tris


# ---- placement ----------------------------------------------------------------------------------------------------


TUFT_COLORS = (life.LIME, life.EMERALD, life.CYAN, life.BLUE, life.VIOLET)


def zone(x, z, scale=1.0):
    """The colour zone of a place: blue zones, violet patches, emerald clusters, cyan and lime between. Returns a
    tuft variant (0 lime, 1 emerald, 2 cyan, 3 blue, 4 violet), by two low-frequency noises (spatially coherent)."""
    a = float(fbm2(np.array([x / (70.0 * scale)]), np.array([z / (70.0 * scale)]), 81, 3)[0])
    b = float(fbm2(np.array([x / (35.0 * scale)]), np.array([z / (35.0 * scale)]), 82, 2)[0])
    if a < 0.38:
        return 3                     # blue zone
    if b > 0.64:
        return 4                     # violet patch
    if a > 0.62:
        return 1                     # emerald cluster
    return 2 if b < 0.45 else 0      # cyan, lime


def _scatter(r, cx, cz, radius, spacing_near, spacing_far, cone=None):
    """Points around (cx, cz) out to `radius`, denser near the centre (spacing grows linearly), optionally inside a
    heading cone (deg, half-angle, heading_rad)."""
    pts = []
    d = 0.3
    while d < radius:
        s = spacing_near + (spacing_far - spacing_near) * (d / radius)
        n = max(3, int(2 * math.pi * d / s))
        for i in range(n):
            a = 2 * math.pi * (i + r.random()) / n
            dd = d + r.uniform(-0.5, 0.5) * s
            if cone is not None:
                half, head = cone
                da = (a - head + math.pi) % (2 * math.pi) - math.pi
                if abs(da) > half:
                    continue
            pts.append((cx + dd * math.sin(a), cz + dd * math.cos(a), dd))
        d += s
    return np.array(pts)


def place(gc, gfine, gfloor, eyes, headings):
    """Return {node prefix: {variant: records}} for every carpet organism."""
    r = np.random.default_rng(21)
    out = {k: {} for k in ("tuft", "grass", "moss", "lattice", "bulbs", "reeds", "f3d_hero", "f3d_bank",
                           "f3d_small", "glade", "mtuft", "mmoss", "frond")}

    def put(kind, v, rc):
        out[kind].setdefault(v, []).append(rc)

    # ---- MEADOW: wall-to-wall, in drifts
    g = gfine["meadow"]
    mx, mz = eyes["meadow"]
    P = _scatter(r, mx, mz, 190.0, 0.32, 2.6, cone=(math.radians(75), headings["meadow"]))
    P = P[P[:, 2] > 1.8]                                          # nothing on the lens
    edge = 190.0 * (0.45 + 0.6 * fbm2(P[:, 0] / 50.0, P[:, 1] / 50.0, 33, 3))
    P = P[P[:, 2] < edge]                                         # a ragged outer edge, not a ring
    X, Z, D = P[:, 0], P[:, 1], P[:, 2]
    H = g.height(X, Z)
    wet = g.wet(X, Z) | g.wet(X + 0.6, Z) | g.wet(X - 0.6, Z)
    drift = fbm2(X / 9.0, Z / 9.0, 31, 3) * 0.45 + fbm2(X / 45.0, Z / 45.0, 32, 2) * 0.55
    keep = (~wet) & (drift > 0.47) & (not LIBRARY)
    for x, z, y, d, dv in zip(X[keep], Z[keep], H[keep], D[keep], drift[keep]):
        put("tuft", zone(x, z), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(0.5, 1.2) *
                                    (0.4 + 1.6 * (dv - 0.47))))
        if d < 45 and r.random() < 0.7:
            put("grass", int(r.integers(0, N_GRASS)), rec(x + r.normal(0, 0.2), y - 0.02, z + r.normal(0, 0.2),
                                                          r.uniform(0, 6.28), r.uniform(0.8, 1.6)))
    # fronds along the meadow stream's banks
    for x, z, y, d in zip(X, Z, H, D):
        if (not LIBRARY) and d < 70 and r.random() < 0.08:
            nw = any(g.wet(np.array([x + dx]), np.array([z + dz]))[0] for dx, dz in ((1.5, 0), (-1.5, 0), (0, 1.5),
                                                                                      (0, -1.5)))
            if nw:
                put("frond", int(r.integers(0, N_FROND)), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(0.8, 1.6)))
    # dark moss between the drifts near the lens
    for x, z, y, d, dv in zip(X, Z, H, D, drift):
        if (not LIBRARY) and d < 40 and dv <= 0.47 and r.random() < 0.25 and not g.wet(np.array([x]), np.array([z]))[0]:
            put("moss", int(r.integers(0, N_MOSS)), rec(x, y - 0.1, z, r.uniform(0, 6.28), r.uniform(0.3, 0.9)))

    # ---- RIVERBANK: both banks of the calm reach, out to 110 m upstream of the lens
    g = gfine["bank"]
    bx, bz = eyes["riverbank"]
    P = _scatter(r, bx, bz, 130.0, 0.35, 2.2, cone=(math.radians(80), headings["riverbank"]))
    X, Z, D = P[:, 0], P[:, 1], P[:, 2]
    H = g.height(X, Z)
    WL = g.water_level(X, Z)
    wet = g.wet(X, Z)
    near_water = np.zeros(len(X), bool)
    for dx, dz in ((1.2, 0), (-1.2, 0), (0, 1.2), (0, -1.2)):
        near_water |= g.wet(X + dx, Z + dz)
    patch = fbm2(X / 6.0, Z / 6.0, 33, 3)
    for x, z, y, d, nw, w_, pv in zip(X, Z, H, D, near_water, wet, patch):
        if w_:
            continue
        if LIBRARY:
            # the library carries the banks: native reeds at the water's edge and sparse dark moss remain
            if False:
                put("reeds", int(r.integers(0, N_REEDS)), rec(x, y - 0.05, z, r.uniform(0, 6.28),
                                                              r.uniform(0.7, 1.3)))
            elif pv > 0.72 and r.random() < 0.4:
                put("moss", int(r.integers(0, N_MOSS)), rec(x, y - 0.12, z, r.uniform(0, 6.28), r.uniform(0.4, 1.2)))
            continue
        if nw and r.random() < 0.45:
            put("reeds", int(r.integers(0, N_REEDS)), rec(x, y - 0.05, z, r.uniform(0, 6.28), r.uniform(0.7, 1.3)))
        elif nw and r.random() < 0.5:
            # fronds lean out over the water: turned so they arch toward it
            to_w = math.atan2(0.0, 1.0)
            put("frond", int(r.integers(0, N_FROND)), rec(x, y - 0.03, z, r.uniform(0, 6.28), r.uniform(1.0, 2.2)))
        elif pv > 0.62:
            put("moss", int(r.integers(0, N_MOSS)), rec(x, y - 0.12, z, r.uniform(0, 6.28), r.uniform(0.4, 1.4)))
        elif pv > 0.4:
            put("tuft", zone(x, z, 0.4), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(0.8, 1.8)))
        if d < 40 and r.random() < 0.35:
            put("grass", int(r.integers(0, N_GRASS)), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(1.0, 2.0)))
        if d < 70 and r.random() < 0.05:
            put("f3d_small", [3, 4, 2][int(r.integers(0, 3))], rec(x, y - 0.01, z, r.uniform(0, 6.28),
                                                                r.uniform(0.8, 2.5)))
    # the bank's bigger fungi along the near left bank (native; the library's veilcaps replace them)
    for k in range(0 if LIBRARY else 22):
        x = bx - r.uniform(5, 60)
        zs = np.arange(land.BANK_Z, land.BANK_Z + 20, 0.4)
        hs = g.height(np.full(len(zs), x), zs)
        dry = zs[hs > (WATER_BANK + 0.4)]
        if len(dry) == 0:
            continue
        z = float(dry[0]) + r.uniform(0.3, 2.5)
        put("f3d_bank", 0, rec(x, float(g.height(np.array([x]), np.array([z]))[0]) - 0.02, z, r.uniform(0, 6.28),
                               r.uniform(0.7, 1.6)))

    # ---- MACRO: within a few metres of the lens, at organism scale: dense and layered (panel 3)
    ex, ez = eyes["macro"]
    head = headings["macro"]

    def ahead(d, off):
        a_ = head + off
        x_, z_ = ex + d * math.sin(a_), ez + d * math.cos(a_)
        return x_, float(g.height(np.array([x_]), np.array([z_]))[0]), z_

    P = _scatter(r, ex, ez, 6.0, 0.05, 0.35, cone=(math.radians(55), head))
    P = P[P[:, 2] > 0.3]
    X, Z, D = P[:, 0], P[:, 1], P[:, 2]
    H = g.height(X, Z)
    wet = g.wet(X, Z)
    patch = fbm2(X / 0.7, Z / 0.7, 35, 3)
    for x, z, y, d, w_, pv in zip(X, Z, H, D, wet, patch):
        if w_:
            continue
        # a continuous floor of moss mounds of all sizes, with stalks and small fungi growing from it
        if r.random() < 0.55:
            put("mmoss", int(r.integers(0, N_MOSS)), rec(x, y - 0.04, z, r.uniform(0, 6.28),
                                                         r.uniform(0.08, 0.3) * (0.6 + d * 0.25)))
        if pv > 0.45 and r.random() < 0.6:
            put("mtuft", zone(x, z, 0.03), rec(x, y - 0.01, z, r.uniform(0, 6.28), r.uniform(0.2, 0.45)))
        elif pv < 0.4 and r.random() < 0.18:
            put("f3d_small", [3, 4, 2, 3][int(r.integers(0, 4))], rec(x, y, z, r.uniform(0, 6.28),
                                                                   r.uniform(0.5, 1.5)))
        elif r.random() < 0.05:
            put("frond", int(r.integers(0, N_FROND)), rec(x, y - 0.01, z, r.uniform(0, 6.28), r.uniform(0.08, 0.22)))
    # the hero pair, left of centre (panel 3's emerald caps), and a second group deeper right
    for d, off, s in ((0.85, 0.2, 1.0), (1.0, 0.3, 0.8), (2.6, -0.3, 1.3), (2.9, -0.24, 1.0)):
        x, y, z = ahead(d, off)
        put("f3d_hero", 0, rec(x, y - 0.01, z, r.uniform(0, 6.28), s))
    for d, off in ((1.1, 0.5), (1.6, -0.15), (2.4, 0.2), (1.3, 0.62), (3.2, 0.4), (2.0, -0.45)):
        x, y, z = ahead(d, off)
        put("lattice", int(r.integers(0, N_LATTICE)), rec(x, y, z, r.uniform(0, 6.28), r.uniform(0.8, 1.6)))
    # foreground orbs, mostly low in frame (panel 3's bottom), of different sizes; variant 2 is faint
    for k, (d, off) in enumerate(((0.3, -0.55), (0.34, -0.42), (0.4, -0.65), (0.33, 0.6), (0.45, -0.25),
                                  (0.5, 0.45), (0.38, -0.7), (0.6, -0.5), (0.55, 0.3))):
        x, y, z = ahead(d, off)
        put("bulbs", k % N_BULBS, rec(x, y - 0.06, z, r.uniform(0, 6.28), r.uniform(0.35, 1.0)))
    # big moss mounds in the lower foreground, both sides (panel 3's dark mounds)
    for d, off, s in ((0.38, -0.6, 0.3), (0.42, 0.62, 0.26), (0.75, -0.4, 0.3), (0.65, 0.5, 0.22)):
        x, y, z = ahead(d, off)
        put("mmoss", int(r.integers(0, N_MOSS)), rec(x, y - 0.06, z, r.uniform(0, 6.28), s))

    # ---- FLOOR GLADES: clustered luminous stalks across the valley floor (their light carries to the vista)
    X, Z = populate._jgrid(r, gfloor.x0 + 10, gfloor.x0 + gfloor.h.shape[1] * gfloor.step - 10,
                           gfloor.z0 + 10, gfloor.z0 + gfloor.h.shape[0] * gfloor.step - 10, 4.0)
    H = gfloor.height(X, Z)
    rel = H - np.array([land.floor_level(z) for z in Z])
    wet = gfloor.wet(X, Z)
    nearw = np.zeros(len(X), bool)
    for dx, dz in ((15, 0), (-15, 0), (0, 15), (0, -15)):
        nearw |= gfloor.wet(X + dx, Z + dz)
    cl = fbm2(X / 120.0, Z / 120.0, 41, 3) * 0.65 + fbm2(X / 25.0, Z / 25.0, 42, 2) * 0.35
    quiet = fbm2(X / 420.0, Z / 420.0, 43, 2)                       # large dark, quiet regions
    p = (cl - 0.5) * 3.0 - rel / 120.0 - 2.2 * np.clip(0.55 - quiet, 0, 1) / 0.2
    keep = (~wet) & (~nearw) & (p > 0.18) & (gfloor.slope(X, Z) < 0.8)   # the river itself stays water
    for exx, ezz in eyes.values():                                # the shots' own carpets are near; glades are far
        keep &= np.hypot(X - exx, Z - ezz) > 160.0
    for x, z, y in zip(X[keep], Z[keep], H[keep]):
        put("glade", zone(x, z, 3.0), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(0.9, 1.8)))
    for k, v in out.items():
        print(f"  {k}: {sum(len(x) for x in v.values())}")
    return out


def split_groves(groves):
    """Split the forest's grove records into the glowing (clustered, toward the river) and the dark."""
    glow, dark = {}, {}
    for v, pts in groves.items():
        for rc in pts:
            x, z = rc[0], rc[2]
            c = fbm2(np.array([x / 150.0]), np.array([z / 150.0]), 51, 3)[0]
            dr = abs(x - land.rx(z))
            p = c + 0.08 * math.exp(-dr / 120.0)
            (glow if p > 0.6 else dark).setdefault(v, []).append(rc)
    return glow, dark


# ---- nodes and layers ------------------------------------------------------------------------------------------------


def nodes(out, mat):
    """Procedural host nodes for the carpets. Bodies stop drawing at tens of metres; their light does not."""
    N = []

    def chunks(lst, n=60000):
        return [lst[i:i + n] for i in range(0, len(lst), n)]

    def add(name, glb, pts, m, maxd, lod=None):
        names = []
        for i, part in enumerate(chunks(pts)):
            nm = f"{name}_{i}"
            N.append({"name": nm, "kind": "procedural", "procedural": {
                "source": {"kind": "mesh", "asset": f"../meshes/{glb}.glb"},
                "distribution": {"kind": "points", "points": part}, "castsShadow": False,
                "lod": lod or {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": maxd},
                "material": m}})
            names.append(nm)
        return names

    stalk = mat([0.015, 0.03, 0.03], 0.6)
    hosts = {}

    def bulb(v):
        return mat([0.02, 0.03, 0.04], 0.4, TUFT_COLORS[v], 1.6, program="bulb")
    for v, pts in out["tuft"].items():
        add(f"tuft{v}s", f"tuft{v}_stalk", pts, stalk, 80.0)
        hosts.setdefault(f"tuft{v}", []).extend(add(f"tuft{v}b", f"tuft{v}_bulb", pts, bulb(v), 80.0))
    for v, pts in out["mtuft"].items():
        add(f"mtuft{v}s", f"tuft{v}_stalk", pts, stalk, 30.0)
        hosts.setdefault(f"mtuft{v}", []).extend(add(f"mtuft{v}b", f"tuft{v}_bulb", pts, bulb(v), 30.0))
    for v, pts in out["frond"].items():
        col = [life.CYAN, life.EMERALD, life.BLUE][v % 3]
        hosts.setdefault("frond", []).extend(add(f"frond{v}", f"frond{v}", pts,
                                                 mat([0.01, 0.03, 0.03], 0.6, col, 0.35, program="frond",
                                                     doubleSided=True, alphaCutoff=0.5, alphaMode="mask"), 120.0))
    for v, pts in out["mmoss"].items():
        col = [life.BLUE, life.TEAL, life.BLUE, life.EMERALD][v % 4]
        hosts.setdefault("mmoss", []).extend(add(f"mmoss{v}", f"moss{v}", pts,
                                                 mat([0.008, 0.016, 0.03], 0.95, col, 0.12, program="moss"), 30.0))
    for v, pts in out["glade"].items():
        hosts.setdefault(f"glade{v}", []).extend(add(f"glade{v}s", f"tuft{v}_stalk", pts, stalk, 60.0))
    for v, pts in out["grass"].items():
        add(f"grass{v}", f"grass{v}", pts, mat([0.012, 0.03, 0.02], 0.8, doubleSided=True), 45.0)
    for v, pts in out["moss"].items():
        col = [life.BLUE, life.TEAL][v % 2]
        hosts.setdefault(f"moss{v % 2}", []).extend(add(f"moss{v}", f"moss{v}", pts,
                                                        mat([0.008, 0.018, 0.03], 0.95, col, 0.07, program="moss"),
                                                        120.0))
    for v, pts in out["reeds"].items():
        hosts.setdefault(f"reeds{v}", []).extend(add(f"reeds{v}", f"reeds{v}", pts,
                                                 mat([0.02, 0.04, 0.035], 0.7, doubleSided=True), 120.0))
    for v, pts in out["lattice"].items():
        hosts.setdefault("lattice", []).extend(add(f"lattice{v}", f"lattice{v}", pts,
                                                   mat([0.04, 0.03, 0.09], 0.5, life.VIOLET, 0.6, program="cap",
                                                       doubleSided=True), 60.0))
    for v, pts in out["bulbs"].items():
        hosts.setdefault(f"bulbs{v}", []).extend(add(f"bulbs{v}_stalk", f"bulbs{v}_stalk", pts,
                                                     mat([0.03, 0.05, 0.04], 0.6), 0.05))   # orbs float: the stalks never draw
    small = out["f3d_small"]
    for v, nm in ((2, "small"), (3, "smallb"), (4, "smallv")):
        out[f"f3d_{nm}"] = {0: small.get(v, []) + (small.get(0, []) if v == 2 else [])}
    for name in ("hero", "bank", "small", "smallb", "smallv"):
        pts = out[f"f3d_{name}"].get(0, [])
        mesh = "small" if name.startswith("small") else name
        if not pts:
            continue
        add(f"f3d_{name}_stalk", f"f3d_{mesh}_stalk", pts, mat([0.02, 0.04, 0.05], 0.5, life.EMERALD, 0.03,
                                                               opacity=0.85), 200.0)
        col = {"hero": life.EMERALD, "bank": life.CYAN, "small": life.TEAL, "smallb": life.BLUE,
               "smallv": life.VIOLET}[name]
        hosts.setdefault("f3d", []).extend(add(f"f3d_{name}_cap", f"f3d_{mesh}_cap", pts,
                                               mat([0.02, 0.06, 0.06], 0.4, col, 0.32, program="cap",
                                                   doubleSided=True), 200.0))
        hosts.setdefault(f"f3d_{mesh}_emit", []).append(f"f3d_{name}_cap_0")
    return N, hosts


def layer(name, hosts, template, color, intensity, **kw):
    d = {"name": name, "hosts": hosts, "template": f"../meshes/{template}", "color": color, "intensity": intensity,
         "excitedColor": color, "excitedIntensity": 0.0, "size": 1.0, "breath": 0.25, "breathRate": 0.06,
         "flicker": 0.15, "flickerRate": 2.0, "sparsity": 0.0, "maxDistance": 300.0}
    d.update(kw)
    return d


def ecosystem(hosts, forest_hosts, shrub_hosts, veil_hosts, water_level):
    """The emitter layers. Tier 1 subtle, tier 2 active, tier 3 hero (see the module docstring). Brightness clumps
    by a low-frequency field (`clumps`, as each layer's wake): drifts of brighter organisms among dimmer ones, never a
    uniform stipple. Bank layers mirror in the calm reach; floor layers in the descending river (ADR-1281)."""
    bank = {"mirror": 1.6, "mirrorLevel": round(water_level, 3), "mirrorStretch": 0.9}
    river = {"mirror": 0.35, "mirrorLevel": round(land.river_level(0.0) + 1.8, 3), "mirrorStretch": 3.0,
             "mirrorSlope": [0.0, 0.0012]}
    clump = {"wakeField": "clumps", "wakeGain": 3.0}
    L = []
    cols = TUFT_COLORS
    for v in range(N_TUFT):
        # the bulbs themselves are lit meshes; the emitter is each bulb's halo near, and its whole light far
        hs = hosts.get(f"tuft{v}", []) + hosts.get(f"mtuft{v}", [])
        gs = hosts.get(f"glade{v}", [])
        if hs:
            L.append(layer(f"tips{v}", hs, f"tuft{v}.emit.json", cols[v], 0.6, maxDistance=900.0, sparsity=0.15,
                           pulseRate=1.5, pulseDecay=0.8, nearFade=0.5, **clump, **bank))
        if gs:
            L.append(layer(f"glade{v}", gs, f"tuft{v}.emit.json", cols[v], 0.9, size=3.5, maxDistance=5200.0,
                           sparsity=0.3, pulseRate=1.0, pulseDecay=0.8, **clump, **river))
    if hosts.get("frond"):
        L.append(layer("frondGlow", hosts["frond"], "frond0.emit.json", life.CYAN, 0.8, maxDistance=200.0,
                       sparsity=0.4, **bank))
    for k, col in ((0, life.BLUE), (1, life.TEAL)):
        hs = hosts.get(f"moss{k}", [])
        if hs:
            L.append(layer(f"mossDots{k}", hs, f"moss{k}.emit.json", col, 0.35, sparsity=0.5, maxDistance=180.0,
                           flicker=0.3, flickerRate=1.3, **clump, **bank))
    if hosts.get("mmoss"):
        L.append(layer("macroMoss", hosts["mmoss"], "moss0.emit.json", life.BLUE, 0.7, sparsity=0.7,
                       maxDistance=40.0, flicker=0.3, flickerRate=1.0, wakeField="clumps", wakeGain=2.0))
    for v, col in ((0, life.EMERALD), (1, life.CYAN), (2, life.VIOLET)):
        hs = hosts.get(f"reeds{v}", [])
        if hs:
            L.append(layer(f"reedTips{v}", hs, f"reeds{v}.emit.json", col, 2.4, maxDistance=600.0, **bank))
    if hosts.get("lattice"):
        L.append(layer("latticeGlow", hosts["lattice"], "lattice0.emit.json", life.VIOLET, 1.2, maxDistance=60.0))
    for name, col, inten in (("hero", life.LIME, 4.0), ("bank", life.CYAN, 3.0), ("small", life.PALE, 1.2)):
        hs = hosts.get(f"f3d_{name}_emit", [])
        if hs:
            L.append(layer(f"gills_{name}", hs, f"f3d_{name}.emit.json", col, inten, maxDistance=400.0, **bank))
    # the macro's foreground orbs: soft discs, varied: a blue and a green of different strength, and a faint cyan
    for v, col, inten in ((0, life.BLUE, 1.4), (1, life.EMERALD, 1.0), (2, life.CYAN, 0.35)):
        hs = hosts.get(f"bulbs{v}", [])
        if hs:
            L.append(layer(f"orbs{v}", hs, f"bulbs{v}.emit.json", col, inten, maxDistance=40.0, flicker=0.1))
    if veil_hosts:
        L.append(layer("veilBeads", veil_hosts, "veil.emit.json", life.PALE, 5.0, maxDistance=900.0, flicker=0.25,
                       flickerRate=0.7, **bank))
    allc = forest_hosts["all"]
    for k, col in ((0, life.CYAN), (1, life.BLUE)):
        hs = [h for v in range(len(allc)) if v % 2 == k for h in allc[v]]
        if hs:
            L.append(layer(f"crown{k}", hs, f"grove{k}_crown.emit.json", col, 0.2, sparsity=0.85, maxDistance=5200.0,
                           **clump))
    glow = [h for v in range(len(forest_hosts["glow"])) for h in forest_hosts["glow"][v]]
    if glow:
        half = len(glow) // 2
        L.append(layer("understoryGreen", glow[:half] or glow, "grove0_under.emit.json", life.LIME, 2.5, size=2.0,
                       maxDistance=5200.0, sparsity=0.2, pulseRate=0.8, **clump, **river))
        L.append(layer("understoryCyan", glow[half:] or glow, "grove1_under.emit.json", life.CYAN, 2.2, size=2.0,
                       maxDistance=5200.0, sparsity=0.2, pulseRate=0.8, **clump, **river))
    if shrub_hosts:
        L.append(layer("ledgeSpecks", shrub_hosts, "shrub0.emit.json", life.TEAL, 1.0, sparsity=0.5,
                       maxDistance=5200.0))
    assert len(L) <= 32, len(L)
    return {"enabled": True, "spriteRadius": 1.5, "maxSprites": 65536, "layers": L}


# ---- particles (spec §16): distinct populations, each with its own scale, motion and light ---------------------------


def particles(eyes):
    """Motes near the close shots, spores over the meadow, spray drifting down the riverbank's stream. Field-like
    motion through the engine's turbulence (curl-ish noise) and drift, never a uniform random cloud."""
    out = []

    def system(name, pos, extent, rate, life_, speed, size, color, emissive, gravity, turb, seed, direction=(0, 1, 0),
               spread=1.0, drag=0.6):
        out.append({"name": name, "kind": "particles", "particles": {
            "capacity": int(rate * life_[1] * 1.2) + 16, "seed": seed, "shape": "disc",
            "position": [round(float(v), 2) for v in pos], "extent": [float(extent[0]), 0.0, float(extent[1])],
            "direction": list(direction), "spread": spread, "spawnRate": float(rate),
            "speedMin": speed[0], "speedMax": speed[1], "gravity": list(gravity), "drag": drag,
            "turbulence": turb[0], "turbulenceScale": turb[1], "turbulenceSpeed": turb[2],
            "sizeStart": size[0], "sizeEnd": size[1], "sizeVariance": 0.6,
            "lifetimeMin": life_[0], "lifetimeMax": life_[1],
            "colorStart": [*color, 1.0], "colorEnd": [*color, 0.0], "emissive": emissive, "blend": "additive",
            "softness": 0.8}})

    bx, bz = eyes["riverbank"]
    mx, mz = eyes["meadow"]
    cx, cz = eyes["macro"]
    gy = 4.0
    # B. near-camera motes: sparse, larger, slow; some drift through the depth of field
    system("motesBank", (bx - 25, gy + 1.5, bz), (40, 30), 40, (8, 14), (0.05, 0.25), (0.03, 0.02), life.CYAN, 2.5,
           (0, 0.02, 0), (0.5, 0.25, 0.15), 71)
    system("motesMeadow", (mx, gy + 1.5, mz + 30), (60, 60), 60, (8, 14), (0.05, 0.25), (0.04, 0.02), life.LIME, 2.0,
           (0, 0.02, 0), (0.5, 0.2, 0.15), 72)
    system("motesMacro", (cx - 1.5, gy + 0.4, cz + 1.0), (3, 3), 12, (6, 10), (0.01, 0.05), (0.004, 0.002), life.PALE,
           2.5, (0, 0.005, 0), (0.3, 1.5, 0.2), 73)
    # A. ambient spores over the meadow: tiny, slow, rising, far more of them
    system("sporesMeadow", (mx, gy + 0.5, mz + 120), (180, 200), 500, (12, 20), (0.02, 0.12), (0.012, 0.006),
           life.EMERALD, 1.5, (0, 0.03, 0), (0.6, 0.06, 0.08), 74)
    # D. water-adjacent spray: drifts downstream (+x, toward the river) low over the bank stream
    system("streamDrift", (bx - 40, gy + 0.6, bz), (50, 8), 120, (6, 10), (0.2, 0.6), (0.01, 0.005), life.TEAL, 2.0,
           (0, 0.01, 0), (0.4, 0.3, 0.3), 75, direction=(1, 0.05, 0), spread=0.4, drag=0.3)
    return out
