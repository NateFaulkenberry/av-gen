"""Rainbow World, Stage 5: the Ridge Pine -- the fourth tree (tall, narrow: vertical rhythm on the ridgelines).

The Great Tree's skeleton and card system (trees.py) with a different canopy: seven stacked, shrinking tiers that
make a tall cone of layered boughs, so it reads as a vertical accent among the Great Trees' wide pads.

Outputs (args: <outdir> [w h samples]): pine_turn_<k>.png, pine{,_wood,_leaves}.glb, pine_core.glb
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

_Z = [8.6, 11.0, 13.9, 16.1, 18.8, 20.9, 23.4]           # irregular tier spacing
_R = [5.2, 4.3, 4.6, 3.4, 3.1, 2.2, 1.4]                  # not a strict taper: a fuller tier breaks the stack
_O = [(0.0, 0.0), (0.6, -0.4), (-0.5, 0.5), (0.4, 0.6), (-0.4, -0.3), (0.3, 0.2), (0.0, 0.0)]
PUFFS = [((_O[k][0], _O[k][1], _Z[k]), (_R[k], _R[k] * 0.92, 1.35 + 0.15 * (k % 2)), 0.04 * ((-1) ** k)) for k in range(7)]


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0] if args else "build/rw/stage5/pine")
    W = int(args[1]) if len(args) > 1 else 640
    H = int(args[2]) if len(args) > 2 else 480
    S = int(args[3]) if len(args) > 3 else 24
    os.makedirs(out, exist_ok=True)
    sc = rw.reset()
    trees.MASSES[:] = PUFFS
    rng = random.Random(53)
    wood, leaves = trees.build_tree(out, rng)
    trees.export_engine(wood, leaves, os.path.join(out, "pine.glb"))
    import engine_assets  # noqa: E402  (the core helper)
    core = engine_assets.metaball_mesh("pine_core", [(Vector(c), 1.0, (r[0] * 0.78, r[1] * 0.78, r[2] * 0.74)) for c, r, _ in PUFFS], 0.8)
    engine_assets.decimate_to(core, 3000)
    rw.export_glb([core], os.path.join(out, "pine_core.glb"))
    sun = Vector((0.55, -0.6, 0.58)).normalized()
    rw.sky_world(sc, sun, strength=0.95)
    rw.sun_light(sun, strength=5.0)
    rw.setup_render(sc, W, H, samples=S)
    # preview colour: tint the leaf material pink so the silhouette sheet reads as blossom
    cam = rw.camera(lens=30)
    for k, ang in enumerate((200, 290, 20)):
        r = math.radians(ang)
        rw.aim(cam, (math.cos(r) * 46, math.sin(r) * 46, 7.0), (0, 0, 12.0))
        rw.render(sc, os.path.join(out, "pine_turn_%d.png" % k))
    print("done")


if __name__ == "__main__":
    main()
