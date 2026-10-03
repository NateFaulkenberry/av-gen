"""Generated meshes for the Sonic Abstract prototypes: faceted landscapes, rocks and blocks written as small GLB files
that a procedural node instances (`{"kind": "mesh", "asset": "meshes/<name>.glb"}`, ADR-044).

Why files: the procedural primitives cannot draw a flat-faceted landform (a sphere, a cylinder or a tube shares its
normals between faces, so their shading is smooth), and an SDF of planes is either marched every frame or meshed
statically. A generated mesh keeps the facets exact, costs a few thousand triangles, and still takes every deformer,
material program and route the procedural system has.

Everything is a pure function of its arguments (seeded), so `abstract.py build` writes the same bytes every time.
Coordinates: metres, +Y up, triangles counter-clockwise seen from outside.
"""
from __future__ import annotations

import json
import math
import os
import struct


# ================================================================================================ noise
def _hash(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)


def value_noise(x, y, seed=1):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = _hash(ix, iy, seed), _hash(ix + 1, iy, seed)
    c, d = _hash(ix, iy + 1, seed), _hash(ix + 1, iy + 1, seed)
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy


def fbm(x, y, seed=1, octaves=4, ridged=False):
    s, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        n = value_noise(x, y, seed + 17 * o)
        if ridged:
            n = 1.0 - abs(2.0 * n - 1.0)
            n = n * n
        s += amp * n
        norm += amp
        amp *= 0.5
        x, y = x * 2.03 + 11.7, y * 2.03 + 3.1
    return s / norm


def value_noise_p(x, y, seed=1, px=0, py=0):
    """Value noise, periodic with integer periods px, py (0 = not periodic) in lattice units."""
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

    def h(a, b):
        if px:
            a %= px
        if py:
            b %= py
        return _hash(a, b, seed)
    a, b, c, d = h(ix, iy), h(ix + 1, iy), h(ix, iy + 1), h(ix + 1, iy + 1)
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy


def fbm_p(x, y, seed=1, octaves=4, py=0, ridged=False):
    """fbm periodic in y with an integer lattice period `py` at the first octave (each octave doubles it)."""
    s, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        n = value_noise_p(x, y, seed + 17 * o, 0, py * (2 ** o))
        if ridged:
            n = 1.0 - abs(2.0 * n - 1.0)
            n = n * n
        s += amp * n
        norm += amp
        amp *= 0.5
        x, y = x * 2.0 + 11.7, y * 2.0
    return s / norm


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


# ================================================================================================ geometry
def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return (v[0] / n, v[1] / n, v[2] / n)


def faceted(tris):
    """Triangles [(a, b, c), ...] -> unshared vertices with each triangle's own normal (flat facets)."""
    pos, nrm = [], []
    for a, b, c in tris:
        n = unit(cross(sub(b, a), sub(c, a)))
        pos += [a, b, c]
        nrm += [n, n, n]
    return pos, nrm


def smooth_shaded(verts, faces):
    """Shared vertices with area-weighted normals (smooth shading). faces: index triples."""
    acc = [[0.0, 0.0, 0.0] for _ in verts]
    for i, j, k in faces:
        n = cross(sub(verts[j], verts[i]), sub(verts[k], verts[i]))
        for v in (i, j, k):
            acc[v][0] += n[0]
            acc[v][1] += n[1]
            acc[v][2] += n[2]
    return list(verts), [unit(a) for a in acc], [i for f in faces for i in f]


def mirrored(tris):
    """The twin below the waterline: y -> -y with the winding reversed (so it still faces outward)."""
    return [((a[0], -a[1], a[2]), (c[0], -c[1], c[2]), (b[0], -b[1], b[2])) for a, b, c in tris]


def polar_range(r0, r1, nr, nth, height, seed=1, jitter=0.32, theta0=0.0, theta1=2.0 * math.pi):
    """A ring of land between radii r0 and r1 (a polar grid, its vertices jittered so the facets are irregular), as
    triangles. `height(x, z, r, theta)` gives the ground height. Quads split along a random diagonal."""
    closed = abs(theta1 - theta0 - 2.0 * math.pi) < 1e-6
    cols = nth if closed else nth + 1
    grid = []
    for i in range(nr + 1):
        row = []
        for j in range(cols):
            jr = (_hash(i, j, seed) - 0.5) * 2.0 * jitter if 0 < i < nr else 0.0
            jt = (_hash(j, i, seed + 5) - 0.5) * 2.0 * jitter
            r = r0 + (r1 - r0) * (i + jr) / nr
            th = theta0 + (theta1 - theta0) * (j + jt) / nth
            x, z = r * math.sin(th), r * math.cos(th)
            row.append((x, height(x, z, r, th), z))
        grid.append(row)
    tris = []
    for i in range(nr):
        for j in range(nth):
            j1 = (j + 1) % cols if closed else j + 1
            a, b, c, d = grid[i][j], grid[i][j1], grid[i + 1][j1], grid[i + 1][j]
            # winding: outward-facing up (seen from above, counter-clockwise)
            if _hash(i, j, seed + 9) < 0.5:
                tris += [(a, c, b), (a, d, c)]
            else:
                tris += [(a, d, b), (b, d, c)]
    return tris


def rock(seed, radius, height, sides=7, rings=3, sink=0.0, lean=(0.0, 0.0)):
    """A low-poly rock or crag: a jittered cone of `sides` facets round and `rings` up, apex off-centre."""
    tris = []
    pts = []
    for i in range(rings + 1):
        t = i / rings
        row = []
        for j in range(sides):
            a = 2.0 * math.pi * (j + 0.35 * (_hash(i, j, seed) - 0.5)) / sides
            rr = radius * (1.0 - t) * (0.75 + 0.5 * _hash(j, i, seed + 3)) if i < rings else 0.0
            y = height * (t ** 0.85) * (0.9 + 0.2 * _hash(i + 7, j, seed)) - sink
            row.append((rr * math.cos(a) + lean[0] * t * height, y, rr * math.sin(a) + lean[1] * t * height))
        pts.append(row)
    for i in range(rings):
        for j in range(sides):
            j1 = (j + 1) % sides
            a, b, c, d = pts[i][j], pts[i][j1], pts[i + 1][j1], pts[i + 1][j]
            if i == rings - 1:
                tris.append((a, d, b))
            else:
                tris += [(a, d, b), (b, d, c)]
    return tris


def clip_above(tris, y0=0.0):
    """Keep only the part of each triangle at or above the plane y = y0 (the land above the water: what dips under it
    becomes water, because the lake is the sky's mirror image wherever nothing stands)."""
    out = []
    for tri in tris:
        poly = []
        for i in range(3):
            a, b = tri[i], tri[(i + 1) % 3]
            ina, inb = a[1] >= y0, b[1] >= y0
            if ina:
                poly.append(a)
            if ina != inb:
                t = (y0 - a[1]) / (b[1] - a[1])
                poly.append((a[0] + (b[0] - a[0]) * t, y0, a[2] + (b[2] - a[2]) * t))
        for k in range(1, len(poly) - 1):
            out.append((poly[0], poly[k], poly[k + 1]))
    return [t for t in out if abs(cross(sub(t[1], t[0]), sub(t[2], t[0]))[1]) > 1e-6 or
            sum(abs(c) for c in cross(sub(t[1], t[0]), sub(t[2], t[0]))) > 1e-4]


def heightfield(x0, x1, z0, z1, nx, nz, height, normal_eps=2.0):
    """A smooth-shaded grid between x0..x1 and z0..z1 (nx, nz cells): shared vertices, normals from the height
    function's own finite differences (so two tiles of a periodic field meet without a seam). Returns
    (positions, normals, indices)."""
    verts, norms = [], []
    for j in range(nz + 1):
        z = z0 + (z1 - z0) * j / nz
        for i in range(nx + 1):
            x = x0 + (x1 - x0) * i / nx
            y = height(x, z)
            hx = height(x + normal_eps, z) - height(x - normal_eps, z)
            hz = height(x, z + normal_eps) - height(x, z - normal_eps)
            verts.append((x, y, z))
            norms.append(unit((-hx, 2.0 * normal_eps, -hz)))
    idx = []
    w = nx + 1
    for j in range(nz):
        for i in range(nx):
            a, b, c, d = j * w + i, j * w + i + 1, (j + 1) * w + i + 1, (j + 1) * w + i
            idx += [a, d, c, a, c, b]
    return verts, norms, idx


def ribbon(path, width, y_of, z0, z1, n, lift=0.4):
    """A flat strip along x = path(z) from z0 to z1 (n segments), at height y_of(z) + lift, facing up."""
    verts, norms, idx = [], [], []
    for j in range(n + 1):
        z = z0 + (z1 - z0) * j / n
        x = path(z)
        dx = (path(z + 1.0) - path(z - 1.0)) / 2.0
        side = unit((1.0, 0.0, -dx))
        y = y_of(z) + lift
        for s in (-1.0, 1.0):
            verts.append((x + side[0] * width * 0.5 * s, y, z + side[2] * width * 0.5 * s))
            norms.append((0.0, 1.0, 0.0))
    for j in range(n):
        a, b, c, d = 2 * j, 2 * j + 1, 2 * j + 3, 2 * j + 2
        idx += [a, d, c, a, c, b]
    return verts, norms, idx


def cone(base, radius, height, sides=6, seed=0):
    """A faceted cone (a stylised conifer), apex up, its base centre at `base`."""
    tris = []
    apex = (base[0], base[1] + height, base[2])
    a0 = _hash(seed, 3, 7) * 6.283
    ring = [(base[0] + radius * math.cos(a0 + 2 * math.pi * k / sides), base[1],
             base[2] + radius * math.sin(a0 + 2 * math.pi * k / sides)) for k in range(sides)]
    for k in range(sides):
        tris.append((ring[k], apex, ring[(k + 1) % sides]))
    return tris


def ball(centre, radius, seg=8, rings=6):
    """A low-poly ball (faceted), for the round trees and the bubbles."""
    tris = []
    pts = []
    for i in range(rings + 1):
        ph = math.pi * i / rings
        row = []
        for j in range(seg):
            th = 2 * math.pi * j / seg
            row.append((centre[0] + radius * math.sin(ph) * math.cos(th), centre[1] + radius * math.cos(ph),
                        centre[2] + radius * math.sin(ph) * math.sin(th)))
        pts.append(row)
    for i in range(rings):
        for j in range(seg):
            j1 = (j + 1) % seg
            a, b, c, d = pts[i][j], pts[i][j1], pts[i + 1][j1], pts[i + 1][j]
            if i > 0:
                tris.append((a, b, c))
            if i < rings - 1:
                tris.append((a, c, d))
    return tris


def prism(base, radius, height, sides=6, tip=0.35, seed=0, lean=(0.0, 0.0)):
    """A crystal: a faceted prism with a pointed tip, leaning."""
    tris = []
    a0 = _hash(seed, 5, 11) * 6.283
    lx, lz = lean
    def at(r, y, k):
        a = a0 + 2 * math.pi * k / sides
        return (base[0] + r * math.cos(a) + lx * y, base[1] + y, base[2] + r * math.sin(a) + lz * y)
    top = height * (1.0 - tip)
    apex = (base[0] + lx * height, base[1] + height, base[2] + lz * height)
    for k in range(sides):
        k1 = (k + 1) % sides
        b0, b1 = at(radius, 0.0, k), at(radius, 0.0, k1)
        t0, t1 = at(radius, top, k), at(radius, top, k1)
        tris += [(b0, t0, t1), (b0, t1, b1), (t0, apex, t1)]
    return tris


def voxels(cells, size=1.0, origin=(0.0, 0.0, 0.0), keep=None):
    """Integer cells {(i, j, k)} -> the faceted faces between a filled cell and an empty neighbour (the 8-bit block
    look: every face flat, nothing hidden drawn). A cell spans [i, i+1] x [j, j+1] x [k, k+1] times `size`. `keep`
    (i, j, k) -> bool emits only some cells' faces while every cell still hides its neighbours' (a checker split into
    two meshes without doubling the faces between them)."""
    cells = set(cells)
    ox, oy, oz = origin
    s = size
    tris = []
    faces = [((1, 0, 0), [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)]),
             ((-1, 0, 0), [(0, 0, 1), (0, 1, 1), (0, 1, 0), (0, 0, 0)]),
             ((0, 1, 0), [(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)]),
             ((0, -1, 0), [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]),
             ((0, 0, 1), [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]),
             ((0, 0, -1), [(1, 0, 0), (0, 0, 0), (0, 1, 0), (1, 1, 0)])]
    for (i, j, k) in cells:
        if keep is not None and not keep(i, j, k):
            continue
        for (dx, dy, dz), quad in faces:
            if (i + dx, j + dy, k + dz) in cells:
                continue
            v = [(ox + (i + a) * s, oy + (j + b) * s, oz + (k + c) * s) for a, b, c in quad]
            tris += [(v[0], v[1], v[2]), (v[0], v[2], v[3])]
    return tris


def write_indexed(path, verts, norms, idx, name="mesh"):
    return write_glb(path, verts, norms, idx, name)


def translate(tris, off):
    return [tuple((v[0] + off[0], v[1] + off[1], v[2] + off[2]) for v in t) for t in tris]


# ================================================================================================ GLB
def write_glb(path, positions, normals, indices=None, name="mesh"):
    """A minimal glTF 2.0 binary: one mesh, one primitive (POSITION, NORMAL, uint32 indices), one plain material."""
    n = len(positions)
    if indices is None:
        indices = list(range(n))
    pb = b"".join(struct.pack("<3f", *p) for p in positions)
    nb = b"".join(struct.pack("<3f", *v) for v in normals)
    ib = struct.pack("<%dI" % len(indices), *indices)
    blob = pb + nb + ib
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "sonic-abstract meshes.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
                                                              "roughnessFactor": 1.0}}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2,
                                                  "material": 0, "mode": 4}]}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pb), "target": 34962},
            {"buffer": 0, "byteOffset": len(pb), "byteLength": len(nb), "target": 34962},
            {"buffer": 0, "byteOffset": len(pb) + len(nb), "byteLength": len(ib), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)
    return path


def write_faceted(path, tris, name="mesh"):
    pos, nrm = faceted(tris)
    return write_glb(path, pos, nrm, None, name)
