"""All You Got, art pass 2: the film from the pause bar to the end (owner bars 41-115 and the tail), on
film_build.py's Builder. PASS2-PLAN.md §5-6 is the score."""

from __future__ import annotations

import math

import intro2 as IN
import kit as K
import outdoor2 as OD
import pass2_grid as G
import rooms2 as RM
from film2 import srgb

t = G.t
BEAT1 = G.BAR1 / 4.0
BEAT2 = G.BAR2 / 4.0


def node_t(room, obj_name, node_name):
    from film_build import _node_translation
    return _node_translation(room, obj_name, node_name)


def hold_keys(t0, t1, v):
    return [(t0, v, "step"), (t1, v, "step")]


def build_part2(b, end):
    f = b.f
    liv = b.worlds["liv"]["room"]
    bed = b.worlds["bed"]["room"]
    kit_ = b.worlds["kit"]["room"]
    stu = RM.study()
    b.world("stu", stu)
    bath = RM.bathroom()
    b.world("bath", bath)
    gal = RM.gallery()
    b.world("gal", gal)
    far = {"max_distance": 260.0}
    land = {"objects": [
        ("landGround", OD.terrain(), "wall", (-170, -12, -170), (170, 6, 170),
         dict(far, edge_pixels=1.8, step_scale=0.55, edge_intensity=5.0)),
        ("landMtn", OD.mountains(), "furn2", (-130, -10, -175), (130, 60, -90), far),
        ("landTrees", OD.grove([("pine", -7, -9, 3.4), ("pine", -10, -14, 4.2), ("round", 8, -11, 3.6),
                                ("pine", 13, -19, 4.6), ("round", -16, -22, 4.2), ("pine", 4, -26, 4.0),
                                ("round", -22, 6, 3.8), ("pine", 20, 4, 4.4), ("pine", -26, -6, 5.0)]),
         "furn", (-34, -6, -34), (34, 12, 12), far),
        ("landStones", K.U(OD.stones([(-3, -5, 0.7), (4, -7, 0.5), (9, -4, 0.9), (-8, 3, 0.8)]),
                           OD.tufts([(-1.5, -3), (1.2, -4), (2.6, -2.5), (-2.8, -6), (5.5, 0.5), (-5.2, 1.5)])),
         "furn2", (-14, -6, -12), (14, 4, 6), far)],
        "lights": []}
    b.world("land", land)
    tree = {"objects": [("treeTrunk", K.U(OD.tree_trunk(), OD.tree_canopy()), "wall", (-3.5, -2.5, -3.5), (3.5, 40, 3.5), far),
                        ("treeA", OD.tree_branch_group(range(0, 5)), "furn", (-6, -1, -6), (6, 13, 6), far),
                        ("treeB", OD.tree_branch_group(range(5, 10)), "furn2", (-6, 9, -6), (6, 24, 6), far),
                        ("treeC", OD.tree_branch_group(range(10, 14)), "furn", (-6, 20, -6), (6, 34, 6), far)],
            "lights": []}
    b.world("tree", tree)
    sky = {"objects": [("ringLow", OD.lantern_ring(16.0, 7, 9.0, "ringLowRot"), "furn2", (-22, 4, -22), (22, 16, 22), far),
                       ("ringHigh", OD.lantern_ring(24.0, 9, 22.0, "ringHighRot"), "furn", (-30, 16, -30), (30, 30, 30), far),
                       ("stair", K.T([-14.0, OD.ground(-14, 0) - 0.2, 0.0], OD.sky_stair(40, 0.45, 0.32, 2.4)), "furn2",
                        (-15, -3, -3), (5, 15, 3), far),
                       ("summit", K.T([10.0, 14.2, 0.0], OD.summit()), "wall", (3, 12.5, -7), (17, 16.0, 7), far),
                       ("sun", K.T([0.0, -8.0, -150.0], K.T([0, 0, 0], OD.sun_disc(9.0), name="sunAt")), "furn2",
                        (-30, -40, -160), (30, 60, -140), far),
                       ("skyFurn", OD.sky_furniture(), "furn", (-17, 5, -21), (15, 18, -1), far)],
           "lights": []}
    b.world("sky", sky)
    f.nodes.append({"name": "stars", "kind": "particles", "particles": {
        "capacity": 3500, "spawnRate": 3500.0, "shape": "box", "position": [0.0, 70.0, -40.0], "extent": [170.0, 40.0, 140.0],
        "lifetimeMin": 900.0, "lifetimeMax": 1000.0, "speedMin": 0.0, "speedMax": 0.0, "spread": 1.0, "gravity": [0.0, 0.0, 0.0],
        "sizeStart": 0.22, "sizeEnd": 0.22, "blend": "additive", "colorStart": [0.85, 0.9, 1.0, 0.9],
        "colorEnd": [0.85, 0.9, 1.0, 0.9]}})
    b.worlds["sky"]["names"].append("stars")
    b.world("discoLiv", {"objects": [("livDisco", disco_floor(liv["interior"], 0.6), "furn", (-2.7, -0.05, -2.3), (2.7, 0.1, 2.3))],
                         "lights": []})
    b.world("discoKit", {"objects": [("kitDisco", disco_floor(kit_["interior"], 0.6), "furn", (-2.3, -0.05, -1.9), (2.3, 0.1, 1.9))],
                         "lights": []})
    b.world("discoBed", {"objects": [("bedDisco", disco_floor(bed["interior"], 0.6), "furn", (-2.1, -0.05, -1.9), (2.1, 0.1, 1.9))],
                         "lights": []})
    hill_man = {"objects": [("hillMan", K.place(K.mannequin("stand", name="hillMan"), (0.0, OD.ground(0.0, -18.0), -18.0), 180.0),
                             "figure", (-1, -5, -19.5), (1, 2.5, -16.5), far)], "lights": []}
    b.world("hillman", hill_man)

    # ---- palette's slow voice -------------------------------------------------------------------------
    b.palette_at(t(41), "P5void")
    b.palette_at(t(41, 4.5), "P6violet", ramp=BEAT1 * 0.5)
    b.palette_at(t(49, 4), "P7tension")
    b.palette_at(t(66), "P8growth", ramp=BEAT1)
    b.palette_at(t(75), "P9jewels")
    b.palette_at(t(83), "P10dance")
    b.palette_at(t(91), "P11open")
    b.palette_at(t(107), "P12summit", ramp=G.BAR2)
    b.palette_at(t(114), "P13dawn", ramp=G.BAR2 * 2.0)
    b.breath += [(t(41) - 0.01, 0.12), (t(41), 0.0), (t(42), 0.0), (t(42) + 0.01, 0.75), (t(48), 0.75), (t(49, 4), 0.2),
                 (t(66) - 0.01, 0.2), (t(66), 0.0)]

    # =========================================================================================================
    # THE PAUSE BAR (41): black; LET IT GO falls through the void; the second one hot pink, stopping dead
    # =========================================================================================================
    b.shot("pause", t(41), t(42), [[0.0, 1.5, 4.0], [0.0, 1.5, 3.95]], (0.0, 1.5, 0.0), fov=50.0)
    for i, (wd, beat, pos) in enumerate((("LET", 1.0, (-0.7, 1.9, 0.0)), ("IT", 1.5, (0.15, 1.45, -0.4)), ("GO", 2.0, (0.75, 1.0, 0.2)))):
        e = b.word(wd, t(41, beat), t(41, 3.0), pos, (0, 0, 1), 0.42, style="cut", role="word", name=f"pauseA{i}",
                   tilt=[-8, 4, 10][i], intensity=4.0)
        p0 = list(pos)
        f.track(f"nodes/pauseA{i}/position", [(t(41, beat), p0, "smooth"),
                                             (t(41, 3.0), [p0[0] * 1.6, p0[1] - 1.8, p0[2] - 6.0], "step")])
    for i, (wd, beat, pos) in enumerate((("LET", 3.0, (-0.9, 1.75, -0.3)), ("IT", 3.5, (0.0, 1.6, -0.3)), ("GO", 4.0, (0.85, 1.45, -0.3)))):
        b.word(wd, t(41, beat), t(42), pos, (0, 0, 1), 0.36, style="cut", role="accent", name=f"pauseB{i}", intensity=5.0)

    # =========================================================================================================
    # LET IT GO (bars 42-49): four back rooms; LET, IT and GO on different surfaces, arriving on their eighths
    # =========================================================================================================
    sides = {
        # each room: four triples of (position, normal, height, tilt) -- LET, IT, GO -- one triple per half bar
        "stu": [
            [((-1.0, 1.75, -1.69), (0, 0, 1), 0.3, -4), ((0.0, 0.762, -1.45), (0, 1, 0), 0.12, 15), ((0.3, 1.55, -1.66), (0, 0, 1), 0.36, 0)],
            [((-1.79, 2.15, 0.6), (1, 0, 0), 0.28, 0), ((-0.3, 0.01, 0.6), (0, 1, 0), 0.4, -20), ((1.79, 1.7, -0.9), (-1, 0, 0), 0.44, 6)],
            [((0.4, 2.59, -0.3), (0, -1, 0), 0.34, 0), ((0.2, 0.95, -0.72), (0, 0, 1), 0.08, 0), ((1.79, 1.6, 0.2), (-1, 0, 0), 0.22, 0)],
            [((-0.9, 2.3, -1.69), (0, 0, 1), 0.2, 0), ((-0.9, 2.02, -1.69), (0, 0, 1), 0.2, 0), ((-0.9, 1.68, -1.69), (0, 0, 1), 0.3, 0)]],
        "bath": [
            [((0.75, 1.62, -1.37), (0, 0, 1), 0.16, 0), ((-1.02, 0.6, 0.25), (0, 1, 0), 0.24, 90), ((0.0, 0.01, 0.6), (0, 1, 0), 0.5, 0)],
            [((-0.6, 2.25, -1.39), (0, 0, 1), 0.3, 8), ((-1.49, 1.8, -0.5), (1, 0, 0), 0.18, 0), ((0.2, 2.59, 0.2), (0, -1, 0), 0.4, 0)],
            [((1.49, 1.9, 0.2), (-1, 0, 0), 0.32, -10), ((1.49, 1.45, 0.2), (-1, 0, 0), 0.32, 0), ((1.49, 0.95, 0.2), (-1, 0, 0), 0.32, 10)],
            [((-0.3, 1.2, -1.39), (0, 0, 1), 0.5, 0), ((0.75, 1.42, -1.37), (0, 0, 1), 0.16, 0), ((0.4, 0.01, -0.4), (0, 1, 0), 0.6, 30)]],
        "bed": [
            [((-0.3, 2.25, -1.79), (0, 0, 1), 0.28, 0), ((-0.3, 0.56, -0.2), (0, 1, 0), 0.3, 0), ((1.99, 1.6, 0.55), (-1, 0, 0), 0.4, 0)],
            [((-1.99, 2.2, 0.3), (1, 0, 0), 0.24, 0), ((-1.35, 0.56, -1.55), (0, 1, 0), 0.07, 0), ((0.0, 2.59, 0.4), (0, -1, 0), 0.5, 0)],
            [((1.0, 2.0, -1.79), (0, 0, 1), 0.2, 12), ((1.5, 1.7, -1.79), (0, 0, 1), 0.14, -12), ((0.6, 1.3, -1.79), (0, 0, 1), 0.3, 6)],
            [((-1.2, 1.4, 1.79), (0, 0, -1), 0.34, 0), ((-0.3, 1.1, 1.79), (0, 0, -1), 0.34, 0), ((0.6, 0.8, 1.79), (0, 0, -1), 0.34, 0)]],
    }
    lig_shots = [("ligStu", "stu", t(42), t(44), [[-1.0, 1.55, 1.95], [-0.4, 1.52, 0.9], [0.45, 1.5, 0.25]],
                  [(t(42), (0.0, 1.4, -1.6)), (t(43), (0.6, 1.3, -1.2)), (t(44), (1.4, 1.5, -0.4))]),
                 ("ligBath", "bath", t(44), t(46), [[0.8, 1.6, 1.75], [0.45, 1.55, 0.9], [0.1, 1.55, 0.45]],
                  [(t(44), (0.4, 1.4, -1.3)), (t(45), (-0.7, 1.0, -0.6)), (t(46), (1.0, 1.4, -0.2))]),
                 ("ligBed", "bed", t(46), t(48), [[1.25, 1.6, 1.75], [0.7, 1.58, 1.2], [0.2, 1.55, 0.9]],
                  [(t(46), (-0.3, 1.5, -1.7)), (t(47), (0.5, 1.4, -1.5)), (t(48), (-0.4, 1.3, 1.2))])]
    for name, world, t0, t1, eye, lk in lig_shots:
        b.shot(name, t0, t1, eye, None, keys=(world,), look_keys=lk, fov=64.0)
    k = 0
    for wi, world in enumerate(("stu", "bath", "bed")):
        for half in range(4):
            bar = 42 + wi * 2 + half // 2
            b0 = 1.0 if half % 2 == 0 else 3.0
            for j, wd in enumerate(("LET", "IT", "GO")):
                pos, nrm, h, tilt = sides[world][half][j]
                tw = t(bar, b0 + 0.5 * j)
                b.word(wd, tw, t(bar, b0 + 2.0) if half % 2 == 0 else t(bar + 1, 1.0), pos, nrm, h,
                       style=["flash", "pop", "flash"][j], role="word" if j != 1 else "accent", tilt=tilt,
                       name=f"lig{k:02d}", intensity=4.2, tin=0.08, tout=0.18)
                k += 1
    # C07 (45.4): every word in the bathroom flashes at once and flies off its wall
    c = b.clap("c07", t(45, 4), release=0.5)
    f.route(c, "post/bloom/intensity", 1.4)
    f.route(c, "camera/exposure/compensation", 1.2)
    f.route(c, "post/grade/hueShift", -1.6)
    for kk in range(12, 24):
        f.route(c, f"nodes/lig{kk:02d}/emissiveBoost", 14.0)
    # the rebuild (48-49): the kitchen in outline, a flicker on the sixteenths accelerating into C08
    b.shot("ligKit", t(48), t(49, 4), [[1.7, 1.5, 1.6], [1.2, 1.5, 1.2], [0.6, 1.45, 0.9]], (-0.5, 1.0, -0.9), keys=("kit",),
           fov=[(t(48), 62.0), (t(49, 3.5), 56.0), (t(49, 4), 40.0)])
    g_rb = b.gate("gRebuild", t(48), t(49, 4))
    for o in ("kitShell", "kitCounter", "kitTable"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 3.0, "sixteenth", g_rb)
    b.val += [(t(48), 1.0, "smooth"), (t(48, 2), 0.55, "smooth"), (t(49, 3.9), 0.55, "step"), (t(49, 4), 1.0, "step")]
    g_lig = b.gate("gLig", t(42), t(48))
    for o in ("stuShell", "stuFurn", "bathShell", "bathFurn", "bedShell", "bedFurn"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 3.5, "quarter", g_lig)
    # C08 (49.4): a whip-zoom, a one-frame negative, a cut into verse 2
    c = b.clap("c08", t(49, 4), release=0.35, attack=0.12)
    f.route(c, "post/lens/distortion", 0.7)
    f.route(c, "post/lens/chromaticAberration", 1.0)
    f.route(c, "camera/exposure/compensation", 1.5)
    f.route(c, "temporal/mosh/amount", 0.5)
    f.track("post/grade/hueShift", [(0.0, 0.0), (t(49, 4) - 0.002, 0.0, "step"), (t(49, 4), 3.1, "step"), (t(49, 4) + 0.04, 0.0, "step")],
            mode="add")

    # =========================================================================================================
    # VERSE 2 (bars 50-65): the apartment breaks. Every two bars a BIG CLAP corrupts it differently and cuts
    # =========================================================================================================
    v2 = [("v2a", "stu", t(49, 4), t(51, 4), [[-1.3, 1.5, 1.5], [-0.6, 1.48, 0.9], [0.0, 1.45, 0.6]], (0.7, 1.0, -1.3)),
          ("v2b", "liv", t(51, 4), t(53, 4), [[1.9, 1.5, 1.8], [1.3, 1.45, 1.1], [0.9, 1.4, 0.3]], (-1.9, 0.9, -0.5)),
          ("v2c", "bed", t(53, 4), t(55, 4), [[-1.6, 1.6, 1.55], [-1.0, 1.55, 1.0], [-0.6, 1.5, 0.6]], (1.3, 1.1, -1.3)),
          ("v2d", "kit", t(55, 4), t(57, 4), [[-1.7, 1.5, 1.4], [-1.0, 1.48, 1.2], [-0.3, 1.45, 1.25]], (1.2, 1.0, -1.0)),
          ("v2e", "liv", t(57, 4), t(59, 4), [[-1.2, 1.4, 1.9], [-0.4, 1.45, 1.6], [0.5, 1.5, 1.4]], (1.6, 1.2, -2.0)),
          ("v2f", "bed", t(59, 4), t(61, 4), [[1.25, 1.6, 1.75], [0.6, 1.55, 1.1], [0.1, 1.55, 0.8]], (-0.4, 1.2, -1.6)),
          ("v2g", "stu", t(61, 4), t(63, 4), [[1.2, 1.55, 1.4], [0.6, 1.5, 0.9], [0.0, 1.45, 0.7]], (-0.2, 1.0, -1.4)),
          ("v2h", "liv", t(63, 4), t(67), [[-0.2, 1.5, 1.9], [0.0, 1.45, 1.0], [0.1, 1.4, 0.35]], None)]
    for name, world, t0, t1, eye, look in v2:
        if name == "v2h":
            b.shot(name, t0, t1, eye, None, keys=(world,), extra=b.worlds["liv"]["names"][:0],
                   look_keys=[(t0, (-1.6, 1.0, -1.0)), (t(65, 3), (-1.0, 1.4, -1.4)), (t(66), (0.0, 2.2, -1.2)),
                              (t(66, 3), (0.1, 3.4, -0.4)), (t(67), (0.2, 6.0, 0.0))],
                   fov=[(t0, 60.0), (t(66, 3), 64.0), (t(67), 74.0)])
        else:
            b.shot(name, t0, t1, eye, look, keys=(world,), fov=62.0, sway=1.6)
    g_v2 = b.gate("gV2", t(49, 4), t(66))
    for o in ("stuShell", "stuFurn", "livShell", "livSofa", "livMedia", "livShelf", "bedShell", "bedFurn", "kitShell",
              "kitCounter", "kitTable"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 3.5, "quarter", g_v2)
        b.pulse(f"sdf/{o}/look/edge/pixels", 1.2, "eighth", g_v2)
    f.track("temporal/mosh/seed", [(t(50) + 0.27 * i, float(i), "step") for i in range(0, 130)])
    g_strain = b.gate("gStrain", t(58), t(66))
    f.route("grid.song.sixteenth", "temporal/mosh/shift", 4.0, depth=g_strain)
    # the globe has been spinning; it stops on "and the world stops turning" (113.1 s); C09 restarts it
    f.track("sdf/stuFurn/node/stuGlobe/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (113.1, [0.0, 2600.0, 0.0], "step"),
                                                  (t(51, 4), [0.0, 2600.0, 0.0], "linear"), (t(54), [0.0, 4400.0, 0.0], "linear"),
                                                  (end, [0.0, 4400.0 + 260.0 * (end - t(54)), 0.0])])
    f.track("sdf/stuShell/node/stuFan/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (end, [0.0, 300.0 * end, 0.0])])
    # the words of verse 2: more of them, smaller, scattered, arriving on the eighths
    vw = [("DO YOU WANNA HAVE FUN?", 109.8, (0.3, 2.18, -1.69), (0, 0, 1), 0.11, "flicker", "word"),
          ("AS THE FIRES KEEP BURNING", 111.4, (-1.79, 1.5, -0.2), (1, 0, 0), 0.09, "pop", "accent"),
          ("THE WORLD STOPS TURNING", 113.1, (0.75, 0.62, -1.38), (0, 0, 1), 0.035, "cut", "word"),
          ("AND EVERYONE", 114.5, (-0.2, 2.3, -2.19), (0, 0, 1), 0.12, "pop", "word"),
          ("UNDER THE SUN", 115.1, (2.59, 1.75, -1.2), (-1, 0, 0), 0.12, "pop", "accent"),
          ("TAKES STEPS IN THE PROCESS", 115.9, (2.075, 0.86, -0.17), (-1, 0, 0), 0.032, "flicker", "screen"),
          ("TO HEAL AND GROW", 117.0, (2.075, 0.74, -0.17), (-1, 0, 0), 0.04, "flicker", "screen"),
          ("TELL ME YOU'RE THE ONE", 118.7, (1.99, 1.4, 0.55), (-1, 0, 0), 0.09, "rise", "word"),
          ("TO MAKE THE WORLD", 120.3, (-0.3, 2.3, -1.79), (0, 0, 1), 0.13, "pop", "word"),
          ("STOP HURTING", 121.1, (-0.3, 2.05, -1.79), (0, 0, 1), 0.16, "flash", "accent"),
          ("AND MY HEART KEEP PUMPING", 122.0, (-1.99, 2.05, 0.3), (1, 0, 0), 0.1, "pop", "word"),
          ("WHILE EVERYONE UNDER THE SUN", 123.7, (1.0, 1.83, -1.46), (0, 0, 1), 0.045, "pop", "word"),
          ("TAKE STEPS", 125.3, (-0.7, 1.62, -1.78), (0, 0, 1), 0.12, "rise", "accent"),
          ("TO FEEL IT GROW", 126.2, (2.19, 1.5, -1.0), (-1, 0, 0), 0.1, "rise", "word"),
          ("HOW LITTLE DO I KNOW?", 127.5, (0.0, 2.35, -2.19), (0, 0, 1), 0.14, "flash", "word"),
          ("HOW LITTLE DO I KNOW?", 128.6, (2.59, 1.9, 0.9), (-1, 0, 0), 0.1, "flash", "accent"),
          ("HOW LITTLE DO I KNOW?", 129.7, (-2.59, 2.3, -0.1), (1, 0, 0), 0.12, "flash", "word"),
          ("HOW LITTLE DO I KNOW?", 130.8, (0.0, 0.03, 0.6), (0, 1, 0), 0.22, "flash", "accent"),
          ("HOW LITTLE DO I KNOW?", 132.0, (-0.3, 2.25, -1.79), (0, 0, 1), 0.15, "flicker", "word"),
          ("HOW LITTLE DO I KNOW?", 133.6, (1.99, 1.7, 0.2), (-1, 0, 0), 0.11, "flicker", "accent"),
          ("BREATHE AND GROW", 136.0, (0.3, 2.2, -1.69), (0, 0, 1), 0.15, "rise", "word"),
          ("HOW LITTLE DO I KNOW?", 138.2, (1.79, 1.9, -0.2), (-1, 0, 0), 0.1, "flash", "accent"),
          ("TELL ME IT'LL BE FINE THOUGH?", 140.6, (2.075, 0.8, -0.17), (-1, 0, 0), 0.03, "flicker", "screen"),
          ("FEEL IT GROW", 143.3, (-0.2, 2.25, -2.19), (0, 0, 1), 0.2, "rise", "word")]
    for i, (text, t0, pos, nrm, h, style, role) in enumerate(vw):
        nxt = vw[i + 1][1] if i + 1 < len(vw) else t(66)
        b.word(text, t0, max(min(t0 + 3.2, nxt + 1.2), t0 + 1.0), pos, nrm, h, style=style, role=role, name=f"v2w{i:02d}",
               tin=0.1, tout=0.15)
    # C09 (51.4) data-mosh: the frame smears (motion blur and a camera jolt) and splits, then snaps
    c = b.clap("c09", t(51, 4), release=0.9, hold=BEAT1 * 0.5)
    f.route(c, "temporal/mosh/amount", 0.75)
    f.route(c, "temporal/mosh/shift", 14.0)
    f.route(c, "post/lens/chromaticAberration", 0.6)
    f.route(c, "camera/breath/side", 0.25)
    # C10 (53.4) positional corruption: every object in the room jumps sideways by its own amount
    c = b.clap("c10", t(53, 4), release=0.5, attack=0.03)
    for o, off in (("bedShell", [0.12, 0.0, -0.05]), ("bedFurn", [-0.35, 0.08, 0.2]), ("bedMan", [0.4, 0.0, -0.3])):
        for comp, v in enumerate(off):
            if v:
                f.route(c, f"sdf/{o}/transform/position", v * 2.5, component=comp)
    f.route(c, "post/lens/chromaticAberration", 0.6)
    f.route(c, "temporal/mosh/amount", 0.3)
    # C11 (55.4) colour corruption: a 180-degree hue jump, a full channel split, the lamp pumps like a heart
    c = b.clap("c11", t(55, 4), release=0.55)
    f.route(c, "post/grade/hueShift", 3.1)
    f.route(c, "post/lens/chromaticAberration", 1.0)
    f.route(c, "lights/kitPend/intensity", 30.0)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "temporal/mosh/shift", 22.0)
    # C12 (57.4) stretch: the room stretches up to twice its height and snaps back
    c = b.clap("c12", t(57, 4), release=0.5)
    for o in ("livShell", "livSofa", "livMedia", "livShelf", "livMan", "livCeil"):
        f.route(c, f"sdf/{o}/transform/scale", 1.1, component=1)
    f.route(c, "post/bloom/intensity", 0.8)
    # C13 (59.4) the mannequin: a flash, and it is standing in the room, close, facing away; gone at C14
    c = b.clap("c13", t(59, 4), release=0.4)
    f.route(c, "camera/exposure/compensation", 2.0)
    bm0 = [0.0, 0.0, 0.0]
    close = [-1.55, 0.0, 1.95]
    f.track("sdf/bedMan/transform/position", [(0.0, bm0, "step"), (t(59, 4), close, "step"), (t(61, 4), bm0, "step")])
    # C14 (61.4) duplication: the study's furniture tiles sideways forever for one beat
    c = b.clap("c14", t(61, 4), release=0.3)
    f.route(c, "post/bloom/intensity", 1.0)
    f.track("sdf/stuFurn/transform/scale", [(0.0, [1.0, 1.0, 1.0], "step"), (t(61, 4), [0.55, 0.55, 0.55], "step"),
                                           (t(62) + 0.05, [1.0, 1.0, 1.0], "step")])
    # C15 (63.4) floor drop: the room falls a metre under the camera, every line flares white
    c = b.clap("c15", t(63, 4), release=0.6)
    f.route(c, "palette/saturation", -0.9)
    f.route(c, "post/bloom/intensity", 1.6)
    for o in ("livShell", "livSofa", "livMedia", "livShelf", "livMan"):
        f.track(f"sdf/{o}/transform/position", [(t(63, 4) - 0.002, [0.0, 0.0, 0.0], "easeIn"), (t(64) - 0.05, [0.0, -1.0, 0.0], "easeOut"),
                                                (t(64, 3), [0.0, 0.0, 0.0], "step")], mode="add")
    # C16 (65.4) lift-off, and the bridge transition (66): the ceiling's halves split (66.3) and fly (66.4)
    c = b.clap("c16", t(65, 4), release=0.6)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "camera/exposure/compensation", 1.0)
    f.track("sdf/livCeil/node/ceilL/translation", [(0.0, [0.0, 0.0, 0.0], "step"), (t(65, 4), [0.0, 0.0, 0.0], "easeOut"),
                                                   (t(65, 4) + 0.3, [0.0, 0.35, 0.0], "smooth"), (t(66, 3), [0.0, 0.35, 0.0], "easeOut"),
                                                   (t(66, 3) + 0.25, [-1.6, 0.9, 0.0], "smooth"), (t(66, 4), [-1.8, 1.1, 0.0], "easeIn"),
                                                   (t(67), [-9.0, 30.0, 0.0], "step")])
    f.track("sdf/livCeil/node/ceilR/translation", [(0.0, [0.0, 0.0, 0.0], "step"), (t(65, 4), [0.0, 0.0, 0.0], "easeOut"),
                                                   (t(65, 4) + 0.3, [0.0, 0.35, 0.0], "smooth"), (t(66, 3), [0.0, 0.35, 0.0], "easeOut"),
                                                   (t(66, 3) + 0.25, [1.6, 0.9, 0.0], "smooth"), (t(66, 4), [1.8, 1.1, 0.0], "easeIn"),
                                                   (t(67), [9.0, 30.0, 0.0], "step")])
    for fill_t in (t(66, 3), t(66, 4)):
        c = b.clap(f"fill{int(fill_t * 100)}", fill_t, release=0.35)
        f.route(c, "camera/exposure/compensation", 1.2)
        f.route(c, "post/bloom/intensity", 0.9)
    for i, (wd, beat, pos) in enumerate((("FEEL", 1.0, (-0.9, 2.45, -1.0)), ("IT", 1.5, (0.0, 2.6, -0.5)), ("GROW", 2.0, (0.9, 2.45, -1.0)),
                                         ("FEEL", 3.0, (-1.4, 2.0, 2.19)), ("IT", 3.5, (2.59, 2.2, 0.0)), ("GROW", 4.0, (0.0, 2.6, 0.6)))):
        nrm = (0, 0, -1) if pos[2] > 2.0 else ((-1, 0, 0) if pos[0] > 2.5 else (0, -1, 0))
        b.word(wd, t(66, beat), t(67), pos, nrm, 0.3, style="rise", role="word", tilt=0,
               name=f"fig66_{i}", tin=0.15)

    # =========================================================================================================
    # BRIDGE 1 (67-74): the house grows into a tree of rooms; each GROW blooms one; the camera rises with them
    # =========================================================================================================
    blooms = []
    for i in range(OD.TREE_ROOMS):
        bar = 66 + i // 2
        beat = 2.0 if i % 2 == 0 else 4.0
        blooms.append(t(bar, beat))
    groups = {range(0, 5): "treeA", range(5, 10): "treeB", range(10, 14): "treeC"}
    for i, tb in enumerate(blooms):
        obj = next(v for r, v in groups.items() if i in r)
        f.track(f"sdf/{obj}/node/gr{i}/scale", [(0.0, 0.0, "step"), (tb - 0.001, 0.0, "easeOut"), (tb + 0.18, 1.12, "smooth"),
                                                (tb + 0.42, 1.0, "step")])
    # the camera spirals up the trunk with the blooms, then pulls out to see the whole tree; then it lands
    spiral = []
    for k in range(0, 9):
        a = math.radians(20.0 + 22.0 * k)
        r = 6.6
        y = 1.8 + 2.15 * k * 1.25
        spiral.append([r * math.cos(a), y, -r * math.sin(a)])
    look_tree = []
    for i, tb in enumerate(blooms):
        if t(67) - 0.01 <= tb <= t(71) + 0.01:
            (x, y, z), _yaw = OD.tree_room_pose(i)
            look_tree.append((tb, (x * 0.45, y + 0.4, z * 0.45)))
    look_tree = [(t(67), (0.0, 7.0, 0.0))] + look_tree + [(t(71), (0.0, 22.0, 0.0))]
    b.shot("tree", t(67), t(71), spiral, None, keys=("tree", "land", "sky"), look_keys=look_tree, fov=68.0, ease_kind="linear")
    b.shot("treeReveal", t(71), t(73), [[10.0, 18.0, 12.0], [17.0, 20.0, 19.0], [24.0, 21.0, 26.0]], None, keys=("tree", "land", "sky"),
           look_keys=[(t(71), (0.0, 17.0, 0.0)), (t(73), (0.0, 14.0, 0.0))], fov=62.0)
    r12, yaw12 = OD.tree_room_pose(12)
    win = [r12[0] * 1.32, r12[1] + 1.0, r12[2] * 1.32]
    b.shot("treeLand", t(73), t(75), [[20.0, 30.0, 18.0], [12.0, r12[1] + 3.5, 10.0], [win[0] * 1.6, win[1] + 0.4, win[2] * 1.6], win],
           None, keys=("tree", "land", "sky"), look_keys=[(t(73), (0.0, 22.0, 0.0)), (t(74), tuple(r12)), (t(75), tuple(r12))],
           fov=[(t(73), 60.0), (t(75), 70.0)], ease_kind="inout")
    for i, tb in enumerate(blooms):
        (x, y, z), yaw = OD.tree_room_pose(i)
        L = math.hypot(x, z)
        nrm = (x / L, 0.0, z / L)
        pos = (x + nrm[0] * 1.03, y + 0.9, z + nrm[2] * 1.03)
        b.word("FEEL IT GROW", tb - BEAT1, min(tb + 3.0, t(73)), pos, nrm, 0.16, style="pop", role="word", name=f"grow{i:02d}",
               tin=0.2, intensity=4.0)
    g_b1 = b.gate("gB1", t(67), t(73))
    for o in ("treeA", "treeB", "treeC", "treeTrunk"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 2.0, "half", g_b1)
        f.track(f"sdf/{o}/surface/{K.GLOW}/emission", hold_keys(0.0, end, [4.0, 4.0, 4.0]))
    b.val += [(t(73), 1.0, "smooth"), (t(74, 4), 0.55, "smooth"), (t(75) - 0.01, 0.55, "step"), (t(75), 1.0, "step")]

    # =========================================================================================================
    # BRIDGE 2 (75-82): the room of objects. One slow orbit; each eighth turns every object; each word lights one
    # =========================================================================================================
    orbit = []
    for k in range(7):
        a = math.radians(15.0 + 15.0 * k)
        orbit.append([3.6 * math.sin(a), 1.55 + 0.05 * k, 3.6 * math.cos(a)])
    b.shot("gallery", t(75), t(83), orbit, (0.0, 0.95, 0.0), keys=("gal",), fov=60.0, ease_kind="linear",
           moves=[(t(75), 0.0), (t(82, 3), 1.0), (t(83), 1.0)])
    items = [it[0] for it in RM.GALLERY_ITEMS]
    for n_i, name in enumerate(items):
        keys_ = [(0.0, [0.0, 0.0, 0.0], "step")]
        for e_ in range(64):
            bar, beat = 75 + e_ // 8, 1.0 + (e_ % 8) * 0.5
            if (bar, beat) >= (82, 3.0):
                break
            te = t(bar, beat)
            ang = 45.0 * (e_ + 1) * (1 if n_i % 2 == 0 else -1)
            keys_ += [(te - 0.001, keys_[-1][1], "easeOut"), (te + 0.09, [0.0, ang, 0.0], "step")]
        f.track(f"sdf/{name}/node/{name}Spin/rotation", keys_)
    # IS THAT ALL YOU: IS on 4.5, THAT 1, ALL 1.5, YOU 2 (GOT? on 78.2.5); four walls; each word lights an object
    walls = [((0.0, 2.0, -3.49), (0, 0, 1)), ((3.49, 2.0, 0.0), (-1, 0, 0)), ((0.0, 2.0, 3.49), (0, 0, -1)), ((-3.49, 2.0, 0.0), (1, 0, 0))]
    wi = 0
    for bar in range(75, 83):
        seq = [("IS", bar - 1, 4.5), ("THAT", bar, 1.0), ("ALL", bar, 1.5), ("YOU", bar, 2.0)]
        if bar == 78:
            seq.append(("GOT?", bar, 2.5))
        for j, (wd, wb, bt) in enumerate(seq):
            wall_pos, nrm = walls[(bar - 75 + j) % 4]
            off = (j - 1.5) * 0.9
            pos = (wall_pos[0] + (off if nrm[0] == 0 else 0.0), wall_pos[1] - 0.25 * (j % 2),
                   wall_pos[2] + (off if nrm[2] == 0 else 0.0))
            te = t(wb, bt)
            b.word(wd + ("..." if bar == 82 and wd == "YOU" else ""), te, te + G.BAR2 * 0.9, pos, nrm,
                   0.42 if wd != "GOT?" else 0.7, style="flash" if wd != "GOT?" else "pop", role="word",
                   name=f"isth{wi:02d}", intensity=4.0 if wd != "GOT?" else 7.0, tin=0.1, tout=0.3)
            target = items[(wi * 4 + j) % len(items)]
            c = b.clap(f"lit{wi:02d}", te, release=0.45)
            f.route(c, f"sdf/{target}/look/edge/intensity", 12.0)
            wi += 1
    f.track("sdf/galShell/look/edge/intensity", hold_keys(0.0, end, 2.0))
    # the eighth thump also pulses the room's lines lightly; the pulse stops on 82.3 with everything
    g_b2 = b.gate("gB2", t(75), t(82, 3))
    b.pulse("sdf/galShell/look/edge/intensity", 2.5, "eighth", g_b2)
    for name in items:
        b.pulse(f"sdf/{name}/look/edge/intensity", 4.0, "eighth", g_b2)
    b.pulse("sdf/galMan/look/edge/intensity", 2.0, "quarter", g_b2)
    # C17 (78.2.5) GOT?: a colourful sparkle explosion
    c = b.clap("c17", t(78, 2.5), release=1.2)
    f.route(c, "post/grade/hueShift", 2.2)
    f.route(c, "post/bloom/intensity", 2.4)
    f.route(c, "palette/saturation", 0.8)
    for name in items:
        f.route(c, f"sdf/{name}/look/edge/intensity", 18.0)
        f.route(c, f"sdf/{name}/look/edge/pixels", 3.0)
    # C18 (82.4): everything falls into the mannequin's chair, then one beat of black
    c = b.clap("c18", t(82, 4), release=0.2, attack=BEAT2 * 0.5)
    for name in items:
        f.track(f"sdf/{name}/transform/scale", [(0.0, [1.0, 1.0, 1.0], "step"), (t(82, 4) - 0.2, [1.0, 1.0, 1.0], "easeIn"),
                                               (t(82, 4) + 0.05, [0.05, 0.05, 0.05], "step"), (t(83), [1.0, 1.0, 1.0], "step")])
    b.val += [(t(82, 4) + 0.02, 1.0, "step"), (t(82, 4) + 0.06, 0.0, "step"), (t(83) - 0.001, 0.0, "step"), (t(83), 1.0, "step")]
    f.route(c, "post/bloom/intensity", 2.0)

    # =========================================================================================================
    # BRIDGE 3 (83-90): the house dances. Floors light in patterns, furniture hops; the camera only glides
    # =========================================================================================================
    dance = [("d1", ("liv", "discoLiv"), t(83), t(85), [[-2.0, 1.2, 1.8], [-0.5, 1.0, 1.9], [1.2, 1.3, 1.5], [1.9, 1.6, 0.2]],
              [(t(83), (-1.2, 0.6, -0.8)), (t(84), (0.0, 0.8, -1.4)), (t(85), (-1.8, 0.8, -0.2))]),
             ("d2", ("kit", "discoKit"), t(85), t(87), [[1.8, 1.7, 1.5], [0.5, 1.9, 1.6], [-1.2, 1.5, 1.4], [-1.8, 1.2, 0.2]],
              [(t(85), (0.0, 0.8, 0.4)), (t(86), (-0.6, 1.0, -1.0)), (t(87), (0.8, 0.9, -0.6))]),
             ("d3", ("bed", "discoBed"), t(87), t(89), [[1.6, 1.3, 1.5], [0.4, 1.0, 1.5], [-1.4, 1.4, 1.3], [-1.6, 1.8, -0.2]],
              [(t(87), (-0.3, 0.5, -0.9)), (t(88), (1.4, 1.0, -1.0)), (t(89), (0.4, 0.6, -1.2))]),
             ("d4", ("liv", "discoLiv"), t(89), t(91), [[1.9, 1.6, -0.9], [1.4, 1.5, 0.8], [0.4, 1.45, 1.4], [-0.4, 1.4, 1.7]],
              [(t(89), (-1.5, 0.7, -0.4)), (t(90), (-0.6, 1.2, 1.6)), (t(90, 3), (-0.6, 1.5, 2.6)), (t(91), (-0.6, 1.5, 3.2))])]
    for name, keys_, t0, t1, eye, lk in dance:
        b.shot(name, t0, t1, eye, None, keys=keys_, look_keys=lk, fov=66.0, ease_kind="linear")
    for disc in ("livDisco", "kitDisco", "bedDisco"):
        for k_, ch in ((K.CANVAS, "discoA"), (K.CANVAS2, "discoB")):
            f.route(f"grid.song.{ch}", f"sdf/{disc}/surface/{k_}/emission", 2.2)
        f.track(f"sdf/{disc}/surface/{K.CANVAS}/emission", hold_keys(0.0, end, [0.15, 0.15, 0.15]))
        f.track(f"sdf/{disc}/surface/{K.CANVAS2}/emission", hold_keys(0.0, end, [0.15, 0.15, 0.15]))
    f.event("discoA", at="bridge3:1:1", release=0.85, units="beats", repeat={"every": 2, "count": 16})
    f.event("discoB", at="bridge3:1:2", release=0.85, units="beats", repeat={"every": 2, "count": 16})
    g_b3 = b.gate("gB3", t(83), t(90, 3))
    for node, obj, h in (("livTable", "livSofa", 0.12), ("livArm", "livMedia", 0.1), ("livPlant", "livShelf", 0.14),
                         ("kitChairA", "kitTable", 0.16), ("kitChairB", "kitTable", 0.16), ("kitPlate", "kitTable", 0.1),
                         ("kitKettle", "kitCounter", 0.12)):
        f.route("grid.song.quarter", f"sdf/{obj}/node/{node}/translation", h, component=1, depth=g_b3)
    f.route("grid.song.half", "sdf/kitTable/node/kitPendant/rotation", 18.0, component=2, depth=g_b3, polarity="bipolar")
    f.route("grid.song.half", "sdf/bedShell/node/bedPendant/rotation", 18.0, component=0, depth=g_b3, polarity="bipolar")
    for o in ("livShell", "kitShell", "bedShell", "livSofa", "livMedia", "livShelf", "kitCounter", "kitTable", "bedFurn"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 2.5, "quarter", g_b3)
    # IS THAT ALL? bouncing across the floors; IS THAT ALL YOU GOT? across a wall on bars 84 and 88
    for bar in range(83, 91):
        room_floor = {83: (0.0, 0.03, 0.3), 84: (0.0, 0.03, 0.3), 85: (0.0, 0.03, 1.1), 86: (0.6, 0.03, 1.2),
                      87: (0.2, 0.03, 0.9), 88: (0.2, 0.03, 0.9), 89: (0.0, 0.03, 0.5), 90: (0.0, 0.03, 0.8)}[bar]
        if bar in (84, 88):
            wall = (-0.2, 1.9, -2.19) if bar == 84 else (-0.3, 2.1, -1.79)
            b.word("IS THAT ALL YOU GOT?", t(bar - 1, 4.5), t(bar, 4.5), wall, (0, 0, 1), 0.22, style="pop", role="word",
                   name=f"b3w{bar}", tin=0.12)
        else:
            b.word("IS THAT ALL?", t(bar - 1, 4.5), t(bar, 4.0), room_floor, (0, 1, 0), 0.3, style="pop", role="accent",
                   name=f"b3w{bar}", tilt=(bar * 23) % 40 - 20, tin=0.12)
            f.route("grid.song.quarter", f"nodes/b3w{bar}/position", 0.25, component=1, depth=f"grid.song.b3w{bar}")
    # THE SWEEP (90.3-90.4 -> 91.1): a rainbow band washes across everything, rising to white; the bass fill kicks
    f.track("post/sweep/progress", [(0.0, 0.0, "step"), (t(90, 3) - 0.01, 0.0, "linear"), (t(91), 1.0, "step"), (end, 1.0, "step")])
    f.track("post/sweep/wash", [(0.0, 0.0), (t(90, 2.5), 0.0, "smooth"), (t(90, 3), 0.85, "smooth"), (t(91), 0.6, "smooth"),
                                (t(91) + 0.5, 0.0, "step")], mode="add")
    f.track("post/sweep/intensity", [(0.0, 0.0), (t(90, 2.5), 0.0, "smooth"), (t(90, 3), 1.6, "smooth"), (t(90, 4.5), 3.0, "smooth"),
                                     (t(91) + 0.4, 0.0, "step")], mode="add")
    f.track("post/sweep/trail", [(0.0, 0.5), (end, 0.5)])
    f.track("post/sweep/span", [(0.0, 1.0), (end, 1.0)])
    f.track("post/sweep/width", [(0.0, 0.32), (end, 0.32)])
    f.track("post/sweep/angle", [(0.0, -20.0), (end, -20.0)])
    f.track("post/grade/hueShift", [(0.0, 0.0), (t(90, 3) - 0.002, 0.0, "step"), (t(90, 3), -1.2, "linear"), (t(91) - 0.02, 1.2, "step"),
                                    (t(91), 0.0, "step")], mode="add")
    b.val += [(t(90, 3), 1.0, "smooth"), (t(90, 4.6), 2.2, "smooth"), (t(91) - 0.02, 3.2, "step"), (t(91), 1.0, "smooth")]
    c = b.clap("sweep", t(91), attack=G.BAR2 / 2, release=0.6, curve="smooth")
    f.route(c, "camera/exposure/compensation", 2.0)
    f.route(c, "post/bloom/intensity", 1.8)
    f.route(c, "palette/saturation", 0.8)
    c = b.clap("bassfill", t(90, 4), release=0.4)
    f.route(c, "camera/breath/forward", 0.6)

    # =========================================================================================================
    # FINAL CHORUS (91-114): the open. The landscape at night, the rooms as lanterns; the stair; the summit
    # =========================================================================================================
    gy = OD.ground
    chA = [[0.0, gy(0, 46) + 2.2, 46.0], [2.0, gy(2, 34) + 2.6, 34.0], [-1.5, gy(-1.5, 22) + 4.0, 22.0], [-4.0, gy(-4, 10) + 7.5, 10.0],
           [-2.0, gy(-2, 0) + 10.0, 2.0]]
    b.shot("chorusA", t(91), t(99), chA, None, keys=("land", "sky"),
           look_keys=[(t(91), (0.0, 3.0, 0.0)), (t(93), (-3.0, 5.0, -20.0)), (t(95), (6.0, 8.0, -30.0)), (t(97), (-10.0, 9.0, -20.0)),
                      (t(99), (-14.0, 4.0, 0.0))], fov=[(t(91), 72.0), (t(93), 64.0), (t(99), 62.0)], ease_kind="linear")
    # LET IT GO, twice a bar, written across the hills: giant letters lying on the slopes
    k = 0
    for bar in range(91, 99):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            for j, wd in enumerate(("LET", "IT", "GO")):
                zc = 40.0 - (bar - 91) * 5.0 - half * 2.5
                xc = (-6.0 + 6.0 * j) * (1 if (bar + half) % 2 == 0 else -1)
                pos = (xc, gy(xc, zc - 6.0) + 0.06, zc - 6.0 - 1.5 * j)
                b.word(wd, t(bar, b0 + 0.5 * j), t(bar, b0 + 0.5 * j) + G.BAR2 * 1.5, pos, (0, 1, 0.25), 1.6, style="flash",
                       role="word" if j != 1 else "accent", name=f"chA{k:02d}", intensity=5.0, tin=0.1, tout=0.35,
                       tilt=-10 + 20 * ((k * 7) % 3) / 2.0)
                k += 1
    g_ch = b.gate("gChorus", t(91), t(113))
    b.pulse("sdf/landGround/look/edge/intensity", 2.0, "quarter", g_ch)
    b.pulse("sdf/ringLow/look/edge/intensity", 4.0, "quarter", g_ch)
    b.pulse("sdf/ringHigh/look/edge/intensity", 4.0, "half", g_ch)
    f.route("grid.song.quarter", "post/grade/hueShift", 0.22, depth=g_ch)
    f.track("sdf/ringLow/node/ringLowRot/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (end, [0.0, 9.0 * end, 0.0])])
    f.track("sdf/ringHigh/node/ringHighRot/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (end, [0.0, -6.0 * end, 0.0])])
    for i in range(5):
        rate = [11.0, -8.0, 6.0, -14.0, 9.0][i]
        f.track(f"sdf/skyFurn/node/sf{i}/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"),
                                                    (end, [rate * end * 0.3, rate * end, rate * end * 0.2])])
    b.pulse("sdf/skyFurn/look/edge/intensity", 3.0, "quarter", g_ch)
    # bars 99-106: the staircase; IT'S JUST STEPS IN A PROCESS on the risers, FOR YOUR LIFE in the sky
    sx0 = -14.0
    sy0 = gy(-14, 0) - 0.2
    slope = 0.32 / 0.45

    def on_stair(dx, z):
        return [sx0 + dx, sy0 + min(max(dx, 0.0), 18.0) * slope + 1.85, z]
    stair_eye = [on_stair(-6.0, 2.4), on_stair(-1.5, 1.4), on_stair(3.0, 0.6), on_stair(8.0, 0.2), on_stair(13.0, 0.0),
                 on_stair(17.0, 0.0), on_stair(20.5, 0.0)]
    b.shot("stair", t(99), t(107), stair_eye, None, keys=("land", "sky"),
           look_keys=[(t(99), (sx0 + 6.0, sy0 + 3.0, 0.0)), (t(103), (sx0 + 16.0, sy0 + 10.0, 0.0)), (t(107), (10.0, 14.0, 0.0))],
           fov=64.0, ease_kind="linear")
    words = ["IT'S", "JUST", "STEPS", "IN", "A", "PROCESS"]
    for rep, bar in enumerate((99, 103)):
        for i, wd in enumerate(words):
            step = 6 + rep * 14 + i * 2
            x = sx0 + step * 0.45
            y = sy0 + step * 0.32 + 0.16
            b.word(wd, t(bar - 1, 4.0) + i * 0.3, t(bar + 3, 4.0), (x + 0.004, y, 0.0), (-1, 0, 0), 0.2, style="pop",
                   role="word", name=f"steps{rep}{i}", rotation=[0.0, -90.0, 0.0])
        b.word("FOR YOUR LIFE", t(bar, 3.0), t(bar + 3, 4.0), (sx0 + 24.0, sy0 + 19.0, 0.0), (-1, 0.2, 0), 1.4, style="rise",
               role="accent", name=f"fyl{rep}", intensity=5.0, tin=0.4, rotation=[0.0, -90.0, 0.0])
    b.pulse("sdf/stair/look/edge/intensity", 3.0, "quarter", g_ch)
    # bars 107-112: the summit; LET IT GO chants in the ring; fireworks of geometry on 2 and 4
    sum_eye = [[5.5, 16.0, 3.5], [8.5, 16.1, 4.8], [12.5, 16.2, 3.6], [14.5, 16.3, -0.8]]
    b.shot("summit", t(107), t(114), sum_eye, None, keys=("land", "sky", "tree"),
           look_keys=[(t(107), (10.0, 15.5, -4.0)), (t(109), (0.0, 22.0, 0.0)), (t(111), (-10.0, 20.0, -10.0)),
                      (t(112, 3), (10.0, 15.0, 0.0)), (t(113), (10.0, 15.0, 0.0)), (t(114), (10.0, 18.0, 0.0))],
           fov=[(t(107), 66.0), (t(112, 3), 66.0), (t(113), 50.0), (t(113) + 0.4, 84.0), (t(114), 76.0)], ease_kind="linear",
           moves=[(t(107), 0.0), (t(113), 0.92), (t(114), 1.0)])
    k = 0
    for bar in range(107, 113):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            for j, wd in enumerate(("LET", "IT", "GO")):
                a = math.radians((bar - 107) * 60.0 + half * 30.0 + j * 10.0)
                pos = (10.0 + 9.0 * math.cos(a), 18.6 + 1.0 * j, -9.0 * math.sin(a))
                nrm = (-math.cos(a), 0.0, math.sin(a))
                b.word(wd, t(bar, b0 + 0.5 * j), t(bar, b0 + 0.5 * j) + G.BAR2, pos, nrm, 0.9, style="flash", role="word",
                       name=f"chC{k:02d}", intensity=6.0, tin=0.08, tout=0.3)
                k += 1
    for bar in range(107, 113):
        for bt in (2.0, 4.0):
            c = b.clap(f"fw{bar}{int(bt)}", t(bar, bt), release=0.5)
            f.route(c, "sdf/ringLow/look/edge/intensity", 10.0)
            f.route(c, "post/bloom/intensity", 0.6)
    # the build (112.3, 112.4): everything is pulled toward the centre in two jolts
    for o in ("ringLow", "ringHigh"):
        f.track(f"sdf/{o}/transform/scale", [(0.0, [1.0, 1.0, 1.0], "step"), (t(112, 3), [1.0, 1.0, 1.0], "easeOut"),
                                            (t(112, 3) + 0.12, [0.7, 1.0, 0.7], "step"), (t(112, 4), [0.7, 1.0, 0.7], "easeOut"),
                                            (t(112, 4) + 0.12, [0.4, 1.0, 0.4], "step"), (t(113), [0.4, 1.0, 0.4], "easeOut"),
                                            (t(113) + 0.6, [4.0, 1.0, 4.0], "smooth"), (t(115), [7.0, 1.0, 7.0], "step")])
    for bt in (3.0, 4.0):
        c = b.clap(f"build{int(bt)}", t(112, bt), release=0.4)
        f.route(c, "camera/exposure/compensation", 1.0)
        f.route(c, "camera/breath/forward", 0.5)
    # THE CRASH (113.1): a supernova; it rings out over two bars
    c = b.clap("crash", t(113), release=4.0)
    f.route(c, "camera/exposure/compensation", 2.8)
    f.route(c, "post/bloom/intensity", 3.0)
    f.route(c, "palette/saturation", 1.0)
    f.route(c, "post/lens/chromaticAberration", 0.8)
    f.route(c, "post/grade/hueShift", 3.1)
    for o in ("ringLow", "ringHigh", "landGround", "landTrees", "landMtn", "stair", "summit", "treeA", "treeB", "treeC"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 16.0)

    # =========================================================================================================
    # ENDING (114-end): dawn on the hill; the mannequin watches; the world washes to white and collapses into
    # the cursor it began as
    # =========================================================================================================
    dawn_eye = [[3.0, gy(3, -8) + 1.9, -8.0], [2.0, gy(2, -10) + 1.85, -10.5], [1.2, gy(1.2, -12) + 1.8, -12.6]]
    b.shot("dawn", t(114), 255.0, dawn_eye, None, keys=("land", "sky", "hillman"),
           look_keys=[(t(114), (0.0, 1.0, -40.0)), (252.0, (0.0, 3.0, -60.0)), (255.0, (0.0, 4.0, -80.0))],
           fov=[(t(114), 60.0), (255.0, 54.0)])
    f.track("sdf/sun/node/sunAt/translation", [(0.0, [0.0, -30.0, 0.0], "step"), (t(114), [0.0, -18.0, 0.0], "smooth"),
                                              (253.5, [0.0, 4.0, 0.0], "smooth"), (end, [0.0, 10.0, 0.0])])
    b.val += [(t(114), 1.0, "smooth"), (253.0, 1.6, "smooth"), (254.8, 4.5, "smooth"), (255.0, 5.0, "step"),
              (255.01, 1.0, "step")]
    f.track("camera/exposure/compensation", [(0.0, 0.0), (252.0, 0.0, "smooth"), (254.9, 2.6, "step"), (255.0, 0.0, "step")], mode="add")
    f.track("temporal/mosh/amount", [(0.0, 0.0), (251.6, 0.0, "smooth"), (253.8, 0.55, "smooth"), (254.95, 0.95, "step"),
                                     (255.0, 0.0, "step")], mode="add")
    f.track("temporal/mosh/shift", [(0.0, 0.0), (252.5, 0.0, "smooth"), (254.95, 30.0, "step"), (255.0, 0.0, "step")], mode="add")
    # the collapse: the seed again, the cursor blinking twice, then out
    b.shot("cursor", 255.0, end, [[0.0, 0.25, 6.2], [0.0, 0.25, 6.15]], (0.0, 0.0, 0.0), keys=("seed",), fov=46.0)
    zero = [0.0, 0.0, 0.0]
    for node, field in (("cell", "size"), ("ray", "size"), ("ringCube", "size"), ("floor", "size")):
        f.track(f"sdf/seed/node/{node}/{field}", [(254.9, zero, "step")], mode="replace") if False else None
    for node in ("frame", "frameX", "frameY", "frameZ", "inner", "innerX", "innerY", "innerZ", "cell", "ray", "ringCube", "floor"):
        f.track(f"sdf/seed/node/{node}/size", [(0.0, zero, "step"), (end, zero, "step")], mode="multiply") if False else None
    b.palette_at(255.0, "P0boot")
    b.breath += [(t(113), 0.0), (end, 0.0)]


def disco_floor(ext, tile=0.6):
    """A dance floor laid on a room's floor: a checkerboard of glowing tiles in two sets (surfaces CANVAS and
    CANVAS2), keyed to light on alternate beats (about 16 nodes)."""
    (x0, x1), (y0, _), (z0, z1) = ext
    a = tile
    tile_box = K.box([a / 2 - 0.03, 0.01, a / 2 - 0.03])
    setA = K.U(K.repeat([2 * a, 0, 2 * a], 0, tile_box), K.T([a, 0, a], K.repeat([2 * a, 0, 2 * a], 0, K.box([a / 2 - 0.03, 0.01, a / 2 - 0.03]))))
    setB = K.U(K.T([a, 0, 0], K.repeat([2 * a, 0, 2 * a], 0, K.box([a / 2 - 0.03, 0.01, a / 2 - 0.03]))),
               K.T([0, 0, a], K.repeat([2 * a, 0, 2 * a], 0, K.box([a / 2 - 0.03, 0.01, a / 2 - 0.03]))))
    clip = K.box([(x1 - x0) / 2 - 0.05, 0.05, (z1 - z0) / 2 - 0.05])
    return K.T([(x0 + x1) / 2, y0 + 0.012, (z0 + z1) / 2],
               K.U(K.S(K.I(setA, clip), K.CANVAS), K.S(K.I(setB, K.box([(x1 - x0) / 2 - 0.05, 0.05, (z1 - z0) / 2 - 0.05])), K.CANVAS2)))
