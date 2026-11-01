"""The three Cydonia pyramids (Art Pass 2, M2): ancient, eroded, partly buried, each in its own state of ruin.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_pyramids.py -- assets/strange-attractor/build

Writes pyramid-a.glb, pyramid-b.glb and pyramid-c.glb: the Giza arrangement's three sizes. All geometry is made here
(no downloads), in metres, the base's centre at the origin on the ground (y up after export).

Each pyramid is a height field over a square grid that reaches past its base, so one surface carries the monument,
the sand banked against it and the ground it is sinking into:
- the casing: the true pyramid (51.8 degrees, Giza's), eroded by a noise, missing in patches where it has fallen;
- the core: courses of stone, stepped, showing wherever the casing is gone;
- the corners: broken back, more at the top;
- collapse: a slumped scar on one face (the oldest pyramid's is the largest);
- the sand: ramps against the base, deepest on the windward side, burying the lowest courses.

Three materials: the casing (pale ochre), the core (darker, redder: the courses' shadowed faces read on their own),
the sand (the plain's rust). The pyramid portal opens in pyramid-b's south face, where its casing has fallen.
"""
import bpy
import bmesh
import math
import random
import sys

out_dir = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)


def hash2(i, j, seed):
    h = (i * 374761393 + j * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)


def vnoise(x, y, seed):
    i, j = math.floor(x), math.floor(y)
    fx, fy = x - i, y - j
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash2(i, j, seed), hash2(i + 1, j, seed)
    c, d = hash2(i, j + 1, seed), hash2(i + 1, j + 1, seed)
    return (a + (b - a) * ux) * (1 - uy) + (c + (d - c) * ux) * uy


def fbm(x, y, seed, octaves=4):
    s, amp, f, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        s += amp * vnoise(x * f, y * f, seed + o * 17)
        norm += amp
        amp *= 0.5
        f *= 2.0
    return s / norm


def smooth(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def material(name, color, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    return m


CASING = material("pyramid_casing", (0.50, 0.33, 0.21), 0.92)
CORE = material("pyramid_core", (0.33, 0.19, 0.12), 0.95)
SAND = material("pyramid_sand", (0.46, 0.24, 0.13), 1.0)

SLOPE = math.tan(math.radians(51.8))

# name, base (m), state: casing kept (0..1), corner loss, collapse (face, size), sand depth (share of height), seed
PYRAMIDS = [
    ("pyramid-a", 300.0, dict(casing=0.55, corners=0.10, collapse=("east", 0.10), sand=0.10, seed=11)),
    ("pyramid-b", 280.0, dict(casing=0.70, corners=0.07, collapse=(None, 0.0), sand=0.08, seed=23)),
    ("pyramid-c", 140.0, dict(casing=0.25, corners=0.18, collapse=("north", 0.22), sand=0.16, seed=37)),
]


def build(name, base, st):
    half = base * 0.5
    H = half * SLOPE
    course = max(base / 90.0, 1.6)          # stone courses (m)
    reach = half * 1.45                      # the grid runs past the base into the sand and the plain
    step = max(base / 220.0, 0.8)            # grid spacing (m)
    n = int(2 * reach / step) + 1
    seed = st["seed"]

    def height(x, y):
        m = max(abs(x), abs(y))              # the square pyramid's distance
        pyr = H * (1.0 - m / half)
        # the core: courses (terraces), each a step back from the casing
        core_h = max(0.0, H * (1.0 - (m + course * 0.9) / half))
        core = math.floor(core_h / course) * course + course * smooth(0.82, 1.0, (core_h / course) % 1.0)
        # the casing, eroded; missing in patches (more toward the bottom and the corners)
        rough = (fbm(x / 9.0, y / 9.0, seed) - 0.5) * course * 0.6
        corner = min(abs(abs(x) - abs(y)) / max(m, 1e-3), 1.0)   # 0 on a corner's arris, 1 mid-face
        keep = fbm(x / 38.0, y / 38.0, seed + 3) + 0.35 * (pyr / H) - 0.25 * (1.0 - corner)
        casing_on = keep > (1.0 - st["casing"])
        h = pyr + rough if casing_on else core
        # broken corners: the arrises chewed back, worse up high
        bite = st["corners"] * H * (1.0 - smooth(0.0, 0.18, corner)) * (0.4 + 0.6 * pyr / H) * fbm(x / 20.0, y / 20.0, seed + 5)
        h -= bite
        # collapse: a slumped scar on one face
        face, size = st["collapse"]
        if face:
            cx, cy = {"east": (half * 0.45, 0.0), "north": (0.0, half * 0.45)}[face]
            r = math.hypot(x - cx, y - cy) / (base * size)
            if r < 1.0:
                h -= H * 0.35 * size * 4.0 * (1.0 - r * r) ** 2 * (0.7 + 0.3 * fbm(x / 7.0, y / 7.0, seed + 9))
        h = max(h, 0.0) if m < half else 0.0
        # the sand: ramps against the base, deeper to the windward (west), rippled
        wind = 0.6 + 0.4 * smooth(-1.0, 1.0, -x / half)
        out = max(m - half * 0.92, 0.0)
        sand = st["sand"] * H * wind * math.exp(-out / (half * 0.16)) * (0.75 + 0.5 * fbm(x / 25.0, y / 25.0, seed + 13))
        sand += 0.25 * math.sin(x * 0.9 + 2.0 * fbm(x / 30.0, y / 30.0, seed + 15)) * smooth(half * 0.95, half * 1.2, m)
        # the plain: the grid's edge sinks a little so the node can sit into the ground without a lip
        sink = -2.0 * smooth(reach * 0.85, reach, m)
        if sand + sink > h:
            return sand + sink, 2
        return h, (0 if casing_on else 1)

    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    for m_ in (CASING, CORE, SAND):
        mesh.materials.append(m_)
    bm = bmesh.new()
    verts, kinds = [], []
    for j in range(n):
        y = -reach + j * step
        for i in range(n):
            x = -reach + i * step
            h, k = height(x, y)
            verts.append(bm.verts.new((x, y, h)))
            kinds.append(k)
    for j in range(n - 1):
        for i in range(n - 1):
            a = j * n + i
            f = bm.faces.new((verts[a], verts[a + 1], verts[a + n + 1], verts[a + n]))  # wound to face up (+z)
            ks = [kinds[a], kinds[a + 1], kinds[a + n], kinds[a + n + 1]]
            f.material_index = 2 if ks.count(2) >= 2 else (1 if 1 in ks else 0)
    bm.to_mesh(mesh)
    bm.free()
    mesh.shade_flat()
    # decimate the flat plain and the smooth faces a little: the height field is dense everywhere
    dec = obj.modifiers.new("decimate", "DECIMATE")
    dec.decimate_type = "DISSOLVE"
    dec.angle_limit = math.radians(1.5)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier="decimate")
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    path = f"{out_dir}/{name}.glb"
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_apply=True)
    print("PYRAMID", path, "base", base, "height", round(H, 1), "triangles", tris)
    return obj


for name, base, st in PYRAMIDS:
    o = build(name, base, st)
    o.hide_set(True)
