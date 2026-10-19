"""Rainbow World, Stage 2 prototype: the Great Tree (the hero landmark of 03-art-direction.md section 6).

Design, in order of importance (02-research.md A):
  1. SILHOUETTE: a twisting trunk that splits low into four limbs; a canopy of seven authored masses in three
     tiers -- a crown, a ring of four, two drooping skirts -- with gaps between them so branches show (negative
     space). It must read from its silhouette alone at 200 m.
  2. SHADING: leaf cards whose normals come from the canopy HULL (the smooth union of the masses), so the
     canopy shades as a few soft volumes, not as thousands of cards.
  3. COLOUR: a gradient from the hull's core (deep canopy green) to its sunlit skin (lit canopy green), a
     per-mass hue shift, a little light through the leaves.
  4. Only then texture: a painted leaf-cluster card (generated here, our own art) and a bark breakup.

Outputs (args: <outdir> [w h samples]): tree_turn_<k>.png, tree_silhouette_200m.png, tree_backlit.png,
great_tree.glb, great_tree.blend, leaf_cluster.png
"""
import math
import os
import random
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw_common as rw  # noqa: E402

# ---- the design --------------------------------------------------------------------------------------------
# canopy masses: centre (m, tree space, z up), radii (x, y, z), hue shift (-1..1)
MASSES = [  # cloud-pruned pads in three tiers (niwaki-like layered canopy): flat-bottomed, wide, with sky between
    ((0.0, 0.5, 23.5), (6.5, 6.0, 3.0), 0.10),      # crown pad
    ((8.0, 3.5, 18.0), (7.0, 6.0, 2.6), -0.04),     # middle tier, spiralling
    ((-7.5, 5.5, 19.0), (6.4, 5.6, 2.5), 0.02),
    ((-2.0, -8.0, 17.4), (6.8, 5.8, 2.6), 0.07),
    ((12.5, -6.0, 13.0), (6.6, 5.4, 2.3), -0.08),   # lower tier, reaching far out
    ((-13.0, -2.5, 13.8), (6.8, 5.6, 2.4), 0.04),
    ((3.0, 12.0, 12.6), (6.0, 5.2, 2.2), -0.10),
    ((-6.0, -14.0, 11.6), (5.2, 4.6, 2.0), 0.0),
]


def hull_sdf(p, k=1.2):
    """Smooth-min union of the ellipsoid masses (approximate ellipsoid distance), metres."""
    acc = 0.0
    for c, r, _ in MASSES:
        q = Vector(((p.x - c[0]) / r[0], (p.y - c[1]) / r[1], (p.z - c[2]) / r[2]))
        d = (q.length - 1.0) * min(r)
        acc += math.exp(-d / k)
    return -k * math.log(max(acc, 1e-12))


def hull_normal(p, e=0.25):
    g = Vector((hull_sdf(p + Vector((e, 0, 0))) - hull_sdf(p - Vector((e, 0, 0))),
                hull_sdf(p + Vector((0, e, 0))) - hull_sdf(p - Vector((0, e, 0))),
                hull_sdf(p + Vector((0, 0, e))) - hull_sdf(p - Vector((0, 0, e)))))
    return g.normalized() if g.length > 1e-9 else Vector((0, 0, 1))


# ---- skeleton -----------------------------------------------------------------------------------------------
def branch_curve(name, pts, radii, coll):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = 1.0
    cu.bevel_resolution = 3
    cu.use_fill_caps = True
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for i, (p, r) in enumerate(zip(pts, radii)):
        sp.points[i].co = (p.x, p.y, p.z, 1.0)
        sp.points[i].radius = r
    ob = bpy.data.objects.new(name, cu)
    coll.objects.link(ob)
    return ob


def bezier_path(a, b, c, d, n):
    out = []
    for i in range(n + 1):
        t = i / n
        mt = 1 - t
        out.append(a * mt ** 3 + b * 3 * mt * mt * t + c * 3 * mt * t * t + d * t ** 3)
    return out


def build_skeleton(coll, rng):
    parts = []
    # trunk: a slow twist, flaring at the root
    base, split = Vector((0, 0, -0.6)), Vector((1.0, -0.6, 9.5))
    tr = bezier_path(base, Vector((1.4, 1.0, 3.0)), Vector((-1.2, 0.6, 6.5)), split, 18)
    rad = [2.0 * (1.0 + 0.9 * max(0.0, 1 - i / 4) ** 2) * (1 - 0.4 * i / 18) for i in range(19)]
    parts.append(branch_curve("trunk", tr, rad, coll))
    # root flares: four short tapering buttresses
    for k in range(5):
        a = k * 2 * math.pi / 5 + 0.4
        d = Vector((math.cos(a), math.sin(a), 0))
        pts = bezier_path(Vector((0, 0, 1.6)), d * 1.2 + Vector((0, 0, 0.6)), d * 2.6 + Vector((0, 0, -0.1)), d * 3.6 + Vector((0, 0, -0.6)), 8)
        parts.append(branch_curve("root%d" % k, pts, [0.9 * (1 - i / 9) + 0.12 for i in range(9)], coll))
    # limbs to each canopy mass; then twigs into the mass
    for mi, (c, r, _) in enumerate(MASSES):
        cv = Vector(c)
        end = cv - Vector((0, 0, r[2] * 0.4))
        d = end - split
        mid1 = split + d * 0.35 + Vector((0, 0, 1.5)) + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0))
        mid2 = split + d * 0.7 + Vector((0, 0, -1.5 if mi >= 4 else 0.8))
        pts = bezier_path(split, mid1, mid2, end, 14)
        r0 = 1.05 if mi < 4 else 0.85
        parts.append(branch_curve("limb%d" % mi, pts, [r0 * (1 - 0.7 * i / 14) for i in range(15)], coll))
        for k in range(5):
            a = rng.uniform(0, 2 * math.pi)
            tip = cv + Vector((math.cos(a) * r[0] * 0.75, math.sin(a) * r[1] * 0.75, rng.uniform(-0.3, 0.5) * r[2]))
            p0 = pts[rng.randint(9, 13)]
            tw = bezier_path(p0, p0.lerp(tip, 0.4) + Vector((0, 0, 0.8)), p0.lerp(tip, 0.75), tip, 8)
            parts.append(branch_curve("twig%d_%d" % (mi, k), tw, [0.32 * (1 - 0.85 * i / 8) for i in range(9)], coll))
    return parts


# ---- leaves -----------------------------------------------------------------------------------------------
def leaf_cluster_image(path, size=512, seed=4):
    """Our own painted leaf-cluster card: 11 almond leaves round a stem point, soft painterly value inside each
    leaf (lighter toward the tip), alpha cut-out. Colour comes from the material, so the card is grey."""
    rng = random.Random(seed)
    img = np.zeros((size, size, 4), np.float32)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) / size
    for k in range(11):
        a = rng.uniform(0, 2 * math.pi)
        dist = rng.uniform(0.08, 0.3)
        cx, cy = 0.5 + math.cos(a) * dist, 0.5 + math.sin(a) * dist
        ang = a + rng.uniform(-0.6, 0.6)
        ln, wd = rng.uniform(0.16, 0.24), rng.uniform(0.06, 0.09)
        ca, sa = math.cos(ang), math.sin(ang)
        u = (xx - cx) * ca + (yy - cy) * sa
        v = -(xx - cx) * sa + (yy - cy) * ca
        t = np.clip(u / ln * 0.5 + 0.5, 0, 1)  # 0 at the base, 1 at the tip
        half = wd * np.sin(np.pi * t) ** 0.8    # almond outline
        inside = (np.abs(u) <= ln) & (np.abs(v) <= half)
        val = 0.62 + 0.3 * t + 0.08 * rng.random()  # lighter toward the tip
        img[inside, 0:3] = val[inside][:, None]
        img[inside, 3] = 1.0
    bi = bpy.data.images.new("leaf_cluster", size, size, alpha=True)
    bi.pixels = img[::-1].ravel()
    bi.filepath_raw = path
    bi.file_format = "PNG"
    bi.save()
    return bi


def leaf_cards(coll, rng, per_mass=560):
    me = bpy.data.meshes.new("leaves")
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    col = bm.loops.layers.color.new("leaf")
    normals = []
    for mi, (c, r, hue) in enumerate(MASSES):
        cv = Vector(c)
        for _ in range(per_mass):
            # positions on and just inside the mass surface (a shell), more toward the top
            d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1) + 0.25)).normalized()
            shell = rng.uniform(0.8, 1.03)
            p = cv + Vector((d.x * r[0], d.y * r[1], d.z * r[2])) * shell
            n = hull_normal(p)
            depth = max(0.0, min(1.0, -hull_sdf(p) / 3.0))  # 0 at the skin, 1 deep inside
            size = rng.uniform(1.3, 2.0)
            # card faces roughly outward, with a random roll
            tgt = (n + Vector((rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5), rng.uniform(-0.3, 0.5)))).normalized()
            q = tgt.to_track_quat("Z", "Y") @ Matrix.Rotation(rng.uniform(0, 2 * math.pi), 4, "Z").to_quaternion()
            m = q.to_matrix()
            corners = [m @ Vector((sx * size * 0.5, sy * size * 0.5, 0)) + p for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            vs = [bm.verts.new(cc) for cc in corners]
            f = bm.faces.new(vs)
            for loop, uv in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
                loop[uvl].uv = uv
                loop[col] = (depth, 0.5 + 0.5 * hue, rng.random(), 1.0)
            for v in vs:
                normals.append((v, hull_normal(v.co)))
    bm.to_mesh(me)
    nmap = {v.index: n for v, n in normals}
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    me.normals_split_custom_set_from_vertices([nmap[i] for i in range(len(me.vertices))])
    ob = bpy.data.objects.new("leaves", me)
    coll.objects.link(ob)
    return ob


def leaf_material(img):
    m = bpy.data.materials.new("rw_leaf")
    m.use_nodes = True
    try:
        m.surface_render_method = "DITHERED"
    except Exception:
        pass
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    b = nt.nodes.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = 0.75
    b.inputs["Specular IOR Level"].default_value = 0.2
    tx = nt.nodes.new("ShaderNodeTexImage")
    tx.image = img
    at = nt.nodes.new("ShaderNodeAttribute")
    at.attribute_name = "leaf"
    sp = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(at.outputs["Color"], sp.inputs[0])
    depth = rw.math_node(nt, "POWER", sp.outputs[0], 0.7)
    g = rw.ramp(nt, [(0.0, rw.hexlin("#93CF4A")), (0.4, rw.P["canopy_lit"]), (0.8, rw.hexlin("#3A7A3A")),
                     (1.0, rw.hexlin("#2A5E36"))])
    nt.links.new(depth, g.inputs[0])
    hs = nt.nodes.new("ShaderNodeHueSaturation")
    hshift = rw.math_node(nt, "MULTIPLY_ADD", sp.outputs[1], 0.08)
    hshift.node.inputs[2].default_value = 0.46
    nt.links.new(hshift, hs.inputs["Hue"])
    nt.links.new(g.outputs[0], hs.inputs["Color"])
    val = rw.mix_rgb(nt, 1.0, hs.outputs[0], tx.outputs["Color"], "MULTIPLY")
    val = rw.mix_rgb(nt, 1.0, val, (1.35, 1.35, 1.35, 1.0), "MULTIPLY")
    nt.links.new(val, b.inputs["Base Color"])
    nt.links.new(tx.outputs["Alpha"], b.inputs["Alpha"])
    # light through the leaves (the back-lit glow; AV Gen will approximate it, see 04 section 4)
    tl = nt.nodes.new("ShaderNodeBsdfTranslucent")
    nt.links.new(val, tl.inputs["Color"])
    mix = nt.nodes.new("ShaderNodeMixShader")
    mix.inputs[0].default_value = 0.22
    nt.links.new(b.outputs[0], mix.inputs[1])
    nt.links.new(tl.outputs[0], mix.inputs[2])
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix2 = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(tx.outputs["Alpha"], mix2.inputs[0])
    nt.links.new(tr.outputs[0], mix2.inputs[1])
    nt.links.new(mix.outputs[0], mix2.inputs[2])
    nt.links.new(mix2.outputs[0], out.inputs[0])
    return m


def bark_material():
    m = bpy.data.materials.new("rw_bark")
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Roughness"].default_value = 0.9
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (3.0, 3.0, 0.4)  # vertical streaks
    nt.links.new(tc.outputs["Object"], mp.inputs[0])
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 2.0
    nz.inputs["Detail"].default_value = 3.0
    nt.links.new(mp.outputs[0], nz.inputs["Vector"])
    c = rw.ramp(nt, [(0.35, rw.hexlin("#3F2B22")), (0.55, rw.P["bark"]), (0.75, rw.hexlin("#8A6A52"))])
    nt.links.new(nz.outputs["Fac"], c.inputs[0])
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    sp = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(geo.outputs["Normal"], sp.inputs[0])
    moss = rw.math_node(nt, "MULTIPLY_ADD", sp.outputs[2], 2.5)
    moss.node.inputs[2].default_value = -1.6
    moss = rw.math_node(nt, "MAXIMUM", moss, 0.0)
    moss = rw.math_node(nt, "MINIMUM", moss, 0.8)
    col = rw.mix_rgb(nt, moss, c.outputs[0], rw.P["olive"])
    nt.links.new(col, b.inputs["Base Color"])
    return m


def build_tree(out, rng):
    coll = bpy.data.collections.new("great_tree")
    bpy.context.scene.collection.children.link(coll)
    parts = build_skeleton(coll, rng)
    # curves -> one mesh
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.join()
    wood = bpy.context.view_layer.objects.active
    wood.name = "wood"
    bpy.ops.object.shade_smooth()
    wood.data.materials.append(bark_material())
    img = leaf_cluster_image(os.path.join(out, "leaf_cluster.png"))
    leaves = leaf_cards(coll, rng)
    leaves.data.materials.append(leaf_material(img))
    return wood, leaves


def export_engine(wood, leaves, path):
    """The AV Gen export: glTF-plain materials (the colour gradient moves to an AV Gen material program, since
    the engine reads no vertex colours): bark as a flat colour, leaves as the cluster card with a MASK alpha
    (the Round node is how Blender's glTF exporter is told 'cutout') and the hull normals kept."""
    w2, l2 = wood.copy(), leaves.copy()
    w2.data, l2.data = wood.data.copy(), leaves.data.copy()
    for o in (w2, l2):
        bpy.context.scene.collection.objects.link(o)
        o.data.materials.clear()
    # the wood at ~2,500 triangles: hundreds of trees draw it into three shadow cascades (Stage 4 profile)
    ntri = sum(len(pp.vertices) - 2 for pp in w2.data.polygons)
    if ntri > 2500:
        dm = w2.modifiers.new("dec", "DECIMATE")
        dm.ratio = 2500.0 / ntri
        bpy.context.view_layer.objects.active = w2
        bpy.ops.object.modifier_apply(modifier="dec")
    mb = bpy.data.materials.new("great_tree_bark")
    mb.use_nodes = True
    mb.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = rw.P["bark"]
    mb.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
    w2.data.materials.append(mb)
    ml = bpy.data.materials.new("great_tree_leaf")
    ml.use_nodes = True
    nt = ml.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Roughness"].default_value = 0.75
    tx = nt.nodes.new("ShaderNodeTexImage")
    tx.image = bpy.data.images["leaf_cluster"]
    nt.links.new(tx.outputs["Color"], b.inputs["Base Color"])
    rnd = nt.nodes.new("ShaderNodeMath")
    rnd.operation = "ROUND"
    nt.links.new(tx.outputs["Alpha"], rnd.inputs[0])
    nt.links.new(rnd.outputs[0], b.inputs["Alpha"])
    ml.use_backface_culling = False
    l2.data.materials.append(ml)
    rw.export_glb([w2, l2], path)
    rw.export_glb([w2], path.replace(".glb", "_wood.glb"))
    rw.export_glb([l2], path.replace(".glb", "_leaves.glb"))
    for o in (w2, l2):
        bpy.data.objects.remove(o)


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0] if args else "build/rw/stage2/tree")
    W = int(args[1]) if len(args) > 1 else 960
    H = int(args[2]) if len(args) > 2 else 720
    S = int(args[3]) if len(args) > 3 else 64
    os.makedirs(out, exist_ok=True)
    sc = rw.reset()
    rng = random.Random(17)
    wood, leaves = build_tree(out, rng)
    export_engine(wood, leaves, os.path.join(out, "great_tree.glb"))
    sun = Vector((0.55, -0.6, 0.58)).normalized()
    rw.sky_world(sc, sun, strength=0.95)
    rw.sun_light(sun, strength=5.0)
    fill = rw.sun_light(Vector((-0.3, 0.4, -0.85)), strength=1.2, angle_deg=40, color=rw.P["cloud_bounce"])
    fill.data.use_shadow = False
    bpy.ops.mesh.primitive_circle_add(vertices=96, radius=40, fill_type="NGON", location=(0, 0, 0))
    ground = bpy.context.object
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = rw.P["meadow"]
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.95
    ground.data.materials.append(gm)
    rw.setup_render(sc, W, H, samples=S)
    cam = rw.camera(lens=30)
    for k, ang in enumerate((200, 290, 20, 110)):
        a = math.radians(ang)
        rw.aim(cam, (math.cos(a) * 58, math.sin(a) * 58, 8.0), (0, 0, 13.0))
        rw.render(sc, os.path.join(out, "tree_turn_%d.png" % k))
    # back-lit: camera looking toward the sun through the canopy
    sxy = Vector((sun.x, sun.y, 0)).normalized()
    rw.aim(cam, -sxy * 42 + Vector((0, 0, 5)), (0, 0, 14))
    rw.render(sc, os.path.join(out, "tree_backlit.png"))
    # the silhouette test: flat black tree on white, at 200 m with a 30 mm lens, 960 px wide
    ground.hide_render = True
    sil = bpy.data.materials.new("sil")
    sil.use_nodes = True
    nt = sil.node_tree
    nt.nodes.clear()
    o = nt.nodes.new("ShaderNodeOutputMaterial")
    e = nt.nodes.new("ShaderNodeEmission")
    e.inputs["Color"].default_value = (0, 0, 0, 1)
    tx = nt.nodes.new("ShaderNodeTexImage")
    tx.image = bpy.data.images["leaf_cluster"]
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    mx = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(tx.outputs["Alpha"], mx.inputs[0])
    nt.links.new(tr.outputs[0], mx.inputs[1])
    nt.links.new(e.outputs[0], mx.inputs[2])
    nt.links.new(mx.outputs[0], o.inputs[0])
    for ob in (wood, leaves):
        ob.data.materials.clear()
        ob.data.materials.append(sil)
    sc.world.node_tree.nodes["Background"].inputs[1].default_value = 0.0
    w2 = bpy.data.worlds.new("white")
    w2.use_nodes = True
    w2.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1)
    sc.world = w2
    sc.view_settings.view_transform = "Standard"
    rw.aim(cam, (-200, 0, 11), (0, 0, 11))
    rw.render(sc, os.path.join(out, "tree_silhouette_200m.png"))
    rw.save_blend(os.path.join(out, "great_tree.blend"))
    print("LEAF CARDS", len(leaves.data.polygons), "WOOD TRIS", sum(len(p.vertices) - 2 for p in wood.data.polygons))
    print("done")


if __name__ == "__main__":
    main()
