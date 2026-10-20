"""Rainbow World, Stage 2 'together' render: the prototypes in one place under one lighting setup.

Builds the band section (grass, adapted-rock rims), places Great Trees and boulders on its top face, raises
the great rainbow arch across the band, and parks the hero cloud beside it. The lighting setup is the one
proposed for the scene (03-art-direction.md section 8): a warm sun at a raking angle, the art-directed all-round
sky, and a warm, shadowless bounce from the cloud sea below.

Args: <outdir> <stage2 dir (holds rocks/rocks.blend)> [w h samples]
"""
import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import band  # noqa: E402
import clouds  # noqa: E402
import rainbows  # noqa: E402
import rw_common as rw  # noqa: E402
import trees  # noqa: E402


def frame_at(fr, s):
    i = max(0, min(band.NS, int(round(s / band.L * band.NS))))
    return fr[i]


def place(objs, pos, up, yaw, scale=1.0):
    fwd = up.orthogonal().normalized()
    fwd = (Matrix.Rotation(yaw, 3, up) @ fwd).normalized()
    side = fwd.cross(up).normalized()
    m = Matrix((side, fwd, up)).transposed().to_4x4()
    m.translation = pos
    m = m @ Matrix.Scale(scale, 4)
    out = []
    for o in objs:
        c = o.copy()
        bpy.context.scene.collection.objects.link(c)
        c.matrix_world = m @ o.matrix_world
        out.append(c)
    return out


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0] if args else "build/rw/stage2/together")
    s2 = os.path.abspath(args[1] if len(args) > 1 else "build/rw/stage2")
    W = int(args[2]) if len(args) > 2 else 960
    H = int(args[3]) if len(args) > 3 else 540
    S = int(args[4]) if len(args) > 4 else 48
    os.makedirs(out, exist_ok=True)
    sc = rw.reset()
    rng = random.Random(3)
    bandobj, fr = band.build_band()
    bandobj.data.materials.append(band.ground_material())
    protos = band.append_rocks(os.path.join(s2, "rocks", "rocks.blend"))
    for o in protos:
        o.location = (0, 0, -9000)
    band.place_rim_rocks(fr, protos)
    _, pm, tm, um, sm = frame_at(fr, band.L / 2)
    sun = (0.45 * um + 0.75 * sm - 0.45 * tm).normalized()
    rw.sky_world(sc, sun, strength=0.95)
    rw.sun_light(sun, strength=5.0)
    fill = rw.sun_light((-0.3 * sun - 0.9 * um).normalized(), strength=1.4, angle_deg=40, color=rw.P["cloud_bounce"])
    fill.data.use_shadow = False

    # grass round the main viewpoints
    clump = band.grass_clump()
    sc.collection.objects.link(clump)
    clump.location = (0, 0, -5000)
    eye, t0, u0, s0 = band.surface_point(fr, 40.0, -50.0, 2.0)
    band.scatter_grass(bandobj, clump, eye, 80.0, 12.0)

    # the Great Tree (built once at the origin, hidden, then placed as copies)
    wood, leaves = trees.build_tree(out, rng)
    for o in (wood, leaves):
        o.hide_render = True
    for (s, x, yaw, scale) in ((95, -40, 0.4, 1.0), (180, 70, 2.1, 0.85), (300, -75, 4.0, 1.1), (70, 85, 1.0, 0.7)):
        p, t, u, side = band.surface_point(fr, s, x, -0.8)
        for c in place([wood, leaves], p, u, yaw, scale):
            c.hide_render = False
    # boulders near the first tree
    for k in range(6):
        s, x = 70 + rng.uniform(-15, 25), -55 + rng.uniform(-20, 20)
        p, t, u, side = band.surface_point(fr, s, x, -0.6)
        proto = rng.choice(protos)
        for c in place([proto], p, u, rng.uniform(0, 6.28), rng.uniform(2.5, 6.0)):
            c.hide_render = False

    # the great arch across the band at s = 210
    _, pa, ta, ua, sa = frame_at(fr, 210.0)
    rainbows.ARCH["centre"] = tuple(pa + ua * (band.FACE - 8.0))
    bows = rainbows.build_rainbow()
    m = Matrix((sa, ua, ta)).transposed().to_4x4()
    m.translation = Vector(rainbows.ARCH["centre"])
    for o in bows:
        o.matrix_world = m

    # the hero cloud, beside and above the band
    lobes = clouds.author_cloud()
    cm = clouds.preview_volume(lobes)
    cm.location = pm + sm * 520 + um * 120 + tm * 150
    up_m = Matrix((sm, tm, um)).transposed().to_4x4()
    cm.rotation_euler = up_m.to_euler()

    rw.setup_render(sc, W, H, samples=S)
    sc.cycles.volume_step_rate = 1.0
    cam = rw.camera()
    # 1 the wide view along the band
    rw.rectilinear(cam, 22)
    _, p0, t0f, u0f, s0f = frame_at(fr, 0.0)
    rw.aim(cam, p0 + u0f * 95 - t0f * 130 - s0f * 140, pa + ua * 40, up=u0f)
    rw.render(sc, os.path.join(out, "together_1_wide.png"))
    # 2 on the surface, fisheye 150, toward the tree and the arch
    rw.fisheye(cam, 150, W, H)
    tgt, _, _, _ = band.surface_point(fr, 200.0, 0.0, 40.0)
    rw.aim(cam, eye, tgt, up=u0)
    rw.render(sc, os.path.join(out, "together_2_surface_fisheye150.png"))
    # 3 rainbow-dominated: under the arch, looking up through it toward the cloud
    e3, _, _, _ = band.surface_point(fr, 180.0, 30.0, 3.0)
    rw.aim(cam, e3, pa + ua * 130 + sa * 30, up=ua)
    rw.render(sc, os.path.join(out, "together_3_under_arch_fisheye150.png"))
    rw.save_blend(os.path.join(out, "together.blend"))
    print("done")


if __name__ == "__main__":
    main()
