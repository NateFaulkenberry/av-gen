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

N_TUFT = 5
N_GRASS = 3
N_MOSS = 4
N_LATTICE = 2
N_BULBS = 3
N_REEDS = 3
WATER_BANK = None   # set by place(): the riverbank's calm water surface


def build(out):
    tris = 0
    for k in range(N_TUFT):
        m, e = life.tuft(1100 + k)
        tris += m.write(out / f"tuft{k}.glb", f"tuft{k}")
        life.write_emit(out / f"tuft{k}.emit.json", e)
    for k in range(N_GRASS):
        tris += life.grass(1200 + k).write(out / f"grass{k}.glb", f"grass{k}")
    for k in range(N_MOSS):
        m, e = life.moss(1300 + k, 1.0, dots=700)
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
                           "f3d_small", "glade")}

    def put(kind, v, rc):
        out[kind].setdefault(v, []).append(rc)

    # ---- MEADOW: wall-to-wall, in drifts
    g = gfine["meadow"]
    mx, mz = eyes["meadow"]
    P = _scatter(r, mx, mz, 190.0, 0.32, 2.6, cone=(math.radians(75), headings["meadow"]))
    P = P[P[:, 2] > 1.8]                                          # nothing on the lens
    X, Z, D = P[:, 0], P[:, 1], P[:, 2]
    H = g.height(X, Z)
    wet = g.wet(X, Z) | g.wet(X + 0.6, Z) | g.wet(X - 0.6, Z)
    drift = fbm2(X / 9.0, Z / 9.0, 31, 3) * 0.6 + fbm2(X / 40.0, Z / 40.0, 32, 2) * 0.4
    keep = (~wet) & (drift > 0.44)
    for x, z, y, d, dv in zip(X[keep], Z[keep], H[keep], D[keep], drift[keep]):
        put("tuft", int(r.integers(0, N_TUFT)), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(0.5, 1.2) *
                                                   (0.4 + 1.6 * (dv - 0.44))))
        if d < 45 and r.random() < 0.7:
            put("grass", int(r.integers(0, N_GRASS)), rec(x + r.normal(0, 0.2), y - 0.02, z + r.normal(0, 0.2),
                                                          r.uniform(0, 6.28), r.uniform(0.8, 1.6)))
    # dark moss between the drifts near the lens
    for x, z, y, d, dv in zip(X, Z, H, D, drift):
        if d < 40 and dv <= 0.44 and r.random() < 0.25 and not g.wet(np.array([x]), np.array([z]))[0]:
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
        if nw and r.random() < 0.5:
            put("reeds", int(r.integers(0, N_REEDS)), rec(x, y - 0.05, z, r.uniform(0, 6.28), r.uniform(0.7, 1.3)))
        elif pv > 0.5:
            put("moss", int(r.integers(0, N_MOSS)), rec(x, y - 0.12, z, r.uniform(0, 6.28), r.uniform(0.4, 1.4)))
        elif pv > 0.38:
            put("tuft", int(r.integers(0, N_TUFT)), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(0.8, 1.6)))
        if d < 40 and r.random() < 0.35:
            put("grass", int(r.integers(0, N_GRASS)), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(1.0, 2.0)))
        if d < 70 and r.random() < 0.04:
            put("f3d_small", 0, rec(x, y - 0.01, z, r.uniform(0, 6.28), r.uniform(0.8, 2.5)))
    # the bank's bigger fungi along the near left bank
    for k in range(22):
        x = bx - r.uniform(5, 60)
        zs = np.arange(land.BANK_Z, land.BANK_Z + 20, 0.4)
        hs = g.height(np.full(len(zs), x), zs)
        dry = zs[hs > (WATER_BANK + 0.4)]
        if len(dry) == 0:
            continue
        z = float(dry[0]) + r.uniform(0.3, 2.5)
        put("f3d_bank", 0, rec(x, float(g.height(np.array([x]), np.array([z]))[0]) - 0.02, z, r.uniform(0, 6.28),
                               r.uniform(0.7, 1.6)))

    # ---- MACRO: within a few metres of the lens, at organism scale
    ex, ez = eyes["macro"]
    head = headings["macro"]
    P = _scatter(r, ex, ez, 7.0, 0.07, 0.5, cone=(math.radians(60), head))
    P = P[P[:, 2] > 0.35]
    X, Z, D = P[:, 0], P[:, 1], P[:, 2]
    H = g.height(X, Z)
    wet = g.wet(X, Z)
    patch = fbm2(X / 0.9, Z / 0.9, 35, 3)
    for x, z, y, d, w_, pv in zip(X, Z, H, D, wet, patch):
        if w_ or d < 0.25:
            continue
        if pv > 0.52:
            put("moss", int(r.integers(0, N_MOSS)), rec(x, y - 0.03, z, r.uniform(0, 6.28), r.uniform(0.06, 0.2) * (0.6 + d * 0.3)))
        elif pv > 0.4:
            put("tuft", int(r.integers(0, N_TUFT)), rec(x, y - 0.01, z, r.uniform(0, 6.28), r.uniform(0.25, 0.55)))
        elif d > 1.5 and r.random() < 0.12:
            put("f3d_small", 0, rec(x, y, z, r.uniform(0, 6.28), r.uniform(0.6, 1.4)))

    def ahead(d, off):
        a = head + off
        x, z = ex + d * math.sin(a), ez + d * math.cos(a)
        return x, float(g.height(np.array([x]), np.array([z]))[0]), z

    # the hero pair, left of centre (panel 3's emerald caps)
    for d, off, s in ((0.85, 0.2, 1.0), (1.0, 0.3, 0.8)):
        x, y, z = ahead(d, off)
        put("f3d_hero", 0, rec(x, y - 0.01, z, r.uniform(0, 6.28), s))
    for d, off in ((1.1, 0.5), (1.6, -0.15), (2.4, 0.2)):
        x, y, z = ahead(d, off)
        put("lattice", int(r.integers(0, N_LATTICE)), rec(x, y, z, r.uniform(0, 6.28), r.uniform(0.8, 1.4)))
    # foreground bulbs on the lens: out of focus, they become the reference's bokeh orbs
    for k in range(26):
        x, y, z = ahead(r.uniform(0.25, 0.55), r.uniform(-0.75, 0.75))
        put("bulbs", int(r.integers(0, N_BULBS)), rec(x, y, z, r.uniform(0, 6.28), r.uniform(0.5, 1.0)))
    # a big moss mound in the lower foreground, both sides (panel 3's dark mounds with dots)
    for d, off, s in ((0.42, -0.62, 0.2), (0.45, 0.66, 0.18), (0.9, -0.35, 0.22)):
        x, y, z = ahead(d, off)
        put("moss", int(r.integers(0, N_MOSS)), rec(x, y - 0.05, z, r.uniform(0, 6.28), s))

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
    p = (cl - 0.5) * 3.0 + 0.2 * nearw - rel / 80.0
    keep = (~wet) & (p > 0.18) & (gfloor.slope(X, Z) < 0.7)
    for exx, ezz in eyes.values():                                # the shots' own carpets are near; glades are far
        keep &= np.hypot(X - exx, Z - ezz) > 160.0
    for x, z, y in zip(X[keep], Z[keep], H[keep]):
        put("glade", int(r.integers(0, N_TUFT)), rec(x, y - 0.02, z, r.uniform(0, 6.28), r.uniform(0.9, 1.8)))
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
            p = c + 0.35 * math.exp(-dr / 120.0)
            (glow if p > 0.62 else dark).setdefault(v, []).append(rc)
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

    stalk = mat([0.03, 0.06, 0.04], 0.6, life.EMERALD, 0.08)
    hosts = {}
    for v, pts in out["tuft"].items():
        hosts.setdefault("tuft", []).extend(add(f"tuft{v}", f"tuft{v}", pts, stalk, 80.0))
    for v, pts in out["glade"].items():
        hosts.setdefault("glade", []).extend(add(f"glade{v}", f"tuft{v}", pts, stalk, 60.0))
    for v, pts in out["grass"].items():
        add(f"grass{v}", f"grass{v}", pts, mat([0.012, 0.03, 0.02], 0.8, doubleSided=True), 45.0)
    for v, pts in out["moss"].items():
        hosts.setdefault(f"moss{v % 2}", []).extend(add(f"moss{v}", f"moss{v}", pts,
                                                        mat([0.015, 0.04, 0.03], 0.95, program="moss"), 120.0))
    for v, pts in out["reeds"].items():
        hosts.setdefault("reeds", []).extend(add(f"reeds{v}", f"reeds{v}", pts,
                                                 mat([0.02, 0.04, 0.035], 0.7, doubleSided=True), 120.0))
    for v, pts in out["lattice"].items():
        hosts.setdefault("lattice", []).extend(add(f"lattice{v}", f"lattice{v}", pts,
                                                   mat([0.04, 0.03, 0.09], 0.5, life.VIOLET, 0.6, program="cap",
                                                       doubleSided=True), 60.0))
    for v, pts in out["bulbs"].items():
        hosts.setdefault(f"bulbs{v}", []).extend(add(f"bulbs{v}_stalk", f"bulbs{v}_stalk", pts,
                                                     mat([0.03, 0.05, 0.04], 0.6), 0.05))   # orbs float: the stalks never draw
    for name in ("hero", "bank", "small"):
        pts = out[f"f3d_{name}"].get(0, [])
        if not pts:
            continue
        add(f"f3d_{name}_stalk", f"f3d_{name}_stalk", pts, mat([0.02, 0.05, 0.045], 0.5, life.EMERALD, 0.03,
                                                               opacity=0.85), 200.0)
        col = life.EMERALD if name == "hero" else life.TEAL
        hosts.setdefault("f3d", []).extend(add(f"f3d_{name}_cap", f"f3d_{name}_cap", pts,
                                               mat([0.02, 0.06, 0.06], 0.4, col, 0.32, program="cap",
                                                   doubleSided=True), 200.0))
        hosts.setdefault(f"f3d_{name}_emit", []).append(f"f3d_{name}_cap_0")
    return N, hosts


def layer(name, hosts, template, color, intensity, **kw):
    d = {"name": name, "hosts": hosts, "template": f"../meshes/{template}", "color": color, "intensity": intensity,
         "excitedColor": color, "excitedIntensity": 0.0, "size": 1.0, "breath": 0.25, "breathRate": 0.06,
         "flicker": 0.15, "flickerRate": 2.0, "sparsity": 0.0, "maxDistance": 300.0}
    d.update(kw)
    return d


def ecosystem(hosts, forest_hosts, shrub_hosts, veil_hosts, water_level):
    """The emitter layers. Tier 1 subtle, tier 2 active, tier 3 hero (see the module docstring)."""
    mirror = {"mirror": 1.0, "mirrorLevel": round(water_level, 3), "mirrorStretch": 0.5}
    L = []

    def per_variant(name, prefix, hosts_list, tmpl_fmt, color, intensity, n, **kw):
        for v in range(n):
            hs = [h for h in hosts_list if h.startswith(f"{prefix}{v}_")]
            if hs:
                L.append(layer(f"{name}{v}", hs, tmpl_fmt.format(v=v), color, intensity, **kw))

    # TIER 2: stalk tips. Green in most tufts, cyan in some (alternate variants), the glades the same at range.
    for v in range(N_TUFT):
        col = life.LIME if v in (0, 2) else (life.EMERALD if v in (1, 4) else life.CYAN)
        hs = [h for h in hosts.get("tuft", []) if h.startswith(f"tuft{v}_")]
        gs = [h for h in hosts.get("glade", []) if h.startswith(f"glade{v}_")]
        if hs:
            L.append(layer(f"tips{v}", hs, f"tuft{v}.emit.json", col, 2.4, maxDistance=900.0, sparsity=0.15,
                           pulseRate=1.5, pulseDecay=0.8, **mirror))
        if gs:
            L.append(layer(f"glade{v}", gs, f"tuft{v}.emit.json", col, 2.0, size=3.5, maxDistance=5200.0, sparsity=0.3,
                           pulseRate=1.0, pulseDecay=0.8))
    # TIER 1: moss dots, blue on half the cushions, green on the other half
    for k, col in ((0, life.BLUE), (1, life.TEAL)):
        hs = hosts.get(f"moss{k}", [])
        if hs:
            L.append(layer(f"mossDots{k}", hs, f"moss{k}.emit.json", col, 1.1, sparsity=0.35, maxDistance=180.0,
                           flicker=0.3, flickerRate=1.3, **mirror))
    if hosts.get("reeds"):
        L.append(layer("reedTips", hosts["reeds"], "reeds0.emit.json", life.EMERALD, 2.2, maxDistance=600.0,
                       **mirror))
    if hosts.get("lattice"):
        L.append(layer("latticeGlow", hosts["lattice"], "lattice0.emit.json", life.VIOLET, 1.8, maxDistance=60.0))
    for name, col, inten in (("hero", life.LIME, 5.0), ("bank", life.CYAN, 3.0), ("small", life.TEAL, 2.0)):
        hs = hosts.get(f"f3d_{name}_emit", [])
        if hs:
            L.append(layer(f"gills_{name}", hs, f"f3d_{name}.emit.json", col, inten, maxDistance=400.0, **mirror))
    # the macro's foreground orbs: near emitters draw as soft discs, and out of focus they become bokeh
    for v, col in ((0, life.BLUE), (1, life.EMERALD), (2, life.CYAN)):
        hs = hosts.get(f"bulbs{v}", [])
        if hs:
            L.append(layer(f"orbs{v}", hs, f"bulbs{v}.emit.json", col, 3.0, maxDistance=40.0, flicker=0.1))
    # TIER 3: the veil fungus's beads
    if veil_hosts:
        L.append(layer("veilBeads", veil_hosts, "veil.emit.json", life.PALE, 7.0, maxDistance=900.0, flicker=0.25,
                       flickerRate=0.7, **mirror))
    # the forest: crown specks everywhere (tier 1); understorey glow on the clustered third (tier 2)
    for v in range(len(forest_hosts["all"])):
        if forest_hosts["all"][v]:
            L.append(layer(f"crown{v}", forest_hosts["all"][v], f"grove{v}_crown.emit.json", life.CYAN, 1.0,
                           sparsity=0.55, maxDistance=5200.0))
    glow = [h for v in range(len(forest_hosts["glow"])) for h in forest_hosts["glow"][v]]
    if glow:
        half = len(glow) // 2
        L.append(layer("understoryGreen", glow[:half] or glow, "grove0_under.emit.json", life.LIME, 5.0, size=2.0,
                       maxDistance=5200.0, sparsity=0.2, pulseRate=0.8))
        L.append(layer("understoryCyan", glow[half:] or glow, "grove1_under.emit.json", life.CYAN, 4.5, size=2.0,
                       maxDistance=5200.0, sparsity=0.2, pulseRate=0.8))
    if shrub_hosts:
        L.append(layer("ledgeSpecks", shrub_hosts, "shrub0.emit.json", life.TEAL, 1.0, sparsity=0.5,
                       maxDistance=5200.0))
    assert len(L) <= 32, len(L)
    return {"enabled": True, "spriteRadius": 1.5, "maxSprites": 65536, "layers": L}
