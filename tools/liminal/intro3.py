"""All You Got, art pass 3: the intro (03-art-pass-3-addendum.md, sections 23-26). No abstract geometry: from the
count-in to the downbeat of bar 17 the whole intro is a neighbourhood being violently constructed, the street, the
houses on both sides of it, the street lamps, the trees, a skyline, and at last the house we enter.

Everything is in the house frame (rooms3.py): the hero house stands at x -2.75..5.1, its front wall at z = 2.35
facing the street; the road's centre line is z = 12. Each object below is one SDF object; every moving part is a
named node the film keys on the grid (intro_keys in film3.py):

  street      the ground slab, its grid (`gridClip`, a box whose size reveals the grid), the kerbs and pavements,
              the road's centre dashes (`dashClip`)
  row         the neighbours: one house module repeated every 13 m along the street and mirrored across it,
              revealed outwards from the hero lot by `rowClip`; the module's roof (`rowRoof`) and windows mutate
  faller      one more copy of the module that drops from the sky onto the next lot on each beat (`fall`),
              mirrored both ways, so four houses land at once
  lamps       street lamps and trees on both pavements (`lampClip` grows along the street on the eighths, the trees
              `treeGrow`)
  skyline     a far city of towers (`towerA`, `towerB`: their heights rise on the sustained bass)
  hero        the house we enter: walls rising out of the ground (`heroRise`), the front window's lit pane
              (`heroPane`), and named parts that fly in (`hp_<part>`)
"""

from __future__ import annotations

import math

import kit as K
from kit import ACCENT, CANVAS, CANVAS2, FILL, FLOOR, GLASS, GLOW, R, S, T, U, X, box

ROAD_Z = 12.0
LOT = 13.0            # house spacing along the street
HERO_X = 1.175        # the hero house's centre (x)
LOTS = 7              # lots each way from the hero lot
HERO = {"x0": -2.75, "x1": 5.1, "z0": -6.55, "z1": 2.35, "eaves": 5.7}


def plane(axis, offset):
    L = math.sqrt(sum(v * v for v in axis))
    return {"kind": "plane", "axis": [v / L for v in axis], "offset": offset / L}


# ---- the street -------------------------------------------------------------------------------------------
def street(size=180.0):
    slab = X(((-size, size), (-0.6, 0.0), (-size, size)))
    grid = U(K.repeat([0, 0, 3.0], 0, box([size, 0.012, 0.03])), K.repeat([3.0, 0, 0], 0, box([0.03, 0.012, size])))
    grid = K.I(T([0, 0.0, 0], grid), T([HERO_X, 0, 2.0], box([0.0, 0.5, 0.0], name="gridClip")))
    kerb = K.mirror([0, 0, 1], X(((-size, size), (0.0, 0.14), (2.45, 2.6))))           # in road-centre coordinates
    pave = K.mirror([0, 0, 1], X(((-size, size), (0.0, 0.02), (4.6, 4.66))))
    dash = K.I(K.repeat([5.0, 0, 0], 0, box([1.1, 0.012, 0.07])), T([-42.0, 0, 0], box([0.0, 0.5, 0.5], name="dashClip")))
    road = T([0, 0, ROAD_Z], U(S(kerb, FILL), S(pave, FLOOR), S(dash, GLOW)))
    return U(S(slab, FLOOR), S(grid, FLOOR), road)


# ---- the house module (the neighbours) --------------------------------------------------------------------
def house_module(name_roof="rowRoof", name_win="rowWin", front=-1.0):
    """A two-storey house, 7 x 5.6 x 7 m, its front facing `front` z (-1: towards -Z, as both rows face the road
    inside the mirror): a body with four front windows and a door, glowing panes (CANVAS: the chop flashes them), a
    gable roof on a named rotate (it can flip), a chimney (about 30 nodes)."""
    f = front

    def zr(a, b):
        return (min(a * f, b * f), max(a * f, b * f))
    body = X(((-3.5, 3.5), (0.0, 5.6), (-3.5, 3.5)))
    win = T([1.6, 1.7, 3.5 * f], K.repeat([0, 2.6, 0], 1, box([0.55, 0.65, 0.25])))
    door = X(((-0.5, 0.5), (0.0, 2.1), zr(3.2, 3.8)))
    walls = K.D(body, K.mirror([1, 0, 0], win), door)
    panes = T([1.6, 1.7, 3.38 * f], K.repeat([0, 2.6, 0], 1, box([0.55, 0.65, 0.02])))
    panes = K.I(K.mirror([1, 0, 0], panes), X(((-4, 4), (0.5, 5.4), zr(3.0, 3.6))))
    roof = K.I(plane([0, 1, 0.75], 5.6 + 3.6 * 0.75), plane([0, 1, -0.75], 5.6 + 3.6 * 0.75), X(((-3.7, 3.7), (5.6, 9.0), (-3.75, 3.75))))
    roof = R([0, 0, 0], roof, name=name_roof)
    chimney = X(((1.6, 2.3), (6.0, 9.2), zr(-1.6, -0.9)))
    step = X(((-0.8, 0.8), (0.0, 0.15), zr(3.5, 4.1)))
    return U(S(walls, FILL), S(panes, CANVAS), S(roof, ACCENT), S(chimney, FILL), S(step, FILL), name=name_win)


NEAR_C = ROAD_Z + 1.15     # a near-row house's centre, as its distance from the road's centre (front at z = 2.35)


def row():
    """The neighbours on both sides of the street: the module every LOT metres, mirrored across the road (inside the
    mirror z' is the distance from the centre line, so a house at z' = NEAR_C facing -z' faces the road from either
    side), the hero lot itself left empty on its own side; revealed by `rowClip` (a box centred on the hero lot whose
    x size grows a lot at a time)."""
    lots = T([HERO_X, 0, NEAR_C], K.repeat([LOT, 0, 0], LOTS, house_module()))
    sides = T([0, 0, ROAD_Z], K.mirror([0, 0, 1], lots))
    clip = T([HERO_X, 4.0, ROAD_Z], box([0.0, 8.0, 30.0], name="rowClip"))
    hero_lot = X(((HERO_X - 6.4, HERO_X + 6.4), (-1.0, 12.0), (-9.0, 4.0)))
    return K.D(K.I(sides, clip), hero_lot)


def faller():
    """The house landing on the next lot: the module under `fall` (keyed to [LOT * k, height, 0]), mirrored across
    the road and about the hero lot, so four land together."""
    m = T([0, 0, 0], house_module("fallRoof", "fallWin"), name="fall")
    return T([0, 0, ROAD_Z], K.mirror([0, 0, 1], T([HERO_X, 0, NEAR_C], K.mirror([1, 0, 0], m))))


# ---- lamps and trees ----------------------------------------------------------------------------------------
def lamps():
    """Street lamps every LOT metres on both pavements (offset half a lot from the houses), trees between; lamps
    revealed by `lampClip`, trees grown by `treeGrow` (a uniform scale about each tree's foot)."""
    pole = U(S(X(((-0.07, 0.07), (0.0, 4.6), (-0.07, 0.07))), FILL), S(X(((-0.05, 0.05), (4.5, 4.6), (-1.2, 0.0))), FILL),
             S(X(((-0.22, 0.22), (4.3, 4.5), (-1.45, -0.95))), GLOW))
    lamp_row = T([HERO_X + LOT / 2, 0, ROAD_Z], K.mirror([0, 0, 1], T([0, 0, 3.9], K.repeat([LOT, 0, 0], LOTS, pole))))
    lamp_row = K.I(lamp_row, T([HERO_X, 3.0, ROAD_Z], box([0.0, 6.0, 30.0], name="lampClip")))
    tree = {"kind": "scale", "scale": 1.0, "name": "treeGrow", "children": [K.round_tree(4.2)]}
    tree_row = T([HERO_X + LOT / 2 - 3.6, 0, ROAD_Z], K.mirror([0, 0, 1], T([0, 0, 6.4], K.repeat([LOT, 0, 0], LOTS, tree))))
    return U(lamp_row, tree_row)


# ---- the far city -------------------------------------------------------------------------------------------
def skyline():
    """Towers on a grid far behind the houses (z -45..-120), two interleaved sets whose heights rise (`towerA`,
    `towerB`: a box's half size y, its foot kept on the ground by a translate of the same height)."""
    a = T([0, 0, 0], box([3.0, 0.0, 3.0], name="towerA"), name="towerAt")
    b = T([0, 0, 0], box([2.4, 0.0, 2.4], name="towerB"), name="towerBt")
    win_a = K.I(T([0, 0, 0], K.repeat([0, 1.6, 0], 0, box([3.05, 0.05, 3.05]))), T([0, 0, 0], box([3.1, 0.0, 3.1], name="towerAw"), name="towerAwt"))
    set_a = T([HERO_X, 0, -100], K.repeat([28.0, 0, 30.0], 2, U(S(a, FILL), S(win_a, CANVAS2))))
    set_b = T([HERO_X + 14, 0, -115], K.repeat([28.0, 0, 30.0], 2, S(b, FILL)))
    return U(set_a, set_b)


# ---- the hero house ------------------------------------------------------------------------------------------
# the roof: a gable with its ridge along x over z = -2.1 at y = 9.26 (slope 0.8): front y + 0.8 z <= 7.58,
# back y - 0.8 z <= 10.94
HERO_PARTS = [
    # name, builder (in place), the offset it flies in from (metres)
    ("roofL", lambda: K.I(plane([0, 1, 0.8], 7.58), X(((HERO["x0"] - 0.25, HERO["x1"] + 0.25), (HERO["eaves"], 10.0), (-2.1, HERO["z1"] + 0.3)))),
     (-16.0, 9.0, 0.0)),
    ("roofR", lambda: K.I(plane([0, 1, -0.8], 10.94), X(((HERO["x0"] - 0.25, HERO["x1"] + 0.25), (HERO["eaves"], 10.0), (HERO["z0"] - 0.3, -2.1)))),
     (16.0, 9.0, 0.0)),
    ("chimney", lambda: X(((3.3, 3.9), (6.4, 9.8), (-4.4, -3.8))), (0.0, 14.0, 0.0)),
    ("porch", lambda: U(X(((2.6, 4.3), (0.0, 0.16), (2.35, 3.6))), X(((2.55, 4.35), (2.45, 2.55), (2.35, 3.6))),
                        X(((2.62, 2.68), (0.16, 2.45), (3.45, 3.51))), X(((4.22, 4.28), (0.16, 2.45), (3.45, 3.51)))), (0.0, -5.0, 6.0)),
    ("frontDoor", lambda: U(K.D(X(((2.9, 4.0), (0.0, 2.2), (2.35, 2.45))), X(((3.0, 3.9), (0.08, 2.12), (2.3, 2.6)))),
                            S(X(((3.0, 3.9), (0.08, 2.12), (2.37, 2.41))), FILL), S(X(((4.05, 4.2), (1.9, 2.1), (2.36, 2.5))), GLOW)), (0.0, 10.0, 0.0)),
    ("bedWin", lambda: K.D(X(((-1.32, 0.12), (3.68, 5.12), (2.33, 2.45))), X(((-1.22, 0.02), (3.78, 5.02), (2.2, 2.6)))), (-9.0, 0.0, 0.0)),
    ("livWin", lambda: K.D(X(((-1.42, 0.22), (0.68, 2.32), (2.33, 2.47))), X(((-1.3, 0.1), (0.8, 2.2), (2.2, 2.6)))), (9.0, 0.0, 0.0)),
    ("path", lambda: U(*[X(((2.9 + 0.05 * (i % 2), 4.0 - 0.05 * (i % 2)), (0.0, 0.03), (3.8 + 0.75 * i, 4.4 + 0.75 * i))) for i in range(4)]), (0.0, -3.0, 0.0)),
    ("fence", lambda: U(X(((-2.7, 2.6), (0.55, 0.62), (7.0, 7.06))), X(((-2.7, 2.6), (0.25, 0.31), (7.0, 7.06))),
                        T([0.0, 0.4, 7.03], K.I(K.repeat([0.5, 0, 0], 0, box([0.03, 0.4, 0.03])), box([2.6, 1.0, 0.2])))), (-14.0, 0.0, 0.0)),
    ("mailbox", lambda: U(X(((4.6, 4.68), (0.0, 1.05), (7.0, 7.08))), X(((4.45, 4.85), (1.05, 1.3), (6.85, 7.3)))), (0.0, -4.0, 0.0)),
]


def hero_shell():
    """The hero house's walls (the house frame's outline, two storeys) with its openings: the living room's front
    window (where the camera goes in), the front door, the bedroom and bathroom windows; a lit pane in the front
    window (`heroPane`); everything under `heroRise` (an intersection box whose height grows: the walls rise out of
    the ground)."""
    x0, x1, z0, z1, h = HERO["x0"], HERO["x1"], HERO["z0"], HERO["z1"], HERO["eaves"]
    body = X(((x0, x1), (0.0, h), (z0, z1)))
    hollow = X(((x0 + 0.15, x1 - 0.15), (0.0, h - 0.15), (z0 + 0.15, z1 - 0.15)))
    cuts = [X(((-1.3, 0.1), (0.8, 2.2), (z1 - 0.5, z1 + 0.5))),          # the living room's front window
            X(((2.95, 3.95), (0.0, 2.05), (z1 - 0.5, z1 + 0.5))),         # the front door
            X(((-1.2, 0.0), (3.75, 5.05), (z1 - 0.5, z1 + 0.5))),         # the bedroom window
            X(((3.9, 4.5), (4.35, 4.95), (z1 - 0.5, z1 + 0.5))),          # the bathroom window
            X(((x0 - 0.5, x0 + 0.5), (1.0, 2.2), (-4.4, -3.2)))]          # a side window (the kitchen)
    walls = K.D(body, hollow, *cuts)
    band = K.D(X(((x0 - 0.05, x1 + 0.05), (2.78, 2.92), (z0 - 0.05, z1 + 0.05))), X(((x0 + 0.1, x1 - 0.1), (2.0, 3.5), (z0 + 0.1, z1 - 0.1))))
    sill = X(((-1.4, 0.2), (0.74, 0.8), (z1, z1 + 0.12)))
    pane = T([0, 0, 0], X(((-1.3, 0.1), (0.8, 2.2), (2.27, 2.29))), name="heroPane")
    others = U(X(((-1.2, 0.0), (3.75, 5.05), (2.27, 2.29))), X(((3.9, 4.5), (4.35, 4.95), (2.27, 2.29))))
    house = U(S(walls, FILL), S(band, ACCENT), S(sill, FILL), S(pane, GLOW), S(others, CANVAS))
    return K.I(house, T([HERO_X, 0, -2.1], box([6.0, 0.0, 6.0], name="heroRise"), name="heroRiseAt"))


def hero_parts(names):
    parts = []
    for name, builder, _off in HERO_PARTS:
        if name in names:
            parts.append(T([0.0, 0.0, 0.0], builder(), name=f"hp_{name}"))
    return S(U(*parts), FILL)


def hero_tree():
    """The tree in the hero's front garden (it grows on the riser: `heroTree` scale)."""
    return T([-1.8, 0.0, 5.2], {"kind": "scale", "scale": 1.0, "name": "heroTree", "children": [K.pine(4.4)]})


def objects():
    """[(name, tree, role, bmin, bmax, opts)] for the intro's world."""
    far = {"max_distance": 170.0}
    return [
        ("street", street(), "wall", (-180, -0.7, -180), (180, 0.3, 180), dict(far, edge_pixels=1.6, step_scale=0.8)),
        ("row", row(), "furn2", (HERO_X - LOT * LOTS - 5, -0.1, -6.0), (HERO_X + LOT * LOTS + 5, 10.0, 30.0), far),
        ("faller", faller(), "furn2", (HERO_X - LOT * LOTS - 5, -0.1, -6.0), (HERO_X + LOT * LOTS + 5, 60.0, 30.0), far),
        ("lamps", lamps(), "furn", (HERO_X - LOT * LOTS - 8, -0.1, 4.0), (HERO_X + LOT * LOTS + 8, 9.0, 20.0), far),
        ("skyline", skyline(), "furn", (HERO_X - 160, -0.1, -130), (HERO_X + 160, 80.0, -35), dict(far, max_distance=260.0)),
        ("heroShell", hero_shell(), "wall", (HERO["x0"] - 0.3, -0.1, HERO["z0"] - 0.3), (HERO["x1"] + 0.3, 6.2, HERO["z1"] + 0.6), far),
        ("heroParts", hero_parts([p[0] for p in HERO_PARTS]), "furn",
         (HERO["x0"] - 20.0, -6.0, HERO["z0"] - 1.0), (HERO["x1"] + 20.0, 25.0, 14.0), far),
        ("heroTree", hero_tree(), "furn", (-4.5, -0.1, 2.5), (1.0, 6.5, 8.0), far),
    ]
