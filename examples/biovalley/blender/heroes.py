"""The Bioluminescent Valley's hero organisms, authored in Blender (the VFX art pass, Phase 2).

Run inside Blender (the MCP session, or `Blender --background --python heroes.py -- <out_dir> [names...]`). Each
organism is built from parametric base meshes refined with Blender's modifiers (subdivision, solidify, wireframe,
displacement), where Blender does better than the numpy generators: smooth organic surfaces, thickness, lace.
Every organism is exported as one GLB per ANATOMICAL PART (membrane, gills, stalk, veil, ...), so AV Gen gives each
part its own material and program, and emitter templates (`<part>.emit.json`) mark its luminous anatomy for the
ecosystem (ADR-1200).

UV convention on every part (as the valley's other species): v runs from the organism's centre or root (0) to its
rim or tip (1); u is around or across. The programs build core -> body -> rim light from it.

Generated GLBs are gitignored (examples/biovalley/meshes/); this script is the source.
"""
from __future__ import annotations

import json
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector, noise


# ---- scene helpers ------------------------------------------------------------------------------------------------

def clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)


def mesh_object(name, verts, faces, uvs=None):
    """An object from verts/faces; `uvs` per loop-vertex index (a list of (u, v) per vertex)."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.update()
    if uvs is not None:
        layer = me.uv_layers.new(name="UVMap")
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi = me.loops[li].vertex_index
                layer.data[li].uv = uvs[vi]
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    for p in me.polygons:
        p.use_smooth = True
    return ob


def add_mod(ob, kind, **kw):
    m = ob.modifiers.new(kind.lower(), kind)
    for k, v in kw.items():
        setattr(m, k, v)
    return m


def grid(rows, cols, wrap):
    F = []
    cc = cols if wrap else cols - 1
    for r in range(rows - 1):
        for c in range(cc):
            c1 = (c + 1) % cols
            F.append((r * cols + c, r * cols + c1, (r + 1) * cols + c1, (r + 1) * cols + c))
    return F


def tube(name, pts, radii, sides=10, v0=0.0, v1=1.0, u=0.0):
    """A swept ring along `pts` (Vectors), parallel-transport frames, capped tip. UV v along, u around."""
    n = len(pts)
    T = [(pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized() for i in range(n)]
    ref = Vector((1, 0, 0)) if abs(T[0].z) > 0.9 else Vector((0, 0, 1))
    N0 = T[0].cross(ref).normalized()
    Ns = [N0]
    for i in range(1, n):
        q = Ns[-1] - Ns[-1].dot(T[i]) * T[i]
        Ns.append(q.normalized() if q.length > 1e-6 else Ns[-1])
    V, UV = [], []
    for i in range(n):
        B = T[i].cross(Ns[i])
        for s in range(sides):
            a = 2 * math.pi * s / sides
            V.append(pts[i] + radii[i] * (math.cos(a) * Ns[i] + math.sin(a) * B))
            UV.append((u + s / sides * 0.0, v0 + (v1 - v0) * i / (n - 1)))
    F = grid(n, sides, True)
    tip = len(V)
    V.append(pts[-1] + T[-1] * radii[-1] * 0.5)
    UV.append((u, v1))
    base = (n - 1) * sides
    for s in range(sides):
        F.append((base + s, base + (s + 1) % sides, tip))
    return mesh_object(name, V, F, UV)


def join(objs, name):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = name
    return ob


def apply_all(ob):
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def export(ob, path):
    """Export one object as GLB (modifiers applied, +Y up, no materials worth keeping: AV Gen assigns them)."""
    for o in bpy.context.selected_objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=True,
                              export_yup=True, export_materials="NONE", export_texcoords=True,
                              export_normals=True, export_animations=False)
    tris = sum(len(p.vertices) - 2 for p in ob.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.polygons)
    return tris


def write_emit(path, pts):
    """Emitter template in glTF space (+Y up): Blender (x, y, z) -> glTF (x, z, -y)."""
    out = [[round(p[0], 4), round(p[2], 4), round(-p[1], 4), round(v, 3), round(u, 3), round(r, 4)]
           for (p, v, u, r) in pts]
    with open(path, "w") as f:
        json.dump({"points": out}, f, separators=(",", ":"))
    return len(out)


# ---- 1. THE VEILED LANTERN: the focal hero -------------------------------------------------------------------------
# A tall translucent bell (a domed, tiered cap with a scalloped, upturned frill), deep radial gills under it that
# carry the light, a slender fibrous stalk with a ring, and from beneath the cap a LACE VEIL -- a hanging net skirt
# (Phallus indusiatus), the silhouette nothing else in the valley has. The light lives in the gills and the veil's
# knots; the cap membrane glows only where it is thin (its rim) and where the gills shine through.

def veiled_lantern(out, name="lantern", seed=3, height=0.42, cap_r=0.11):
    rnd = random.Random(seed)
    parts = {}
    emit = {"gills": [], "veil": [], "cap": []}

    # stalk: a soft S-curve, swollen foot, fibrous (displaced along its length), a ring under the cap
    n = 28
    lean = Vector((rnd.uniform(-0.06, 0.06), rnd.uniform(-0.06, 0.06), 0))
    pts = [Vector((lean.x * (t * t) * height + 0.012 * math.sin(t * 5 + seed), lean.y * t * t * height,
                   t * height)) for t in [i / (n - 1) for i in range(n)]]
    rad = [height * 0.028 * (1.0 + 0.9 * math.exp(-t / 0.06)) * (1.0 - 0.3 * t) *
           (1.0 + 0.45 * math.exp(-((t - 0.86) / 0.018) ** 2)) for t in [i / (n - 1) for i in range(n)]]
    stalk = tube(f"{name}_stalk", pts, rad, sides=14)
    add_mod(stalk, "SUBSURF", levels=1, render_levels=1)
    tex = bpy.data.textures.new(f"{name}_fibres", "CLOUDS")
    tex.noise_scale = 0.004
    d = add_mod(stalk, "DISPLACE", strength=0.0015, texture=tex, texture_coords="LOCAL")
    parts["stalk"] = stalk
    top = pts[-1]

    # cap: two shells (top membrane, underside), a dome that flares into an upturned scalloped frill, tiered by a
    # step two-thirds out (the cap grew in two flushes)
    rings, segs = 26, 144
    k1, k2, k3 = rnd.uniform(0, 6), rnd.uniform(0, 6), rnd.uniform(0, 6)
    V, UV, Vb = [], [], []
    for i in range(rings):
        t = i / (rings - 1)
        for s in range(segs):
            a = 2 * math.pi * s / segs
            scallop = 0.06 * math.sin(9 * a + k1) + 0.035 * math.sin(17 * a + k2) + 0.02 * math.sin(29 * a + k3)
            frill = (t ** 3) * scallop
            R = cap_r * t * (1.0 + frill)
            tier = 0.18 * cap_r * (1.0 / (1.0 + math.exp(-(t - 0.62) / 0.03)))        # the step
            dome = cap_r * 0.95 * (1.0 - t ** 1.7) ** 0.9
            lift = cap_r * 0.22 * t ** 6 * (1.0 + 2.0 * scallop)                     # the upturned frill
            z = top.z + dome - tier + lift + cap_r * 0.08
            V.append(Vector((top.x + R * math.cos(a), top.y + R * math.sin(a), z)))
            thick = cap_r * 0.16 * (1.0 - t ** 1.3) + cap_r * 0.004
            Vb.append(Vector((top.x + R * 0.985 * math.cos(a), top.y + R * 0.985 * math.sin(a), z - thick)))
            UV.append((s / segs, t))
    F = grid(rings, segs, True)
    cap = mesh_object(f"{name}_cap", V, F, UV)
    under = mesh_object(f"{name}_under", Vb, [tuple(reversed(f)) for f in F], UV)
    for ob in (cap, under):
        add_mod(ob, "SUBSURF", levels=1, render_levels=1)
    parts["cap"] = cap
    parts["under"] = under
    for s in range(0, segs, 3):
        emit["cap"].append((V[(rings - 1) * segs + s], 1.0, s / segs, cap_r * 0.012))

    # gills: 72 thin radial fins hanging under the underside, deepest mid-way, wavy edges; they carry the light
    G = 72
    gV, gF, gUV = [], [], []
    for g in range(G):
        a = 2 * math.pi * (g + rnd.uniform(-0.2, 0.2)) / G
        short = g % 3 != 0                                                             # two in three are short
        i0 = 10 if short else 3
        base = len(gV)
        cols = rings - 1 - i0
        for j, i in enumerate(range(i0, rings - 1)):
            t = i / (rings - 1)
            s = int(round(a / (2 * math.pi) * segs)) % segs
            p = Vb[i * segs + s]
            depth = cap_r * 0.16 * math.sin(math.pi * (t - i0 / rings) / (1 - i0 / rings)) ** 0.8
            wav = 0.006 * math.sin(t * 40 + g)
            gV += [p + Vector((0, 0, 0.001)), p - Vector((0, 0, depth)) + Vector((wav * math.cos(a + 1.57),
                                                                                     wav * math.sin(a + 1.57), 0))]
            gUV += [(g / G, t), (g / G, t * 0.85)]
            if j % 3 == 1:
                emit["gills"].append((p - Vector((0, 0, depth * 0.55)), t, g / G, cap_r * 0.01))
        for j in range(cols - 1):
            q = base + 2 * j
            gF.append((q, q + 2, q + 3, q + 1))
    gills = mesh_object(f"{name}_gills", gV, gF, gUV)
    add_mod(gills, "SOLIDIFY", thickness=0.0006)
    parts["gills"] = gills

    # the veil: a hanging net skirt from under the cap down two-thirds of the stalk -- a cone of irregular cells
    # turned into lace by the wireframe modifier. Knots (vertices) carry tiny lights.
    vr, vs = 16, 40
    vV, vUV = [], []
    z0 = top.z - cap_r * 0.05
    for i in range(vr):
        t = i / (vr - 1)
        z = z0 - t * height * 0.62
        r = rad[-4] * 1.3 + (cap_r * 0.85 - rad[-4]) * (t ** 0.75)
        for s in range(vs):
            a = 2 * math.pi * (s + 0.5 * (i % 2)) / vs + 0.08 * noise.noise(Vector((i * 0.7, s * 0.7, seed)))
            jitter = 0.18 * r * noise.noise(Vector((s * 0.9, i * 0.9, seed + 3)))
            sway = 0.012 * math.sin(a * 3 + t * 4)
            vV.append(Vector((top.x + (r + jitter) * math.cos(a), top.y + (r + jitter) * math.sin(a),
                              z + sway * t)))
            vUV.append((s / vs, t))
            if i > 0 and rnd.random() < 0.45:
                emit["veil"].append((vV[-1], t, rnd.random(), cap_r * 0.008))
    vF = []
    for i in range(vr - 1):
        for s in range(vs):
            a_, b_ = i * vs + s, i * vs + (s + 1) % vs
            c_, d_ = (i + 1) * vs + (s + 1) % vs, (i + 1) * vs + s
            vF.append((a_, b_, c_))
            vF.append((a_, c_, d_))
    veil = mesh_object(f"{name}_veil", vV, vF, vUV)
    bpy.context.view_layer.objects.active = veil
    veil.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bm = bmesh.from_edit_mesh(veil.data)
    rng2 = random.Random(seed + 9)
    # dissolve some edges so the cells are irregular polygons (lace, not a grid)
    edges = [e for e in bm.edges if not e.is_boundary and rng2.random() < 0.42]
    bmesh.ops.dissolve_edges(bm, edges=edges, use_verts=False)
    bmesh.update_edit_mesh(veil.data)
    bpy.ops.object.mode_set(mode="OBJECT")
    add_mod(veil, "WIREFRAME", thickness=0.0016, use_even_offset=True, use_relative_offset=False,
            use_replace=True)
    add_mod(veil, "SUBSURF", levels=1, render_levels=1)
    parts["veil"] = veil

    tris = {}
    for part, ob in parts.items():
        apply_all(ob)
        tris[part] = export(ob, os.path.join(out, f"hero_{name}_{part}.glb"))
    for part, pts_ in emit.items():
        write_emit(os.path.join(out, f"hero_{name}_{part}.emit.json"), pts_)
    return tris


BUILDERS = {"lantern": veiled_lantern}


def main(out, names):
    os.makedirs(out, exist_ok=True)
    report = {}
    for nm in names:
        clear()
        report[nm] = BUILDERS[nm](out)
    return report


if __name__ == "__main__" and "--" in sys.argv:
    args = sys.argv[sys.argv.index("--") + 1:]
    print(json.dumps(main(args[0], args[1:] or list(BUILDERS))))
