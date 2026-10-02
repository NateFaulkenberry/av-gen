"""All You Got, art pass 4: owner bars 91 to the end (199.73-258 s), on Builder4 (04-art-pass-4.md PARTS 11-13).
Pass 3's final chorus (film3_late.py) kept where the owner liked it, changed where pass 4 asks:

  91-98   the open: a line-drawn landscape; LET IT GO stands on the hills; the lantern rings, the floating furniture.
          Far to the west, beyond the land's edge, the city he came through glows on the horizon
  99-106  the stair into the sky: IT'S JUST STEPS IN A PROCESS on its risers -- and now he is at the top, standing in the
          middle of the summit, his back to us, against the open sky (PART 11)
  107-112 the summit, the loudest bars: the camera comes up beside him, looks back down at everything below (the stair,
          the land, the city's lights), comes round in front of him; upright, the chest lifted, the head raised, the arms
          a little open; the LET IT GO chants and the fireworks over him
  113.1   the crash: the digital transition (kept)
  113.3   and dawn at once, on the summit (PART 12): the crash's white clears onto the sunrise ahead of him, the sun
          already over the eastern peaks; no night, no other hill in between. The world washes out with the chord
          (it stops at 251.4 s) and breaks up; it ends where it began, on the road's four dashes in the black
"""

from __future__ import annotations

import city4 as C
import kit as K
import outdoor2 as OD
import pass2_grid as G
import tableaux4 as T4
from film3_house import lyric_glitch

t = G.t
BEAT1 = G.BAR1 / 4.0
BEAT2 = G.BAR2 / 4.0
SUMMIT = (10.0, 14.2, 0.0)
# pass 4 review: he stands AT THE TOP OF THE STAIRS -- on the summit's edge where the flight arrives, through the gap in
# its wall, facing out to the east -- not in the middle of the summit, where its edge hid him from the whole climb
TOP = (5.3, 14.2, 0.0)       # his soles
DAWN = t(113, 3)             # 248.38 s: the crash's bright attack has fallen; the chord rings on to 251.4


def summit_with_gap():
    """Pass 3's summit with an opening in its low wall where the stair arrives (a stair should arrive somewhere: pass 3's
    wall crossed its top step)."""
    s = OD.summit()
    gap = K.X(((-6.4, -5.2), (-0.5, 2.0), (-1.4, 1.4)))
    return K.D(s, gap)


def east_mountains(k=K.FILL):
    """A range on the eastern horizon for the sun to rise behind (pass 3's peaks are in the north)."""
    peaks = [(150.0, -60.0, 50.0, 26.0), (165.0, 5.0, 64.0, 36.0), (150.0, 70.0, 46.0, 24.0)]
    return K.U(*[K.S(K.T([x, -6.0, z], K.R([0, 15 * i, 0], K.pyramid(bb, h))), k) for i, (x, z, bb, h) in enumerate(peaks)])


def far_city():
    """The city he came through, on the plain far to the west (x -340..-170), seen from the summit: the same blocks,
    windows and roof lights as city4's, on its own ground, beyond the land's edge (about 60 nodes)."""
    region = ((-340.0, -170.0), (-130.0, 130.0))
    sets = []
    for nm_ in ("Low", "Mid", "TowerA", "Sky"):
        w, d, h, (ox, oz), (px, pz), (bi, bk) = C.SETS[nm_]
        bld = C.building(w, d, h, shop=K.CANVAS if nm_ == "Low" else None, crown=K.GLOW if nm_ != "Low" else None,
                         floor=C.STOREY if nm_ in ("Low", "Mid") else C.STOREY * 2)
        sets.append(K.T([-250.0 + ox, -1.0, oz], K.repeat([px, 0.0, pz], 0, bld)))
    ground = K.S(K.X(((-345.0, -168.0), (-1.6, -1.0), (-135.0, 135.0))), K.FLOOR)
    lamps = K.S(K.T([-250.0, -1.0, 0.0], K.repeat([20.0, 0.0, 40.0], 0, K.X(((-0.25, 0.25), (4.6, 5.0), (-0.25, 0.25))))), K.GLOW)
    return K.I(K.U(*sets, ground, lamps), K.X(((region[0][0], region[0][1]), (-2.0, 140.0), (region[1][0], region[1][1]))))


def build(b):
    f = b.f
    end = f.end
    far = {"max_distance": 520.0}
    land = {"objects": [
        ("landGround", OD.terrain(), "wall", (-170, -12, -170), (170, 6, 170), dict(far, edge_pixels=1.8, step_scale=0.55, edge_intensity=5.0)),
        ("landMtn", K.U(OD.mountains(), east_mountains()), "furn2", (-130, -10, -175), (200, 60, 110), far),
        ("landTrees", OD.grove([("pine", -7, -9, 3.4), ("pine", -10, -14, 4.2), ("round", 8, -11, 3.6), ("pine", 13, -19, 4.6),
                                ("round", -16, -22, 4.2), ("pine", 4, -26, 4.0), ("round", -22, 6, 3.8), ("pine", 20, 4, 4.4),
                                ("pine", -26, -6, 5.0)]), "furn", (-34, -6, -34), (34, 12, 12), far),
        ("landStones", K.U(OD.stones([(-3, -5, 0.7), (4, -7, 0.5), (9, -4, 0.9), (-8, 3, 0.8)]),
                           OD.tufts([(-1.5, -3), (1.2, -4), (2.6, -2.5), (-2.8, -6), (5.5, 0.5), (-5.2, 1.5)])),
         "furn2", (-14, -6, -12), (14, 4, 6), far),
        # (its windows and roof lights its own warm light, so the city reads from the summit whatever the palette)
        ("farCity", far_city(), "wall", (-345, -2, -135), (-168, 125, 135),
         dict(far, edge_pixels=1.2, emission={K.CANVAS2: [1.5, 1.25, 0.85], K.GLOW: [2.6, 1.7, 0.8], K.CANVAS: [1.6, 0.8, 0.35]}))],
        "lights": []}
    b.world("land", land)
    rings = {"objects": [("ringLow", OD.lantern_ring(16.0, 7, 9.0, "ringLowRot"), "furn2", (-22, 4, -22), (22, 16, 22), far),
                         ("ringHigh", OD.lantern_ring(24.0, 9, 22.0, "ringHighRot"), "furn", (-30, 16, -30), (30, 30, 30), far)],
             "lights": []}
    b.world("rings", rings)
    sky = {"objects": [("stair", K.T([-14.0, OD.ground(-14, 0) - 0.2, 0.0], OD.sky_stair(40, 0.45, 0.32, 2.4)), "furn2",
                        (-15, -3, -3), (5, 15, 3), far),
                       ("summit", K.T(list(SUMMIT), summit_with_gap()), "wall", (3, 12.5, -7), (17, 16.0, 7), far),
                       ("sun", K.T([175.0, -8.0, 0.0], K.R([0, -90, 0], K.T([0, 0, 0], OD.sun_disc(9.0), name="sunAt"))), "furn2",
                        (160, -60, -40), (190, 70, 40), far),
                       ("skyFurn", OD.sky_furniture(), "furn", (-17, 5, -21), (15, 18, -1), far)], "lights": []}
    b.world("sky", sky)
    # him, at the top: the triumph tableau in the middle of the summit, facing east (the open sky, the sunrise)
    man_tree, lo, hi = T4.placed("triumph", "topMan", TOP, 90.0, room=None)
    b.room_world("topman", {"objects": [("topMan", man_tree, "figure", lo, hi, dict(far, figure=True))], "lights": []})
    f.nodes.append({"name": "stars", "kind": "particles", "particles": {
        "capacity": 3500, "spawnRate": 3500.0, "shape": "box", "position": [0.0, 70.0, -40.0], "extent": [170.0, 40.0, 140.0],
        "lifetimeMin": 900.0, "lifetimeMax": 1000.0, "speedMin": 0.0, "speedMax": 0.0, "spread": 1.0, "gravity": [0.0, 0.0, 0.0],
        "sizeStart": 0.22, "sizeEnd": 0.22, "blend": "additive", "colorStart": [0.85, 0.9, 1.0, 0.9], "colorEnd": [0.85, 0.9, 1.0, 0.9]}})
    b.worlds["rings"]["names"].append("stars")
    fw_cols = {"fwA": ([1.0, 0.3, 0.85, 1.0], [0.55, 0.2, 1.0, 0.0]), "fwB": ([0.4, 0.95, 1.0, 1.0], [1.0, 1.0, 1.0, 0.0]),
               "fwC": ([1.0, 0.82, 0.3, 1.0], [1.0, 0.35, 0.1, 0.0])}
    for nm_, (c0, c1) in fw_cols.items():
        f.nodes.append({"name": nm_, "kind": "particles", "particles": {
            "capacity": 9000, "spawnRate": 0.0, "burst": 0.0, "shape": "sphere", "position": [10.0, 26.0, 0.0],
            "extent": [0.25, 0.25, 0.25], "lifetimeMin": 0.9, "lifetimeMax": 1.7, "direction": [0.0, 1.0, 0.0], "spread": 1.0,
            "speedMin": 5.0, "speedMax": 11.0, "gravity": [0.0, -4.0, 0.0], "drag": 0.9, "sizeStart": 0.2, "sizeEnd": 0.0,
            "blend": "additive", "colorStart": c0, "colorEnd": c1, "emissive": 3.0, "velocityStretch": 0.06, "stretchMax": 6.0}})
        b.worlds["sky"]["names"].append(nm_)
    gy = OD.ground
    keys = ("land", "rings", "sky", "topman")

    # =========================================================================================================
    # FINAL CHORUS (91-114)
    # =========================================================================================================
    chA = [[0.0, gy(0, 46) + 2.2, 46.0], [2.0, gy(2, 34) + 2.6, 34.0], [-1.5, gy(-1.5, 22) + 4.0, 22.0], [-4.0, gy(-4, 10) + 7.5, 10.0],
           [-2.0, gy(-2, 0) + 10.0, 2.0]]
    b.shot("chorusA", t(91), t(99), chA, None, keys=keys,
           look_keys=[(t(91), (0.0, 3.0, 0.0)), (t(93), (-3.0, 5.0, -20.0)), (t(95), (6.0, 8.0, -30.0)), (t(97), (-10.0, 9.0, -20.0)),
                      (t(99), (-14.0, 4.0, 0.0))], fov=[(t(91), 72.0), (t(93), 64.0), (t(99), 62.0)], ease_kind="linear")
    k = 0
    pats = [[(-0.5, -0.35), (0.0, -0.55), (0.5, -0.3)], [(0.45, -0.5), (-0.1, -0.3), (-0.55, -0.6)],
            [(-0.6, -0.2), (-0.15, -0.45), (0.35, -0.65)], [(0.55, -0.25), (0.1, -0.6), (-0.4, -0.4)]]
    import sdf_eval
    trees_n = next(n for n in f.nodes if n["name"] == "landTrees")["sdf"]["tree"]["root"]
    stones_n = next(n for n in f.nodes if n["name"] == "landStones")["sdf"]["tree"]["root"]

    def clear_of_trees(e):
        p = e["position"]
        n = e["normal"]
        rgt = [n[2], 0.0, -n[0]]
        hw = 0.5 * e["height"] * 0.66 / 0.72 * len(e["text"])
        pts = [[p[0] + rgt[0] * hw * u, p[1] + e["height"] * v, p[2] + rgt[2] * hw * u] for u in (-1.0, 0.0, 1.0) for v in (-0.5, 0.0, 0.5)]
        return all(min(sdf_eval.evaluate(trees_n, q), sdf_eval.evaluate(stones_n, q)) > 0.25 for q in pts)
    for bar in range(91, 99):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            pat = pats[(2 * (bar - 91) + half) % 4]
            for j, wd in enumerate(("LET", "IT", "GO")):
                tw = t(bar, b0 + 0.5 * j)
                # a standing word must not stand inside a tree (ADR-1056's text check found one): try nearby spots
                for dx, dy in ((0.0, 0.0), (0.12, 0.0), (-0.12, 0.0), (0.0, -0.1), (0.24, 0.05), (-0.24, 0.05)):
                    e = b.word_at(wd, tw, tw + G.BAR2 * 1.5, ("ground", gy), pat[j][0] + dx, pat[j][1] + 0.12 + dy, k=0.085, style="rise",
                                  role="word" if j != 1 else "accent", name=f"chA{k:02d}", intensity=3.0, tin=0.15, tout=0.35,
                                  stand=True, category="floatingText")
                    if clear_of_trees(e):
                        break
                    b.words.remove(e)
                k += 1
    g_ch = b.gate("gChorus", t(91), t(113))
    b.pulse("sdf/landGround/look/edge/intensity", 2.0, "quarter", g_ch)
    b.pulse("sdf/ringLow/look/edge/intensity", 4.0, "quarter", g_ch)
    b.pulse("sdf/ringHigh/look/edge/intensity", 4.0, "half", g_ch)
    b.pulse("sdf/farCity/surface/5/emission", 1.5, "quarter", g_ch)
    f.route("grid.song.quarter.wave", "post/grade/hueShift", 0.22, depth=g_ch)
    f.track("sdf/ringLow/node/ringLowRot/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (end, [0.0, 9.0 * end, 0.0])])
    f.track("sdf/ringHigh/node/ringHighRot/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (end, [0.0, -6.0 * end, 0.0])])
    for i in range(5):
        rate = [11.0, -8.0, 6.0, -14.0, 9.0][i]
        f.track(f"sdf/skyFurn/node/sf{i}/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"),
                                                    (end, [rate * end * 0.3, rate * end, rate * end * 0.2])])
    b.pulse("sdf/skyFurn/look/edge/intensity", 3.0, "quarter", g_ch)
    sx0 = -14.0
    sy0 = gy(-14, 0) - 0.2
    slope = 0.32 / 0.45

    def on_stair(dx, z):
        return [sx0 + dx, sy0 + min(max(dx, 0.0), 18.0) * slope + 1.85, z]
    # (the climb stops 2.7 m short of him: he is standing on the top step's landing)
    stair_eye = [on_stair(-6.0, 2.4), on_stair(-1.5, 1.4), on_stair(3.0, 0.6), on_stair(7.5, 0.2), on_stair(11.5, 0.0),
                 on_stair(14.6, 0.0), on_stair(16.6, 0.0)]
    # the climb looks up the flight to him: he stands at the top against the sky, his back to us
    b.shot("stair", t(99), t(107), stair_eye, None, keys=keys,
           look_keys=[(t(99), (sx0 + 9.0, sy0 + 5.5, 0.0)), (t(100, 3), (TOP[0], TOP[1] + 1.1, 0.0)),
                      (t(103), (TOP[0], TOP[1] + 1.25, 0.0)), (t(107), (TOP[0], TOP[1] + 1.35, 0.0))],
           fov=[(t(99), 50.0), (t(102), 54.0), (t(105), 60.0), (t(107), 64.0)], ease_kind="linear")
    # (preview v2: at 64 degrees he was a speck at the top for the first half of the climb; a longer lens at the
    # bottom of the stair, opening as we come up to him)
    b.show("topMan", t(99), 255.0)
    words = ["IT'S", "JUST", "STEPS", "IN", "A", "PROCESS"]
    for rep, bar in enumerate((99, 103)):
        for i, wd in enumerate(words):
            step = 6 + rep * 14 + i * 2
            x = sx0 + step * 0.45
            y = sy0 + step * 0.32 + 0.16
            b.word(wd, t(bar - 1, 4.0) + i * 0.3, t(bar + 3, 4.0), (x + 0.004, y, 0.0), (-1, 0, 0), 0.2, style="pop",
                   role="word", name=f"steps{rep}{i}x", rotation=[0.0, -90.0, 0.0], category="stairText")
        b.word("FOR YOUR LIFE", t(bar, 3.0), t(bar + 3, 4.0), (sx0 + 24.0, sy0 + 19.0, 0.0), (-1, 0.2, 0), 1.4, style="rise",
               role="accent", name=f"fyl{rep}", intensity=5.0, tin=0.4, rotation=[0.0, -90.0, 0.0], category="floatingText")
        lyric_c = b.clap(f"lgc{rep}", t(bar - 1, 4.0), release=0.15)
        f.route(lyric_c, "temporal/mosh/shift", 9.0)
    b.pulse("sdf/stair/look/edge/intensity", 3.0, "quarter", g_ch)
    # his rim brightens as we come up the last steps: the silhouette against the sky
    f.track("sdf/topMan/look/rim/intensity", [(0.0, 1.15, "step"), (t(105), 1.15, "smooth"), (t(107), 2.4, "step")])

    # ---- the summit (107-113.3): up beside him, a look back down at everything below, round to his front --------
    sx, sy, sz = TOP
    # up from the top step to beside him and above the summit's wall, the look back down at everything below (the stair,
    # the land, the far city), then round in front of him and low, the city he came through behind him
    sum_eye = [on_stair(16.6, 0.0), [sx - 0.9, sy + 2.4, 2.4], [sx + 1.5, sy + 1.9, 3.4], [sx + 3.6, sy + 1.0, 1.6],
               [sx + 3.9, sy + 0.9, -0.4]]
    b.shot("summit", t(107), DAWN, sum_eye, None, keys=keys,
           look_keys=[(t(107), (sx, sy + 1.35, 0.0)), (t(108), (sx, sy + 1.3, 0.3)), (t(109), (-90.0, -6.0, 30.0)),
                      (t(110), (-200.0, 4.0, 10.0)), (t(111), (sx, sy + 1.6, 0.0)), (t(112, 3), (sx, sy + 1.5, 0.0)),
                      (t(113), (sx, sy + 1.5, 0.0)), (DAWN, (sx, sy + 2.5, 0.0))],
           fov=[(t(107), 66.0), (t(109), 58.0), (t(110), 52.0), (t(111), 62.0), (t(112, 3), 62.0), (t(113), 50.0),
                (t(113) + 0.4, 84.0), (DAWN, 80.0)], ease_kind="linear",
           moves=[(t(107), 0.0), (t(109), 0.42), (t(111), 0.75), (t(113), 0.97), (DAWN, 1.0)])
    # the gaze turns by angle (film4.Builder4._angular_gaze): a straight line from him, two metres off, to the far city
    # swept the look point past the lens and whipped the view round in a sixth of a second (236.5)
    b._angular_gaze(f.shots[-1])
    k = 0
    arcs = [[(-0.6, 0.3), (0.0, 0.42), (0.6, 0.3)], [(-0.55, -0.1), (0.0, 0.1), (0.55, -0.1)], [(-0.4, 0.42), (0.05, 0.25), (0.5, 0.42)]]
    for bar in range(107, 113):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            arc = arcs[(2 * (bar - 107) + half) % 3]
            for j, wd in enumerate(("LET", "IT", "GO")):
                tw = t(bar, b0 + 0.5 * j)
                b.word_at(wd, tw, tw + G.BAR2, ("view", 11.0 + 2.0 * j), arc[j][0], arc[j][1] + 0.12, k=0.07, style="flash",
                          role="word", name=f"chC{k:02d}", intensity=4.5, tin=0.08, tout=0.3, category="floatingText")
                k += 1
    fw_pos = {"fwA": [], "fwB": [], "fwC": []}
    n_fw = 0
    for bar in range(107, 113):
        for bt in (2.0, 4.0):
            tb = t(bar, bt)
            c = b.clap(f"fw{bar}{int(bt)}", tb, release=0.5)
            f.route(c, "sdf/ringLow/look/edge/intensity", 10.0)
            f.route(c, "post/bloom/intensity", 0.6)
            em = ["fwA", "fwB", "fwC"][n_fw % 3]
            sx_, sy_ = [(-0.55, 0.45), (0.5, 0.55), (0.0, 0.62), (-0.3, 0.3), (0.62, 0.35), (-0.62, 0.6)][n_fw % 6]
            pos = f.in_view(tb, sx_, sy_, 22.0 + 4.0 * (n_fw % 3))[0]
            fw_pos[em].append((tb - 0.05, [round(v, 3) for v in pos]))
            cb = b.clap(f"fwb{bar}{int(bt)}", tb, release=0.06)
            f.route(cb, f"particles/{em}/burst", 900.0)
            n_fw += 1
    for em in ("fwA", "fwB", "fwC"):
        fw_pos[em].append((t(113) - 0.05, [sx, sy + 5.0, 0.0]))
        cb = b.clap(f"crash{em}", t(113), release=0.1)
        f.route(cb, f"particles/{em}/burst", 2600.0)
    for em, ks in fw_pos.items():
        ks.sort()
        f.track(f"particles/{em}/position", [(0.0, ks[0][1], "step")] + [(tt, p, "step") for tt, p in ks])
    for o in ("ringLow", "ringHigh"):
        f.track(f"sdf/{o}/transform/scale", [(0.0, [1.0, 1.0, 1.0], "step"), (t(112, 3), [1.0, 1.0, 1.0], "easeOut"),
                                            (t(112, 3) + 0.12, [0.7, 1.0, 0.7], "step"), (t(112, 4), [0.7, 1.0, 0.7], "easeOut"),
                                            (t(112, 4) + 0.12, [0.4, 1.0, 0.4], "step"), (t(113), [0.4, 1.0, 0.4], "easeOut"),
                                            (t(113) + 0.6, [4.0, 1.0, 4.0], "smooth"), (t(115), [7.0, 1.0, 7.0], "step")])
    for bt in (3.0, 4.0):
        c = b.clap(f"build{int(bt)}", t(112, bt), release=0.4)
        f.route(c, "camera/exposure/compensation", 1.0)
        f.route(c, "temporal/mosh/amount", 0.3)
    # the air clears at the summit (the open's fog is 0.022: at 250 m the far city was gone in it) and comes back in the crash
    b.key("scene/volumeDensity", t(107), 0.022, "smooth")
    b.key("scene/volumeDensity", t(108, 2), 0.006, "step")
    b.key("scene/volumeDensity", t(112, 4), 0.006, "step")
    b.key("scene/volumeDensity", t(113), 0.022, "step")
    # ---- 113.1: the crash -- the digital transition (pass 3's, its tail shortened so the dawn can follow at once) --
    c = b.clap("crash", t(113), release=1.0)
    f.route(c, "camera/exposure/compensation", 2.8)
    f.route(c, "post/bloom/intensity", 3.0)
    f.route(c, "palette/saturation", 1.0)
    f.route(c, "post/lens/chromaticAberration", 0.8)
    f.route(c, "post/grade/hueShift", 3.1)
    f.route(c, "temporal/mosh/amount", 0.6)
    f.route(c, "temporal/mosh/shift", 20.0)
    for o in ("ringLow", "ringHigh", "landGround", "landTrees", "landMtn", "stair", "summit", "skyFurn"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 16.0)

    # =========================================================================================================
    # DAWN (113.3-255): at once, on the summit; he faces the sunrise; the world washes out; back to the road
    # =========================================================================================================
    # (preview v2: from 1.1 m up, off to one side of the wall's gap, a piece of the summit's wall filled a quarter of the
    # dawn; from over the gap's middle and higher the wall lies below the horizon on both sides)
    dawn_eye = [[sx - 2.6, sy + 1.6, 0.35], [sx - 3.0, sy + 1.55, 0.25], [sx - 3.4, sy + 1.5, 0.15]]
    b.shot("dawn", DAWN, 255.0, dawn_eye, None, keys=keys,
           look_keys=[(DAWN, (140.0, 18.0, -4.0)), (252.0, (150.0, 22.0, -2.0)), (255.0, (160.0, 26.0, 0.0))],
           fov=[(DAWN, 58.0), (255.0, 52.0)])
    # the sun is already over the peaks when the white clears (pass 3's rose from below the horizon a second late)
    f.track("sdf/sun/node/sunAt/translation", [(0.0, [0.0, -60.0, 0.0], "step"), (DAWN - 0.05, [0.0, 4.0, 0.0], "smooth"),
                                              (252.3, [0.0, 24.0, 0.0], "smooth"), (end, [0.0, 34.0, 0.0])])
    b.val += [(DAWN, 1.0, "smooth"), (252.0, 1.4, "smooth"), (254.8, 4.5, "smooth"), (255.0, 5.0, "step"), (255.01, 1.0, "step")]
    f.track("camera/exposure/compensation", [(0.0, 0.0), (251.4, 0.0, "smooth"), (254.9, 1.9, "step"), (255.0, 0.0, "step")], mode="add")
    f.track("temporal/mosh/amount", [(0.0, 0.0), (253.0, 0.0, "smooth"), (254.3, 0.5, "smooth"), (254.95, 0.95, "step"),
                                     (255.0, 0.0, "step")], mode="add")
    f.track("temporal/mosh/shift", [(0.0, 0.0), (253.4, 0.0, "smooth"), (254.95, 30.0, "step"), (255.0, 0.0, "step")], mode="add")
    for o, t_out in (("landMtn", 252.2), ("ringLow", 252.6), ("ringHigh", 252.4), ("sun", 254.6), ("landGround", 253.4),
                     ("landTrees", 253.8), ("landStones", 254.1), ("summit", 254.3), ("stair", 253.6), ("farCity", 252.0),
                     ("topMan", 254.7)):
        f.track(f"sdf/{o}/look/edge/intensity", [(0.0, 1.0, "step"), (DAWN, 1.0, "smooth"), (t_out - 1.4, 1.0, "smooth"),
                                                 (t_out, 0.0, "step")], mode="multiply")
    # the coda: the road's four dashes in the black, as at the count-in, going out one by one
    b.shot("coda", 255.0, end, [[-46.0, 2.0, 12.6], [-45.99, 2.0, 12.6]], (-20.0, 0.8, 12.0), keys=("street",), fov=54.0)
    for o in ("row", "faller", "lamps", "parked", "fences", "heroShell", "heroParts", "heroRoof", "heroTree"):
        b.key(f"nodes/{o}/visible", 254.99, 0.0)
    f.track("sdf/street/node/gridClip/size", [(254.99, [0.0, 0.5, 0.0], "step"), (end, [0.0, 0.5, 0.0], "step")], mode="replace")
    f.track("sdf/street/look/edge/intensity", [(0.0, 1.0, "step"), (254.99, 0.0, "step"), (end, 0.0, "step")], mode="multiply")
    blink = [(254.99, [260.0, 0.5, 0.5])]
    for tt, x_end in ((255.6, -28.6), (256.2, -33.6), (256.8, -38.6), (257.4, -60.0)):
        blink.append((tt, [x_end + 42.0, 0.5, 0.5]))
    f.track("sdf/street/node/dashClip/size", [(tt, v, "step") for tt, v in blink], mode="replace")
    b.palette_at(255.0, "P0boot")
