"""CHRONOS, The Devourer (Gates D1, D1b): the creature from a sculptor's scan -- the scan route.

The base is Miquel Blay's study of an old man (photogrammetry of the plaster, CC BY 4.0, albert_sierra on Sketchfab;
docs/prototypes/chronos/assets.md): a sculptor's exaggeration of age, with the hair and beard as one matted, sculpted
mass, which is how Goya paints Saturn's. The surface detail is Rodin's Heroic Head of Pierre de Wissant (CC0, Cleveland
Museum of Art). This script turns the bust into a rigged creature:

  1. normalise (1 unit = the bust's height, the face toward -Y), cut away the pedestal;
  2. D1b: past human proportion -- a narrower skull, the face lengthened below the eyes, larger and wider-set sockets;
     all landmarks go through the same warp, so the rig and the cuts follow;
  3. push it further: deeper sockets, hollowed cheeks, a hanging lower face;
  4. cut the mouth open along the lip line (shorter than the mouth, so the corners stay joined);
  5. a rig of its own -- root, neck, head, jaw, eye.L, eye.R -- the jaw blending widely into the cheeks at the corners
     (D1b: the corners tore at 66 degrees with a sharp blend);
  6. D1b eyes: sunk deep in the sockets under heavy upper and lower lid hoods that ride the head, not the eye; a
     yellowed, bloodshot sclera, a streaked amber iris with a dark limbal ring, a wet low roughness;
  7. D1b mouth: a real cavity (fleshy crimson with palate ridges near the lips going to black at the back), a tongue on
     the jaw, gum ridges along both arches, and teeth irregular in size, spacing, angle and colour, with gaps, broken
     and worn ones;
  8. D1b skin: Rodin's clay surface, transferred as a normal map (its high-frequency relief extracted in a cylindrical
     projection about the head's axis, which needs no alignment, then baked through the creature's UVs); a brush-broken
     albedo (ochre, umber, sickly grey-green in the cavities, burgundy patches, stroke-shaped value breaks); a baked
     roughness map (oily ridges, dry hollows);
  9. the looping 12 s life clip (`idle`): the jaw hanging at 12 degrees at rest (D1b), breathing, one slow 66-degree
     hungry opening, eye saccades under the lids, a slight head drift;
 10. glTF binary export, and Blender stills under the chiaroscuro light.

    /Applications/Blender.app/Contents/MacOS/Blender --background --python examples/chronos/devourer/build_scan_creature.py \
        -- <blay.glb> <rodin.glb> <out.glb> [<stills_dir>]

Landmarks were read from orthographic grid renders made by landmarks.py. The outputs are CC BY-derived and stay out of
the repository (assets/chronos/ is gitignored); this script is original code.
"""
import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
SCAN, RODIN, OUT = argv[0], argv[1], argv[2]
STILLS = argv[3] if len(argv) > 3 else ""
os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
# absolute: the .blend is saved beside the GLB, and a relative image path then resolves against the .blend's folder
# and goes missing (Blender's magenta), which is what the first D1 build rendered
TEX = os.path.abspath(os.path.join(os.path.dirname(OUT), "chronos-scan-textures"))
os.makedirs(TEX, exist_ok=True)
rng = random.Random(1934)

# ---- landmarks, in the normalised frame of the raw scan (landmarks.py) --------------------------------------------
EYE_R = Vector((-0.093, -0.200, 0.698))   # the subject's right (image left in the front view)
EYE_L = Vector((0.046, -0.205, 0.700))
MOUTH = Vector((-0.024, -0.245, 0.536))   # centre of the lip line, under the moustache
MOUTH_HALF = 0.052                        # half the mouth's width
HINGE = Vector((-0.024, -0.060, 0.600))   # the jaw's hinge, in front of the ears
CHIN = Vector((-0.024, -0.235, 0.405))    # the beard's point
NECK_CUT = 0.335                          # below this is pedestal
EYE_RADIUS = 0.0215                       # D1b: larger eyes


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


# ---- the proportion warp (D1b) -----------------------------------------------------------------------------------

EYE_LINE = 0.70
AXIS_X = MOUTH.x


def proportion(p):
    """Past human: the skull narrows above the eyes, the face lengthens below them. Applied to every vertex and every
    landmark alike."""
    x, y, z = p.x, p.y, p.z
    skull = smooth(EYE_LINE - 0.06, EYE_LINE + 0.08, z)
    sx = 0.95 - 0.07 * skull                       # 0.95 at the jaw, 0.88 across the skull
    x = AXIS_X + (x - AXIS_X) * sx
    if z < EYE_LINE:
        z = EYE_LINE - (EYE_LINE - z) * 1.15       # 15% longer from the eyes down
    z += 0.02 * skull * smooth(EYE_LINE, 1.0, p.z)  # and the crown a little higher
    return Vector((x, y, z))


def socket_warp(p, eyes):
    """Larger, wider-set sockets: each grows about its centre and moves outward."""
    q = p.copy()
    for e in eyes:
        d = (p - e).length
        f = smooth(0.07, 0.0, d)   # a wide falloff: a steep one folded the brow into shards above the inner corners
        side = 1.0 if e.x > AXIS_X else -1.0
        q += (p - e) * 0.16 * f + Vector((side * 0.009 * f, 0, 0))
    return q


def warp_landmarks():
    global EYE_R, EYE_L, MOUTH, HINGE, CHIN, NECK_CUT
    EYE_R, EYE_L = proportion(EYE_R), proportion(EYE_L)
    MOUTH, HINGE, CHIN = proportion(MOUTH), proportion(HINGE), proportion(CHIN)
    EYE_R = EYE_R + Vector((-0.009, 0, 0))
    EYE_L = EYE_L + Vector((0.009, 0, 0))
    NECK_CUT = proportion(Vector((0, 0, NECK_CUT))).z


def load_normalised(path, name):
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    for o in list(bpy.context.scene.objects):
        if o.type == "EMPTY":
            bpy.data.objects.remove(o)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    co = np.empty(len(obj.data.vertices) * 3)
    obj.data.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    lo, hi = co.min(0), co.max(0)
    s = 1.0 / (hi[2] - lo[2])
    co = np.stack([(co[:, 0] - (lo[0] + hi[0]) / 2) * s, (co[:, 1] - (lo[1] + hi[1]) / 2) * s, (co[:, 2] - lo[2]) * s], 1)
    obj.data.vertices.foreach_set("co", co.ravel())
    obj.data.update()
    obj.name = name
    return obj


# ---- Rodin's surface, as a cylindrical relief map ------------------------------------------------------------------

def rodin_relief(path):
    """The Wissant head's high-frequency relief (thumb-marks, clay strips), as a height image in a cylindrical projection
    about its vertical axis: u = angle, v = height. Rays from the axis outward find its radius; the relief is the radius
    minus a broad blur of itself. No alignment with the creature is needed -- this is a surface, not a shape."""
    rodin = load_normalised(path, "rodin")
    dg = bpy.context.evaluated_depsgraph_get()
    bvh = BVHTree.FromObject(rodin, dg)
    W, H = 1024, 1024
    radius = np.zeros((H, W), np.float32)
    for j in range(H):
        z = 0.08 + 0.84 * (j + 0.5) / H
        for i in range(W):
            a = 2 * math.pi * (i + 0.5) / W
            d = Vector((math.cos(a), math.sin(a), 0.0))
            loc, n, idx, dist = bvh.ray_cast(Vector((0, 0, z)) + d * 0.9, -d)
            radius[j, i] = 0.9 - dist if loc is not None else 0.0
    # fill misses with the row's mean, then high-pass
    for j in range(H):
        row = radius[j]
        good = row > 0
        if good.any():
            row[~good] = row[good].mean()
    def blur(img, r):
        k = np.exp(-0.5 * (np.arange(-3 * r, 3 * r + 1) / r) ** 2)
        k /= k.sum()
        out = np.apply_along_axis(lambda m: np.convolve(np.concatenate([m[-3 * r:], m, m[:3 * r]]), k, "same")[3 * r:-3 * r], 1, img)
        out = np.apply_along_axis(lambda m: np.convolve(np.pad(m, 3 * r, mode="edge"), k, "same")[3 * r:-3 * r], 0, out)
        return out
    relief = radius - blur(radius, 12)
    relief = np.clip(relief / (np.percentile(np.abs(relief), 99) + 1e-6), -1, 1) * 0.5 + 0.5
    img = bpy.data.images.new("rodin-relief", W, H, float_buffer=True)
    rgba = np.stack([relief, relief, relief, np.ones_like(relief)], -1).astype(np.float32)
    img.pixels.foreach_set(rgba.ravel())
    img.colorspace_settings.name = "Non-Color"
    bpy.data.objects.remove(rodin)
    print("rodin relief extracted", W, H)
    return img


# ---- the creature ---------------------------------------------------------------------------------------------------

def shape(obj):
    """D1b proportion, sockets, then the D1 push (sockets deeper, cheeks hollowed toward the axis, lower face hanging)."""
    me = obj.data
    raw_eyes = (Vector((-0.093, -0.200, 0.698)), Vector((0.046, -0.205, 0.700)))
    warped_eyes = [proportion(e) for e in raw_eyes]
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    for k in range(len(co)):
        p = proportion(Vector(co[k]))
        p = socket_warp(p, warped_eyes)
        co[k] = p
    me.vertices.foreach_set("co", co.ravel())
    me.update()
    eyes = (EYE_L + EYE_R) * 0.5
    for v in me.vertices:
        p = v.co.copy()
        d = Vector()
        for e in (EYE_L, EYE_R):
            pass  # D1b: no push here -- pushing the socket back drove the brow's undercut through its front surface
                  # (dark shards); the eyes are sunk by seating the ball deeper instead (seat_eye)
        for side in (-1, 1):
            c = Vector((eyes.x + side * 0.072, -0.18, (EYE_L.z + MOUTH.z) * 0.5 + 0.01))
            r = (p - c).length / 0.055
            inward = Vector((MOUTH.x - p.x, -p.y, 0.0))
            if inward.length > 1e-6 and p.y < -0.1:
                d += inward.normalized() * 0.010 * smooth(1.0, 0.0, r)
        below = smooth(MOUTH.z, MOUTH.z - 0.15, p.z) * smooth(0.2, 0.05, abs(p.x - MOUTH.x)) * smooth(0.05, -0.12, p.y)
        d += Vector((0, -0.25, -1)).normalized() * 0.03 * below
        v.co = p + d
    me.update()


def cut(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < NECK_CUT], context="VERTS")
    kill = []
    for f in bm.faces:
        c = f.calc_center_median()
        dx = (c.x - MOUTH.x) / (MOUTH_HALF * 0.85)  # D1b: the slit stops short of the corners
        if abs(dx) > 1.0 or c.y > MOUTH.y + 0.09:
            continue
        half = 0.0065 * (1.0 - dx * dx) + 0.001
        if abs(c.z - (MOUTH.z + 0.004 * dx * dx)) < half:
            kill.append(f)
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    # the photogrammetry carries a few inward-facing faces (dark shards at the brows once the skin had relief): make
    # every face's winding consistent and outward
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def build_rig(obj):
    arm = bpy.data.armatures.new("chronos-rig")
    rig = bpy.data.objects.new("chronos-rig", arm)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.edit_bones

    def bone(name, head, tail, parent=None):
        b = eb.new(name)
        b.head, b.tail = head, tail
        b.roll = 0.0
        if parent:
            b.parent = eb[parent]
    bone("root", Vector((MOUTH.x, 0.0, NECK_CUT)), Vector((MOUTH.x, 0.0, NECK_CUT + 0.05)))
    bone("neck", Vector((MOUTH.x, 0.0, NECK_CUT + 0.06)), Vector((MOUTH.x, -0.01, MOUTH.z - 0.02)), "root")
    bone("head", Vector((MOUTH.x, -0.01, MOUTH.z - 0.02)), Vector((MOUTH.x, -0.02, 0.99)), "neck")
    bone("jaw", HINGE, CHIN, "head")
    bone("eye.L", EYE_L, EYE_L + Vector((0, -0.04, 0)), "head")
    bone("eye.R", EYE_R, EYE_R + Vector((0, -0.04, 0)), "head")
    bpy.ops.object.mode_set(mode="OBJECT")
    for name in ("root", "neck", "head", "jaw"):
        obj.vertex_groups.new(name=name)
    gj, gh, gn = obj.vertex_groups["jaw"], obj.vertex_groups["head"], obj.vertex_groups["neck"]
    for v in obj.data.vertices:
        p = v.co
        ax = abs(p.x - MOUTH.x)
        # D1b: across the slit the blend is sharp (it opens); toward and past the corners it widens and rises, so the
        # cheeks at the corners stretch with the jaw instead of tearing away from it
        t = smooth(MOUTH_HALF * 0.6, MOUTH_HALF * 1.3, ax)
        top = MOUTH.z + 0.004 + 0.035 * t
        bottom = MOUTH.z - 0.03 - 0.01 * t
        vertical = smooth(top, bottom, p.z)
        lateral = smooth(MOUTH_HALF + 0.12, MOUTH_HALF + 0.03, ax)
        front = smooth(HINGE.y + 0.03, HINGE.y - 0.06, p.y)
        wj = lateral * vertical * front
        wn = smooth(NECK_CUT + 0.13, NECK_CUT + 0.04, p.z) * smooth(-0.12, -0.02, p.y) * (1 - wj)
        wh = max(0.0, 1.0 - wj - wn)
        if wj > 1e-4:
            gj.add([v.index], wj, "REPLACE")
        if wn > 1e-4:
            gn.add([v.index], wn, "REPLACE")
        if wh > 1e-4:
            gh.add([v.index], wh, "REPLACE")
    obj.parent = rig
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    return rig


def attach(o, rig, group):
    o.vertex_groups.clear()
    g = o.vertex_groups.new(name=group)
    g.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
    o.parent = rig
    m = o.modifiers.new("Armature", "ARMATURE")
    m.object = rig


def material(name, color, rough=0.6, image=None, rough_image=None, normal_image=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = color
    b.inputs["Roughness"].default_value = rough
    if image is not None:
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = image
        nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    if rough_image is not None:
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = rough_image
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(t.outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], b.inputs["Roughness"])
    if normal_image is not None:
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = normal_image
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nt.links.new(t.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
    return m


def save(img, name, srgb=True):
    img.filepath_raw = os.path.join(TEX, name)
    img.file_format = "PNG"
    img.save()


def gaze_uv():
    """Where a Blender UV sphere's UVs put the point that faces -Y: measured, not assumed."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, segments=32, ring_count=16)
    o = bpy.context.active_object
    me = o.data
    uv = me.uv_layers.active.data
    best, best_uv = -2.0, (0.25, 0.5)
    for poly in me.polygons:
        for li in poly.loop_indices:
            v = me.vertices[me.loops[li].vertex_index].co
            if -v.y > best:
                best, best_uv = -v.y, tuple(uv[li].uv)
    bpy.data.objects.remove(o)
    return best_uv


def eye_image(g):
    """D1b: a yellowed, bloodshot sclera; an amber-brown iris with radial streaks and a dark limbal ring; a pupil."""
    w, h = 1024, 512
    U, V = np.meshgrid((np.arange(w) + 0.5) / w, (np.arange(h) + 0.5) / h)
    du = ((U - g[0] + 0.5) % 1.0 - 0.5) * 2 * math.pi
    dv = (V - g[1]) * math.pi
    ang = np.sqrt(du * du + dv * dv)
    theta = np.arctan2(dv, du)
    sclera = np.stack([0.76 + 0 * ang, 0.66 + 0 * ang, 0.44 + 0 * ang], -1)       # old ivory, yellowed
    blood = np.array([0.42, 0.06, 0.04])
    rim = np.clip((ang - 0.75) / 0.8, 0, 1)[..., None]
    col = sclera * (1 - 0.8 * rim) + blood * 0.8 * rim
    veins = np.zeros_like(ang)
    vr = np.random.RandomState(7)
    for _ in range(40):
        t0 = vr.uniform(-math.pi, math.pi)
        wig = vr.uniform(4, 11)
        width = vr.uniform(0.012, 0.03)
        line = np.abs(((theta - t0 + 0.15 * np.sin(ang * wig)) + math.pi) % (2 * math.pi) - math.pi)
        veins = np.maximum(veins, np.clip(1 - line / width, 0, 1) * np.clip((ang - 0.45) / 0.5, 0, 1))
    col = col * (1 - 0.75 * veins[..., None]) + blood * 0.75 * veins[..., None]
    iris_r, pupil_r = 0.27, 0.085   # more of the yellowed white shows: the mad, wide stare
    streak = 0.6 + 0.4 * np.sin(theta * 37 + np.sin(theta * 11) * 2) * np.clip(1 - ang / iris_r, 0, 1)
    iris_col = np.stack([0.50 * streak, 0.31 * streak, 0.09 * streak], -1)   # amber, not near-black
    iris = np.clip((iris_r - ang) / 0.02, 0, 1)[..., None]
    col = col * (1 - iris) + iris_col * iris
    limbal = np.clip(1 - np.abs(ang - iris_r * 0.97) / 0.03, 0, 1)[..., None]
    col = col * (1 - 0.85 * limbal) + np.array([0.03, 0.02, 0.01]) * 0.85 * limbal
    # an off-centre pupil, slightly irregular: madness, not a cartoon dot
    pd = np.sqrt((du - 0.02) ** 2 + (dv + 0.01) ** 2) * (1 + 0.08 * np.sin(theta * 5))
    pupil = np.clip((pupil_r - pd) / 0.015, 0, 1)[..., None]
    col = col * (1 - pupil) + np.array([0.008, 0.006, 0.005]) * pupil
    img = bpy.data.images.new("chronos-eye", w, h)
    img.pixels.foreach_set(np.concatenate([col, np.ones((h, w, 1))], -1).astype(np.float32).ravel())
    save(img, "chronos-eye.png")
    return img


def seat_eye(obj, guess):
    """The socket's deepest point near the landmark: rays toward +Y over a small grid, keeping the furthest hit."""
    best = None
    for i in range(-7, 8):
        for k in range(-7, 8):
            o = Vector((guess.x + i * 0.003, -1.0, guess.z + k * 0.003))
            hit, loc, nrm, idx = obj.ray_cast(o, Vector((0, 1, 0)))
            if hit and (best is None or loc.y > best.y) and (loc - guess).length < 0.045:
                best = loc.copy()
    if best is None:
        return guess
    # D1b: sunk -- the ball's front sits only just proud of the socket floor, under the lids
    return Vector((best.x, best.y + 0.3 * EYE_RADIUS, best.z))  # sunk, but proud enough to catch the key


def eyes_and_lids(rig, skin_color):
    img = eye_image(gaze_uv())
    eye_mat = material("chronos-eye", (1, 1, 1, 1), 0.08, img)  # wet
    lid_mat = material("chronos-lids", skin_color, 0.62)
    for name, c in (("eye.L", EYE_L), ("eye.R", EYE_R)):
        bpy.ops.mesh.primitive_uv_sphere_add(radius=EYE_RADIUS, segments=48, ring_count=24, location=c)
        o = bpy.context.active_object
        bpy.ops.object.shade_smooth()
        o.name = "chronos-" + name
        o.data.materials.append(eye_mat)
        attach(o, rig, name)
        # the lids: heavy hoods over the top (a third of the ball) and a sagging lower lid, riding the head
        bpy.ops.mesh.primitive_uv_sphere_add(radius=EYE_RADIUS * 1.07, segments=48, ring_count=24, location=c)
        lid = bpy.context.active_object
        bpy.ops.object.shade_smooth()
        lid.name = "chronos-lids-" + name
        bm = bmesh.new()
        bm.from_mesh(lid.data)
        keep = set()
        for v in bm.verts:
            rel = v.co  # object-local, centred on the eye
            front = rel.y < -EYE_RADIUS * 0.2   # only the front of the ball: no disc edge from the side
            upper = rel.z > EYE_RADIUS * (0.32 + 0.12 * (rel.x / EYE_RADIUS) ** 2)  # a heavy hood, a slight droop at the corners
            lower = rel.z < -EYE_RADIUS * 0.62
            if front and (upper or lower):
                keep.add(v.index)
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in keep], context="VERTS")
        bm.to_mesh(lid.data)
        bm.free()
        lid.data.materials.append(lid_mat)
        attach(lid, rig, "head")


def mouth_cavity_image(g):
    """Seen from inside: black at the back (the gaze's antipode, +Y), wet crimson flesh with palate ridges toward the
    lips."""
    w, h = 512, 256
    U, V = np.meshgrid((np.arange(w) + 0.5) / w, (np.arange(h) + 0.5) / h)
    du = ((U - g[0] + 0.5) % 1.0 - 0.5) * 2 * math.pi
    dv = (V - g[1]) * math.pi
    ang = np.sqrt(du * du + dv * dv)          # 0 at the front (the lips), pi at the back (the throat)
    deep = np.clip((ang - 1.25) / 1.1, 0, 1)
    flesh = np.array([0.22, 0.035, 0.025])
    ridges = 0.75 + 0.25 * np.sin(dv * 40) * (np.abs(dv) > 0.15)
    col = flesh * ridges[..., None] * (1 - deep[..., None]) + np.array([0.004, 0.0, 0.0]) * deep[..., None]
    img = bpy.data.images.new("chronos-mouth", w, h)
    img.pixels.foreach_set(np.concatenate([col, np.ones((h, w, 1))], -1).astype(np.float32).ravel())
    save(img, "chronos-mouth.png")
    return img


def mouth(rig):
    g = gaze_uv()
    cav = material("chronos-mouth", (1, 1, 1, 1), 0.6, mouth_cavity_image(g))
    # the cavity: an inflated sphere behind the lips, open to them
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, segments=40, ring_count=20)
    t = bpy.context.active_object
    t.name = "chronos-mouth"
    bpy.ops.object.shade_smooth()
    t.scale = (0.058, 0.07, 0.085)
    t.location = (MOUTH.x, MOUTH.y + 0.085, MOUTH.z - 0.025)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # seen from inside: normals turned inward and the material single-sided, so the near wall is culled and the mouth
    # shows the far wall of a cavity -- not the outside of a ball, which is what a double-sided sphere showed
    bm = bmesh.new()
    bm.from_mesh(t.data)
    bmesh.ops.reverse_faces(bm, faces=bm.faces)
    bm.to_mesh(t.data)
    bm.free()
    cav.use_backface_culling = True
    t.data.materials.append(cav)
    attach(t, rig, "head")
    # its lower half rides the jaw, so the cavity opens with the mouth
    t.vertex_groups.clear()
    gh, gj = t.vertex_groups.new(name="head"), t.vertex_groups.new(name="jaw")
    for v in t.data.vertices:
        w = smooth(MOUTH.z + 0.01, MOUTH.z - 0.04, v.co.z)
        if w > 1e-4:
            gj.add([v.index], w, "REPLACE")
        if 1 - w > 1e-4:
            gh.add([v.index], 1 - w, "REPLACE")
    # the tongue, on the jaw
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, segments=32, ring_count=16)
    tg = bpy.context.active_object
    tg.name = "chronos-tongue"
    bpy.ops.object.shade_smooth()
    tg.scale = (0.032, 0.05, 0.012)
    tg.location = (MOUTH.x, MOUTH.y + 0.06, MOUTH.z - 0.03)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    tg.data.materials.append(material("chronos-tongue", (0.09, 0.018, 0.014, 1), 0.5))
    attach(tg, rig, "jaw")
    # gums and teeth, per arch
    gum = material("chronos-gums", (0.14, 0.03, 0.025, 1), 0.5)
    for row, group, sign in (("upper", "head", -1), ("lower", "jaw", 1)):
        # the gum ridge: a flattened tube along the arch
        pts = []
        for i in range(25):
            u = i / 24 * 2 - 1
            pts.append(Vector((MOUTH.x + u * MOUTH_HALF * 0.98, MOUTH.y + 0.022 + 0.034 * u * u,
                               MOUTH.z + 0.004 * u * u - sign * 0.011)))
        bm = bmesh.new()
        ring_n = 8
        rings = []
        for k, p in enumerate(pts):
            tdir = (pts[min(k + 1, len(pts) - 1)] - pts[max(k - 1, 0)]).normalized()
            a = Vector((0, 0, 1)).cross(tdir).normalized()
            b = tdir.cross(a)
            ring = []
            for j in range(ring_n):
                ang = 2 * math.pi * j / ring_n
                ring.append(bm.verts.new(p + a * math.cos(ang) * 0.0065 + b * math.sin(ang) * 0.004))
            rings.append(ring)
        for k in range(len(rings) - 1):
            for j in range(ring_n):
                bm.faces.new((rings[k][j], rings[k][(j + 1) % ring_n], rings[k + 1][(j + 1) % ring_n], rings[k + 1][j]))
        me = bpy.data.meshes.new("chronos-gum-" + row)
        bm.to_mesh(me)
        bm.free()
        for poly in me.polygons:
            poly.use_smooth = True
        go = bpy.data.objects.new("chronos-gum-" + row, me)
        bpy.context.scene.collection.objects.link(go)
        go.data.materials.append(gum)
        attach(go, rig, group)
        # the teeth: irregular in size, spacing and angle; gaps; broken and worn; each its own shade
        bm = bmesh.new()
        u = -0.95
        mats = []
        while u < 0.95:
            step = rng.uniform(0.11, 0.22)
            u += step
            if u > 0.95:
                break
            if rng.random() < 0.18:
                continue  # a gap
            x = MOUTH.x + u * MOUTH_HALF * 0.95 + rng.uniform(-0.002, 0.002)
            y = MOUTH.y + 0.02 + 0.034 * u * u + rng.uniform(-0.003, 0.004)
            z = MOUTH.z + 0.004 * u * u - sign * 0.011
            length = rng.uniform(0.012, 0.034) * (1.3 if abs(u) < 0.3 else 1.0)
            width = rng.uniform(0.004, 0.0105)
            broken = rng.random() < 0.25
            tip = width * (rng.uniform(0.6, 0.95) if broken else rng.uniform(0.1, 0.35))
            if broken:
                length *= rng.uniform(0.35, 0.6)
            ret = bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=7, radius1=width,
                                        radius2=tip, depth=length)
            verts = ret["verts"]
            # worn and lumpy: jitter the outline a little
            for v in verts:
                v.co.x *= rng.uniform(0.8, 1.15)
                v.co.y *= rng.uniform(0.7, 1.0)
            lean = (Matrix.Rotation(math.radians(rng.uniform(-18, 18)), 4, "Y") @
                    Matrix.Rotation(math.radians(rng.uniform(-14, 24)), 4, "X") @
                    Matrix.Rotation(math.radians(rng.uniform(-25, 25)), 4, "Z"))
            flip = Matrix.Rotation(math.pi, 4, "X") if sign < 0 else Matrix.Identity(4)
            M = Matrix.Translation((x, y, z + sign * length * 0.45)) @ lean @ flip
            bmesh.ops.transform(bm, matrix=M, verts=verts)
            shade = rng.uniform(0.0, 1.0)
            mats.append((verts, shade))
        me = bpy.data.meshes.new("chronos-teeth-" + row)
        bm.to_mesh(me)
        bm.free()
        for poly in me.polygons:
            poly.use_smooth = True
        o = bpy.data.objects.new("chronos-teeth-" + row, me)
        bpy.context.scene.collection.objects.link(o)
        # three shades of aged tooth, per tooth by its first face
        shades = [material(f"chronos-teeth-{k}", c, r) for k, (c, r) in enumerate(
            [((0.38, 0.28, 0.12, 1), 0.55), ((0.27, 0.19, 0.08, 1), 0.65), ((0.18, 0.15, 0.11, 1), 0.75)])]
        for m in shades:
            o.data.materials.append(m)
        faces_per = len(me.polygons) // max(1, len(mats))
        for i, poly in enumerate(me.polygons):
            k = min(i // max(1, faces_per), len(mats) - 1)
            s = mats[k][1]
            poly.material_index = 0 if s < 0.45 else (1 if s < 0.8 else 2)
        attach(o, rig, group)


def hair_mask(obj):
    """Where the sculpted hair and beard are (the scalp above the forehead and behind the ears, the moustache, the
    beard below the mouth), as a vertex colour the skin bake reads: so the matted mass is painted as hair, not as flesh
    (the first D1b build painted it all as skin and the beard vanished)."""
    me = obj.data
    attr = me.color_attributes.new(name="hair", type="FLOAT_COLOR", domain="POINT")
    eyes = (EYE_L + EYE_R) * 0.5
    for v in me.vertices:
        p = v.co
        ax = abs(p.x - MOUTH.x)
        front = smooth(-0.04, -0.12, p.y)
        scalp = smooth(eyes.z + 0.075, eyes.z + 0.11, p.z) * (1 - 0.0 * front)
        sides = smooth(eyes.z - 0.06, eyes.z - 0.02, p.z) * smooth(-0.07, 0.0, p.y)   # behind the ears, upward
        moustache = smooth(MOUTH.z - 0.002, MOUTH.z + 0.006, p.z) * smooth(MOUTH.z + 0.05, MOUTH.z + 0.035, p.z) * \
            smooth(MOUTH_HALF * 1.5, MOUTH_HALF * 0.9, ax) * front
        beard = smooth(MOUTH.z - 0.012, MOUTH.z - 0.03, p.z) * smooth(0.17, 0.12, ax) * smooth(0.0, -0.08, p.y)
        cheeks = smooth(MOUTH.z + 0.03, MOUTH.z - 0.01, p.z) * smooth(0.07, 0.11, ax) * smooth(0.0, -0.06, p.y)
        h = max(scalp, sides, moustache, beard, cheeks)
        attr.data[v.index].color = (h, h, h, 1.0)


def paint_skin(obj, relief):
    """D1b: brush-broken albedo (cavity-driven umber to ochre, grey-green in the hollows, burgundy patches, stroke-shaped
    value breaks), a roughness map, and Rodin's clay surface as a tangent-space normal (cylindrical projection)."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 24
    src = None
    for m in obj.data.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image:
                    src = n.image
                    break
    # D1b: bake into fresh, non-overlapping UVs. The photogrammetry's own UVs overlap in places, and a bake into
    # overlapping UVs writes one face's texels onto another -- the dark triangular shards at the brows were exactly
    # that (a deep cavity's occlusion painted onto a lit face). The plaster trace still reads the old layout.
    me = obj.data
    old_uv = me.uv_layers.active.name if me.uv_layers.active else None
    new_uv = me.uv_layers.new(name="bake")
    me.uv_layers.active = new_uv
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.0015)
    bpy.ops.object.mode_set(mode="OBJECT")
    S = 4096
    albedo = bpy.data.images.new("chronos-skin", S, S)
    rough = bpy.data.images.new("chronos-rough", 2048, 2048)
    normal = bpy.data.images.new("chronos-normal", S, S)
    rough.colorspace_settings.name = "Non-Color"
    normal.colorspace_settings.name = "Non-Color"
    m = bpy.data.materials.new("chronos-bake")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    N = nt.nodes.new
    L = nt.links.new
    out = N("ShaderNodeOutputMaterial")
    emit = N("ShaderNodeEmission")
    ao = N("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.03
    ao.samples = 16
    ramp = N("ShaderNodeValToRGB")
    e = ramp.color_ramp.elements
    e[0].position, e[0].color = 0.25, (0.05, 0.022, 0.014, 1)
    e[1].position, e[1].color = 0.95, (0.56, 0.41, 0.24, 1)
    mid = e.new(0.6)
    mid.color = (0.3, 0.15, 0.08, 1)
    coord = N("ShaderNodeTexCoord")
    # brush strokes: noise stretched along one axis (dabs), at two scales
    mapping = N("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (22.0, 22.0, 70.0)
    strokes = N("ShaderNodeTexNoise")
    strokes.inputs["Scale"].default_value = 2.0
    strokes.inputs["Detail"].default_value = 3.0
    strokes.inputs["Roughness"].default_value = 0.7
    quant = N("ShaderNodeMath")   # posterise into dabs of value
    quant.operation = "SNAP"
    quant.inputs[1].default_value = 0.08
    value = N("ShaderNodeMapRange")
    value.inputs["From Min"].default_value = 0.3
    value.inputs["From Max"].default_value = 0.7
    value.inputs["To Min"].default_value = 0.7
    value.inputs["To Max"].default_value = 1.15
    mul = N("ShaderNodeMix")
    mul.data_type = "RGBA"
    mul.blend_type = "MULTIPLY"
    mul.inputs["Factor"].default_value = 1.0
    # sickly grey-green where it is deep, burgundy in patches
    green = N("ShaderNodeMix")
    green.data_type = "RGBA"
    green.inputs["B"].default_value = (0.10, 0.11, 0.075, 1)
    inv = N("ShaderNodeMapRange")
    inv.inputs["From Min"].default_value = 0.35
    inv.inputs["From Max"].default_value = 0.75
    inv.inputs["To Min"].default_value = 0.55
    inv.inputs["To Max"].default_value = 0.0
    patches = N("ShaderNodeTexNoise")
    patches.inputs["Scale"].default_value = 9.0
    patches.inputs["Detail"].default_value = 2.0
    pr = N("ShaderNodeMapRange")
    pr.inputs["From Min"].default_value = 0.55
    pr.inputs["From Max"].default_value = 0.72
    pr.inputs["To Min"].default_value = 0.0
    pr.inputs["To Max"].default_value = 0.45
    burg = N("ShaderNodeMix")
    burg.data_type = "RGBA"
    burg.inputs["B"].default_value = (0.26, 0.07, 0.06, 1)
    tex = N("ShaderNodeTexImage")
    tex.image = src
    if old_uv:
        olduv = N("ShaderNodeUVMap")
        olduv.uv_map = old_uv
        L(olduv.outputs["UV"], tex.inputs["Vector"])
    trace = N("ShaderNodeMix")
    trace.data_type = "RGBA"
    trace.blend_type = "MULTIPLY"
    trace.inputs["Factor"].default_value = 0.25 if src else 0.0
    L(ao.outputs["AO"], ramp.inputs["Fac"])
    L(coord.outputs["Object"], mapping.inputs["Vector"])
    L(mapping.outputs["Vector"], strokes.inputs["Vector"])
    L(strokes.outputs["Fac"], quant.inputs[0])
    L(quant.outputs["Value"], value.inputs["Value"])
    L(ramp.outputs["Color"], mul.inputs["A"])
    L(value.outputs["Result"], mul.inputs["B"])
    L(ao.outputs["AO"], inv.inputs["Value"])
    L(inv.outputs["Result"], green.inputs["Factor"])
    L(mul.outputs["Result"], green.inputs["A"])
    L(coord.outputs["Object"], patches.inputs["Vector"])
    L(patches.outputs["Fac"], pr.inputs["Value"])
    L(pr.outputs["Result"], burg.inputs["Factor"])
    L(green.outputs["Result"], burg.inputs["A"])
    # the hair and beard: dark grey-umber matted hair with lighter strands, by the vertex mask
    hm = N("ShaderNodeVertexColor")
    hm.layer_name = "hair"
    hmap = N("ShaderNodeMapping")
    hmap.inputs["Scale"].default_value = (90.0, 90.0, 6.0)   # strands run down
    hstr = N("ShaderNodeTexNoise")
    hstr.inputs["Scale"].default_value = 3.0
    hstr.inputs["Detail"].default_value = 6.0
    hcol = N("ShaderNodeValToRGB")
    hcol.color_ramp.elements[0].position, hcol.color_ramp.elements[0].color = 0.35, (0.05, 0.045, 0.04, 1)
    hcol.color_ramp.elements[1].position, hcol.color_ramp.elements[1].color = 0.7, (0.30, 0.27, 0.22, 1)
    hshade = N("ShaderNodeMix")
    hshade.data_type = "RGBA"
    hshade.blend_type = "MULTIPLY"
    hshade.inputs["Factor"].default_value = 1.0
    hairmix = N("ShaderNodeMix")
    hairmix.data_type = "RGBA"
    L(coord.outputs["Object"], hmap.inputs["Vector"])
    L(hmap.outputs["Vector"], hstr.inputs["Vector"])
    L(hstr.outputs["Fac"], hcol.inputs["Fac"])
    L(hcol.outputs["Color"], hshade.inputs["A"])
    L(ramp.outputs["Color"], hshade.inputs["B"])
    L(hm.outputs["Color"], hairmix.inputs["Factor"])
    L(burg.outputs["Result"], hairmix.inputs["A"])
    L(hshade.outputs["Result"], hairmix.inputs["B"])
    L(hairmix.outputs["Result"], trace.inputs["A"])
    if src:
        L(tex.outputs["Color"], trace.inputs["B"])
    L(trace.outputs["Result"], emit.inputs["Color"])
    L(emit.outputs["Emission"], out.inputs["Surface"])
    target = N("ShaderNodeTexImage")
    target.image = albedo
    nt.nodes.active = target
    obj.data.materials.clear()
    obj.data.materials.append(m)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type="EMIT", margin=24, margin_type="EXTEND")
    save(albedo, "chronos-skin.png")
    # roughness: oily on the proud forms, dry in the hollows, broken by the strokes
    rr = N("ShaderNodeMapRange")
    rr.inputs["To Min"].default_value = 0.42
    rr.inputs["To Max"].default_value = 0.88
    L(ao.outputs["AO"], rr.inputs["Value"])
    rr.inputs["From Min"].default_value = 1.0
    rr.inputs["From Max"].default_value = 0.4
    rmix = N("ShaderNodeMath")
    rmix.operation = "MULTIPLY_ADD"
    L(rr.outputs["Result"], rmix.inputs[0])
    rmix.inputs[1].default_value = 1.0
    L(value.outputs["Result"], rmix.inputs[2])
    rsub = N("ShaderNodeMath")
    rsub.operation = "SUBTRACT"
    L(rmix.outputs["Value"], rsub.inputs[0])
    rsub.inputs[1].default_value = 0.9
    rhair = N("ShaderNodeMix")
    rhair.data_type = "FLOAT"
    L(hm.outputs["Color"], rhair.inputs["Factor"])
    L(rsub.outputs["Value"], rhair.inputs["A"])
    rhair.inputs["B"].default_value = 0.85
    L(rhair.outputs["Result"], emit.inputs["Color"])
    target.image = rough
    bpy.ops.object.bake(type="EMIT", margin=24, margin_type="EXTEND")
    save(rough, "chronos-rough.png")
    # normal: Rodin's relief in a cylindrical projection about the head's axis, plus a fine pore layer
    for n in list(nt.nodes):
        if n not in (out, target, coord, hm, hmap):
            nt.nodes.remove(n)
    bsdf = N("ShaderNodeBsdfPrincipled")
    sep = N("ShaderNodeSeparateXYZ")
    L(coord.outputs["Object"], sep.inputs["Vector"])
    ax = N("ShaderNodeMath")
    ax.operation = "SUBTRACT"
    L(sep.outputs["X"], ax.inputs[0])
    ax.inputs[1].default_value = AXIS_X
    at = N("ShaderNodeMath")
    at.operation = "ARCTAN2"
    L(sep.outputs["Y"], at.inputs[0])
    L(ax.outputs["Value"], at.inputs[1])
    uu = N("ShaderNodeMath")
    uu.operation = "MULTIPLY_ADD"
    L(at.outputs["Value"], uu.inputs[0])
    uu.inputs[1].default_value = 1.0 / (2 * math.pi)
    uu.inputs[2].default_value = 0.5
    vv = N("ShaderNodeMapRange")
    L(sep.outputs["Z"], vv.inputs["Value"])
    vv.inputs["From Min"].default_value = 0.08
    vv.inputs["From Max"].default_value = 0.92
    comb = N("ShaderNodeCombineXYZ")
    L(uu.outputs["Value"], comb.inputs["X"])
    L(vv.outputs["Result"], comb.inputs["Y"])
    rel = N("ShaderNodeTexImage")
    rel.image = relief
    rel.extension = "REPEAT"
    L(comb.outputs["Vector"], rel.inputs["Vector"])
    pores = N("ShaderNodeTexVoronoi")
    pores.inputs["Scale"].default_value = 700.0
    pw = N("ShaderNodeMath")
    pw.operation = "MULTIPLY_ADD"
    L(pores.outputs["Distance"], pw.inputs[0])
    pw.inputs[1].default_value = 0.15
    L(rel.outputs["Color"], pw.inputs[2])
    hstreak = N("ShaderNodeTexNoise")
    hstreak.inputs["Scale"].default_value = 4.0
    hstreak.inputs["Detail"].default_value = 8.0
    L(hmap.outputs["Vector"], hstreak.inputs["Vector"])
    hsel = N("ShaderNodeMix")
    hsel.data_type = "FLOAT"
    L(hm.outputs["Color"], hsel.inputs["Factor"])
    L(pw.outputs["Value"], hsel.inputs["A"])
    L(hstreak.outputs["Fac"], hsel.inputs["B"])
    bump = N("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.9
    bump.inputs["Distance"].default_value = 0.004
    L(hsel.outputs["Result"], bump.inputs["Height"])
    L(bump.outputs["Normal"], bsdf.inputs["Normal"])
    L(bsdf.outputs["BSDF"], out.inputs["Surface"])
    target.image = normal
    bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", margin=24, margin_type="EXTEND")
    save(normal, "chronos-normal.png")
    final = material("chronos-skin", (1, 1, 1, 1), 0.7, albedo, rough, normal)
    obj.data.materials.clear()
    obj.data.materials.append(final)
    if old_uv and old_uv in me.uv_layers:
        me.uv_layers.remove(me.uv_layers[old_uv])
    me.uv_layers["bake"].active_render = True


def life_clip(rig):
    fps = 24
    scene = bpy.context.scene
    scene.render.fps = fps
    act = bpy.data.actions.new("idle")
    rig.animation_data_create()
    rig.animation_data.action = act
    pb = rig.pose.bones
    for n in pb:
        n.rotation_mode = "XYZ"
    # D1b: the jaw hangs at 12 degrees at rest
    jaw = [(0, 12), (1.6, 17), (2.9, 13), (4.4, 20), (5.6, 14), (6.6, 28), (8.2, 62), (9.4, 66), (10.6, 32),
           (11.4, 16), (12.0, 12)]
    for t, deg in jaw:
        pb["jaw"].rotation_euler = Euler((math.radians(deg), 0, 0))
        pb["jaw"].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    look = [(0, (0, 0)), (2.0, (0, 0)), (2.15, (-4, -9)), (4.5, (-4, -9)), (4.65, (2, 8)), (7.4, (2, 8)),
            (7.55, (-6, 2)), (10.2, (-6, 2)), (10.35, (0, 0)), (12.0, (0, 0))]
    for t, (p, y) in look:
        for n in ("eye.L", "eye.R"):
            pb[n].rotation_euler = Euler((math.radians(p), 0, math.radians(y)))
            pb[n].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    head = [(0, (0, 0, 0)), (4.0, (1.2, 0, -1.5)), (8.2, (-6, 0, 1.0)), (9.4, (-7, 0, 1.5)), (12.0, (0, 0, 0))]
    for t, rot in head:
        pb["head"].rotation_euler = Euler(tuple(math.radians(v) for v in rot))
        pb["head"].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    scene.frame_start, scene.frame_end = 0, 12 * fps


def export():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", use_selection=False, export_apply=False,
                              export_animations=True, export_skins=True, export_morph=False)
    print("exported", OUT)


def stills():
    if not STILLS:
        return
    os.makedirs(STILLS, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1200
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - High Contrast"
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.008, 0.002, 0.002, 1)
    scene.world = world
    c = Vector((MOUTH.x, -0.15, (EYE_L.z + MOUTH.z) * 0.5 + 0.02))

    def area(name, energy, color, loc, size):
        Lt = bpy.data.lights.new(name, "AREA")
        Lt.energy, Lt.color, Lt.size = energy, color, size
        o = bpy.data.objects.new(name, Lt)
        o.location = c + Vector(loc)
        o.rotation_euler = (c - o.location).to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(o)
    area("key", 22.0, (1.0, 0.68, 0.38), (-0.75, -0.55, 0.65), 0.12)
    area("under", 1.0, (0.9, 0.12, 0.05), (0.05, -0.6, -0.35), 0.15)
    area("rim", 5.0, (0.6, 0.12, 0.2), (0.7, 0.6, 0.35), 0.2)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    for name, frame, az, el, dist, lens in (("three-quarter-rest", 48, 32, 5, 1.25, 75),
                                            ("three-quarter-devour", 220, 30, 1, 1.2, 75),
                                            ("eyes-close", 48, 22, 4, 0.55, 85), ("mouth-close", 220, 18, -4, 0.6, 85)):
        scene.frame_set(frame)
        a, e = math.radians(az), math.radians(el)
        cam.data.lens = lens
        tgt = c
        if name == "eyes-close":
            tgt = (EYE_L + EYE_R) * 0.5
        if name == "mouth-close":
            tgt = MOUTH + Vector((0, 0, -0.03))
        cam.location = tgt + Vector((math.sin(a) * dist * math.cos(e), -math.cos(a) * dist * math.cos(e), dist * math.sin(e)))
        cam.rotation_euler = (tgt - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(STILLS, f"chronos-scan-{name}.png")
        bpy.ops.render.render(write_still=True)


bpy.ops.wm.read_homefile(use_empty=True)
relief = rodin_relief(RODIN)
obj = load_normalised(SCAN, "chronos-head")
warp_landmarks()
shape(obj)
cut(obj)
hair_mask(obj)
paint_skin(obj, relief)
EYE_L = seat_eye(obj, EYE_L)
EYE_R = seat_eye(obj, EYE_R)
print("eye seats", tuple(round(x, 3) for x in EYE_L), tuple(round(x, 3) for x in EYE_R))
rig = build_rig(obj)
eyes_and_lids(rig, (0.30, 0.19, 0.11, 1))
mouth(rig)
life_clip(rig)
bpy.ops.wm.save_as_mainfile(filepath=os.path.splitext(OUT)[0] + ".blend")
export()
stills()
print("done")
