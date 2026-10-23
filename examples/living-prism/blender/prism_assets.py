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


def gem(bm, base, axis, radius, length, tip, rng, chips=3, subs=0, sides=6, jitter=0.1, flat=1.0, taper=0.9,
        oblique=0.3, embed=0.15):
    """A jewel-quality crystal (pass 2): a prism with a two-tier termination (a shoulder ring, then the apex: twelve
    tip facets instead of six), edges chipped by plane cuts (each chip a new small facet that catches light on its
    own), and `subs` small sub-crystals sprouting from its sides. Built in its own bmesh, then appended to `bm`."""
    t = bmesh.new()
    u, v, a = basis(axis)
    basev = Vector(base)
    spin = rng.uniform(0, math.tau)
    rs = [radius * (1.0 + rng.uniform(-jitter, jitter)) for _ in range(sides)]

    def ring(center, scale, twist=0.0):
        out = []
        for i in range(sides):
            ang = spin + twist + math.tau * i / sides
            d = u * (math.cos(ang) * rs[i] * scale * flat) + v * (math.sin(ang) * rs[i] * scale)
            out.append(t.verts.new(center + d))
        return out
    off = (u * rng.uniform(-1, 1) + v * rng.uniform(-1, 1)) * radius * oblique
    r0 = ring(basev - a * (embed * length), 1.0)
    r1 = ring(basev + a * length, taper)
    sh = rng.uniform(0.35, 0.55)
    r2 = ring(basev + a * (length + tip * sh) + off * sh, taper * rng.uniform(0.42, 0.6), math.pi / sides)
    apex = t.verts.new(basev + a * (length + tip) + off)
    for i in range(sides):
        j = (i + 1) % sides
        t.faces.new((r0[i], r0[j], r1[j], r1[i]))
        t.faces.new((r1[i], r1[j], r2[i]))          # the shoulder: alternating triangles between the two rings
        t.faces.new((r1[j], r2[j], r2[i]))
        t.faces.new((r2[i], r2[j], apex))
    t.faces.new(list(reversed(r0)))
    centre = basev + a * (length * 0.5)
    for _ in range(chips):
        # a corner of the body or the shoulder, never the apex (a cut there truncates the crystal); taken from the
        # current vertices, since an earlier chip may have removed some
        upper = [x for x in t.verts if length * 0.3 < a.dot(x.co - basev) < length + tip * 0.6
                 and (x.co - basev - a * a.dot(x.co - basev)).length > radius * 0.5]
        if not upper:
            break
        vt = rng.choice(upper)
        n = (vt.co - (centre + a * a.dot(vt.co - centre) * 0.6)).normalized()
        n = (n + Vector((rng.uniform(-.3, .3), rng.uniform(-.3, .3), rng.uniform(-.3, .3)))).normalized()
        depth = radius * rng.uniform(0.07, 0.16)
        geom = t.verts[:] + t.edges[:] + t.faces[:]
        res = bmesh.ops.bisect_plane(t, geom=geom, dist=1e-6, plane_co=vt.co - n * depth, plane_no=n,
                                     clear_outer=True)
        cut = [e for e in res["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
        if cut:
            bmesh.ops.holes_fill(t, edges=cut, sides=0)
    bmesh.ops.recalc_face_normals(t, faces=t.faces)
    me = bpy.data.meshes.new("_gem")
    t.to_mesh(me)
    t.free()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)
    for _ in range(subs):
        h = rng.uniform(0.05, 0.5) * length
        side = (u * rng.uniform(-1, 1) + v * rng.uniform(-1, 1)).normalized()
        p0 = basev + a * h + side * radius * 0.55
        ax = (side * rng.uniform(0.35, 0.65) + a).normalized()     # leaning 20-35 degrees off the parent
        sr = radius * rng.uniform(0.32, 0.48)
        sl = length * rng.uniform(0.18, 0.38)
        gem(bm, p0, ax, sr, sl, sr * 1.7, rng, chips=1, subs=0, embed=0.5)


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
        gem(bm, (0, 0, 0.6), (0.03, 0.02, 1), 0.55, 5.0, 1.1, rng, chips=5, subs=4, jitter=0.08, oblique=0.15)
        spec = [(-0.9, 0.3, 3.6, 0.42, -0.35, 0.1), (0.85, 0.2, 3.1, 0.38, 0.4, 0.05), (-0.3, 0.75, 4.1, 0.34, -0.1, 0.35),
                (0.45, 0.8, 2.6, 0.3, 0.25, 0.4), (-1.25, -0.25, 2.0, 0.3, -0.6, -0.15), (1.3, -0.35, 1.7, 0.26, 0.65, -0.2),
                (0.0, -0.75, 1.3, 0.24, 0.05, -0.6), (-0.65, -0.55, 1.0, 0.18, -0.4, -0.5), (1.6, 0.6, 1.2, 0.2, 0.8, 0.3)]
        for x, y, ln, r, lx, ly in spec:
            gem(bm, (x, y, 0.35), (lx, ly, 1.0), r, ln, r * 1.8, rng, chips=3, subs=2 if r > 0.3 else 1)
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
            gem(bm, (x, y, 0.4), ax, r, ln, r * 1.5, rng, chips=4, subs=2, flat=0.32, jitter=0.06, oblique=0.5)
    export(xtal_obj("fan_xtal", "fan", b), out)
    export(rock_lump("fan_rock", 1.8, 0.55, 5, faces=380, scale=(1.0, 1.2, 1)), out)


def build_geode(out):
    """A cracked geode: an egg of rock broken open along a jagged line, its wall showing a pale chalcedony band, its
    cavity lined with prisms that grow larger toward the bottom; the broken-off cap lies beside it, lining up.
    The opening faces Blender -Y (AV Gen +Z), tilted up so the cavity reads from the route's eye height."""
    R, T = 1.9, 0.42
    rng = random.Random(31)
    tilt = Matrix.Rotation(math.radians(-28), 4, "X")  # opening tilted up toward the viewer

    def crack(d):
        # the break: a plane through the front, made jagged by two low harmonics of the angle round the opening
        ang = math.atan2(d.z, d.x)
        return d.y < -0.18 + 0.16 * math.sin(3 * ang + 0.7) + 0.09 * math.sin(7 * ang + 2.1)

    def shell(name, keep_cut, centre, rot):
        bm = bmesh.new()
        bmesh.ops.create_icosphere(bm, subdivisions=4, radius=1.0)
        for v in bm.verts:  # an egg, a little lumpy
            n = v.co.normalized()
            v.co = Vector((n.x * R, n.y * R, n.z * R * 0.88)) * (1 + 0.06 * math.sin(4 * n.x + 1) * math.cos(3 * n.z))
        dead = [f for f in bm.faces if crack(f.calc_center_median().normalized()) != keep_cut]
        bmesh.ops.delete(bm, geom=dead, context="FACES")
        for v in bm.verts:
            v.co = rot @ v.co + Vector(centre)
        ob = new_obj(name, bm, mat("rock", ROCK_RGB))
        ob.data.materials.append(mat("chalcedony", (0.75, 0.7, 0.85)))
        so = ob.modifiers.new("solid", "SOLIDIFY")
        so.thickness = T
        so.offset = -1.0
        so.use_rim = True
        so.material_offset_rim = 1
        apply_mods(ob)
        return ob

    body = shell("geode_body", False, (0, 0, R * 0.78), tilt)
    cap = shell("geode_cap", True, (0, 0, 0), Matrix.Identity(4))
    # the cap lies to the side, broken face up, partly sunk
    cap.data.transform(Matrix.Translation((1.9, -1.4, 0.15)) @ Matrix.Rotation(math.radians(160), 4, "X") @
                       Matrix.Rotation(math.radians(30), 4, "Z") @ Matrix.Translation((0, R * 0.6, 0)))
    ob = join([body, cap], "geode_all")
    # split the chalcedony rim from the rock
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="DESELECT")
    ob.active_material_index = 1
    bpy.ops.object.material_slot_select()
    bpy.ops.mesh.separate(type="SELECTED")
    bpy.ops.object.mode_set(mode="OBJECT")
    rim = [o for o in bpy.context.selected_objects if o != ob][0]
    rim.name = "geode_rim"
    ob.name = "geode_rock"
    for o in (ob, rim):
        o.data.materials.clear()
    ob.data.materials.append(mat("rock", ROCK_RGB))
    rim.data.materials.append(mat("chalcedony", (0.75, 0.7, 0.85)))
    facet(ob, 0.22, 0.9, faces=1400)
    for p in rim.data.polygons:
        p.use_smooth = False
    export(ob, out)
    export(rim, out)

    def b(bm2):
        def lining(centre, rot, keep_cut, n, up_bias):
            for i in range(n):
                yv = 1 - 2 * (i + 0.5) / n
                r = math.sqrt(max(0.0, 1 - yv * yv))
                th = math.pi * (3 - math.sqrt(5)) * i
                d = Vector((math.cos(th) * r, yv, math.sin(th) * r))
                if crack(d) != keep_cut:
                    continue
                inner = d * (R - T) * Vector((1, 1, 0.88)) if False else Vector((d.x * (R - T), d.y * (R - T),
                                                                                 d.z * (R - T) * 0.88))
                depth = 0.5 + 0.5 * (-d.z if up_bias else d.z)   # bigger toward the bottom of the bowl
                ln = rng.uniform(0.15, 0.35) + 0.45 * depth * rng.random()
                p = rot @ inner + Vector(centre)
                ax = rot @ (-d + Vector((rng.uniform(-.25, .25), rng.uniform(-.25, .25), rng.uniform(-.25, .25))))
                prism(bm2, p, ax, rng.uniform(0.05, 0.1) + 0.06 * depth, ln, ln * 0.35, rng, embed=0.35)
        lining((0, 0, R * 0.78), tilt, False, 520, True)
    xt = xtal_obj("geode_xtal", "geode", b)
    export(xt, out)
    # the cap's lining: a small druse on its broken face
    rngc = random.Random(37)

    def bc(bm3):
        for _ in range(26):
            ang, rr = rngc.uniform(0, math.tau), rngc.uniform(0, 0.9)
            p = Vector((1.9 + math.cos(ang) * rr, -1.4 + math.sin(ang) * rr * 0.7, 0.45))
            ln = rngc.uniform(0.12, 0.4)
            prism(bm3, p, (rngc.uniform(-.3, .3), rngc.uniform(-.3, .3), 1), rngc.uniform(0.04, 0.09), ln, ln * 0.4,
                  rngc, embed=0.4)
    export(xtal_obj("geodecap_xtal", "geode", bc), out)


def build_chandelier(out):
    """Built upright (the knob at z = 0, prisms rising); the scene flips it to hang from the ceiling."""
    rng = random.Random(41)

    def b(bm):
        spec = [(0, 0, 4.4, 0.46, 0, 0), (0.6, 0.2, 3.3, 0.32, 0.3, 0.1), (-0.55, 0.3, 3.7, 0.34, -0.25, 0.12),
                (0.2, -0.6, 2.7, 0.28, 0.1, -0.3), (-0.35, -0.45, 2.2, 0.24, -0.2, -0.3), (0.85, -0.3, 1.8, 0.22, 0.5, -0.2),
                (-0.9, -0.1, 1.5, 0.2, -0.55, 0.0), (0.4, 0.75, 1.2, 0.18, 0.3, 0.5)]
        for x, y, ln, r, lx, ly in spec:
            gem(bm, (x * 0.8, y * 0.8, 1.9), (lx, ly, 1.0), r, ln, r * 2.0, rng, chips=3, subs=1)
    export(xtal_obj("chandelier_xtal", "chandelier", b), out)
    # a hanging pendant of rock: it brings the crystals down from the roof into the frame
    export(rock_lump("chandelier_rock", 1.4, 1.6, 7, faces=360, scale=(1.0, 0.9, 1.0)), out)


def build_ember(out):
    """The warm accent: a cluster of stout, terraced prisms (the macro amethyst's growth steps), broken at different
    heights, leaning apart -- many mid-size facets instead of a few big ones, so it reads as crystal, not a block."""
    rng = random.Random(53)

    def b(bm):
        spec = [(0.0, 0.0, 0.62, 1.5, 0.0, 0.0), (0.7, 0.25, 0.45, 1.1, 0.35, 0.1), (-0.6, 0.35, 0.5, 1.25, -0.3, 0.15),
                (0.25, -0.6, 0.4, 0.8, 0.1, -0.35), (-0.45, -0.5, 0.33, 0.7, -0.3, -0.3), (0.95, -0.35, 0.28, 0.55, 0.6, -0.2)]
        for x, y, r, ln, lx, ly in spec:
            ax = Vector((lx, ly, 1.0)).normalized()
            gem(bm, (x, y, 0.0), ax, r, ln, r * 0.35, rng, chips=3, subs=0, jitter=0.1, taper=0.92, oblique=0.7,
                embed=0.3)
            u, v, a_ = basis(ax)
            # two or three growth terraces stepping down one side, each a short, narrower prism
            side = (u * rng.uniform(-1, 1) + v * rng.uniform(-1, 1)).normalized()
            for k in range(rng.randint(2, 3)):
                f = 0.72 - 0.16 * k
                base = Vector((x, y, 0.0)) + side * r * (0.45 + 0.2 * k) + a_ * ln * (0.15 + 0.12 * k)
                prism(bm, base, (ax + side * 0.12).normalized(), r * f, ln * (0.55 - 0.12 * k), r * 0.2, rng,
                      jitter=0.08, taper=0.95, oblique=0.6, embed=0.4)
    export(xtal_obj("ember_xtal", "ember", b), out)
    export(rock_lump("ember_rock", 1.4, 0.4, 9, faces=300), out)


def build_arch(out):
    """A natural mineral bridge: lumps of rock strung along an asymmetric span, fused (voxel remesh), weathered at two
    scales and faceted. A heavy, flared left foot, a thinner right one stepping back, a deck thicker front to back than
    it is tall; seven patches of druse on the underside."""
    rng = random.Random(59)
    ctrl = [Vector(p) for p in ((-6.6, 0.8, -0.6), (-6.0, 0.3, 3.0), (-4.2, -0.2, 6.0), (-1.2, 0.1, 7.6),
                                (2.0, 0.6, 7.4), (4.6, 1.2, 5.6), (6.2, 1.8, 2.6), (6.9, 2.2, -0.6))]
    rads = [1.7, 1.05, 0.72, 0.58, 0.55, 0.62, 0.75, 1.05]

    def cat(t):
        n = len(ctrl) - 1
        sg = min(int(t * n), n - 1)
        u = t * n - sg
        p0, p1, p2, p3 = ctrl[max(sg - 1, 0)], ctrl[sg], ctrl[sg + 1], ctrl[min(sg + 2, n)]
        u2, u3 = u * u, u * u * u
        pos = 0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3)
        r = rads[sg] + (rads[sg + 1] - rads[sg]) * u
        return pos, r
    lumps = []
    for k in range(46):
        t = k / 45
        pos, r = cat(t)
        rr = r * rng.uniform(0.85, 1.15)
        off = Vector((rng.uniform(-.15, .15), rng.uniform(-.3, .3), rng.uniform(-.12, .12))) * r
        lumps.append(ico("arch_l%d" % k, rr, (rng.uniform(0.9, 1.2), rng.uniform(1.25, 1.6), rng.uniform(0.8, 0.95)),
                         pos + off, subdiv=3))
    for x, y, rr in ((-7.3, 1.2, 1.5), (-6.2, -0.6, 1.1), (7.3, 2.5, 1.1)):   # flared feet, sunk
        lumps.append(ico("arch_f%d" % len(lumps), rr, (1.2, 1.0, 0.6), (x, y, -0.4), subdiv=3))
    ob = join(lumps, "arch_rock")
    ob.data.materials.clear()
    ob.data.materials.append(mat("rock", ROCK_RGB))
    m = ob.modifiers.new("remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = 0.14
    sm = ob.modifiers.new("smooth", "SMOOTH")
    sm.factor = 1.0
    sm.iterations = 12
    apply_mods(ob)
    facet(ob, 0.4, 2.2, kind="CLOUDS")          # broad weathering
    facet(ob, 0.22, 0.6, faces=2600)            # breakage, then the facets
    export(ob, out)
    # pass 2: the arch's crust is ONE small druse instanced along the underside, its placements ordered by their
    # distance along the span (the arc parameter), so `instanceIndex` is the path distance a travelling light reads
    me = ob.data
    samples = [cat(k / 400)[0] for k in range(401)]
    under = [p for p in me.polygons if p.normal.z < -0.25 and p.center.z > 1.2]
    w = [p.area for p in under]
    recs = []
    for _ in range(84):
        p = rng.choices(under, weights=w)[0]
        c = Vector(p.center)
        k = min(range(401), key=lambda i: (samples[i] - c).length)
        n = (Vector(p.normal) + Vector((rng.uniform(-.25, .25), rng.uniform(-.25, .25), rng.uniform(-.15, .05))))
        n.normalize()
        recs.append({"t": k / 400, "pos": list(c - n * 0.05), "normal": list(n), "scale": rng.uniform(0.55, 1.15),
                     "spin": rng.uniform(0, 360)})
    recs.sort(key=lambda r: r["t"])
    import json as _json
    with open(os.path.join(out, "arch_crust.json"), "w") as f:
        _json.dump(recs, f)
    rngd = random.Random(67)

    def b(bm):
        for _ in range(9):
            ang, rr = rngd.uniform(0, math.tau), rngd.uniform(0, 0.22)
            x, y = math.cos(ang) * rr, math.sin(ang) * rr
            ln = rngd.uniform(0.18, 0.55) * (1.0 - rr)
            gem(bm, (x, y, 0.05), (x * 1.5 + rngd.uniform(-.2, .2), y * 1.5 + rngd.uniform(-.2, .2), 1.0),
                rngd.uniform(0.05, 0.11), ln, ln * 0.4, rngd, chips=1, embed=0.4)
    export(xtal_obj("archdruse_xtal", "arch", b), out)


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
                gem(bm, (x, y, 0.15), ax, rng.uniform(0.07, 0.17), ln, ln * 0.35, rng, chips=1)
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
    # crust: druse patches on the roof and the upper walls, so the frame's upper third has structure (no dead space)
    rng = random.Random(101)
    roof = [p for p in air.data.polygons if p.normal.z < -0.45 and p.center.z > 5.5 and p.center.y > -13.0]
    wall = [p for p in air.data.polygons if abs(p.normal.z) < 0.35 and 3.2 < p.center.z < 7.5 and p.center.y > -13.0]
    seeds = []
    for pool, n, dmin in ((roof, 16, 3.2), (wall, 12, 3.0)):
        tries = 0
        got = 0
        while got < n and tries < 4000:
            tries += 1
            c = rng.choice(pool)
            if all((c.center - q.center).length > dmin for q in seeds):
                seeds.append(c)
                got += 1
    allf = roof + wall

    def b(bm):
        for sd in seeds:
            near = [p for p in allf if (p.center - sd.center).length < 1.3]
            big = rng.uniform(0.7, 1.4)
            for _ in range(rng.randint(10, 24)):
                p = rng.choice(near)
                n = Vector(p.normal) + Vector((rng.uniform(-.35, .35), rng.uniform(-.35, .35), rng.uniform(-.35, .35)))
                ln = big * rng.uniform(0.2, 0.75) * (1.3 - (p.center - sd.center).length / 1.3)
                prism(bm, p.center - n * 0.06, n, rng.uniform(0.05, 0.13) * big, ln, ln * 0.4, rng, embed=0.6)
    export(xtal_obj("crust_xtal", "druse", b), out)


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
    imp("crust_xtal")
    for h in L.HEROES:
        rec = L.hero_placement(h)
        place(h["name"] + "_xtal", rec)
        place(h["name"] + "_rock", rec)
        if h["name"] == "geode":
            place("geode_rim", rec)
            place("geodecap_xtal", rec)
    rec = L.placement(*L.ARCH["pos"], L.ARCH["yaw"], L.ARCH["scale"], L.ARCH["sink"])
    place("arch_rock", rec)
    pass  # the arch crust is instanced in the engine (arch_crust.json)
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
