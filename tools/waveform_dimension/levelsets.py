"""Waveform Dimension: the carrier field and its nested level-set sheets (build time, numpy).

The sculpture's large form is the standing interference of three long spherical waves from points far outside the
frame (the CARRIER, C(x)); its openings are where a second, shorter interference (the APERTURE, A(x)) falls below a
threshold. Both are exactly the engine's `wave` field kind with spherical geometry (amplitude sin(2 pi |x - p| /
wavelength)), so the live scene can read them as fields and the material can colour by them.

`level_sheet` extracts one level set C(x) = c as a mesh by naive surface nets (the engine's own meshing scheme,
docs/sdf.md), clipped to a soft region: every vertex carries q = min(aperture margin, bound margin) in metres-ish units in
its UV.x (the material cuts the sheet at q = 0 with sub-cell precision and traces the cut with light) and the level's
index in UV.y. Faces wholly outside the region are dropped.
"""
from __future__ import annotations

import math

import numpy as np

# (point, wavelength m, amplitude): the carrier's three long waves, from far outside the frame. One leads (its shells
# are the sculpture's large sheets, nested and roughly offset from one another); the other two are weaker, so they
# bend the shells into saddles and press neighbouring sheets together or apart -- uneven, legible spacing, the family
# relationship visible (Stage B, second pass: with three equal waves the sheets read as unrelated petals)
CARRIER = [
    ((-260.0, 40.0, -180.0), 150.0, 1.0),
    ((220.0, -60.0, -240.0), 128.0, 0.6),
    ((30.0, 200.0, 240.0), 172.0, 0.4),
]
# the aperture's two longer waves: where they cancel, a few large, smooth openings show the next sheet behind --
# deliberate openings off the frame's middle, never ragged holes everywhere, never one centred portal
APERTURE = [
    ((-60.0, -30.0, 70.0), 74.0, 1.0),
    ((80.0, 50.0, -40.0), 63.0, 0.7),
]
LEVELS = [-0.6, 0.0, 0.45, 0.9]            # four nested sheets, unevenly spaced
APERTURE_THRESHOLD = -0.65                 # a sheet exists where A > this (about 15% of the volume is open)
BOUND_RADII = (124.0, 76.0, 124.0)         # the sculpture's soft outer ellipsoid (m)
# the cut's quality (resonator-quality, Phase D): the corner rounding of region()'s smooth min, and the smallest piece
# of a sheet kept (m^2): smaller fragments are shards the cut left floating in an opening
CORNER = 8.0
MIN_PIECE = 400.0


def wave_sum(p, waves):
    out = np.zeros(p.shape[:-1])
    for c, lam, a in waves:
        out += a * np.sin(2.0 * math.pi / lam * np.linalg.norm(p - np.array(c), axis=-1))
    return out


def carrier(p):
    return wave_sum(p, CARRIER)


def aperture(p):
    return wave_sum(p, APERTURE)


def region(p):
    """> 0 inside the sculpture: the aperture's margin and the outer ellipsoid's, in comparable units (~metres)."""
    a = (aperture(p) - APERTURE_THRESHOLD) * 12.0
    r = np.linalg.norm(p / np.array(BOUND_RADII), axis=-1)
    b = (1.0 - r) * 240.0   # steep, so the outer edge's traced line stays as narrow as the openings'
    # a smooth minimum (resonator-quality): where an opening meets the outer bound, min() made a cusp -- a horn or a
    # torn flap at the sheet's edge; the polynomial smooth min rounds that corner over CORNER units and is min()
    # everywhere else
    h = np.maximum(CORNER - np.abs(a - b), 0.0) / CORNER
    return np.minimum(a, b) - h * h * CORNER * 0.25


def level_sheet(level, index, lo=(-125.0, -78.0, -125.0), hi=(125.0, 78.0, 125.0), cell=1.0, margin=2.5):
    """One level set of the carrier as (positions, normals, uvs, indices) float32/uint32, clipped to `region`."""
    lo, hi = np.array(lo), np.array(hi)
    n = np.ceil((hi - lo) / cell).astype(int)
    xs = [lo[a] + cell * np.arange(n[a] + 1) for a in range(3)]
    X, Y, Z = np.meshgrid(*xs, indexing="ij")
    P = np.stack([X, Y, Z], -1)
    f = carrier(P) - level
    s = f > 0.0
    # cells with a sign change (corners differ)
    c000 = s[:-1, :-1, :-1]
    any_ = np.zeros_like(c000)
    all_ = np.ones_like(c000)
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                v = s[dx:dx + n[0], dy:dy + n[1], dz:dz + n[2]]
                any_ |= v
                all_ &= v
    active = any_ & ~all_
    idx = -np.ones(active.shape, dtype=np.int64)
    cells = np.argwhere(active)
    idx[active] = np.arange(len(cells))
    # vertex = mean of the linear crossings on the cell's sign-changing edges
    acc = np.zeros((len(cells), 3))
    cnt = np.zeros(len(cells))
    ci, cj, ck = cells[:, 0], cells[:, 1], cells[:, 2]
    edges = []
    for a in range(3):
        for u in (0, 1):
            for v in (0, 1):
                o0 = [0, 0, 0]
                b, c = [x for x in range(3) if x != a]
                o0[b], o0[c] = u, v
                o1 = list(o0)
                o1[a] = 1
                edges.append((tuple(o0), tuple(o1)))
    for o0, o1 in edges:
        f0 = f[ci + o0[0], cj + o0[1], ck + o0[2]]
        f1 = f[ci + o1[0], cj + o1[1], ck + o1[2]]
        m = (f0 > 0) != (f1 > 0)
        t = np.where(m, f0 / np.where(m, f0 - f1, 1.0), 0.0)
        p0 = lo + cell * (np.stack([ci, cj, ck], -1) + np.array(o0))
        p1 = lo + cell * (np.stack([ci, cj, ck], -1) + np.array(o1))
        acc += np.where(m[:, None], p0 + (p1 - p0) * t[:, None], 0.0)
        cnt += m
    verts = acc / np.maximum(cnt, 1)[:, None]
    # quads: one per sign-changing lattice edge whose four surrounding cells exist
    quads = []
    for a in range(3):
        b, c = [x for x in range(3) if x != a]
        sl0 = [slice(None)] * 3
        sl1 = [slice(None)] * 3
        sl0[a] = slice(0, n[a])
        sl1[a] = slice(1, n[a] + 1)
        e0 = s[tuple(sl0)]
        e1 = s[tuple(sl1)]
        change = e0 != e1
        # the edge at lattice point (i, j, k) along a: its four cells are offset by -1/0 in b and c
        pts = np.argwhere(change)
        pts = pts[(pts[:, b] >= 1) & (pts[:, c] >= 1) & (pts[:, b] < n[b]) & (pts[:, c] < n[c])]
        if len(pts) == 0:
            continue
        def cell_at(db, dc):
            q = pts.copy()
            q[:, b] -= db
            q[:, c] -= dc
            return idx[q[:, 0], q[:, 1], q[:, 2]]
        v00, v10, v11, v01 = cell_at(1, 1), cell_at(0, 1), cell_at(0, 0), cell_at(1, 0)
        inside = e0[pts[:, 0], pts[:, 1], pts[:, 2]]
        quad = np.stack([v00, v10, v11, v01], 1)
        quad[~inside] = quad[~inside][:, ::-1]
        ok = (quad >= 0).all(1)
        quads.append(quad[ok])
    quads = np.concatenate(quads, 0)
    # clip: keep quads with any vertex inside the region's margin; drop unreferenced vertices
    q = region(verts)
    keep = (q[quads] > -margin).any(1)
    quads = quads[keep]
    used = np.unique(quads)
    remap = -np.ones(len(verts), dtype=np.int64)
    remap[used] = np.arange(len(used))
    verts, q = verts[used], q[used]
    quads = remap[quads]
    tri = np.concatenate([quads[:, [0, 1, 2]], quads[:, [0, 2, 3]]], 0)
    # normals from the carrier's gradient (smooth, exact), oriented with +gradient (toward higher C)
    e = 0.25 * cell
    g = np.stack([carrier(verts + np.array(d)) - carrier(verts - np.array(d)) for d in
                  ([e, 0, 0], [0, e, 0], [0, 0, e])], -1)
    nrm = g / (np.linalg.norm(g, axis=1, keepdims=True) + 1e-12)
    # winding consistent with the normal
    fn = np.cross(verts[tri[:, 1]] - verts[tri[:, 0]], verts[tri[:, 2]] - verts[tri[:, 0]])
    flip = (fn * nrm[tri[:, 0]]).sum(1) < 0
    tri[flip] = tri[flip][:, [0, 2, 1]]
    uv = np.stack([q, np.full(len(q), float(index))], 1)
    verts, nrm, uv, tri = clip_mesh(verts, nrm, uv, tri, region, level)
    verts, nrm, uv, tri = drop_fragments(verts, nrm, uv, tri)
    return (verts.astype(np.float32), nrm.astype(np.float32), uv.astype(np.float32),
            tri.reshape(-1).astype(np.uint32))


def _onto_surface(p, level, iters=3):
    """Newton steps along the carrier's gradient onto C(p) = level (the sheet itself)."""
    e = 0.05
    for _ in range(iters):
        g = np.stack([carrier(p + np.array(d)) - carrier(p - np.array(d)) for d in
                      ([e, 0, 0], [0, e, 0], [0, 0, e])], -1) / (2 * e)
        f = carrier(p) - level
        p = p - (f / np.maximum((g * g).sum(1), 1e-12))[:, None] * g
    return p


def drop_fragments(verts, nrm, uv, tri):
    """Drops connected pieces smaller than MIN_PIECE m^2: shards the cut left floating in an opening."""
    t = tri
    parent = np.arange(len(verts))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b, c in t:
        ra = find(a)
        parent[find(b)] = ra
        parent[find(c)] = ra
    lab = np.array([find(i) for i in range(len(verts))])
    area = np.linalg.norm(np.cross(verts[t[:, 1]] - verts[t[:, 0]], verts[t[:, 2]] - verts[t[:, 0]]), axis=1) * 0.5
    ids, inv = np.unique(lab[t[:, 0]], return_inverse=True)
    keep = np.bincount(inv, weights=area)[inv] >= MIN_PIECE
    t = t[keep]
    used = np.unique(t)
    remap = -np.ones(len(verts), dtype=np.int64)
    remap[used] = np.arange(len(used))
    return verts[used], nrm[used], uv[used], remap[t]


def clip_mesh(verts, nrm, uv, tri, cut=None, level=None):
    """Cuts the mesh exactly along q = 0 (q = uv.x): triangles wholly inside are kept, wholly outside dropped, and a
    crossing triangle is split where its edges cross the cut -- found by bisection on `cut` itself and put back on the
    sheet (resonator-quality: linear interpolation of q put the crossings off the curve, a kinked, notched edge),
    or at the interpolated crossings when no `cut` is given. The cut is geometry, so the sheets stay OPAQUE (a masked
    material's discard defeats the GPU's hidden-surface removal: five layered masked sheets cost 56 ms at 1080p with
    no field evaluated at all, Stage B profile)."""
    q = uv[:, 0]
    qt = q[tri]
    inside = qt >= 0.0
    n_in = inside.sum(1)
    out_tris = [tri[n_in == 3]]
    new_v, new_n, new_uv = [verts], [nrm], [uv]
    base = len(verts)

    def crossing(a, b):
        t = (q[a] / (q[a] - q[b]))[:, None]
        if cut is not None:
            # bisection on the cut along the edge (its sign is known at both ends), then onto the sheet
            lo, hi = np.zeros(len(a)), np.ones(len(a))
            side_a = q[a] >= 0.0
            for _ in range(12):
                mid = 0.5 * (lo + hi)
                same = (cut(verts[a] + (verts[b] - verts[a]) * mid[:, None]) >= 0.0) == side_a
                lo = np.where(same, mid, lo)
                hi = np.where(same, hi, mid)
            t = (0.5 * (lo + hi))[:, None]
        p = verts[a] + (verts[b] - verts[a]) * t
        if cut is not None:
            p = _onto_surface(p, level, 2)
        n = nrm[a] + (nrm[b] - nrm[a]) * t
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        w = uv[a] + (uv[b] - uv[a]) * t
        w[:, 0] = 0.0
        return p, n, w

    for count in (1, 2):
        sel = tri[n_in == count]
        if len(sel) == 0:
            continue
        ins = inside[n_in == count]
        # rotate each triangle so that its odd vertex (the lone inside one for count 1, the lone outside one for
        # count 2) comes first, keeping the winding
        odd = np.argmax(ins if count == 1 else ~ins, axis=1)
        r = np.stack([sel[np.arange(len(sel)), (odd + k) % 3] for k in range(3)], 1)
        a, b, c = r[:, 0], r[:, 1], r[:, 2]
        pab, nab, wab = crossing(a, b)
        pac, nac, wac = crossing(a, c)
        i_ab = base + np.arange(len(sel))
        i_ac = base + len(sel) + np.arange(len(sel))
        base += 2 * len(sel)
        new_v += [pab, pac]
        new_n += [nab, nac]
        new_uv += [wab, wac]
        if count == 1:   # a inside: one triangle a, ab, ac
            out_tris.append(np.stack([a, i_ab, i_ac], 1))
        else:            # a outside: the quad ab, b, c, ac
            out_tris.append(np.stack([i_ab, b, c], 1))
            out_tris.append(np.stack([i_ab, c, i_ac], 1))
    verts = np.concatenate(new_v, 0)
    nrm = np.concatenate(new_n, 0)
    uv = np.concatenate(new_uv, 0)
    tri = np.concatenate(out_tris, 0)
    used = np.unique(tri)
    remap = -np.ones(len(verts), dtype=np.int64)
    remap[used] = np.arange(len(used))
    return verts[used], nrm[used], uv[used], remap[tri]
