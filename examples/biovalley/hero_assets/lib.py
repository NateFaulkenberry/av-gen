"""Hero-asset library for the Bioluminescent Valley (VFX art pass, Phase 2).

Every organism is a set of parametric grid surfaces (numpy) whose UVs are known by construction, so its anatomy is
painted straight into UV space: base colour + alpha, emissive, roughness (ORM) and a normal map from a painted height
field. Blender builds the mesh, the Principled materials that the glTF exporter understands, exports the GLB and
renders the look-dev. Nothing is hand-edited: `build.py` regenerates every asset.

Only what AV Gen's loader honours is authored (docs/prototypes/bioluminescent-valley/03-hero-assets.md):
POSITION / NORMAL / TEXCOORD_0, baseColor (+alpha, MASK), ORM, normal, emissive + KHR_materials_emissive_strength.
No vertex colours, no transmission: translucency is painted (thin = lighter base + emissive rim/veins).

UV convention (shared with the lead's material programs): v runs from root / centre (0) to tip / rim (1); u runs
around or across. Several parts of one material slot share the atlas by splitting u into strips.
"""
from __future__ import annotations

import json
import math
import os
import struct

import numpy as np

# ---- small maths -------------------------------------------------------------------------------------------------


def smoothstep(a, b, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    t = np.asarray(t, dtype=np.float64)
    if a.ndim == 1 and t.ndim >= 1:
        t = t[..., None]
    return a + (b - a) * t


def normalize(v):
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.maximum(n, 1e-12)


def col(c, mask):
    """A colour (3,) times a mask (...) -> (..., 3)."""
    return np.asarray(c, dtype=np.float64) * np.asarray(mask, dtype=np.float64)[..., None]


# ---- hashed value noise, periodic in x on request ----------------------------------------------------------------


def _hash(ix, iy, seed):
    h = (ix.astype(np.int64) * 374761393 + iy.astype(np.int64) * 668265263 + (seed * 2246822519) % 4294967291)
    h &= 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    h ^= h >> 16
    return (h & 0xFFFFFF).astype(np.float64) / float(0xFFFFFF)


def vnoise(x, y, seed=0, px=None):
    """Value noise in [0, 1] on the integer lattice; `px` makes it periodic in x with that many cells."""
    x = np.asarray(x, dtype=np.float64)
    y = np.asarray(y, dtype=np.float64)
    x0 = np.floor(x)
    y0 = np.floor(y)
    fx = x - x0
    fy = y - y0
    ix0 = x0.astype(np.int64)
    iy0 = y0.astype(np.int64)
    ix1 = ix0 + 1
    iy1 = iy0 + 1
    if px:
        ix0 %= px
        ix1 %= px
    ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
    uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    a = _hash(ix0, iy0, seed)
    b = _hash(ix1, iy0, seed)
    c = _hash(ix0, iy1, seed)
    d = _hash(ix1, iy1, seed)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def fbm(x, y, octaves=4, seed=0, px=None, gain=0.5, lac=2):
    s = 0.0
    amp = 1.0
    tot = 0.0
    for o in range(octaves):
        s = s + amp * vnoise(x * lac ** o, y * lac ** o, seed + 131 * o, px * lac ** o if px else None)
        tot += amp
        amp *= gain
    return s / tot


def ridge(x, y, octaves=3, seed=0, px=None):
    """1 on the zero-crossings of a noise, falling off either side: vein / crease networks."""
    n = fbm(x, y, octaves, seed, px)
    return 1.0 - np.abs(2.0 * n - 1.0)


def worley(x, y, seed=0, px=None, jitter=0.9):
    """Jittered-grid cellular noise. Returns F1, F2, F3 (distances in cell units) and the nearest cell's hash."""
    x = np.asarray(x, dtype=np.float64)
    y = np.asarray(y, dtype=np.float64)
    cx = np.floor(x).astype(np.int64)
    cy = np.floor(y).astype(np.int64)
    D = []
    H = []
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            ix = cx + dx
            iy = cy + dy
            hx = ix % px if px else ix
            jx = 0.5 + jitter * (_hash(hx, iy, seed) - 0.5)
            jy = 0.5 + jitter * (_hash(hx, iy, seed + 7) - 0.5)
            px_ = ix + jx
            py_ = iy + jy
            D.append(np.hypot(px_ - x, py_ - y))
            H.append(_hash(hx, iy, seed + 19))
    D = np.stack(D)
    H = np.stack(H)
    order = np.argsort(D, axis=0)
    Ds = np.take_along_axis(D, order, 0)
    h1 = np.take_along_axis(H, order[:1], 0)[0]
    return Ds[0], Ds[1], Ds[2], h1


# ---- grid surfaces ----------------------------------------------------------------------------------------------


def grid_normals(P, wrap_u):
    """N = cross(dP/du, dP/dv) for a (nv, nu, 3) grid; u = columns, v = rows."""
    if wrap_u:
        Pu = np.roll(P, -1, axis=1) - np.roll(P, 1, axis=1)
    else:
        Pu = np.gradient(P, axis=1)
    Pv = np.gradient(P, axis=0)
    N = np.cross(Pu, Pv)
    n = np.linalg.norm(N, axis=-1)
    bad = n < 1e-14
    N = N / np.maximum(n, 1e-14)[..., None]
    if bad.any():
        # a collapsed row (a pole): borrow the nearest good row's normal
        for r in range(P.shape[0]):
            if bad[r].all():
                rr = r + 1 if r + 1 < P.shape[0] else r - 1
                N[r] = N[rr]
    return N


def frames(curve):
    """Parallel-transport frames (T, N, B) along a polyline (n, 3), with B = T x N."""
    curve = np.asarray(curve, dtype=np.float64)
    T = np.gradient(curve, axis=0)
    T = normalize(T)
    ref = np.array([0.0, 0.0, 1.0]) if abs(T[0, 2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    Nn = normalize(np.cross(np.cross(T[0], ref), T[0]))
    Ns = [Nn]
    for i in range(1, len(curve)):
        n = Ns[-1] - T[i] * np.dot(Ns[-1], T[i])
        Ns.append(n / max(np.linalg.norm(n), 1e-12))
    Ns = np.array(Ns)
    B = np.cross(T, Ns)
    return T, Ns, B


def tube_grid(curve, radii, sides, shape=None):
    """A tube around `curve` (n, 3): grid (n, sides, 3), u around, v along; outward normals for cross(Pu, Pv).
    `shape(theta (sides,), t (n,)) -> (n, sides)` multiplies the radius (flutes, flattening)."""
    curve = np.asarray(curve, dtype=np.float64)
    T, N, B = frames(curve)
    th = np.linspace(0.0, 2 * math.pi, sides, endpoint=False)
    t = np.linspace(0.0, 1.0, len(curve))
    R = np.asarray(radii, dtype=np.float64)[:, None] * (shape(th, t) if shape else 1.0)
    ring = np.cos(th)[None, :, None] * N[:, None, :] + np.sin(th)[None, :, None] * B[:, None, :]
    return curve[:, None, :] + R[..., None] * ring


def bezier(p0, p1, p2, p3, n):
    t = np.linspace(0, 1, n)[:, None]
    p0, p1, p2, p3 = (np.asarray(p, dtype=np.float64) for p in (p0, p1, p2, p3))
    return (1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t ** 2 * p2 + t ** 3 * p3


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


# ---- the asset: material slots, regions, parts -------------------------------------------------------------------


class Region:
    def __init__(self, u0, u1, v0, v1, painter, bump=3.0):
        self.u0, self.u1, self.v0, self.v1 = u0, u1, v0, v1
        self.painter = painter
        self.bump = bump


class Slot:
    """One material: its geometry and its texture atlas."""

    def __init__(self, asset, name, res=(1024, 1024), mode="OPAQUE", double_sided=True, emissive_strength=4.0,
                 normal_scale=1.0, cutoff=0.5):
        self.asset = asset
        self.name = name
        self.W, self.H = res
        self.mode = mode
        self.double_sided = double_sided
        self.emissive_strength = emissive_strength
        self.normal_scale = normal_scale
        self.cutoff = cutoff
        self.regions = []
        self.V, self.F, self.UV, self.N = [], [], [], []
        self.nverts = 0

    @property
    def full_name(self):
        return f"{self.asset.name}_{self.name}"

    def region(self, u0, u1, painter, v0=0.0, v1=1.0, bump=3.0):
        r = Region(u0, u1, v0, v1, painter, bump)
        self.regions.append(r)
        return r

    def add_grid(self, P, region, wrap_u=False, s_range=(0.0, 1.0), t_range=(0.0, 1.0), N=None):
        """Adds a (nv, nu, 3) grid. Its columns span s_range of the region, its rows t_range."""
        P = np.asarray(P, dtype=np.float64)
        if N is None:
            N = grid_normals(P, wrap_u)
        nv, nu = P.shape[:2]
        if wrap_u:  # duplicate the seam column so u runs 0..1 without a wrap in UV space
            P = np.concatenate([P, P[:, :1]], axis=1)
            N = np.concatenate([N, N[:, :1]], axis=1)
            su = np.linspace(0.0, 1.0, nu + 1)
            nu += 1
        else:
            su = np.linspace(0.0, 1.0, nu)
        tv = np.linspace(0.0, 1.0, nv)
        s = s_range[0] + su * (s_range[1] - s_range[0])
        t = t_range[0] + tv * (t_range[1] - t_range[0])
        # half-texel inset keeps bilinear lookups inside the region
        du = 0.5 / self.W
        dv = 0.5 / self.H
        U = region.u0 + du + s * (region.u1 - region.u0 - 2 * du)
        Vv = region.v0 + dv + t * (region.v1 - region.v0 - 2 * dv)
        UU, VV = np.meshgrid(U, Vv)
        r = np.arange(nv - 1)[:, None]
        c = np.arange(nu - 1)[None, :]
        p00 = r * nu + c
        p01 = p00 + 1
        p10 = p00 + nu
        p11 = p10 + 1
        F = np.concatenate([np.stack([p00, p01, p11], -1).reshape(-1, 3),
                            np.stack([p00, p11, p10], -1).reshape(-1, 3)])
        self.V.append(P.reshape(-1, 3))
        self.N.append(N.reshape(-1, 3))
        self.UV.append(np.stack([UU, VV], -1).reshape(-1, 2))
        self.F.append(F + self.nverts)
        self.nverts += nv * nu

    def triangles(self):
        return sum(len(f) for f in self.F)

    # -- painting
    def paint(self, scale=1.0):
        W, H = int(self.W * scale), int(self.H * scale)
        base = np.zeros((H, W, 3))
        alpha = np.ones((H, W))
        emis = np.zeros((H, W, 3))
        rough = np.full((H, W), 0.5)
        ao = np.ones((H, W))
        nrm = np.zeros((H, W, 3))
        nrm[..., 2] = 1.0
        for rg in self.regions:
            x0, x1 = int(round(rg.u0 * W)), int(round(rg.u1 * W))
            y0, y1 = int(round(rg.v0 * H)), int(round(rg.v1 * H))
            s = (np.arange(x0, x1) + 0.5 - rg.u0 * W) / ((rg.u1 - rg.u0) * W)
            t = (np.arange(y0, y1) + 0.5 - rg.v0 * H) / ((rg.v1 - rg.v0) * H)
            S, T = np.meshgrid(s, t)
            out = rg.painter(S, T)
            sl = (slice(y0, y1), slice(x0, x1))
            if "base" in out:
                base[sl] = out["base"]
            if "alpha" in out:
                alpha[sl] = out["alpha"]
            if "emis" in out:
                emis[sl] = out["emis"]
            if "rough" in out:
                rough[sl] = out["rough"]
            if "ao" in out:
                ao[sl] = out["ao"]
            if "height" in out:
                h = np.asarray(out["height"], dtype=np.float64)
                k = rg.bump * scale  # bump is in texels at full resolution
                gx = np.gradient(h, axis=1) * W / max(self.W, 1) * (1.0 / scale) * k
                gy = np.gradient(h, axis=0) * H / max(self.H, 1) * (1.0 / scale) * k
                n = normalize(np.stack([-gx, -gy, np.ones_like(h)], -1))
                nrm[sl] = n
        return dict(base=np.clip(base, 0, 1), alpha=np.clip(alpha, 0, 1), emis=np.clip(emis, 0, 1),
                    rough=np.clip(rough, 0.03, 1), ao=np.clip(ao, 0, 1), normal=nrm)


class Asset:
    def __init__(self, name):
        self.name = name
        self.slots = []
        self.emitters = []  # [x, y, z (Blender Z-up), v, u, radius]

    def slot(self, name, **kw):
        s = Slot(self, name, **kw)
        self.slots.append(s)
        return s

    def triangles(self):
        return sum(s.triangles() for s in self.slots)

    def bounds(self):
        V = np.concatenate([np.concatenate(s.V) for s in self.slots if s.V])
        return V.min(0), V.max(0)


# ---- colour encoding ----------------------------------------------------------------------------------------------


def to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def box_down(a, f):
    """Box-filter downsample by an integer factor (H, W, ...)."""
    if f == 1:
        return a
    H, W = a.shape[:2]
    a = a[: H - H % f, : W - W % f]
    sh = (H // f, f, W // f, f) + a.shape[2:]
    return a.reshape(sh).mean(axis=(1, 3))


# ---- Blender side -------------------------------------------------------------------------------------------------


def _image(bpy, name, rgba, path, srgb):
    H, W = rgba.shape[:2]
    old = bpy.data.images.get(name)
    if old:
        bpy.data.images.remove(old)
    img = bpy.data.images.new(name, W, H, alpha=True)
    img.colorspace_settings.name = "sRGB" if srgb else "Non-Color"
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    return img


def build_textures(bpy, slot, tex_dir, scale=1.0):
    os.makedirs(tex_dir, exist_ok=True)
    m = slot.paint(scale)
    H, W = m["base"].shape[:2]
    one = np.ones((H, W, 1))
    base = np.concatenate([to_srgb(m["base"]), m["alpha"][..., None]], -1)
    emis = np.concatenate([to_srgb(m["emis"]), one], -1)
    orm = np.stack([m["ao"], m["rough"], np.zeros((H, W)), np.ones((H, W))], -1)
    nrm = np.concatenate([m["normal"] * 0.5 + 0.5, one], -1)
    tag = f"{slot.full_name}" + ("" if scale == 1.0 else f"_{int(slot.W * scale)}")
    imgs = {
        "base": _image(bpy, f"{tag}_base", base, os.path.join(tex_dir, f"{tag}_base.png"), True),
        "emis": _image(bpy, f"{tag}_emissive", emis, os.path.join(tex_dir, f"{tag}_emissive.png"), True),
        "orm": _image(bpy, f"{tag}_orm", orm, os.path.join(tex_dir, f"{tag}_orm.png"), False),
        "normal": _image(bpy, f"{tag}_normal", nrm, os.path.join(tex_dir, f"{tag}_normal.png"), False),
    }
    return imgs


def build_material(bpy, slot, imgs, emissive_scale=1.0):
    name = slot.full_name
    old = bpy.data.materials.get(name)
    if old:
        bpy.data.materials.remove(old)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    def tex(img):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = img
        return n

    tb = tex(imgs["base"])
    nt.links.new(tb.outputs["Color"], bsdf.inputs["Base Color"])
    if slot.mode != "OPAQUE":
        nt.links.new(tb.outputs["Alpha"], bsdf.inputs["Alpha"])
    to = tex(imgs["orm"])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(to.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    tn = tex(imgs["normal"])
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nm.inputs["Strength"].default_value = slot.normal_scale
    nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    te = tex(imgs["emis"])
    nt.links.new(te.outputs["Color"], bsdf.inputs["Emission Color"])
    bsdf.inputs["Emission Strength"].default_value = slot.emissive_strength * emissive_scale
    bsdf.inputs["IOR"].default_value = 1.4
    mat.use_backface_culling = not slot.double_sided
    if slot.mode != "OPAQUE":
        try:
            mat.surface_render_method = "DITHERED"
        except Exception:
            pass
    return mat


def build_object(bpy, asset, mats, name=None):
    V = []
    F = []
    UV = []
    N = []
    MI = []
    off = 0
    for i, s in enumerate(asset.slots):
        if not s.V:
            continue
        v = np.concatenate(s.V)
        f = np.concatenate(s.F)
        V.append(v)
        F.append(f + off)
        UV.append(np.concatenate(s.UV))
        N.append(np.concatenate(s.N))
        MI.append(np.full(len(f), i))
        off += len(v)
    V = np.concatenate(V)
    F = np.concatenate(F)
    UV = np.concatenate(UV)
    N = normalize(np.concatenate(N))
    MI = np.concatenate(MI)
    name = name or asset.name
    old = bpy.data.objects.get(name)
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    me = bpy.data.meshes.new(name)
    me.from_pydata(V.tolist(), [], F.tolist())
    me.update()
    for m in mats:
        me.materials.append(m)
    me.polygons.foreach_set("material_index", MI.astype(np.int32))
    me.polygons.foreach_set("use_smooth", np.ones(len(F), dtype=bool))
    uvl = me.uv_layers.new(name="UVMap")
    li = np.zeros(len(me.loops), dtype=np.int32)
    me.loops.foreach_get("vertex_index", li)
    uvl.data.foreach_set("uv", UV[li].astype(np.float32).ravel())
    me.normals_split_custom_set_from_vertices(N.tolist())
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def patch_glb(path, slots):
    """The exporter's alpha-mode guess is replaced by the slot's declared mode, cutoff and sidedness."""
    data = open(path, "rb").read()
    jlen, _ = struct.unpack_from("<II", data, 12)
    js = json.loads(data[20:20 + jlen])
    rest = data[20 + jlen:]
    by = {s.full_name: s for s in slots}
    for m in js.get("materials", []):
        s = by.get(m.get("name"))
        if not s:
            continue
        m["alphaMode"] = s.mode
        if s.mode == "MASK":
            m["alphaCutoff"] = s.cutoff
        else:
            m.pop("alphaCutoff", None)
        m["doubleSided"] = bool(s.double_sided)
        # the ORM image's R channel is the painted ambient occlusion: reference it as the occlusion texture
        mr = m.get("pbrMetallicRoughness", {}).get("metallicRoughnessTexture")
        if mr is not None:
            m["occlusionTexture"] = {"index": mr["index"], "strength": 1.0}
    jb = json.dumps(js, separators=(",", ":")).encode()
    jb += b" " * ((4 - len(jb) % 4) % 4)
    out = struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(jb) + len(rest)) + struct.pack("<II", len(jb), 0x4E4F534A)
    open(path, "wb").write(out + jb + rest)
    return js


def export_glb(bpy, ob, path, slots):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_texcoords=True, export_normals=True,
                              export_materials="EXPORT", export_image_format="AUTO", export_animations=False,
                              export_tangents=False)
    return patch_glb(path, slots)


def write_emitters(path, pts):
    """Sidecar emitters in glTF space (Y up): [x, y, z, v, u, radius]."""
    out = [[round(p[0], 5), round(p[2], 5), round(-p[1], 5), round(p[3], 3), round(p[4], 3), round(p[5], 5)]
           for p in pts]
    with open(path, "w") as f:
        json.dump({"points": out}, f)


# ---- explicit vein trees ------------------------------------------------------------------------------------------


def grow_veins(rng, n_primary, r0=0.1, r1=1.0, step=0.025, wander=0.12, branch_p=0.07, spread=0.35, phase=0.0,
               w0=1.0, decay=0.85):
    """Radial branching veins in the unit disc. Returns segments (k, 5): x0, y0, x1, y1, width (relative)."""
    segs = []
    tips = []
    for i in range(n_primary):
        a = phase + 2 * math.pi * (i + rng.uniform(-0.3, 0.3)) / n_primary
        tips.append([r0 * math.cos(a), r0 * math.sin(a), a, w0])
    while tips:
        nxt = []
        for x, y, a, w in tips:
            r = math.hypot(x, y)
            if r >= r1 or w < 0.12:
                continue
            a2 = a + rng.normal(0, wander)
            # veins bend back toward radial
            rad = math.atan2(y, x)
            a2 += 0.25 * math.atan2(math.sin(rad - a2), math.cos(rad - a2))
            st = step * (0.6 + 0.8 * r)
            x2, y2 = x + st * math.cos(a2), y + st * math.sin(a2)
            w2 = w * (0.975 - 0.03 * r)
            segs.append([x, y, x2, y2, w])
            if rng.random() < branch_p * (0.3 + 2.0 * r * r):
                side = rng.choice([-1, 1])
                nxt.append([x2, y2, a2 + side * spread * rng.uniform(0.6, 1.3), w2 * decay * 0.8])
                a2 -= side * spread * 0.3
                w2 *= decay
            nxt.append([x2, y2, a2, w2])
        tips = nxt
    return np.array(segs)


def raster_segments(segs, res, width, extent=1.0, soft=1.5):
    """Distance-field rasterisation of segments onto a square canvas covering [-extent, extent]^2.
    Returns (core, glow): core = smooth line of `width * w` (canvas units), glow = wider exponential halo."""
    core = np.zeros((res, res))
    glow = np.zeros((res, res))
    px = 2 * extent / res
    for x0, y0, x1, y1, w in segs:
        ww = width * w
        pad = ww * 4 + 3 * px
        lo_x, hi_x = min(x0, x1) - pad, max(x0, x1) + pad
        lo_y, hi_y = min(y0, y1) - pad, max(y0, y1) + pad
        i0 = max(int((lo_x + extent) / px), 0)
        i1 = min(int((hi_x + extent) / px) + 1, res)
        j0 = max(int((lo_y + extent) / px), 0)
        j1 = min(int((hi_y + extent) / px) + 1, res)
        if i0 >= i1 or j0 >= j1:
            continue
        X = -extent + (np.arange(i0, i1) + 0.5) * px
        Y = -extent + (np.arange(j0, j1) + 0.5) * px
        XX, YY = np.meshgrid(X, Y)
        dx, dy = x1 - x0, y1 - y0
        L2 = dx * dx + dy * dy + 1e-18
        t = np.clip(((XX - x0) * dx + (YY - y0) * dy) / L2, 0, 1)
        d = np.hypot(XX - (x0 + t * dx), YY - (y0 + t * dy))
        c = (1 - smoothstep(ww * 0.5, ww * 0.5 + soft * px, d)) * (0.45 + 0.55 * min(w, 1.0))
        g = np.exp(-d / (ww * 2.5 + px))
        sl = (slice(j0, j1), slice(i0, i1))
        core[sl] = np.maximum(core[sl], c)
        glow[sl] = np.maximum(glow[sl], g * min(w, 1.0))
    return core, glow


def sample_canvas(canvas, X, Y, extent=1.0):
    """Bilinear lookup of a square canvas at canvas-space coordinates (X, Y in [-extent, extent])."""
    res = canvas.shape[0]
    fx = np.clip((X + extent) / (2 * extent) * res - 0.5, 0, res - 1.001)
    fy = np.clip((Y + extent) / (2 * extent) * res - 0.5, 0, res - 1.001)
    i = np.floor(fx).astype(int)
    j = np.floor(fy).astype(int)
    ax = fx - i
    ay = fy - j
    c00 = canvas[j, i]
    c10 = canvas[j, i + 1]
    c01 = canvas[j + 1, i]
    c11 = canvas[j + 1, i + 1]
    return (c00 * (1 - ax) + c10 * ax) * (1 - ay) + (c01 * (1 - ax) + c11 * ax) * ay


# ---- shared organism kit ------------------------------------------------------------------------------------------

# the family palette (linear): emerald and cyan luminous anatomy, blue and violet lace, near-black teal tissue
EMERALD = np.array([0.06, 1.0, 0.30])
CYAN = np.array([0.02, 0.75, 1.0])
BLUE = np.array([0.10, 0.30, 1.0])
VIOLET = np.array([0.40, 0.18, 1.0])
MINT = np.array([0.5, 1.0, 0.7])
TISSUE_DARK = np.array([0.010, 0.016, 0.022])
TEAL_DARK = np.array([0.012, 0.035, 0.045])
TEAL_THIN = np.array([0.030, 0.095, 0.105])


def rodrigues(v, axis, ang):
    axis = axis / max(np.linalg.norm(axis), 1e-12)
    return (v * math.cos(ang) + np.cross(axis, v) * math.sin(ang) + axis * np.dot(axis, v) * (1 - math.cos(ang)))


def perp(d, rng):
    a = rng.normal(size=3)
    a -= d * np.dot(a, d)
    return a / max(np.linalg.norm(a), 1e-12)


class Branch:
    __slots__ = ("pts", "radii", "d0", "d1", "gen", "tip")

    def __init__(self, pts, radii, d0, d1, gen, tip):
        self.pts, self.radii, self.d0, self.d1, self.gen, self.tip = pts, radii, d0, d1, gen, tip


def grow_branches(rng, p0, d0, length, radius, max_gen, children=(2, 3), spread=(0.35, 0.8), shrink=0.7,
                  rshrink=0.6, curl=0.6, up=0.15, segs=14, crozier=0.3, crozier_turns=1.4, min_r=0.0004,
                  child_t=(0.35, 1.0), min_dz=-1.0, sink=0.0):
    """A recursive branching body of curved, tapering tubes. Returns [Branch]; each branch records its path distance
    from the root (d0 -> d1) so a v = distance / max-distance runs root (0) to tips (1) over the whole organism."""
    out = []

    def rec(p, d, L, r, gen, dist):
        d = d / np.linalg.norm(d)
        axis = perp(d, rng)
        tot = curl * rng.uniform(0.5, 1.2) * (1 + 0.5 * gen)
        terminal = gen >= max_gen
        if terminal and rng.random() < crozier:
            tot = crozier_turns * 2 * math.pi * rng.uniform(0.6, 1.0)
        n = segs
        pts = [p.copy()]
        dirs = [d.copy()]
        cur = d.copy()
        step = L / n
        for i in range(n):
            f = i / n
            a = tot / n * (0.3 + 1.4 * f) if terminal else tot / n
            cur = rodrigues(cur, axis, a)
            cur = cur + np.array([0, 0, up]) * (1 - gen / (max_gen + 1)) * step * 8
            cur /= np.linalg.norm(cur)
            # coiling tips get shorter steps so the crozier tightens
            s = step * (1 - 0.6 * f) if (terminal and tot > 3) else step
            pts.append(pts[-1] + cur * s)
            dirs.append(cur)
        pts = np.array(pts)
        seglen = np.r_[0, np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))]
        t = seglen / seglen[-1]
        r_end = max(r * rshrink, min_r) if not terminal else min_r
        radii = r + (r_end - r) * t ** 0.8
        out.append(Branch(pts, radii, dist, dist + seglen[-1], gen, terminal))
        if terminal:
            return
        k = rng.integers(children[0], children[1] + 1)
        for j in range(k):
            tt = rng.uniform(*child_t) if j < k - 1 else 1.0
            i = min(int(tt * n), n)
            axis = perp(dirs[i], rng)  # drawn before the angle: the approved organisms depend on this order
            ang = rng.uniform(*spread) * (1.0 if tt < 1 else (0.35 if sink > 0 else 1.0))  # the leader turns gently
            nd = rodrigues(dirs[i], axis, ang)
            if nd[2] < min_dz:  # keep limbs from diving into the ground
                nd = nd.copy()
                nd[2] = min_dz
                nd /= np.linalg.norm(nd)
            # `sink` starts a side branch inside its parent so the joint has no visible seam
            start = pts[i] - nd * radii[i] * (sink if tt < 1 else 0.5 * sink)
            rec(start, nd, L * shrink * rng.uniform(0.8, 1.15), radii[i] * (0.85 if tt < 1 else 1.0), gen + 1,
                dist + seglen[i])

    rec(np.asarray(p0, dtype=np.float64), np.asarray(d0, dtype=np.float64), length, radius, 0, 0.0)
    return out


def tube_up(curve, radii, sides, shape=None, up=(0.0, 0.0, 1.0)):
    """A tube whose u = 0 faces up (world +Z projected off the axis): painters can put moss on top."""
    curve = np.asarray(curve, dtype=np.float64)
    T = normalize(np.gradient(curve, axis=0))
    U = np.asarray(up, dtype=np.float64)[None, :] * np.ones_like(T)
    N = U - T * np.sum(U * T, axis=1, keepdims=True)
    bad = np.linalg.norm(N, axis=1) < 1e-4
    N[bad] = np.cross(T[bad], [1.0, 0, 0])
    N = normalize(N)
    B = np.cross(T, N)
    th = np.linspace(0, 2 * math.pi, sides, endpoint=False)
    t = np.linspace(0.0, 1.0, len(curve))
    R = np.asarray(radii, dtype=np.float64)[:, None] * (shape(th, t) if shape else 1.0)
    ring = np.cos(th)[None, :, None] * N[:, None, :] + np.sin(th)[None, :, None] * B[:, None, :]
    return curve[:, None, :] + R[..., None] * ring


def add_branches(slot, region, branches, sides_fn, shape=None, s_width=0.25, seed=0, up_frame=False):
    """Tubes for every branch; v = path distance / the organism's longest path. With `up_frame`, u = 0 faces up on
    every branch and u spans the region's full width (s_width is ignored), so a painter can tell top from bottom."""
    rng = np.random.default_rng(seed)
    dmax = max(b.d1 for b in branches)
    for b in branches:
        sides = sides_fn(b)
        if up_frame:
            P = tube_up(b.pts, b.radii, sides, shape)
            slot.add_grid(P, region, wrap_u=True, t_range=(b.d0 / dmax, b.d1 / dmax))
            continue
        P = tube_grid(b.pts, b.radii, sides, shape)
        s0 = rng.uniform(0, 1 - s_width)
        slot.add_grid(P, region, wrap_u=True, s_range=(s0, s0 + s_width), t_range=(b.d0 / dmax, b.d1 / dmax))
    return dmax


def lace(S, T, cells_u, cells_v, seed, wall=(0.045, 0.10), px=True, warp=0.6):
    """A cellular net in (S, T): returns wall (strand) mask, knot mask and the cell hash. cells_v may be an array."""
    x = S * cells_u
    y = T * cells_v
    wx = x + warp * (fbm(S * 8, T * 4, 2, seed, px=8 if px else None) - 0.5)
    wy = y + warp * (fbm(S * 8 + 3, T * 4, 2, seed + 1, px=8 if px else None) - 0.5)
    F1, F2, F3, h = worley(wx, wy, seed + 2, px=cells_u if px else None, jitter=0.9)
    w = 1 - smoothstep(wall[0], wall[1], F2 - F1)
    k = 1 - smoothstep(0.05, 0.2, F3 - F1)
    return w, k, h, F1


def revolve(profile_r, profile_z, nth, radial=None, phase=0.0, clockwise=True):
    """A surface of revolution: rows follow the profile (v), columns go around (u). `radial(th, v) -> (nv, nth)`
    multiplies the radius (folds, lobes). Clockwise keeps cross(Pu, Pv) outward for an outward-going profile."""
    th = np.linspace(0, 2 * math.pi, nth, endpoint=False) + phase
    if clockwise:
        th = -th
    v = np.linspace(0, 1, len(profile_r))
    R = np.asarray(profile_r)[:, None] * (radial(th, v) if radial else 1.0)
    Z = np.asarray(profile_z)[:, None] * np.ones_like(th)[None, :]
    return np.stack([R * np.cos(th)[None, :], R * np.sin(th)[None, :], Z], -1), th


def frame_to(a):
    """A rotation taking +Z to the unit vector a."""
    a = a / np.linalg.norm(a)
    z = np.array([0, 0, 1.0])
    v = np.cross(z, a)
    c = float(np.dot(z, a))
    if np.linalg.norm(v) < 1e-9:
        return np.eye(3) if c > 0 else np.diag([1.0, -1.0, -1.0])
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx * (1 / (1 + c))


def _hash3(ix, iy, iz, seed):
    return _hash(ix * 7919 + iz * 104729, iy, seed)


def worley3(x, y, z, seed=0, jitter=0.9, chunk=1 << 18):
    """3D jittered-grid cellular noise: F1 and the nearest cell's hash. For painting detail on a surface by its own
    3D positions, so it keeps its shape however the UVs stretch."""
    shape = np.shape(x)
    x = np.ravel(x).astype(np.float64)
    y = np.ravel(y).astype(np.float64)
    z = np.ravel(z).astype(np.float64)
    F1 = np.empty_like(x)
    H1 = np.empty_like(x)
    for a in range(0, len(x), chunk):
        xs, ys, zs = x[a:a + chunk], y[a:a + chunk], z[a:a + chunk]
        cx, cy, cz = np.floor(xs).astype(np.int64), np.floor(ys).astype(np.int64), np.floor(zs).astype(np.int64)
        best = np.full(len(xs), 1e9)
        hb = np.zeros(len(xs))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    ix, iy, iz = cx + dx, cy + dy, cz + dz
                    px = ix + 0.5 + jitter * (_hash3(ix, iy, iz, seed) - 0.5)
                    py = iy + 0.5 + jitter * (_hash3(ix, iy, iz, seed + 7) - 0.5)
                    pz = iz + 0.5 + jitter * (_hash3(ix, iy, iz, seed + 13) - 0.5)
                    d = np.sqrt((px - xs) ** 2 + (py - ys) ** 2 + (pz - zs) ** 2)
                    m = d < best
                    best = np.where(m, d, best)
                    hb = np.where(m, _hash3(ix, iy, iz, seed + 19), hb)
        F1[a:a + chunk] = best
        H1[a:a + chunk] = hb
    return F1.reshape(shape), H1.reshape(shape)
