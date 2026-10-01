"""All You Got, art pass 2: the rooms, furnished by hand (kit.py), each as two to four SDF objects.

A room function returns a dict: `objects` [(name, tree, role, bmin, bmax)], `lights` [(name, pos, kind)],
`anchors` {name: point} (where things are, for framing shots and placing words), and `interior` extents.
`role` names the object's line colour in the palette: "wall", "furn", "furn2", "figure".

Every room is small (3-6 m), lit by its own lamps, and has a window, a picture, a light source and a strange
object, because the brief forbids barren rooms (02-art-pass-2.md, sections 2-3).
"""

from __future__ import annotations

import kit as K
from kit import ACCENT, CANVAS, CANVAS2, FILL, FLOOR, GLASS, GLOW, SCREEN, D, R, S, T, U, X, box, place


def painting_plain(w=0.8, h=0.6, depth=0.04, canvas=CANVAS, k=FILL):
    """A frame and a glowing canvas (8 nodes)."""
    fr = 0.05
    return U(S(D(X(((-w / 2, w / 2), (-h / 2, h / 2), (0.0, depth))),
                 X(((-w / 2 + fr, w / 2 - fr), (-h / 2 + fr, h / 2 - fr), (depth - 0.012, depth + 0.1)))), k),
             S(X(((-w / 2 + fr, w / 2 - fr), (-h / 2 + fr, h / 2 - fr), (0.0, depth - 0.012))), canvas))


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


def living_room():
    """The release's room (owner bars 17-24), revisited in verse 1 and verse 2. 5.2 x 2.7 x 4.4 m.

    Layout (looking from the front window at +z towards -z): the couch along the left wall facing the TV on the
    right wall, a coffee table on a rug between them, the bookcase on the back wall beside the kitchen door, a
    floor lamp in the back-left corner, a plant in the back-right corner, an armchair by the front window, a
    window with curtains on the right wall, a clock above the TV, pictures over the couch and on the back wall.
    """
    ext = ((-2.6, 2.6), (0.0, 2.7), (-2.2, 2.2))
    (x0, x1), (y0, y1), (z0, z1) = ext
    cuts = [K.door_cut("-z", ext, 1.6, 0.9, 2.05),
            K.window_cut(1.2, 1.3, 0.9, x1 - 0.1, x1 + 0.1) if False else X(((x1 - 0.5, x1 + 0.5), (0.9, 2.2), (0.3, 1.5))),
            X(((-1.3, 0.1), (0.8, 2.2), (z1 - 0.5, z1 + 0.5)))]
    cuts.append(X(((x0 - 0.3, x1 + 0.3), (y1 - 0.02, y1 + 0.4), (z0 - 0.3, z1 + 0.3))))   # the ceiling is its own object
    walls = D(K.shell(ext, 0.15), *cuts)
    # the right window (in the plane x = x1): build in its own frame (facing +Z) then turn it onto the wall
    win_r = place(U(K.window_frame(1.2, 1.3, 0.9, 0.0, 0.0), K.curtains(1.2, 1.75, 2.35, 0.0, 0.0, name="livCurtain")),
                  (x1, 0.0, 0.9), -90.0)
    win_f = place(K.window_frame(1.4, 1.4, 0.8, 0.0, 0.0), (-0.6, 0.0, z1), 180.0)
    dado = K.wall_band(ext, 0.88, 0.92, 0.015)
    shell = U(walls, K.floorboards(ext, 0.2, "x"), K.skirting(ext), dado, K.door_frame("-z", ext, 1.6, 0.9, 2.05),
              win_r, win_f)
    sofa = U(place(K.couch(2.0, 0.88, 0.82), (x0 + 0.46, 0.0, -0.1), 90.0, name="livCouch"),
             place(K.rug(2.0, 1.5), (-0.95, 0.0, -0.1), 90.0),
             place(K.coffee_table(1.0, 0.55, 0.42), (-0.95, 0.0, -0.1), 90.0, name="livTable"),
             place(K.floor_lamp(1.6), (x0 + 0.35, 0.0, z0 + 0.35), name="livLampAt"),
             on_wall(K.painting(1.1, 0.7, motif="horizon"), "-x", ext, -0.1, 1.62))
    media = U(place(K.cabinet(1.4, 0.48, 0.42), (x1 - 0.23, 0.0, -0.1), -90.0),
              place(K.tv(0.9, 0.55), (x1 - 0.3, 0.48, -0.1), -90.0, name="livTV"),
              on_wall(K.wall_clock(0.19, name="livClock"), "+x", ext, -0.1, 2.05),
              place(K.armchair(), (1.0, 0.0, 1.45), 205.0, name="livArm"))
    shelf = U(place(K.bookshelf(1.0, 1.9, 0.32), (-0.2, 0.0, z0 + 0.17), name="livShelfAt"),
              place(K.plant(1.0), (x1 - 0.4, 0.0, z0 + 0.4), name="livPlant"),
              on_wall(K.painting(0.7, 0.9, motif="portrait"), "-z", ext, -1.75, 1.55))
    man = place(K.mannequin("sit", name="livMan"), (x0 + 0.62, 0.0, -0.45), 90.0, name="livManAt")
    hw = (x1 - x0) / 2 + 0.15
    ceil = U(T([0, 0, 0], X(((x0 - 0.15, 0.0), (y1, y1 + 0.15), (z0 - 0.15, z1 + 0.15))), name="ceilL"),
             T([0, 0, 0], X(((0.0, x1 + 0.15), (y1, y1 + 0.15), (z0 - 0.15, z1 + 0.15))), name="ceilR"),
             T([0, 0, 0], K.CY([0, y1 - 0.04, 0], 0.18, 0.08), name="ceilRose"))
    return {
        "interior": ext,
        "objects": [("livShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                    ("livSofa", sofa, "furn", (x0 - 0.1, -0.05, z0 - 0.1), (x1 + 0.1, 1.95, 1.0)),
                    ("livMedia", media, "furn2", (0.2, -0.05, -1.0), (x1 + 0.05, 2.4, z1 + 0.05)),
                    ("livShelf", shelf, "furn2", (-2.2, -0.05, z0 - 0.05), (x1 + 0.05, 2.1, z0 + 0.9)),
                    ("livMan", man, "figure", (x0 + 0.05, -0.05, -1.2), (x0 + 1.6, 1.5, 0.3)),
                    ("livCeil", S(ceil, FILL), "wall", (x0 - 12, y1 - 0.3, z0 - 12), (x1 + 12, y1 + 40.0, z1 + 12))],
        "lights": [("livLamp", (x0 + 0.35, 1.45, z0 + 0.35), "lamp"), ("livTV", (x1 - 0.75, 0.85, -0.1), "screen")],
        "anchors": {"couch": (x0 + 0.5, 0.6, -0.1), "tv": (x1 - 0.3, 0.8, -0.1), "door": (1.6, 1.0, z0),
                    "lamp": (x0 + 0.35, 1.5, z0 + 0.35), "window": (x1, 1.5, 0.9), "front": (-0.6, 1.5, z1),
                    "shelf": (-0.2, 1.0, z0 + 0.2), "man": (x0 + 0.62, 1.0, -0.45), "centre": (0.0, 1.2, 0.0)},
    }


def _shell_bits(ext, door=None, windows=(), boards="x", tiled=False, dado=True, wall=0.15):
    """Walls (with cuts), floor lines, skirting and a dado. `door`: (wall, along, width); `windows`: a list of
    (wall, along, width, height, sill)."""
    (x0, x1), (y0, y1), (z0, z1) = ext
    cuts = []
    if door:
        cuts.append(K.door_cut(door[0], ext, door[1], door[2], 2.05))
    for wl, along, w, h, sill in windows:
        if wl == "+x":
            cuts.append(X(((x1 - 0.5, x1 + 0.5), (sill, sill + h), (along - w / 2, along + w / 2))))
        elif wl == "-x":
            cuts.append(X(((x0 - 0.5, x0 + 0.5), (sill, sill + h), (along - w / 2, along + w / 2))))
        elif wl == "+z":
            cuts.append(X(((along - w / 2, along + w / 2), (sill, sill + h), (z1 - 0.5, z1 + 0.5))))
        else:
            cuts.append(X(((along - w / 2, along + w / 2), (sill, sill + h), (z0 - 0.5, z0 + 0.5))))
    parts = [D(K.shell(ext, wall), *cuts), K.tiles(ext, 0.5) if tiled else K.floorboards(ext, 0.22, boards),
             K.skirting(ext)]
    if dado:
        parts.append(K.wall_band(ext, 0.88, 0.92, 0.015))
    if door:
        parts.append(K.door_frame(door[0], ext, door[1], door[2], 2.05))
    return parts


def _window(wl, ext, along, w, h, sill, curtains=None):
    (x0, x1), _, (z0, z1) = ext
    body = K.window_frame(w, h, sill, 0.0, 0.0)
    if curtains:
        body = U(body, K.curtains(w, min(2.3, sill + h + 0.4) - 0.3, min(2.35, sill + h + 0.35), 0.0, 0.0, name=curtains))
    if wl == "-z":
        return place(body, (along, 0.0, z0), 0.0)
    if wl == "+z":
        return place(body, (along, 0.0, z1), 180.0)
    if wl == "-x":
        return place(body, (x0, 0.0, along), 90.0)
    return place(body, (x1, 0.0, along), -90.0)


def bedroom():
    """Verse 1, bars 25-28 (and verse 2): the bed against the back wall, nightstands and a lamp, a wardrobe, a
    window with curtains on the left, a picture over the bed, a chair, a rug, a pendant, and the mannequin
    standing in the back-right corner facing the wall. 4.0 x 2.6 x 3.6 m."""
    ext = ((-2.0, 2.0), (0.0, 2.6), (-1.8, 1.8))
    (x0, x1), (y0, y1), (z0, z1) = ext
    shell = U(*_shell_bits(ext, door=("+z", 1.2, 0.9), windows=[("-x", 0.3, 1.1, 1.3, 0.9)]),
              _window("-x", ext, 0.3, 1.1, 1.3, 0.9, curtains="bedCurtain"),
              on_wall(K.painting(1.0, 0.55, motif="horizon"), "-z", ext, -0.3, 1.62),
              place(K.hanging_lamp(0.7, name="bedPendant"), (0.0, y1, 0.2)))
    furn = U(place(K.bed(1.5, 2.05), (-0.3, 0.0, z0 + 1.05)),
             place(K.nightstand(), (-1.35, 0.0, z0 + 0.25)), place(K.nightstand(), (0.75, 0.0, z0 + 0.25)),
             place(K.table_lamp(0.45), (-1.35, 0.55, z0 + 0.25)),
             place(K.wardrobe(1.0, 2.0, 0.58), (x1 - 0.3, 0.0, 0.55), -90.0),
             place(K.rug(1.6, 1.0), (-0.3, 0.0, 0.85)),
             place(K.chair(), (1.2, 0.0, -0.75), -60.0))
    man = place(K.mannequin("stand", name="bedMan"), (x1 - 0.42, 0.0, z0 + 0.38), 135.0)
    return {"interior": ext,
            "objects": [("bedShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("bedFurn", furn, "furn", (x0 - 0.05, -0.05, z0 - 0.1), (x1 + 0.05, 2.1, 1.5)),
                        ("bedMan", man, "figure", (x1 - 0.9, -0.05, z0 - 0.05), (x1 + 0.05, 1.95, z0 + 0.9))],
            "lights": [("bedLamp", (-1.35, 0.95, z0 + 0.25), "lamp"), ("bedPend", (0.0, 1.75, 0.2), "lamp")],
            "anchors": {"bed": (-0.3, 0.6, z0 + 1.0), "wall": (-0.3, 1.85, z0), "window": (x0, 1.5, 0.3),
                        "man": (x1 - 0.42, 1.2, z0 + 0.38), "door": (1.2, 1.0, z1), "lamp": (-1.35, 0.8, z0 + 0.25),
                        "ceiling": (0.0, y1, 0.0), "wardrobe": (x1 - 0.3, 1.0, 0.55)}}


def kitchen():
    """Verse 1, bars 33-36 (and verse 2, dance): a counter with a sink and a kettle under a window on the back
    wall, upper cupboards, a fridge, a table set for one with two chairs under a swinging pendant, a clock.
    4.4 x 2.6 x 3.6 m."""
    ext = ((-2.2, 2.2), (0.0, 2.6), (-1.8, 1.8))
    (x0, x1), (y0, y1), (z0, z1) = ext
    shell = U(*_shell_bits(ext, door=("-x", 1.0, 0.9), windows=[("-z", -0.7, 1.0, 0.9, 1.15)], tiled=True, dado=False),
              _window("-z", ext, -0.7, 1.0, 0.9, 1.15),
              on_wall(K.wall_clock(0.18, name="kitClock"), "+x", ext, 0.2, 1.85),
              on_wall(K.painting(0.5, 0.65, motif="grid"), "+x", ext, -1.0, 1.5))
    counter = U(place(K.counter(2.6, 0.9, 0.6), (-0.6, 0.0, z0 + 0.3)),
                place(K.kettle(), (-1.5, 0.9, z0 + 0.3), name="kitKettle"),
                place(K.fridge(0.7, 1.75, 0.62), (1.55, 0.0, z0 + 0.33)),
                place(K.cabinet(1.2, 0.55, 0.32, 3), (0.35, 1.55, z0 + 0.16)),
                place(K.cabinet(0.9, 0.55, 0.32, 3), (-1.65, 1.55, z0 + 0.16)) if False else
                place(K.stack_of_books(), (0.6, 0.9, z0 + 0.25)))
    table = U(place(K.table(1.1, 0.75, 0.75), (0.0, 0.0, 0.45), name="kitTableAt"),
              place(K.chair(), (0.0, 0.0, 1.2), 180.0, name="kitChairA"), place(K.chair(), (0.0, 0.0, -0.3), name="kitChairB"),
              place(K.plate_and_cup(), (0.0, 0.75, 0.6), name="kitPlate"),
              place(K.hanging_lamp(0.85, name="kitPendant"), (0.0, y1, 0.45)))
    return {"interior": ext,
            "objects": [("kitShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("kitCounter", counter, "furn2", (x0 - 0.05, -0.05, z0 - 0.05), (x1 + 0.05, 2.15, z0 + 0.8)),
                        ("kitTable", table, "furn", (-0.8, -0.05, -0.9), (0.8, y1 + 0.05, 1.7))],
            "lights": [("kitPend", (0.0, 1.55, 0.45), "lamp")],
            "anchors": {"table": (0.0, 0.85, 0.45), "counter": (-0.6, 1.0, z0 + 0.3), "fridge": (1.55, 1.0, z0 + 0.4),
                        "window": (-0.7, 1.6, z0), "lamp": (0.0, 1.6, 0.45), "door": (x0, 1.0, 1.0),
                        "clock": (x1, 1.85, 0.2), "cupboards": (0.35, 1.8, z0 + 0.2)}}


def hallway():
    """Verse 1, bars 37-40: a narrow passage (1.5 m) with coat hooks, a mirror, a hall table with a telephone, a
    runner, a picture, and at its end a short stair of eight risers up to a door (one word per riser).
    Runs along -z: the stair's foot is at z = -3.2. 1.5 x 2.6 x 6.2 m."""
    ext = ((-0.75, 0.75), (0.0, 2.6), (-5.2, 1.0))
    (x0, x1), (y0, y1), (z0, z1) = ext
    steps, run, rise = 8, 0.27, 0.18
    foot = -3.0
    top_y = steps * rise
    stair = K.T([0.0, 0.0, foot], K.R([0, 90, 0], {"kind": "stairs", "size": [run, rise, 0.65], "count": steps, "height": 0.0}))
    landing = X(((x0, x1), (0.0, top_y), (z0, foot - steps * run)))
    door_top = X(((-0.45, 0.45), (top_y, top_y + 2.0), (z0 - 0.5, z0 + 0.5)))
    walls = D(K.shell(ext, 0.15), door_top)
    shell = U(walls, K.floorboards(((x0, x1), (y0, y1), (foot, z1)), 0.18, "z"), K.skirting(ext), K.wall_band(ext, 0.88, 0.92, 0.015),
              S(stair, FILL), S(landing, FILL),
              S(D(X(((-0.53, 0.53), (top_y, top_y + 2.1), (z0, z0 + 0.04))), X(((-0.45, 0.45), (top_y - 0.1, top_y + 2.0), (z0 - 0.2, z0 + 0.2)))), FILL),
              on_wall(K.painting(0.45, 0.6, motif="portrait"), "-x", ext, -1.2, 1.55))
    furn = U(on_wall(K.coat_hooks(4), "+x", ext, 0.2, 1.7),
             on_wall(K.mirror_frame(0.55, 0.85), "+x", ext, -1.4, 1.5),
             place(K.phone_table(), (x1 - 0.22, 0.0, -1.4), -90.0),
             place(K.rug(0.8, 2.6), (0.0, 0.0, -1.3)))
    return {"interior": ext,
            "objects": [("hallShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("hallFurn", furn, "furn", (x0 - 0.05, -0.05, -2.8), (x1 + 0.05, 2.1, 0.8))],
            "lights": [("hallLamp", (0.0, 2.3, -1.0), "lamp")],
            "anchors": {"stair": (0.0, 0.5, foot - 1.0), "door": (0.0, top_y + 1.0, z0), "mirror": (x1, 1.5, -1.4),
                        "phone": (x1 - 0.22, 0.85, -1.4), "riser0": (0.0, rise / 2, foot), "run": run, "rise": rise,
                        "foot": foot, "steps": steps}}


def study():
    """Let it go and verse 2: a desk under a window with a lamp and the globe, a chair, a bookcase, a filing
    cabinet, a picture, a ceiling fan. 3.6 x 2.6 x 3.4 m."""
    ext = ((-1.8, 1.8), (0.0, 2.6), (-1.7, 1.7))
    (x0, x1), (y0, y1), (z0, z1) = ext
    shell = U(*_shell_bits(ext, door=("+z", -1.0, 0.9), windows=[("-z", 0.3, 1.2, 1.2, 1.0)]),
              _window("-z", ext, 0.3, 1.2, 1.2, 1.0, curtains="stuCurtain"),
              on_wall(K.painting(0.8, 0.55, motif="horizon"), "+x", ext, -0.2, 1.6),
              place(K.ceiling_fan(name="stuFan"), (0.0, y1, 0.2)))
    furn = U(place(K.desk(1.3, 0.62, 0.75), (0.3, 0.0, z0 + 0.36)),
             place(K.globe(0.16, name="stuGlobe"), (0.75, 0.75, z0 + 0.32)),
             place(K.table_lamp(0.4), (-0.2, 0.75, z0 + 0.25)),
             place(K.chair(), (0.2, 0.0, z0 + 1.0), 180.0),
             place(K.bookshelf(0.9, 1.9, 0.3), (x0 + 0.16, 0.0, 0.2), 90.0))
    return {"interior": ext,
            "objects": [("stuShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("stuFurn", furn, "furn", (x0 - 0.05, -0.05, z0 - 0.05), (x1 + 0.05, 2.1, 0.9))],
            "lights": [("stuLamp", (-0.2, 1.1, z0 + 0.25), "lamp")],
            "anchors": {"desk": (0.3, 0.9, z0 + 0.4), "globe": (0.75, 1.05, z0 + 0.32), "window": (0.3, 1.6, z0),
                        "shelf": (x0 + 0.16, 1.0, 0.2), "fan": (0.0, y1 - 0.3, 0.2), "door": (-1.0, 1.0, z1),
                        "wallR": (x1, 1.5, 0.0)}}


def bathroom():
    """Let it go: a tub, a sink with a mirror, a towel rail, a small window, a tiled floor. 3.0 x 2.6 x 2.8 m."""
    ext = ((-1.5, 1.5), (0.0, 2.6), (-1.4, 1.4))
    (x0, x1), (y0, y1), (z0, z1) = ext
    shell = U(*_shell_bits(ext, door=("+z", 0.8, 0.8), windows=[("-x", -0.5, 0.6, 0.6, 1.5)], tiled=True),
              _window("-x", ext, -0.5, 0.6, 0.6, 1.5),
              on_wall(K.mirror_frame(0.6, 0.8), "-z", ext, 0.75, 1.55))
    sink = U(X(((-0.32, 0.32), (0.78, 0.9), (-0.25, 0.25))), X(((-0.06, 0.06), (0.0, 0.78), (-0.06, 0.06))),
             K.SEG([0, 0.9, -0.2], [0, 1.05, -0.08], 0.015))
    furn = U(place(K.bathtub(), (x0 + 0.48, 0.0, 0.25)),
             place(sink, (0.75, 0.0, z0 + 0.3)),
             on_wall(U(X(((-0.35, 0.35), (-0.012, 0.012), (0.05, 0.08))), K.mirror([1, 0, 0], X(((0.33, 0.36), (-0.02, 0.02), (0.0, 0.08))))), "+x", ext, 0.4, 1.1))
    return {"interior": ext,
            "objects": [("bathShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                        ("bathFurn", furn, "furn", (x0 - 0.05, -0.05, z0 - 0.05), (x1 + 0.05, 1.3, 1.2))],
            "lights": [("bathLamp", (0.0, 2.3, 0.0), "lamp")],
            "anchors": {"tub": (x0 + 0.48, 0.6, 0.25), "mirror": (0.75, 1.55, z0), "window": (x0, 1.8, -0.5),
                        "door": (0.8, 1.0, z1), "wallR": (x1, 1.5, 0.4)}}


GALLERY_ITEMS = [  # name, builder, place on its pedestal (radius, angle deg, height), palette role
    ("galLamp", lambda: K.floor_lamp(1.25), 2.0, 0.0, 0.0, "jewel1"),
    ("galGlobe", lambda: K.globe(0.2), 2.0, 40.0, 0.75, "jewel2"),
    ("galClock", lambda: R([-90, 0, 0], K.wall_clock(0.24)), 2.0, 80.0, 1.0, "jewel3"),
    ("galTV", lambda: K.tv(0.8, 0.5), 2.0, 120.0, 0.6, "jewel4"),
    ("galPlant", lambda: K.plant(0.9), 2.0, 160.0, 0.6, "jewel5"),
    ("galChair", lambda: K.chair(), 2.0, 200.0, 0.3, "jewel6"),
    ("galCup", lambda: K.plate_and_cup(), 2.0, 240.0, 1.05, "jewel1"),
    ("galFrame", lambda: K.T([0, 0.4, 0], K.painting(0.6, 0.8, motif="portrait")), 2.0, 280.0, 0.75, "jewel2"),
    ("galBooks", lambda: K.stack_of_books(), 2.0, 320.0, 1.05, "jewel3"),
]


def gallery():
    """Bridge 2: one dark square room; the mannequin on a chair at its centre, back to the camera's first view;
    around it on pedestals of different heights, the house's things, each its own SDF object so each has its own
    jewel colour and its own spin (`sdf/<name>/node/<name>Spin/rotation`). 7 x 3.2 x 7 m."""
    import math as _m
    ext = ((-3.5, 3.5), (0.0, 3.2), (-3.5, 3.5))
    (x0, x1), (y0, y1), (z0, z1) = ext
    shell = U(D(K.shell(ext, 0.15)), K.tiles(ext, 0.7), K.skirting(ext), K.wall_band(ext, 2.4, 2.45, 0.02))
    objs = [("galShell", shell, "wall", (x0 - 0.5, -0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5))]
    anchors = {}
    for name, builder, r, ang, h, role in GALLERY_ITEMS:
        a = _m.radians(ang)
        px, pz = r * _m.sin(a), r * _m.cos(a)
        ped = X(((-0.22, 0.22), (0.0, h), (-0.22, 0.22))) if h > 0.05 else None
        item = R([0, 0, 0], builder(), name=name + "Spin")
        body = U(*(c for c in (ped, T([0, h, 0], item)) if c is not None))
        tree = place(body, (px, 0.0, pz), ang + 180.0)
        objs.append((name, tree, role, (px - 1.2, -0.05, pz - 1.2), (px + 1.2, h + 1.9, pz + 1.2)))
        anchors[name] = (px, h + 0.4, pz)
    man = U(place(K.chair(), (0.0, 0.0, 0.0), 180.0), place(K.mannequin("sit", name="galMan"), (0.0, 0.0, 0.08), 180.0))
    objs.append(("galMan", man, "figure", (-0.7, -0.05, -0.8), (0.7, 1.6, 0.8)))
    return {"interior": ext, "objects": objs,
            "lights": [("galKey", (0.0, 2.9, 0.0), "lamp")],
            "anchors": dict(anchors, man=(0.0, 1.0, 0.0), centre=(0.0, 1.2, 0.0))}



def clutter():
    """Verse 2's density: the same home, stranger. Per room, an object shown only in verse 2 (each about 40-70
    nodes): chairs stacked on the coffee table and boxes by the door (living room); boxes and a chair on the bed
    (bedroom); chairs upturned on the table and a tower of plates (kitchen); a tower of books on the desk (study)."""
    def stacked_chairs(n=3):
        return U(*[place(K.chair(), (0.02 * i, 0.47 * i, 0.0), 180.0 * (i % 2) + 9 * i) for i in range(n)])
    liv = U(place(stacked_chairs(3), (-0.95, 0.42, -0.1), 15.0),
            place(K.boxes(), (1.1, 0.0, -1.75), 20.0),
            place(K.boxes(), (2.0, 0.0, 1.6), -35.0))
    bed = U(place(K.boxes(), (-0.6, 0.55, -0.6), 12.0),
            place(R([180, 0, 0], K.chair()), (0.3, 1.47, -0.2), 30.0))
    kit = U(place(R([180, 0, 0], K.chair()), (-0.25, 1.68, 0.45), 8.0),
            place(R([180, 0, 0], K.chair()), (0.3, 1.68, 0.45), -14.0),
            T([0.35, 0.75, 0.75], K.repeat([0, 0.05, 0], 6, K.cyl(0.13, 0.016))),
            place(K.boxes(), (1.6, 0.0, 1.2), 25.0))
    stu = U(*[place(K.stack_of_books(), (0.6 - 0.02 * i, 0.75 + 0.11 * i, -1.4), 23.0 * i) for i in range(6)])
    return {"livClutter": (liv, (-2.7, -0.05, -2.3), (2.7, 2.6, 2.3)), "bedClutter": (bed, (-2.0, -0.05, -1.8), (2.0, 2.6, 1.8)),
            "kitClutter": (kit, (-2.2, -0.05, -1.8), (2.2, 2.6, 1.8)), "stuClutter": (stu, (-1.8, -0.05, -1.7), (1.8, 2.6, 1.7))}
