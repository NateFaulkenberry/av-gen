"""Stage 4 prep, E6: the full knot band in grey-box, swept on AV Gen's own spline frames (knot_path.Path).

Questions it answers before any detail is built: does a 260 m band on this loop fill a 150-180 degree frame; how much
land hangs overhead at the crossings; does the twist read as gravity turning; where are the empty-sky stations.

Args: <outdir> [scale twist_turns w h]   (twist_turns: extra whole turns of the band about its centreline)
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "blender"))
import band  # noqa: E402
import knot_path  # noqa: E402
import rw_common as rw  # noqa: E402

args = rw.argv_after_dashes()
OUT = os.path.abspath(args[0])
SCALE = float(args[1]) if len(args) > 1 else 220.0
TURNS = float(args[2]) if len(args) > 2 else 0.0
W = int(args[3]) if len(args) > 3 else 640
H = int(args[4]) if len(args) > 4 else 360
os.makedirs(OUT, exist_ok=True)

ZAMP = float(args[5]) if len(args) > 5 else 0.8
controls = knot_path.trefoil_controls(n=72, scale=SCALE, z_amp=ZAMP)
base = knot_path.Path(controls)
# desired up at each control: the corrected RMF turned by `TURNS` whole turns spread along the loop. A whole number of
# turns closes; the per-point rolls then carry it (unwrapped), and the seam segment is continuous only if TURNS = 0 --
# which is why the default is 0 (see 07-stage3-report.md).
ups = []
for i in range(len(controls)):
    k = i * base.per
    t, n = base.T[k], base.rmf[k]
    ang = 2 * math.pi * TURNS * base.D[k] / base.length
    ups.append(knot_path._rotate_about(n, t, ang))
rolls, path = knot_path.rolls_to_follow(controls, ups)
print("LENGTH %.0f m, rolls %.2f..%.2f" % (path.length, rolls.min(), rolls.max()))

rw.reset()
band.configure_knot(path, ring_spacing=6.0)
ob, fr = band.build_band("knot")
ob.data.materials.append(band.ground_material())
sc = bpy.context.scene
sun = Vector((0.35, -0.45, 0.82)).normalized()
rw.sky_world(sc, sun, strength=0.95)
rw.sun_light(sun, strength=5.0)
fill = rw.sun_light(Vector((0, 0, -1)), strength=1.2, angle_deg=40, color=rw.P["cloud_bounce"])
fill.data.use_shadow = False
rw.setup_render(sc, W, H, samples=12)
cam = rw.camera()
cam.data.clip_end = 30000
rw.rectilinear(cam, 22)
rw.aim(cam, Vector((0, -3.6, 2.2)) * SCALE * 1.6, Vector((0, 0, 0)))
rw.render(sc, os.path.join(OUT, "knot_overview.png"))
rw.fisheye(cam, 170, W, H)
for k, f in enumerate(np.linspace(0.02, 0.94, 8)):
    s = float(f * path.length)
    p, t, u, sd = band.surface_point(fr, s, -20.0, 4.0)
    tgt, _, _, _ = band.surface_point(fr, s + 120.0, -10.0, 25.0)
    rw.aim(cam, p, tgt, up=u)
    rw.render(sc, os.path.join(OUT, "knot_cam_%d.png" % k))
print("done")
