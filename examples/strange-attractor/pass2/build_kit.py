"""The M3b prop kit (Art Pass 2): all geometry made here, in metres, nothing downloaded.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_kit.py -- assets/strange-attractor/build

Writes, each at the ground (y = 0 after export) unless noted:
- scrub-{a,b,c}.glb: desert scrub (rabbitbrush and sage), 0.6-1.4 m: twiggy branches, dusty olive and grey-green
  leaf clusters;
- rock-{a,b,c,d}.glb: canyon sandstone boulders 0.4-1.6 m, layered (their strata), rounded by wind;
- basalt-{a,b,c}.glb: dark Martian rocks, 0.3-1.2 m, vesicular and angular;
- seat-rock.glb: a flat-topped boulder 0.5 m high (a Sasquatch sits on it);
- cave-mouth.glb: a cave in the bank behind the camp: a rough rock surround 7 m wide, an opening 3.6 x 3 m and a
  throat 7 m deep into darkness (its front faces +z);
- laser-fan.glb: twelve laser beams from a hub, five colours, 40 m long, emissive (the domes' rave);
- kelp-{a,b}.glb, sponge.glb, fan-coral.glb, seagrass.glb: deep-water life for the shelf;
- flash.glb: a small, bright bulb (the rover's mast camera flash);
- rover-tracks.glb: two wheel ruts with chevron treads, 1 m of track per segment along +z (instanced along the path).
"""
import bmesh
import bpy
import math
import random
import sys

from mathutils import Matrix, Vector

out_dir = sys.argv[sys.argv.index("--") + 1]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color, rough=0.9, metal=0.0, emit=None, strength=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1.0)
        b.inputs["Emission Strength"].default_value = strength
    return m


def new_obj(name, bm, mats):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    for m in mats:
        me.materials.append(m)
    return o


def export(name, objs=None, smooth=True):
    for o in bpy.context.scene.objects:
        o.select_set(objs is None or o in objs)
    if smooth:
        for o in bpy.context.scene.objects:
            if o.type == "MESH" and o.select_get():
                for p in o.data.polygons:
                    p.use_smooth = True
    path = f"{out_dir}/{name}.glb"
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_apply=True)
    tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in bpy.context.scene.objects
               if o.type == "MESH" and o.select_get())
    print("KIT", path, tris, "triangles")


def vnoise3(p, seed):
    def h(i, j, k):
        x = (i * 73856093 ^ j * 19349663 ^ k * 83492791 ^ seed * 2654435761) & 0xFFFFFFFF
        x = ((x >> 13) ^ x) * 1274126177 & 0xFFFFFFFF
        return (x & 0xFFFF) / 65535.0
    i, j, k = math.floor(p.x), math.floor(p.y), math.floor(p.z)
    fx, fy, fz = p.x - i, p.y - j, p.z - k
    ux, uy, uz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    def lerp(a, b, t):
        return a + (b - a) * t
    c = [[[h(i + a, j + b, k + c) for c in (0, 1)] for b in (0, 1)] for a in (0, 1)]
    return lerp(lerp(lerp(c[0][0][0], c[0][0][1], uz), lerp(c[0][1][0], c[0][1][1], uz), uy),
                lerp(lerp(c[1][0][0], c[1][0][1], uz), lerp(c[1][1][0], c[1][1][1], uz), uy), ux)


def fbm3(p, seed, oct=4):
    s, a, f, n = 0.0, 1.0, 1.0, 0.0
    for o in range(oct):
        s += a * vnoise3(p * f, seed + o * 31)
        n += a
        a *= 0.5
        f *= 2.03
    return s / n


# ---------------------------------------------------------------------------------------------------------- rocks
def rock_mesh(seed, size, squash, layered, angular, sub=5):
    rnd = random.Random(seed)
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=1.0)
    for v in bm.verts:
        p = v.co.copy()
        n = fbm3(p * 1.6 + Vector((seed, 0, 0)), seed) - 0.5
        r = 1.0 + 0.45 * n
        if angular:
            r += 0.18 * (abs(fbm3(p * 3.2, seed + 7) - 0.5) * 2.0 - 0.4)
        q = p * r
        q.z *= squash
        if layered:
            # strata: the height stepped into ledges a few cm proud of each other
            band = 0.22
            t = (q.z + 2.0) / band
            q.x *= 1.0 - 0.06 * (0.5 + 0.5 * math.cos(math.pi * 2.0 * t))
            q.y *= 1.0 - 0.06 * (0.5 + 0.5 * math.cos(math.pi * 2.0 * t))
        v.co = q * size
    zmin = min(v.co.z for v in bm.verts)
    for v in bm.verts:
        v.co.z -= zmin + 0.12 * size * squash   # sunk into the ground a little
    return bm


def build_rocks():
    reset()
    sand = [material("sandstone_a", (0.36, 0.17, 0.1), 0.92), material("sandstone_b", (0.44, 0.24, 0.14), 0.9),
            material("sandstone_c", (0.28, 0.13, 0.08), 0.95)]
    for k, (nm, size, sq) in enumerate((("rock-a", 0.5, 0.75), ("rock-b", 0.9, 0.7), ("rock-c", 1.4, 0.62), ("rock-d", 0.3, 0.85))):
        o = new_obj(nm, rock_mesh(101 + k, size, sq, True, False), [sand[k % 3]])
        export(nm, [o])
        bpy.data.objects.remove(o)
    o = new_obj("seat-rock", rock_mesh(131, 0.62, 0.62, True, False), [sand[0]])
    bm = bmesh.new()
    bm.from_mesh(o.data)
    top = max(v.co.z for v in bm.verts)
    for v in bm.verts:   # a flat top at 0.5 m
        if v.co.z > 0.5:
            v.co.z = 0.5 + (v.co.z - 0.5) * 0.08
    bm.to_mesh(o.data)
    bm.free()
    export("seat-rock", [o])
    bpy.data.objects.remove(o)
    basalt = [material("basalt_a", (0.12, 0.07, 0.055), 0.85), material("basalt_b", (0.17, 0.09, 0.06), 0.88)]
    for k, (nm, size, sq) in enumerate((("basalt-a", 0.35, 0.7), ("basalt-b", 0.8, 0.6), ("basalt-c", 1.2, 0.55))):
        o = new_obj(nm, rock_mesh(201 + k, size, sq, False, True), [basalt[k % 2]])
        export(nm, [o])
        bpy.data.objects.remove(o)


# ---------------------------------------------------------------------------------------------------------- scrub
def build_scrub():
    for k, (nm, height, leaf_col, twig_col) in enumerate((
            ("scrub-a", 0.9, (0.2, 0.22, 0.12), (0.18, 0.13, 0.09)),
            ("scrub-b", 1.35, (0.24, 0.25, 0.17), (0.2, 0.15, 0.1)),
            ("scrub-c", 0.6, (0.17, 0.2, 0.15), (0.16, 0.12, 0.09)))):
        reset()
        rnd = random.Random(300 + k)
        twig = material("scrub_twig", twig_col, 0.95)
        leaf = material("scrub_leaf", leaf_col, 0.85)
        bt = bmesh.new()
        bl = bmesh.new()
        def branch(base, direction, length, radius, depth):
            end = base + direction * length
            mat = Matrix.Translation(base) @ direction.to_track_quat("Z", "Y").to_matrix().to_4x4()
            geom = bmesh.ops.create_cone(bt, cap_ends=False, segments=5, radius1=radius, radius2=radius * 0.6, depth=length,
                                         matrix=mat @ Matrix.Translation((0, 0, length / 2)))
            if depth == 0:
                # many small leaf clusters along the last twigs (rabbitbrush and sage read as fine, dusty texture)
                for j in range(rnd.randint(14, 22)):
                    c = base + direction * length * rnd.uniform(0.2, 1.05) + Vector((rnd.uniform(-1, 1) for _ in range(3))) * 0.05 * height
                    r = rnd.uniform(0.016, 0.034) * height
                    rot = Matrix.Rotation(rnd.uniform(0, 6.28), 4, "Z") @ Matrix.Rotation(rnd.uniform(-0.6, 0.6), 4, "X")
                    bmesh.ops.create_icosphere(bl, subdivisions=1, radius=r,
                                               matrix=Matrix.Translation(c) @ rot @ Matrix.Diagonal((1.0, 0.45, 1.6, 1.0)))
                return
            for j in range(rnd.randint(2, 4)):
                d = (direction + Vector((rnd.uniform(-0.7, 0.7), rnd.uniform(-0.7, 0.7), rnd.uniform(0.0, 0.5)))).normalized()
                branch(base + direction * length * rnd.uniform(0.45, 1.0), d, length * rnd.uniform(0.5, 0.75), radius * 0.6, depth - 1)
        for j in range(rnd.randint(7, 11)):
            a = rnd.uniform(0, 2 * math.pi)
            d = Vector((math.cos(a) * rnd.uniform(0.3, 0.8), math.sin(a) * rnd.uniform(0.3, 0.8), 1.0)).normalized()
            branch(Vector((rnd.uniform(-0.05, 0.05), rnd.uniform(-0.05, 0.05), 0.0)), d, height * rnd.uniform(0.35, 0.5), 0.018, 2)
        ot = new_obj(nm + "-twig", bt, [twig])
        ol = new_obj(nm + "-leaf", bl, [leaf])
        export(nm, [ot, ol])


# ---------------------------------------------------------------------------------------------------------- cave
def build_cave():
    reset()
    rock = material("cave_rock", (0.3, 0.14, 0.085), 0.93)
    dark = material("cave_dark", (0.012, 0.008, 0.006), 1.0)
    bm = bmesh.new()
    # the surround: a lumpy mass 7 x 5 x 4 m, its front at y = 0 (Blender -y = glTF +z: out of the bank)
    bmesh.ops.create_icosphere(bm, subdivisions=4, radius=1.0)
    for v in bm.verts:
        p = v.co.copy()
        r = 1.0 + 0.35 * (fbm3(p * 1.8, 7) - 0.5) + 0.12 * (fbm3(p * 5.0, 9) - 0.5)
        v.co = Vector((p.x * 4.8 * r, p.y * 3.0 * r + 2.6, max(p.z, -0.25) * 4.2 * r + 0.6))
    # the opening: push the front verts near the arch back into the throat
    for v in bm.verts:
        x, z = v.co.x, v.co.z
        inside = (x / 1.6) ** 2 + ((z - 0.2) / 2.7) ** 2
        if v.co.y < 1.5 and inside < 1.0 and z > -0.2:
            v.co.y += 7.0 * (1.0 - inside) ** 0.5
    o = new_obj("cave-surround", bm, [rock])
    # the throat: a dark tube receding (the inside never lit)
    bt = bmesh.new()
    bmesh.ops.create_cone(bt, cap_ends=False, segments=16, radius1=1.6, radius2=1.1, depth=7.0,
                          matrix=Matrix.Translation((0, 5.2, 1.3)) @ Matrix.Rotation(-math.pi / 2, 4, "X") @ Matrix.Diagonal((1.0, 1.6, 1.0, 1.0)))
    bmesh.ops.create_circle(bt, cap_ends=True, segments=16, radius=1.1,
                            matrix=Matrix.Translation((0, 8.7, 1.3)) @ Matrix.Rotation(math.pi / 2, 4, "X") @ Matrix.Diagonal((1.0, 1.6, 1.0, 1.0)))
    bmesh.ops.reverse_faces(bt, faces=bt.faces[:])
    ot = new_obj("cave-throat", bt, [dark])
    for ob in (o, ot):
        ob.data.transform(Matrix.Rotation(math.pi, 4, "Z"))   # the front toward -y (glTF +z)
    export("cave-mouth", [o, ot])


# ---------------------------------------------------------------------------------------------------------- lasers
def build_lasers():
    reset()
    cols = [(1.0, 0.1, 0.3), (0.1, 1.0, 0.3), (0.2, 0.4, 1.0), (1.0, 0.2, 1.0), (0.1, 1.0, 1.0)]
    mats = [material(f"laser_{i}", (0.1, 0.1, 0.1), 0.5, 0.0, c, 30.0) for i, c in enumerate(cols)]
    objs = []
    for i in range(12):
        bm = bmesh.new()
        a = 2 * math.pi * i / 12
        el = math.radians(28 + 22 * ((i * 7) % 5) / 4.0)
        d = Vector((math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el)))
        mat = d.to_track_quat("Z", "Y").to_matrix().to_4x4()
        bmesh.ops.create_cone(bm, cap_ends=True, segments=6, radius1=0.06, radius2=0.18, depth=40.0,
                              matrix=mat @ Matrix.Translation((0, 0, 20.0)))
        o = new_obj(f"beam{i}", bm, [mats[i % 5]])
        objs.append(o)
    bh = bmesh.new()
    bmesh.ops.create_icosphere(bh, subdivisions=2, radius=0.6)
    objs.append(new_obj("hub", bh, [mats[2]]))
    export("laser-fan", objs, smooth=False)


# ---------------------------------------------------------------------------------------------------------- sea life
def ribbon(bm, base, height, width, sway, seed, segs=24):
    rnd = random.Random(seed)
    prev = None
    ph = rnd.uniform(0, 6.28)
    for k in range(segs + 1):
        t = k / segs
        z = height * t
        x = base.x + sway * math.sin(ph + t * 4.0) * t
        y = base.y + sway * 0.5 * math.cos(ph + t * 3.0) * t
        w = width * (0.4 + 0.6 * math.sin(math.pi * min(1.0, t * 1.2 + 0.1)))
        a = bm.verts.new((x - w, y, base.z + z))
        b = bm.verts.new((x + w, y, base.z + z))
        if prev:
            bm.faces.new((prev[0], prev[1], b, a))
        prev = (a, b)


def build_sea():
    reset()
    kelp = material("kelp", (0.16, 0.13, 0.05), 0.7)
    for nm, n, hmax, seed in (("kelp-a", 14, 11.0, 401), ("kelp-b", 8, 6.0, 402)):
        rnd = random.Random(seed)
        bm = bmesh.new()
        for j in range(n):
            ribbon(bm, Vector((rnd.uniform(-1.5, 1.5), rnd.uniform(-1.5, 1.5), 0)), hmax * rnd.uniform(0.6, 1.0),
                   rnd.uniform(0.12, 0.22), rnd.uniform(0.4, 1.0), seed * 10 + j)
        o = new_obj(nm, bm, [kelp])
        export(nm, [o])
        bpy.data.objects.remove(o)
    # a barrel sponge: a lumpy open cylinder, orange
    sp = material("sponge", (0.55, 0.25, 0.06), 0.9)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=False, segments=24, radius1=0.55, radius2=0.8, depth=1.4,
                          matrix=Matrix.Translation((0, 0, 0.7)))
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=3, use_grid_fill=False)
    for v in bm.verts:
        v.co += Vector((v.co.x, v.co.y, 0)).normalized() * 0.08 * (fbm3(v.co * 3.0, 5) - 0.5) * 2.0
    inner = bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=0.45, radius2=0.7, depth=1.3,
                                  matrix=Matrix.Translation((0, 0, 0.72)))
    o = new_obj("sponge", bm, [sp])
    export("sponge", [o])
    bpy.data.objects.remove(o)
    # a sea fan: a flat branching net, deep red
    fc = material("fan_coral", (0.35, 0.05, 0.05), 0.8)
    rnd = random.Random(450)
    bm = bmesh.new()
    def twig(a, d, L, depth):
        b = a + d * L
        mat = Matrix.Translation(a) @ d.to_track_quat("Z", "Y").to_matrix().to_4x4()
        bmesh.ops.create_cone(bm, cap_ends=False, segments=4, radius1=0.03 + 0.01 * depth, radius2=0.025, depth=L,
                              matrix=mat @ Matrix.Translation((0, 0, L / 2)))
        if depth > 0:
            for s in (-1, 1):
                nd = (d + Vector((s * rnd.uniform(0.3, 0.7), 0, rnd.uniform(-0.1, 0.2)))).normalized()
                twig(b, nd, L * 0.72, depth - 1)
    twig(Vector((0, 0, 0)), Vector((0, 0, 1)), 0.7, 5)
    o = new_obj("fan-coral", bm, [fc])
    export("fan-coral", [o])
    bpy.data.objects.remove(o)
    # sea grass: a clump of thin blades
    sg = material("seagrass", (0.12, 0.16, 0.07), 0.75)
    rnd = random.Random(470)
    bm = bmesh.new()
    for j in range(40):
        ribbon(bm, Vector((rnd.uniform(-0.6, 0.6), rnd.uniform(-0.6, 0.6), 0)), rnd.uniform(0.5, 1.2), 0.025, 0.2, 470 + j, 8)
    o = new_obj("seagrass", bm, [sg])
    export("seagrass", [o])


# ---------------------------------------------------------------------------------------------------------- tracks
def build_tracks():
    reset()
    rut = material("rut", (0.22, 0.1, 0.05), 1.0)
    bm = bmesh.new()
    gauge = 2.6
    for side in (-1, 1):
        x0 = side * gauge / 2
        for k in range(10):          # 10 cm chevrons along a 1 m segment
            z0 = k * 0.1
            for (xa, za), (xb, zb) in (((-0.22, 0.0), (0.0, 0.05)), ((0.0, 0.05), (0.22, 0.0))):
                v = [bm.verts.new((x0 + xa, z0 + za, 0.015)), bm.verts.new((x0 + xb, z0 + zb, 0.015)),
                     bm.verts.new((x0 + xb, z0 + zb + 0.04, 0.015)), bm.verts.new((x0 + xa, z0 + za + 0.04, 0.015))]
                bm.faces.new(v)
        v = [bm.verts.new((x0 - 0.25, 0.0, 0.006)), bm.verts.new((x0 + 0.25, 0.0, 0.006)),
             bm.verts.new((x0 + 0.25, 1.0, 0.006)), bm.verts.new((x0 - 0.25, 1.0, 0.006))]
        bm.faces.new(v)
    o = new_obj("rover-tracks", bm, [rut])
    # Blender +y -> glTF -z: turn so a segment runs along glTF +z
    o.data.transform(Matrix.Rotation(math.pi, 4, "Z"))
    export("rover-tracks", [o], smooth=False)


def build_flash():
    reset()
    m = material("flash", (1.0, 1.0, 1.0), 0.3, 0.0, (1.0, 0.97, 0.9), 60.0)
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=2, radius=0.07)
    export("flash", [new_obj("flash", bm, [m])])


which = sys.argv[sys.argv.index("--") + 2:] or ["rocks", "scrub", "cave", "lasers", "sea", "tracks", "flash"]
for w in which:
    {"rocks": build_rocks, "scrub": build_scrub, "cave": build_cave, "lasers": build_lasers, "sea": build_sea,
     "tracks": build_tracks, "flash": build_flash}[w]()
