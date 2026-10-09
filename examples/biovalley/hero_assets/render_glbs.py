"""Hero look-dev renders of exported GLBs (for colourway contact sheets).

    Blender -b --factory-startup -P render_glbs.py -- <out dir> <view>[:clay] <glb> [<glb> ...]

With ":clay", every material becomes neutral grey clay with no emission (alpha cut-outs and normal maps kept) under a
brighter neutral rig: the spec's neutral-material diagnostic (05-hero-asset-breakthrough.md section 3.3).

Each GLB is imported as AV Gen would see it (its own materials and textures) and rendered under build.py's
look-dev rig from the named view (hero, side, under or top). Writes <out dir>/<glb stem>_<view>.png.
"""
import os
import sys

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402


class Bounds:
    def __init__(self, objs):
        from mathutils import Vector
        pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
        P = np.array([[p.x, p.y, p.z] for p in pts])
        self.lo, self.hi = P.min(0), P.max(0)

    def bounds(self):
        return self.lo, self.hi


def clay():
    for m in bpy.data.materials:
        if not m.use_nodes:
            continue
        b = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if b is None:
            continue
        for l in list(b.inputs["Base Color"].links):
            m.node_tree.links.remove(l)
        b.inputs["Base Color"].default_value = (0.42, 0.42, 0.42, 1)
        for l in list(b.inputs["Roughness"].links):
            m.node_tree.links.remove(l)
        b.inputs["Roughness"].default_value = 0.55
        b.inputs["Metallic"].default_value = 0.0
        b.inputs["Emission Strength"].default_value = 0.0


def clay_rig():
    sc = bpy.context.scene
    sc.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.035, 0.035, 0.04, 1)
    for o in sc.objects:
        if o.type == "LIGHT":
            o.data.energy *= 3.0
            o.data.color = (1, 1, 1)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    out, view, glbs = argv[0], argv[1], argv[2:]
    view, _, mode = view.partition(":")
    os.makedirs(out, exist_ok=True)
    for g in glbs:
        build.clear_scene()
        for w in list(bpy.data.worlds):
            bpy.data.worlds.remove(w)
        bpy.ops.import_scene.gltf(filepath=g)
        objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
        stem = os.path.splitext(os.path.basename(g))[0]
        name = stem.replace("hero_", "").rsplit("_lod", 1)[0]
        build.setup_lookdev(700)
        if mode == "clay":
            clay()
        b = Bounds(objs)
        build.render_views(name.split("_")[0], None, out, b, only=view, stem=stem + ("_clay" if mode else ""),
                           post_rig=clay_rig if mode == "clay" else None)


if __name__ == "__main__":
    main()
