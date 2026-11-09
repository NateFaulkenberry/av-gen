"""Hero asset H5 (brief 49, Phases 4.1-4.2): the parked craft, upgraded from BrunoLatour's "UFO" (Sketchfab, CC BY 4.0;
assets-pass2.md). Geometry and UVs are the author's; the materials are reworked and restrained emissive elements added.

    Blender -b --factory-startup --python examples/strange-attractor/hero/build_ufo_v1.py -- \
        assets/strange-attractor/sketchfab/ufo/ufo-brunolatour/ufo.glb assets/strange-attractor/hero/ufo

Writes ufo-v1.glb (+ .blend) in the out dir (gitignored, never committed), in the same glTF frame and units as
build/ufo-craft.glb, so the scene's node (scale 0.018, emissiveBoost) takes it unchanged.

Why it read as a dark toy (H1 audit): every surface is a near-mirror metal (roughness 0.30 +- 0.02) that at night
reflects a black sky, and its occlusion map is very dark (means 0.17-0.46) and AV Gen multiplies IBL by it. So:
- roughness: 0.34-0.62 by panel, plus a fine brushed streak (anisotropy is unsupported, H1 G5) -- the moon and fire
  now spread over the hull instead of vanishing in a mirror;
- occlusion: remapped to 0.55-1.0 (it keeps the seams and recesses, no longer blacks out whole panels), and the
  recesses also darkened in the base colour (grime in the seams, 15%);
- emissives: the authored GREEN recoloured -- the window band a dim warm interior, the underside lights cool white --
  and the green spots in the base colour turned to dark lenses;
- added: 48 rim light cells (individual segments round the perimeter, cool white), four soft white navigation lights
  at the rim and a thin underbody ring glow (pale cyan). No greebles.
"""
import json
import math
import os
import struct
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

src, outdir = sys.argv[sys.argv.index("--") + 1:][:2]
os.makedirs(outdir, exist_ok=True)
rng = np.random.default_rng(3)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
scene = bpy.context.scene
meshes = [o for o in bpy.data.objects if o.type == "MESH"]


def pix(im):
    w, h = im.size
    a = np.empty(w * h * 4, np.float32)
    im.pixels.foreach_get(a)
    return a.reshape(h, w, 4)


def put(im, a):
    im.pixels.foreach_set(np.ascontiguousarray(a).ravel())
    im.pack()


def streaks(h, w, seed):
    """Brushed metal: thin random bands along the texture's u, broken by a coarse panel mottle."""
    r = np.random.default_rng(seed)
    rows = r.normal(0, 1, h).astype(np.float32)
    for _ in range(2):
        rows = (rows + np.roll(rows, 1) + np.roll(rows, -1)) / 3
    rows /= rows.std() + 1e-6
    coarse = r.random((9, 9)).astype(np.float32)
    ys = np.linspace(0, 7.999, h)[:, None]
    xs = np.linspace(0, 7.999, w)[None, :]
    y0, x0 = ys.astype(int), xs.astype(int)
    fy, fx = ys - y0, xs - x0
    c = (coarse[y0, x0] * (1 - fx) + coarse[y0, x0 + 1] * fx) * (1 - fy) + \
        (coarse[y0 + 1, x0] * (1 - fx) + coarse[y0 + 1, x0 + 1] * fx) * fy
    fine = r.normal(0, 1, (h, w)).astype(np.float32) * 0.25
    return rows[:, None] * 0.6 + fine, c


done = set()
WARM = np.array([1.0, 0.82, 0.62], np.float32)
COOL = np.array([0.8, 0.9, 1.0], np.float32)
for mat in bpy.data.materials:
    if not mat.use_nodes:
        continue
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        continue
    def img_into(inp):
        for l in nt.links:
            if l.to_socket == bsdf.inputs[inp]:
                n = l.from_node
                while n.type != "TEX_IMAGE" and n.inputs and any(i.is_linked for i in n.inputs):
                    n = next(i.links[0].from_node for i in n.inputs if i.is_linked)
                return n.image if n.type == "TEX_IMAGE" else None
        return None
    base, rough, emis = img_into("Base Color"), img_into("Roughness"), img_into("Emission Color")
    em_a = pix(emis) if emis is not None else None
    if rough is not None and rough.name not in done:
        a = pix(rough)
        h, w = a.shape[:2]
        st, panel = streaks(h, w, len(done))
        a[..., 0] = 0.55 + 0.45 * a[..., 0]                             # occlusion (R): no more blacked-out panels
        a[..., 1] = np.clip(0.34 + (a[..., 1] - 0.3) * 1.2 + 0.18 * panel + 0.05 * st, 0.24, 0.66)
        put(rough, a)
        done.add(rough.name)
        ao = a[..., 0]
    else:
        ao = None
    if base is not None and base.name not in done:
        a = pix(base)
        if ao is not None and ao.shape == a.shape[:2]:
            a[..., :3] *= (0.85 + 0.15 * (ao[..., None] - 0.55) / 0.45)  # grime in the recesses
        if em_a is not None and em_a.shape == a.shape:
            lens = np.clip(em_a[..., :3].max(-1) * 4, 0, 1)[..., None]
            a[..., :3] = a[..., :3] * (1 - lens) + 0.06 * lens          # the green spots become dark lenses
        put(base, a)
        done.add(base.name)
    if emis is not None and emis.name not in done:
        L = em_a[..., :3].max(-1)
        tint = WARM if mat.name == "Medium_02" else COOL                 # the window band: a dim warm interior
        em_a[..., :3] = L[..., None] * tint
        put(emis, em_a)
        done.add(emis.name)
    bsdf.inputs["Emission Strength"].default_value = 0.6 if mat.name == "Medium_02" else 2.0
    try:
        mat.blend_method = "OPAQUE"
    except Exception:
        pass
    print("MAT", mat.name, "reworked")

# ---------------------------------------------------------------- the profile, in world space (Blender z-up)
bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
pts = []
bm_all = bmesh.new()
for o in meshes:
    ev = o.evaluated_get(dg)
    m = ev.to_mesh()
    tmp = bmesh.new()
    tmp.from_mesh(m)
    tmp.transform(ev.matrix_world)
    tm = bpy.data.meshes.new("tmp")
    tmp.to_mesh(tm)
    bm_all.from_mesh(tm)
    bpy.data.meshes.remove(tm)
    tmp.free()
    ev.to_mesh_clear()
bm_all.verts.ensure_lookup_table()
P = np.array([v.co[:] for v in bm_all.verts])
bvh = BVHTree.FromBMesh(bm_all)
r = np.hypot(P[:, 0], P[:, 1])
R = r.max()
rim_z = P[r > 0.995 * R, 2].mean()
print("UFO radius", round(R, 2), "rim z", round(rim_z, 2), "z", P[:, 2].min().round(2), P[:, 2].max().round(2))


def surface_z(rad, ang, from_below=True):
    o = Vector((rad * math.cos(ang), rad * math.sin(ang), P[:, 2].min() - 10 if from_below else P[:, 2].max() + 10))
    hit = bvh.ray_cast(o, Vector((0, 0, 1 if from_below else -1)))
    return hit[0].z if hit[0] is not None else None


def emissive(name, col, strength):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.05, 0.05, 0.055, 1)
    b.inputs["Roughness"].default_value = 0.2
    b.inputs["Emission Color"].default_value = col + (1,)
    b.inputs["Emission Strength"].default_value = strength
    return m


def add_obj(name, bm, mat):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    scene.collection.objects.link(o)
    me.materials.append(mat)
    return o


# rim cells: 48 individual segments in the lip just under the rim's widest edge
bm = bmesh.new()
cells = 48
for k in range(cells):
    a = 2 * math.pi * (k + 0.5) / cells
    seg = bmesh.ops.create_cube(bm, size=1.0)
    for v in seg["verts"]:
        lx, ly, lz = v.co
        v.co = Vector((lx * 0.006 * R, ly * 2 * math.pi * R / cells * 0.32, lz * 0.009 * R))
        rr = R * 0.99 + v.co.x
        v.co = Vector((rr * math.cos(a) - v.co.y * math.sin(a), rr * math.sin(a) + v.co.y * math.cos(a),
                       rim_z - 0.035 * R + v.co.z))
add_obj("ufo_rim_cells", bm, emissive("ufo_rim_cells", (0.78, 0.9, 1.0), 0.9))
# navigation lights: four soft white points on the rim
bm = bmesh.new()
for k in range(4):
    a = math.pi / 4 + k * math.pi / 2
    ico = bmesh.ops.create_icosphere(bm, subdivisions=2, radius=0.007 * R)
    for v in ico["verts"]:
        v.co += Vector((R * 1.004 * math.cos(a), R * 1.004 * math.sin(a), rim_z))
add_obj("ufo_nav_lights", bm, emissive("ufo_nav_lights", (1.0, 0.97, 0.92), 3.0))
# the underbody ring: a thin annulus hugging the underside
bm = bmesh.new()
r0, r1, seg = 0.42 * R, 0.45 * R, 128
ring = []
for k in range(seg):
    a = 2 * math.pi * k / seg
    zs = [surface_z(rr, a) for rr in (r0, r1)]
    zs = [z if z is not None else P[:, 2].min() for z in zs]
    ring.append((bm.verts.new((r0 * math.cos(a), r0 * math.sin(a), zs[0] - 0.004 * R)),
                 bm.verts.new((r1 * math.cos(a), r1 * math.sin(a), zs[1] - 0.004 * R))))
for k in range(seg):
    a_, b_ = ring[k], ring[(k + 1) % seg]
    bm.faces.new((a_[0], b_[0], b_[1], a_[1]))
add_obj("ufo_underglow", bm, emissive("ufo_underglow", (0.6, 0.85, 1.0), 0.9))

blend = os.path.join(outdir, "ufo-v1.blend")
glb = os.path.join(outdir, "ufo-v1.glb")
bpy.ops.wm.save_as_mainfile(filepath=blend, compress=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=glb, export_image_format="JPEG", export_jpeg_quality=92)
b = open(glb, "rb").read()
n = struct.unpack("<I", b[12:16])[0]
js = json.loads(b[20:20 + n])
rest = b[20 + n:]
for m in js.get("materials", []):
    m["alphaMode"] = "OPAQUE"
    m.pop("alphaCutoff", None)
enc = json.dumps(js, separators=(",", ":")).encode()
enc += b" " * ((-len(enc)) % 4)
open(glb, "wb").write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(enc) + len(rest)) +
                      struct.pack("<II", len(enc), 0x4E4F534A) + enc + rest)
print("UFO", glb, round(os.path.getsize(glb) / 1e6, 1), "MB")
