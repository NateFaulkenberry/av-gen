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
- th-body.glb, th-neck.glb, th-head.glb: the three articulated pieces (see "the articulation" below); the head's
  mouth is a slit between closed lips that the engine's Hinge deformer opens (ADR-1672, cut_mouth);
- th-mouth-cavity.glb (head frame): the mouth's near-black interior, seen only through the parted lips;
- th-eye-white.glb, th-eye-iris.glb: an eyeball's white and its dark iris disc (facing +Z); th-lid-upper/lower.glb:
  its lids;
- th-head-ghost.glb: the head, one alpha-BLEND material, for the spectral copies;
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
from mathutils import Matrix, Vector

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
PROFILE = [(0.000, 0.176, 0.100, 0.084, 0.012, 3.4),   # flat base: a cut classical bust with a real chest (stop 3:
           (0.030, 0.184, 0.104, 0.086, 0.012, 3.4),   # the shallow chest read as a column in profile)
           (0.072, 0.176, 0.098, 0.082, 0.012, 3.0),   # shoulder edge
           (0.108, 0.126, 0.074, 0.070, 0.014, 2.4),   # trapezius slope
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
            ("mouth",       0.0, 0.383, 0.0, 0.0058, -0.0015, False)]  # a faint parting line; the lips close
MOUTH_HALF_DEG = 17.0      # the opening's half width, in degrees round the face
EYE_Z = 0.463
EYE_DEG = 24.0
MOUTH_Z = 0.383


# The owner (stops 1 and 2): "shorten the neck some" ... "the neck still reads long". Everything from the jaw up is
# authored above at its old height and dropped by NECK_DROP here; the long-neck control point between them goes. The
# neck (shoulders to jaw) is 39% shorter than the probe's.
NECK_DROP = 0.065
PROFILE = [(z - NECK_DROP if z >= 0.29 else z,) + tuple(rest) for (z, *rest) in PROFILE if not 0.2 < z < 0.29]
FEATURES = [(n, a, z - NECK_DROP, sa, sz, amp, m) for (n, a, z, sa, sz, amp, m) in FEATURES]
EYE_Z -= NECK_DROP
MOUTH_Z -= NECK_DROP
# The owner (stops 1 and 2): "give the bust an adam's apple too" ... "adams apple needs to be moved up a bit". One soft,
# clean bump high on the throat, about 3 cm under the jaw's underside, in the bending part of the neck (it bends with
# the neck, and the chin swings up and away from it as the head snaps back). Authored at its final height.
FEATURES.append(("adamsApple", 0.0, 0.316 - NECK_DROP - 0.030, 13.0, 0.0095, +0.0068, False))


def mouth_shape(da, z):
    """0..1: the mouth opening's profile (a rounded rectangle, superelliptic in both directions)."""
    return math.exp(-abs(da / MOUTH_HALF_DEG) ** 4) * math.exp(-abs((z - MOUTH_Z) / 0.0058) ** 4)


def rings_z():
    """Ring heights: coarse on the base and neck, fine through the face, finest across the mouth."""
    zs, z = [], 0.0
    d = NECK_DROP
    while z < 0.140:
        zs.append(z); z += 0.008
    while z < 0.300 - d:   # the neck bends: fine rings, so the bent surface stays smooth (no facets, no wobble)
        zs.append(z); z += 0.003
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


NECK_TOP = _ring_near(0.316 - NECK_DROP - 0.014)   # just below the jaw (the owner's circled point)
NECK_LOW = _ring_near(NECK_TOP - 0.075)            # the bend's foot, where the neck meets the shoulders: the whole
                                                   # neck bends, with a gentle curvature (no notch at the nape)
JAW_HALF_DEG = 50.0                                # the jaw's half width round the face
JAW_CORE_DEG = 42.0                                # the head keeps its skin over the jaw's sides beyond this, and
JAW_SKIRT = 0.006                                  # over its lower edge by this much: the jaw slides under the head
JAW_INSET = 0.0006                                 # how far the jaw's hidden edges sit under the head's skin
JAW_BOTTOM_Z = 0.312 - NECK_DROP                   # under the chin: the block's underside (above NECK_TOP)
JAW_HINGE = (0.0, None, MOUTH_Z + 0.010)           # y filled in from the profile (the head's centre line)
TILT_TEST_DEG = 32.0
JAW_TEST_DEG = 10.0
EYE_RADIUS = 0.0125            # the eyeballs: 25 mm across, seated in the 17 mm recesses
EYE_SEAT = 0.0050              # how far behind the recess surface the eyeball's centre sits
JOINT_INSET = 0.004            # the junction domes sit 4 mm inside the walls: hidden, and never coincident


def in_jaw(c, f=None, half=None, skirt=0.0):
    """The chin block: below the mouth line, above the chin's underside, inside the front sector. The sector is read
    from the faces' SEGMENT indices (build_mesh makes the ring vertices in order), so its side cuts run straight down
    a segment line instead of stepping where the superellipse's angle and the ring's own angle disagree."""
    half = JAW_HALF_DEG if half is None else half
    if not max(JAW_BOTTOM_Z, NECK_TOP) + skirt < c.z < MOUTH_Z:
        return False
    if f is None:
        cy = interp(min(max(c.z, 0.0), 0.6))[3]
        a = angle_of(c, cy)
        return abs((a + 180.0) % 360.0 - 180.0) < half
    S = args.segments
    for v in f.verts:
        k = v.index % S
        a = (360.0 * k / S + 180.0) % 360.0 - 180.0
        if abs(a) > half + 1e-6:
            return False
    return True


def sink_jaw_edges(jaw):
    """The softened jaw (the owner, stop 2): the head keeps its skin over the jaw's sides (beyond JAW_CORE_DEG) and its
    lower edge (JAW_SKIRT), and the jaw's own surface there sinks JAW_INSET under it, easing in over 4 degrees / 4 mm. So
    at rest the face reads as one surface with a fine seam, and as the jaw drops its sides slide under the cheeks: no
    dark wedges at the sides of the chin."""
    def sm(a, b, x):
        u = max(0.0, min(1.0, (x - a) / (b - a)))
        return u * u * (3.0 - 2.0 * u)
    for v in jaw.data.vertices:
        cy = interp(min(max(v.co.z, 0.0), 0.6))[3]
        a = abs((angle_of(v.co, cy) + 180.0) % 360.0 - 180.0)
        w = max(sm(JAW_CORE_DEG - 4.0, JAW_CORE_DEG, a),
                sm(JAW_BOTTOM_Z + JAW_SKIRT + 0.004, JAW_BOTTOM_Z + JAW_SKIRT, v.co.z))
        if w > 0.0:
            v.co -= v.normal * (JAW_INSET * w)
    jaw.data.update()


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


def eye_centre(surface, side):
    """The eyeball's centre: EYE_SEAT behind the recess surface point, along the recess's axis (the face's forward,
    turned a little outward like a real gaze at rest)."""
    a = math.radians(side * 8.0)
    d = Vector((math.sin(a), -math.cos(a), 0.0))   # Blender: forward is -Y
    return surface - d * EYE_SEAT


MOUTH_SLIT_DEG = 16.0      # the slit (the lips' parting) runs this far either side of the front
HINGE_DOWN = -0.063        # the hinge mask's centre sits this far below the slit (here above it) ...
HINGE_RADIUS = 0.140       # ... with this radius: its lowest point stays above the head piece's bottom (the neck join),
                           # and its ease spans the whole under-chin (fx2: at 0.055 the ease ran over 3 cm while the
                           # chin swung 2.5 cm, and the skin folded into a dark pocket under the open jaw; at 0.10 the lip
                           # ramp, r/4 tall, still sheared the cheek beside the chin; seam_check's jaw views hold it)
LIP_GAP = 0.0006           # the closed lips sit this far apart: a fine dark line
LIP_ROLL = 0.012           # how deep the lips' inner walls roll into the mouth
IRIS_DEG = 29.0            # the iris's angular radius from the gaze (its edge lands on the sphere's 6th ring, 27 deg)
LID_GAP = 0.0012           # the lids' shells sit this far outside the eyeball
LID_UPPER = (24.0, 82.0)   # elevation range of the upper lid at rest (degrees above the eye's horizontal)
LID_LOWER = (-82.0, -30.0)


def cut_mouth(ob):
    """The mouth (stop 3: "the mouth should completely close ... not this cut out thing"). The surface is SLIT along the
    mouth's ring between its corners -- the corners stay joined -- the two lips are set LIP_GAP apart (a fine line), and
    each lip's edge rolls LIP_ROLL into the head as a near-black wall. The engine's Hinge deformer (ADR-1672) turns
    what lies below the slit inside a soft mask about a jaw pivot: the lower lip and chin drop, the corners and cheeks
    stretch, and the walls and the dark cavity behind show through the parted lips."""
    S = args.segments
    zs = rings_z()
    iz = min(range(len(zs)), key=lambda i: abs(zs[i] - MOUTH_Z))
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    ks = [k for k in range(-S // 2, S // 2 + 1) if abs(360.0 * k / S) <= MOUTH_SLIT_DEG]
    chain = [bm.verts[iz * S + (k % S)] for k in ks]
    edges = [bm.edges.get((a, b)) for a, b in zip(chain, chain[1:])]
    ends = {chain[0], chain[-1]}
    corner = max(abs(chain[0].co.x), abs(chain[-1].co.x))   # the cut's half-width: the Hinge's `frequency`
    res = bmesh.ops.split_edges(bm, edges=[e for e in edges if e is not None])
    zm = zs[iz]
    upper, lower = [], []
    for e in bm.edges:
        if not e.is_boundary:
            continue
        if not all(abs(v.co.z - zm) < 1e-6 for v in e.verts):
            continue
        f = e.link_faces[0]
        (upper if f.calc_center_median().z > zm else lower).append(e)
    for group, sgn in ((upper, 1.0), (lower, -1.0)):
        for v in {v for e in group for v in e.verts} - ends:
            v.co.z += sgn * 0.5 * LIP_GAP
    mi = len(me.materials) - 1   # mouth_interior
    for group, sgn in ((upper, 1.0), (lower, -1.0)):
        ext = bmesh.ops.extrude_edge_only(bm, edges=group)
        new_verts = [g for g in ext["geom"] if isinstance(g, bmesh.types.BMVert)]
        cy = interp(zm)[3]
        for v in new_verts:
            d = Vector((v.co.x, v.co.y - cy, 0.0))
            L = d.length or 1.0
            v.co -= d / L * LIP_ROLL
            v.co.z += sgn * 0.002
        for g in ext["geom"]:
            if isinstance(g, bmesh.types.BMFace):
                g.material_index = mi
                g.smooth = True
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    return zm, corner


def lid_mesh(name, elev, gap=None):
    """An eyelid: a thin spherical shell round the eyeball's centre (LID_GAP outside it), between two elevations and
    within +-90 degrees of the gaze (its ends on the hinge axis, so shut lids meet at the corners). Turned about the eye's horizontal axis it closes over the eyeball -- the upper lid
    down, the lower up -- and it can never touch the ball, however the eye turns."""
    o = bpy.data.objects.new(name, bpy.data.meshes.new(name))
    bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(make_mat("porcelain", (0.86, 0.82, 0.74), 0.36, 0.5, 0.12))
    bm = bmesh.new()
    R_ = EYE_RADIUS + (LID_GAP if gap is None else gap)
    rows, cols = 14, 28
    grid = []
    for i in range(rows + 1):
        e = math.radians(elev[0] + (elev[1] - elev[0]) * i / rows)
        row = []
        for j in range(cols + 1):
            az = math.radians(-90.0 + 180.0 * j / cols)   # to the hinge axis itself: the corners meet
            # forward is -Y in Blender
            row.append(bm.verts.new((R_ * math.cos(e) * math.sin(az), -R_ * math.cos(e) * math.cos(az),
                                     R_ * math.sin(e))))
        grid.append(row)
    for i in range(rows):
        for j in range(cols):
            f = bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]))
            f.smooth = True
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    for f in bm.faces:   # outward
        if f.normal.dot(f.calc_center_median()) < 0:
            f.normal_flip()
    bm.to_mesh(o.data)
    bm.free()
    m = o.modifiers.new("solid", "SOLIDIFY")
    m.thickness = 0.0010
    m.offset = 1.0
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier=m.name)
    return o


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ob = build_mesh()
    assign_regions(ob)
    for poly in ob.data.polygons:   # the old recess region is gone: the lips close
        poly.material_index = 0
    lip = surface_point(ob, 0.0, MOUTH_Z + 0.012)
    mouth_front = surface_point(ob, 0.0, MOUTH_Z)
    eyes = [surface_point(ob, s * EYE_DEG, EYE_Z) for s in (1, -1)]   # +X first = the bust's LEFT eye
    centre = Vector((0.0, interp(0.465 - NECK_DROP)[3], 0.465 - NECK_DROP))
    neck_foot = Vector((0.0, interp(NECK_LOW)[3], NECK_LOW))
    head_origin = Vector((0.0, interp(NECK_LOW)[3], NECK_TOP))      # straight above the neck's foot: the bend's axis
    whole = ob.copy()
    whole.data = ob.data.copy()
    zm, corner = cut_mouth(ob)
    pivot = Vector((0.0, interp(MOUTH_Z)[3], MOUTH_Z + 0.010))       # the jaw's pivot, near the hinge of a real jaw
    body = piece(ob, "th-body", lambda c, f: c.z < NECK_LOW)
    neck = piece(ob, "th-neck", lambda c, f: NECK_LOW < c.z < NECK_TOP)
    head = piece(ob, "th-head", lambda c, f: c.z > NECK_TOP)
    for p_ in (body, neck, head):
        copy_smooth_normals(whole, p_)
    head_g = piece(head, "th-head-g", lambda c, f: True)
    copy_smooth_normals(whole, head_g)
    add_cap_dome(neck, NECK_TOP, up=True, depth_scale=0.3)
    add_cap_dome(head, NECK_TOP, up=False, depth_scale=0.22)

    def dark_piece(name, centre_, radii):
        o = bpy.data.objects.new(name, bpy.data.meshes.new(name))
        bpy.context.scene.collection.objects.link(o)
        o.data.materials.append(make_mat("mouth_interior", (0.012, 0.010, 0.012), 0.85, 0.1))
        add_dark_ellipsoid(o, centre_, radii, 0)
        return o
    eyeball = bpy.data.objects.new("th-eye", bpy.data.meshes.new("th-eye"))
    bpy.context.scene.collection.objects.link(eyeball)
    eyeball.data.materials.append(make_mat("eye_white", (0.86, 0.84, 0.80), 0.25, 0.6, 0.3))
    eyeball.data.materials.append(make_mat("iris", (0.03, 0.025, 0.04), 0.3, 0.5, 0.3))
    ebm = bmesh.new()
    res = bmesh.ops.create_uvsphere(ebm, u_segments=64, v_segments=40, radius=EYE_RADIUS)
    # the sphere's pole turned onto the gaze (-Y), so the iris's edge is one of its rings: a true circle, not the
    # stair-step a cap cut across the rings draws
    bmesh.ops.rotate(ebm, verts=res["verts"], cent=(0.0, 0.0, 0.0), matrix=Matrix.Rotation(math.radians(90.0), 3, "X"))
    for fce in {f for v in res["verts"] for f in v.link_faces}:
        fce.smooth = True
        c = fce.calc_center_median()
        polar = math.degrees(math.acos(max(-1.0, min(1.0, -c.y / c.length))))
        fce.material_index = 1 if polar < IRIS_DEG else 0
    ebm.to_mesh(eyeball.data)
    ebm.free()
    # the upper lid rides 0.7 mm outside the lower, so where they overlap when shut it lies in front (no z-fight seam)
    lid_up = lid_mesh("th-lid-upper", LID_UPPER, LID_GAP + 0.0007)
    lid_lo = lid_mesh("th-lid-lower", LID_LOWER)
    cy_m = interp(MOUTH_Z)[3]
    # the cavity: the dark behind the lips, its front just inside their walls
    cavity = dark_piece("th-mouth-cavity", (0.0, mouth_front.y + LIP_ROLL + 0.026, MOUTH_Z - 0.006),
                        (0.030, 0.030, 0.020))
    shift(neck, neck_foot)
    shift(head, head_origin)
    shift(cavity, head_origin)
    shift(head_g, head_origin)
    G = lambda v: to_gltf(v)
    F = NECK_TOP - NECK_LOW
    # the hinge mask's centre (the chin) and radius: it must end above the head piece's lower edge, or the head turns
    # where the still neck meets it (the owner's "seams under the chins", fx1)
    mask_c = Vector((0.0, mouth_front.y + 0.008, MOUTH_Z - HINGE_DOWN))
    anchors = {
        "note": "tools/talking_heads/make_bust.py. Frames are the exported GLBs' (+Y up, face +Z, metres). Hierarchy: "
                "body (origin at the base's bottom centre) > neck at body.neckFoot (bent by a Bend deformer: local, "
                "axis +Y, falloff neck.bendLength, centre 0) ; head (a child of the BODY, at body.headRest) carries "
                "the mouth's Hinge deformer (head.mouthHinge: ADR-1672), the cavity, the eyes and lids. head.mouth is "
                "the rays' origin, behind the lips. Lids: th-lid-upper/lower.glb at each eye's centre; a positive "
                "turn about X closes the upper lid (to lidCloseUpperDeg), a negative turn the lower.",
        "body": {"neckFoot": G(neck_foot), "headRest": G(head_origin)},
        "neck": {"bendLength": round(F, 4)},
        "eyes": {"radius": EYE_RADIUS, "left": G(eye_centre(eyes[0], 1) - head_origin),
                 "right": G(eye_centre(eyes[1], -1) - head_origin),
                 "lidCloseUpperDeg": LID_UPPER[0] + 3.0, "lidCloseLowerDeg": -LID_LOWER[1] + 1.0,
                 "note": "th-eye-white/iris.glb's centre goes here (head frame); its iris faces +Z. Keep yaw within +-28 and "
                         "pitch within -4..+16 degrees (down positive) so the iris never sinks into the socket."},
        "head": {"eyeLeft": G(eyes[0] - head_origin), "eyeRight": G(eyes[1] - head_origin),
                 "headCentre": G(centre - head_origin),
                 "mouth": G(Vector((0.0, mouth_front.y + 0.010, MOUTH_Z)) - head_origin),
                 "mouthLip": G(Vector((0.0, lip.y, MOUTH_Z)) - head_origin),
                 "mouthHinge": {"center": G(pivot - head_origin), "axis": [1.0, 0.0, 0.0],
                                "displacementAxis": [0.0, 0.0, 1.0], "phase": round(zm - pivot.z, 5),
                                "falloff": HINGE_RADIUS, "frequency": round(corner, 5),
                                "speed": round(-(mask_c.y - pivot.y), 5), "scale": round(mask_c.z - pivot.z, 5)},
                 "forward": [0.0, 0.0, 1.0], "up": [0.0, 1.0, 0.0]},
        "test": {"tiltDeg": TILT_TEST_DEG},
        "mesh": {n.name: stats(n) for n in (body, neck, head)} | {"whole": stats(whole)},
    }
    with open(os.path.join(OUT, "th-bust.anchors.json"), "w") as f:
        json.dump(anchors, f, indent=1)
        f.write("\n")
    for p_, name in ((body, "th-body"), (neck, "th-neck"), (head, "th-head"), (cavity, "th-mouth-cavity"),
                     (lid_up, "th-lid-upper"), (lid_lo, "th-lid-lower")):
        export(p_, os.path.join(OUT, name + ".glb"))
    # the eyeball as two single-material meshes, the white and the iris: the scene draws them as procedural pieces
    # carrying the head's own surface deformers, so a wave across the face carries the eyes with it (fx1: the owner,
    # "the eyes look like they are not apart of the head"), and a procedural draws one material
    for keep, name in ((0, "th-eye-white"), (1, "th-eye-iris")):
        part = eyeball.copy()
        part.data = eyeball.data.copy()
        bpy.context.scene.collection.objects.link(part)
        pbm = bmesh.new()
        pbm.from_mesh(part.data)
        bmesh.ops.delete(pbm, geom=[f for f in pbm.faces if f.material_index != keep], context="FACES")
        pbm.to_mesh(part.data)
        pbm.free()
        export(part, os.path.join(OUT, name + ".glb"))
        bpy.data.objects.remove(part)
    gm = ghost_material()
    head_g.data.materials.clear()
    head_g.data.materials.append(gm)
    # the ghost is only a fresnel edge: a fifth of the head's triangles draws it the same, and six of them at full
    # resolution cost 2.2 ms a frame at 1080p (stop-4 profile)
    dec = head_g.modifiers.new("decimate", "DECIMATE")
    dec.ratio = 0.2
    bpy.context.view_layer.objects.active = head_g
    bpy.ops.object.modifier_apply(modifier=dec.name)
    export(head_g, os.path.join(OUT, "th-head-ghost.glb"))
    print("BUST_STATS", json.dumps(anchors["mesh"]))
    if not PREV:
        return
    for o in (ob, whole, head_g):
        bpy.data.objects.remove(o)
    head.location = head_origin
    cavity.location = head_origin
    for i, side in enumerate((1, -1)):
        for proto in (eyeball, lid_up, lid_lo):
            e = proto if i == 0 else bpy.data.objects.new(proto.name + "-R", proto.data)
            if i:
                bpy.context.scene.collection.objects.link(e)
            e.location = eye_centre(eyes[i], side)
    cam = setup_render()
    shoot(cam, 0, "bust-front.png")
    shoot(cam, 35, "bust-three-quarter.png")
    shoot(cam, -90, "bust-profile.png")
    shoot(cam, 20, "bust-face-close.png", dist=0.75, height=0.42, aim_z=0.38)
    bpy.context.scene.camera.data.lens = 85


def export(ob, path):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_normals=True, export_texcoords=False,
                              export_materials="EXPORT")

main()
