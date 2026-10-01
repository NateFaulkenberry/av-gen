"""All You Got, art pass 3: owner bars 67 to the end (147.52-258 s), on Builder3. Pass 2's second half kept where the
owner liked it, changed where the addendum asks:

  67-74   bridge 1, the house grows into a tree of rooms (pass 2's, unchanged in design)
  75-82   bridge 2, the room of objects: the mannequin sits among the house's things, now a still tableau (no head
          turn: section 18); the objects turn on the eighths (rotations, not jumps)
  83-90   bridge 3, the house dances: through the pass 3 house this time, the living room, the kitchen and the hall to
          the front door, without A-B-A; nothing hops (section 28): the furniture rocks and turns on smooth waves, the
          floors light in patterns, the lamps swing
  90.3    the colour wave through the hall (section 30): see `colour_wave()`
  91-114  the open, the stair into the sky, the summit, the crash (pass 2's)
  114-end dawn: the mannequin on the hill (the head now drawn and inside its march box); the world washes out and
          breaks up; it ends where it began, on the road's four dashes in the black, which go out one by one
"""

from __future__ import annotations

import math

import kit as K
import outdoor2 as OD
import pass2_grid as G
import rooms2 as RM
import rooms3 as R3
import tableaux3 as TB
from film_build2 import disco_floor

t = G.t
BEAT1 = G.BAR1 / 4.0
BEAT2 = G.BAR2 / 4.0


def hold_keys(t0, t1, v):
    return [(t0, v, "step"), (t1, v, "step")]


def gallery_room():
    """Bridge 2's room of objects (pass 2's gallery) with the pass 3 figure: a named, sealed room (no door: a room of
    the mind), each object on a pedestal tagged as a surface it rests on, the clock lying face up on its stand, and
    the mannequin in the middle on a chair (the gallery tableau). 7 x 3.2 x 7 m."""
    import liminal_space as ls
    from kit import R as R_, T as T_, U as U_, X as X_, place as place_
    ext = ((-3.5, 3.5), (0.0, 3.2), (-3.5, 3.5))
    (x0, x1), (y0, y1), (z0, z1) = ext
    shell = U_(K.D(K.shell(ext, 0.15, entity={"id": "gallery", "sealed": True})), K.tiles(ext, 0.7), K.skirting(ext),
               K.wall_band(ext, 2.4, 2.45, 0.02))
    objs = [("galShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5))]
    anchors = {}
    for name, builder, r, ang, h, role in RM.GALLERY_ITEMS:
        a = math.radians(ang)
        px, pz = r * math.sin(a), r * math.cos(a)
        ped = ls.tag(X_(((-0.22, 0.22), (0.0, h), (-0.22, 0.22))), "shelf", id=f"{name}Plinth", room="gallery") if h > 0.05 else None
        item = R_([0, 0, 0], builder(), name=name + "Spin")
        if name in ("galClock", "galFrame"):
            ls.tag(item, "prop", id={"galClock": "GalleryClock", "galFrame": "GalleryFrame"}[name], room="gallery")
        body = U_(*(c for c in (ped, T_([0, h, 0], item)) if c is not None))
        tree = place_(body, (px, 0.0, pz), ang + 180.0)
        objs.append((name, tree, role, (px - 1.2, -0.05, pz - 1.2), (px + 1.2, h + 1.9, pz + 1.2)))
        anchors[name] = (px, h + 0.4, pz)
    chair = K.place(K.chair(entity={"id": "GalleryChair", "room": "gallery"}), (0.0, 0.0, 0.0), 180.0)
    tree, lo, hi = TB.placed("gallery", "galMan", (0.0, 0.0, 0.0), 180.0, room="gallery", anchor_id="GalleryChair")
    objs.append(("galSeat", chair, "furn", (-0.5, -0.05, -0.5), (0.5, 1.0, 0.5)))
    objs.append(("galMan", tree, "figure", lo, hi, {"figure": True}))
    return {"id": "gallery", "interior": ext, "objects": objs, "lights": [("galKey", (0.0, 2.9, 0.0), "lamp")],
            "anchors": dict(anchors, man=(0.0, 1.0, 0.0), centre=(0.0, 1.2, 0.0))}


def build(b):
    f = b.f
    end = f.end
    far = {"max_distance": 260.0}
    land = {"objects": [
        ("landGround", OD.terrain(), "wall", (-170, -12, -170), (170, 6, 170), dict(far, edge_pixels=1.8, step_scale=0.55, edge_intensity=5.0)),
        ("landMtn", OD.mountains(), "furn2", (-130, -10, -175), (130, 60, -90), far),
        ("landTrees", OD.grove([("pine", -7, -9, 3.4), ("pine", -10, -14, 4.2), ("round", 8, -11, 3.6), ("pine", 13, -19, 4.6),
                                ("round", -16, -22, 4.2), ("pine", 4, -26, 4.0), ("round", -22, 6, 3.8), ("pine", 20, 4, 4.4),
                                ("pine", -26, -6, 5.0)]), "furn", (-34, -6, -34), (34, 12, 12), far),
        ("landStones", K.U(OD.stones([(-3, -5, 0.7), (4, -7, 0.5), (9, -4, 0.9), (-8, 3, 0.8)]),
                           OD.tufts([(-1.5, -3), (1.2, -4), (2.6, -2.5), (-2.8, -6), (5.5, 0.5), (-5.2, 1.5)])),
         "furn2", (-14, -6, -12), (14, 4, 6), far)], "lights": []}
    b.world("land", land)
    tree = {"objects": [("treeTrunk", K.U(OD.tree_trunk(), OD.tree_canopy()), "wall", (-3.5, -2.5, -3.5), (3.5, 40, 3.5), far),
                        ("treeA", OD.tree_branch_group(range(0, 5)), "furn", (-6, -1, -6), (6, 13, 6), far),
                        ("treeB", OD.tree_branch_group(range(5, 10)), "furn2", (-6, 9, -6), (6, 24, 6), far),
                        ("treeC", OD.tree_branch_group(range(10, 14)), "furn", (-6, 20, -6), (6, 34, 6), far)], "lights": []}
    b.world("tree", tree)
    rings = {"objects": [("ringLow", OD.lantern_ring(16.0, 7, 9.0, "ringLowRot"), "furn2", (-22, 4, -22), (22, 16, 22), far),
                         ("ringHigh", OD.lantern_ring(24.0, 9, 22.0, "ringHighRot"), "furn", (-30, 16, -30), (30, 30, 30), far)],
             "lights": []}
    b.world("rings", rings)
    sky = {"objects": [("stair", K.T([-14.0, OD.ground(-14, 0) - 0.2, 0.0], OD.sky_stair(40, 0.45, 0.32, 2.4)), "furn2",
                        (-15, -3, -3), (5, 15, 3), far),
                       ("summit", K.T([10.0, 14.2, 0.0], OD.summit()), "wall", (3, 12.5, -7), (17, 16.0, 7), far),
                       ("sun", K.T([8.0, -8.0, -170.0], K.T([0, 0, 0], OD.sun_disc(8.0), name="sunAt")), "furn2",
                        (-22, -50, -180), (38, 60, -160), far),
                       ("skyFurn", OD.sky_furniture(), "furn", (-17, 5, -21), (15, 18, -1), far)], "lights": []}
    b.world("sky", sky)
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
    gal = gallery_room()
    b.room_world("gal", gal)
    hill = {"objects": []}
    htree, hlo, hhi = TB.placed("hill", "hillMan", (0.0, OD.ground(0.0, -18.0), -18.0), 180.0, room=None)
    hill["objects"].append(("hillMan", htree, "figure", hlo, hhi, dict(far, figure=True)))
    b.room_world("hillman", hill)

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
    trunk = [(0.0, [3.0, 3.0, 3.0], "step")]
    for i, tb in enumerate(blooms):
        h = 1.2 + OD.TREE_STEP * i + 3.2 + 2.0
        trunk.append((tb - 0.001, trunk[-1][1], "easeOut"))
        trunk.append((tb + 0.4, [3.0, min(h, OD.TRUNK_H + 2.0), 3.0], "step"))
    f.track("sdf/treeTrunk/node/trunkTop/size", trunk)
    f.track("sdf/treeTrunk/node/canopy/scale", [(0.0, 0.0, "step"), (t(71) - 0.001, 0.0, "easeOut"), (t(72), 1.15, "smooth"),
                                               (t(72, 3), 1.0, "step")])
    spiral = []
    for k in range(0, 9):
        a = math.radians(20.0 + 22.0 * k)
        spiral.append([6.6 * math.cos(a), 1.8 + 2.15 * k * 1.25, -6.6 * math.sin(a)])
    look_tree = [(t(bar), (0.0, 5.8 + (bar - 67) * 4.6, 0.0)) for bar in range(67, 72)]
    b.shot("tree", t(67), t(71), spiral, None, keys=("tree", "land", "rings"), look_keys=look_tree, fov=68.0, ease_kind="linear")
    b.shot("treeReveal", t(71), t(73), [[10.0, 18.0, 12.0], [17.0, 20.0, 19.0], [24.0, 21.0, 26.0]], None, keys=("tree", "land", "rings"),
           look_keys=[(t(71), (0.0, 17.0, 0.0)), (t(73), (0.0, 14.0, 0.0))], fov=62.0)
    r12, yaw12 = OD.tree_room_pose(12)
    win = [r12[0] * 1.32, r12[1] + 1.0, r12[2] * 1.32]
    b.shot("treeLand", t(73), t(75), [[20.0, 30.0, 18.0], [12.0, r12[1] + 3.5, 10.0], [win[0] * 1.6, win[1] + 0.4, win[2] * 1.6], win],
           None, keys=("tree", "land", "rings"), look_keys=[(t(73), (0.0, 22.0, 0.0)), (t(73, 3), (r12[0] * 0.5, r12[1] + 1.0, r12[2] * 0.5)),
                                                         (t(75), tuple(r12))], fov=[(t(73), 60.0), (t(75), 70.0)], ease_kind="inout")
    for i, tb in enumerate(blooms):
        if tb < t(67):
            continue
        (x, y, z), yaw = OD.tree_room_pose(i)
        cam = f.camera_at(max(tb, t(66, 2)) + 0.2)
        ex, ez = (cam[0][0], cam[0][2]) if cam and cam[3] in ("tree", "treeReveal", "treeLand") else (8.0, 8.0)
        L = math.hypot(ex - x, ez - z) or 1.0
        nrm = ((ex - x) / L, 0.0, (ez - z) / L)
        pos = (x + nrm[0] * 1.05, y + 0.55, z + nrm[2] * 1.05)
        b.word("FEEL IT GROW", tb - BEAT1, min(tb + 3.0, t(73)), pos, nrm, 0.16, style="pop", role="word", name=f"grow{i:02d}",
               tin=0.2, intensity=4.0, category="floatingText")
        c = b.clap(f"bloom{i:02d}", tb, release=0.25)
        f.route(c, "temporal/mosh/shift", 6.0)
    g_b1 = b.gate("gB1", t(67), t(73))
    for o in ("treeA", "treeB", "treeC", "treeTrunk"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 2.0, "half", g_b1)
        f.track(f"sdf/{o}/surface/{K.GLOW}/emission", hold_keys(0.0, end, [4.0, 4.0, 4.0]))
    b.val += [(t(73), 1.0, "smooth"), (t(74, 4), 0.55, "smooth"), (t(75) - 0.01, 0.55, "step"), (t(75), 1.0, "step")]

    # =========================================================================================================
    # BRIDGE 2 (75-82): the room of objects; he sits still at its centre
    # =========================================================================================================
    orbit = []
    for k in range(7):
        a = math.radians(15.0 + 15.0 * k)
        orbit.append([2.95 * math.sin(a), 1.55 + 0.05 * k, 2.95 * math.cos(a)])
    b.shot("gallery", t(75), t(83), orbit, (0.0, 0.95, 0.0), keys=("gal",), extra=["fwG"], fov=60.0, ease_kind="linear",
           moves=[(t(75), 0.0), (t(82, 3), 1.0), (t(83), 1.0)])
    b.show("galMan", t(75), t(83))
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
    wi = 0
    b.word_at("IS", t(74, 4.5), t(75), ("view", 4.0), 0.0, 0.15, k=0.08, style="flash", role="word", name="isFirst",
              intensity=4.0, tin=0.08, category="floatingText")
    for bar in range(75, 83):
        seq = [("IS", bar - 1, 4.5), ("THAT", bar, 1.0), ("ALL", bar, 1.5), ("YOU", bar, 2.0)]
        if bar == 75:
            seq = seq[1:]
        if bar == 78:
            seq.append(("GOT?", bar, 2.5))
        back = bar <= 78
        for j, (wd, wb, bt) in enumerate(seq):
            jj = j if seq[0][0] == "IS" else j + 1                 # the column a word sits in (IS, THAT, ALL, YOU)
            along = -2.2 + 1.2 * jj + (0.2 if bar % 2 else 0.0)
            yy = 2.75 - 0.52 * jj - (0.06 if bar % 2 else 0.0)
            if wd == "GOT?":
                along, yy = 0.6, 0.5
            pos, nrm = ((along, yy, -3.49), (0, 0, 1)) if back else ((-3.49, yy, -along), (1, 0, 0))
            te = t(wb, bt)
            b.word(wd + ("..." if bar == 82 and wd == "YOU" else ""), te, te + G.BAR2 * 0.85, pos, nrm,
                   0.36 if wd != "GOT?" else 0.7, style="flash" if wd != "GOT?" else "pop", role="word",
                   name=f"isth{wi:02d}", intensity=3.6 if wd != "GOT?" else 6.0, tin=0.1, tout=0.3, room="gallery")
            target = items[(wi * 4 + j) % len(items)]
            c = b.clap(f"lit{wi:02d}", te, release=0.45)
            f.route(c, f"sdf/{target}/look/edge/intensity", 12.0)
            wi += 1
    f.track("sdf/galShell/look/edge/intensity", hold_keys(0.0, end, 2.0))
    g_b2 = b.gate("gB2", t(75), t(82, 3))
    b.pulse("sdf/galShell/look/edge/intensity", 2.5, "eighth", g_b2)
    for name in items:
        b.pulse(f"sdf/{name}/look/edge/intensity", 4.0, "eighth", g_b2)
    # C17 (78.2.5) GOT?: a colourful sparkle explosion (he does not turn: he never moves while seen)
    c = b.clap("c17", t(78, 2.5), release=1.2)
    f.route(c, "post/grade/hueShift", 2.2)
    f.route(c, "post/bloom/intensity", 2.4)
    f.route(c, "palette/saturation", 0.8)
    f.route(c, "temporal/mosh/shift", 16.0)
    for name in items:
        f.route(c, f"sdf/{name}/look/edge/intensity", 18.0)
        f.route(c, f"sdf/{name}/look/edge/pixels", 3.0)
    f.nodes.append({"name": "fwG", "kind": "particles", "particles": {
        "capacity": 6000, "spawnRate": 0.0, "burst": 0.0, "shape": "sphere", "position": [0.0, 1.4, 0.0],
        "extent": [1.2, 0.6, 1.2], "lifetimeMin": 0.8, "lifetimeMax": 1.6, "direction": [0.0, 1.0, 0.0], "spread": 1.0,
        "speedMin": 1.0, "speedMax": 3.2, "gravity": [0.0, -1.2, 0.0], "drag": 1.2, "sizeStart": 0.035, "sizeEnd": 0.0,
        "blend": "additive", "colorStart": [1.0, 0.85, 1.0, 1.0], "colorEnd": [0.3, 0.9, 1.0, 0.0], "emissive": 4.0}})
    cb = b.clap("gotSpark", t(78, 2.5), release=0.08)
    f.route(cb, "particles/fwG/burst", 2600.0)
    # C18 (82.4): everything falls into his chair -- the objects, not him -- then one beat of black
    c = b.clap("c18", t(82, 4), release=0.2, attack=BEAT2 * 0.5)
    for name in items:
        f.track(f"sdf/{name}/transform/scale", [(0.0, [1.0, 1.0, 1.0], "step"), (t(82, 4) - 0.2, [1.0, 1.0, 1.0], "easeIn"),
                                               (t(82, 4) + 0.05, [0.05, 0.05, 0.05], "step"), (t(83), [1.0, 1.0, 1.0], "step")])
    b.val += [(t(82, 4) + 0.02, 1.0, "step"), (t(82, 4) + 0.06, 0.0, "step"), (t(83) - 0.001, 0.0, "step"), (t(83), 1.0, "step")]
    f.route(c, "post/bloom/intensity", 2.0)
    f.route(c, "temporal/mosh/amount", 0.5)

    # =========================================================================================================
    # BRIDGE 3 (83-90): the house dances. Living room, kitchen, hall, the front door; the camera only glides
    # =========================================================================================================
    house = {k: b.worlds[k]["room"] for k in ("liv", "kit", "hall")}
    for key, (obj, ext) in {"discoLiv": ("livDisco", house["liv"]["interior"]), "discoKit": ("kitDisco", house["kit"]["interior"]),
                            "discoHall": ("hallDisco", ((2.75, 3.85), (0.0, 2.7), (-6.3, 2.1)))}.items():
        (x0, x1), _, (z0, z1) = ext
        b.world(key, {"objects": [(obj, disco_floor(ext, 0.6), "furn", (x0 - 0.1, -0.05, z0 - 0.1), (x1 + 0.1, 0.1, z1 + 0.1))], "lights": []})
    eye = [(t(83), (1.6, 1.25, 1.7)), (t(84), (0.6, 1.1, 1.5)), (t(85), (-1.2, 1.3, -1.0)), (t(85, 3), (-1.75, 1.45, -2.3)),
           (t(86), (-1.3, 1.6, -3.1)), (t(87), (0.9, 1.4, -3.4)), (t(87, 3), (1.9, 1.45, -4.55)), (t(88), (2.75, 1.5, -4.6)),
           (t(88, 3), (3.3, 1.45, -3.8)), (t(89, 3), (3.35, 1.4, -1.6)), (t(90, 3), (3.4, 1.45, 0.3)), (t(90, 4), (3.42, 1.47, 1.2)),
           (t(91), (3.45, 1.5, 2.05))]
    look = [(t(83), (-1.2, 0.6, -0.6)), (t(84), (-1.8, 0.8, 0.8)), (t(84, 3), (-1.6, 1.0, -2.2)), (t(85, 3), (-1.75, 1.0, -3.6)),
            (t(86), (0.0, 0.9, -4.6)), (t(87), (0.3, 0.9, -5.4)), (t(87, 3), (2.8, 1.2, -4.6)), (t(88), (3.6, 1.2, -3.6)),
            (t(88, 3), (3.4, 1.3, -1.0)), (t(89, 3), (3.4, 1.4, 1.0)), (t(90, 3), (3.45, 1.4, 2.2)), (t(91), (3.45, 1.45, 3.5))]
    b.glide("dance", eye, look, nodes_keys=("liv", "kit", "hall", "discoLiv", "discoKit", "discoHall"), fov=66.0)
    for disc in ("livDisco", "kitDisco", "hallDisco"):
        for k_, ch in ((K.CANVAS, "discoA"), (K.CANVAS2, "discoB")):
            f.route(f"grid.song.{ch}", f"sdf/{disc}/surface/{k_}/emission", 2.2)
        f.track(f"sdf/{disc}/surface/{K.CANVAS}/emission", hold_keys(0.0, end, [0.15, 0.15, 0.15]))
        f.track(f"sdf/{disc}/surface/{K.CANVAS2}/emission", hold_keys(0.0, end, [0.15, 0.15, 0.15]))
    f.event("discoA", at="bridge3:1:1", release=0.85, units="beats", repeat={"every": 2, "count": 16})
    f.event("discoB", at="bridge3:1:2", release=0.85, units="beats", repeat={"every": 2, "count": 16})
    g_b3 = b.gate("gB3", t(83), t(90, 3))
    # the furniture rocks and turns on smooth waves (no beat pulse moves anything: section 28)
    for node, obj, amt in (("livTableRock", "livSofa", 14.0), ("kitChairARock", "kitTable", 16.0), ("kitKettleRock", "kitCounter", 25.0)):
        f.route("grid.song.half.wave", f"sdf/{obj}/node/{node}/rotation", amt, component=1, depth=g_b3, polarity="bipolar")
    f.route("grid.song.half.wave", "sdf/kitTable/node/kitPendant/rotation", 18.0, component=2, depth=g_b3, polarity="bipolar")
    for o in ("livSofa", "livMedia", "livShelf", "livDecor", "kitCounter", "kitTable", "hallFurn", "hallStair"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 2.5, "quarter", g_b3)
    for o in ("livShell", "kitShell", "hallShell"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 2.0, "half", g_b3)
        f.route("grid.song.quarter.wave", f"sdf/{o}/transform/scale", 0.015, depth=g_b3)
    for lamp in ("livLamp", "livSideLamp", "kitPend", "hallLamp", "hallLamp2"):
        f.route("grid.song.quarter", f"lights/{lamp}/intensity", 3.0, depth=g_b3)
    # IS THAT ALL? on the floors (it swells with the quarter's smooth wave, it does not bounce)
    rooms_at = {83: "liv", 84: "liv", 85: "kit", 86: "kit", 87: "kit", 88: "hall", 89: "hall", 90: "hall"}
    for bar in range(83, 91):
        room = house[rooms_at[bar]]
        if bar in (84, 88):
            b.word_at("IS THAT ALL YOU GOT?", t(bar - 1, 4.5), t(bar, 4.5), ("box", room["interior"]), 0.0, 0.45, k=0.05,
                      style="pop", role="word", name=f"b3w{bar}", tin=0.12, room=room["id"])
        else:
            b.word_at("IS THAT ALL?", t(bar - 1, 4.5), t(bar, 4.0), ("box", room["interior"]), 0.05 * ((bar % 3) - 1), -0.5,
                      k=0.07, style="pop", role="accent", name=f"b3w{bar}", tilt=(bar * 23) % 30 - 15, tin=0.12,
                      at=0.32 if bar == 83 else 0.12, room=room["id"])
            f.route("grid.song.quarter.wave", f"nodes/b3w{bar}/scale", 0.15, depth=f"grid.song.b3w{bar}")
    colour_wave(b)
    # the bass fill (90.4) throws the front door open; the open's light floods in
    f.track("sdf/hallShell/node/frontDoorSwing/rotation", [(0.0, [0.0, 0.0, 0.0], "step"), (t(90, 4) - 0.001, [0.0, 0.0, 0.0], "easeOut"),
                                                         (t(90, 4) + 0.25, [0.0, -105.0, 0.0], "smooth"), (t(91), [0.0, -100.0, 0.0], "step")])
    c = b.clap("sweep", t(91), attack=G.BAR2 / 2, release=0.6, curve="smooth")
    f.route(c, "camera/exposure/compensation", 2.2)
    f.route(c, "post/bloom/intensity", 1.8)
    f.route(c, "palette/saturation", 0.8)
    b.val += [(t(90, 4), 1.0, "smooth"), (t(91) - 0.02, 2.4, "step"), (t(91), 1.0, "smooth")]

    # =========================================================================================================
    # FINAL CHORUS (91-114): the open, the stair into the sky, the summit, the crash (pass 2's)
    # =========================================================================================================
    gy = OD.ground
    chA = [[0.0, gy(0, 46) + 2.2, 46.0], [2.0, gy(2, 34) + 2.6, 34.0], [-1.5, gy(-1.5, 22) + 4.0, 22.0], [-4.0, gy(-4, 10) + 7.5, 10.0],
           [-2.0, gy(-2, 0) + 10.0, 2.0]]
    b.shot("chorusA", t(91), t(99), chA, None, keys=("land", "rings", "sky"),
           look_keys=[(t(91), (0.0, 3.0, 0.0)), (t(93), (-3.0, 5.0, -20.0)), (t(95), (6.0, 8.0, -30.0)), (t(97), (-10.0, 9.0, -20.0)),
                      (t(99), (-14.0, 4.0, 0.0))], fov=[(t(91), 72.0), (t(93), 64.0), (t(99), 62.0)], ease_kind="linear")
    k = 0
    pats = [[(-0.5, -0.35), (0.0, -0.55), (0.5, -0.3)], [(0.45, -0.5), (-0.1, -0.3), (-0.55, -0.6)],
            [(-0.6, -0.2), (-0.15, -0.45), (0.35, -0.65)], [(0.55, -0.25), (0.1, -0.6), (-0.4, -0.4)]]
    for bar in range(91, 99):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            pat = pats[(2 * (bar - 91) + half) % 4]
            for j, wd in enumerate(("LET", "IT", "GO")):
                tw = t(bar, b0 + 0.5 * j)
                b.word_at(wd, tw, tw + G.BAR2 * 1.5, ("ground", gy), pat[j][0], pat[j][1] + 0.12, k=0.085, style="rise",
                          role="word" if j != 1 else "accent", name=f"chA{k:02d}", intensity=3.0, tin=0.15, tout=0.35,
                          stand=True, category="floatingText")
                k += 1
    g_ch = b.gate("gChorus", t(91), t(113))
    b.pulse("sdf/landGround/look/edge/intensity", 2.0, "quarter", g_ch)
    b.pulse("sdf/ringLow/look/edge/intensity", 4.0, "quarter", g_ch)
    b.pulse("sdf/ringHigh/look/edge/intensity", 4.0, "half", g_ch)
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
    stair_eye = [on_stair(-6.0, 2.4), on_stair(-1.5, 1.4), on_stair(3.0, 0.6), on_stair(8.0, 0.2), on_stair(13.0, 0.0),
                 on_stair(17.0, 0.0), on_stair(20.5, 0.0)]
    b.shot("stair", t(99), t(107), stair_eye, None, keys=("land", "rings", "sky"),
           look_keys=[(t(99), (sx0 + 6.0, sy0 + 3.0, 0.0)), (t(103), (sx0 + 16.0, sy0 + 10.0, 0.0)), (t(107), (10.0, 14.0, 0.0))],
           fov=64.0, ease_kind="linear")
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
    sum_eye = [[5.5, 16.0, 3.5], [8.5, 16.1, 4.8], [12.5, 16.2, 3.6], [14.5, 16.3, -0.8]]
    b.shot("summit", t(107), t(114), sum_eye, None, keys=("land", "rings", "sky"),
           look_keys=[(t(107), (10.0, 15.5, -4.0)), (t(109), (0.0, 22.0, 0.0)), (t(111), (-10.0, 20.0, -10.0)),
                      (t(112, 3), (10.0, 15.0, 0.0)), (t(113), (10.0, 15.0, 0.0)), (t(114), (10.0, 18.0, 0.0))],
           fov=[(t(107), 66.0), (t(112, 3), 66.0), (t(113), 50.0), (t(113) + 0.4, 84.0), (t(114), 76.0)], ease_kind="linear",
           moves=[(t(107), 0.0), (t(113), 0.92), (t(114), 1.0)])
    k = 0
    arcs = [[(-0.6, 0.3), (0.0, 0.48), (0.6, 0.3)], [(-0.55, -0.1), (0.0, 0.1), (0.55, -0.1)], [(-0.4, 0.55), (0.05, 0.35), (0.5, 0.55)]]
    for bar in range(107, 113):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            arc = arcs[(2 * (bar - 107) + half) % 3]
            for j, wd in enumerate(("LET", "IT", "GO")):
                tw = t(bar, b0 + 0.5 * j)
                b.word_at(wd, tw, tw + G.BAR2, ("view", 11.0 + 2.0 * j), arc[j][0], arc[j][1], k=0.07, style="flash",
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
            sx, sy = [(-0.55, 0.45), (0.5, 0.55), (0.0, 0.62), (-0.3, 0.3), (0.62, 0.35), (-0.62, 0.6)][n_fw % 6]
            pos = f.in_view(tb, sx, sy, 22.0 + 4.0 * (n_fw % 3))[0]
            fw_pos[em].append((tb - 0.05, [round(v, 3) for v in pos]))
            cb = b.clap(f"fwb{bar}{int(bt)}", tb, release=0.06)
            f.route(cb, f"particles/{em}/burst", 900.0)
            n_fw += 1
    for em in ("fwA", "fwB", "fwC"):
        fw_pos[em].append((t(113) - 0.05, [10.0, 19.0, 0.0]))
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
    c = b.clap("crash", t(113), release=4.0)
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
    # ENDING (114-end): dawn on the hill; he watches; the world washes out and breaks up; back to the road
    # =========================================================================================================
    dawn_eye = [[3.0, gy(3, -8) + 1.9, -8.0], [2.0, gy(2, -10) + 1.85, -10.5], [1.2, gy(1.2, -12) + 1.8, -12.6]]
    b.shot("dawn", t(114), 255.0, dawn_eye, None, keys=("land", "rings", "sky", "hillman"),
           look_keys=[(t(114), (2.0, 1.0, -40.0)), (252.0, (3.0, 3.0, -60.0)), (255.0, (4.0, 5.0, -80.0))],
           fov=[(t(114), 60.0), (255.0, 54.0)])
    b.show("hillMan", t(114), 255.0)
    f.track("sdf/sun/node/sunAt/translation", [(0.0, [0.0, -40.0, 0.0], "step"), (t(114), [0.0, -6.0, 0.0], "smooth"),
                                              (252.3, [0.0, 26.0, 0.0], "smooth"), (end, [0.0, 36.0, 0.0])])
    b.val += [(t(114), 1.0, "smooth"), (253.0, 1.6, "smooth"), (254.8, 4.5, "smooth"), (255.0, 5.0, "step"), (255.01, 1.0, "step")]
    f.track("camera/exposure/compensation", [(0.0, 0.0), (252.0, 0.0, "smooth"), (254.9, 2.6, "step"), (255.0, 0.0, "step")], mode="add")
    f.track("temporal/mosh/amount", [(0.0, 0.0), (253.0, 0.0, "smooth"), (254.3, 0.5, "smooth"), (254.95, 0.95, "step"),
                                     (255.0, 0.0, "step")], mode="add")
    f.track("temporal/mosh/shift", [(0.0, 0.0), (253.4, 0.0, "smooth"), (254.95, 30.0, "step"), (255.0, 0.0, "step")], mode="add")
    for o, t_out in (("landMtn", 252.2), ("ringLow", 252.6), ("ringHigh", 252.4), ("sun", 254.6), ("landGround", 253.4),
                     ("landTrees", 253.8), ("landStones", 254.1), ("hillMan", 254.7)):
        f.track(f"sdf/{o}/look/edge/intensity", [(0.0, 1.0, "step"), (t(114), 1.0, "smooth"), (t_out - 1.4, 1.0, "smooth"),
                                                 (t_out, 0.0, "step")], mode="multiply")
    # the coda: the road's four dashes in the black, as at the count-in, going out one by one
    b.shot("coda", 255.0, end, [[-46.0, 2.0, 12.6], [-45.99, 2.0, 12.6]], (-20.0, 0.8, 12.0), keys=("street",), fov=54.0)
    for o in ("row", "faller", "lamps", "skyline", "heroShell", "heroParts", "heroTree"):
        f.track(f"nodes/{o}/visible", [(0.0, 1.0, "step"), (254.99, 0.0, "step"), (end, 0.0, "step")])
    f.track("sdf/street/node/gridClip/size", [(254.99, [0.0, 0.5, 0.0], "step"), (end, [0.0, 0.5, 0.0], "step")], mode="replace")
    f.track("sdf/street/look/edge/intensity", [(0.0, 1.0, "step"), (254.99, 0.0, "step"), (end, 0.0, "step")], mode="multiply")
    blink = [(254.99, [260.0, 0.5, 0.5])]
    for tt, x_end in ((255.6, -28.6), (256.2, -33.6), (256.8, -38.6), (257.4, -60.0)):
        blink.append((tt, [x_end + 42.0, 0.5, 0.5]))
    f.track("sdf/street/node/dashClip/size", [(tt, v, "step") for tt, v in blink], mode="replace")
    b.palette_at(255.0, "P0boot")


def colour_wave(b):
    """Section 30, 90.3 -> 91.1: a wave of coloured light that travels through the hall and the house, not a screen
    wipe. Until the engineer's spatial wave lands (ADR-1055?), it is built from what the scene has: a front of five
    coloured point lights that enters through the front door and sweeps back through the hall past the camera,
    lighting the surfaces it passes, with each object's lines catching the new palette as the front reaches it."""
    f = b.f
    t0, t1 = t(90, 3), t(91)
    zs = (2.4, -6.6)
    hues = [(1.0, 0.25, 0.6), (1.0, 0.6, 0.15), (0.3, 1.0, 0.5), (0.2, 0.7, 1.0), (0.75, 0.3, 1.0)]
    for i, (x, col) in enumerate(zip((2.95, 3.35, 3.75, 4.15, 4.55), hues)):
        name = f"wave{i}"
        f.point_light(name, (x, 1.3 + 0.25 * (i % 2), zs[0]), col, 0.0, 2.8, vol=1.5)
        f.track(f"lights/{name}/position", [(0.0, [x, 1.3, zs[0]], "step"), (t0 - 0.05, [x, 1.3, zs[0]], "linear"),
                                           (t1, [x, 1.4, zs[1]], "step")])
        f.track(f"lights/{name}/intensity", [(0.0, 0.0, "step"), (t0 - 0.05, 0.0, "smooth"), (t0 + 0.15, 40.0, "smooth"),
                                            (t1 - 0.1, 40.0, "smooth"), (t1, 0.0, "step")])
    # the lines catch the colour as the front passes each object's middle (front at z(t) = 2.4 - 9 (t - t0)/(t1 - t0))
    for o, zc in (("hallFurn", 0.0), ("hallStair", -2.5), ("hallShell", -2.0), ("hallDisco", -2.0)):
        tc = t0 + (zs[0] - zc) / (zs[0] - zs[1]) * (t1 - t0)
        c = b.clap(f"wave_{o}", tc, attack=0.05, release=0.35)
        f.route(c, f"sdf/{o}/look/edge/intensity", 10.0)
    b.palette_at(t0 + 0.1, "P11open", ramp=t1 - t0 - 0.1)
