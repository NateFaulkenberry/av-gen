"""The Living Prism (aurora revision): the crystal formations, generated (Blender 5.x, background).

    Blender -b --factory-startup --python-exit-code 1 -P examples/living-prism-aurora/blender/aurora_assets.py --
        <out dir> [--preview <dir>]

Writes <out>/<family>.glb, one mesh per formation family, base at Blender z = 0 (AV Gen y = 0), its front facing
Blender -Y (AV Gen +Z). --preview composes the valley from layout.py (a height-field mesh) with the formations and
renders a Workbench still from the fixed camera: a composition check, not the look.

The shard (the reference's crystals, 3.1): a long, irregular, tabular prism of 4-6 sides with large flat faces and an
oblique termination (one or two apex points), not a hexagonal quartz. Families give each formation its own
silhouette (spec 4.4): a tall leaning cluster, a wide tilted pair, spires, small and distant groups.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import layout as L  # noqa: E402


def argv():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return a[0], (a[a.index("--preview") + 1] if "--preview" in a else None)


def basis(axis):
    a = Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(a.z) < 0.9 else Vector((1, 0, 0))
    u = a.cross(ref).normalized()
    return u, a.cross(u).normalized(), a


def shard(bm, base, axis, width, length, rng, sides=None, flat=None, tip=None, chisel=None, embed=0.12):
    """One crystal shard into bm. width: its cross-section's larger radius; flat: thickness / width."""
    sides = sides or rng.choice((4, 5, 5, 6))
    flat = flat if flat is not None else rng.uniform(0.6, 0.9)
    tip = tip if tip is not None else rng.uniform(0.25, 0.5) * length
    chisel = rng.random() < 0.4 if chisel is None else chisel
    u, v, a = basis(axis)
    spin = rng.uniform(0, math.tau)
    angles = sorted(spin + math.tau * (i + rng.uniform(-0.18, 0.18)) / sides for i in range(sides))
    rs = [width * rng.uniform(0.75, 1.15) for _ in range(sides)]
    base = Vector(base)

    def ring(c, sc):
        return [bm.verts.new(c + u * (math.cos(t) * r * sc) + v * (math.sin(t) * r * sc * flat))
                for t, r in zip(angles, rs)]
    r0 = ring(base - a * (embed * length), 1.0)
    r1 = ring(base + a * (length - tip), rng.uniform(0.88, 1.0))
    off = (u * rng.uniform(-1, 1) + v * rng.uniform(-0.5, 0.5)) * width * 0.7
    top = base + a * length + off
    if chisel:   # a two-point (chisel) termination: two apices on a short ridge
        e = u * width * 0.35
        p1, p2 = bm.verts.new(top - e), bm.verts.new(top + e * 0.6 - a * tip * 0.15)
    for i in range(sides):
        j = (i + 1) % sides
        bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
        if chisel:
            # split the ring between the two apices by which side of the ridge each vertex lies
            pa = p1 if (r1[i].co - top).dot(u) < 0 else p2
            pb = p1 if (r1[j].co - top).dot(u) < 0 else p2
            if pa is pb:
                bm.faces.new((r1[i], r1[j], pa))
            else:
                bm.faces.new((r1[i], r1[j], pb, pa))
        else:
            apex = getattr(shard, "_apex", None)
            if i == 0:
                shard._apex = bm.verts.new(top)
            bm.faces.new((r1[i], r1[j], shard._apex))
    if chisel:
        pass
    bm.faces.new(list(reversed(r0)))


def lean(rng, out_x, deg_lo, deg_hi, spread=25):
    t = math.radians(rng.uniform(deg_lo, deg_hi))
    az = math.radians(rng.uniform(-spread, spread)) + (0.0 if out_x > 0 else math.pi)
    return (math.sin(t) * math.cos(az), math.sin(t) * math.sin(az), math.cos(t))


FAMILIES = {
    # name: (seed, [(dx, dy, width, length, lean_lo, lean_hi)], outward +x)
    "cluster": (11, [(0, 0, 4.6, 40, 4, 10), (5, 3, 3.6, 30, 14, 24), (-4, 2, 3.2, 26, 8, 18), (3, -5, 3.0, 22, 20, 34),
                     (-6, -4, 2.6, 17, 24, 38), (8, -2, 2.4, 14, 30, 45), (-2, 6, 2.2, 12, 18, 30)], 1),
    "pair": (23, [(0, 0, 6.4, 28, 26, 34), (8, 3, 5.6, 24, 34, 42), (-5, 4, 3.0, 13, 14, 24), (10, -4, 2.6, 10, 40, 52)], -1),
    "spire": (31, [(0, 0, 3.8, 30, 4, 10), (5, 2, 2.8, 19, 14, 22)], 1),
    "spire2": (37, [(0, 0, 4.2, 34, 6, 12), (-5, 3, 2.6, 15, 18, 28)], -1),
    "small": (41, [(0, 0, 2.6, 16, 6, 14), (4, 3, 2.2, 12, 14, 24), (-3, 3, 1.8, 9, 18, 30)], 1),
    "small2": (43, [(0, 0, 2.8, 18, 6, 14), (-4, 2, 2.0, 11, 16, 26)], -1),
    "far": (47, [(0, 0, 4.0, 28, 2, 8), (9, 4, 3.4, 22, 8, 14), (-8, 2, 3.0, 18, 6, 14), (16, -3, 2.6, 15, 12, 20)], 1),
}


def build_family(name, out):
    seed, spec, outward = FAMILIES[name]
    rng = random.Random(seed)
    bm = bmesh.new()
    for dx, dy, w, ln, lo, hi in spec:
        shard._apex = None
        shard(bm, (dx, dy, 0.0), lean(rng, outward, lo * 0.8, hi * 0.8), w * 1.6, ln, rng)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    m = bpy.data.materials.get("xtal") or bpy.data.materials.new("xtal")
    m.diffuse_color = (0.75, 0.6, 0.95, 1)
    me.materials.append(m)
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + ".glb"), export_format="GLB", use_selection=True,
                              export_yup=True, export_apply=True, export_normals=True)
    print("EXPORTED", name, len(me.polygons), "faces")
    return ob


def av2bl(v):
    return Vector((v[0], -v[2], v[1]))


def preview(objs, pdir):
    os.makedirs(pdir, exist_ok=True)
    sc = bpy.context.scene
    # the valley as a height field, coarse far away (the camera's view only)
    bm = bmesh.new()
    xs = [x for x in range(-800, 801, 10)]
    zs = list(range(80, -3000, -12))
    grid = [[bm.verts.new(av2bl((x, L.height(x, z), z))) for x in xs] for z in zs]
    for i in range(len(zs) - 1):
        for j in range(len(xs) - 1):
            bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
    me = bpy.data.meshes.new("valley")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("valley", me)
    sc.collection.objects.link(ob)
    m = bpy.data.materials.new("moss")
    m.diffuse_color = (0.05, 0.12, 0.06, 1)
    me.materials.append(m)
    # the water: a sheet at each river point's level, a coarse stand-in
    bm = bmesh.new()
    for a, b in zip(L._RIVER_S, L._RIVER_S[1:]):
        d = Vector((b[0] - a[0], 0, b[2] - a[2])).normalized()
        n = Vector((-d.z, 0, d.x)) * 8.0
        q = [bm.verts.new(av2bl((p[0] + s * n.x, p[1] + 0.05, p[2] + s * n.z))) for p, s in ((a, -1), (a, 1), (b, 1), (b, -1))]
        bm.faces.new(q)
    me = bpy.data.meshes.new("water")
    bm.to_mesh(me)
    bm.free()
    wo = bpy.data.objects.new("water", me)
    sc.collection.objects.link(wo)
    wm = bpy.data.materials.new("water")
    wm.diffuse_color = (0.55, 0.6, 0.8, 1)
    me.materials.append(wm)
    fam = {o.name: o for o in objs}
    conv = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0))).to_4x4()
    for name, x, z, yaw, s, f in L.FORMATIONS:
        src = fam[f]
        o = src.copy()
        sc.collection.objects.link(o)
        q = L.quat_y(yaw)
        mw = Matrix.Translation((x, L.height(x, z) - 0.8 * s, z)) @ Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4() \
            @ Matrix.Diagonal((s, s, s, 1))
        o.matrix_world = conv @ mw @ conv.inverted()
    for o in objs:
        o.hide_render = True
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.background_type = "VIEWPORT"
    sc.display.shading.background_color = (0.09, 0.1, 0.18)
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    cam = bpy.data.cameras.new("cam")
    cam.sensor_fit = "VERTICAL"
    cam.angle_y = math.radians(L.CAMERA_FOV)
    cam.clip_end = 5000
    co = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    eye, tgt = av2bl(L.CAMERA_POS), av2bl(L.CAMERA_TARGET)
    co.location = eye
    co.rotation_euler = (tgt - eye).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = os.path.join(pdir, "camera.png")
    bpy.ops.render.render(write_still=True)


def main():
    out, pdir = argv()
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = [build_family(n, out) for n in FAMILIES]
    if pdir:
        preview(objs, pdir)


main()
