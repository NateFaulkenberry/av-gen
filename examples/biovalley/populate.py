"""CP2: hero vegetation, placed by ecological rules on the engine's own heights.

  forest     groves of canopy trees on the floor: dense near water and in hollows, thinning on slopes, broken by
             clearings (a low-frequency mask) and kept out of the river, the meadow and the shots' sight lines
  ledges     shrubs and small trees on the mountains' flat ledges and crowns (from each massif's heightfield)
  framing    the vista's hero tree and the dark FG masses on the promontory; the meadow's arched trees; the
             riverbank's woods
  boulders   dark rocks on the riverbank and in the meadow's foreground

Every rule is deterministic (fixed seeds); randomness is bounded and spatially coherent (noise masks), never uniform.
"""
from __future__ import annotations

import math

import numpy as np

import flora
import land
import massifs
from geom import fbm2, quat_y

N_GROVES = 6
CANOPY_ASSET = True   # the floor forest and the vista's framing tree come from the hero library
N_SHRUBS = 4
N_BOULDERS = 6
N_ARCH = 3

BARK = [0.022, 0.02, 0.024]
LEAF = [0.016, 0.04, 0.036]
LEAF_FAR = [0.02, 0.045, 0.045]
STONE = [0.03, 0.033, 0.042]


def build(out):
    tris = 0
    for k in range(N_GROVES):
        # the far forest: a light build (clumps of 80 triangles, five-sided limbs) -- it is most of the valley's
        # triangles, and from the vista one grove is a few pixels; the near forest (grovehq) keeps the full build
        w, l = flora.grove(700 + k, leaf_level=1, sides=5)
        tris += w.write(out / f"grove{k}_wood.glb", f"grove{k}_wood")
        tris += l.write(out / f"grove{k}_leaf.glb", f"grove{k}_leaf")
        w, l = flora.grove(700 + k)
        tris += w.write(out / f"grovehq{k}_wood.glb", f"grovehq{k}_wood")
        tris += l.write(out / f"grovehq{k}_leaf.glb", f"grovehq{k}_leaf")
    for k in range(N_SHRUBS):
        t = flora.tree("shrub", 800 + k, 4.0)
        tris += t.wood.write(out / f"shrub{k}_wood.glb", f"shrub{k}_wood")
        tris += t.leaf.write(out / f"shrub{k}_leaf.glb", f"shrub{k}_leaf")
    for k in range(N_ARCH):
        t = flora.tree("arch", 900 + k, 17.0)
        tris += t.wood.write(out / f"arch{k}_wood.glb", f"arch{k}_wood")
        tris += t.leaf.write(out / f"arch{k}_leaf.glb", f"arch{k}_leaf")
    t = flora.tree("hero", 7, 30.0)
    tris += t.wood.write(out / "hero_wood.glb", "hero_wood")
    tris += t.leaf.write(out / "hero_leaf.glb", "hero_leaf")
    for k in range(N_BOULDERS):
        tris += flora.boulder(950 + k, 1.0).write(out / f"boulder{k}.glb", f"boulder{k}")
    return tris


FUNGI = {"veil": (31, 3.6, 1.7, "veil"), "parasol0": (32, 0.35, 0.16, "parasol"), "parasol1": (33, 0.4, 0.2, "parasol"),
         "parasol2": (34, 0.3, 0.13, "parasol")}


def build_fungi(out):
    tris = 0
    for name, (seed, h, cr, style) in FUNGI.items():
        st, cap, th = flora.fungus(seed, h, cr, style)
        tris += st.write(out / f"fungus_{name}_stalk.glb", "stalk") + cap.write(out / f"fungus_{name}_cap.glb", "cap")
        if th.P:
            tris += th.write(out / f"fungus_{name}_threads.glb", "threads")
    return tris


def fungi(gfine, eyes):
    """Hero fungi: the riverbank's tall veil fungus on the near left bank and parasols along both banks; a colony of
    parasols in front of the macro lens."""
    r = np.random.default_rng(14)
    out = {n: [] for n in FUNGI}
    g = gfine["bank"]

    def at(x, z):
        return float(max(g.height(np.array([x]), np.array([z]))[0], -1e9))

    bx, bz = eyes["riverbank"]
    # the veil: 13 m ahead, left of the stream (north bank), leaning over the water
    x = bx - 19.0
    zs = np.arange(land.BANK_Z, land.BANK_Z + 25, 0.5)
    hs = np.array([at(x, zz) for zz in zs])
    z = float(zs[np.argmax(hs > 3.6)]) + 1.5                 # the first dry ground north of the stream
    import ecosys
    if not ecosys.LIBRARY:                                   # the library's veilcap is the bank's hero now
        out["veil"].append(rec(x, at(x, z) - 0.1, z, 0.3, 1.0))
    return out


def fungus_nodes(groups, mat):
    out = []
    for name, pts in groups.items():
        if not pts:
            continue
        lod = {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": 400.0}
        parts = [("stalk", mat([0.03, 0.08, 0.06], 0.5, [0.15, 0.85, 0.5], 0.12, opacity=0.85)),
                 ("cap", mat([0.02, 0.06, 0.07], 0.4, [0.2, 1.0, 0.8], 0.35, doubleSided=True, program="cap"))]
        if name == "veil":
            parts.append(("threads", mat([0.02, 0.04, 0.06], 0.4, [0.3, 0.75, 1.0], 0.35, doubleSided=True,
                                         opacity=0.55)))
        for part, m in parts:
            out.append({"name": f"fungus_{name}_{part}", "kind": "procedural", "procedural": {
                "source": {"kind": "mesh", "asset": f"../meshes/fungus_{name}_{part}.glb"},
                "distribution": {"kind": "points", "points": pts}, "castsShadow": False, "lod": lod,
                "material": m}})
    return out


def rec(x, y, z, yaw, s, sy=None):
    return [round(float(x), 2), round(float(y), 2), round(float(z), 2), *[round(v, 5) for v in quat_y(yaw)],
            round(float(s), 3), round(float(sy if sy is not None else s), 3), round(float(s), 3)]


def _jgrid(r, x0, x1, z0, z1, step):
    xs = np.arange(x0, x1, step)
    zs = np.arange(z0, z1, step)
    X, Z = np.meshgrid(xs, zs)
    X = X.ravel() + r.uniform(0, step, X.size)
    Z = Z.ravel() + r.uniform(0, step, Z.size)
    return X, Z


def forest(gfloor, vista_eye, vista_yaw, shot_eyes):
    """Groves on the floor and the lower slopes."""
    r = np.random.default_rng(11)
    X, Z = _jgrid(r, gfloor.x0 + 10, gfloor.x0 + gfloor.h.shape[1] * gfloor.step - 10,
                  gfloor.z0 + 10, gfloor.z0 + gfloor.h.shape[0] * gfloor.step - 10, 21.0)
    H = gfloor.height(X, Z)
    fl = np.array([land.floor_level(z) for z in Z])
    rel = H - fl
    sl = gfloor.slope(X, Z)
    wet = gfloor.wet(X, Z)
    for dx, dz in ((9, 0), (-9, 0), (0, 9), (0, -9)):
        wet |= gfloor.wet(X + dx, Z + dz)
    # habitat: low ground, gentle slopes; wetter (near the river: lower and flatter) is denser
    habitat = np.clip(1.0 - rel / 260.0, 0.15, 1) * np.clip(1.0 - sl / 1.4, 0, 1)
    groves = fbm2(X / 260.0, Z / 260.0, 21, 3)                 # clearings and groves at two scales
    patches = fbm2(X / 70.0, Z / 70.0, 22, 2)
    p = habitat * (0.35 + 1.1 * (groves - 0.42)) + 0.25 * (patches - 0.5)
    keep = (~wet) & (p > 0.22) & (r.random(len(X)) < 0.94)
    # sight lines and clearings
    mx, mz = land.MEADOW_X, land.MEADOW_Z + 60
    keep &= np.hypot(X - mx, Z - mz) > 200
    bx, bz = land.BANK_X0 + 120, land.BANK_Z
    keep &= ~((np.abs(Z - bz) < 22) & (X > land.BANK_X0 + 30) & (X < land.BANK_X0 + 260))     # the stream's corridor
    for ex, ez in shot_eyes:
        keep &= np.hypot(X - ex, Z - ez) > 18
    # the meadow's view to the glow: an open run of low ground right of its centre, out to 1.6 km
    mex, mez = land.MEADOW_X + 4, land.MEADOW_Z - 30
    brg = np.degrees(np.arctan2(X - mex, Z - mez))
    dm = np.hypot(X - mex, Z - mez)
    keep &= ~((brg > -14.0) & (brg < 6.0) & (dm < 1600.0) & (r.random(len(X)) < 0.85))
    X, Z, H = X[keep], Z[keep], H[keep]
    out = {k: [] for k in range(N_GROVES)}
    for k in range(len(X)):
        v = int(r.integers(0, N_GROVES))
        thick = float(patches[keep][k]) if False else 1.0
        out[v].append(rec(X[k], H[k] - 0.4, Z[k], r.uniform(0, 6.283), r.uniform(0.85, 1.35), r.uniform(0.85, 1.35)))
    print(f"  forest: {len(X)} groves")
    return out


def ledges(massif_points, out_dir):
    """Shrubs and small trees on every massif's ledges and crowns."""
    r = np.random.default_rng(12)
    out = {k: [] for k in range(N_SHRUBS)}
    n = 0
    for name, recs in massif_points.items():
        if not recs:
            continue
        F = massifs.Field(out_dir, name)
        L = F.ledges
        if len(L) == 0:
            continue
        for rc in recs:
            s = rc[7]
            pick = L[r.choice(len(L), min(len(L), 70), replace=False)]
            yaw = 2.0 * math.atan2(rc[4], rc[6])
            c, sn = math.cos(yaw), math.sin(yaw)
            for p in pick:
                px, py, pz = p * s
                wx = rc[0] + c * px + sn * pz
                wz = rc[2] - sn * px + c * pz
                v = int(r.integers(0, N_SHRUBS))
                sc = r.uniform(1.6, 4.5) * (0.6 + 0.4 * s)            # 6-18 m: small trees at mountain scale
                out[v].append(rec(wx, rc[1] + py - 0.3, wz, r.uniform(0, 6.283), sc))
                n += 1
    print(f"  ledge vegetation: {n}")
    return out


def framing(gc, gfine, vista_eye, vista_yaw):
    """The hand-placed framing: the vista's hero tree and FG masses, the meadow's arched trees, bank boulders."""
    r = np.random.default_rng(13)
    hero, shrubs, arch, boulders = [], {k: [] for k in range(N_SHRUBS)}, {k: [] for k in range(N_ARCH)}, \
        {k: [] for k in range(N_BOULDERS)}
    ex, ey, ez = vista_eye

    def polar(b_deg, dist):
        b = math.radians(vista_yaw + b_deg)
        return ex + dist * math.sin(b), ez + dist * math.cos(b)

    # VISTA: the hero tree left of frame, below the eye so its crown frames the left edge and the upper left
    hx, hz = polar(33.0, 46.0)
    hy = float(gc.height(np.array([hx]), np.array([hz]))[0])
    hero.append(rec(hx, hy - 1.0, hz, math.radians(200.0), 1.3))
    # a second, nearer, larger: its trunk and limbs cut the frame's left edge and corner (panel 2's big dark tree)
    hx, hz = polar(50.0, 26.0)
    hy = float(gc.height(np.array([hx]), np.array([hz]))[0])
    hero.append(rec(hx, hy - 4.0, hz, math.radians(120.0), 1.35))
    # dark FG masses on the promontory's lip: shrubs (scaled to small trees) across the bottom of frame
    for k in range(90):
        b = r.uniform(-36, 36)
        d = r.uniform(18, 110) if r.random() < 0.6 else r.uniform(18, 45)
        x, z = polar(b, d)
        y = float(gc.height(np.array([x]), np.array([z]))[0])
        shrubs[int(r.integers(0, N_SHRUBS))].append(rec(x, y - 0.3, z, r.uniform(0, 6.28), r.uniform(2.5, 5.5)))

    # MEADOW: arched trees on the left of its horizon, a pair of them overlapping (panel 4)
    g = gfine["meadow"]
    for k, (dx, dz, s) in enumerate(((-150, 230, 1.15), (-120, 255, 0.95), (-75, 300, 0.8), (-200, 210, 1.3),
                                     (40, 330, 0.7))):
        x, z = land.MEADOW_X + dx, land.MEADOW_Z + dz
        y = float(g.height(np.array([x]), np.array([z]))[0])
        arch[k % N_ARCH].append(rec(x, y - 0.3, z, r.uniform(0, 6.28), s))
    # meadow FG rocks, low and dark (panel 4's bottom)
    mx, mz = land.MEADOW_X + 4, land.MEADOW_Z - 30
    for k in range(18):
        a = r.uniform(-1.2, 1.2) + math.radians(12.0)
        d = r.uniform(3, 18)
        x, z = mx + d * math.sin(a), mz + d * math.cos(a)
        y = float(g.height(np.array([x]), np.array([z]))[0])
        boulders[int(r.integers(0, N_BOULDERS))].append(rec(x, y - 0.15, z, r.uniform(0, 6.28), r.uniform(0.4, 1.4)))

    # RIVERBANK: boulders along both banks of the calm reach, larger near the camera, a few in the water
    g = gfine["bank"]
    for k in range(70):
        t = r.uniform(0, 1)
        x = land.BANK_X0 + 50 + t * 150
        side = 1 if r.random() < 0.5 else -1
        z = land.BANK_Z + side * r.uniform(4.0, 9.0) + (2 - 4 * t)
        y = float(g.height(np.array([x]), np.array([z]))[0])
        s = r.uniform(0.5, 2.2) * (1.4 - 0.6 * t)
        boulders[int(r.integers(0, N_BOULDERS))].append(rec(x, y - 0.25 * s, z, r.uniform(0, 6.28), s))
    return hero, shrubs, arch, boulders


def nodes(groups, mat):
    """Scene nodes for the placed vegetation. `groups` = {species prefix: {variant: records}}."""
    out = []
    lod_grove = {"cull": True, "count": 4, "byScreenSize": False, "maxDistance": 5200.0,
                 "distance1": 70.0, "distance2": 200.0, "distance3": 480.0}
    lod_small = {"cull": True, "count": 3, "byScreenSize": False, "maxDistance": 4800.0,
                 "distance1": 120.0, "distance2": 500.0}

    def chunks(lst, n=60000):
        return [lst[i:i + n] for i in range(0, len(lst), n)]

    def add(name, glb, pts, material, lod):
        for i, part in enumerate(chunks(pts)):
            out.append({"name": f"{name}{i}" if i else name, "kind": "procedural", "procedural": {
                "source": {"kind": "mesh", "asset": f"../meshes/{glb}.glb"},
                "distribution": {"kind": "points", "points": part}, "castsShadow": False, "lod": lod,
                "material": material}})

    for prefix, variants in groups.items():
        mesh = {"groveglow": "grove", "grovedark": "grove", "grovehqglow": "grovehq",
                "grovehqdark": "grovehq"}.get(prefix, prefix)
        for v, pts in variants.items():
            if not pts:
                continue
            if prefix == "boulder":
                add(f"boulder{v}", f"boulder{v}", pts, mat(STONE, 0.75, program="rock"), lod_small)
                continue
            lod = lod_grove if mesh in ("grove", "grovehq") else lod_small
            if prefix == "hero":
                lod = {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": 0.0}
            if CANOPY_ASSET and mesh in ("grove", "grovehq"):
                # the asset artist's canopy grove (painted self-shadowing crowns), its own materials, dialled
                for i, part in enumerate(chunks(pts)):
                    out.append({"name": f"{prefix}{v}_leaf{i}" if i else f"{prefix}{v}_leaf", "kind": "procedural",
                                "procedural": {"source": {"kind": "mesh",
                                                          "asset": "../meshes/hero/hero_canopygrove_lod1.glb"},
                                               "distribution": {"kind": "points", "points": part},
                                               "castsShadow": False, "lod": lod,
                                               "material": {"baseColor": [0.8, 0.85, 0.9], "roughness": 1.0,
                                                            "metallic": 0.0, "emissiveColor": [1.0, 1.0, 1.0],
                                                            "emissiveIntensity": 1.6}}})
                continue
            if CANOPY_ASSET and prefix == "hero":
                # the framing tree is the artist's shadowbough
                for i, part in enumerate(chunks(pts)):
                    out.append({"name": "hero0_leaf", "kind": "procedural", "procedural": {
                        "source": {"kind": "mesh", "asset": "../meshes/hero/hero_shadowbough_lod0.glb"},
                        "distribution": {"kind": "points", "points": part}, "castsShadow": False, "lod": lod,
                        "material": {"baseColor": [1.0, 1.0, 1.0], "roughness": 1.0, "metallic": 0.0,
                                     "emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": 2.0}}})
                continue
            add(f"{prefix}{v}_wood", f"{mesh}{v}_wood" if prefix != "hero" else "hero_wood", pts, mat(BARK, 0.85), lod)
            add(f"{prefix}{v}_leaf", f"{mesh}{v}_leaf" if prefix != "hero" else "hero_leaf", pts,
                mat(LEAF, 0.7, program="crown"), lod)
    return out
