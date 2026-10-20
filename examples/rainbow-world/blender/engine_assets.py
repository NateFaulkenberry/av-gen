"""Rainbow World: export every AV Gen asset the look-development scene needs, from the prototype scripts.

AV Gen reads glTF 2.0 with no vertex colours and one UV set, so the painterly colour that Blender computed from
attributes moves to AV Gen material programs (written by build_lookdev.py). Each asset here is exported plain:
geometry, custom normals, UVs, and only the textures the engine needs.

  band_section.glb            the 420 m band section (no rim rocks: those are instanced separately)
  rim_rocks.points.json       the rim rock placements (Y-up [px py pz qx qy qz qw sx sy sz]) per rock id
  rock_<id>.glb               the adapted rocks (geometry only; colour from a program)
  grass_lite.glb              the near-field grass clump (24 triangles, dome normals)
  great_tree_wood.glb / _leaves.glb / _core.glb   the tree; the core is the shrunk canopy hull, opaque, dark,
                              the canopy's shadow caster (the cards do not cast: ADR-1545)
  cloud_<kind>.glb            sculpted cloud meshes (hero, tower, bank, puff) from authored lobes
  rainbow_arch.glb            the arch: a front-faced tube, alpha-BLENDED, spectral emissive texture (U6)

Args: <outdir> <stage2 dir (holds rocks/rocks.blend)>
"""
import json
import math
import os
import random
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import band  # noqa: E402
import clouds  # noqa: E402
import rw_common as rw  # noqa: E402
import trees  # noqa: E402


def plain_material(name, rgb, rough=0.9):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = tuple(rgb[:3]) + (1.0,)
    b.inputs["Roughness"].default_value = rough
    return m


def export_band(out):
    rw.reset()
    ob, fr = band.build_band()
    ob.data.materials.append(plain_material("band_ground", rw.P["meadow"], 0.92))
    rw.export_glb([ob], os.path.join(out, "band_section.glb"))
    # the distant echoes' copy: same form at ~1/10 of the triangles
    decimate_to(ob, 34000)
    rw.export_glb([ob], os.path.join(out, "band_echo.glb"))
    return fr


def export_rim_rocks(out, s2):
    rw.reset()
    fr = band.frames()
    protos = band.append_rocks(os.path.join(s2, "rocks", "rocks.blend"))
    placed = band.place_rim_rocks(fr, protos)
    by = {}
    for o in placed:
        rid = o.data.name if False else o.name.split(".")[0]
        q = o.rotation_quaternion
        a = rw.to_avgen(o.location)
        w, x, y, z = rw.quat_to_avgen(q)
        # glTF export maps Blender (x, y, z) scale axes to (x, z, y) in Y-up
        sc = o.scale
        by.setdefault(rid, []).append([round(a[0], 3), round(a[1], 3), round(a[2], 3), round(x, 5), round(y, 5),
                                       round(z, 5), round(w, 5), round(sc[0], 3), round(sc[2], 3), round(sc[1], 3)])
    with open(os.path.join(out, "rim_rocks.points.json"), "w") as f:
        json.dump(by, f)
    # each adapted rock, geometry only, at its 1 m nominal size
    for p in protos:
        c = p.copy()
        c.data = p.data.copy()
        bpy.context.scene.collection.objects.link(c)
        c.location = (0, 0, 0)
        c.rotation_euler = (0, 0, 0)
        c.scale = (1, 1, 1)
        c.data.materials.clear()
        c.data.materials.append(plain_material("rock", rw.P["stone_warm"], 0.85))
        rw.export_glb([c], os.path.join(out, p.name.split(".")[0] + ".glb"))
        # the rim's copy: the same rock at ~300 triangles (rims are seen from tens to hundreds of metres; 2,000
        # of them at full detail cost ~3 M triangles a pass -- Stage 4's first world profile)
        decimate_to(c, 300)
        rw.export_glb([c], os.path.join(out, "rim" + p.name.split(".")[0] + ".glb"))
    print("RIM ROCKS", {k: len(v) for k, v in by.items()})


def export_grass(out):
    rw.reset()
    lite = band.grass_clump(name="grass_lite", blades=14, seed=5, segs=2)
    bpy.context.scene.collection.objects.link(lite)
    lite.data.materials.clear()
    lite.data.materials.append(plain_material("grass", rw.P["meadow"], 0.95))
    rw.export_glb([lite], os.path.join(out, "grass_lite.glb"))


def export_flowers(out):
    """A drift-sized clump of stylised flowers: thin stems and simple five-petal heads (a flat star of petals round a
    centre), heads at 0.35-0.7 m. One mesh; the material program colours heads by instance (accent palette) and stems
    green by height (objectPosition.y below the head line)."""
    import random as _r
    rw.reset()
    rng = _r.Random(9)
    me = bpy.data.meshes.new("flowers")
    bm = bmesh.new()
    for k in range(11):
        a, r = rng.uniform(0, 6.283), rng.uniform(0.0, 0.35)
        base = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        h = rng.uniform(0.3, 0.65)
        top = base + Vector((rng.uniform(-0.06, 0.06), rng.uniform(-0.06, 0.06), h))
        # stem: a thin triangular prism
        ring0 = [bm.verts.new(base + Vector((math.cos(t) * 0.012, math.sin(t) * 0.012, 0))) for t in (0, 2.09, 4.19)]
        ring1 = [bm.verts.new(top + Vector((math.cos(t) * 0.008, math.sin(t) * 0.008, 0))) for t in (0, 2.09, 4.19)]
        for i in range(3):
            bm.faces.new((ring0[i], ring0[(i + 1) % 3], ring1[(i + 1) % 3], ring1[i]))
        # head: five petals as a flat star tilted up
        c = bm.verts.new(top + Vector((0, 0, 0.01)))
        pr = rng.uniform(0.13, 0.22)
        outer = []
        for i in range(10):
            t = i * math.pi / 5
            rr = pr if i % 2 == 0 else pr * 0.42
            outer.append(bm.verts.new(top + Vector((math.cos(t) * rr, math.sin(t) * rr, 0.05 * rr / pr + 0.015 * (i % 2)))))
        for i in range(10):
            bm.faces.new((c, outer[i], outer[(i + 1) % 10]))
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("flowers", me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(plain_material("flowers", rw.P["magenta"], 0.6))
    rw.export_glb([ob], os.path.join(out, "flower_clump.glb"))


def metaball_mesh(name, ellipsoids, resolution, threshold=0.6):
    mb = bpy.data.metaballs.new(name)
    mb.resolution = resolution
    mb.render_resolution = resolution
    mb.threshold = threshold
    for c, r, sc in ellipsoids:
        e = mb.elements.new()
        e.type = "ELLIPSOID"
        e.co = c
        e.radius = r
        e.size_x, e.size_y, e.size_z = sc
    ob = bpy.data.objects.new(name, mb)
    bpy.context.scene.collection.objects.link(ob)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.convert(target="MESH")
    mesh = bpy.context.view_layer.objects.active
    mesh.name = name
    return mesh


def decimate_to(o, tris):
    n = sum(len(p.vertices) - 2 for p in o.data.polygons)
    if n > tris:
        d = o.modifiers.new("dec", "DECIMATE")
        d.ratio = tris / n
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_apply(modifier="dec")
    bpy.ops.object.shade_smooth()


def export_tree(out):
    rw.reset()
    rng = random.Random(17)
    wood, leaves = trees.build_tree(out, rng)
    trees.export_engine(wood, leaves, os.path.join(out, "great_tree.glb"))
    # the canopy core: the pads' union shrunk to 80%, opaque and dark -- it fills the see-through holes between
    # cards from inside and is the canopy's shadow caster
    core = metaball_mesh("great_tree_core", [(Vector(c), 1.0, (r[0] * 0.8, r[1] * 0.8, r[2] * 0.72)) for c, r, _ in trees.MASSES], 0.8)
    decimate_to(core, 1500)
    core.data.materials.append(plain_material("canopy_core", rw.P["canopy_core"], 0.9))
    rw.export_glb([core], os.path.join(out, "great_tree_core.glb"))


CLOUD_KINDS = {  # kind -> (seed, width, flatten z, triangles)
    "hero": (8, 620.0, 1.0, 30000),
    "tower": (21, 420.0, 1.25, 24000),
    "bank": (5, 1100.0, 0.55, 30000),
    "puff": (13, 300.0, 0.45, 8000),
}


def export_clouds(out):
    for kind, (seed, width, fz, tris) in CLOUD_KINDS.items():
        rw.reset()
        lobes = clouds.author_cloud(seed=seed, width=width)
        ell = [(Vector((c.x, c.y, c.z * fz)), r * 1.15, (sc[0], sc[1], sc[2] * fz)) for c, r, sc, _, _ in lobes]
        m = metaball_mesh("cloud_" + kind, ell, 5.0 if width < 700 else 8.0)
        # merge the cauliflower's spheres into billows: a voxel remesh then a gentle smooth
        bpy.context.view_layer.objects.active = m
        r = m.modifiers.new("remesh", "REMESH")
        r.mode = "VOXEL"
        r.voxel_size = width * 0.0055
        sm = m.modifiers.new("smooth", "SMOOTH")
        sm.factor = 0.5
        sm.iterations = 2
        bpy.ops.object.modifier_apply(modifier="remesh")
        bpy.ops.object.modifier_apply(modifier="smooth")
        decimate_to(m, tris)
        m.data.materials.append(plain_material("cloud", (1.0, 1.0, 1.0), 1.0))
        # origin at the floor's centre, so a placement puts the flat base where it says
        rw.export_glb([m], os.path.join(out, "cloud_%s.glb" % kind))
        dims = m.dimensions
        print("CLOUD", kind, len(m.data.polygons), [round(d) for d in dims])


def export_cloud_floor(out, radius=14000.0, n=360, seed=12):
    """The cloud sea as one continuous floor of billows under the whole world, so 'down' is never a flat colour:
    a disc of lumps (smooth union of a few hundred domes, 150-600 m across), flattening toward the rim, where it
    meets the sky's ground colour in the haze."""
    import random as _r
    rng = _r.Random(seed)
    # two octaves: big rolling billows (500-1600 m, 120-420 m high) and cauliflower heads on them (150-450 m)
    lumps = [(rng.uniform(-radius, radius), rng.uniform(-radius, radius), rng.uniform(500, 1600), rng.uniform(120, 420))
             for _ in range(420)]
    lumps += [(rng.uniform(-radius, radius), rng.uniform(-radius, radius), rng.uniform(150, 450), rng.uniform(80, 220))
              for _ in range(1600)]
    lumps = [l for l in lumps if math.hypot(l[0], l[1]) < radius]
    L_ = np.array(lumps)
    xs = np.linspace(-radius, radius, n)
    X, Y = np.meshgrid(xs, xs)
    Z = np.zeros_like(X)
    k = 60.0
    acc = np.zeros_like(X)
    for lx, ly, lr, lh in L_:
        d2 = ((X - lx) ** 2 + (Y - ly) ** 2) / (lr * lr)
        dome = lh * np.sqrt(np.clip(1.0 - d2, 0.0, None))
        acc += np.exp(dome / k)
    Z = k * np.log(acc)              # smooth max of the domes
    Z -= Z.min()
    r = np.hypot(X, Y) / radius
    Z *= np.clip(1.0 - r, 0.0, 1.0) ** 0.2   # flatten toward the rim (late: the far billows must still read)
    rw.reset()
    me = bpy.data.meshes.new("cloud_floor")
    verts = [(float(X[i, j]), float(Y[i, j]), float(Z[i, j])) for i in range(n) for j in range(n)]
    faces = []
    for i in range(n - 1):
        for j in range(n - 1):
            if r[i, j] > 1.0 and r[i + 1, j + 1] > 1.0:
                continue
            a_ = i * n + j
            faces.append((a_, a_ + 1, a_ + n + 1, a_ + n))
    me.from_pydata(verts, [], faces)
    me.update()
    for poly in me.polygons:
        poly.use_smooth = True
    ob = bpy.data.objects.new("cloud_floor", me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(plain_material("cloud", (1.0, 1.0, 1.0), 1.0))
    rw.export_glb([ob], os.path.join(out, "cloud_floor.glb"))
    print("CLOUD FLOOR", len(faces))


def spectral(t):
    s = rw.SPECTRUM
    for (t0, c0), (t1, c1) in zip(s, s[1:]):
        if t <= t1:
            f = (t - t0) / max(1e-9, t1 - t0)
            return [c0[i] + (c1[i] - c0[i]) * f for i in range(3)]
    return list(s[-1][1])


def rainbow_textures(out, reverse=False, alpha_peak=0.85, name="rainbow"):
    W, H = 256, 64
    emis = np.zeros((H, W, 4), np.float32)
    base = np.zeros((H, W, 4), np.float32)
    for x in range(W):
        u = (x + 0.5) / W
        c = spectral(1.0 - u if reverse else u)
        w = (u - 0.5) / 0.5
        a = alpha_peak * math.exp(-((abs(w) / 0.8) ** 4))
        for y in range(H):
            v = (y + 0.5) / H
            feet = min(1.0, v / 0.1) * min(1.0, (1.0 - v) / 0.1)
            emis[y, x, :3] = c
            emis[y, x, 3] = 1.0
            base[y, x, 3] = a * feet
    imgs = []
    for nm, arr in (("%s_emissive" % name, emis), ("%s_alpha" % name, base)):
        im = bpy.data.images.new(nm, W, H, alpha=True)
        im.pixels = arr.ravel()
        im.filepath_raw = os.path.join(out, nm + ".png")
        im.file_format = "PNG"
        im.save()
        imgs.append(im)
    return imgs


def tube_arc(name, R, m, arc_segs=240, ring=28, theta0=-0.08, theta1=math.pi + 0.08):
    """A tube round a semicircle in the XZ plane (Z up). UV u = radial position across the tube (0 inner, 1
    outer) so a front-faced tube seen face-on shows the spectrum in radial order; v = along the arc."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    vs = []
    for i in range(arc_segs + 1):
        th = theta0 + (theta1 - theta0) * i / arc_segs
        ring_v = []
        for j in range(ring):
            ph = 2 * math.pi * j / ring
            r = R + m * math.cos(ph)
            ring_v.append(bm.verts.new((r * math.cos(th), m * math.sin(ph), r * math.sin(th))))
        vs.append(ring_v)
    for i in range(arc_segs):
        for j in range(ring):
            j2 = (j + 1) % ring
            f = bm.faces.new((vs[i][j], vs[i + 1][j], vs[i + 1][j2], vs[i][j2]))
            for loop, (ii, jj) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j2), (i, j2))):
                ph = 2 * math.pi * jj / ring
                loop[uvl].uv = (0.5 + 0.5 * math.cos(ph), ii / arc_segs)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def rainbow_material(name, emis_img, alpha_img, strength):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0, 0, 0, 1)
    b.inputs["Roughness"].default_value = 1.0
    b.inputs["Specular IOR Level"].default_value = 0.0
    te = nt.nodes.new("ShaderNodeTexImage")
    te.image = emis_img
    ta = nt.nodes.new("ShaderNodeTexImage")
    ta.image = alpha_img
    nt.links.new(te.outputs["Color"], b.inputs["Emission Color"])
    b.inputs["Emission Strength"].default_value = strength
    nt.links.new(ta.outputs["Alpha"], b.inputs["Alpha"])
    m.use_backface_culling = True
    return m


def export_rainbow(out, strength=2.0):
    rw.reset()
    R = 115.0
    e1, a1 = rainbow_textures(out, name="rainbow_primary", alpha_peak=0.66)
    e2, a2 = rainbow_textures(out, reverse=True, name="rainbow_secondary", alpha_peak=0.22)
    p = tube_arc("rainbow_primary", R, 13.0)
    p.data.materials.append(rainbow_material("rainbow_primary", e1, a1, strength))
    s = tube_arc("rainbow_secondary", R * 1.24, 18.0)
    s.data.materials.append(rainbow_material("rainbow_secondary", e2, a2, strength * 0.5))
    rw.export_glb([p, s], os.path.join(out, "rainbow_arch.glb"))


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0])
    s2 = os.path.abspath(args[1])
    os.makedirs(out, exist_ok=True)
    only = set(args[2].split(",")) if len(args) > 2 else None
    steps = [("band", lambda: export_band(out)), ("rocks", lambda: export_rim_rocks(out, s2)),
             ("grass", lambda: export_grass(out)), ("flowers", lambda: export_flowers(out)),
             ("tree", lambda: export_tree(out)),
             ("clouds", lambda: export_clouds(out)), ("floor", lambda: export_cloud_floor(out)),
             ("rainbow", lambda: export_rainbow(out))]
    for name, fn in steps:
        if only is None or name in only:
            fn()
            print("STEP", name)
    print("done")


if __name__ == "__main__":
    main()
