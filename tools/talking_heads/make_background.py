"""Talking Heads: the background's shapes (docs/prototypes/talking-heads/10-brief-background.md, 11-background-plan.md).

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P tools/talking_heads/make_background.py -- \
        --out examples/sonic-abstract/meshes

Writes, each in its own frame (glTF: +Y up, +Z toward the camera, metres), all deterministic:
- th-bg-membrane-a.glb, th-bg-membrane-b.glb: the warped spatial planes, and th-bg-membrane-{a,b}-lines.glb their
  contour lines as thin ribbons on the same surface. Each is a curved sheet with an irregular,
  noise-eroded outline, gridded in rings (the engine's Sine and Noise deformers wave it, so it needs the vertices); the
  sheet lies in the XY plane bowed along Z, so `localPosition` xy are its surface coordinates for the contour program.
- th-bg-arcs.glb: the structures -- partial rings and a bent open frame, thin swept sections, asymmetric.
- th-bg-bands.glb: two broad curved ribbons crossing the distance.
- th-bg-filaments.glb: sparse thin strands, slack curves through the space behind the busts.
Nothing here is a room, a tunnel or a repeated object: every piece is a one-off curve.
"""
import argparse
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector, noise

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
args = ap.parse_args(argv)
OUT = os.path.expanduser(args.out)
os.makedirs(OUT, exist_ok=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def new_obj(name):
    me = bpy.data.meshes.new(name)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    mat = bpy.data.materials.new(name + "-mat")
    mat.diffuse_color = (0.05, 0.04, 0.08, 1.0)
    me.materials.append(mat)
    return ob


def export(ob, path):
    for o in bpy.context.scene.objects:
        o.select_set(o == ob)
    bpy.context.view_layer.objects.active = ob
    # Blender is Z-up; the exporter's +Y-up conversion maps Blender (x, y, z) -> glTF (x, z, -y). Authored here
    # directly in glTF terms by building in Blender as (x, -z_gltf, y_gltf).
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_apply=True, export_yup=True,
                              export_normals=True, export_materials="EXPORT")


def g(x, y, z):
    """glTF (x, y, z) -> Blender coordinates."""
    return Vector((x, -z, y))


def edge_radius(ang, seed, erosion):
    rnd = Vector((seed * 1.7, seed * 0.3, seed * 2.9))
    return (0.86 + erosion * noise.noise(Vector((math.cos(ang) * 1.6, math.sin(ang) * 1.6, 0.0)) + rnd)
            + 0.5 * erosion * noise.noise(Vector((math.cos(ang) * 4.0, math.sin(ang) * 4.0, 0.0)) + rnd * 2.0))


def sheet_z(u, v, bow, seed):
    return bow * (0.6 * u * u - 0.4 * v * v) + 0.15 * bow * math.sin(2.1 * u + 1.3 * v + seed)


def membrane(name, width, height, n, bow, seed, erosion):
    """A sheet in glTF XY, bowed along Z (a shallow saddle). An n x n square grid mapped onto a disc (Shirley-Chiu
    concentric mapping, so the quads stay even -- a polar grid's slivers at the centre cost fill rate), its rim
    pushed out and in by noise: an irregular silhouette with a smooth edge."""
    ob = new_obj(name)
    bm = bmesh.new()
    verts = {}
    for j in range(n + 1):
        for i in range(n + 1):
            a_, b_ = i / n * 2.0 - 1.0, j / n * 2.0 - 1.0
            if a_ == 0.0 and b_ == 0.0:
                r, phi = 0.0, 0.0
            elif abs(a_) > abs(b_):
                r, phi = a_, math.pi / 4.0 * b_ / a_
            else:
                r, phi = b_, math.pi / 2.0 - math.pi / 4.0 * a_ / b_
            x, y = r * math.cos(phi), r * math.sin(phi)
            k = edge_radius(math.atan2(y, x), seed, erosion)
            u, v = x * k, y * k
            verts[i, j] = bm.verts.new(g(u * width * 0.5, v * height * 0.5, sheet_z(u, v, bow, seed)))
    for j in range(n):
        for i in range(n):
            f = bm.faces.new((verts[i, j], verts[i + 1, j], verts[i + 1, j + 1], verts[i, j + 1]))
            f.smooth = True
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    for f in bm.faces:   # facing +Z (glTF), toward the camera
        if f.normal.dot(g(0, 0, 1)) < 0:
            f.normal_flip()
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def membrane_lines(name, width, height, bow, seed, erosion, count, lift=0.006, half=0.004):
    """The membrane's contour lines as geometry: `count` thin flat ribbons lying on the sheet (lifted `lift` toward the
    camera) along lines of constant height, ending at its rim. Drawn with the sheet's own deformers they ride its
    waves; they cover few pixels, so a cheap glow program lights them (a per-pixel contour program over the whole
    sheet cost several ms a frame)."""
    ob = new_obj(name)
    bm = bmesh.new()
    for c in range(count):
        v = -0.92 + 1.84 * (c + 0.5) / count
        prev = None
        for k in range(161):
            u = -1.0 + 2.0 * k / 160
            inside = math.hypot(u, v) < edge_radius(math.atan2(v, u), seed, erosion) * 0.995
            if not inside:
                prev = None
                continue
            z = sheet_z(u, v, bow, seed) + lift
            # the ribbon's width runs along the sheet's v direction
            dv = half / (height * 0.5)
            p0 = bm.verts.new(g(u * width * 0.5, (v - dv) * height * 0.5, z))
            p1 = bm.verts.new(g(u * width * 0.5, (v + dv) * height * 0.5, z))
            if prev:
                bm.faces.new((prev[0], p0, p1, prev[1]))
            prev = (p0, p1)
    for v_ in [v_ for v_ in bm.verts if not v_.link_faces]:
        bm.verts.remove(v_)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    for f in bm.faces:
        if f.normal.dot(g(0, 0, 1)) < 0:
            f.normal_flip()
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def sweep(bm, pts, radius, sides, flat=1.0):
    """A thin tube (flattened by `flat` into a band) along a polyline of glTF points."""
    ring_prev = None
    n = len(pts)
    for k, p in enumerate(pts):
        a = pts[min(k + 1, n - 1)] - pts[max(k - 1, 0)]
        t = a.normalized()
        ref = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
        nx = t.cross(ref).normalized()
        ny = t.cross(nx).normalized()
        ring = []
        for s in range(sides):
            th = 2.0 * math.pi * s / sides
            off = nx * (math.cos(th) * radius) + ny * (math.sin(th) * radius * flat)
            ring.append(bm.verts.new(p + off))
        if ring_prev:
            for s in range(sides):
                f = bm.faces.new((ring_prev[s], ring_prev[(s + 1) % sides], ring[(s + 1) % sides], ring[s]))
                f.smooth = True
        ring_prev = ring


def arc_pts(centre, r, a0, a1, tilt, wobble, steps, seed):
    pts = []
    for k in range(steps + 1):
        a = a0 + (a1 - a0) * k / steps
        w = 1.0 + wobble * math.sin(3.0 * a + seed)
        x, y = r * w * math.cos(a), r * w * math.sin(a)
        # tilt about X (degrees) so the ring is seen obliquely
        ct, st = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))
        pts.append(g(centre[0] + x, centre[1] + y * ct, centre[2] + y * st))
    return pts


def main():
    reset()
    random.seed(7)
    for nm, w, h, n, bow, seed, ero, count in (("a", 9.0, 4.6, 96, 0.9, 1.0, 0.16, 26),
                                               ("b", 5.5, 2.6, 72, 0.5, 4.0, 0.22, 18)):
        reset()
        export(membrane("th-bg-membrane-" + nm, w, h, n, bow, seed, ero), os.path.join(OUT, "th-bg-membrane-%s.glb" % nm))
        reset()
        export(membrane_lines("th-bg-membrane-%s-lines" % nm, w, h, bow, seed, ero, count),
               os.path.join(OUT, "th-bg-membrane-%s-lines.glb" % nm))

    # the structures: three partial rings at different sizes and tilts, and one bent open frame
    reset()
    ob = new_obj("th-bg-arcs")
    bm = bmesh.new()
    sweep(bm, arc_pts((-2.3, 1.1, 0.0), 1.7, math.radians(-40), math.radians(150), 62, 0.06, 90, 1.0), 0.018, 8)
    sweep(bm, arc_pts((2.6, 0.2, -0.6), 2.4, math.radians(100), math.radians(250), -28, 0.04, 90, 2.0), 0.014, 8)
    sweep(bm, arc_pts((0.9, 1.9, 0.4), 0.9, math.radians(200), math.radians(330), 75, 0.1, 50, 3.0), 0.010, 8)
    # the bent frame: an open rectangle whose long sides sag and twist out of plane
    fr = []
    for k in range(121):
        s = k / 120.0 * 3.4   # three and a bit sides of a 1.6 x 1.0 frame, left open
        if s < 1.0:
            x, y = -0.8 + 1.6 * s, 0.5
        elif s < 2.0:
            x, y = 0.8, 0.5 - (s - 1.0)
        elif s < 3.0:
            x, y = 0.8 - 1.6 * (s - 2.0), -0.5
        else:
            x, y = -0.8, -0.5 + (s - 3.0)
        z = 0.35 * math.sin(1.7 * s) + 0.12 * x * y
        fr.append(g(-3.1 + x * math.cos(0.5) - z * math.sin(0.5), -0.55 + y, 0.4 + x * math.sin(0.5) + z * math.cos(0.5)))
    sweep(bm, fr, 0.012, 6)
    bm.to_mesh(ob.data)
    bm.free()
    export(ob, os.path.join(OUT, "th-bg-arcs.glb"))

    # the bands: two broad curved ribbons crossing the far distance, flattened tubes
    reset()
    ob = new_obj("th-bg-bands")
    bm = bmesh.new()
    p1 = [g(-6.0 + 12.0 * t, 2.2 - 1.6 * t + 0.9 * math.sin(2.6 * t), -0.8 * math.sin(3.1 * t)) for t in
          [k / 80.0 for k in range(81)]]
    p2 = [g(5.5 - 10.0 * t, -1.1 + 0.8 * math.sin(3.4 * t + 0.6) + 0.5 * t, 0.6 * math.cos(2.2 * t)) for t in
          [k / 80.0 for k in range(81)]]
    sweep(bm, p1, 0.16, 6, flat=0.08)
    sweep(bm, p2, 0.11, 6, flat=0.08)
    bm.to_mesh(ob.data)
    bm.free()
    export(ob, os.path.join(OUT, "th-bg-bands.glb"))

    # the filaments: five slack strands, hanging and crossing behind the busts at different depths
    reset()
    ob = new_obj("th-bg-filaments")
    bm = bmesh.new()
    rnd = random.Random(11)
    for f in range(5):
        x0, x1 = rnd.uniform(-3.5, -0.5), rnd.uniform(0.5, 3.5)
        y0, y1 = rnd.uniform(-0.6, 1.8), rnd.uniform(-0.6, 1.8)
        z = rnd.uniform(-1.2, 0.4)
        sag = rnd.uniform(0.3, 0.9)
        pts = []
        for k in range(61):
            t = k / 60.0
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t - sag * 4.0 * t * (1.0 - t) + 0.08 * math.sin(9.0 * t + f)
            pts.append(g(x, y, z + 0.3 * math.sin(4.0 * t + f)))
        if f % 2:
            pts = pts[: 35]   # some end in mid-air
        sweep(bm, pts, 0.0035, 5)
    bm.to_mesh(ob.data)
    bm.free()
    export(ob, os.path.join(OUT, "th-bg-filaments.glb"))


main()
