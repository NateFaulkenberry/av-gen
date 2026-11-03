"""Hero-asset Stop H1 (brief 49, Phase 2.2): the fur proof. Three fur representations on ONE Sasquatch (variant A,
same rig, same idle), exported side by side so AV Gen renders them under the same fire and moon.

    Blender -b --factory-startup --python examples/strange-attractor/hero/fur_proof.py -- \
        assets/strange-attractor/build/sasquatch-a.glb assets/strange-attractor/hero/fur-proof

Writes into the out dir (gitignored, never committed):
- fur-p1-cards.glb   denser hair cards (60k), a 1024 px strand atlas with dark roots and per-strand tone, the cards'
                     normals bent to the body's (so a card lights like the fur volume, not like a flat ribbon), and
                     the body's skin texture re-tinted to the fur colour (so gaps read as under-fur, not pale skin);
- fur-p2-shells.glb  16 skinned shells over the body (offset along the normal, drooping with gravity), alpha-tested
                     against one strand-height texture with a rising cutoff per shell (the classic shell-fur), no cards;
- fur-p3-hybrid.glb  10 shells for the body's fur volume plus 18k longer silhouette cards where a Sasquatch's hair is
                     long (head, shoulders, back, forearms) -- the brief's option 5.
Each keeps the rig and the "idle" clip of the input. Every fur material is written alphaMode MASK (the original
build left the silverback's grey fur BLEND).
"""
import bisect
import json
import math
import os
import random
import struct
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

if __name__ == "__main__":
    src, outdir = sys.argv[sys.argv.index("--") + 1:][:2]
    os.makedirs(outdir, exist_ok=True)
FUR = np.array([0.045, 0.028, 0.017])     # variant A's dark brown (build_sasquatch.py)


def load(path=None):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path or src)
    rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    body = next(o for o in bpy.data.objects if o.type == "MESH" and "fur" not in o.name and o.parent == rig)
    for o in list(bpy.data.objects):
        if o.type == "MESH" and "fur" in o.name:
            bpy.data.objects.remove(o)
    return rig, body


def retint_body(body, gain=2.6, colour=None):
    colour = FUR if colour is None else colour
    """The scan's texture is pale skin with baked light; keep its luminance (the anatomy's shading cues), recolour it
    to the fur's brown, a little darker in the creases."""
    mat = body.data.materials[0]
    tex = next(n for n in mat.node_tree.nodes if n.type == "TEX_IMAGE")
    im = tex.image
    w, h = im.size
    px = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(px)
    px = px.reshape(-1, 4)
    lum = px[:, 0] * 0.3 + px[:, 1] * 0.59 + px[:, 2] * 0.11
    lum = np.clip(lum / max(1e-4, float(np.percentile(lum, 70))), 0, 1.6) ** 1.3
    new = bpy.data.images.new("sasquatch_skin_fur", w, h)
    out = np.empty_like(px)
    for c in range(3):
        out[:, c] = np.clip(colour[c] * gain * lum, 0, 1)
    out[:, 3] = 1.0
    new.pixels.foreach_set(out.ravel())
    new.pack()
    tex.image = new


def strand_atlas(name, size=1024, cols=4, strands=150, seed=3, colour=FUR, lock=False):
    """Four columns of strand clumps: per-strand tone, width tapering to the tip, roots at 35% brightness."""
    rng = np.random.default_rng(seed)
    rgba = np.zeros((size, size, 4), dtype=np.float32)
    rgba[..., :3] = np.asarray(colour) * 2.2   # colour bleed: transparent texels carry the fur colour, so mips and
    #                                         filtering never pull black into the strands (the "dark flakes")
    cw = size // cols
    ys = np.arange(size)
    for c in range(cols):
        for _ in range(strands):
            x0 = c * cw + rng.uniform(0.08, 0.92) * cw
            top = int(rng.uniform(0.7, 1.0) * size)
            bend = rng.uniform(-0.12, 0.12) * cw
            tone = rng.uniform(0.6, 1.6)
            width = (rng.uniform(0.8, 1.7) if lock else rng.uniform(1.2, 3.2)) * size / 1024
            y0 = int(rng.uniform(0.0, 0.35) * size) if lock else 0
            yy = ys[y0:top]
            t = yy / size
            xc = x0 + bend * t * t
            wv = width * (1.0 - (yy - y0) / max(1, top - y0)) + 0.5
            root = (0.8 + 0.2 * np.clip(t / 0.6, 0, 1)) if lock else 0.35 + 0.65 * np.clip(t / 0.6, 0, 1)
            for dx in range(-5, 6):
                xi = np.clip((xc + dx).astype(int), c * cw, c * cw + cw - 1)
                a = np.clip(1.0 - np.abs(xi + 0.5 - xc) / wv, 0, 1)
                if lock:   # soft roots: the lock fades in, so no solid wedge at the card's base
                    a = a * np.clip((yy - y0) / (0.12 * size), 0, 1)
                cur = rgba[yy, xi, 3]
                m = a > cur
                for k in range(3):
                    rgba[yy[m], xi[m], k] = np.clip(colour[k] * tone * (3.0 if lock else 2.4) * root[m], 0, 1)
                rgba[yy[m], xi[m], 3] = a[m]
    im = bpy.data.images.new(name, size, size, alpha=True)
    im.pixels.foreach_set(rgba.ravel())
    im.pack()
    return im


def shell_texture(name, size=1024, count=26000, seed=5, colour=FUR, radius=1.9):
    """One tile: strand roots as small discs; alpha = the strand's height (0.35-1), so a shell at height t keeps a
    pixel where alpha > t (its alphaCutoff); the colour brightens toward the tips."""
    rng = np.random.default_rng(seed)
    rgba = np.zeros((size, size, 4), dtype=np.float32)
    rgba[..., :3] = np.asarray(colour) * 2.2   # colour bleed: transparent texels carry the fur colour, so mips and
    #                                         filtering never pull black into the strands (the "dark flakes")
    xs, ys = rng.uniform(0, size, count), rng.uniform(0, size, count)
    hs = rng.uniform(0.35, 1.0, count)
    tones = rng.uniform(0.6, 1.5, count)
    r = radius * size / 1024
    ri = int(math.ceil(r))
    for dx in range(-ri, ri + 1):
        for dy in range(-ri, ri + 1):
            d = math.hypot(dx, dy)
            if d > r:
                continue
            xi = (xs + dx).astype(int) % size
            yi = (ys + dy).astype(int) % size
            h = hs * (1.0 - 0.35 * d / r)        # thinner toward the edge: tapered strands
            m = h > rgba[yi, xi, 3]
            rgba[yi[m], xi[m], 3] = h[m]
            for k in range(3):
                rgba[yi[m], xi[m], k] = np.clip(colour[k] * tones[m] * 2.4, 0, 1)
    im = bpy.data.images.new(name, size, size, alpha=True)
    im.pixels.foreach_set(rgba.ravel())
    im.pack()
    return im


def fur_material(name, img, factor=1.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = img
    nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    nt.links.new(t.outputs["Alpha"], b.inputs["Alpha"])
    b.inputs["Roughness"].default_value = 0.85
    if factor != 1.0:
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        mix.inputs["B"].default_value = (factor, factor, factor, 1)
        nt.links.new(t.outputs["Color"], mix.inputs["A"])
        nt.links.new(mix.outputs["Result"], b.inputs["Base Color"])
    try:
        m.blend_method = "CLIP"
    except Exception:
        pass
    return m


def make_shells(rig, body, n, length, uv_scale=7.0, clump=False, colour=None, face=0.2, bare=None):
    colour = FUR if colour is None else colour
    img = shell_texture("sasquatch_shell_clump", count=5200, radius=6.5, colour=colour) if clump else shell_texture("sasquatch_shell", colour=colour)
    me = body.data
    H = max(v.co.z for v in me.vertices)
    bm = bmesh.new()
    for k in range(1, n + 1):
        t = k / n
        part = bmesh.new()
        part.from_mesh(me)
        uvl = part.loops.layers.uv.active
        down = Vector((0, 0, -1))
        for v in part.verts:
            nv = v.normal
            zr = v.co.z / H
            L = length * (0.55 + 0.8 * math.exp(-((zr - 0.72) / 0.18) ** 2))
            if zr > 0.86 and nv.y < -0.5:      # the face: short
                L *= face
            if zr < 0.04:                      # soles
                L *= 0.1
            if bare is not None and bare(v.co / H, nv):   # a contact patch (a seat): no fur volume
                L *= 0.05
            g = down - nv * down.dot(nv)
            v.co = v.co + nv * L * t + g * L * 0.6 * t * t
        for f in part.faces:
            f.material_index = k - 1
            for lp in f.loops:
                lp[uvl].uv = lp[uvl].uv * uv_scale
        tmp = bpy.data.meshes.new("tmp")
        part.to_mesh(tmp)
        part.free()
        bm.from_mesh(tmp)
        bpy.data.meshes.remove(tmp)
    sme = bpy.data.meshes.new("sasquatch_shells")
    bm.to_mesh(sme)
    bm.free()
    obj = bpy.data.objects.new("sasquatch_shells", sme)
    bpy.context.scene.collection.objects.link(obj)
    cut = {}
    for k in range(1, n + 1):
        t = k / n
        m = fur_material(f"sasquatch_shell_{k:02d}", img, factor=0.45 + 0.55 * t)   # inner shells darker: occlusion
        cut[m.name] = 0.3 + 0.68 * t
        sme.materials.append(m)
    # the same vertex order as the body, n times: copy the weights
    nv = len(me.vertices)
    for g in body.vertex_groups:
        obj.vertex_groups.new(name=g.name)
    for v in me.vertices:
        for ge in v.groups:
            name = body.vertex_groups[ge.group].name
            for k in range(n):
                obj.vertex_groups[name].add([v.index + k * nv], ge.weight, "REPLACE")
    obj.parent = rig
    obj.modifiers.new("Armature", "ARMATURE").object = rig
    return obj, cut


def make_cards(rig, body, N, lenk, region=None, seed=29, name="sasquatch_cards", width=(0.28, 0.4), strands=150, lock=False,
               colour=None, root_weights=False, skip=None):
    rng = random.Random(seed)
    colour = FUR if colour is None else colour
    img = strand_atlas(name + "_atlas", strands=strands, lock=lock, colour=colour)
    roots = []
    me = body.data
    H = max(v.co.z for v in me.vertices)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    faces = [f for f in bm.faces if region is None or region(f.calc_center_median() / H, f.normal)]
    cum, acc = [], 0.0
    for f in faces:
        acc += f.calc_area()
        cum.append(acc)
    cards = bmesh.new()
    uvl = cards.loops.layers.uv.new()
    down = Vector((0, 0, -1))
    vnorm = []
    for _ in range(N):
        f = faces[min(len(cum) - 1, bisect.bisect_left(cum, rng.uniform(0, acc)))]
        a, b = rng.random(), rng.random()
        if a + b > 1:
            a, b = 1 - a, 1 - b
        vs = [l.vert.co for l in f.loops]
        pt = vs[0] + (vs[1] - vs[0]) * a + (vs[2] - vs[0]) * b
        n = f.normal.normalized()
        zr = pt.z / H
        if skip is not None and skip(pt / H, n):          # a bare region (the face, palms): no card
            continue
        L = lenk * H * (0.6 + 0.8 * math.exp(-((zr - 0.72) / 0.16) ** 2)) * rng.uniform(0.7, 1.3)
        if zr > 0.86 and n.y < -0.5:
            L *= 0.25
        roots.append([(l.vert.index, w_) for l, w_ in zip(f.loops, (1 - a - b, a, b))])
        t = down - n * down.dot(n)
        if t.length < 1e-3:
            t = Vector((0, 1, 0)) - n * n.y
        t.normalize()
        sd = n.cross(t).normalized()
        w = L * rng.uniform(*width)
        root, mid, tip = pt - n * 0.003, pt + n * L * 0.22 + t * L * 0.48, pt + n * L * 0.3 + t * L * 0.97
        ring = [cards.verts.new(p) for p in (root - sd * w, root + sd * w, mid - sd * w * 0.85, mid + sd * w * 0.85,
                                             tip - sd * w * 0.55, tip + sd * w * 0.55)]
        # the card's shading normal: the body's, tipped a little along the strand (a fur volume, not a ribbon)
        sn = (n * 0.85 + t * 0.15).normalized()
        vnorm.extend([sn] * 6)
        u0 = rng.choice((0.0, 0.25, 0.5, 0.75))
        for (i0, i1, i2, i3), (v0, v1) in (((0, 1, 3, 2), (0.0, 0.5)), ((2, 3, 5, 4), (0.5, 1.0))):
            face = cards.faces.new((ring[i0], ring[i1], ring[i3], ring[i2]))
            for loop, (uu, vv) in zip(face.loops, ((u0, v0), (u0 + 0.25, v0), (u0 + 0.25, v1), (u0, v1))):
                loop[uvl].uv = (uu, vv)
        # the back side as its own single-sided faces with the SAME shading normal: a glTF doubleSided card's back
        # face gets its normal flipped (into the body) and shades black -- the proof's "dark flakes"
        back = [cards.verts.new(v.co) for v in ring]
        vnorm.extend([sn] * 6)
        for (i0, i1, i2, i3), (v0, v1) in (((0, 1, 3, 2), (0.0, 0.5)), ((2, 3, 5, 4), (0.5, 1.0))):
            face = cards.faces.new((back[i0], back[i2], back[i3], back[i1]))
            for loop, (uu, vv) in zip(face.loops, ((u0, v0), (u0, v1), (u0 + 0.25, v1), (u0 + 0.25, v0))):
                loop[uvl].uv = (uu, vv)
    cme = bpy.data.meshes.new(name)
    cards.to_mesh(cme)
    cme.normals_split_custom_set_from_vertices([tuple(v) for v in vnorm])
    obj = bpy.data.objects.new(name, cme)
    bpy.context.scene.collection.objects.link(obj)
    m = fur_material(name, img)
    cme.materials.append(m)
    for g in body.vertex_groups:
        obj.vertex_groups.new(name=g.name)
    if root_weights:
        # every vertex of a card takes the skin weights at its ROOT (barycentric over the root face), so a card that
        # hangs from the forearm never takes the hip's weights at its tip (the proof's stretched cards)
        per = 12
        for ci, root_ in enumerate(roots):
            acc_w = {}
            for vi, bw in root_:
                for ge in me.vertices[vi].groups:
                    acc_w[ge.group] = acc_w.get(ge.group, 0.0) + ge.weight * bw
            tot = sum(acc_w.values()) or 1.0
            idx = list(range(ci * per, ci * per + per))
            for gi, wv_ in acc_w.items():
                if wv_ / tot > 0.01:
                    obj.vertex_groups[body.vertex_groups[gi].name].add(idx, wv_ / tot, "REPLACE")
        obj.parent = rig
        obj.modifiers.new("Armature", "ARMATURE").object = rig
        return obj, {m.name: 0.4}
    dt = obj.modifiers.new("weights", "DATA_TRANSFER")
    dt.object = body
    dt.use_vert_data = True
    dt.data_types_verts = {"VGROUP_WEIGHTS"}
    dt.vert_mapping = "POLYINTERP_NEAREST"
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier="weights")
    obj.parent = rig
    obj.modifiers.new("Armature", "ARMATURE").object = rig
    return obj, {m.name: 0.4}


def patch_mask(path, cutoffs):
    b = open(path, "rb").read()
    n = struct.unpack("<I", b[12:16])[0]
    js = json.loads(b[20:20 + n])
    rest = b[20 + n:]
    for m in js.get("materials", []):
        if m.get("name") in cutoffs:
            m["alphaMode"] = "MASK"
            m["alphaCutoff"] = cutoffs[m["name"]]
            m["doubleSided"] = False   # cards carry explicit back faces; shells face outward
    enc = json.dumps(js, separators=(",", ":")).encode()
    enc += b" " * ((-len(enc)) % 4)
    open(path, "wb").write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(enc) + len(rest)) +
                          struct.pack("<II", len(enc), 0x4E4F534A) + enc + rest)


def export(path, cut):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=path, export_animations=True, export_animation_mode="ACTIONS",
                              export_normals=True)
    patch_mask(path, cut)
    tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in bpy.data.objects if o.type == "MESH")
    print("FURPROOF", path, "tris", tris)



def bake_fur(body, rig, size=2048, streak=(140.0, 140.0, 9.0), seed=0.0, grey=None):
    """The fur's look baked into the body's own maps (the brief's option 1): a 3-D streak noise in the rest pose's
    object space, stretched along gravity so it reads as hanging hair, gives a tangent-space normal map and an albedo
    (the anatomy's luminance kept, fur-coloured, the streaks and a large-scale mottling on top). The baked maps replace
    the body's material, so it ships as plain glTF (base colour + normal)."""
    rig.data.pose_position = "REST"
    bpy.context.view_layer.update()
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 4
    mat = body.data.materials[0]
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    base_tex = next(n for n in nt.nodes if n.type == "TEX_IMAGE")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = streak
    mp.inputs["Location"].default_value = (seed, seed, seed)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 1.0
    nz.inputs["Detail"].default_value = 6.0
    nz.inputs["Roughness"].default_value = 0.65
    nt.links.new(mp.outputs["Vector"], nz.inputs["Vector"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.8
    bump.inputs["Distance"].default_value = 0.004
    nt.links.new(nz.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    # albedo: the retinted anatomy x (0.55 + 0.9 * streak) x a broad mottle
    mot = nt.nodes.new("ShaderNodeTexNoise")
    mot.inputs["Scale"].default_value = 3.0
    nt.links.new(tc.outputs["Object"], mot.inputs["Vector"])
    m1 = nt.nodes.new("ShaderNodeMath"); m1.operation = "MULTIPLY_ADD"
    m1.inputs[1].default_value = 0.9; m1.inputs[2].default_value = 0.55
    nt.links.new(nz.outputs["Fac"], m1.inputs[0])
    m2 = nt.nodes.new("ShaderNodeMath"); m2.operation = "MULTIPLY_ADD"
    m2.inputs[1].default_value = 0.6; m2.inputs[2].default_value = 0.7
    nt.links.new(mot.outputs["Fac"], m2.inputs[0])
    m3 = nt.nodes.new("ShaderNodeMath"); m3.operation = "MULTIPLY"
    nt.links.new(m1.outputs[0], m3.inputs[0]); nt.links.new(m2.outputs[0], m3.inputs[1])
    mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = "RGBA"; mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    nt.links.new(base_tex.outputs["Color"], mix.inputs["A"])
    cmb = nt.nodes.new("ShaderNodeCombineColor")
    for k in range(3):
        nt.links.new(m3.outputs[0], cmb.inputs[k])
    nt.links.new(cmb.outputs[0], mix.inputs["B"])
    albedo = mix.outputs["Result"]
    if grey is not None:
        # a silverback: grey over the crown, shoulders and back (object z above 58% of the height, or facing back)
        Hh = max(v.co.z for v in body.data.vertices)
        sxyz = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(tc.outputs["Object"], sxyz.inputs[0])
        up = nt.nodes.new("ShaderNodeMapRange")
        up.inputs["From Min"].default_value, up.inputs["From Max"].default_value = 0.55 * Hh, 0.68 * Hh
        nt.links.new(sxyz.outputs["Z"], up.inputs["Value"])
        gn = nt.nodes.new("ShaderNodeTexNoise"); gn.inputs["Scale"].default_value = 9.0
        nt.links.new(tc.outputs["Object"], gn.inputs["Vector"])
        gm = nt.nodes.new("ShaderNodeMath"); gm.operation = "MULTIPLY"; gm.use_clamp = True
        nt.links.new(up.outputs[0], gm.inputs[0]); nt.links.new(gn.outputs["Fac"], gm.inputs[1])
        # not on the face (rest pose: above 82% of the height and facing forward, -y)
        sn = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(tc.outputs["Normal"], sn.inputs[0])
        fy = nt.nodes.new("ShaderNodeMapRange")
        fy.inputs["From Min"].default_value, fy.inputs["From Max"].default_value = -0.45, -0.15
        nt.links.new(sn.outputs["Y"], fy.inputs["Value"])                 # 0 facing forward, 1 otherwise
        fz = nt.nodes.new("ShaderNodeMapRange")
        fz.inputs["From Min"].default_value, fz.inputs["From Max"].default_value = 0.84 * Hh, 0.8 * Hh
        nt.links.new(sxyz.outputs["Z"], fz.inputs["Value"])                # 1 below the face, 0 at it
        nf = nt.nodes.new("ShaderNodeMath"); nf.operation = "MAXIMUM"
        nt.links.new(fy.outputs[0], nf.inputs[0]); nt.links.new(fz.outputs[0], nf.inputs[1])
        gm2 = nt.nodes.new("ShaderNodeMath"); gm2.operation = "MULTIPLY"; gm2.use_clamp = True
        gm3 = nt.nodes.new("ShaderNodeMath"); gm3.operation = "MULTIPLY"
        nt.links.new(gm.outputs[0], gm3.inputs[0]); nt.links.new(nf.outputs[0], gm3.inputs[1])
        nt.links.new(gm3.outputs[0], gm2.inputs[0]); gm2.inputs[1].default_value = 1.8
        gmix = nt.nodes.new("ShaderNodeMix"); gmix.data_type = "RGBA"
        nt.links.new(gm2.outputs[0], gmix.inputs["Factor"]); nt.links.new(albedo, gmix.inputs["A"])
        gmix.inputs["B"].default_value = tuple(min(1.0, c * 2.4) for c in grey) + (1,)
        albedo = gmix.outputs["Result"]
    nt.links.new(albedo, bsdf.inputs["Base Color"])
    out = {}
    for kind, cs in (("NORMAL", "Non-Color"), ("DIFFUSE", "sRGB")):
        im = bpy.data.images.new(f"sasquatch_baked_{kind.lower()}", size, size)
        im.colorspace_settings.name = cs
        tn = nt.nodes.new("ShaderNodeTexImage")
        tn.image = im
        for n in nt.nodes:
            n.select = False
        tn.select = True
        nt.nodes.active = tn
        for o in bpy.context.view_layer.objects:
            o.select_set(False)
        body.select_set(True)
        bpy.context.view_layer.objects.active = body
        if kind == "DIFFUSE":
            bpy.ops.object.bake(type="DIFFUSE", pass_filter={"COLOR"}, margin=8)
        else:
            bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", margin=8)
        im.pack()
        out[kind] = im
        nt.nodes.remove(tn)
    # the shipped material: baked albedo + baked normal
    for n in list(nt.nodes):
        if n.type not in ("BSDF_PRINCIPLED", "OUTPUT_MATERIAL"):
            nt.nodes.remove(n)
    tb = nt.nodes.new("ShaderNodeTexImage"); tb.image = out["DIFFUSE"]
    nt.links.new(tb.outputs["Color"], bsdf.inputs["Base Color"])
    tnn = nt.nodes.new("ShaderNodeTexImage"); tnn.image = out["NORMAL"]
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(tnn.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    bsdf.inputs["Roughness"].default_value = 0.8
    rig.data.pose_position = "POSE"
    return out


long_hair = lambda c, n: c.z > 0.55 or (abs(c.x) > 0.12 and 0.3 < c.z < 0.6)   # shoulders, back, head, forearms

if __name__ == "__main__":
    which = sys.argv[sys.argv.index("--") + 3].split(",") if len(sys.argv) > sys.argv.index("--") + 3 else ["p1", "p2", "p3", "p4", "p5", "p6"]

    if "p1" in which:
        rig, body = load()
        retint_body(body)
        _, cut = make_cards(rig, body, 60000, 0.042)
        export(os.path.join(outdir, "fur-p1-cards.glb"), cut)

    if "p2" in which:
        rig, body = load()
        retint_body(body)
        _, cut = make_shells(rig, body, 16, 0.05)
        export(os.path.join(outdir, "fur-p2-shells.glb"), cut)

    if "p3" in which:
        rig, body = load()
        retint_body(body)
        _, cut = make_shells(rig, body, 10, 0.04)
        _, cut2 = make_cards(rig, body, 18000, 0.055, region=long_hair)
        cut.update(cut2)
        export(os.path.join(outdir, "fur-p3-hybrid.glb"), cut)
    if "p4" in which:
        rig, body = load()
        retint_body(body)
        bake_fur(body, rig)
        export(os.path.join(outdir, "fur-p4-baked.glb"), {})
    if "p5" in which:
        rig, body = load()
        retint_body(body)
        bake_fur(body, rig)
        _, cut = make_shells(rig, body, 8, 0.05, uv_scale=2.5, clump=True)
        _, cut2 = make_cards(rig, body, 9000, 0.06, region=long_hair, width=(0.14, 0.22), strands=320)
        cut.update(cut2)
        export(os.path.join(outdir, "fur-p5-hybrid2.glb"), cut)
    if "p6" in which:
        # the H1 recommendation in miniature: baked maps + clump shells + sparse soft-rooted locks on the long-hair regions
        rig, body = load()
        retint_body(body)
        bake_fur(body, rig)
        _, cut = make_shells(rig, body, 8, 0.05, uv_scale=2.5, clump=True)
        _, cut2 = make_cards(rig, body, 9000, 0.07, region=long_hair, width=(0.12, 0.18), strands=70, lock=True)
        cut.update(cut2)
        export(os.path.join(outdir, "fur-p6-locks.glb"), cut)
