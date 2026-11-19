"""Neon Dreams blockout helpers (Blender 5.x, headless). Metres, Z up."""
import bpy, math, random
import numpy as np
from mathutils import Vector, Matrix


def hex2lin(h):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple((x / 12.92) if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def link(ob, coll=None):
    (coll or bpy.context.scene.collection).objects.link(ob)
    return ob


# ----------------------------------------------------------------------------------------------
class MB:
    """Accumulates boxes / beams / cylinders / quads into one mesh (no shared vertices)."""

    def __init__(self):
        self.v, self.f, self.uv, self.attr = [], [], [], {}

    def _add(self, pts, faces, attrs=None, uvs=None):
        b = len(self.v)
        self.v.extend(pts)
        for f in faces:
            self.f.append(tuple(b + i for i in f))
        n = len(pts)
        attrs = attrs or {}
        for k in set(list(self.attr.keys()) + list(attrs.keys())):
            col = self.attr.setdefault(k, [0.0] * (len(self.v) - n))
            col.extend([float(attrs.get(k, 0.0))] * n)
        if uvs is None:
            for f in faces:
                self.uv.extend([(0.0, 0.0)] * len(f))
        else:
            self.uv.extend(uvs)

    BOXF = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]

    def box(self, c, size, yaw=0.0, attrs=None, M=None, skip=()):
        hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
        loc = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
               (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
        if M is None:
            cs, sn = math.cos(yaw), math.sin(yaw)
            pts = [(c[0] + x * cs - y * sn, c[1] + x * sn + y * cs, c[2] + z) for x, y, z in loc]
        else:
            pts = [tuple(Vector(c) + M @ Vector(p)) for p in loc]
        self._add(pts, [f for i, f in enumerate(self.BOXF) if i not in skip], attrs)   # skip: 0 bottom, 1 top, 2 -y, 3 +x, 4 +y, 5 -x

    def beam(self, p0, p1, t=0.3, attrs=None, t2=None):
        p0, p1 = Vector(p0), Vector(p1)
        d = p1 - p0
        L = d.length
        if L < 1e-6:
            return
        z = d / L
        up = Vector((0, 0, 1)) if abs(z.z) < 0.95 else Vector((1, 0, 0))
        x = z.cross(up).normalized()
        y = z.cross(x)
        M = Matrix((x, y, z)).transposed()
        self.box((p0 + p1) / 2, (t, t2 or t, L), M=M, attrs=attrs)

    def cyl(self, base, r, h, n=12, r_top=None, attrs=None, cap=True):
        rt = r if r_top is None else r_top
        pts = []
        for i in range(n):
            a = 2 * math.pi * i / n
            pts.append((base[0] + r * math.cos(a), base[1] + r * math.sin(a), base[2]))
        for i in range(n):
            a = 2 * math.pi * i / n
            pts.append((base[0] + rt * math.cos(a), base[1] + rt * math.sin(a), base[2] + h))
        faces = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
        if cap:
            faces.append(tuple(range(n, 2 * n)))
            faces.append(tuple(reversed(range(n))))
        self._add(pts, faces, attrs)

    def sphere(self, c, r, n=12, m=8, attrs=None, squash=1.0):
        pts, faces = [], []
        for j in range(m + 1):
            th = math.pi * j / m
            for i in range(n):
                ph = 2 * math.pi * i / n
                pts.append((c[0] + r * math.sin(th) * math.cos(ph), c[1] + r * math.sin(th) * math.sin(ph),
                            c[2] + r * squash * math.cos(th)))
        for j in range(m):
            for i in range(n):
                a, b = j * n + i, j * n + (i + 1) % n
                faces.append((a, a + n, b + n, b))
        self._add(pts, faces, attrs)

    def quad(self, p0, p1, p2, p3, uv=(0, 0, 1, 1), attrs=None, double=True):
        # `double` is kept for callers but adds nothing: the kit's materials export as glTF doubleSided, and a coincident
        # reversed copy z-fights (and renders black in Cycles).
        u0, v0, u1, v1 = uv
        self._add([p0, p1, p2, p3], [(0, 1, 2, 3)], attrs, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)])

    def build(self, name, mat, smooth=False):
        if not self.v:
            return None
        me = bpy.data.meshes.new(name)
        me.from_pydata(self.v, [], self.f)
        uvl = me.uv_layers.new(name="UVMap")
        uvl.data.foreach_set("uv", np.array(self.uv, dtype=np.float32).ravel())
        for k, col in self.attr.items():
            a = me.attributes.new(k, 'FLOAT', 'POINT')
            a.data.foreach_set("value", np.array(col, dtype=np.float32))
        if smooth:
            me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
        me.update()
        ob = bpy.data.objects.new(name, me)
        if mat:
            me.materials.append(mat)
        return link(ob)


# ----------------------------------------------------------------------------------------------
def new_mat(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    return m, nt


def node(nt, typ, **kw):
    n = nt.nodes.new(typ)
    for k, v in kw.items():
        setattr(n, k, v)
    return n


def setin(nt, sock, val):
    if isinstance(val, bpy.types.NodeSocket):
        nt.links.new(val, sock)
    else:
        sock.default_value = val


def math_(nt, op, a, b=None, c=None, clamp=False):
    n = node(nt, 'ShaderNodeMath', operation=op, use_clamp=clamp)
    setin(nt, n.inputs[0], a)
    if b is not None:
        setin(nt, n.inputs[1], b)
    if c is not None:
        setin(nt, n.inputs[2], c)
    return n.outputs[0]


def vmath(nt, op, a, b=None, out=0):
    n = node(nt, 'ShaderNodeVectorMath', operation=op)
    setin(nt, n.inputs[0], a)
    if b is not None:
        setin(nt, n.inputs[1], b)
    return n.outputs[out] if isinstance(out, int) else n.outputs[out]


def mixc(nt, fac, a, b):
    n = node(nt, 'ShaderNodeMix', data_type='RGBA')
    setin(nt, n.inputs[0], fac)
    setin(nt, n.inputs[6], a)
    setin(nt, n.inputs[7], b)
    return n.outputs[2]


def rgb(nt, col=(1, 1, 1)):
    n = node(nt, 'ShaderNodeRGB')
    n.outputs[0].default_value = (*col, 1)
    return n


def attr(nt, name):
    return node(nt, 'ShaderNodeAttribute', attribute_name=name).outputs[2]


# ----------------------------------------------------------------------------------------------
def cloud_hull(name, balls, res=16.0, disp=((260, 55), (90, 22)), thr=0.6):
    """Metaball cluster -> mesh hull with cauliflower displacement. balls: (x,y,z,r_visible)."""
    mb = bpy.data.metaballs.new(name + "_mb")
    mb.resolution = res
    mb.render_resolution = res
    mb.threshold = thr
    for (x, y, z, r) in balls:
        e = mb.elements.new()
        e.co = (x, y, z)
        e.radius = r * 1.75
    ob = link(bpy.data.objects.new(name + "_mbo", mb))
    dg = bpy.context.evaluated_depsgraph_get()
    dg.update()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    bpy.data.objects.remove(ob)
    bpy.data.metaballs.remove(mb)
    hull = link(bpy.data.objects.new(name + "_hull", me))
    for i, (scale, strength) in enumerate(disp):
        tex = bpy.data.textures.new(name + "_t%d" % i, 'CLOUDS')
        tex.noise_scale = scale
        tex.noise_depth = 3
        tex.noise_basis = 'VORONOI_F1' if i == 0 else 'ORIGINAL_PERLIN'
        md = hull.modifiers.new("d%d" % i, 'DISPLACE')
        md.texture = tex
        md.strength = strength
        md.mid_level = 0.35 if i == 0 else 0.5
        md.texture_coords = 'GLOBAL'
    return hull


def to_volume(name, hull, voxel, mat, band=40.0, vdisp=(120, 30), density=1.0):
    vol = bpy.data.volumes.new(name)
    ob = link(bpy.data.objects.new(name, vol))
    m = ob.modifiers.new("m2v", 'MESH_TO_VOLUME')
    m.object = hull
    m.resolution_mode = 'VOXEL_SIZE'
    m.voxel_size = voxel
    m.interior_band_width = band
    m.density = density
    if vdisp:
        tex = bpy.data.textures.new(name + "_vd", 'CLOUDS')
        tex.noise_scale = vdisp[0]
        tex.noise_depth = 2
        d = ob.modifiers.new("vd", 'VOLUME_DISPLACE')
        d.texture = tex
        d.strength = vdisp[1]
        d.texture_map_mode = 'GLOBAL'
        d.texture_mid_level = (0.5, 0.5, 0.5)
    vol.materials.append(mat)
    hull.hide_render = True
    hull.hide_viewport = True
    return ob


def slab(name, cx, cy, size, n, z_top, z_bot, height_fn):
    """Closed slab: displaced top grid, flat bottom. height_fn(X, Y) -> dz array."""
    xs = np.linspace(cx - size / 2, cx + size / 2, n)
    ys = np.linspace(cy - size / 2, cy + size / 2, n)
    X, Y = np.meshgrid(xs, ys)
    Zt = z_top + height_fn(X, Y)
    top = np.stack([X, Y, Zt], -1).reshape(-1, 3)
    bot = np.stack([X, Y, np.full_like(X, z_bot)], -1).reshape(-1, 3)
    verts = np.concatenate([top, bot])
    N = n * n
    idx = np.arange(N).reshape(n, n)
    a, b, c, d = idx[:-1, :-1].ravel(), idx[:-1, 1:].ravel(), idx[1:, 1:].ravel(), idx[1:, :-1].ravel()
    faces = [np.stack([a, b, c, d], 1), np.stack([d + N, c + N, b + N, a + N], 1)]
    for edge in (idx[0, :], idx[:, -1], idx[-1, ::-1], idx[::-1, 0]):
        e0, e1 = edge[:-1], edge[1:]
        faces.append(np.stack([e1, e0, e0 + N, e1 + N], 1))
    faces = np.concatenate(faces)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts.tolist(), [], faces.tolist())
    me.update()
    return link(bpy.data.objects.new(name, me))


def vnoise(X, Y, scale, seed=0):
    """Cheap smooth value noise on arrays (bicubic-ish via smoothstep)."""
    rs = np.random.RandomState(seed)
    G = rs.rand(256, 256).astype(np.float32)
    x = X / scale + 1000.0
    y = Y / scale + 1000.0
    xi = np.floor(x).astype(int)
    yi = np.floor(y).astype(int)
    fx = x - xi
    fy = y - yi
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    g = lambda i, j: G[i % 256, j % 256]
    return (g(xi, yi) * (1 - fx) * (1 - fy) + g(xi + 1, yi) * fx * (1 - fy)
            + g(xi, yi + 1) * (1 - fx) * fy + g(xi + 1, yi + 1) * fx * fy)


def billow(X, Y, scale, seed=0):
    """Cellular 'cauliflower' bumps: 1 - nearest feature distance (rounded domes)."""
    rs = np.random.RandomState(seed)
    J = rs.rand(64, 64, 2).astype(np.float32)
    x = X / scale + 500.0
    y = Y / scale + 500.0
    xi = np.floor(x).astype(int)
    yi = np.floor(y).astype(int)
    best = np.full(X.shape, 9.0, dtype=np.float32)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            cx_ = xi + dx
            cy_ = yi + dy
            j = J[cx_ % 64, cy_ % 64]
            d = np.sqrt((cx_ + j[..., 0] - x) ** 2 + (cy_ + j[..., 1] - y) ** 2)
            best = np.minimum(best, d)
    return np.clip(1.0 - best, 0, 1) ** 0.6
