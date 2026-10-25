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
- th-body.glb, th-neck.glb, th-head.glb, th-jaw.glb: the four articulated pieces (see "the articulation" below);
- th-mouth-cavity.glb (head frame), th-jaw-floor.glb (jaw frame): the mouth's near-black interior;
- th-head-ghost.glb, th-jaw-ghost.glb: the head and jaw, one alpha-BLEND material, for the spectral copies;
- th-bust.anchors.json: the hierarchy, the joints, the emitter anchors in each piece's frame, mesh statistics.
The head tilts back on a neck that BENDS (the owner, stop 1: "the neck to snap back"; no ridge at the joint) and the jaw
hinges open; the mouth's dark interior shows when it does.

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


# The owner (stop 1, 2026-10-25): "shorten the neck some". Everything from the jaw up is authored above at its old
# height and dropped by NECK_DROP here; the long-neck control point between them goes. The neck (shoulders to jaw)
# is 30% shorter.
NECK_DROP = 0.050
PROFILE = [(z - NECK_DROP if z >= 0.29 else z,) + tuple(rest) for (z, *rest) in PROFILE if not 0.2 < z < 0.29]
FEATURES = [(n, a, z - NECK_DROP, sa, sz, amp, m) for (n, a, z, sa, sz, amp, m) in FEATURES]
EYE_Z -= NECK_DROP
MOUTH_Z -= NECK_DROP
# The owner (stop 1): "give the bust an adam's apple too". A soft, restrained bump on the front of the shortened neck,
# on the body piece just below the bend's foot (it does not move when the head snaps back, and the chin swings away
# from it). Authored at its final height.
FEATURES.append(("adamsApple", 0.0, 0.188, 11.0, 0.0085, +0.0065, False))


def mouth_shape(da, z):
    """0..1: the mouth opening's profile (a rounded rectangle, superelliptic in both directions)."""
    return math.exp(-abs(da / MOUTH_HALF_DEG) ** 4) * math.exp(-abs((z - MOUTH_Z) / 0.0058) ** 4)


def rings_z():
    """Ring heights: coarse on the base and neck, fine through the face, finest across the mouth."""
    zs, z = [], 0.0
    d = NECK_DROP
    while z < 0.300 - d:
        zs.append(z); z += 0.008
    while z < 0.520 - d:
        zs.append(z); z += (0.0010 if 0.370 - d < z < 0.396 - d else 0.0018)
    while z < 0.588 - d:
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


# ------------------------------------------------------------------------------------------- the articulation
# Four pieces (the owner, 2026-10-25: the neck snaps back and the mouth opens on a gate; no ridge at the neck):
#   body  -- shoulders and lower neck, up to NECK_LOW;            rigid
#   neck  -- the neck between NECK_LOW and NECK_TOP;                BENT by the engine's Bend deformer (local, about +Y,
#            curving toward -Z = the back), so it stays joined to the body at its foot and carries the head at its
#            top: one continuous porcelain neck, no seam that opens, no ring
#   head  -- everything above NECK_TOP except the jaw;             rigid, turned and moved to the bent neck's top
#   jaw   -- the chin block below the mouth line, hinged at JAW_HINGE; rigid, a child of the head
# Every cut runs along an existing ring of the mesh, and every piece takes the WHOLE mesh's smooth normals, so the
# pieces meet flush and shade as one surface.
def _ring_near(target):
    return min(rings_z(), key=lambda z: abs(z - target))


NECK_TOP = _ring_near(0.316 - NECK_DROP - 0.016)   # just below the jaw (the owner's circled point)
NECK_LOW = _ring_near(NECK_TOP - 0.048)            # the bend's foot, mid-neck
JAW_HALF_DEG = 62.0                                # the jaw block's half width round the face
JAW_BOTTOM_Z = 0.312 - NECK_DROP                   # under the chin: the block's underside (above NECK_TOP)
JAW_HINGE = (0.0, None, MOUTH_Z + 0.010)           # y filled in from the profile (the head's centre line)
TILT_TEST_DEG = 32.0
JAW_TEST_DEG = 14.0
JOINT_INSET = 0.004            # the junction domes sit 4 mm inside the walls: hidden, and never coincident


def in_jaw(c, f=None):
    """The chin block: below the mouth line, above the chin's underside, inside the front sector. The sector is read
    from the faces' SEGMENT indices (build_mesh makes the ring vertices in order), so its side cuts run straight down
    a segment line instead of stepping where the superellipse's angle and the ring's own angle disagree."""
    if not max(JAW_BOTTOM_Z, NECK_TOP) < c.z < MOUTH_Z:
        return False
    if f is None:
        cy = interp(min(max(c.z, 0.0), 0.6))[3]
        a = angle_of(c, cy)
        return abs((a + 180.0) % 360.0 - 180.0) < JAW_HALF_DEG
    S = args.segments
    for v in f.verts:
        k = v.index % S
        a = (360.0 * k / S + 180.0) % 360.0 - 180.0
        if abs(a) > JAW_HALF_DEG + 1e-6:
            return False
    return True


def piece(src, name, keep):
    """A copy of the whole mesh keeping the faces `keep(centre, face)` accepts (vertex indices are the original
    mesh's, ring by ring)."""
    me = src.data.copy()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not keep(f.calc_center_median(), f)], context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(me)
    bm.free()
    return ob


def copy_smooth_normals(whole, ob):
    """Give a piece the WHOLE mesh's smooth vertex normals, so the cuts shade as one surface."""
    key = lambda co: (round(co.x, 6), round(co.y, 6), round(co.z, 6))
    normals = {key(v.co): v.normal.copy() for v in whole.data.vertices}
    ob.data.normals_split_custom_set_from_vertices([normals.get(key(v.co), v.normal) for v in ob.data.vertices])


def add_cap_dome(ob, z, up, depth_scale=0.6):
    """A dome on the ring's own cross-section at height z, JOINT_INSET inside the wall, closing a piece: hidden at
    rest, it fills any sliver the neck-head junction opens (the head is placed on the bent neck to a millimetre)."""
    W, DF, DB, CY, N = interp(z)
    depth = 0.5 * (DF + DB)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    rings, seg = 8, 104
    verts = []
    for i in range(rings + 1):
        t = (math.pi / 2) * i / rings
        r, h = math.cos(t), math.sin(t) * (1.0 if up else -1.0)
        row = []
        for k in range(seg if i < rings else 1):
            p = ring_point(360.0 * k / seg, z, W, DF, DB, CY, N)
            dx, dy = p.x, p.y - CY
            L = math.hypot(dx, dy) or 1.0
            sc = r * (1.0 - JOINT_INSET / L)
            row.append(bm.verts.new((dx * sc, CY + dy * sc, z + depth_scale * depth * h)))
        verts.append(row)
    faces = []
    for i in range(rings - 1):
        for k in range(seg):
            q = (verts[i][k], verts[i][(k + 1) % seg], verts[i + 1][(k + 1) % seg], verts[i + 1][k])
            faces.append(bm.faces.new(q[::-1] if up else q))
    for k in range(seg):
        q = (verts[rings - 1][k], verts[rings - 1][(k + 1) % seg], verts[rings][0])
        faces.append(bm.faces.new(q[::-1] if up else q))
    for f in faces:
        f.material_index = 0
        f.smooth = True
    bm.normal_update()
    bm.to_mesh(ob.data)
    bm.free()


def add_dark_ellipsoid(ob, centre, radii, mat_index, inward=False):
    """A dark ellipsoid (the mouth's interior): what the opening jaw shows."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    res = bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=1.0)
    for v in res["verts"]:
        v.co = Vector((centre[0] + v.co.x * radii[0], centre[1] + v.co.y * radii[1], centre[2] + v.co.z * radii[2]))
    faces = list({f for v in res["verts"] for f in v.link_faces})
    for f in faces:
        f.material_index = mat_index
        f.smooth = True
    if inward:
        bmesh.ops.reverse_faces(bm, faces=faces)
    bm.normal_update()
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


def neck_bend(theta, F):
    """The engine's Bend (procedural.cpp applyBend, falloff F, centre at the neck's foot, axis +Z, bending toward +Y in
    Blender = the back): where the top of the bend region goes. Returns (dy, dz) of the axis point at height F and
    the angle the region above it turns by. For the scene's head placement and the previews."""
    if abs(theta) < 1e-9:
        return 0.0, 0.0
    R = F / theta
    return R * (1.0 - math.cos(theta)), R * math.sin(theta) - F


def bend_mesh(ob, theta, z0, F):
    """Apply the same Bend to a Blender mesh (previews only): Barr bend about the X axis, toward +Y."""
    if abs(theta) < 1e-9:
        return
    k = theta / F
    R = 1.0 / k
    cy = interp(z0)[3]
    for v in ob.data.vertices:
        y = v.co.z - z0
        x = v.co.y - cy
        yb_ = max(-F, min(F, y))
        rest = y - yb_
        th = k * yb_
        xb = R - (R - x) * math.cos(th) + rest * math.sin(th)
        ybb = (R - x) * math.sin(th) + rest * math.cos(th)
        v.co.y, v.co.z = cy + xb, z0 + ybb
    ob.data.update()


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ob = build_mesh()
    assign_regions(ob)
    mouth = surface_point(ob, 0.0, MOUTH_Z)
    lip = surface_point(ob, 0.0, MOUTH_Z + 0.012)
    eyes = [surface_point(ob, s * EYE_DEG, EYE_Z) for s in (1, -1)]   # +X first = the bust's LEFT eye
    centre = Vector((0.0, interp(0.465 - NECK_DROP)[3], 0.465 - NECK_DROP))
    neck_foot = Vector((0.0, interp(NECK_LOW)[3], NECK_LOW))
    head_origin = Vector((0.0, interp(NECK_LOW)[3], NECK_TOP))      # straight above the neck's foot: the bend's axis
    hinge = Vector((0.0, interp(MOUTH_Z)[3] - 0.010, JAW_HINGE[2]))
    body = piece(ob, "th-body", lambda c, f: c.z < NECK_LOW)
    neck = piece(ob, "th-neck", lambda c, f: NECK_LOW < c.z < NECK_TOP)
    head = piece(ob, "th-head", lambda c, f: c.z > NECK_TOP and not in_jaw(c, f))
    jaw = piece(ob, "th-jaw", lambda c, f: in_jaw(c, f))
    for p_ in (body, neck, head, jaw):
        copy_smooth_normals(ob, p_)
    # the ghosts' copies are taken BEFORE the junction domes: a spectral copy is offset sideways, and the head's dome
    # (hidden inside the neck on the real head) would stick out of the neck as a dark band on the copy
    head_g = piece(head, "th-head-g", lambda c, f: True)
    jaw_g = piece(jaw, "th-jaw-g", lambda c, f: True)
    copy_smooth_normals(ob, head_g)
    copy_smooth_normals(ob, jaw_g)
    add_cap_dome(neck, NECK_TOP, up=True, depth_scale=0.3)
    add_cap_dome(head, NECK_TOP, up=False, depth_scale=0.22)
    # the mouth's dark interior, as SEPARATE meshes (the scene draws them as plain gltf children with their own near-black
    # material: a procedural node's one program would paint them porcelain): a cavity in the head behind the jaw block,
    # and a dark floor riding the jaw
    def dark_piece(name, centre, radii):
        o = bpy.data.objects.new(name, bpy.data.meshes.new(name))
        bpy.context.scene.collection.objects.link(o)
        o.data.materials.append(make_mat("mouth_interior", (0.012, 0.010, 0.012), 0.85, 0.1))
        add_dark_ellipsoid(o, centre, radii, 0)
        return o
    cavity = dark_piece("th-mouth-cavity", (0.0, interp(MOUTH_Z)[3] - 0.040, MOUTH_Z - 0.006), (0.042, 0.046, 0.026))
    floor = dark_piece("th-jaw-floor", (0.0, interp(MOUTH_Z)[3] - 0.046, MOUTH_Z - 0.014), (0.040, 0.040, 0.016))
    shift(neck, neck_foot)
    shift(head, head_origin)
    shift(cavity, head_origin)
    shift(jaw, hinge)
    shift(floor, hinge)
    shift(head_g, head_origin)
    shift(jaw_g, hinge)
    # frames (glTF): neck at its foot, head at the bend axis above it, jaw at its hinge in the head's frame
    G = lambda v: to_gltf(v)
    F = NECK_TOP - NECK_LOW
    anchors = {
        "note": "tools/talking_heads/make_bust.py. Frames are the exported GLBs' (+Y up, face +Z, metres). Hierarchy: "
                "body (origin at the base's bottom centre) > neck at body.neckFoot (bent by a Bend deformer: local, "
                "axis +Y, displacementAxis -Z, falloff neck.bendLength, centre 0) ; head (a child of the BODY, at "
                "body.headRest, turned by the bend's angle and moved by neck.headOffset to the bent neck's top) > jaw "
                "at head.jawHinge (a positive turn about X opens it). jaw.mouth is the mouth beams' origin, inside the "
                "opening, in the jaw's frame. head.eyeLeft/eyeRight are the eye recesses (for the darting-eyes pass).",
        "body": {"neckFoot": G(neck_foot), "headRest": G(head_origin)},
        "neck": {"bendLength": round(F, 4), "headOffsetNote": "for a tilt back of theta (rad): the head turns by "
                 "-theta about X and moves by (0, (R sin(theta) - F), -R (1 - cos(theta))) with R = F / theta"},
        "head": {"jawHinge": G(hinge - head_origin), "eyeLeft": G(eyes[0] - head_origin),
                 "eyeRight": G(eyes[1] - head_origin), "headCentre": G(centre - head_origin),
                 "mouthLip": G(Vector((0.0, lip.y, MOUTH_Z)) - head_origin), "forward": [0.0, 0.0, 1.0],
                 "up": [0.0, 1.0, 0.0]},
        "jaw": {"mouth": G(Vector((0.0, mouth.y - 0.004, MOUTH_Z + 0.001)) - hinge)},
        "test": {"tiltDeg": TILT_TEST_DEG, "jawDeg": JAW_TEST_DEG},
        "mesh": {n.name: stats(n) for n in (body, neck, head, jaw)} | {"whole": stats(ob)},
    }
    with open(os.path.join(OUT, "th-bust.anchors.json"), "w") as f:
        json.dump(anchors, f, indent=1)
        f.write("\n")
    for p_, name in ((body, "th-body"), (neck, "th-neck"), (head, "th-head"), (jaw, "th-jaw"),
                     (cavity, "th-mouth-cavity"), (floor, "th-jaw-floor")):
        export(p_, os.path.join(OUT, name + ".glb"))
    gm = ghost_material()
    for p_, name in ((head_g, "th-head-ghost.glb"), (jaw_g, "th-jaw-ghost.glb")):
        keep = list(p_.data.materials)
        p_.data.materials.clear()
        p_.data.materials.append(gm)
        export(p_, os.path.join(OUT, name))
        p_.data.materials.clear()
        for m in keep:
            p_.data.materials.append(m)
    print("BUST_STATS", json.dumps(anchors["mesh"]))
    if not PREV:
        return
    bpy.data.objects.remove(ob)
    neck_rest = neck.data.copy()
    cam = setup_render()

    def pose(tilt_deg, jaw_deg):
        th = math.radians(tilt_deg)
        neck.data = neck_rest.copy()
        shift(neck, -neck_foot)
        bend_mesh(neck, th, NECK_LOW, F)
        dy, dz = neck_bend(th, F)
        head.location = head_origin + Vector((0.0, dy, dz))
        head.rotation_euler = (-th, 0.0, 0.0)
        jaw.parent = head
        jaw.location = hinge - head_origin
        jaw.rotation_euler = (math.radians(jaw_deg), 0.0, 0.0)
        cavity.parent = head
        floor.parent = jaw

    pose(0.0, 0.0)
    shoot(cam, 0, "bust-front.png")
    shoot(cam, 35, "bust-three-quarter.png")
    shoot(cam, -90, "bust-profile.png")
    shoot(cam, 20, "bust-face-close.png", dist=0.75, height=0.42, aim_z=0.38)
    pz = NECK_TOP
    for deg, jd in ((0, 0), (20, JAW_TEST_DEG), (int(TILT_TEST_DEG), JAW_TEST_DEG)):
        pose(deg, jd)
        shoot(cam, -90, "neck-%02ddeg-profile-close.png" % deg, dist=0.55, height=pz + 0.03, aim_z=pz)
        shoot(cam, -8, "neck-%02ddeg-front-close.png" % deg, dist=0.55, height=pz + 0.03, aim_z=pz)
        shoot(cam, -35, "bust-%02ddeg-three-quarter.png" % deg, dist=1.65, height=0.40, aim_z=0.29)
        shoot(cam, -90, "bust-%02ddeg-profile.png" % deg, dist=1.65, height=0.40, aim_z=0.29)
    pose(0.0, JAW_TEST_DEG)
    shoot(cam, -25, "jaw-open-close.png", dist=0.6, height=MOUTH_Z + 0.03, aim_z=MOUTH_Z)
    pose(0.0, 0.0)
    shoot(cam, -25, "jaw-shut-close.png", dist=0.6, height=MOUTH_Z + 0.03, aim_z=MOUTH_Z)


def export(ob, path):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_normals=True, export_texcoords=False,
                              export_materials="EXPORT")

main()
