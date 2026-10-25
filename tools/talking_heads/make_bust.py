"""Talking Heads: the mannequin bust (production asset generator; ADR-none, scene docs in
docs/prototypes/talking-heads/).

The bust is ONE generalised cylinder:
- horizontal cross-sections (superellipses with separate front and back depths) swept up a monotone-cubic profile;
- closed by a pole at the crown and a flat base: a cut classical bust with squared shoulders.
Sculpted features are smooth Gaussian offsets in (angle, height):
- a T-shaped brow bar and nose ridge;
- almond eye recesses under it, 17 mm deep;
- sharp cheekbones over a hollow;
- a squared jaw with a jaw line, a defined chin;
- lips round a real mouth opening (a 22 mm deep cavity).
Two material regions: `porcelain` (the shell) and `mouth_interior` (near black), separated along the opening's own
contour. Quad topology, no decimation: the ring and segment counts set the triangles. Deterministic.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P tools/talking_heads/make_bust.py -- \
        --out examples/sonic-abstract/meshes [--previews ~/Desktop/av-gen-review/talking-heads/bust] [--samples 48]

Writes to --out:
- th-head.glb: the head and upper neck (porcelain + mouth_interior), its origin AT THE NECK JOINT, closed by a dome;
- th-body.glb: the lower neck and bust, closed by a dome rising into the head, with a collar ring over the seam;
- th-head-ghost.glb, th-body-ghost.glb: the same pieces, one alpha-BLEND material, for the spectral copies;
- th-bust.anchors.json: the joint, the emitter anchors in the head's frame, and the mesh statistics.
The head tilts back about the joint (the owner, 2026-10-25: "when the head talks its head can tilt back") with no gap
up to 32 degrees: the body's dome fills the throat as it opens.

Units: metres. Blender Z-up, the face looks down -Y. The glTF exporter's +Y-up conversion gives +Y up and the face
toward **+Z** in the GLB (Blender (x, y, z) -> glTF (x, z, -y)). Origin: the centre of the base's bottom face. Height
0.604 m; the scene scales it.
"""
import argparse
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
ap.add_argument("--previews", default="")
ap.add_argument("--segments", type=int, default=104, help="segments round each ring")
ap.add_argument("--samples", type=int, default=48)
args = ap.parse_args(argv)
OUT = os.path.expanduser(args.out)
os.makedirs(OUT, exist_ok=True)
PREV = os.path.expanduser(args.previews) if args.previews else ""
if PREV:
    os.makedirs(PREV, exist_ok=True)

# ------------------------------------------------------------------------------------------- the profile
# Control points up the bust: z, half-width W, front depth DF, back depth DB, centre offset CY (+Y = back),
# superellipse exponent N (2 = ellipse, larger = boxier: the squared jaw, the cut shoulders). Monotone cubic.
#            z      W      DF     DB     CY      N
PROFILE = [(0.000, 0.176, 0.072, 0.068, 0.012, 3.4),   # flat base: a cut classical bust, not a bell
           (0.030, 0.184, 0.076, 0.072, 0.012, 3.4),
           (0.072, 0.176, 0.073, 0.071, 0.012, 3.0),   # shoulder edge
           (0.108, 0.126, 0.060, 0.062, 0.014, 2.4),   # trapezius slope
           (0.150, 0.068, 0.048, 0.052, 0.020, 2.0),
           (0.195, 0.046, 0.040, 0.046, 0.024, 2.0),   # the long neck, leaning forward
           (0.245, 0.044, 0.040, 0.046, 0.018, 2.0),
           (0.290, 0.045, 0.042, 0.047, 0.012, 2.0),
           (0.316, 0.049, 0.048, 0.052, 0.008, 2.0),   # under the jaw
           (0.334, 0.057, 0.068, 0.060, 0.002, 2.3),   # chin line
           (0.352, 0.065, 0.080, 0.068, 0.000, 2.7),   # the jaw angle: wide and squared
           (0.385, 0.067, 0.088, 0.076, 0.000, 2.4),   # mouth level
           (0.425, 0.072, 0.091, 0.086, 0.002, 2.25),  # cheekbones
           (0.462, 0.075, 0.090, 0.094, 0.005, 2.15),  # eye line
           (0.497, 0.077, 0.089, 0.100, 0.008, 2.1),   # brow / temple
           (0.528, 0.075, 0.083, 0.103, 0.011, 2.05),  # the occiput's fullness
           (0.556, 0.065, 0.070, 0.094, 0.013, 2.0),
           (0.576, 0.047, 0.050, 0.070, 0.015, 2.0),
           (0.588, 0.023, 0.025, 0.036, 0.015, 2.0),
           (0.592, 0.000, 0.000, 0.000, 0.015, 2.0)]   # crown (pole)

# Features: (name, angle deg from the front, height z, sigma angle deg, sigma z, amplitude m, mirrored).
# The front is angle 0 (-Y); positive angles turn toward +X. Amplitude > 0 pushes out, < 0 presses in.
# The brow bar and the nose ridge make one T-shaped plane; the eyes are almond recesses under it (the Innerscene
# reference's clarity, 01 section 3); the cheekbones are sharp with a hollow beneath; the mouth is an opening.
FEATURES = [("nose",        0.0, 0.437, 5.5, 0.026, +0.021, False),
            ("noseTip",     0.0, 0.413, 6.0, 0.008, +0.005, False),
            ("browBar",     0.0, 0.488, 28.0, 0.0075, +0.0075, False),
            ("glabella",    0.0, 0.474, 6.0, 0.012, +0.006, False),
            ("socket",     24.0, 0.463, 11.0, 0.0085, -0.017, True),
            ("cheekbone",  42.0, 0.437, 9.0, 0.011, +0.012, True),
            ("cheekHollow", 40.0, 0.403, 12.0, 0.014, -0.007, True),
            ("jawLine",    65.0, 0.350, 14.0, 0.008, +0.004, True),
            ("temple",     68.0, 0.490, 12.0, 0.020, -0.004, True),
            ("chin",        0.0, 0.347, 10.0, 0.009, +0.009, False),
            ("upperLip",    0.0, 0.3935, 14.0, 0.0035, +0.003, False),
            ("lowerLip",    0.0, 0.3725, 13.0, 0.0045, +0.004, False),
            ("mouth",       0.0, 0.383, 0.0, 0.0058, -0.022, False)]   # the opening: its own shape
MOUTH_HALF_DEG = 14.0      # the opening's half width, in degrees round the face
EYE_Z = 0.463
EYE_DEG = 24.0
MOUTH_Z = 0.383


def mouth_shape(da, z):
    """0..1: the mouth opening's profile (a rounded rectangle, superelliptic in both directions)."""
    return math.exp(-abs(da / MOUTH_HALF_DEG) ** 4) * math.exp(-abs((z - MOUTH_Z) / 0.0058) ** 4)


def rings_z():
    """Ring heights: coarse on the base and neck, fine through the face, finest across the mouth."""
    zs, z = [], 0.0
    while z < 0.300:
        zs.append(z); z += 0.008
    while z < 0.520:
        zs.append(z); z += (0.0010 if 0.370 < z < 0.396 else 0.0018)
    while z < 0.588:
        zs.append(z); z += 0.0040
    return zs  # the pole is added separately


def interp(z):
    """Monotone cubic (Fritsch-Carlson) interpolation of every profile column at z."""
    P = PROFILE
    cols = len(P[0]) - 1
    i = max(0, min(len(P) - 2, next((k for k in range(len(P) - 1) if P[k + 1][0] >= z), len(P) - 2)))
    out = []
    for c in range(1, cols + 1):
        xs = [p[0] for p in P]
        ys = [p[c] for p in P]
        d = [(ys[k + 1] - ys[k]) / (xs[k + 1] - xs[k]) for k in range(len(P) - 1)]
        m = [d[0]] + [0.0 if d[k - 1] * d[k] <= 0 else 2.0 / (1.0 / d[k - 1] + 1.0 / d[k]) for k in range(1, len(d))] + [d[-1]]
        h = xs[i + 1] - xs[i]
        t = (z - xs[i]) / h
        h00, h10, h01, h11 = 2*t**3 - 3*t**2 + 1, t**3 - 2*t**2 + t, -2*t**3 + 3*t**2, t**3 - t**2
        out.append(h00 * ys[i] + h10 * h * m[i] + h01 * ys[i + 1] + h11 * h * m[i + 1])
    return out


def feature_offset(adeg, z):
    off = 0.0
    for name, a0, z0, sa, sz, amp, mirrored in FEATURES:
        for sgn in ((1, -1) if mirrored else (1,)):
            da = adeg - sgn * a0
            da = (da + 180.0) % 360.0 - 180.0
            if name == "mouth":
                g = mouth_shape(da, z)
            else:
                g = math.exp(-0.5 * (da / sa) ** 2 - 0.5 * ((z - z0) / sz) ** 2)
            off += amp * g
    return off


def ring_point(adeg, z, W, DF, DB, CY, N):
    """adeg 0 = the front (-Y), 90 = +X."""
    a = math.radians(adeg)
    s, c = math.sin(a), math.cos(a)           # s -> x, c -> -y (front)
    e = 2.0 / N
    x = W * math.copysign(abs(s) ** e, s)
    D = DF if c > 0 else DB
    y = CY - D * math.copysign(abs(c) ** e, c)
    # push along the horizontal outward direction from the ring centre
    dx, dy = x, y - CY
    L = math.hypot(dx, dy) or 1.0
    off = feature_offset(adeg, z)
    return Vector((x + off * dx / L, y + off * dy / L, z))


def build_mesh():
    zs = rings_z()
    S = args.segments
    bm = bmesh.new()
    rings = []
    for z in zs:
        W, DF, DB, CY, N = interp(z)
        rings.append([bm.verts.new(ring_point(360.0 * k / S, z, W, DF, DB, CY, N)) for k in range(S)])
    pole = bm.verts.new((0.0, PROFILE[-1][4], PROFILE[-1][0]))
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(S):
            bm.faces.new((r0[k], r0[(k + 1) % S], r1[(k + 1) % S], r1[k]))
    top = rings[-1]
    for k in range(S):
        bm.faces.new((top[k], top[(k + 1) % S], pole))
    bm.faces.new(list(reversed(rings[0])))  # flat base
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("talking_head_bust")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("talking_head_bust", me)
    bpy.context.scene.collection.objects.link(ob)
    for p in me.polygons:
        p.use_smooth = True
    # the flat base keeps a crisp edge
    me.polygons[-1].use_smooth = False
    return ob


def make_mat(name, rgb, rough, spec=0.5, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular IOR Level"].default_value = spec
    b.inputs["Coat Weight"].default_value = coat
    b.inputs["Coat Roughness"].default_value = 0.2
    return m


def angle_of(p, cy):
    return math.degrees(math.atan2(p.x, -(p.y - cy)))


def assign_regions(ob):
    mats = [make_mat("porcelain", (0.80, 0.74, 0.64), 0.38, 0.5, 0.12),   # warm ivory, restrained coat
            make_mat("mouth_interior", (0.012, 0.010, 0.012), 0.85, 0.1)]  # near black
    for m in mats:
        ob.data.materials.append(m)
    for poly in ob.data.polygons:
        c = poly.center
        cy = interp(min(max(c.z, 0.0), 0.6))[3]
        a = angle_of(c, cy)
        if mouth_shape((a + 180.0) % 360.0 - 180.0, c.z) > 0.5:
            poly.material_index = 1
    # No eye-socket material: a hard region on this grid has stepped edges at 1080p. The sockets darken
    # by their own depth here, and at runtime by a smooth object-space mask in the material program.


def stats(ob):
    me = ob.data
    me.calc_loop_triangles()
    bm = bmesh.new()
    bm.from_mesh(me)
    nonmanifold = sum(1 for e in bm.edges if not e.is_manifold)
    bm.free()
    per = [0, 0]
    for t in me.loop_triangles:
        per[t.material_index] += 1
    lo = [min(v.co[i] for v in me.vertices) for i in range(3)]
    hi = [max(v.co[i] for v in me.vertices) for i in range(3)]
    return {"triangles": len(me.loop_triangles), "vertices": len(me.vertices), "nonManifoldEdges": nonmanifold,
            "trianglesPerMaterial": dict(zip(["porcelain", "mouth_interior"], per)),
            "boundsMinBlender": [round(c, 4) for c in lo], "boundsMaxBlender": [round(c, 4) for c in hi]}


def surface_point(ob, adeg, z):
    """Where a ray from outside, aimed at the ring centre at height z and angle adeg, meets the surface."""
    cy = interp(z)[3]
    a = math.radians(adeg)
    d = Vector((-math.sin(a), math.cos(a), 0.0))      # inward
    o = Vector((0.0, cy, z)) - d * 1.0
    ok, loc, n, _ = ob.ray_cast(o, d)
    return loc if ok else None


def to_gltf(v):  # Blender (x, y, z), Z-up, face -Y  ->  glTF (x, z, -y), Y-up, face +Z
    return [round(v[0], 4), round(v[2], 4), round(-v[1], 4)]


# ------------------------------------------------------------------------------------------- render
def setup_render():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = args.samples
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = 1080, 1080
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "AgX - Medium High Contrast"
    world = bpy.data.worlds.new("ether")
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.010, 0.006, 0.022, 1.0)  # near-black indigo
    bg.inputs["Strength"].default_value = 1.0

    def area(name, loc, energy, color, size):
        L = bpy.data.lights.new(name, "AREA")
        L.energy, L.color, L.size = energy, color, size
        o = bpy.data.objects.new(name, L)
        o.location = loc
        sc.collection.objects.link(o)
        d = Vector((0, 0, 0.42)) - Vector(loc)
        o.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        return o
    area("key", (-0.55, -0.75, 0.80), 55.0, (1.0, 0.84, 0.66), 0.5)     # warm key, high left
    area("rim", (0.55, 0.60, 0.70), 50.0, (0.60, 0.40, 1.0), 0.35)      # violet rim, behind right
    area("fill", (0.65, -0.55, 0.30), 5.0, (0.50, 0.45, 1.0), 0.8)      # faint blue-purple fill
    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = 85
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    return cam


def shoot(cam, yaw_deg, name, dist=1.65, height=0.42, aim_z=0.33):
    a = math.radians(yaw_deg)
    cam.location = (dist * math.sin(a), -dist * math.cos(a), height)
    d = Vector((0, 0, aim_z)) - cam.location
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.render.filepath = os.path.join(PREV, name)
    bpy.ops.render.render(write_still=True)


def silhouette(cam, ob, name):
    """Black bust on white, front and three-quarter: does the silhouette read as a head?"""
    sc = bpy.context.scene
    sc.world.node_tree.nodes["Background"].inputs["Color"].default_value = (1, 1, 1, 1)
    hold = make_mat("hold", (0, 0, 0), 1.0, 0.0)
    saved = [s.material for s in ob.material_slots]
    for s in ob.material_slots:
        s.material = hold
    for L in [o for o in sc.objects if o.type == "LIGHT"]:
        L.hide_render = True
    sc.cycles.samples = 4
    shoot(cam, 0, name + "-front.png")
    shoot(cam, 35, name + "-34.png")
    shoot(cam, 90, name + "-profile.png")
    for s, m in zip(ob.material_slots, saved):
        s.material = m


NECK_PIVOT_Z = 0.300   # the neck joint: the upper neck just below the jaw (the owner's circled point)
TILT_TEST_DEG = 32.0   # the tilt the seam is checked at


def split_piece(src, name, keep_above):
    """A copy of the bust cut at the neck joint's plane: the head (above) or the body (below)."""
    me = src.data.copy()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    bm = bmesh.new()
    bm.from_mesh(me)
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(0, 0, NECK_PIVOT_Z), plane_no=(0, 0, 1),
                           clear_inner=keep_above, clear_outer=not keep_above)
    bm.to_mesh(me)
    bm.free()
    return ob


def add_joint_shapes(ob, up, collar):
    """Close a piece at the joint with a dome of the neck's own cross-section, pushed 0.6 mm inside it: the body's dome
    rises into the head (it fills the gap that opens at the front of the throat when the head tilts back), the head's
    dome hangs into the body. The body also gets a thin collar ring over the seam: a mannequin's neck joint."""
    W, DF, DB, CY, _N = interp(NECK_PIVOT_Z)
    ry = 0.5 * (DF + DB)
    cy = CY + 0.5 * (DB - DF)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    rings, seg = 10, 48
    verts = []
    for i in range(rings + 1):
        t = (math.pi / 2) * i / rings
        r, h = math.cos(t), math.sin(t) * (1.0 if up else -1.0)
        row = []
        for k in range(seg if i < rings else 1):
            a = 2.0 * math.pi * k / seg
            row.append(bm.verts.new(((W - 0.0006) * r * math.cos(a), cy + (ry - 0.0006) * r * math.sin(a),
                                     NECK_PIVOT_Z + 0.85 * ry * h)))
        verts.append(row)
    faces = []
    for i in range(rings - 1):
        for k in range(seg):
            q = (verts[i][k], verts[i][(k + 1) % seg], verts[i + 1][(k + 1) % seg], verts[i + 1][k])
            faces.append(bm.faces.new(q if up else q[::-1]))
    for k in range(seg):
        q = (verts[rings - 1][k], verts[rings - 1][(k + 1) % seg], verts[rings][0])
        faces.append(bm.faces.new(q if up else q[::-1]))
    if collar:
        # an elliptical tube round the seam: major radii 4% over the neck, minor radius 3.5 mm
        segs, sides, mr = 64, 10, 0.0035
        ring = []
        for k in range(segs):
            a = 2.0 * math.pi * k / segs
            cx, cyy = (W * 1.04) * math.cos(a), cy + (ry * 1.04) * math.sin(a)
            nx, ny = math.cos(a) / (W * 1.04), math.sin(a) / (ry * 1.04)
            nl = math.hypot(nx, ny)
            nx, ny = nx / nl, ny / nl
            ring.append([bm.verts.new((cx + mr * math.cos(b) * nx, cyy + mr * math.cos(b) * ny,
                                       NECK_PIVOT_Z + mr * math.sin(b)))
                         for b in (2.0 * math.pi * j / sides for j in range(sides))])
        for k in range(segs):
            for j in range(sides):
                faces.append(bm.faces.new((ring[k][j], ring[(k + 1) % segs][j], ring[(k + 1) % segs][(j + 1) % sides],
                                           ring[k][(j + 1) % sides])))
    for f in faces:
        f.material_index = 0
        f.smooth = True
    bm.to_mesh(ob.data)
    bm.free()


def shift(ob, by):
    for v in ob.data.vertices:
        v.co -= by
    ob.data.update()


def ghost_material():
    """The ghost: the same surface, one blended material (its program writes its colour and opacity at runtime)."""
    ghost = make_mat("ghost", (0.0, 0.0, 0.0), 0.5, 0.0)
    ghost.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 0.4
    ghost.surface_render_method = "BLENDED"
    return ghost


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ob = build_mesh()
    assign_regions(ob)
    mouth = surface_point(ob, 0.0, MOUTH_Z)
    lip = surface_point(ob, 0.0, MOUTH_Z + 0.012)
    eyes = [surface_point(ob, s * EYE_DEG, EYE_Z) for s in (1, -1)]   # +X first = the bust's LEFT eye
    centre = Vector((0.0, interp(0.465)[3], 0.465))
    pivot = Vector((0.0, interp(NECK_PIVOT_Z)[3], NECK_PIVOT_Z))
    # the two pieces: the head (with the upper neck) and the body (lower neck and bust), each closed at the joint
    head = split_piece(ob, "th-head", True)
    body = split_piece(ob, "th-body", False)
    add_joint_shapes(head, up=False, collar=False)
    add_joint_shapes(body, up=True, collar=True)
    shift(head, pivot)  # the head's origin IS the joint, so turning its node turns it about the neck
    H = lambda v: to_gltf(v - pivot)
    anchors = {
        "note": "tools/talking_heads/make_bust.py. Frames are the exported GLBs' (+Y up, face +Z, metres). 'head' points "
                "are in th-head.glb's own frame, whose origin is the neck joint; 'body' points in th-body.glb's frame "
                "(origin at the base's bottom centre). Place the head node at body.neckPivot as a child of the body; "
                "turning it about its X axis tilts the head (negative = chin up, back). eyeLeft is the bust's own "
                "left (+X). mouthBack is the back of the mouth cavity (the hero beam's apex).",
        "head": {"mouth": H(Vector((0.0, lip.y, MOUTH_Z))), "mouthBack": H(mouth), "eyeLeft": H(eyes[0]),
                 "eyeRight": H(eyes[1]), "headCentre": H(centre), "forward": [0.0, 0.0, 1.0], "up": [0.0, 1.0, 0.0]},
        "body": {"neckPivot": to_gltf(pivot), "tiltSeamTestedDeg": TILT_TEST_DEG},
        "mesh": {"head": stats(head), "body": stats(body), "whole": stats(ob)},
    }
    with open(os.path.join(OUT, "th-bust.anchors.json"), "w") as f:
        json.dump(anchors, f, indent=1)
        f.write("\n")
    export(head, os.path.join(OUT, "th-head.glb"))
    export(body, os.path.join(OUT, "th-body.glb"))
    gm = ghost_material()
    for piece, name in ((head, "th-head-ghost.glb"), (body, "th-body-ghost.glb")):
        keep = [m for m in piece.data.materials]
        piece.data.materials.clear()
        piece.data.materials.append(gm)
        export(piece, os.path.join(OUT, name))
        piece.data.materials.clear()
        for m in keep:
            piece.data.materials.append(m)
    print("BUST_STATS", json.dumps(anchors["mesh"]))
    if not PREV:
        return
    bpy.data.objects.remove(ob)
    head.location = pivot  # reassemble for the previews
    cam = setup_render()
    shoot(cam, 0, "bust-front.png")
    shoot(cam, 35, "bust-three-quarter.png")
    shoot(cam, -90, "bust-profile.png")
    shoot(cam, 20, "bust-face-close.png", dist=0.75, height=0.47, aim_z=0.43)
    bpy.context.scene.camera.data.lens = 50
    shoot(cam, 28, "bust-engine-framing.png", dist=1.9, height=0.55, aim_z=0.36)
    # the seam at full tilt, chin up: a NEGATIVE turn about X (+X tips the face, which looks down -Y, downward)
    head.rotation_euler = (-math.radians(TILT_TEST_DEG), 0.0, 0.0)
    shoot(cam, -90, "bust-tilt-profile.png", dist=2.0)
    shoot(cam, -35, "bust-tilt-three-quarter.png")
    shoot(cam, -20, "bust-tilt-throat-close.png", dist=0.7, height=0.30, aim_z=0.31)
    bpy.context.scene.camera.data.lens = 85


def export(ob, path):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_normals=True, export_texcoords=False,
                              export_materials="EXPORT")

main()
