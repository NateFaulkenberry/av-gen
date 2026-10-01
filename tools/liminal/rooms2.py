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
    walls = D(K.shell(ext, 0.15), *cuts)
    # the right window (in the plane x = x1): build in its own frame (facing +Z) then turn it onto the wall
    win_r = place(U(K.window_frame(1.2, 1.3, 0.9, 0.0, 0.0), K.curtains(1.2, 1.75, 2.35, 0.0, 0.0, name="livCurtain")),
                  (x1, 0.0, 0.9), -90.0)
    win_f = place(K.window_frame(1.4, 1.4, 0.8, 0.0, 0.0), (-0.6, 0.0, z1), 180.0)
    dado = K.wall_band(ext, 0.88, 0.92, 0.015)
    shell = U(walls, K.floorboards(ext, 0.2, "x"), K.skirting(ext), dado, K.door_frame("-z", ext, 1.6, 0.9, 2.05),
              win_r, win_f)
    sofa = U(place(K.couch(2.0, 0.88, 0.82), (x0 + 0.46, 0.0, -0.1), 90.0),
             place(K.rug(2.0, 1.5), (-0.95, 0.0, -0.1), 90.0),
             place(K.coffee_table(1.0, 0.55, 0.42), (-0.95, 0.0, -0.1), 90.0),
             place(K.floor_lamp(1.6), (x0 + 0.35, 0.0, z0 + 0.35)),
             on_wall(K.painting(1.1, 0.7, motif="horizon"), "-x", ext, -0.1, 1.62))
    media = U(place(K.cabinet(1.4, 0.48, 0.42), (x1 - 0.23, 0.0, -0.1), -90.0),
              place(K.tv(0.9, 0.55), (x1 - 0.3, 0.48, -0.1), -90.0),
              on_wall(K.wall_clock(0.19, name="livClock"), "+x", ext, -0.1, 2.05),
              place(K.armchair(), (1.0, 0.0, 1.45), 205.0))
    shelf = U(place(K.bookshelf(1.0, 1.9, 0.32), (-0.2, 0.0, z0 + 0.17)),
              place(K.plant(1.0), (x1 - 0.4, 0.0, z0 + 0.4)),
              on_wall(K.painting(0.7, 0.9, motif="portrait"), "-z", ext, -1.75, 1.55))
    man = place(K.mannequin("sit", name="livMan"), (x0 + 0.62, 0.0, -0.45), 90.0)
    return {
        "interior": ext,
        "objects": [("livShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5)),
                    ("livSofa", sofa, "furn", (x0 - 0.1, -0.05, z0 - 0.1), (x1 + 0.1, 1.95, 1.0)),
                    ("livMedia", media, "furn2", (0.2, -0.05, -1.0), (x1 + 0.05, 2.4, z1 + 0.05)),
                    ("livShelf", shelf, "furn2", (-2.2, -0.05, z0 - 0.05), (x1 + 0.05, 2.1, z0 + 0.9)),
                    ("livMan", man, "figure", (x0 + 0.05, -0.05, -1.2), (x0 + 1.6, 1.5, 0.3))],
        "lights": [("livLamp", (x0 + 0.35, 1.45, z0 + 0.35), "lamp"), ("livTV", (x1 - 0.75, 0.85, -0.1), "screen")],
        "anchors": {"couch": (x0 + 0.5, 0.6, -0.1), "tv": (x1 - 0.3, 0.8, -0.1), "door": (1.6, 1.0, z0),
                    "lamp": (x0 + 0.35, 1.5, z0 + 0.35), "window": (x1, 1.5, 0.9), "front": (-0.6, 1.5, z1),
                    "shelf": (-0.2, 1.0, z0 + 0.2), "man": (x0 + 0.62, 1.0, -0.45), "centre": (0.0, 1.2, 0.0)},
    }
