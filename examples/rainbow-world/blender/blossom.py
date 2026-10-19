"""Rainbow World, Stage 5: the Blossom Puff -- the third tree (R4's pink weeper's cousin; used sparingly).

The Great Tree's skeleton and card system (trees.py) with a different canopy: five round, overlapping puffs that make
one rounded cloud of blossom on a shorter crown, so its silhouette is a ball, not the Great Tree's layered pads. The
blossom colour comes from an AV Gen material program (pink-lilac), like every other canopy's.

Outputs (args: <outdir> [w h samples]): blossom_turn_<k>.png, blossom{,_wood,_leaves}.glb, blossom_core.glb
"""
import math
import os
import random
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw_common as rw  # noqa: E402
import trees  # noqa: E402

PUFFS = [((0.0, 0.0, 17.5), (6.0, 6.0, 5.2), 0.05), ((4.6, 2.0, 14.8), (4.6, 4.4, 4.0), -0.05),
         ((-4.4, 2.8, 15.2), (4.4, 4.4, 4.0), 0.0), ((-1.5, -4.8, 14.6), (4.6, 4.2, 3.8), 0.08),
         ((3.0, -3.6, 18.6), (3.8, 3.6, 3.4), -0.08)]


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0] if args else "build/rw/stage5/blossom")
    W = int(args[1]) if len(args) > 1 else 640
    H = int(args[2]) if len(args) > 2 else 480
    S = int(args[3]) if len(args) > 3 else 24
    os.makedirs(out, exist_ok=True)
    sc = rw.reset()
    trees.MASSES[:] = PUFFS
    rng = random.Random(41)
    wood, leaves = trees.build_tree(out, rng)
    trees.export_engine(wood, leaves, os.path.join(out, "blossom.glb"))
    import engine_assets  # noqa: E402  (the core helper)
    core = engine_assets.metaball_mesh("blossom_core", [(Vector(c), 1.0, (r[0] * 0.78, r[1] * 0.78, r[2] * 0.74)) for c, r, _ in PUFFS], 0.8)
    engine_assets.decimate_to(core, 3000)
    rw.export_glb([core], os.path.join(out, "blossom_core.glb"))
    sun = Vector((0.55, -0.6, 0.58)).normalized()
    rw.sky_world(sc, sun, strength=0.95)
    rw.sun_light(sun, strength=5.0)
    rw.setup_render(sc, W, H, samples=S)
    # preview colour: tint the leaf material pink so the silhouette sheet reads as blossom
    for n in leaves.data.materials[0].node_tree.nodes:
        if n.type == "HUE_SAT":
            n.inputs["Hue"].default_value = 0.92
    cam = rw.camera(lens=30)
    for k, ang in enumerate((200, 290, 20)):
        r = math.radians(ang)
        rw.aim(cam, (math.cos(r) * 46, math.sin(r) * 46, 7.0), (0, 0, 12.0))
        rw.render(sc, os.path.join(out, "blossom_turn_%d.png" % k))
    print("done")


if __name__ == "__main__":
    main()
