"""All You Got, art pass 4: the house's changed rooms (04-art-pass-4.md, PARTS 4 and 6). The rest of the house is
pass 3's (rooms3.py), imported unchanged.

    kitchen    the counter run along the back wall now starts with a range: a pot steams on its lit front burner
               under a hood light (the clap at 1:17 finds him there, making dinner); the table set for one stays
    basement   three rooms with distinct lives, off one hall-less plan:
                 laundry  (kept)            he folds a towel on the dryer
                 gym      (was storage)     a bench, a racked barbell, dumbbells, a mat, a bike, a mirror wall;
                                            he stands there with a dumbbell hanging from his hand, not lifting it
                 lounge   (was the boiler   a sofa facing a television of static, a low table, a lamp; and at its
                          room and the      far end a home bar: a counter with three stools, a back bar of bottles
                          tool bench)       over strips of coloured light, a neon sign: his private downstairs
                                            room, not a club. He sits at the bar; later, alone on the sofa.
"""

from __future__ import annotations

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools"))

import kit as K  # noqa: E402
import liminal_space as ls  # noqa: E402

ls.instrument_kit(K)

import props3 as PR  # noqa: E402
import props4 as P4  # noqa: E402
import rooms3 as R3  # noqa: E402
import tableaux3 as TB  # noqa: E402
import tableaux4 as T4  # noqa: E402
from kit import ACCENT, CANVAS, CANVAS2, FILL, FLOOR, GLASS, GLOW, SCREEN, D, R, S, T, U, X, place  # noqa: E402
from rooms3 import CEIL, DOWN, on_wall, shell_with, tag, window_on  # noqa: E402


# ---- the walls' trim stops at the doors and windows (ADR-1056's openingCrossed: pass 3's skirting and dado ran across
# every doorway, the owner's "wall details intersecting doors") ----------------------------------------------------------
def shell_with(ext, rid, doors=(), windows=(), tiled=False, dado=True, boards="x", extra_cuts=(), ceiling=True, sealed=False):
    """rooms3.shell_with, with the skirting and the dado inside the difference that cuts the openings, so they stop at
    every door and window (the bands are pieces of the room's shell, not separate trim entities: the cut has to be in
    the same subtree as the band for the band to be cut). Same nodes as pass 3's."""
    (x0, x1), (y0, y1), (z0, z1) = ext
    cuts = [K.door_cut(wl, ext, along, w, 2.05) for wl, along, w, _ in doors]
    for wl, along, w, h, sill in windows:
        ys = (y0 + sill, y0 + sill + h)
        cuts.append({"+x": X(((x1 - 0.5, x1 + 0.5), ys, (along - w / 2, along + w / 2))),
                     "-x": X(((x0 - 0.5, x0 + 0.5), ys, (along - w / 2, along + w / 2))),
                     "+z": X(((along - w / 2, along + w / 2), ys, (z1 - 0.5, z1 + 0.5))),
                     "-z": X(((along - w / 2, along + w / 2), ys, (z0 - 0.5, z0 + 0.5)))}[wl])
    if not ceiling:
        cuts.append(X(((x0 - 0.3, x1 + 0.3), (y1 - 0.02, y1 + 0.5), (z0 - 0.3, z1 + 0.3))))
    cuts += list(extra_cuts)
    ent = {"id": rid}
    if sealed:
        ent["sealed"] = True
    bands = [K.skirting(ext)]
    if dado:
        bands.append(K.wall_band(ext, y0 + 0.88, y0 + 0.92, 0.015))
    for bnd in bands:
        bnd.pop("entity", None)
    sh = K.shell(ext, 0.15, entity=ent)
    room_ent = sh.pop("entity")
    body = U(sh, *bands)
    body["entity"] = room_ent        # the validator gives a difference's cuts to its first child: the room is that child
    walls = D(body, *cuts)
    ext_in = ((x0 + 0.03, x1 - 0.03), (y0, y1), (z0 + 0.03, z1 - 0.03))     # the floor's lines stop short of the thresholds
    parts = [walls, K.tiles(ext_in, 0.5) if tiled else K.floorboards(ext_in, 0.2, boards)]
    for wl, along, w, did in doors:
        parts.append(K.door_frame(wl, ext, along, w, 2.05, entity={"id": did, "room": rid}))
    return parts


R3.shell_with = shell_with      # pass 4's rooms (and pass 3's room builders when called from pass 4) cut their trim;
                                # pass 3's own generator never imports this module, so its film is unchanged


def rot(yaw, x, z):
    a = math.radians(yaw)
    c, s = math.cos(a), math.sin(a)
    return x * c + z * s, -x * s + z * c


def fig_facing(anchor_in_fig, anchor_yaw_in_fig, anchor_at, anchor_yaw):
    """Where a tableau's figure stands in a room, given where the anchor (stove, dryer, counter) stands in the room
    and where it stands in the figure's frame: (at, yaw)."""
    yaw = anchor_yaw - anchor_yaw_in_fig
    ax, az = rot(yaw, anchor_in_fig[0], anchor_in_fig[2])
    return (anchor_at[0] - ax, anchor_at[1], anchor_at[2] - az), yaw


def figures(rid, placements):
    """[(object name, tableau, at, yaw, anchor id)] -> figure objects (with their hand-held props)."""
    objs = []
    for name, tab, at, yaw, anchor in placements:
        tree, lo, hi = T4.placed(tab, name, at, yaw, room=rid, anchor_id=anchor)
        objs.append((name, tree, "figure", lo, hi, {"figure": True}))
    return objs


# =============================================================================================================
# THE KITCHEN
# =============================================================================================================
STOVE = (1.75, 0.0, -6.4 + 0.31)


def kitchen():
    """Verse 1, bars 33-37. The back wall, left to right: the fridge, the counter with its sink under the window and
    the kettle, the range (the pot on its lit front-left burner, under a hood) in the right-hand corner, so a camera
    coming in at the living-room door sees the fridge and the window first and has to turn to find him; the table set for one in the middle with its chair
    facing the living-room door and a second chair; a pendant; a clock. Doors: the living room (front wall), the
    hall (right wall). Tableaux: stove (dinner), table."""
    rid = "kitchen"
    ext = ((-2.6, 2.6), (0.0, CEIL), (-6.4, -2.35))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+z", -1.75, 0.9, "KitchenLivingDoor"), ("+x", -4.6, 0.9, "KitchenHallDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("-z", -0.4, 1.0, 0.9, 1.15)], tiled=True, dado=False),
              window_on("-z", ext, -0.4, 1.0, 0.9, 1.15, eid="KitchenWindow", room=rid),
              on_wall(K.wall_clock(0.18, name="kitClock", entity={"id": "KitchenClock", "room": rid}), "+x", ext, -3.4, 1.9),
              on_wall(tag(PR.wall_cupboard(0.8, 0.6, 0.32), "wallCupboard", "WallCupboard", rid), "-z", ext, -1.35, 1.85),
              on_wall(K.painting(0.5, 0.65, motif="grid", entity={"id": "KitchenPicture", "room": rid}), "-x", ext, -4.4, 1.55))
    pot_at = (STOVE[0] - 0.17, 0.9, STOVE[2] + 0.12)
    range_ = U(place(tag(P4.stove(name_ring="kitBurner"), "stove", "Stove", rid), STOVE),
               place(tag(P4.pot(), "prop", "Pot", rid, anchor="Stove"), pot_at, name="kitPot"),
               on_wall(tag(P4.range_hood(0.8, 0.5), "wallCupboard", "RangeHood", rid), "-z", ext, STOVE[0], 1.62))
    counter = U(place(tag(PR.counter3(3.1, 0.9, 0.6, sink_x=-0.2), "counter", "Counter", rid), (-0.2, 0.0, z0 + 0.3)),
                place(R([0, 0, 0], K.kettle(entity={"id": "Kettle", "room": rid}), name="kitKettleRock"), (0.85, 0.9, z0 + 0.3), name="kitKettle"),
                place(K.fridge(0.7, 1.75, 0.62, entity={"id": "Fridge", "room": rid}), (-2.15, 0.0, z0 + 0.33)),
                place(K.stack_of_books(entity={"id": "CookBooks", "room": rid}), (-1.45, 0.9, z0 + 0.22)))
    chair_at = (0.0, 0.0, -5.125)
    fig_at, fig_yaw = R3.fig_from_anchor("table", chair_at, 0.0)
    table = U(place(K.table(1.1, 0.75, 0.75, entity={"id": "KitchenTable", "room": rid}), (0.0, 0.0, -4.4), name="kitTableAt"),
              place(K.chair(entity={"id": "KitchenChair", "room": rid, "anchor": "KitchenTable"}), chair_at, 0.0, name="kitChairB"),
              place(R([0, 0, 0], K.chair(entity={"id": "KitchenChair2", "room": rid, "anchor": "KitchenTable"}), name="kitChairARock"),
                    (0.35, 0.0, -3.675), 180.0, name="kitChairA"),
              place(K.plate_and_cup(entity={"id": "PlateAndCup", "room": rid}), (0.0, 0.75, -4.65), 180.0, name="kitPlate"),
              place(PR.pendant(0.85, name="kitPendant", eid="Pendant", room=rid), (0.0, y1, -4.4)))
    stove_fig, stove_yaw = fig_facing(T4.STOVE_AT, 180.0, STOVE, 0.0)
    figs = figures(rid, [("kitStove", "stove", stove_fig, stove_yaw, "Stove")])
    return {"id": rid, "interior": ext,
            "objects": [("kitShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("kitCounter", counter, "furn2", (x0 - 0.05, -0.05, z0 - 0.05), (x1 + 0.05, 2.2, z0 + 0.8)),
                        ("kitRange", range_, "furn2", (1.25, -0.05, z0 - 0.05), (x1 + 0.05, 2.6, z0 + 0.8)),
                        ("kitTable", table, "furn", (-0.8, -0.05, -5.5), (0.8, y1 + 0.05, -3.3))] + figs,
            "lights": [("kitPend", (0.0, 1.6, -4.4), "lamp"), ("kitHood", (STOVE[0], 1.5, STOVE[2] + 0.15), "lamp")],
            "anchors": {"table": (0.0, 0.85, -4.4), "counter": (-0.35, 1.0, z0 + 0.3), "window": (-0.4, 1.6, z0),
                        "livingDoor": (-1.75, 1.0, z1), "hallDoor": (x1, 1.0, -4.6), "man": (0.0, 1.0, -5.1),
                        "pot": pot_at, "stove": STOVE, "stoveMan": stove_fig}}


# =============================================================================================================
# THE BASEMENT
# =============================================================================================================

def laundry():
    """Pass 3's laundry (a washer and a dryer on the left wall, a basket, a rack of boxes, a high window, a bare
    bulb), now with him at the dryer folding a towel. Doors: the gym (right wall), the lounge (back wall)."""
    room = R3.laundry()
    rid = room["id"]
    (x0, x1), (y0, y1), (z0, z1) = room["interior"]
    dryer_at, dryer_yaw = (x0 + 0.33, y0, 0.3), 90.0
    fig_at, fig_yaw = fig_facing(T4.DRYER_AT, 180.0, dryer_at, dryer_yaw)
    towel = place(T4.towel_on_dryer(), fig_at, fig_yaw)
    ls.tag(towel, "prop", id="Towel", room=rid)
    objs = []
    for o in room["objects"]:
        if o[0] == "lauShell":
            o = (o[0], U(o[1], towel)) + tuple(o[2:])
        objs.append(o)
    objs += figures(rid, [("lauFold", "fold", fig_at, fig_yaw, "Dryer")])
    room["objects"] = objs
    room["anchors"]["foldMan"] = fig_at
    return room


def gym_mirror(w, h, entity=None):
    """The gym's wall mirror (pass 4 review: the kit's pane read as a black hole in a frame): the dark pane (GLASS) with
    two diagonal glints across its upper left (CANVAS2, lit), the way a drawing says 'mirror' (about 14 nodes). (A
    faintly lit pane showed concentric rings of speckle in preview v2: keep the pane black.) Its back on z = 0, facing
    +Z."""
    import liminal_space as ls
    frame = S(K.D(X(((-w / 2, w / 2), (-h / 2, h / 2), (0.0, 0.035))), X(((-w / 2 + 0.04, w / 2 - 0.04), (-h / 2 + 0.04, h / 2 - 0.04), (0.02, 0.1)))), FILL)
    pane = S(X(((-w / 2 + 0.04, w / 2 - 0.04), (-h / 2 + 0.04, h / 2 - 0.04), (0.0, 0.02))), GLASS)
    inside = X(((-w / 2 + 0.06, w / 2 - 0.06), (-h / 2 + 0.06, h / 2 - 0.06), (0.0, 0.03)))
    glints = K.I(U(T([-w * 0.22, h * 0.08, 0.022], R([0, 0, -38], K.box([0.05, h, 0.004]))),
                   T([-w * 0.22 + 0.17, h * 0.08, 0.022], R([0, 0, -38], K.box([0.016, h, 0.004])))), inside)
    m = U(frame, pane, S(glints, CANVAS2))
    return ls.tag(m, "mirror", **entity) if entity else m


def gym():
    """Where the storage room was (x 1.05..4.95): a rubber-tiled floor, a mirror along the right wall over a rack of
    dumbbells, a squat rack against the front wall with its barbell racked, a flat bench in the middle, a mat, an
    exercise bike, a high window, a bulb. Doors: the laundry (left wall), the lounge (back wall). Tableau:
    gym_stand (by the bench, facing the mirror)."""
    rid = "gym"
    ext = ((1.05, 4.95), (DOWN, DOWN + CEIL), (-2.2, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("-x", 0.0, 0.9, "GymLaundryDoor"), ("-z", 3.0, 0.9, "GymLoungeDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("+z", 3.0, 1.2, 0.4, 2.05)], tiled=True, dado=False),
              window_on("+z", ext, 3.0, 1.2, 0.4, 2.05, eid="GymWindow", room=rid),
              on_wall(gym_mirror(2.2, 1.3, entity={"id": "GymMirror", "room": rid}), "+x", ext, -0.2, y0 + 1.45),
              place(tag(PR.floor_bulb(0.45), "hangingLamp", "GymBulb", rid), (3.0, y1, 0.0)))
    furn = U(place(tag(P4.squat_rack(), "gymRack", "SquatRack", rid), (2.2, y0, z1 - 0.55), 180.0),
             place(tag(P4.weight_bench(), "gymBench", "WeightBench", rid), (3.15, y0, 0.15)),
             place(tag(P4.dumbbell_rack(1.1), "gymRack", "DumbbellRack", rid), (x1 - 0.25, y0, -0.2), -90.0),
             place(tag(P4.exercise_mat(), "rug", "GymMat", rid), (1.75, y0, -1.0)),
             place(tag(P4.exercise_bike(), "gymBike", "ExerciseBike", rid), (4.2, y0, 1.55), -90.0))
    figs = figures(rid, [("gymMan", "gym_stand", (3.85, y0, 0.9), 90.0, None)])
    return {"id": rid, "interior": ext,
            "objects": [("gymShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5),
                         {"emission": {GLASS: [0.0, 0.0, 0.0], CANVAS2: [0.75, 0.72, 0.95]}}),     # the pane dark, its glints lit
                        ("gymFurn", furn, "furn2", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.3, z1 + 0.05))] + figs,
            "lights": [("gymBulb", (3.0, y1 - 0.55, 0.0), "lamp")],
            "anchors": {"mirror": (x1, y0 + 1.45, -0.2), "bench": (3.15, y0 + 0.5, 0.15), "rack": (2.2, y0 + 1.2, z1),
                        "man": (3.85, y0 + 1.0, 0.9)}}


BAR_COUNTER = (3.6, DOWN, -4.4)


def lounge():
    """Where the boiler room and the tool bench were (x -2.6..4.95, z -6.4..-2.35): the west end a sitting room (a
    sofa against the left wall facing a television of static on a low stand, a rug, a low table, a reading lamp),
    the east end his home bar (the counter with three stools, a back bar of bottles over strips of coloured light on
    the right wall, a neon sign), pendants over the bar. Doors: the laundry and the gym (front wall). Tableaux:
    bar_sit (the middle stool), lounge_sit (the sofa)."""
    rid = "lounge"
    ext = ((-2.6, 4.95), (DOWN, DOWN + CEIL), (-6.4, -2.35))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+z", -0.6, 0.9, "LoungeLaundryDoor"), ("+z", 3.0, 0.9, "LoungeGymDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, tiled=False, dado=True, boards="z"),
              on_wall(K.painting(1.0, 0.6, motif="horizon", entity={"id": "LoungePainting", "room": rid}), "-z", ext, -1.0, y0 + 1.6),
              on_wall(tag(P4.neon_sign(0.9, 0.32), "wallDecoration", "NeonSign", rid), "-z", ext, 3.5, y0 + 2.05),
              place(PR.pendant(0.7, eid="BarPendant", room=rid), (3.6, y1, -5.0)),
              place(PR.pendant(0.7, eid="BarPendant2", room=rid), (3.6, y1, -3.8)))
    sofa_at, sofa_yaw = (x0 + 0.46, y0, -4.4), 90.0
    sitting = U(place(tag(PR.sofa3(), "couch", "LoungeSofa", rid, seatHeight=0.42, surfaceHeight=0.42), sofa_at, sofa_yaw),
                place(K.rug(2.2, 1.6, entity={"id": "LoungeRug", "room": rid}), (-1.2, y0, -4.4), 90.0),
                place(tag(P4.low_table(1.0, 0.55, 0.4), "coffeeTable", "LoungeTable", rid), (-1.15, y0, -4.4), 90.0),
                place(tag(P4.tv_stand(1.2), "television", "LoungeTV", rid), (0.5, y0, -4.4), -90.0),
                place(K.floor_lamp(1.5, entity={"id": "LoungeLamp", "room": rid}), (x0 + 0.32, y0, -5.95)))
    bar = U(place(tag(P4.back_bar(2.4), "cabinet", "BackBar", rid), (x1, y0, -4.4), -90.0),
            place(tag(P4.bar_counter(2.6), "barCounter", "BarCounter", rid), BAR_COUNTER, -90.0))
    stools = U(*[place(tag(P4.bar_stool(), "barStool", f"BarStool{i}", rid, anchor="BarCounter"),
                       (BAR_COUNTER[0] - T4.BAR_AT[2], y0, -4.4 + dz), 90.0) for i, dz in enumerate((-0.85, 0.0, 0.85))])
    bar_fig, bar_yaw = fig_facing(T4.BAR_AT, 180.0, BAR_COUNTER, -90.0)
    glass = place(K.S(K.D(K.CY([0, 1.08 + 0.06, 0.58], 0.035, 0.12), K.CY([0, 1.08 + 0.08, 0.58], 0.028, 0.12)), K.GLASS), bar_fig, bar_yaw)
    ls.tag(glass, "prop", id="BarGlass", room=rid, anchor="BarCounter")
    lounge_fig, lounge_yaw = R3.fig_from_anchor("lounge_sit", sofa_at, sofa_yaw)
    figs = figures(rid, [("barMan", "bar_sit", bar_fig, bar_yaw, "BarStool1"),
                         ("loungeMan", "lounge_sit", lounge_fig, lounge_yaw, "LoungeSofa")])
    return {"id": rid, "interior": ext,
            "objects": [("lngShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("lngSit", sitting, "furn", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (1.2, y0 + 1.8, z1 + 0.05)),
                        ("lngBar", bar, "furn2", (2.0, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.5, z1 + 0.05),
                         {"emission": {CANVAS: [1.5, 0.25, 0.85], SCREEN: [1.6, 0.85, 0.25], CANVAS2: [0.25, 1.3, 1.5],
                                       GLOW: [1.6, 0.9, 0.35], FLOOR: [0.3, 1.6, 1.9]}}),
                        ("lngStools", U(stools, glass), "furn", (2.0, y0 - 0.05, -5.6), (3.7, y0 + 1.35, -3.2))] + figs,
            "lights": [("lngBarGlow", (4.2, y0 + 1.3, -4.4), "lamp"), ("lngPend", (3.6, y1 - 0.85, -4.4), "lamp"),
                       ("lngLamp", (x0 + 0.32, y0 + 1.4, -5.95), "lamp"), ("lngTV", (0.0, y0 + 0.9, -4.4), "screen")],
            "anchors": {"bar": (BAR_COUNTER[0], y0 + 1.1, -4.4), "barMan": bar_fig, "sofa": sofa_at, "tv": (0.5, y0 + 0.86, -4.4),
                        "loungeMan": lounge_fig}}


def basement():
    return {"lau": laundry(), "gym": gym(), "lng": lounge()}


def study():
    """Pass 3's study with the bookcase moved off the door (ADR-1056: it stood across a third of StudyDoor's opening
    and 5 cm into the wall) and the painting moved past it."""
    rid = "study"
    ext = ((-2.6, 1.2), (R3.UP, R3.UP + CEIL), (-6.4, -2.35))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+x", -3.0, 0.9, "StudyDoor")]
    walls = U(*shell_with(ext, rid, doors=doors, windows=[("-z", -0.9, 1.2, 1.1, 1.05)], ceiling=False))
    shell = U(K.wave(walls, 0.0, 7.0, [1.0, 0.0, 0.0], name="stuWarp"),
              window_on("-z", ext, -0.9, 1.2, 1.1, 1.05, eid="StudyWindow", room=rid),
              on_wall(K.painting(0.8, 0.55, motif="horizon", entity={"id": "StudyPainting", "room": rid}), "+x", ext, -5.8, y0 + 1.6))
    desk_at = (-0.9, y0, z0 + 0.46)
    fig_at = (desk_at[0] - 0.2, y0, desk_at[2] + 0.6)
    group = place(TB.desk_group("Study", rid), fig_at, 180.0)
    furn = U(group, place(K.globe(0.16, name="stuGlobe", entity={"id": "Globe", "room": rid}), (-0.25, y0 + 0.75, z0 + 0.3)))
    shelf = place(K.bookshelf(0.9, 1.9, 0.3, entity={"id": "StudyBookcase", "room": rid}), (x1 - 0.165, y0, -4.65), -90.0)
    figs = R3.figure_objects(rid, [("stuDesk", "desk", fig_at, 180.0, "StudyChair"),
                                   ("stuFloor", "floor_sit", (x0 + 0.32, y0, -4.2), 90.0, None)])
    return {"id": rid, "interior": ext,
            "objects": [("stuShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("stuFurn", furn, "furn", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.0, z1 + 0.05)),
                        ("stuShelf", shelf, "furn2", (x1 - 0.5, y0 - 0.05, -5.2), (x1 + 0.05, y0 + 2.0, -4.1)),
                        ("stuCeil", R3.ceiling_halves(ext, "stu", attach_left=[place(tag(U(K.ceiling_fan(name="stuFan"), K.CY([0, -0.02, 0], 0.08, 0.04)),
                                                                                          "ceilingFan", "CeilingFan", rid), (-0.8, y1, -4.3))]),
                         "wall", (x0 - 12, y1 - 0.8, z0 - 12), (x1 + 12, y1 + 40.0, z1 + 12))] + figs,
            "lights": [("stuLamp", (-0.2, y0 + 1.6, z0 + 0.5), "lamp"), ("stuScreen", (-1.1, y0 + 1.0, z0 + 0.9), "screen")],
            "anchors": {"desk": (desk_at[0], y0 + 0.9, desk_at[2]), "monitor": (-1.1, y0 + 1.0, z0 + 0.31), "window": (-0.9, y0 + 1.6, z0),
                        "wall": (x0, y0 + 1.0, -4.2), "door": (x1, y0 + 1.0, -3.0), "fan": (-0.8, y1 - 0.3, -4.3)}}


def upstairs():
    return {"bed": R3.bedroom(), "bath": R3.bathroom(), "stu": study()}


def hall():
    """Pass 3's hall (rooms3.hall), rebuilt so the front door's closed leaf belongs to its frame's entity (ADR-1056's
    opening check takes anything in a doorway that is not the door's own frame for an obstruction: a closed leaf was
    reported as crossing its own doorway); its stair ends at the cliff on purpose (the climb's top): `terminates`."""
    rid = "hall"
    ext = ((2.75, 4.95), (0.0, CEIL), (-6.4, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    s = R3.STAIR
    top_z = s["foot_z"] + s["steps"] * s["run"]
    doors = [("-x", -1.2, 0.9, "HallLivingDoor"), ("-x", -4.6, 0.9, "HallKitchenDoor")]
    well = X(((s["x0"] - 0.05, x1 + 0.3), (y1 - 0.05, y1 + 0.6), (s["foot_z"] + 1.1, top_z + 0.1)))
    flight = {"kind": "stairs", "size": [s["run"], s["rise"], (s["x1"] - s["x0"]) / 2], "count": s["steps"], "height": 0.0}
    stair = T([(s["x0"] + s["x1"]) / 2, 0.0, s["foot_z"]], R([0, -90, 0], flight))
    ls.tag(stair, "stairs", id="HallStair", room=rid, terminates=True)
    rail = K.SEG([s["x0"] - 0.03, 0.95, s["foot_z"]], [s["x0"] - 0.03, CEIL + 0.95, top_z], 0.022)
    post = K.SEG([s["x0"] - 0.03, 0.0, s["foot_z"]], [s["x0"] - 0.03, 0.97, s["foot_z"]], 0.03, box_section=(0.03, 0.03))
    stair_obj = U(S(stair, FILL), S(U(rail, post), ACCENT))
    front_cut = K.door_cut("+z", ext, 3.45, 1.0, 2.05)
    frame = K.door_frame("+z", ext, 3.45, 1.0, 2.05)
    frame.pop("entity", None)
    leaf = place(PR.front_door(1.0, 2.05, name="frontDoorSwing"), (3.45, 0.0, z1), 180.0)
    front = ls.tag(U(frame, leaf), "door", id="FrontDoor", room=rid, normal=[0, 0, -1])
    shell = U(*shell_with(ext, rid, doors=doors, windows=[], boards="z", extra_cuts=[well, front_cut]), front,
              on_wall(K.painting(0.45, 0.6, motif="portrait", entity={"id": "HallPortrait", "room": rid}), "-x", ext, 0.5, 1.6))
    furn = U(on_wall(K.coat_hooks(4, entity={"id": "CoatHooks", "room": rid}), "-x", ext, 1.4, 1.7),
             on_wall(K.mirror_frame(0.55, 0.85, entity={"id": "HallMirror", "room": rid}), "-x", ext, -2.6, 1.5),
             place(K.phone_table(entity={"id": "PhoneTable", "room": rid}), (x0 + 0.24, 0.0, -2.0), 90.0),
             place(K.rug(0.8, 2.4, entity={"id": "Runner", "room": rid}), (3.35, 0.0, 0.6)))
    figs = R3.figure_objects(rid, [("hallMan", "stair", (3.32, 0.0, -3.55), 10.0, None)])
    return {"id": rid, "interior": ext,
            "objects": [("hallShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("hallStair", stair_obj, "furn2", (s["x0"] - 0.2, -0.4, s["foot_z"] - 0.1), (x1 + 0.05, CEIL + 1.1, top_z + 0.1)),
                        ("hallFurn", furn, "furn", (x0 - 0.05, -0.05, -3.0), (4.0, 2.1, 2.2))] + figs,
            "lights": [("hallLamp", (3.4, 2.35, 0.6), "lamp"), ("hallLamp2", (3.4, 2.35, -5.4), "lamp")],
            "anchors": {"frontDoor": (3.45, 1.0, z1), "stairFoot": (4.42, 0.0, s["foot_z"]), "stairTop": (4.42, CEIL, top_z),
                        "mirror": (x0, 1.5, -2.6)}}


if __name__ == "__main__":
    for key, room in {"kit": kitchen(), **basement()}.items():
        print(key, room["id"], [(o[0], K.count(o[1])[0]) for o in room["objects"]])
