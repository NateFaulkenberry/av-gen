"""All You Got, art pass 3: one coherent house (03-art-pass-3-addendum.md, section 27), every prop tagged for the
spatial validator (ADR-1051).

Pass 2's rooms each sat at the origin and the film cut between them. Pass 3's sit in one house frame, so the camera
can walk from the living room into the kitchen and down the hall, and the validator can check each room as a room.
The street is at +Z; the house's front wall is z = 2.35.

    ground floor (floor y = 0, ceiling 2.7)
        livingRoom   x -2.6..2.6   z -2.2..2.2    the front window (the intro's entry), doors to the kitchen and hall
        kitchen      x -2.6..2.6   z -6.4..-2.35  the table set for one
        hall         x 2.75..4.95  z -6.4..2.2    the front door, the stair along its right wall climbing to -Z
    basement (floor y = -2.9): laundry, storage, boilerRoom          (let it go, bars 42-49)
    upstairs (floor y = 2.85): bedroom, bathroom, study               (verse 2, bars 50-65)

A room is a dict: id, interior, objects [(name, tree, role, bmin, bmax[, opts])], lights [(name, pos, kind)],
anchors {name: point}, tableaux {tableau: (figure at, figure yaw, anchor entity id)}, words (the walls' clear
areas are the validator's business; `interior` is what word placement casts against).

Tableau furniture is placed where the tableau expects it: `fig_from_anchor()` turns the anchor's room placement
into the figure frame's.
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
import tableaux3 as TB  # noqa: E402
from kit import ACCENT, CANVAS, CANVAS2, FILL, FLOOR, GLASS, GLOW, SCREEN, D, R, S, T, U, X, box, place  # noqa: E402

CEIL = 2.7
UP = 2.85            # the upstairs floor
DOWN = -2.9          # the basement floor


def tag(node, category, eid, room, **kw):
    return ls.tag(node, category, id=eid, room=room, **kw)


def fig_from_anchor(tableau, anchor_at, anchor_yaw):
    """The figure frame's (at, yaw) in the room, given where the tableau's anchor furniture stands in it."""
    spec = TB.TABLEAUX[tableau]()
    _, (ax, ay, az), a_yaw = spec["anchor"]
    yaw = anchor_yaw - a_yaw
    r = math.radians(yaw)
    c, s = math.cos(r), math.sin(r)
    # anchor = at + R(yaw) a  ->  at = anchor - R(yaw) a
    return (anchor_at[0] - (ax * c + az * s), anchor_at[1] - ay, anchor_at[2] - (-ax * s + az * c)), yaw


def on_wall(node, wall, ext, along, y, inset=0.0):
    """Hang a wall piece (built with its back on z = 0, facing +Z) on a wall of the room `ext`."""
    (x0, x1), _, (z0, z1) = ext
    if wall == "-z":
        return place(node, (along, y, z0 + inset), 0.0)
    if wall == "+z":
        return place(node, (along, y, z1 - inset), 180.0)
    if wall == "-x":
        return place(node, (x0 + inset, y, along), 90.0)
    return place(node, (x1 - inset, y, along), -90.0)


def window_on(wl, ext, along, w, h, sill, curtains=None, eid=None, room=None):
    """A window frame (tagged) in a wall, optionally with curtains; its opening is cut by `shell_with()`."""
    (x0, x1), (y0, _), (z0, z1) = ext
    body = K.window_frame(w, h, sill, 0.0, 0.0, entity={"id": eid, "room": room} if eid else None)
    if curtains:
        body = U(body, K.curtains(w, min(2.3, sill + h + 0.4) - 0.3, min(2.35, sill + h + 0.35), 0.0, 0.0, name=curtains,
                                  entity={"id": curtains, "room": room}))
    at = {"-z": ((along, y0, z0), 0.0), "+z": ((along, y0, z1), 180.0), "-x": ((x0, y0, along), 90.0), "+x": ((x1, y0, along), -90.0)}[wl]
    return place(body, at[0], at[1])


def shell_with(ext, rid, doors=(), windows=(), tiled=False, dado=True, boards="x", extra_cuts=(), ceiling=True, sealed=False):
    """A room's walls with door and window cuts, floor lines, skirting, a dado, door frames. `doors`: (wall, along,
    width, id); `windows`: (wall, along, width, height, sill)."""
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
    walls = D(K.shell(ext, 0.15, entity=ent), *cuts)
    parts = [walls, K.tiles(ext, 0.5) if tiled else K.floorboards(ext, 0.2, boards), K.skirting(ext)]
    if dado:
        parts.append(K.wall_band(ext, y0 + 0.88, y0 + 0.92, 0.015))
    for wl, along, w, did in doors:
        parts.append(K.door_frame(wl, ext, along, w, 2.05, entity={"id": did, "room": rid}))
    return parts


def figure_objects(rid, placements, t_spans=None):
    """The room's mannequins: one SDF object per tableau (shown one at a time; the film keys `visible`).
    placements: [(object name, tableau, figure at, figure yaw, anchor id)]."""
    objs = []
    for name, tab, at, yaw, anchor in placements:
        tree, lo, hi = TB.placed(tab, name, at, yaw, room=rid, anchor_id=anchor)
        objs.append((name, tree, "figure", lo, hi, {"figure": True}))
    return objs


# =============================================================================================================
# THE GROUND FLOOR
# =============================================================================================================

def living_room():
    """The release and verse 1 (owner bars 17-32): the room the intro's camera enters through the front window.

    The sofa along the left wall (its head end, with a pillow, towards the front), a coffee table on a rug, the TV on
    a low cabinet against the right wall, the armchair in the back-right corner turned towards the front window, the
    bookcase on the back wall, the reading lamp by the sofa's head, a plant, a clock, two paintings. Doors: the
    kitchen (back wall, left), the hall (right wall, back). Tableaux: thinker (armchair), couch_lie and head_hands
    (sofa), window (the front window)."""
    rid = "livingRoom"
    ext = ((-2.6, 2.6), (0.0, CEIL), (-2.2, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("-z", -1.75, 0.9, "LivingKitchenDoor"), ("+x", -1.2, 0.9, "LivingHallDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("+z", -0.6, 1.4, 1.4, 0.8)]),
              window_on("+z", ext, -0.6, 1.4, 1.4, 0.8, curtains="livCurtain", eid="FrontWindow", room=rid))
    decor = U(on_wall(K.painting(1.1, 0.6, motif="horizon", entity={"id": "SofaPainting", "room": rid}), "-x", ext, 0.2, 1.62),
              on_wall(K.painting(0.6, 0.8, motif="portrait", entity={"id": "BackPortrait", "room": rid}), "-z", ext, 1.25, 1.6),
              on_wall(K.wall_clock(0.18, name="livClock", entity={"id": "LivingClock", "room": rid}), "+x", ext, 0.6, 1.95))
    sofa_at, sofa_yaw = (x0 + 0.46, 0.0, 0.2), 90.0
    arm_at, arm_yaw = (1.35, 0.0, -1.2), -25.0
    sofa = U(place(tag(PR.sofa3(), "couch", "Sofa", rid, seatHeight=0.42, surfaceHeight=0.42), sofa_at, sofa_yaw, name="livCouch"),
             place(tag(PR.pillow(0.5, 0.34, 0.12), "prop", "SofaPillow", rid, anchor="Sofa"),
                   (sofa_at[0] + 0.08, 0.42, sofa_at[2] + 0.66), sofa_yaw + 90.0),
             place(K.rug(2.0, 1.5, entity={"id": "LivingRug", "room": rid}), (-1.05, 0.0, 0.2), 90.0),
             place(R([0, 0, 0], K.coffee_table(1.0, 0.55, 0.42, entity={"id": "CoffeeTable", "room": rid}), name="livTableRock"),
                   (-1.05, 0.0, 0.2), 90.0, name="livTable"),
             place(K.floor_lamp(1.6, entity={"id": "ReadingLamp", "room": rid}), (x0 + 0.3, 0.0, 1.85), name="livLampAt"))
    media = U(place(tag(PR.low_cabinet(1.4, 0.48, 0.42), "cabinet", "TVCabinet", rid), (x1 - 0.22, 0.0, 0.6), -90.0),
              place(K.tv(0.9, 0.55, entity={"id": "TV", "room": rid}), (x1 - 0.3, 0.48, 0.6), -90.0, name="livTV"),
              place(tag(PR.armchair3(), "armchair", "Armchair", rid, seatHeight=0.42), arm_at, arm_yaw, name="livArm"),
              place(K.plant(1.0, entity={"id": "LivingPlant", "room": rid}), (x1 - 0.38, 0.0, z1 - 0.38), name="livPlant"))
    shelf = U(place(K.bookshelf(1.0, 1.9, 0.32, entity={"id": "Bookcase", "room": rid}), (0.0, 0.0, z0 + 0.17), name="livShelfAt"),
              place(tag(PR.side_table(), "table", "SideTable", rid), (2.2, 0.0, -1.75)),
              place(K.table_lamp(0.42, entity={"id": "SideLamp", "room": rid}), (2.2, 0.56, -1.75)))
    figs = figure_objects(rid, [("livThinker", "thinker", *fig_from_anchor("thinker", arm_at, arm_yaw), "Armchair"),
                                ("livLie", "couch_lie", *fig_from_anchor("couch_lie", sofa_at, sofa_yaw), "Sofa"),
                                ("livHands", "head_hands", *fig_from_anchor("head_hands", sofa_at, sofa_yaw), "Sofa"),
                                ("livWindow", "window", (-0.6, 0.0, z1 - 0.5), 0.0, "FrontWindow")])
    return {"id": rid, "interior": ext,
            "objects": [("livShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("livSofa", sofa, "furn", (x0 - 0.05, -0.05, -1.0), (0.0, 1.95, 2.25)),
                        ("livMedia", media, "furn2", (0.4, -0.05, -1.95), (x1 + 0.05, 2.3, z1 + 0.05)),
                        ("livShelf", shelf, "furn2", (-0.6, -0.05, z0 - 0.05), (x1 + 0.05, 2.0, -1.35)),
                        ("livDecor", decor, "furn", (x0 - 0.05, 0.9, z0 - 0.05), (x1 + 0.05, 2.3, z1 + 0.05))] + figs,
            "lights": [("livLamp", (x0 + 0.3, 1.45, 1.85), "lamp"), ("livSideLamp", (2.2, 0.95, -1.75), "lamp"),
                       ("livTVGlow", (x1 - 0.8, 0.85, 0.6), "screen")],
            "anchors": {"front": (-0.6, 1.5, z1), "armchair": (arm_at[0], 0.9, arm_at[2]), "sofa": (sofa_at[0], 0.6, sofa_at[2]),
                        "tv": (x1 - 0.3, 0.8, 0.6), "kitchenDoor": (-1.75, 1.0, z0), "hallDoor": (x1, 1.0, -1.2),
                        "shelf": (0.0, 1.0, z0), "lamp": (x0 + 0.3, 1.5, 1.85)}}


def kitchen():
    """Verse 1, bars 33-36: the counter along the back wall under the window, a fridge, wall cupboards, the table set
    for one in the middle with its chair on the far side facing the living-room door, a second chair, a pendant, a
    clock. Doors: the living room (front wall), the hall (right wall). Tableau: table."""
    rid = "kitchen"
    ext = ((-2.6, 2.6), (0.0, CEIL), (-6.4, -2.35))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+z", -1.75, 0.9, "KitchenLivingDoor"), ("+x", -4.6, 0.9, "KitchenHallDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("-z", -0.4, 1.0, 0.9, 1.15)], tiled=True, dado=False),
              window_on("-z", ext, -0.4, 1.0, 0.9, 1.15, eid="KitchenWindow", room=rid),
              on_wall(K.wall_clock(0.18, name="kitClock", entity={"id": "KitchenClock", "room": rid}), "+x", ext, -3.4, 1.9),
              on_wall(tag(PR.wall_cupboard(1.2, 0.6, 0.32), "wallCupboard", "WallCupboard", rid), "-z", ext, -1.8, 1.85),
              on_wall(K.painting(0.5, 0.65, motif="grid", entity={"id": "KitchenPicture", "room": rid}), "-x", ext, -4.4, 1.55))
    counter = U(place(tag(PR.counter3(2.6, 0.9, 0.6, sink_x=0.2), "counter", "Counter", rid), (-0.6, 0.0, z0 + 0.3)),
                place(R([0, 0, 0], K.kettle(entity={"id": "Kettle", "room": rid}), name="kitKettleRock"), (-1.5, 0.9, z0 + 0.3), name="kitKettle"),
                place(K.fridge(0.7, 1.75, 0.62, entity={"id": "Fridge", "room": rid}), (1.75, 0.0, z0 + 0.33)),
                place(K.stack_of_books(entity={"id": "CookBooks", "room": rid}), (0.35, 0.9, z0 + 0.22)))
    # the tableau's chair on the far side of the table, facing the living-room door (+Z)
    chair_at = (0.0, 0.0, -5.125)
    fig_at, fig_yaw = fig_from_anchor("table", chair_at, 0.0)
    table = U(place(K.table(1.1, 0.75, 0.75, entity={"id": "KitchenTable", "room": rid}), (0.0, 0.0, -4.4), name="kitTableAt"),
              place(K.chair(entity={"id": "KitchenChair", "room": rid, "anchor": "KitchenTable"}), chair_at, 0.0, name="kitChairB"),
              place(R([0, 0, 0], K.chair(entity={"id": "KitchenChair2", "room": rid, "anchor": "KitchenTable"}), name="kitChairARock"),
                    (0.35, 0.0, -3.675), 180.0, name="kitChairA"),
              place(K.plate_and_cup(entity={"id": "PlateAndCup", "room": rid}), (0.0, 0.75, -4.65), 180.0, name="kitPlate"),
              place(PR.pendant(0.85, name="kitPendant", eid="Pendant", room=rid), (0.0, y1, -4.4)))
    figs = figure_objects(rid, [("kitMan", "table", fig_at, fig_yaw, "KitchenChair")])
    return {"id": rid, "interior": ext,
            "objects": [("kitShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("kitCounter", counter, "furn2", (x0 - 0.05, -0.05, z0 - 0.05), (x1 + 0.05, 2.2, z0 + 0.8)),
                        ("kitTable", table, "furn", (-0.8, -0.05, -5.5), (0.8, y1 + 0.05, -3.3))] + figs,
            "lights": [("kitPend", (0.0, 1.6, -4.4), "lamp")],
            "anchors": {"table": (0.0, 0.85, -4.4), "counter": (-0.6, 1.0, z0 + 0.3), "window": (-0.4, 1.6, z0),
                        "livingDoor": (-1.75, 1.0, z1), "hallDoor": (x1, 1.0, -4.6), "man": (0.0, 1.0, -5.1)}}


STAIR = {"x0": 3.9, "x1": 4.95, "foot_z": -4.7, "steps": 16, "run": 0.27, "rise": CEIL / 16}


def riser(i):
    """The face of riser i (0 = the first, at the foot) of the hall stair, which climbs towards +Z: (centre, the
    face's normal (towards the foot), its height)."""
    s = STAIR
    return ((s["x0"] + s["x1"]) / 2, (i + 0.5) * s["rise"], s["foot_z"] + i * s["run"] - 0.003), (0.0, 0.0, -1.0), s["rise"]


def hall():
    """Bars 37-40 (and the dance's way out): a long hall down the right side of the house. The front door at +Z,
    doors to the living room and the kitchen on its left wall, a coat rail, a mirror, a phone table, a runner. The
    stair (its own object, `hallStair`, so it can stay lit while the house dissolves) climbs along the right wall from
    behind the kitchen door towards the front, up through an open stairwell: in verse 1 there is nothing above, so the
    flight carries on up out of the house to an edge (the 1:28 climb)."""
    rid = "hall"
    ext = ((2.75, 4.95), (0.0, CEIL), (-6.4, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    s = STAIR
    top_z = s["foot_z"] + s["steps"] * s["run"]
    doors = [("-x", -1.2, 0.9, "HallLivingDoor"), ("-x", -4.6, 0.9, "HallKitchenDoor"), ("+z", 3.45, 1.0, "FrontDoor")]
    well = X(((s["x0"] - 0.05, x1 + 0.3), (y1 - 0.05, y1 + 0.6), (s["foot_z"] + 1.1, top_z + 0.1)))
    flight = {"kind": "stairs", "size": [s["run"], s["rise"], (s["x1"] - s["x0"]) / 2], "count": s["steps"], "height": 0.0}
    stair = T([(s["x0"] + s["x1"]) / 2, 0.0, s["foot_z"]], R([0, -90, 0], flight))
    ls.tag(stair, "stairs", id="HallStair", room=rid)
    rail = K.SEG([s["x0"] - 0.03, 0.95, s["foot_z"]], [s["x0"] - 0.03, CEIL + 0.95, top_z], 0.022)
    post = K.SEG([s["x0"] - 0.03, 0.0, s["foot_z"]], [s["x0"] - 0.03, 0.97, s["foot_z"]], 0.03, box_section=(0.03, 0.03))
    stair_obj = U(S(stair, FILL), S(U(rail, post), ACCENT))
    shell = U(*shell_with(ext, rid, doors=doors, windows=[], boards="z", extra_cuts=[well]),
              on_wall(K.painting(0.45, 0.6, motif="portrait", entity={"id": "HallPortrait", "room": rid}), "-x", ext, 0.5, 1.6),
              place(tag(PR.front_door(1.0, 2.05, name="frontDoorSwing"), "door", "FrontDoorLeaf", rid, normal=[0, 0, -1]),
                    (3.45, 0.0, z1), 180.0))
    furn = U(on_wall(K.coat_hooks(4, entity={"id": "CoatHooks", "room": rid}), "-x", ext, 1.4, 1.7),
             on_wall(K.mirror_frame(0.55, 0.85, entity={"id": "HallMirror", "room": rid}), "-x", ext, -2.6, 1.5),
             place(K.phone_table(entity={"id": "PhoneTable", "room": rid}), (x0 + 0.24, 0.0, -2.0), 90.0),
             place(K.rug(0.8, 2.4, entity={"id": "Runner", "room": rid}), (3.35, 0.0, 0.6)))
    figs = figure_objects(rid, [("hallMan", "stair", (3.32, 0.0, -3.55), 10.0, None)])
    return {"id": rid, "interior": ext,
            "objects": [("hallShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("hallStair", stair_obj, "furn2", (s["x0"] - 0.2, -0.4, s["foot_z"] - 0.1), (x1 + 0.05, CEIL + 1.1, top_z + 0.1)),
                        ("hallFurn", furn, "furn", (x0 - 0.05, -0.05, -3.0), (4.0, 2.1, 2.2))] + figs,
            "lights": [("hallLamp", (3.4, 2.35, 0.6), "lamp"), ("hallLamp2", (3.4, 2.35, -5.4), "lamp")],
            "anchors": {"frontDoor": (3.45, 1.0, z1), "stairFoot": (4.42, 0.0, s["foot_z"]), "stairTop": (4.42, CEIL, top_z),
                        "mirror": (x0, 1.5, -2.6)}}


def ground_floor():
    return {"liv": living_room(), "kit": kitchen(), "hall": hall()}


# =============================================================================================================
# THE BASEMENT (let it go, bars 42-49): where the house keeps what it cannot let go of
# =============================================================================================================

def laundry():
    """A washer and a dryer on the left wall, a basket, a rack of boxes on the back wall, a high window, a bare bulb.
    Doors: the storage room (right wall), the boiler room (back wall)."""
    rid = "laundry"
    ext = ((-2.6, 0.9), (DOWN, DOWN + CEIL), (-2.2, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+x", 0.0, 0.9, "LaundryStorageDoor"), ("-z", -0.6, 0.9, "LaundryBoilerDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("+z", -1.0, 0.9, 0.45, 2.0)], tiled=True, dado=False),
              window_on("+z", ext, -1.0, 0.9, 0.45, 2.0, eid="LaundryWindow", room=rid),
              place(tag(PR.floor_bulb(0.5), "hangingLamp", "LaundryBulb", rid), (-0.9, y1, 0.2)))
    furn = U(place(tag(PR.washing_machine(), "appliance", "Washer", rid), (x0 + 0.33, y0, -0.4), 90.0),
             place(tag(PR.dryer(), "appliance", "Dryer", rid), (x0 + 0.33, y0, 0.3), 90.0),
             place(tag(PR.laundry_basket(), "prop", "Basket", rid), (-1.3, y0, 1.4), 15.0),
             place(tag(PR.shelving(1.4, 1.9, 0.45), "shelf", "LaundryRack", rid), (-1.6, y0, z0 + 0.25)),
             place(K.boxes(entity={"id": "LaundryBoxes", "room": rid}), (-0.25, y0, 1.6), -20.0))
    return {"id": rid, "interior": ext,
            "objects": [("lauShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("lauFurn", furn, "furn", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.0, z1 + 0.05))],
            "lights": [("lauBulb", (-0.9, y1 - 0.6, 0.2), "lamp")],
            "anchors": {"washer": (x0 + 0.33, y0 + 0.5, -0.4), "window": (-1.0, y0 + 2.2, z1), "rack": (-1.6, y0 + 1.0, z0)}}


def storage():
    """Racks of boxes, boxes stacked on the floor, an old chair upside down on a box (tilted on purpose), the
    basement stair down from the hall. Doors: the laundry (left wall), the boiler room (back wall)."""
    rid = "storage"
    ext = ((1.05, 4.95), (DOWN, DOWN + CEIL), (-2.2, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("-x", 0.0, 0.9, "StorageLaundryDoor"), ("-z", 3.0, 0.9, "StorageBoilerDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, tiled=True, dado=False),
              place(tag(PR.floor_bulb(0.45), "hangingLamp", "StorageBulb", rid), (3.0, y1, 0.0)))
    furn = U(place(tag(PR.shelving(1.4, 1.9, 0.45), "shelf", "RackA", rid), (x1 - 0.25, y0, 0.9), -90.0),
             place(tag(PR.shelving(1.4, 1.9, 0.45), "shelf", "RackB", rid), (2.2, y0, z1 - 0.25), 180.0),
             place(K.boxes(entity={"id": "BoxesA", "room": rid}), (2.0, y0, -1.2), 25.0),
             place(K.boxes(entity={"id": "BoxesB", "room": rid}), (3.9, y0, -1.5), -10.0),
             place(K.boxes(entity={"id": "BoxesC", "room": rid}), (1.6, y0, 0.9), 70.0))
    return {"id": rid, "interior": ext,
            "objects": [("stoShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("stoFurn", furn, "furn2", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.0, z1 + 0.05))],
            "lights": [("stoBulb", (3.0, y1 - 0.55, 0.0), "lamp")],
            "anchors": {"rackA": (x1 - 0.25, y0 + 1.0, 0.9), "rackB": (2.2, y0 + 1.0, z1), "centre": (3.0, y0 + 1.2, 0.0)}}


def boiler_room():
    """The water heater and its pipes, a workbench with a pegboard, boxes, a bulb. Doors: the laundry and storage
    (front wall)."""
    rid = "boilerRoom"
    ext = ((-2.6, 4.95), (DOWN, DOWN + CEIL), (-6.4, -2.35))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+z", -0.6, 0.9, "BoilerLaundryDoor"), ("+z", 3.0, 0.9, "BoilerStorageDoor")]
    pipes = U(K.SEG([x0 + 0.2, y1 - 0.15, z0 + 0.3], [x1 - 0.2, y1 - 0.15, z0 + 0.3], 0.04),
              K.SEG([x0 + 0.2, y1 - 0.28, z0 + 0.45], [x1 - 0.2, y1 - 0.28, z0 + 0.45], 0.03))
    shell = U(*shell_with(ext, rid, doors=doors, tiled=False, dado=False, boards="z"),
              S(pipes, ACCENT),
              place(tag(PR.floor_bulb(0.4), "hangingLamp", "BoilerBulb", rid), (1.2, y1, -4.4)))
    furn = U(place(tag(PR.water_heater(), "waterHeater", "WaterHeater", rid), (x1 - 0.5, y0, z0 + 0.5)),
             place(tag(PR.workbench(), "table", "Workbench", rid), (-0.4, y0, z0 + 0.32)),
             place(K.boxes(entity={"id": "BoilerBoxes", "room": rid}), (x0 + 0.5, y0, -3.0), 40.0),
             place(K.boxes(entity={"id": "BoilerBoxes2", "room": rid}), (1.5, y0, -3.3), -15.0))
    return {"id": rid, "interior": ext,
            "objects": [("boiShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("boiFurn", furn, "furn", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y1, z1 + 0.05))],
            "lights": [("boiBulb", (1.2, y1 - 0.5, -4.4), "lamp")],
            "anchors": {"heater": (x1 - 0.5, y0 + 1.0, z0 + 0.5), "bench": (-0.4, y0 + 1.2, z0 + 0.3), "centre": (1.2, y0 + 1.2, -4.4)}}


def basement():
    return {"lau": laundry(), "sto": storage(), "boi": boiler_room()}


# =============================================================================================================
# UPSTAIRS (verse 2, bars 50-65): the bedroom, the bathroom, the study, off a landing
# =============================================================================================================

def bedroom():
    """The bed with its head against the left wall, a nightstand and a lamp, a wardrobe, the front window over the
    street with curtains, a chair, a rug, a painting over the bed, a pendant. Door: the landing (right wall).
    Tableaux: bed (lying), bed_edge (sitting on its side)."""
    rid = "bedroom"
    ext = ((-2.6, 1.2), (UP, UP + CEIL), (-2.2, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+x", -1.4, 0.9, "BedroomDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("+z", -0.6, 1.2, 1.3, 0.9)]),
              window_on("+z", ext, -0.6, 1.2, 1.3, 0.9, curtains="bedCurtain", eid="BedroomWindow", room=rid),
              on_wall(K.painting(1.0, 0.55, motif="horizon", entity={"id": "BedPainting", "room": rid}), "-x", ext, 0.0, y0 + 1.6))
    bed_at, bed_yaw = (x0 + 1.09, y0, 0.0), 90.0
    furn = U(place(tag(PR.bed3(), "bed", "Bed", rid, seatHeight=0.52, surfaceHeight=0.52), bed_at, bed_yaw, name="bedAt"),
             place(K.nightstand(entity={"id": "Nightstand", "room": rid}), (x0 + 0.27, y0, -1.05)),
             place(K.table_lamp(0.45, entity={"id": "BedLamp", "room": rid}), (x0 + 0.27, y0 + 0.55, -1.05)),
             place(tag(PR.wardrobe3(1.0, 2.0, 0.58), "wardrobe", "Wardrobe", rid), (0.3, y0, z0 + 0.3)),
             place(K.rug(1.6, 1.0, entity={"id": "BedRug", "room": rid}), (0.2, y0, 0.0), 90.0),
             place(K.chair(entity={"id": "BedroomChair", "room": rid}), (0.6, y0, 1.45), -150.0),
             place(PR.pendant(0.5, name="bedPendant", eid="BedPendant", room=rid), (-0.6, y1, 0.0)))
    edge_at, edge_yaw = fig_from_anchor("bed_edge", bed_at, bed_yaw)
    figs = figure_objects(rid, [("bedLie", "bed", *fig_from_anchor("bed", bed_at, bed_yaw), "Bed"),
                                ("bedEdge", "bed_edge", edge_at, edge_yaw, "Bed")])
    return {"id": rid, "interior": ext,
            "objects": [("bedShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("bedFurn", furn, "furn", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y1, z1 + 0.05))] + figs,
            "lights": [("bedLamp", (x0 + 0.27, y0 + 0.95, -1.05), "lamp"), ("bedPend", (-0.6, y1 - 0.75, 0.0), "lamp")],
            "anchors": {"bed": (bed_at[0] + 0.3, y0 + 0.6, 0.0), "window": (-0.6, y0 + 1.5, z1), "door": (x1, y0 + 1.0, -1.4),
                        "wardrobe": (0.3, y0 + 1.0, z0)}}


def bathroom():
    """A tub along the left wall, the vanity and its mirror on the right wall, the toilet against the front wall, a
    small high window, a towel rail. Door: the landing (back wall). Tableaux: mirror, toilet."""
    rid = "bathroom"
    ext = ((1.35, 4.95), (UP, UP + CEIL), (-0.6, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("-z", 2.1, 0.8, "BathroomDoor")]
    shell = U(*shell_with(ext, rid, doors=doors, windows=[("+z", 4.2, 0.6, 0.6, 1.5)], tiled=True),
              window_on("+z", ext, 4.2, 0.6, 0.6, 1.5, eid="BathroomWindow", room=rid),
              on_wall(K.mirror_frame(0.6, 0.8, entity={"id": "BathMirror", "room": rid}), "+x", ext, 0.75, y0 + 1.55))
    vanity_at, vanity_yaw = (x1 - 0.24, y0, 0.75), -90.0
    toilet_at, toilet_yaw = (3.0, y0, z1 - 0.32), 180.0
    furn = U(place(K.bathtub(entity={"id": "Bathtub", "room": rid}), (x0 + 0.42, y0, 1.25)),
             place(tag(PR.vanity(), "sink", "Vanity", rid), vanity_at, vanity_yaw),
             place(tag(PR.toilet(), "toilet", "Toilet", rid, seatHeight=0.42), toilet_at, toilet_yaw),
             on_wall(U(X(((-0.35, 0.35), (-0.012, 0.012), (0.05, 0.08))), K.mirror([1, 0, 0], X(((0.33, 0.36), (-0.02, 0.02), (0.0, 0.08))))),
                     "-z", ext, 3.8, y0 + 1.1))
    figs = figure_objects(rid, [("bathMirror", "mirror", *fig_from_anchor("mirror", vanity_at, vanity_yaw), "Vanity"),
                                ("bathToilet", "toilet", *fig_from_anchor("toilet", toilet_at, toilet_yaw), "Toilet")])
    return {"id": rid, "interior": ext,
            "objects": [("bathShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("bathFurn", furn, "furn", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 1.3, z1 + 0.05))] + figs,
            "lights": [("bathLamp", (3.1, y1 - 0.3, 0.8), "lamp")],
            "anchors": {"mirror": (x1, y0 + 1.55, 0.75), "toilet": (3.0, y0 + 0.6, z1 - 0.3), "tub": (x0 + 0.42, y0 + 0.6, 1.25),
                        "door": (2.1, y0 + 1.0, z0)}}


def study():
    """The desk under the back window with its CRT monitor (static on its screen) and an office chair, a bookcase,
    the globe, a ceiling fan, a bare stretch of wall to sit against. Door: the landing (right wall). Tableaux: desk,
    floor_sit."""
    rid = "study"
    ext = ((-2.6, 1.2), (UP, UP + CEIL), (-6.4, -2.35))
    (x0, x1), (y0, y1), (z0, z1) = ext
    doors = [("+x", -3.0, 0.9, "StudyDoor")]
    walls = U(*shell_with(ext, rid, doors=doors, windows=[("-z", -0.9, 1.2, 1.1, 1.05)], ceiling=False))
    shell = U(K.wave(walls, 0.0, 7.0, [1.0, 0.0, 0.0], name="stuWarp"),
              window_on("-z", ext, -0.9, 1.2, 1.1, 1.05, eid="StudyWindow", room=rid),

              on_wall(K.painting(0.8, 0.55, motif="horizon", entity={"id": "StudyPainting", "room": rid}), "+x", ext, -5.0, y0 + 1.6))
    # the desk against the back wall under the window, its front (+Z) to the room; the chair in front of it
    desk_at = (-0.9, y0, z0 + 0.46)
    fig_at = (desk_at[0] - 0.2, y0, desk_at[2] + 0.6)
    group = place(TB.desk_group("Study", rid), fig_at, 180.0)
    furn = U(group, place(K.globe(0.16, name="stuGlobe", entity={"id": "Globe", "room": rid}), (-0.25, y0 + 0.75, z0 + 0.3)))
    shelf = place(K.bookshelf(0.9, 1.9, 0.3, entity={"id": "StudyBookcase", "room": rid}), (x1 - 0.16, y0, -3.6), -90.0)
    figs = figure_objects(rid, [("stuDesk", "desk", fig_at, 180.0, "StudyChair"),
                                ("stuFloor", "floor_sit", (x0 + 0.3, y0, -4.2), 90.0, None)])
    return {"id": rid, "interior": ext,
            "objects": [("stuShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("stuFurn", furn, "furn", (x0 - 0.05, y0 - 0.05, z0 - 0.05), (x1 + 0.05, y0 + 2.0, z1 + 0.05)),
                        ("stuShelf", shelf, "furn2", (x1 - 0.5, y0 - 0.05, -4.2), (x1 + 0.05, y0 + 2.0, -3.0)),
                        ("stuCeil", ceiling_halves(ext, "stu", attach_left=[place(tag(U(K.ceiling_fan(name="stuFan"), K.CY([0, -0.02, 0], 0.08, 0.04)),
                                                                                       "ceilingFan", "CeilingFan", rid), (-0.8, y1, -4.3))]),
                         "wall", (x0 - 12, y1 - 0.8, z0 - 12), (x1 + 12, y1 + 40.0, z1 + 12))] + figs,
            "lights": [("stuLamp", (-0.2, y0 + 1.6, z0 + 0.5), "lamp"), ("stuScreen", (-1.1, y0 + 1.0, z0 + 0.9), "screen")],
            "anchors": {"desk": (desk_at[0], y0 + 0.9, desk_at[2]), "monitor": (-1.1, y0 + 1.0, z0 + 0.31), "window": (-0.9, y0 + 1.6, z0),
                        "wall": (x0, y0 + 1.0, -4.2), "door": (x1, y0 + 1.0, -3.0), "fan": (-0.8, y1 - 0.3, -4.3)}}


def ceiling_halves(ext, prefix, attach_left=()):
    """A room's ceiling as its own object in two halves (`<prefix>CeilL`, `<prefix>CeilR`: named translates), so the
    roof can split and fly off (bars 65.4-66.4); anything hanging from the left half (`attach_left`) goes with it."""
    (x0, x1), (y0, y1), (z0, z1) = ext
    xm = (x0 + x1) / 2
    ceil = U(T([0, 0, 0], U(X(((x0 - 0.15, xm), (y1, y1 + 0.15), (z0 - 0.15, z1 + 0.15))), *attach_left), name=f"{prefix}CeilL"),
             T([0, 0, 0], X(((xm, x1 + 0.15), (y1, y1 + 0.15), (z0 - 0.15, z1 + 0.15))), name=f"{prefix}CeilR"))
    return S(ceil, FILL)


def upstairs():
    return {"bed": bedroom(), "bath": bathroom(), "stu": study()}


if __name__ == "__main__":
    for key, room in {**ground_floor(), **basement(), **upstairs()}.items():
        print(key, room["id"], [(o[0], K.count(o[1])[0]) for o in room["objects"]])
