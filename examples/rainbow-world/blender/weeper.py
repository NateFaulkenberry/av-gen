"""Rainbow World, Stage 5: the Weeper -- the second tree of the family (03-art-direction.md section 6; R4's weeping trees).

Silhouette first: a short, leaning trunk that splits into six arching limbs; from the arches, ~220 hanging strands fall
almost to the ground, so the tree reads as a dome with a fringed curtain -- unmistakable next to the Great Tree's
flat pads. Leaf cards (a narrow willow-leaf cluster, our own) hang along each strand; their normals come from the
curtain's hull (a dome), so the curtain shades as one soft volume.

Outputs (args: <outdir> [w h samples]): weeper_turn_<k>.png, weeper_silhouette_200m.png, weeper_{wood,leaves,core}.glb
"""
import math
import os
import random
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw_common as rw  # noqa: E402
import trees  # noqa: E402

DOME_C = Vector((0.0, 0.0, 7.0))
DOME_R = Vector((11.0, 10.5, 6.2))


def dome_normal(p):
    q = Vector(((p.x - DOME_C.x) / DOME_R.x ** 2, (p.y - DOME_C.y) / DOME_R.y ** 2, (p.z - DOME_C.z) / DOME_R.z ** 2))
    return q.normalized() if q.length > 1e-9 else Vector((0, 0, 1))


def willow_image(path, size=256, seed=6):
    """Narrow hanging willow leaves along a central stem, the card's top at v = 1."""
    rng = random.Random(seed)
    img = np.zeros((size, size, 4), np.float32)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) / size
    for k in range(14):
        cy = rng.uniform(0.08, 0.95)
        cx = 0.5 + rng.uniform(-0.06, 0.06)
        ang = math.pi / 2 + rng.uniform(-0.5, 0.5) + (0.35 if k % 2 else -0.35)
        ln, wd = rng.uniform(0.18, 0.28), rng.uniform(0.025, 0.04)
        ca, sa = math.cos(ang), math.sin(ang)
        u = (xx - cx) * ca + (yy - cy) * sa
        v = -(xx - cx) * sa + (yy - cy) * ca
        t = np.clip(u / ln * 0.5 + 0.5, 0, 1)
        half = wd * np.sin(np.pi * t) ** 0.7
        inside = (np.abs(u) <= ln) & (np.abs(v) <= half)
        val = 0.65 + 0.3 * t
        img[inside, 0:3] = val[inside][:, None]
        img[inside, 3] = 1.0
    bi = bpy.data.images.new("willow_cluster", size, size, alpha=True)
    bi.pixels = img[::-1].ravel()
    bi.filepath_raw = path
    bi.file_format = "PNG"
    bi.save()
    return bi


def build_weeper(out, rng):
    coll = bpy.data.collections.new("weeper")
    bpy.context.scene.collection.children.link(coll)
    parts = []
    base, split = Vector((0, 0, -0.5)), Vector((0.8, 0.4, 4.8))
    tr = trees.bezier_path(base, Vector((0.6, 0.2, 2.0)), Vector((1.1, 0.5, 3.6)), split, 12)
    parts.append(trees.branch_curve("trunk", tr, [1.25 * (1 + 0.8 * max(0.0, 1 - i / 3) ** 2) * (1 - 0.35 * i / 12) for i in range(13)], coll))
    arches = []
    for k in range(6):
        a = k * 2 * math.pi / 6 + rng.uniform(-0.25, 0.25)
        d = Vector((math.cos(a), math.sin(a), 0))
        reach = rng.uniform(6.5, 8.5)
        top = split + d * reach * 0.55 + Vector((0, 0, rng.uniform(5.5, 7.0)))
        end = split + d * reach + Vector((0, 0, rng.uniform(2.5, 4.0)))
        pts = trees.bezier_path(split, split + d * 1.5 + Vector((0, 0, 3.0)), top, end, 16)
        parts.append(trees.branch_curve("limb%d" % k, pts, [0.6 * (1 - 0.75 * i / 16) for i in range(17)], coll))
        arches.append(pts)
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.join()
    wood = bpy.context.view_layer.objects.active
    wood.name = "wood"
    bpy.ops.object.shade_smooth()
    wood.data.materials.append(trees.bark_material())
    # the curtain: strands hanging from the arches (and from a crown ring), cards along each
    img = willow_image(os.path.join(out, "willow_cluster.png"))
    me = bpy.data.meshes.new("leaves")
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    normals = []
    anchors = []
    for pts in arches:
        for i in range(2, 17):
            for _ in range(5):
                anchors.append(pts[i] + Vector((rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), rng.uniform(-0.3, 0.4))))
    for _ in range(140):  # the crown: strands from inside the dome's top
        a, r = rng.uniform(0, 2 * math.pi), rng.uniform(0, 4.5)
        anchors.append(split + Vector((math.cos(a) * r, math.sin(a) * r, rng.uniform(5.0, 7.5))))
    gaps = [rng.uniform(0, 2 * math.pi) for _ in range(2)]  # two windows in the curtain where the trunk shows
    for anc in anchors:
        az = math.atan2(anc.y, anc.x)
        if any(abs((az - g + math.pi) % (2 * math.pi) - math.pi) < 0.28 for g in gaps) and rng.random() < 0.85:
            continue
        radial = min(1.0, Vector((anc.x, anc.y)).length / DOME_R.x)
        # outer strands sweep to the ground, inner ones hang short: the skirt flares
        drop = (0.35 + 0.6 * radial ** 1.5) * rng.uniform(0.85, 1.05) * (anc.z + 0.5)
        sway = Vector((rng.uniform(-0.6, 0.6), rng.uniform(-0.6, 0.6), 0))
        n = max(3, int(drop / 0.75))
        for j in range(n):
            t = j / max(1, n - 1)
            p = anc + Vector((0, 0, -drop * t)) + sway * t * t
            size = rng.uniform(0.8, 1.15)
            nrm = dome_normal(p)
            # hang the card: its long axis down, facing outward
            out_dir = Vector((nrm.x, nrm.y, 0)).normalized() if Vector((nrm.x, nrm.y)).length > 1e-3 else Vector((1, 0, 0))
            out_dir = (Matrix.Rotation(rng.uniform(-0.6, 0.6), 3, "Z") @ out_dir).normalized()
            side = Vector((0, 0, 1)).cross(out_dir).normalized()
            down = Vector((0, 0, -1))
            corners = [p + side * (sx * size * 0.55) + down * (sy * size) for sx, sy in ((-1, 1), (1, 1), (1, 0), (-1, 0))]
            vs = [bm.verts.new(c) for c in corners]
            f = bm.faces.new(vs)
            for loop, uv in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
                loop[uvl].uv = uv
            for v in vs:
                normals.append((v, dome_normal(v.co)))
    # the cap: cards over the dome's upper surface, so the top reads as foliage, not as the core
    for _ in range(900):
        d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), abs(rng.gauss(0, 1)) + 0.5)).normalized()
        p = DOME_C + Vector((d.x * DOME_R.x, d.y * DOME_R.y, d.z * DOME_R.z)) * rng.uniform(0.82, 1.0)
        nrm = dome_normal(p)
        size = rng.uniform(1.1, 1.6)
        q = (nrm + Vector((rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4), 0.2))).normalized().to_track_quat("Z", "Y")
        q = q @ Quaternion((0, 0, 1), rng.uniform(0, 6.283))
        mm = q.to_matrix()
        vs = [bm.verts.new(mm @ Vector((sx * size * 0.5, sy * size * 0.5, 0)) + p) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        f = bm.faces.new(vs)
        for loop, uv in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
            loop[uvl].uv = uv
        for v in vs:
            normals.append((v, dome_normal(v.co)))
    bm.to_mesh(me)
    nmap = {v.index: nn for v, nn in normals}
    bm.free()
    for poly in me.polygons:
        poly.use_smooth = True
    me.normals_split_custom_set_from_vertices([nmap[i] for i in range(len(me.vertices))])
    leaves = bpy.data.objects.new("leaves", me)
    coll.objects.link(leaves)
    leaves.data.materials.append(trees.leaf_material(img))
    # an inner core (the dome at 70%) for the canopy's shadow and to close the see-through
    mb = bpy.data.metaballs.new("core")
    mb.resolution = 0.8
    e = mb.elements.new()
    e.type = "ELLIPSOID"
    e.co = DOME_C + Vector((0, 0, 2.2))
    e.radius = 1.0
    e.size_x, e.size_y, e.size_z = DOME_R.x * 0.42, DOME_R.y * 0.42, DOME_R.z * 0.35
    cob = bpy.data.objects.new("core", mb)
    bpy.context.scene.collection.objects.link(cob)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = cob
    cob.select_set(True)
    bpy.ops.object.convert(target="MESH")
    core = bpy.context.view_layer.objects.active
    core.name = "weeper_core"
    m = bpy.data.materials.new("core")
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = rw.P["canopy_core"]
    core.data.materials.append(m)
    return wood, leaves, core


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0] if args else "build/rw/stage5/weeper")
    W = int(args[1]) if len(args) > 1 else 640
    H = int(args[2]) if len(args) > 2 else 480
    S = int(args[3]) if len(args) > 3 else 32
    os.makedirs(out, exist_ok=True)
    sc = rw.reset()
    wood, leaves, core = build_weeper(out, random.Random(31))
    # engine exports (glTF-plain; colour from AV Gen programs)
    bpy.data.images["willow_cluster"].name = "leaf_cluster" if "leaf_cluster" not in bpy.data.images else "willow_cluster"
    if "leaf_cluster" not in bpy.data.images:
        bpy.data.images["willow_cluster"].name = "leaf_cluster"
    trees.export_engine(wood, leaves, os.path.join(out, "weeper.glb"))
    import engine_assets  # noqa: E402
    bpy.ops.object.select_all(action="DESELECT")
    engine_assets.decimate_to(core, 1500)
    rw.export_glb([core], os.path.join(out, "weeper_core.glb"))
    sun = Vector((0.55, -0.6, 0.58)).normalized()
    rw.sky_world(sc, sun, strength=0.95)
    rw.sun_light(sun, strength=5.0)
    bpy.ops.mesh.primitive_circle_add(vertices=96, radius=40, fill_type="NGON")
    g = bpy.context.object
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = rw.P["meadow"]
    g.data.materials.append(gm)
    rw.setup_render(sc, W, H, samples=S)
    cam = rw.camera(lens=30)
    for k, ang in enumerate((200, 290, 20)):
        r = math.radians(ang)
        rw.aim(cam, (math.cos(r) * 40, math.sin(r) * 40, 5.0), (0, 0, 6.0))
        rw.render(sc, os.path.join(out, "weeper_turn_%d.png" % k))
    print("WEEPER cards", len(leaves.data.polygons))
    print("done")


if __name__ == "__main__":
    main()
