"""Builds, exports and look-devs the Bioluminescent Valley hero organisms.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P build.py -- \
        --assets veilcap [more...] --out <meshes/hero dir> [--copy <second dir>] [--render <review dir>] [--lod0-only]

Per asset it writes hero_<name>_lod0.glb (the macro LOD), hero_<name>_lod1.glb (scene scale, half-res textures),
hero_<name>.emit.json (luminous knots / tips in glTF space, Y up) and textures/<material>_{base,emissive,orm,normal}.png
(the same images embedded in the GLB, for inspection). With --render it writes look-dev views (EEVEE) for sheet.py.
"""
from __future__ import annotations

import argparse
import importlib
import json
import math
import os
import shutil
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import lib  # noqa: E402

ASSETS = {
    "veilcap": "veilcap",
    "lattice": "lattice",
    "filigree": "filigree",
    "chalice": "chalice",
    "lanternberry": "lanternberry",
    "veilfern": "frond",
    "bellspire": "spire",
    "glowmat": "mat",
    "reliquary": "reliquary",
}


def clear_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.lights, bpy.data.cameras):
        for x in list(coll):
            coll.remove(x)


def build_one(name, out, lod, tex_dir):
    mod = importlib.import_module(ASSETS[name])
    A = mod.make(lod=lod)
    mats = []
    for s in A.slots:
        imgs = lib.build_textures(bpy, s, tex_dir)
        mats.append(lib.build_material(bpy, s, imgs))
    ob = lib.build_object(bpy, A, mats, name=f"hero_{name}_lod{lod}")
    path = os.path.join(out, f"hero_{name}_lod{lod}.glb")
    lib.export_glb(bpy, ob, path, A.slots)
    lo, hi = A.bounds()
    info = {"glb": os.path.basename(path), "triangles": int(A.triangles()),
            "slots": {s.full_name: {"triangles": int(s.triangles()), "mode": s.mode, "doubleSided": s.double_sided,
                                    "texture": [int(s.W), int(s.H)], "emissiveStrength": s.emissive_strength}
                      for s in A.slots},
            "bounds_zup_m": [[round(float(x), 4) for x in lo], [round(float(x), 4) for x in hi]],
            "bytes": os.path.getsize(path)}
    if lod == 0 and A.emitters:
        lib.write_emitters(os.path.join(out, f"hero_{name}.emit.json"), A.emitters)
        info["emitters"] = len(A.emitters)
    return ob, A, info


# ---- look-dev ----------------------------------------------------------------------------------------------------


def setup_lookdev(res=900):
    sc = bpy.context.scene
    for eng in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            sc.render.engine = eng
            break
        except TypeError:
            continue
    sc.render.resolution_x = res
    sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    try:
        sc.eevee.taa_render_samples = 64
        sc.eevee.use_raytracing = True
    except AttributeError:
        pass
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "AgX - Punchy"
    sc.view_settings.exposure = 0.0
    world = bpy.data.worlds.get("lookdev") or bpy.data.worlds.new("lookdev")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (0.0025, 0.0035, 0.010, 1)
    bg.inputs["Strength"].default_value = 1.0
    sc.world = world

    # ground: dark, slightly glossy humus
    me = bpy.data.meshes.new("ground")
    s = 20.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new("ground", me)
    sc.collection.objects.link(g)
    m = bpy.data.materials.new("ground")
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.008, 0.012, 0.016, 1)
    b.inputs["Roughness"].default_value = 0.8
    me.materials.append(m)

    def light(name, kind, loc, rot, energy, color, size):
        ld = bpy.data.lights.new(name, kind)
        ld.energy = energy
        ld.color = color
        if kind == "AREA":
            ld.size = size
        o = bpy.data.objects.new(name, ld)
        o.location = loc
        o.rotation_euler = rot
        sc.collection.objects.link(o)
        return o

    # cool back/rim light high behind, a dim violet fill, a faint moon-blue top
    light("back", "AREA", (-0.55, 0.65, 0.65), (math.radians(-55), math.radians(-35), math.radians(0)), 6.0,
          (0.55, 0.68, 1.0), 0.35)
    light("fill", "AREA", (0.7, -0.5, 0.25), (math.radians(75), 0, math.radians(55)), 0.5, (0.45, 0.4, 1.0), 0.6)
    light("top", "AREA", (0.0, 0.0, 1.2), (0, 0, 0), 0.4, (0.5, 0.6, 1.0), 1.0)


def look_at(cam, target):
    from mathutils import Vector
    d = Vector(target) - cam.location
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def render_views(name, ob, out_dir, A):
    sc = bpy.context.scene
    lo, hi = A.bounds()
    h = float(hi[2])
    cx, cy = float((lo[0] + hi[0]) / 2), float((lo[1] + hi[1]) / 2)
    cd = bpy.data.cameras.new("cam")
    cd.lens = 50
    cd.clip_start = 0.01
    cam = bpy.data.objects.new("cam", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    R = max(h, float(hi[0] - lo[0]), float(hi[1] - lo[1]))
    views = {
        "hero": ((cx + 1.7 * R * math.cos(-0.5), cy + 1.7 * R * math.sin(-0.5), 0.55 * h), (cx, cy, 0.52 * h)),
        "side": ((cx, cy - 2.0 * R, 0.5 * h), (cx, cy, 0.5 * h)),
        "under": ((cx + 0.55 * R, cy - 0.6 * R, 0.25 * h), (cx, cy, 0.78 * h)),
        "top": ((cx + 0.9 * R, cy - 1.0 * R, 1.75 * h), (cx, cy, 0.62 * h)),
    }
    os.makedirs(out_dir, exist_ok=True)
    paths = {}
    for v, (loc, tgt) in views.items():
        cam.location = loc
        look_at(cam, tgt)
        cd.lens = 50 if v != "under" else 28
        p = os.path.join(out_dir, f"{name}_{v}.png")
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        paths[v] = p
    return paths


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", nargs="+", default=list(ASSETS))
    ap.add_argument("--out", required=True)
    ap.add_argument("--copy", default=None)
    ap.add_argument("--render", default=None)
    ap.add_argument("--lod0-only", action="store_true")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    tex_dir = os.path.join(a.out, "textures")
    report = {}
    for name in a.assets:
        clear_scene()
        info = {}
        if not a.lod0_only:
            ob1, _, info[1] = build_one(name, a.out, 1, tex_dir)
            bpy.data.objects.remove(ob1, do_unlink=True)
        ob0, A0, info[0] = build_one(name, a.out, 0, tex_dir)
        report[name] = info
        print("HERO", name, json.dumps(info))
        if a.render:
            setup_lookdev()
            render_views(name, ob0, a.render, A0)
    rp = os.path.join(a.out, "report.json")
    old = json.load(open(rp)) if os.path.exists(rp) else {}
    old.update(report)
    json.dump(old, open(rp, "w"), indent=1)
    if a.copy:
        os.makedirs(os.path.join(a.copy, "textures"), exist_ok=True)
        for root, _, files in os.walk(a.out):
            for f in files:
                src = os.path.join(root, f)
                dst = os.path.join(a.copy, os.path.relpath(src, a.out))
                shutil.copy2(src, dst)


main()
