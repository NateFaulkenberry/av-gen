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
from kit import ACCENT, CANVAS, CANVAS2, FILL, FLOOR, GLASS, GLOW, SCREEN, R, S, T, U, X, place  # noqa: E402
from rooms3 import CEIL, DOWN, on_wall, shell_with, tag, window_on  # noqa: E402


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
STOVE = (-1.85, 0.0, -6.4 + 0.31)


def kitchen():
    """Verse 1, bars 33-37. The back wall: the range (with the pot on its lit front-left burner, under a hood),
    the counter with its sink under the window, the fridge; the table set for one in the middle with its chair
    facing the living-room door and a second chair; a pendant; a clock. Doors: the living room (front wall), the
    hall (right wall). Tableaux: stove (dinner), table."""
    rid = "kitchen"
    ext = ((-2.6, 2.6), (0.0, CEIL), (-6.4, -2.35))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+z", -1.75, 0.9, "KitchenLivingDoor"), ("+x", -4.6, 0.9, "KitchenHallDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("-z", -0.4, 1.0, 0.9, 1.15)], tiled=True, dado=False),
              window_on("-z", ext, -0.4, 1.0, 0.9, 1.15, eid="KitchenWindow", room=rid),
              on_wall(K.wall_clock(0.18, name="kitClock", entity={"id": "KitchenClock", "room": rid}), "+x", ext, -3.4, 1.9),
              on_wall(tag(PR.wall_cupboard(0.9, 0.6, 0.32), "wallCupboard", "WallCupboard", rid), "-z", ext, 0.78, 1.85),
              on_wall(K.painting(0.5, 0.65, motif="grid", entity={"id": "KitchenPicture", "room": rid}), "-x", ext, -4.4, 1.55))
    pot_at = (STOVE[0] - 0.17, 0.9, STOVE[2] + 0.12)
    range_ = U(place(tag(P4.stove(name_ring="kitBurner"), "stove", "Stove", rid), STOVE),
               place(tag(P4.pot(), "prop", "Pot", rid, anchor="Stove"), pot_at, name="kitPot"),
               on_wall(tag(P4.range_hood(0.8, 0.5), "wallCupboard", "RangeHood", rid), "-z", ext, STOVE[0], 1.62))
    counter = U(place(tag(PR.counter3(2.2, 0.9, 0.6, sink_x=-0.05), "counter", "Counter", rid), (-0.35, 0.0, z0 + 0.3)),
                place(R([0, 0, 0], K.kettle(entity={"id": "Kettle", "room": rid}), name="kitKettleRock"), (-1.0, 0.9, z0 + 0.3), name="kitKettle"),
                place(K.fridge(0.7, 1.75, 0.62, entity={"id": "Fridge", "room": rid}), (1.75, 0.0, z0 + 0.33)),
                place(K.stack_of_books(entity={"id": "CookBooks", "room": rid}), (0.5, 0.9, z0 + 0.22)))
    chair_at = (0.0, 0.0, -5.125)
    fig_at, fig_yaw = R3.fig_from_anchor("table", chair_at, 0.0)
    table = U(place(K.table(1.1, 0.75, 0.75, entity={"id": "KitchenTable", "room": rid}), (0.0, 0.0, -4.4), name="kitTableAt"),
              place(K.chair(entity={"id": "KitchenChair", "room": rid, "anchor": "KitchenTable"}), chair_at, 0.0, name="kitChairB"),
              place(R([0, 0, 0], K.chair(entity={"id": "KitchenChair2", "room": rid, "anchor": "KitchenTable"}), name="kitChairARock"),
                    (0.35, 0.0, -3.675), 180.0, name="kitChairA"),
              place(K.plate_and_cup(entity={"id": "PlateAndCup", "room": rid}), (0.0, 0.75, -4.65), 180.0, name="kitPlate"),
              place(PR.pendant(0.85, name="kitPendant", eid="Pendant", room=rid), (0.0, y1, -4.4)))
    stove_fig, stove_yaw = fig_facing(T4.STOVE_AT, 180.0, STOVE, 0.0)
    figs = R3.figure_objects(rid, [("kitMan", "table", fig_at, fig_yaw, "KitchenChair")]) + \
        figures(rid, [("kitStove", "stove", stove_fig, stove_yaw, "Stove")])
    return {"id": rid, "interior": ext,
            "objects": [("kitShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("kitCounter", counter, "furn2", (x0 - 0.05, -0.05, z0 - 0.05), (x1 + 0.05, 2.2, z0 + 0.8)),
                        ("kitRange", range_, "furn2", (x0 - 0.05, -0.05, z0 - 0.05), (-1.3, 2.6, z0 + 0.8)),
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
              on_wall(K.mirror_frame(2.2, 1.3, entity={"id": "GymMirror", "room": rid}), "+x", ext, -0.2, y0 + 1.45),
              place(tag(PR.floor_bulb(0.45), "hangingLamp", "GymBulb", rid), (3.0, y1, 0.0)))
    furn = U(place(tag(P4.squat_rack(), "gymRack", "SquatRack", rid), (2.2, y0, z1 - 0.55), 180.0),
             place(tag(P4.weight_bench(), "gymBench", "WeightBench", rid), (3.15, y0, 0.15)),
             place(tag(P4.dumbbell_rack(1.1), "gymRack", "DumbbellRack", rid), (x1 - 0.25, y0, -0.2), -90.0),
             place(tag(P4.exercise_mat(), "rug", "GymMat", rid), (1.75, y0, -1.0)),
             place(tag(P4.exercise_bike(), "gymBike", "ExerciseBike", rid), (4.2, y0, 1.55), -90.0))
    figs = figures(rid, [("gymMan", "gym_stand", (3.75, y0, -1.2), 75.0, None)])
    return {"id": rid, "interior": ext,
            "objects": [("gymShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("gymFurn", furn, "furn2", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.3, z1 + 0.05))] + figs,
            "lights": [("gymBulb", (3.0, y1 - 0.55, 0.0), "lamp")],
            "anchors": {"mirror": (x1, y0 + 1.45, -0.2), "bench": (3.15, y0 + 0.5, 0.15), "rack": (2.2, y0 + 1.2, z1),
                        "man": (3.75, y0 + 1.0, -1.2)}}


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
                        ("lngBar", bar, "furn2", (2.0, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.0, z1 + 0.05),
                         {"emission": {CANVAS: [1.4, 0.3, 0.9], CANVAS2: [0.25, 1.3, 1.5], GLOW: [1.6, 0.9, 0.35]}}),
                        ("lngStools", U(stools, glass), "furn", (2.0, y0 - 0.05, -5.6), (3.4, y0 + 1.3, -3.2))] + figs,
            "lights": [("lngBarGlow", (4.2, y0 + 1.3, -4.4), "lamp"), ("lngPend", (3.6, y1 - 0.85, -4.4), "lamp"),
                       ("lngLamp", (x0 + 0.32, y0 + 1.4, -5.95), "lamp"), ("lngTV", (0.0, y0 + 0.9, -4.4), "screen")],
            "anchors": {"bar": (BAR_COUNTER[0], y0 + 1.1, -4.4), "barMan": bar_fig, "sofa": sofa_at, "tv": (0.5, y0 + 0.86, -4.4),
                        "loungeMan": lounge_fig}}


def basement():
    return {"lau": laundry(), "gym": gym(), "lng": lounge()}


if __name__ == "__main__":
    for key, room in {"kit": kitchen(), **basement()}.items():
        print(key, room["id"], [(o[0], K.count(o[1])[0]) for o in room["objects"]])
