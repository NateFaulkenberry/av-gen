"""Celestial and cloud pieces (07-cinematic-finishing-pass.md sections 5.2-5.3).

env_moon: a real sphere of unit radius (scale it in the scene), Z up in Blender (Y up exported); UV is
equirectangular.
  - Low-frequency albedo: a few large, soft, dark maria basins (merged blobs); brighter, faintly rugged highlands;
    two or three big, soft-rimmed craters; one faint ray crater. Stylised, not a photograph.
  - Relief: a very gentle normal map from the same fields, enough to catch the terminator.
  - No emission and no crescent cut: the phase comes from the scene's light and the terminator. A faint earthshine
    can be added in the material.

Clouds (env_cloud_a, _b, _c): sculptural cloud masses built in Blender from hierarchical metaballs.
  - Structure: large lobes set the mass; medium billows grow on their upper surfaces; small billows on those.
    Every lobe is squashed to a flat base, then the surface is displaced by 3D noise for broken edges.
  - Painted from height and exposure: deep cool blue-violet interiors and undersides, paler upper and side
    surfaces, a faint warm cast low on the horizon side. It is a starting point the lead's lighting refines.
  - Each cloud also exports its lobe skeleton (env_cloud_*.lobes.json: [x, y, z, radius] in GLB space, Y up), so the
    same design can drive a density field.
  Sizes:
  - cloud_a: a towering bank, ~900 m wide, billowing up on one side (to frame the moon).
  - cloud_b: a long, low band, ~1400 m, lumpy above and ragged below.
  - cloud_c: a broken cluster of separate puffs and wisps, ~700 m.
"""
from __future__ import annotations

import json
import math
import os

import numpy as np

from lib import Asset, col, fbm, lerp, smoothstep, worley3
from rocks import noise3

TAU = 2 * math.pi


# ---- moon ---------------------------------------------------------------------------------------------------------

def moon_fields(x, y, z):
    """Albedo-darkening (maria), crater rims and relief from a unit direction (x, y, z)."""
    maria = np.zeros_like(x)
    # the designed face looks along Blender -Y (GLB +Z): the big maria sit there
    for cx, cy, cz, r in ((0.35, -0.8, 0.35, 0.55), (-0.25, -0.85, 0.15, 0.40), (0.1, -0.7, -0.45, 0.36),
                          (0.7, -0.5, -0.15, 0.30), (-0.6, -0.5, 0.5, 0.26), (0.6, 0.6, 0.3, 0.4), (-0.5, 0.7, -0.3, 0.35)):
        c = np.array([cx, cy, cz]) / np.linalg.norm([cx, cy, cz])
        d = np.arccos(np.clip(x * c[0] + y * c[1] + z * c[2], -1, 1))
        edge = 0.35 * (noise3(x, y, z, 1.6, 700, 3) - 0.5) + 0.12 * (noise3(x, y, z, 5.0, 703, 2) - 0.5)
        maria = np.maximum(maria, (1 - smoothstep(r * 0.35, r * 1.1, d + r * edge)) * (0.75 + 0.25 * noise3(x, y, z, 3.0, 704, 2)))
    rims = np.zeros_like(x)
    bowls = np.zeros_like(x)
    for cx, cy, cz, r in ((0.2, -0.6, 0.75, 0.16), (-0.6, -0.6, -0.4, 0.12), (0.5, -0.75, -0.6, 0.1)):
        c = np.array([cx, cy, cz]) / np.linalg.norm([cx, cy, cz])
        d = np.arccos(np.clip(x * c[0] + y * c[1] + z * c[2], -1, 1))
        rims = np.maximum(rims, 0.5 * np.exp(-((d - r) / (r * 0.25)) ** 2))
        bowls = np.maximum(bowls, 1 - smoothstep(0.0, r, d))
    # one young crater with soft rays
    c = np.array([-0.3, -0.9, -0.35]) / np.linalg.norm([-0.3, -0.9, -0.35])
    d = np.arccos(np.clip(x * c[0] + y * c[1] + z * c[2], -1, 1))
    ang = np.arctan2(y * c[0] - x * c[1], z - c[2] * (x * c[0] + y * c[1] + z * c[2]))
    rays = (1 - smoothstep(0.05, 0.5, d)) ** 2 * smoothstep(0.55, 0.95, 0.5 + 0.5 * np.sin(ang * 13 + 5 * noise3(x, y, z, 2.0, 701, 2))) * 0.6
    highland = noise3(x, y, z, 3.0, 702, 4)
    return maria, rims, bowls, rays, highland


def paint_moon(S, T):
    th = TAU * S
    ph = -math.pi / 2 + math.pi * T
    x, y, z = np.cos(ph) * np.cos(th), np.cos(ph) * np.sin(th), np.sin(ph)
    maria, rims, bowls, rays, highland = moon_fields(x, y, z)
    albedo = 0.62 + 0.14 * (highland - 0.5) - 0.36 * maria - 0.05 * bowls + 0.06 * rims + 0.10 * rays
    tint = lerp(np.array([0.92, 0.93, 1.0]), np.array([0.82, 0.86, 0.98]), maria)
    base = tint * np.clip(albedo, 0.15, 0.95)[..., None]
    height = 0.5 * highland - 0.4 * maria + 0.6 * rims - 0.5 * bowls
    return dict(base=base, emis=np.zeros(S.shape + (3,)), height=height, rough=0.95, ao=1.0)


def make_moon(lod=0):
    A = Asset("env_moon")
    res = 1.0 if lod == 0 else 0.5
    sl = A.slot("moon", res=(int(2048 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                emissive_strength=1.0)
    nth, nph = (192, 96) if lod == 0 else (64, 32)
    s = np.linspace(0, 1, nth, endpoint=False)
    t = np.linspace(0, 1, nph)
    S, T = np.meshgrid(s, t)
    th = TAU * S
    ph = -math.pi / 2 + math.pi * T
    x, y, z = np.cos(ph) * np.cos(th), np.cos(ph) * np.sin(th), np.sin(ph)
    maria, rims, bowls, rays, highland = moon_fields(x, y, z)
    r = 1.0 + 0.0015 * (rims - bowls) + 0.002 * (highland - 0.5)  # barely there: the silhouette stays round
    P = np.stack([x * r, y * r, z * r], -1)
    # counter-clockwise u with rows rising faces outward
    sl.add_grid(P, sl.region(0, 1, paint_moon, bump=2.5), wrap_u=True)
    return A


# ---- clouds -------------------------------------------------------------------------------------------------------

CLOUDS = {
    # name: (seed, big-lobe path [(x, y, z, r)], billow levels, base height)
    "env_cloud_a": (800, [(-380, 0, 60, 120), (-220, 20, 70, 150), (-60, -10, 110, 170), (90, 10, 180, 160),
                          (200, -20, 260, 130), (300, 0, 340, 95), (380, 10, 260, 90)], 3, 0.0),
    "env_cloud_b": (810, [(x, 30 * math.sin(x / 200), 40 + 20 * math.sin(x / 130), 115 + 35 * math.cos(x / 170))
                          for x in range(-700, 701, 110)], 2, 0.0),
    "env_cloud_c": (820, [(-300, 0, 30, 70), (-170, 40, 50, 55), (-20, -30, 40, 80), (140, 20, 70, 60),
                          (260, -10, 30, 45), (340, 30, 55, 35)], 1, 0.0),
}


def cloud_lobes(name):
    seed, path, levels, base = CLOUDS[name]
    rng = np.random.default_rng(seed)
    lobes = [(np.array(p[:3], dtype=float), float(p[3])) for p in path]
    level = lobes
    for lv in range(levels):
        nxt = []
        for c, r in level:
            for _ in range(rng.integers(5, 9) if lv == 0 else rng.integers(3, 6)):
                # billows grow on the upper and outer surface
                a = rng.uniform(0, TAU)
                el = rng.uniform(0.15, 1.2)
                d = np.array([math.cos(a) * math.cos(el), math.sin(a) * math.cos(el) * 0.6, math.sin(el)])
                rr = r * rng.uniform(0.45, 0.66)
                nxt.append((c + d * r * rng.uniform(0.42, 0.7), rr))
        lobes += nxt
        level = nxt
    return lobes, base


def cloud_paint(V, zmin, zmax):
    def paint(S, T):
        # S, T: a front projection (u across x, v up z); light lives on the upper part
        up = smoothstep(0.0, 1.0, T)
        n1 = fbm(S * 8, T * 6, 4, 830)
        n2 = fbm(S * 30, T * 20, 3, 831)
        lit = smoothstep(0.35, 0.95, up * 0.8 + 0.4 * (n1 - 0.5) + 0.15)
        deep = np.array([0.035, 0.04, 0.09])
        mid = np.array([0.12, 0.12, 0.25])
        top = np.array([0.38, 0.36, 0.55])
        base = lerp(deep, mid, smoothstep(0.0, 0.5, up)) * (1 - lit)[..., None] + lerp(mid, top, lit) * lit[..., None]
        base = base * (0.85 + 0.3 * n2)[..., None]
        warm = (1 - smoothstep(0.0, 0.35, T)) * smoothstep(0.5, 0.8, n1)  # a faint warm cast low down
        base = base + col([0.08, 0.04, 0.03], warm)
        return dict(base=base, emis=col([0.05, 0.05, 0.12], 0.02 * lit), height=n2 + n1, rough=0.95, ao=0.5 + 0.5 * up)
    return paint


def make_cloud(name, lod=0):
    """Metaballs -> mesh in Blender, then flattened base, noise breakup, front-projected UVs and paint."""
    import bpy
    lobes, base = cloud_lobes(name)
    mb = bpy.data.metaballs.new(name + "_mb")
    span = max(abs(c[0]) + r for c, r in lobes)
    mb.resolution = mb.render_resolution = (14.0 if lod == 0 else 36.0) * span / 700.0
    mb.threshold = 0.35
    for c, r in lobes:
        e = mb.elements.new()
        e.type = "ELLIPSOID"
        e.co = (float(c[0]), float(c[1]), float(c[2]))
        e.radius = float(r)
        e.size_x, e.size_y, e.size_z = 1.0, 0.8, 0.75
    ob = bpy.data.objects.new(name + "_mbo", mb)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = ob.evaluated_get(dg).to_mesh()
    me.calc_loop_triangles()
    V = np.array([v.co[:] for v in me.vertices], dtype=np.float64)
    F = np.array([t.vertices[:] for t in me.loop_triangles], dtype=np.int64)
    ob.evaluated_get(dg).to_mesh_clear()
    bpy.data.objects.remove(ob, do_unlink=True)
    bpy.data.metaballs.remove(mb)
    # flat-ish bases, then broken edges: displace along the normal by multi-scale noise
    from lib import normalize
    def vnormals(V, F):
        fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
        N = np.zeros_like(V)
        for k in range(3):
            np.add.at(N, F[:, k], fn)
        return normalize(N)
    zmin = V[:, 2].min()
    floor = zmin + 0.12 * (V[:, 2].max() - zmin)
    V[:, 2] = np.where(V[:, 2] < floor, floor - 0.25 * (floor - V[:, 2]), V[:, 2])
    N = vnormals(V, F)
    x, y, z = V[:, 0], V[:, 1], V[:, 2]
    disp = (34.0 * (noise3(x, y, z, 0.006, 840, 3) - 0.5) + 14.0 * (noise3(x, y, z, 0.025, 841, 2) - 0.5)
            + 5.0 * (noise3(x, y, z, 0.09, 842, 2) - 0.5)) * (0.4 + 0.6 * np.clip(N[:, 2] + 0.3, 0, 1))
    V = V + N * disp[:, None]
    N = vnormals(V, F)
    lo, hi = V.min(0), V.max(0)
    U = np.stack([(V[:, 0] - lo[0]) / (hi[0] - lo[0]), (V[:, 2] - lo[2]) / (hi[2] - lo[2])], -1)
    A = Asset(name)
    res = 1.0 if lod == 0 else 0.5
    sl = A.slot("cloud", res=(int(1024 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                emissive_strength=1.0)
    sl.region(0, 1, cloud_paint(V, lo[2], hi[2]), bump=2.0)
    du, dv = 0.5 / sl.W, 0.5 / sl.H
    sl.add_mesh(V, F, U * np.array([1 - 2 * du, 1 - 2 * dv]) + np.array([du, dv]), N)
    A.extra_files = {f"{name}.lobes.json": {"lobes": [[round(float(c[0]), 2), round(float(c[2]), 2),
                                                      round(float(-c[1]), 2), round(float(r), 2)] for c, r in lobes]}}
    return A


def make_cloud_a(lod=0):
    return make_cloud("env_cloud_a", lod)


def make_cloud_b(lod=0):
    return make_cloud("env_cloud_b", lod)


def make_cloud_c(lod=0):
    return make_cloud("env_cloud_c", lod)


# ---- vista cloud layout (for the lead's blob-driven cloud renderer) ------------------------------------------------
#
# World frame of the vista (metres, Y up, as the scene files). The camera and the moon come from the lead's scene:
# eye (127.74, 320.36, -1090.0); MOON_DIR = _dir(-12, 9.5) from valley.py (yaw from +z toward +x, pitch up).

VISTA_EYE = np.array([127.74, 320.36, -1090.0])


def _dir(yaw_deg, pitch_deg):
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    return np.array([math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p)])


MOON_DIR = _dir(-12.0, 9.5)


def at(bearing, dist, height):
    """A world point at a bearing (deg, as MOON_DIR's yaw) and horizontal distance from the vista eye, at a world
    height."""
    b = math.radians(bearing)
    return np.array([VISTA_EYE[0] + dist * math.sin(b), height, VISTA_EYE[2] + dist * math.cos(b)])


def mass(rng, spine, r0, levels=3, flat=0.55, edge_lobes=40, density=1.0):
    """A cloud mass: big lobes along a spine (list of world points and radii), billows grown on their upper and outer
    surfaces, and a fringe of small, soft lobes at the edges. Returns lobe dicts."""
    lobes = [dict(c=np.asarray(p, float), r=float(r), scale=[1.0, flat, 1.0], density=density, soft=0.35)
             for p, r in spine]
    level = lobes
    for lv in range(levels):
        nxt = []
        for L in level:
            for _ in range(rng.integers(4, 7) if lv == 0 else rng.integers(2, 4)):
                a = rng.uniform(0, TAU)
                el = rng.uniform(0.1, 1.2)
                d = np.array([math.cos(a) * math.cos(el), math.sin(el) * 0.9, math.sin(a) * math.cos(el)])
                rr = L["r"] * rng.uniform(0.42, 0.62)
                nxt.append(dict(c=L["c"] + d * L["r"] * rng.uniform(0.45, 0.75), r=rr,
                                scale=[1.0, rng.uniform(0.7, 0.9), 1.0], density=density * rng.uniform(0.8, 1.0),
                                soft=0.4 + 0.1 * lv))
        lobes += nxt
        level = nxt
    # the fringe: many small, soft, thin lobes scattered just outside the mass's surface
    allc = np.array([L["c"] for L in lobes])
    allr = np.array([L["r"] for L in lobes])
    for _ in range(edge_lobes):
        i = rng.integers(len(allc))
        d = rng.normal(size=3)
        d[1] = abs(d[1]) * 0.5 - 0.15
        d /= np.linalg.norm(d)
        lobes.append(dict(c=allc[i] + d * allr[i] * rng.uniform(0.9, 1.25), r=float(allr[i] * rng.uniform(0.15, 0.3)),
                          scale=[1.0, 0.6, 1.0], density=density * rng.uniform(0.3, 0.6), soft=0.8))
    return lobes


VISTA_TGT = np.array([196.73, 172.55, -103.39])
VISTA_FOV = 44.0  # vertical, degrees; checked: MOON_DIR projects to (1358, 96) on a 1920x1080 frame


def unproject(sx, sy, dist, W=1920, H=1080):
    """The world point at screen pixel (sx, sy) of the vista camera, `dist` metres along the view axis; and the
    metres per pixel there."""
    f = VISTA_TGT - VISTA_EYE
    f = f / np.linalg.norm(f)
    r = np.cross(f, [0, 1.0, 0])
    r = r / np.linalg.norm(r)
    u = np.cross(r, f)
    t = math.tan(math.radians(VISTA_FOV) / 2)
    x = (sx - W / 2) / (W / 2) * t * W / H
    y = (H / 2 - sy) / (H / 2) * t
    return VISTA_EYE + dist * (f + x * r + y * u), dist * 2 * t / H


def vista_clouds():
    """Masses framing the moon (screen (1358, 96) in the vista): a layered bank below-left of it, behind the far
    peaks; a towering mass rising on its right; a thin broken veil across the top; and a low band of small masses at
    the horizon glow. Placed by screen position and distance, so the composition holds for this camera."""
    rng = np.random.default_rng(850)
    out = {}

    def spine(xs, ys, d, rpx):
        pts = []
        for x, y, r in zip(xs, ys, rpx):
            p, mpp = unproject(x, y, d)
            pts.append((p, r * mpp))
        return pts
    out["bank_left"] = mass(rng, spine([600, 720, 840, 960, 1070, 1160], [228, 212, 200, 192, 196, 210], 7000,
                                       [55, 70, 80, 78, 62, 45]), 0, levels=3, edge_lobes=80)
    out["tower_right"] = mass(rng, spine([1500, 1515, 1535, 1555, 1575, 1590], [190, 150, 112, 78, 46, 18], 8000,
                                         [62, 60, 56, 50, 42, 32]), 0, levels=3, flat=0.7, edge_lobes=80)
    xs = list(np.linspace(860, 1880, 18))
    out["veil_upper"] = mass(rng, spine(xs, [22 + 9 * math.sin(i * 0.9) for i in range(18)], 9500, [30] * 18), 0,
                             levels=2, flat=0.35, edge_lobes=60, density=0.5)
    band = []
    for x in np.linspace(640, 1700, 9):
        y = 262 + rng.uniform(-6, 8)
        band += mass(rng, spine([x, x + 22], [y, y - 6], 11000 + rng.uniform(-800, 800),
                                [rng.uniform(18, 30), rng.uniform(14, 24)]), 0, levels=2, flat=0.45, edge_lobes=12,
                     density=0.8)
    out["horizon_band"] = band
    return out


def write_vista_clouds(path):
    out = vista_clouds()
    js = {"frame": "vista world frame: metres, Y up, as the scene files",
          "eye": VISTA_EYE.round(2).tolist(), "moon_dir": MOON_DIR.round(5).tolist(),
          "note": "per lobe: c = centre (m), r = radius (m), scale = per-axis multipliers (x, y, z), "
                  "density 0..1, soft = edge softness as a fraction of r",
          "masses": {k: [dict(c=[round(float(x), 1) for x in L["c"]], r=round(L["r"], 1),
                              scale=[round(float(s), 2) for s in L["scale"]], density=round(float(L["density"]), 2),
                              soft=round(float(L["soft"]), 2)) for L in v] for k, v in out.items()}}
    with open(path, "w") as f:
        json.dump(js, f, indent=0)
    return out


def make_vista_clouds(lod=0):
    """The same lobes as one world-space mesh (Y-up world converted to Blender Z-up): a silhouette fallback and a
    framing check. The node goes at the world origin with no rotation."""
    import bpy
    out = vista_clouds()
    A = Asset("env_vista_clouds")
    sl = A.slot("cloud", res=(1024, 512), mode="OPAQUE", double_sided=False, emissive_strength=1.0)
    sl.region(0, 1, cloud_paint(None, 0, 1), bump=2.0)
    from lib import normalize
    for name, lobes in out.items():
        mb = bpy.data.metaballs.new(name)
        mb.resolution = mb.render_resolution = 30.0 if lod == 0 else 80.0
        mb.threshold = 0.35
        for L in lobes:
            e = mb.elements.new()
            e.type = "ELLIPSOID"
            x, y, z = L["c"]
            e.co = (float(x), float(-z), float(y))  # world (x, y up, z) -> Blender (x, -z, y)
            e.radius = float(L["r"])
            e.size_x, e.size_y, e.size_z = float(L["scale"][0]), float(L["scale"][2]), float(L["scale"][1])
        ob = bpy.data.objects.new(name, mb)
        bpy.context.scene.collection.objects.link(ob)
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        me = ob.evaluated_get(dg).to_mesh()
        me.calc_loop_triangles()
        V = np.array([v.co[:] for v in me.vertices], dtype=np.float64)
        F = np.array([t.vertices[:] for t in me.loop_triangles], dtype=np.int64)
        ob.evaluated_get(dg).to_mesh_clear()
        bpy.data.objects.remove(ob, do_unlink=True)
        bpy.data.metaballs.remove(mb)
        if len(F) == 0:
            continue
        fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
        N = np.zeros_like(V)
        for k in range(3):
            np.add.at(N, F[:, k], fn)
        N = normalize(N)
        lo, hi = V.min(0), V.max(0)
        U = np.stack([(V[:, 0] - lo[0]) / max(hi[0] - lo[0], 1), (V[:, 2] - lo[2]) / max(hi[2] - lo[2], 1)], -1)
        sl.add_mesh(V, F, U * 0.998 + 0.001, N)
    A.extra_files = {"env_vista_clouds.lobes.json": None}
    return A
