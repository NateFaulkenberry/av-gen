"""Talking Heads -- procedural mannequin bust PROBE (prep only, not the production asset).

Builds a simplified, slightly elongated mannequin head, neck and small bust base as ONE generalised
cylinder: horizontal cross-sections (superellipses with separate front and back depths) swept up a
smooth profile, closed by a pole at the crown and a flat base. Sculptural features are smooth Gaussian
offsets in (angle, height): a restrained nose, high cheekbones, a brow, a simplified chin, shallow eye
sockets and a recessed mouth slot. Two material regions: porcelain shell and mouth interior (near
black); the sockets darken by their depth (and at runtime by an object-space mask). Clean quad topology, no decimation needed; the triangle count is
set by the ring and segment counts.

Renders front / three-quarter / profile / silhouette previews on a dark ground (Cycles CPU) and exports
a GLB plus an anchors file. Deterministic: no randomness.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P make_bust.py -- \
        --out ~/Desktop/av-gen-review/talking-heads/prep [--segments 96] [--samples 48] [--no-render]

Units: metres. Blender Z-up, the face looks down -Y (Blender's front view). The glTF exporter's +Y-up
conversion maps that to +Y up and the face toward +Z in the GLB. Origin: the centre of the base's bottom
face. Overall height ~0.60 m (about a real bust); the scene scales it.

<out>/bust_anchors.json records the emitter anchors (mouth, eyes, head centre) in both frames so an
emitter can be placed in the node's LOCAL space, and the mesh statistics.
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
ap.add_argument("--out", default=os.path.expanduser("~/Desktop/av-gen-review/talking-heads/prep"))
ap.add_argument("--segments", type=int, default=96, help="segments round each ring")
ap.add_argument("--samples", type=int, default=48)
ap.add_argument("--no-render", action="store_true")
args = ap.parse_args(argv)
OUT = os.path.expanduser(args.out)
os.makedirs(OUT, exist_ok=True)

# ------------------------------------------------------------------------------------------- the profile
# Control points up the bust: z, half-width W, front depth DF, back depth DB, centre offset CY (+Y = back),
# superellipse exponent N (2 = ellipse, larger = boxier, flatter face). Interpolated with a monotone cubic.
#            z      W      DF     DB     CY      N
PROFILE = [(0.000, 0.158, 0.080, 0.080, 0.010, 2.8),   # flat base, bottom
           (0.025, 0.166, 0.086, 0.086, 0.010, 2.8),
           (0.065, 0.164, 0.082, 0.082, 0.010, 2.8),   # shoulders (a small bust termination)
           (0.110, 0.128, 0.066, 0.068, 0.010, 2.5),
           (0.150, 0.066, 0.050, 0.054, 0.012, 2.0),
           (0.190, 0.044, 0.040, 0.044, 0.014, 2.0),   # the long neck
           (0.240, 0.040, 0.038, 0.042, 0.016, 2.0),
           (0.290, 0.041, 0.040, 0.044, 0.016, 2.0),
           (0.318, 0.045, 0.046, 0.050, 0.012, 2.0),   # under the jaw
           (0.336, 0.050, 0.070, 0.058, 0.004, 2.1),   # chin line (the front jumps forward)
           (0.360, 0.056, 0.084, 0.066, 0.000, 2.2),   # jaw, simplified and narrow
           (0.395, 0.064, 0.090, 0.076, 0.000, 2.3),   # mouth level
           (0.430, 0.070, 0.092, 0.084, 0.002, 2.3),   # cheekbones
           (0.465, 0.073, 0.092, 0.090, 0.004, 2.2),   # eye line
           (0.500, 0.074, 0.090, 0.094, 0.006, 2.1),   # brow / temple
           (0.535, 0.072, 0.084, 0.094, 0.008, 2.0),
           (0.565, 0.062, 0.070, 0.082, 0.010, 2.0),
           (0.585, 0.044, 0.050, 0.060, 0.012, 2.0),
           (0.597, 0.020, 0.024, 0.030, 0.012, 2.0),
           (0.600, 0.000, 0.000, 0.000, 0.012, 2.0)]   # crown (pole)

# Features: (name, angle deg from the front, height z, sigma angle deg, sigma z, amplitude m, mirrored).
# The front is angle 0 (-Y); positive angles turn toward +X. Amplitude > 0 pushes out, < 0 presses in.
FEATURES = [("nose",       0.0, 0.432, 7.5, 0.024, +0.019, False),
            ("noseTip",    0.0, 0.414, 6.0, 0.008, +0.004, False),
            ("brow",       0.0, 0.487, 34.0, 0.010, +0.004, False),
            ("cheekbone", 40.0, 0.440, 11.0, 0.016, +0.008, True),
            ("socket",    23.0, 0.462, 8.5, 0.0115, -0.0095, True),
            ("chin",       0.0, 0.346, 13.0, 0.010, +0.006, False),
            ("mouth",      0.0, 0.384, 0.0, 0.0042, -0.0110, False)]   # the slot: its own angular shape
MOUTH_HALF_DEG = 15.0      # the slot's half width, in degrees round the face
EYE_Z = 0.462
EYE_DEG = 23.0
MOUTH_Z = 0.384


def rings_z():
    """Ring heights: coarse on the base and neck, fine through the face (the mouth slot is 7 mm tall)."""
    zs, z = [], 0.0
    while z < 0.300:
        zs.append(z); z += 0.008
    while z < 0.520:
        zs.append(z); z += 0.0022
    while z < 0.597:
        zs.append(z); z += 0.0042
    return zs  # the pole at 0.600 is added separately


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
                g = math.exp(-abs(da / MOUTH_HALF_DEG) ** 6) * math.exp(-((z - z0) / sz) ** 2)
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
        if abs(a) < MOUTH_HALF_DEG * 0.92 and abs(c.z - MOUTH_Z) < 0.0046:
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
    bpy.context.scene.render.filepath = os.path.join(OUT, name)
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


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ob = build_mesh()
    assign_regions(ob)
    st = stats(ob)
    mouth = surface_point(ob, 0.0, MOUTH_Z)
    eyes = [surface_point(ob, s * EYE_DEG, EYE_Z) for s in (1, -1)]   # +X first = the bust's LEFT eye
    head = Vector((0.0, interp(0.465)[3], 0.465))
    anchors = {
        "note": "Emitter anchors for the node's LOCAL space. 'gltf' is the exported GLB's frame (+Y up, face +Z). "
                "eyeLeft is the bust's own left (+X).",
        "blender": {"mouth": [round(c, 4) for c in mouth], "eyeLeft": [round(c, 4) for c in eyes[0]],
                    "eyeRight": [round(c, 4) for c in eyes[1]], "headCentre": [round(c, 4) for c in head]},
        "gltf": {"mouth": to_gltf(mouth), "eyeLeft": to_gltf(eyes[0]), "eyeRight": to_gltf(eyes[1]),
                 "headCentre": to_gltf(head), "forward": [0.0, 0.0, 1.0], "up": [0.0, 1.0, 0.0]},
        "mesh": st,
    }
    with open(os.path.join(OUT, "bust_anchors.json"), "w") as f:
        json.dump(anchors, f, indent=1)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "talking_head_bust_probe.glb"), export_format="GLB",
                              use_selection=True, export_yup=True, export_apply=True, export_normals=True,
                              export_texcoords=False, export_materials="EXPORT")
    print("BUST_STATS", json.dumps(st))
    if args.no_render:
        return
    cam = setup_render()
    shoot(cam, 0, "bust-front.png")
    shoot(cam, 35, "bust-three-quarter.png")
    shoot(cam, -90, "bust-profile.png")
    shoot(cam, 20, "bust-face-close.png", dist=0.75, height=0.47, aim_z=0.43)
    silhouette(cam, ob, "bust-silhouette")


main()
