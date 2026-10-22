"""The Living Prism: every mesh the grotto uses, generated (Blender 5.x, background).

    Blender -b --factory-startup --python-exit-code 1 -P examples/living-prism/blender/prism_assets.py -- <out dir>
        [--preview <dir>]    also compose the whole grotto from layout.py and render Workbench stills from the
                             camera stations (layout checks without the engine; not the look)

Writes <out>/<asset>.glb. Each asset is built at its own origin, base at Blender z = 0 (AV Gen y = 0), its front
facing Blender -Y (AV Gen +Z); the shell is built in place (identity placement). Crystals and their rock are separate
assets so each gets its own engine material: `<formation>_xtal.glb` and `<formation>_rock.glb`.

The visual language (docs/prototypes/living-prism/05-stage1-plan.md):
  - crystal: hexagonal prisms with six-faced terminations, flat-shaded so every facet is its own plane of light;
    irregular radii, oblique tips and tabular (flattened) blades so no two formations share a silhouette;
  - rock: dark, faceted (decimated displacement), the same planar language as the crystal at a coarser scale.
Deterministic: every random draw is seeded.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import layout as L  # noqa: E402


def argv():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = a[0]
    preview = a[a.index("--preview") + 1] if "--preview" in a else None
    return out, preview


def av2bl(v):
    """AV Gen (x, y up, z) -> Blender (x, -z, y up)."""
    return Vector((v[0], -v[2], v[1]))


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


MATS = {}


def mat(name, rgb):
    if name not in MATS:
        m = bpy.data.materials.new(name)
        m.diffuse_color = (*rgb, 1.0)
        m.use_nodes = True
        m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rgb, 1.0)
        MATS[name] = m
    return MATS[name]


ROCK_RGB = (0.035, 0.03, 0.05)
XTAL_RGB = {"spire": (0.55, 0.2, 0.9), "fan": (0.2, 0.35, 1.0), "geode": (1.0, 0.25, 0.6),
            "chandelier": (0.6, 0.4, 1.0), "ember": (1.0, 0.45, 0.12), "arch": (0.7, 0.3, 1.0),
            "druse": (0.75, 0.35, 0.95), "shard": (0.5, 0.5, 1.0)}


def new_obj(name, bm, material):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.materials.append(material)
    return ob


def basis(axis):
    a = Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(a.z) < 0.9 else Vector((1, 0, 0))
    u = a.cross(ref).normalized()
    v = a.cross(u).normalized()
    return u, v, a


def prism(bm, base, axis, radius, length, tip, rng, sides=6, jitter=0.12, flat=1.0, taper=0.9, oblique=0.25,
          embed=0.15, spin=None):
    """One crystal: a prism of `sides` with a pointed termination, its base sunk `embed` x length below `base`."""
    u, v, a = basis(axis)
    base = Vector(base)
    spin = rng.uniform(0, math.tau) if spin is None else spin
    rs = [radius * (1.0 + rng.uniform(-jitter, jitter)) for _ in range(sides)]

    def ring(center, scale):
        vs = []
        for i in range(sides):
            ang = spin + math.tau * i / sides
            d = u * (math.cos(ang) * rs[i] * scale * flat) + v * (math.sin(ang) * rs[i] * scale)
            vs.append(bm.verts.new(center + d))
        return vs

    r0 = ring(base - a * (embed * length), 1.0)
    r1 = ring(base + a * length, taper)
    off = (u * rng.uniform(-1, 1) + v * rng.uniform(-1, 1)) * radius * oblique
    apex = bm.verts.new(base + a * (length + tip) + off)
    for i in range(sides):
        j = (i + 1) % sides
        bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
        bm.faces.new((r1[i], r1[j], apex))
    bm.faces.new(list(reversed(r0)))


def stepped_prism(bm, base, axis, radius, length, rng, steps=3):
    """A stout prism with terraced growth plates down one side and a low, truncated termination (the ember)."""
    u, v, a = basis(axis)
    prism(bm, base, axis, radius, length, radius * 0.28, rng, jitter=0.05, taper=0.97, oblique=0.6, embed=0.25)
    side = u * 0.55 + v * 0.2
    for k in range(steps):
        f = 1.0 - (k + 1) / (steps + 1.5)
        b = Vector(base) + side.normalized() * radius * (0.55 + 0.25 * k) + a * (length * 0.1 * k)
        prism(bm, b, (a + side * 0.15 * k).normalized(), radius * f * 0.8, length * (0.85 - 0.2 * k),
              radius * 0.2, rng, jitter=0.06, taper=0.95, oblique=0.5, embed=0.4)


def apply_mods(ob):
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def facet(ob, strength, size, ratio=None, faces=None, voxel=None, seed=0, kind="VORONOI"):
    """Dark faceted rock: (optional voxel remesh) -> displaced by a noise texture -> decimated to big planar facets."""
    if voxel:
        m = ob.modifiers.new("remesh", "REMESH")
        m.mode = "VOXEL"
        m.voxel_size = voxel
        sm = ob.modifiers.new("smooth", "SMOOTH")  # the voxel staircase would survive decimation as square facets
        sm.factor = 1.0
        sm.iterations = 25
        apply_mods(ob)
    tex = bpy.data.textures.new(ob.name + "_tex", kind if kind != "CLOUDS" else "CLOUDS")
    if kind == "VORONOI":
        tex.distance_metric = "DISTANCE"
        tex.noise_intensity = 1.0
    tex.noise_scale = size
    d = ob.modifiers.new("disp", "DISPLACE")
    d.texture = tex
    d.strength = strength
    d.mid_level = 0.5
    d.texture_coords = "LOCAL"
    # a seed: shift the texture space so variants differ
    ob.modifiers["disp"].texture_coords_object = None
    apply_mods(ob)
    me = ob.data
    me.transform(Matrix.Translation((0, 0, 0)))
    if faces or ratio:
        dec = ob.modifiers.new("dec", "DECIMATE")
        dec.decimate_type = "COLLAPSE"
        n = len(me.polygons)
        dec.ratio = min(1.0, (faces / max(n, 1)) if faces else ratio)
        apply_mods(ob)
    for p in ob.data.polygons:
        p.use_smooth = False


def ico(name, radius, scale=(1, 1, 1), loc=(0, 0, 0), subdiv=4):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=radius)
    for v in bm.verts:
        v.co = Vector((v.co.x * scale[0], v.co.y * scale[1], v.co.z * scale[2])) + Vector(loc)
    return new_obj(name, bm, mat("rock", ROCK_RGB))


def rock_lump(name, radius, flat, seed, faces=260, scale=(1, 1, 1), loc=(0, 0, 0)):
    ob = ico(name, radius, (scale[0], scale[1], scale[2] * flat), loc)
    random.seed(seed)
    ob.location = (0, 0, 0)
    for v in ob.data.vertices:  # a seeded lumpiness before the texture, so variants differ in silhouette
        n = v.co.normalized()
        v.co += n * radius * 0.18 * math.sin(3.1 * n.x + seed) * math.cos(2.3 * n.y - seed * 0.7)
    facet(ob, radius * 0.35, radius * 0.7, faces=faces, seed=seed)
    return ob


def join(obs, name):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = name
    return ob


def export(ob, out):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    path = os.path.join(out, ob.name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_normals=True, export_materials="EXPORT")
    print("EXPORTED %s tris=%d" % (path, sum(len(p.vertices) - 2 for p in ob.data.polygons)))
    return path


def xtal_obj(name, rgb_key, build):
    bm = bmesh.new()
    build(bm)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return new_obj(name, bm, mat("xtal_" + rgb_key, XTAL_RGB[rgb_key]))


# ------------------------------------------------------------------------------------------------- formations
def build_spire(out):
    """The primary: a tall fan of prisms, one dominant (6 m), the rest splayed outward and back."""
    rng = random.Random(11)

    def b(bm):
        prism(bm, (0, 0, 0.6), (0.03, 0.02, 1), 0.55, 5.0, 1.1, rng, jitter=0.08, oblique=0.15)
        spec = [(-0.9, 0.3, 3.6, 0.42, -0.35, 0.1), (0.85, 0.2, 3.1, 0.38, 0.4, 0.05), (-0.3, 0.75, 4.1, 0.34, -0.1, 0.35),
                (0.45, 0.8, 2.6, 0.3, 0.25, 0.4), (-1.25, -0.25, 2.0, 0.3, -0.6, -0.15), (1.3, -0.35, 1.7, 0.26, 0.65, -0.2),
                (0.0, -0.75, 1.3, 0.24, 0.05, -0.6), (-0.65, -0.55, 1.0, 0.18, -0.4, -0.5), (1.6, 0.6, 1.2, 0.2, 0.8, 0.3)]
        for x, y, ln, r, lx, ly in spec:
            prism(bm, (x, y, 0.35), (lx, ly, 1.0), r, ln, r * 1.8, rng)
    export(xtal_obj("spire_xtal", "spire", b), out)
    export(rock_lump("spire_rock", 2.0, 0.45, 3, faces=420, scale=(1.1, 0.9, 1)), out)


def build_fan(out):
    """Tabular blades leaning toward +X over the water: flattened prisms in a loose fan."""
    rng = random.Random(23)

    def b(bm):
        spec = [(0.0, 0.0, 5.2, 0.55, 55, 0), (0.2, 0.5, 4.2, 0.45, 48, 18), (-0.2, -0.5, 3.9, 0.48, 60, -16),
                (0.5, 0.1, 2.8, 0.36, 40, 6), (-0.4, 0.3, 2.3, 0.34, 66, 30), (0.1, -0.2, 1.6, 0.28, 30, -35)]
        for x, y, ln, r, tilt, az in spec:
            t, z = math.radians(tilt), math.radians(az)
            ax = (math.sin(t) * math.cos(z), math.sin(t) * math.sin(z), math.cos(t))
            prism(bm, (x, y, 0.4), ax, r, ln, r * 1.5, rng, flat=0.32, jitter=0.06, oblique=0.5)
    export(xtal_obj("fan_xtal", "fan", b), out)
    export(rock_lump("fan_rock", 1.8, 0.55, 5, faces=380, scale=(1.0, 1.2, 1)), out)


def build_geode(out):
    """A split geode: a thick rock half-shell open to the front (-Y), its inside lined with small prisms."""
    R, T = 1.9, 0.35
    rng = random.Random(31)
    # the shell: an icosphere, the front cap removed, given thickness, then faceted
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=4, radius=R)
    cut = [f for f in bm.faces if f.calc_center_median().y < -R * 0.25]
    bmesh.ops.delete(bm, geom=cut, context="FACES")
    for v in bm.verts:
        v.co.z += R * 0.8
    shell = new_obj("geode_rock", bm, mat("rock", ROCK_RGB))
    s = shell.modifiers.new("solid", "SOLIDIFY")
    s.thickness = T
    s.offset = 1.0
    apply_mods(shell)
    facet(shell, 0.28, 0.9, faces=900)
    export(shell, out)

    def b(bm2):
        n = 260
        for i in range(n):  # Fibonacci points on the inner surface's back hemisphere, pointing to the centre
            yv = 1 - 2 * (i + 0.5) / n
            r = math.sqrt(max(0.0, 1 - yv * yv))
            th = math.pi * (3 - math.sqrt(5)) * i
            d = Vector((math.cos(th) * r, yv, math.sin(th) * r))
            if d.y < -0.2:
                continue
            p = d * (R - 0.02) + Vector((0, 0, R * 0.8))
            ln = rng.uniform(0.18, 0.5)
            prism(bm2, p, -d + Vector((rng.uniform(-.2, .2), rng.uniform(-.2, .2), rng.uniform(-.2, .2))),
                  rng.uniform(0.05, 0.11), ln, ln * 0.35, rng, embed=0.3)
    export(xtal_obj("geode_xtal", "geode", b), out)


def build_chandelier(out):
    """Built upright (the knob at z = 0, prisms rising); the scene flips it to hang from the ceiling."""
    rng = random.Random(41)

    def b(bm):
        spec = [(0, 0, 4.4, 0.46, 0, 0), (0.6, 0.2, 3.3, 0.32, 0.3, 0.1), (-0.55, 0.3, 3.7, 0.34, -0.25, 0.12),
                (0.2, -0.6, 2.7, 0.28, 0.1, -0.3), (-0.35, -0.45, 2.2, 0.24, -0.2, -0.3), (0.85, -0.3, 1.8, 0.22, 0.5, -0.2),
                (-0.9, -0.1, 1.5, 0.2, -0.55, 0.0), (0.4, 0.75, 1.2, 0.18, 0.3, 0.5)]
        for x, y, ln, r, lx, ly in spec:
            prism(bm, (x, y, 0.2), (lx, ly, 1.0), r, ln, r * 2.0, rng)
    export(xtal_obj("chandelier_xtal", "chandelier", b), out)
    export(rock_lump("chandelier_rock", 1.6, 0.6, 7, faces=320), out)


def build_ember(out):
    rng = random.Random(53)

    def b(bm):
        stepped_prism(bm, (0, 0, 0.0), (0.0, 0.0, 1.0), 0.95, 1.9, rng, steps=3)
        prism(bm, (-0.9, 0.4, 0.0), (-0.5, 0.2, 1.0), 0.35, 0.9, 0.3, rng, oblique=0.5)
    export(xtal_obj("ember_xtal", "ember", b), out)
    export(rock_lump("ember_rock", 1.4, 0.4, 9, faces=300), out)


def build_arch(out):
    """A natural rock bridge spanning ~11 m, crown ~7.5 m, thick at the legs; small crystal on its underside."""
    cu = bpy.data.curves.new("arch_curve", "CURVE")
    cu.dimensions = "3D"
    sp = cu.splines.new("BEZIER")
    pts = [(-5.6, 0.3, -0.5, 1.5), (-4.9, 0.0, 3.6, 1.05), (-2.6, -0.3, 6.6, 0.8), (0.3, 0.0, 7.6, 0.72),
           (3.0, 0.3, 6.7, 0.8), (5.0, 0.2, 3.8, 1.1), (5.7, -0.1, -0.5, 1.6)]
    sp.bezier_points.add(len(pts) - 1)
    for bp, (x, y, z, r) in zip(sp.bezier_points, pts):
        bp.co = (x, y, z)
        bp.handle_left_type = bp.handle_right_type = "AUTO"
        bp.radius = r
    cu.bevel_depth = 1.0
    cu.use_fill_caps = True
    cu.bevel_resolution = 4
    cu.resolution_u = 16
    ob = bpy.data.objects.new("arch_rock", cu)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.convert(target="MESH")
    ob = bpy.context.view_layer.objects.active
    ob.name = "arch_rock"
    ob.data.materials.clear()
    ob.data.materials.append(mat("rock", ROCK_RGB))
    facet(ob, 0.55, 1.1, faces=1600, voxel=0.12)
    export(ob, out)
    # druse on the underside: faces facing down, area-weighted picks
    rng = random.Random(61)
    me = ob.data
    # the intrados only: faces that look down and in toward the span's centre, densest at the crown
    down = [p for p in me.polygons if p.normal.z < -0.45 and p.center.z > 2.0]
    w = [p.area * (0.4 + p.center.z / 7.6) for p in down]

    def b(bm):
        for _ in range(95):
            p = rng.choices(down, weights=w)[0]
            n = Vector(p.normal) + Vector((rng.uniform(-.25, .25), rng.uniform(-.25, .25), rng.uniform(-.2, .05)))
            ln = rng.uniform(0.14, 0.5)
            prism(bm, p.center, n, rng.uniform(0.05, 0.12), ln, ln * 0.4, rng, embed=0.35)
    export(xtal_obj("arch_xtal", "arch", b), out)


def build_druses(out):
    for k, seed in (("A", 71), ("B", 73), ("C", 79)):
        rng = random.Random(seed)
        n = {"A": 9, "B": 13, "C": 7}[k]

        def b(bm, rng=rng, n=n):
            for i in range(n):
                ang = rng.uniform(0, math.tau)
                rr = rng.uniform(0, 0.45)
                x, y = math.cos(ang) * rr, math.sin(ang) * rr
                ax = (x * 1.2 + rng.uniform(-.2, .2), y * 1.2 + rng.uniform(-.2, .2), 1.0)
                ln = rng.uniform(0.3, 1.25) * (1.0 - rr)
                prism(bm, (x, y, 0.15), ax, rng.uniform(0.07, 0.17), ln, ln * 0.35, rng)
        export(xtal_obj("druse%s_xtal" % k, "druse", b), out)
        export(rock_lump("druse%s_rock" % k, 0.7, 0.45, seed, faces=140), out)


def build_shards(out):
    for k, seed, (r, ln) in (("A", 81, (0.12, 0.8)), ("B", 83, (0.16, 1.1)), ("C", 89, (0.09, 0.6))):
        rng = random.Random(seed)
        export(xtal_obj("shard%s_xtal" % k, "shard",
                        lambda bm, rng=rng, r=r, ln=ln: prism(bm, (0, 0, 0), (0, 0, 1), r, ln, r * 2.2, rng)), out)


def build_boulders(out):
    for k, seed, sc in (("A", 91, (1, 0.9, 0.7)), ("B", 93, (1.3, 0.8, 0.6)), ("C", 97, (0.9, 1.0, 0.95)),
                        ("D", 99, (1.2, 1.1, 0.55))):
        ob = rock_lump("boulder" + k, 1.0, 1.0, seed, faces=220, scale=sc)
        for v in ob.data.vertices:
            v.co.z += 0.35
        export(ob, out)


def build_shell(out):
    """The grotto: the surface of the 'air' volume (chamber + alcoves + passage), faceted, normals inward."""
    parts = []
    c, r = L.CHAMBER["center"], L.CHAMBER["radii"]
    parts.append(ico("air_chamber", 1.0, (r[0], r[2], r[1]), av2bl(c), subdiv=5))
    for i, (cc, rr) in enumerate(L.ALCOVES):
        parts.append(ico("air_alcove%d" % i, rr, (1, 1, 0.85), av2bl(cc), subdiv=4))
    for i, ((p0, r0), (p1, r1)) in enumerate(zip(L.PASSAGE, L.PASSAGE[1:])):
        a, b = av2bl(p0), av2bl(p1)
        n = int((b - a).length / 1.0) + 2
        for k in range(n):
            t = k / (n - 1)
            parts.append(ico("air_p%d_%d" % (i, k), r0 + (r1 - r0) * t, (1, 1, 0.9), a.lerp(b, t), subdiv=3))
    air = join(parts, "shell")
    facet(air, 1.1, 2.6, faces=16000, voxel=0.4)
    bm = bmesh.new()
    bm.from_mesh(air.data)
    bmesh.ops.reverse_faces(bm, faces=bm.faces)
    bm.to_mesh(air.data)
    bm.free()
    air.data.materials.clear()
    air.data.materials.append(mat("rock", ROCK_RGB))
    export(air, out)


# ------------------------------------------------------------------------------------------------- preview
def preview(out, pdir):
    """Compose the grotto from layout.py (the engine's placements) and render Workbench stills per station."""
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    os.makedirs(pdir, exist_ok=True)
    col = bpy.context.scene.collection

    def imp(name):
        bpy.ops.import_scene.gltf(filepath=os.path.join(out, name + ".glb"))
        ob = bpy.context.selected_objects[0]
        return ob

    def place(name, rec):
        ob = imp(name)
        x, y, z, qx, qy, qz, qw, sx, sy, sz = rec
        # an AV Gen transform expressed in Blender: conjugate the Y-up rotation by the axis change
        from mathutils import Quaternion
        conv = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0))).to_4x4()
        m = Matrix.Translation((x, y, z)) @ Quaternion((qw, qx, qy, qz)).to_matrix().to_4x4() @ \
            Matrix.Diagonal((sx, sy, sz, 1))
        ob.matrix_world = conv @ m @ conv.inverted() @ ob.matrix_world
        return ob

    imp("shell")
    for h in L.HEROES:
        rec = L.hero_placement(h)
        place(h["name"] + "_xtal", rec)
        place(h["name"] + "_rock", rec)
    rec = L.placement(*L.ARCH["pos"], L.ARCH["yaw"], L.ARCH["scale"], L.ARCH["sink"])
    place("arch_rock", rec)
    place("arch_xtal", rec)
    for v, x, z, yaw, s in L.CLUSTERS:
        rec = L.placement(x, z, yaw, s, 0.12 * s)
        place(v + "_xtal", rec)
        place(v + "_rock", rec)
    for v, x, z, yaw, s, lean in L.SHARDS:
        place(v + "_xtal", L.placement(x, z, yaw, s, 0.1 * s, (lean, 0.0)))
    for v, x, z, yaw, s, sink in L.BOULDERS:
        place(v, L.placement(x, z, yaw, s, sink))
    # terrain and water
    bm = bmesh.new()
    N, half = 120, 18.0
    vs = [[bm.verts.new(av2bl((-half + 2 * half * i / N, L.height(-half + 2 * half * i / N, -half + 2 * half * j / N),
                               -half + 2 * half * j / N))) for j in range(N + 1)] for i in range(N + 1)]
    for i in range(N):
        for j in range(N):
            bm.faces.new((vs[i][j], vs[i + 1][j], vs[i + 1][j + 1], vs[i][j + 1]))
    new_obj("terrain", bm, mat("floor", (0.05, 0.045, 0.06)))
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=12.0)
    new_obj("water", bm, mat("water", (0.02, 0.05, 0.15)))
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.show_cavity = True
    sc.display.shading.show_shadows = False
    sc.render.resolution_x, sc.render.resolution_y = 960, 540
    cam = bpy.data.cameras.new("cam")
    cam.sensor_fit = "VERTICAL"
    cam.angle_y = math.radians(L.CAMERA_FOV)
    cam.clip_end = 200
    co = bpy.data.objects.new("cam", cam)
    col.objects.link(co)
    sc.camera = co
    for i, (p, t, note) in enumerate(L.CAMERA_STATIONS):
        eye, tgt = av2bl(p), av2bl(t)
        co.location = eye
        co.rotation_euler = (tgt - eye).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(pdir, "station%d.png" % i)
        bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(pdir, "layout.blend"))


def main():
    out, pdir = argv()
    os.makedirs(out, exist_ok=True)
    reset()
    build_spire(out)
    build_fan(out)
    build_geode(out)
    build_chandelier(out)
    build_ember(out)
    build_arch(out)
    build_druses(out)
    build_shards(out)
    build_boulders(out)
    build_shell(out)
    if pdir:
        preview(out, pdir)


main()
