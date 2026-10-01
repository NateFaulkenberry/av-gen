"""All You Got, art pass 2: the world kit -- furniture, props, a mannequin, rooms and outdoor pieces, built from
SDF primitives so every object is drawn by the same luminous edge lines (the SDF `look/edge` term) as the walls.

The brief (02-art-pass-2.md) asks for small dense furnished rooms in a primitive simulated-3D universe: windows,
curtains, paintings, shelves, couches, chairs, tables, plants, lamps, strange objects, doors, frames, hanging
objects; simple outdoor spaces; mannequins. Primitive geometry is preferred, so everything here is a box, a
rounded box, a cylinder, a cone, a sphere, a capsule or a torus.

Conventions: metres, +Y up. A prop is built in its own frame with its footprint centred on the origin, its base
on y = 0 and its FRONT facing +Z; `place()` moves and turns it. Surfaces (ADR-1044) are small ints, named below;
an SDF object lists up to 8. Every helper returns a plain SDF node dict.

Node budget: an SDF tree may hold 96 nodes (`count()`), so a furnished room is two or three SDF objects. Each
prop's docstring gives its node count.
"""

from __future__ import annotations

import math
from typing import Optional, Sequence

# ---- surfaces: one convention for every pass 2 object ----------------------------------------------------
FILL, FLOOR, GLOW, SCREEN, CANVAS, CANVAS2, ACCENT, GLASS = range(8)
SURFACE_NAMES = ["fill", "floor", "glow", "screen", "canvas", "canvas2", "accent", "glass"]


# ---- primitives and operators ------------------------------------------------------------------------------

def _f(v):
    return [float(x) for x in v]


def nm(node: dict, name: Optional[str]) -> dict:
    if name:
        node["name"] = name
    return node


def T(t, child, name=None):
    if name is None and all(abs(x) < 1e-12 for x in t):
        return child
    return nm({"kind": "translate", "translation": _f(t), "children": [child]}, name)


def R(rot, child, name=None):
    if name is None and all(abs(x) < 1e-12 for x in rot):
        return child
    return nm({"kind": "rotate", "rotation": _f(rot), "children": [child]}, name)


def box(h, name=None):
    return nm({"kind": "box", "size": _f(h)}, name)


def rbox(h, r, name=None):
    return nm({"kind": "roundedBox", "size": _f(h), "rounding": float(r)}, name)


def cyl(r, h, name=None):
    return nm({"kind": "cylinder", "radius": float(r), "height": float(h)}, name)


def cone(r, h, name=None):
    return nm({"kind": "cone", "radius": float(r), "height": float(h)}, name)


def sph(r, name=None):
    return nm({"kind": "sphere", "radius": float(r)}, name)


def cap(r, h, name=None):
    return nm({"kind": "capsule", "radius": float(r), "height": float(h)}, name)


def tor(R_, r, name=None):
    return nm({"kind": "torus", "radius": float(R_), "rounding": float(r)}, name)


def U(*children, name=None):
    """A union of any number of children, nested in groups of at most 8 (the node's limit)."""
    kids = [c for c in children if c is not None]
    if not kids:
        raise ValueError("empty union")
    while len(kids) > 8:
        kids = [kids[i] if len(kids[i:i + 8]) == 1 else {"kind": "union", "children": kids[i:i + 8]}
                for i in range(0, len(kids), 8)]
    if len(kids) == 1 and not name:
        return kids[0]
    return nm({"kind": "union", "children": kids}, name)


def D(solid, *cuts, name=None):
    cuts = [c for c in cuts if c is not None]
    if not cuts:
        return solid
    if len(cuts) > 7:
        cuts = [U(*cuts)]
    return nm({"kind": "difference", "children": [solid] + cuts}, name)


def I(*children, name=None):
    return nm({"kind": "intersection", "children": list(children)}, name)


def mirror(mask, child, name=None):
    return nm({"kind": "mirror", "size": _f(mask), "children": [child]}, name)


def repeat(size, count, child, name=None):
    c = count if isinstance(count, int) else 0
    return nm({"kind": "repeat", "size": _f(size), "count": int(c), "children": [child]}, name)


def polar(count, child, name=None):
    return nm({"kind": "polarRepeat", "count": int(count), "children": [child]}, name)


def wave(child, amount, frequency, axis, speed=0.0, name=None):
    return nm({"kind": "displaceWave", "amount": float(amount), "frequency": float(frequency), "speed": float(speed),
               "axis": _f(axis), "children": [child]}, name)


def S(node, k):
    """Shade this subtree with surface k (ADR-1044)."""
    node["material"] = int(k)
    return node


# ---- placed shapes ------------------------------------------------------------------------------------------

def B(c, h, k=None, name=None):
    """An axis-aligned box: centre c, half extents h (2 nodes)."""
    n = T(c, box(h, name=name))
    return S(n, k) if k is not None else n


def X(ext, k=None, name=None):
    """A box from extents ((x0, x1), (y0, y1), (z0, z1)) (2 nodes)."""
    (x0, x1), (y0, y1), (z0, z1) = ext
    return B([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2], [abs(x1 - x0) / 2, abs(y1 - y0) / 2, abs(z1 - z0) / 2],
             k, name)


def RB(c, h, r, k=None, name=None):
    n = T(c, rbox(h, r, name=name))
    return S(n, k) if k is not None else n


def CY(c, r, h, k=None, name=None):
    n = T(c, cyl(r, h, name=name))
    return S(n, k) if k is not None else n


def SP(c, r, k=None, name=None):
    n = T(c, sph(r, name=name))
    return S(n, k) if k is not None else n


def euler_y_to(d) -> list:
    """Euler degrees (x, 0, z) that turn +Y onto the direction d (R = Rz Ry Rx, ADR-1001's convention)."""
    L = math.sqrt(sum(v * v for v in d)) or 1.0
    dx, dy, dz = (v / L for v in d)
    rx = math.degrees(math.asin(max(-1.0, min(1.0, dz))))
    rz = math.degrees(math.atan2(-dx, dy))
    return [rx, 0.0, rz]


def SEG(a, b, r, k=None, name=None, box_section=None):
    """A capsule (or a box with half section `box_section` across) from point a to point b (3 nodes)."""
    mid = [(x + y) / 2 for x, y in zip(a, b)]
    d = [y - x for x, y in zip(a, b)]
    L = math.sqrt(sum(v * v for v in d))
    prim = cap(r, L) if box_section is None else box([box_section[0], L / 2, box_section[1]])
    n = T(mid, R(euler_y_to(d), prim), name=name)
    return S(n, k) if k is not None else n


def place(node, at=(0.0, 0.0, 0.0), yaw=0.0, name=None):
    """Move a prop to `at` and turn it `yaw` degrees about +Y (+ turns its front from +Z towards +X)."""
    inner = R([0.0, yaw, 0.0], node) if abs(yaw) > 1e-9 else node
    return T(at, inner, name=name) if (name or any(abs(v) > 1e-12 for v in at)) else inner


def count(tree) -> tuple:
    """(nodes, deepest nesting of point ops, depth): the limits are 96 nodes and depth 16."""
    unary = {"translate", "rotate", "repeat", "screw", "warp", "shell", "scale", "mirror", "fold", "twist",
             "bend", "polarRepeat", "recurse", "displaceWave", "displaceNoise"}

    def walk(n, ops, depth):
        o = ops + (1 if n["kind"] in unary else 0)
        total, worst, deep = 1, o, depth
        for c in n.get("children", []):
            t, w, d = walk(c, o, depth + 1)
            total += t
            worst = max(worst, w)
            deep = max(deep, d)
        return total, worst, deep
    return walk(tree, 0, 1)


# =============================================================================================================
# FURNITURE AND PROPS. Front faces +Z. Sizes are real-world, a little chunky (primitive 3D).
# =============================================================================================================

def couch(w=2.0, d=0.88, h=0.82, seats=3, k=FILL):
    """A sofa: plinth, seat cushions, back cushions, arms, feet (about 16 nodes)."""
    arm = 0.17
    inner = w - 2 * arm
    seat_h = 0.42
    return S(U(
        X(((-w / 2, w / 2), (0.08, 0.28), (-d / 2, d / 2))),                                     # plinth
        T([0.0, (0.28 + seat_h) / 2, 0.06], repeat([inner / seats, 0, 0], seats // 2 if seats % 2 else 0,
                                                   rbox([inner / seats / 2 - 0.012, (seat_h - 0.28) / 2,
                                                         (d - 0.22) / 2], 0.008))),              # seat cushions
        T([0.0, (seat_h + h) / 2 + 0.02, -d / 2 + 0.12], repeat([inner / seats, 0, 0], seats // 2 if seats % 2 else 0,
                                                                 rbox([inner / seats / 2 - 0.012, (h - seat_h) / 2,
                                                                       0.09], 0.008))),            # back cushions
        T([0.0, 0.0, 0.0], mirror([1, 0, 0], RB([w / 2 - arm / 2, 0.36, 0.0], [arm / 2, 0.28, d / 2], 0.006))),
        mirror([1, 0, 1], B([w / 2 - 0.08, 0.04, d / 2 - 0.08], [0.03, 0.04, 0.03])),           # feet
    ), k)


def armchair(k=FILL):
    """One seat of the couch, deeper arms (about 15 nodes)."""
    return couch(w=0.95, d=0.86, h=0.86, seats=1, k=k)


def chair(seat=0.46, w=0.46, d=0.46, back=0.92, k=FILL):
    """A dining chair: seat, a back frame with a slat, four legs (about 13 nodes)."""
    t = 0.035
    return S(U(
        X(((-w / 2, w / 2), (seat - 0.04, seat), (-d / 2, d / 2))),
        mirror([1, 0, 1], B([w / 2 - t / 2, (seat - 0.04) / 2, d / 2 - t / 2], [t / 2, (seat - 0.04) / 2, t / 2])),
        mirror([1, 0, 0], B([w / 2 - t / 2, (seat + back) / 2, -d / 2 + t / 2], [t / 2, (back - seat) / 2, t / 2])),
        X(((-w / 2, w / 2), (back - 0.08, back), (-d / 2, -d / 2 + t))),
        X(((-w / 2, w / 2), (seat + 0.22, seat + 0.28), (-d / 2, -d / 2 + t))),
    ), k)


def table(w=1.2, d=0.8, h=0.75, top=0.04, k=FILL):
    """A table: a top with an apron and four legs (about 9 nodes)."""
    leg = 0.05
    return S(U(
        X(((-w / 2, w / 2), (h - top, h), (-d / 2, d / 2))),
        X(((-w / 2 + 0.05, w / 2 - 0.05), (h - top - 0.08, h - top), (-d / 2 + 0.05, d / 2 - 0.05))),
        mirror([1, 0, 1], B([w / 2 - 0.08, (h - top) / 2, d / 2 - 0.08], [leg / 2, (h - top) / 2, leg / 2])),
    ), k)


def round_table(r=0.45, h=0.72, k=FILL):
    """A round café table on a pedestal (about 7 nodes)."""
    return S(U(CY([0, h - 0.02, 0], r, 0.04), CY([0, h / 2, 0], 0.035, h), CY([0, 0.02, 0], 0.22, 0.04)), k)


def coffee_table(w=1.0, d=0.55, h=0.42, k=FILL):
    """A low table with a shelf (about 11 nodes)."""
    return S(U(table(w, d, h, 0.035, k=k), X(((-w / 2 + 0.06, w / 2 - 0.06), (0.1, 0.13), (-d / 2 + 0.06, d / 2 - 0.06)))), k)


def desk(w=1.3, d=0.65, h=0.75, k=FILL):
    """A desk: top, a drawer pedestal with three drawers, a side panel (about 11 nodes)."""
    ped = 0.42
    return S(U(
        X(((-w / 2, w / 2), (h - 0.035, h), (-d / 2, d / 2))),
        D(X(((w / 2 - ped, w / 2 - 0.02), (0.0, h - 0.035), (-d / 2 + 0.02, d / 2 - 0.02))),
          T([w / 2 - ped / 2 - 0.01, 0.0, d / 2 - 0.02], repeat([0, 0.23, 0], 1, box([ped / 2 - 0.03, 0.008, 0.02])))),
        X(((-w / 2 + 0.02, -w / 2 + 0.05), (0.0, h - 0.035), (-d / 2 + 0.02, d / 2 - 0.02))),
    ), k)


def bed(w=1.5, l=2.05, k=FILL, soft=ACCENT):
    """A bed: frame, mattress, headboard, two pillows, a folded blanket (about 14 nodes)."""
    return U(
        S(X(((-w / 2, w / 2), (0.1, 0.32), (-l / 2, l / 2))), k),
        S(RB([0, 0.42, 0.02], [w / 2 - 0.03, 0.1, l / 2 - 0.04], 0.05), soft),
        S(X(((-w / 2 - 0.03, w / 2 + 0.03), (0.0, 1.05), (-l / 2 - 0.06, -l / 2))), k),
        S(T([0, 0.57, -l / 2 + 0.3], repeat([w / 2, 0, 0], 0, rbox([w / 4 - 0.06, 0.06, 0.17], 0.05))), soft)
        if False else S(T([0, 0.57, -l / 2 + 0.3], mirror([1, 0, 0], RB([w / 4, 0, 0], [w / 4 - 0.06, 0.06, 0.17], 0.05))), soft),
        S(RB([0, 0.53, l / 2 - 0.45], [w / 2 + 0.01, 0.025, 0.42], 0.02), k),
        mirror([1, 0, 1], S(B([w / 2 - 0.05, 0.05, l / 2 - 0.05], [0.04, 0.05, 0.04]), k)),
    )


def nightstand(k=FILL):
    """A bedside table with a drawer line (about 6 nodes)."""
    return S(D(X(((-0.24, 0.24), (0.0, 0.55), (-0.2, 0.2))), X(((-0.2, 0.2), (0.36, 0.375), (0.17, 0.25)))), k)


def wardrobe(w=1.0, h=2.0, d=0.58, k=FILL):
    """A wardrobe: a box with two doors (a split and two handles) (about 9 nodes)."""
    return S(U(
        D(X(((-w / 2, w / 2), (0.05, h), (-d / 2, d / 2))), X(((-0.006, 0.006), (0.12, h - 0.06), (d / 2 - 0.02, d / 2 + 0.1))),
          X(((-w / 2 + 0.02, w / 2 - 0.02), (h - 0.2, h - 0.19), (d / 2 - 0.02, d / 2 + 0.1)))),
        mirror([1, 0, 0], B([0.06, h * 0.55, d / 2 + 0.015], [0.012, 0.09, 0.015])),
    ), k)


def bookshelf(w=1.0, h=1.9, d=0.32, shelves=5, k=FILL, books=ACCENT):
    """A bookcase: a frame, shelves, and rows of books (two sizes) (about 17 nodes)."""
    sp = (h - 0.08) / shelves
    frame = D(X(((-w / 2, w / 2), (0.0, h), (-d / 2, d / 2))), X(((-w / 2 + 0.03, w / 2 - 0.03), (0.04, h - 0.04), (-d / 2 + 0.02, d / 2 + 0.1))))
    boards = T([0, 0.04 + sp, 0], repeat([0, sp, 0], 0, box([w / 2 - 0.03, 0.012, d / 2 - 0.02])))
    boards = I(boards, X(((-w, w), (0.06, h - 0.06), (-d, d))))
    row_a = T([-w / 4, 0.04 + 0.13, 0.0], repeat([0.045, sp * 2, 0], 0, box([0.017, 0.12, d / 2 - 0.05])))
    row_b = T([w / 4, 0.04 + sp + 0.11, 0.0], repeat([0.055, sp * 2, 0], 0, box([0.02, 0.1, d / 2 - 0.04])))
    books_n = I(U(I(row_a, X(((-w / 2 + 0.05, -0.02), (0.04, h - 0.06), (-d, d)))),
                  I(row_b, X(((0.06, w / 2 - 0.05), (0.04, h - 0.06), (-d, d))))), X(((-w, w), (0.0, h - 0.08), (-d, d))))
    return U(S(frame, k), S(boards, k), S(books_n, books))


def tv(w=0.95, h=0.56, k=FILL):
    """An old box television on its own: a deep cabinet, a screen, two knobs, an antenna (about 15 nodes)."""
    return U(
        S(D(RB([0, h / 2 + 0.03, 0], [w / 2, h / 2 + 0.03, 0.26], 0.03), X(((-w / 2 + 0.06, w / 2 - 0.2), (0.09, h - 0.03), (0.2, 0.4)))), k),
        S(X(((-w / 2 + 0.06, w / 2 - 0.2), (0.09, h - 0.03), (0.2, 0.235))), SCREEN),
        S(T([w / 2 - 0.1, 0.0, 0.27], repeat([0, 0.14, 0], 0, cyl(0.03, 0.03)) if False else mirror([0, 0, 0], T([0, h / 2 + 0.05, 0], R([90, 0, 0], cyl(0.028, 0.03))))), k),
        SEG([-0.05, h + 0.06, -0.05], [-0.28, h + 0.42, -0.12], 0.008, k),
        SEG([0.05, h + 0.06, -0.05], [0.24, h + 0.44, -0.1], 0.008, k),
    )


def cabinet(w=1.4, h=0.5, d=0.45, doors=3, k=FILL):
    """A low sideboard with door lines (about 6 nodes)."""
    return S(D(X(((-w / 2, w / 2), (0.06, h), (-d / 2, d / 2))),
               T([0, h / 2 + 0.03, d / 2], repeat([w / doors, 0, 0], 1 if doors == 3 else 0, box([0.006, h / 2 - 0.06, 0.03])))), k)


def floor_lamp(h=1.6, k=FILL):
    """A standard lamp: base, pole, a glowing shade, the bulb (about 11 nodes)."""
    return U(
        S(CY([0, 0.02, 0], 0.17, 0.04), k),
        S(CY([0, h / 2, 0], 0.017, h), k),
        S(D(T([0, h + 0.05, 0], cone(0.26, 0.5)), X(((-1, 1), (h + 0.17, h + 1), (-1, 1)))), GLOW),
        S(SP([0, h - 0.04, 0], 0.06), GLOW),
    )


def table_lamp(h=0.48, k=FILL):
    """A bedside lamp: base, stem, glowing shade (about 9 nodes)."""
    return U(
        S(CY([0, 0.04, 0], 0.08, 0.08), k),
        S(CY([0, h * 0.45, 0], 0.012, h * 0.7), k),
        S(D(T([0, h + 0.02, 0], cone(0.17, 0.34)), X(((-1, 1), (h + 0.1, h + 1), (-1, 1)))), GLOW),
    )


def hanging_lamp(drop=0.9, name=None):
    """A pendant from the ceiling (its origin is the ceiling point): cord, shade, bulb. Named, its root rotate
    is the pivot, so `sdf/<o>/node/<name>/rotation` swings it (about 9 nodes)."""
    body = U(
        S(CY([0, -drop / 2, 0], 0.006, drop), FILL),
        S(D(T([0, -drop - 0.08, 0], R([180, 0, 0], cone(0.22, 0.3))), X(((-1, 1), (-drop - 1, -drop - 0.17), (-1, 1)))), GLOW),
        S(SP([0, -drop - 0.12, 0], 0.05), GLOW),
    )
    return R([0, 0, 0], body, name=name) if name else body


def window_cut(w, h, sill, wall_z0, wall_z1, x=0.0):
    """The opening a window needs, through a wall lying across z in [wall_z0, wall_z1] (2 nodes)."""
    return X(((x - w / 2, x + w / 2), (sill, sill + h), (wall_z0 - 0.3, wall_z1 + 0.3)))


def window_frame(w=1.2, h=1.4, sill=0.9, x=0.0, z=0.0, k=FILL, glass=GLASS):
    """A frame, a cross of mullions, a sill and a dim pane behind, in the plane z (about 13 nodes)."""
    fr = 0.05
    frame = D(X(((x - w / 2, x + w / 2), (sill, sill + h), (z - 0.05, z + 0.05))),
              X(((x - w / 2 + fr, x + w / 2 - fr), (sill + fr, sill + h - fr), (z - 0.2, z + 0.2))))
    mull = U(X(((x - 0.02, x + 0.02), (sill, sill + h), (z - 0.03, z + 0.03))),
             X(((x - w / 2, x + w / 2), (sill + h * 0.6 - 0.02, sill + h * 0.6 + 0.02), (z - 0.03, z + 0.03))))
    ledge = X(((x - w / 2 - 0.08, x + w / 2 + 0.08), (sill - 0.04, sill), (z - 0.06, z + 0.14)))
    pane = X(((x - w / 2 + fr, x + w / 2 - fr), (sill + fr, sill + h - fr), (z - 0.12, z - 0.1)))
    return U(S(frame, k), S(mull, k), S(ledge, k), S(pane, glass))


def curtains(w=1.5, h=1.9, top=2.3, x=0.0, z=0.0, k=ACCENT, name=None):
    """Two gathered curtain panels either side of a window, rippled (a displaceWave), with a rail (about 9
    nodes). Named, the wave's `amount` sways them."""
    panel = wave(box([0.16, h / 2, 0.02]), 0.035, 18.0, [1.0, 0.0, 0.0], name=name)
    return U(S(T([x, top - h / 2, z + 0.12], mirror([1, 0, 0], T([w / 2 + 0.08, 0, 0], panel))), k),
             S(X(((x - w / 2 - 0.35, x + w / 2 + 0.35), (top, top + 0.025), (z + 0.08, z + 0.11))), FILL))


def painting(w=0.8, h=0.6, depth=0.04, k=FILL, canvas=CANVAS, motif="horizon", name=None):
    """A framed picture for a wall (its back on z = 0, facing +Z). The image is raised shapes on the canvas
    (CANVAS2), so its outlines draw as lines: 'horizon' (a sun over a horizon bar and a slope), 'portrait'
    (a head and shoulders), 'grid' (a window of four panes), 'plain' (about 8-13 nodes)."""
    fr = 0.05
    cz = depth - 0.012
    frame = D(X(((-w / 2, w / 2), (-h / 2, h / 2), (0.0, depth))), X(((-w / 2 + fr, w / 2 - fr), (-h / 2 + fr, h / 2 - fr), (cz, depth + 0.1))))
    canvas_n = X(((-w / 2 + fr, w / 2 - fr), (-h / 2 + fr, h / 2 - fr), (0.0, cz)))
    iw, ih = w / 2 - fr, h / 2 - fr
    if motif == "horizon":
        img = U(T([iw * 0.35, ih * 0.35, cz], R([90, 0, 0], cyl(ih * 0.3, 0.016))),
                X(((-iw + 0.01, iw - 0.01), (-ih * 0.25, -ih * 0.25 + 0.025), (cz, cz + 0.012))),
                T([-iw * 0.35, -ih * 0.25, cz], R([0, 0, 30], box([iw * 0.45, 0.012, 0.006]))))
    elif motif == "portrait":
        img = U(T([0, ih * 0.22, cz], R([0, 0, 0], rbox([iw * 0.28, ih * 0.36, 0.008], iw * 0.2))),
                X(((-iw * 0.7, iw * 0.7), (-ih + 0.01, -ih * 0.42), (cz, cz + 0.016))))
    elif motif == "grid":
        img = U(X(((-0.012, 0.012), (-ih, ih), (cz, cz + 0.016))), X(((-iw, iw), (-0.012, 0.012), (cz, cz + 0.016))))
    else:
        img = None
    parts = [S(frame, k), S(canvas_n, canvas)]
    if img is not None:
        parts.append(S(img, CANVAS2))
    body = U(*parts)
    return T([0, 0, 0], body, name=name) if name else body


def rug(w=2.0, d=1.4, k=ACCENT):
    """A rug 2.5 cm thick with a raised border band (about 6 nodes)."""
    return S(U(X(((-w / 2, w / 2), (0.0, 0.022), (-d / 2, d / 2))),
               D(X(((-w / 2 + 0.08, w / 2 - 0.08), (0.0, 0.04), (-d / 2 + 0.08, d / 2 - 0.08))),
                 X(((-w / 2 + 0.13, w / 2 - 0.13), (0.0, 0.06), (-d / 2 + 0.13, d / 2 - 0.13))))), k)


def mirror_frame(w=0.6, h=0.9, k=FILL):
    """A wall mirror (its back on z = 0): a frame and a bright pane (about 6 nodes)."""
    return U(S(D(X(((-w / 2, w / 2), (-h / 2, h / 2), (0.0, 0.035))), X(((-w / 2 + 0.04, w / 2 - 0.04), (-h / 2 + 0.04, h / 2 - 0.04), (0.02, 0.1)))), k),
             S(X(((-w / 2 + 0.04, w / 2 - 0.04), (-h / 2 + 0.04, h / 2 - 0.04), (0.0, 0.02))), GLASS))


def plant(h=0.95, k=FILL, leaves=ACCENT):
    """A potted plant: a tapered pot with a rim, and two whorls of tilted blade leaves (about 15 nodes)."""
    pot = U(T([0, 0.17, 0], R([180, 0, 0], cone(0.2, 0.42))) if False else D(T([0, 0.2, 0], R([180, 0, 0], cone(0.22, 0.6))), X(((-1, 1), (0.34, 2), (-1, 1))), X(((-1, 1), (-2, 0.0), (-1, 1)))),
            CY([0, 0.33, 0], 0.17, 0.03))
    whorl1 = T([0, 0.36, 0], polar(7, T([0.17, 0.28, 0], R([0, 0, -38], rbox([0.035, 0.3, 0.012], 0.01)))))
    whorl2 = T([0, 0.42, 0], R([0, 26, 0], polar(5, T([0.08, h * 0.45, 0], R([0, 0, -14], rbox([0.03, 0.26, 0.01], 0.01))))))
    return U(S(pot, k), S(whorl1, leaves), S(whorl2, leaves))


def wall_clock(r=0.2, name=None, k=FILL):
    """A clock on a wall (its back on z = 0): a face, a rim, an hour hand and a minute hand. Named, the hands'
    rotate nodes are `<name>H` and `<name>M` (rotation about z, degrees) (about 13 nodes)."""
    face = T([0, 0, 0.02], R([90, 0, 0], cyl(r, 0.04)))
    rim = T([0, 0, 0.045], R([90, 0, 0], tor(r - 0.012, 0.014)))
    hh = R([0, 0, 0], T([0, r * 0.25, 0.05], box([0.012, r * 0.27, 0.006])), name=(name + "H") if name else None)
    mm = R([0, 0, 0], T([0, r * 0.38, 0.056], box([0.008, r * 0.4, 0.004])), name=(name + "M") if name else None)
    return U(S(face, k), S(rim, ACCENT), S(hh, GLOW), S(mm, GLOW))


def globe(r=0.17, name=None, k=FILL):
    """A desk globe: a sphere with an equator and two meridians, on a tilted axis and a stand. Named, the
    globe's spin node is `<name>` (rotation about its own axis) (about 15 nodes)."""
    ball = U(S(sph(r), ACCENT), S(R([90, 0, 0], tor(r, 0.008)), FILL),
             S(tor(r, 0.008) if False else R([0, 0, 90], tor(r, 0.008)), FILL), S(R([0, 60, 90], tor(r, 0.008)), FILL))
    spin = R([0, 0, 0], ball, name=name)
    tilt = T([0, r + 0.12, 0], R([0, 0, 23], spin))
    return U(tilt, S(SEG([0, 0.03, 0], [0, 0.12, 0], 0.012), k), S(CY([0, 0.015, 0], 0.11, 0.03), k),
             S(T([0, r + 0.12, 0], R([0, 0, 23], R([90, 0, 0], tor(r + 0.03, 0.007)))), k))


def fridge(w=0.7, h=1.75, d=0.65, k=FILL):
    """A fridge: body, door split, two handles (about 10 nodes)."""
    return S(U(D(RB([0, h / 2, 0], [w / 2, h / 2, d / 2], 0.04), X(((-w / 2, w / 2), (h * 0.62, h * 0.62 + 0.012), (d / 2 - 0.02, d / 2 + 0.1)))),
               B([-w / 2 + 0.07, h * 0.8, d / 2 + 0.02], [0.012, 0.12, 0.02]),
               B([-w / 2 + 0.07, h * 0.4, d / 2 + 0.02], [0.012, 0.18, 0.02])), k)


def counter(w=1.8, h=0.9, d=0.6, k=FILL):
    """A kitchen counter: cupboards with door lines, a worktop, a sink cut (about 9 nodes)."""
    return S(U(D(X(((-w / 2, w / 2), (0.05, h - 0.04), (-d / 2, d / 2))),
                 T([0, h / 2, d / 2], repeat([w / 4, 0, 0], 0, box([0.006, h / 2 - 0.08, 0.03]))) if False else
                 I(T([w / 8, h / 2, d / 2], repeat([w / 4, 0, 0], 0, box([0.006, h / 2 - 0.08, 0.03]))), X(((-w / 2 + 0.05, w / 2 - 0.05), (0, h), (0, d))))),
               D(X(((-w / 2 - 0.02, w / 2 + 0.02), (h - 0.04, h), (-d / 2, d / 2 + 0.03))),
                 X(((w * 0.1, w * 0.1 + 0.5), (h - 0.03, h + 0.1), (-d / 2 + 0.1, d / 2 - 0.08))))), k)


def kettle(k=ACCENT):
    """A kettle: body, lid knob, spout, handle (about 11 nodes)."""
    return S(U(CY([0, 0.11, 0], 0.1, 0.2), SP([0, 0.23, 0], 0.02), SEG([0.08, 0.1, 0], [0.18, 0.2, 0], 0.018),
               T([-0.1, 0.16, 0], R([90, 0, 0], tor(0.07, 0.012)))), k)


def plate_and_cup(k=ACCENT):
    """A plate, a cup with a handle, a fork (about 12 nodes): table-top scale, origin on the table."""
    return S(U(CY([0, 0.008, 0], 0.13, 0.016), CY([0.22, 0.05, -0.08], 0.045, 0.1),
               T([0.27, 0.05, -0.08], R([90, 0, 0], tor(0.03, 0.007))), X(((-0.19, -0.17), (0.0, 0.01), (-0.1, 0.1)))), k)


def boxes(k=FILL):
    """Stacked moving boxes, slightly askew (about 9 nodes)."""
    return S(U(X(((-0.35, 0.25), (0.0, 0.42), (-0.25, 0.25))), T([0.02, 0.62, 0.0], R([0, 14, 0], box([0.25, 0.2, 0.2]))),
               T([0.5, 0.18, 0.1], R([0, -20, 0], box([0.2, 0.18, 0.2])))), k)


def fireplace(w=1.4, h=1.15, k=FILL, name=None):
    """A fireplace: surround with an opening, a mantel, three flame cones (GLOW). Named, the flames' group is
    `<name>` (key its translation y or scale to flicker) (about 14 nodes)."""
    surround = D(X(((-w / 2, w / 2), (0.0, h), (-0.25, 0.15))), X(((-w / 2 + 0.25, w / 2 - 0.25), (0.08, h - 0.35), (-0.15, 0.4))))
    mantel = X(((-w / 2 - 0.1, w / 2 + 0.1), (h, h + 0.06), (-0.27, 0.22)))
    flames = T([0, 0.08, -0.02], U(T([0, 0.2, 0], cone(0.11, 0.4)), T([-0.16, 0.14, 0.02], cone(0.08, 0.28)),
                                   T([0.15, 0.15, -0.01], cone(0.08, 0.3))), name=name)
    return U(S(surround, k), S(mantel, k), S(flames, GLOW))


def phone_table(k=FILL):
    """A small hall table with an old telephone on it (about 13 nodes)."""
    return U(S(table(0.5, 0.35, 0.78, 0.03), k),
             S(U(RB([0, 0.82, 0], [0.11, 0.035, 0.09], 0.02), SEG([-0.1, 0.885, 0], [0.1, 0.885, 0], 0.025)), ACCENT))


def ceiling_fan(name=None, k=FILL):
    """A ceiling fan (origin at the ceiling): a rod, a hub, four blades. Named, the blades' spin node is
    `<name>` (rotation about y) (about 9 nodes)."""
    blades = R([0, 0, 0], polar(4, T([0.42, 0, 0], R([8, 0, 0], box([0.36, 0.008, 0.07])))), name=name)
    return S(U(CY([0, -0.15, 0], 0.012, 0.3), CY([0, -0.32, 0], 0.07, 0.08), T([0, -0.34, 0], blades)), k)


def stack_of_books(k=ACCENT):
    """Three books lying on a surface (about 7 nodes)."""
    return S(U(X(((-0.12, 0.12), (0.0, 0.035), (-0.09, 0.09))), T([0.01, 0.055, 0], R([0, 12, 0], box([0.11, 0.02, 0.08]))),
               T([-0.01, 0.09, 0], R([0, -8, 0], box([0.1, 0.015, 0.075])))), k)


def bathtub(k=FILL):
    """A clawfoot-ish tub: a rounded box hollowed out, on four feet (about 9 nodes)."""
    return S(U(D(RB([0, 0.38, 0], [0.38, 0.28, 0.82], 0.12), RB([0, 0.55, 0], [0.31, 0.3, 0.74], 0.1)),
               mirror([1, 0, 1], SP([0.28, 0.06, 0.62], 0.06))), k)


def coat_hooks(n=4, k=FILL):
    """A rail of coat hooks on a wall (back on z = 0) (about 6 nodes)."""
    return S(U(X(((-0.5, 0.5), (-0.04, 0.04), (0.0, 0.025))),
               T([-(n - 1) * 0.12, 0, 0.04], repeat([0.24, 0, 0], 0, cap(0.014, 0.06)) if False else
                 I(T([0.12, 0, 0.05], repeat([0.24, 0, 0], 0, R([60, 0, 0], cap(0.014, 0.08)))), X(((-0.45, 0.45), (-0.2, 0.2), (-0.2, 0.3)))))), k)


# =============================================================================================================
# THE MANNEQUIN: an artist's wooden figure from capsules, spheres and rounded boxes, posed by forward kinematics
# in Python and emitted as flat segments (so its body is drawn by the same lines as the room). About 62 nodes:
# one SDF object of its own. Its head is a named rotate (`<name>Head`): key the rotation to turn it.
# =============================================================================================================

def _rot_y(v, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return [v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c]


def _add(a, b):
    return [x + y for x, y in zip(a, b)]


def _sc(a, k):
    return [x * k for x in a]


def _limb(start, direction_deg, length):
    """A point `length` from `start` along a direction given as (pitch from straight down, toward +Z; roll toward
    +X) in degrees."""
    fwd, side = (math.radians(d) for d in direction_deg)
    d = [math.sin(side) * math.cos(fwd), -math.cos(side) * math.cos(fwd), math.sin(fwd)]
    return _add(start, _sc(d, length))


POSES = {
    # thighs/shins/upper arms/forearms as (forward, sideways) degrees from hanging straight down
    "stand": {"hip_y": 0.94, "thigh": (0, 3), "shin": (0, 0), "uarm": (4, 8), "farm": (12, 4), "lean": 0.0},
    "sit": {"hip_y": 0.47, "thigh": (88, 4), "shin": (2, 0), "uarm": (18, 6), "farm": (70, 2), "lean": -6.0},
    "wait": {"hip_y": 0.94, "thigh": (0, 4), "shin": (0, 0), "uarm": (-6, 14), "farm": (-10, 6), "lean": 2.0},
}


def mannequin(pose="stand", scale=1.0, name="man", k=FILL, joint=ACCENT, head_yaw=0.0):
    """A wooden artist's mannequin facing +Z, standing on y = 0 (or sitting with its hips at `hip_y`)."""
    P = POSES[pose]
    s = scale
    hip = [0.0, P["hip_y"] * s, 0.0]
    lean = math.radians(P["lean"])
    up = [0.0, math.cos(lean), math.sin(lean)]
    chest = _add(hip, _sc(up, 0.42 * s))
    neck = _add(hip, _sc(up, 0.6 * s))
    head = _add(hip, _sc(up, 0.76 * s))
    parts = []
    # torso: pelvis and chest blocks, a waist ball, a neck
    parts.append(S(T(_add(hip, _sc(up, 0.04 * s)), R([math.degrees(-lean), 0, 0], rbox([0.16 * s, 0.09 * s, 0.1 * s], 0.012 * s))), k))
    parts.append(S(T(chest, R([math.degrees(-lean), 0, 0], rbox([0.19 * s, 0.17 * s, 0.11 * s], 0.012 * s))), k))
    parts.append(S(T(_add(hip, _sc(up, 0.19 * s)), R([math.degrees(-lean), 45, 0], box([0.075 * s, 0.07 * s, 0.075 * s]))), joint))
    parts.append(S(SEG(_add(hip, _sc(up, 0.55 * s)), neck, 0.04 * s, box_section=(0.035 * s, 0.035 * s)), k))
    # the head: a faceted egg (a box turned 45 degrees, chamfered), on a named turn
    head_n = R([0, head_yaw, 0], R([0, 45, 0], rbox([0.075 * s, 0.11 * s, 0.075 * s], 0.025 * s)), name=name + "Head")
    parts.append(S(T(head, R([math.degrees(-lean), 0, 0], head_n)), k))
    for side in (-1, 1):
        sh = _add(chest, [side * 0.22 * s, 0.11 * s, 0.0])
        hp = _add(hip, [side * 0.1 * s, -0.04 * s, 0.0])
        ua = P["uarm"]
        el = _limb(sh, (ua[0], side * ua[1]), 0.29 * s)
        fa = P["farm"]
        wr = _limb(el, (fa[0], side * fa[1]), 0.26 * s)
        th = P["thigh"]
        kn = _limb(hp, (th[0], side * th[1]), 0.44 * s)
        shn = P["shin"]
        an = _limb(kn, (shn[0], side * shn[1]), 0.43 * s)
        parts += [S(SP(sh, 0.055 * s), joint), S(SEG(sh, el, 0.042 * s, box_section=(0.04 * s, 0.04 * s)), k),
                  S(SP(el, 0.04 * s), joint),
                  S(SEG(el, wr, 0.036 * s, box_section=(0.034 * s, 0.034 * s)), k),
                  S(RB(_add(wr, [0, -0.06 * s, 0.0]), [0.025 * s, 0.07 * s, 0.045 * s], 0.006 * s), k),
                  S(SEG(hp, kn, 0.06 * s, box_section=(0.058 * s, 0.058 * s)), k), S(SP(kn, 0.052 * s), joint),
                  S(SEG(kn, an, 0.048 * s, box_section=(0.046 * s, 0.046 * s)), k),
                  S(RB(_add(an, [0, -0.02 * s, 0.07 * s]), [0.045 * s, 0.03 * s, 0.11 * s], 0.006 * s), k)]
    return U(*parts)


# =============================================================================================================
# ROOM SHELLS. A room is given by its INTERIOR extents; walls, floor and ceiling are one hollow box (3 nodes)
# minus its openings, plus floorboards (raised hairline strips, so they draw as lines), a skirting and a
# cornice line (3 + 3 nodes). The floor's top is y0.
# =============================================================================================================

def shell(ext, wall=0.15, k=FILL):
    (x0, x1), (y0, y1), (z0, z1) = ext
    c = [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]
    h = [(x1 - x0) / 2 + wall / 2, (y1 - y0) / 2 + wall / 2, (z1 - z0) / 2 + wall / 2]
    return S(T(c, {"kind": "shell", "offset": float(wall), "children": [box(h)]}), k)


def floorboards(ext, pitch=0.18, along="x", k=FLOOR):
    """Hairline strips 2 mm proud of the floor every `pitch` metres, running along x or z (3 nodes): the floor
    reads as boards, drawn in lines."""
    (x0, x1), (y0, _), (z0, z1) = ext
    if along == "x":
        strip = box([(x1 - x0) / 2, 0.006, 0.006])
        rep = repeat([0, 0, pitch], 0, strip)
        n = T([(x0 + x1) / 2, y0 + 0.001, (z0 + z1) / 2], I(rep, box([(x1 - x0) / 2, 0.01, (z1 - z0) / 2 - 0.02])))
    else:
        strip = box([0.006, 0.006, (z1 - z0) / 2])
        rep = repeat([pitch, 0, 0], 0, strip)
        n = T([(x0 + x1) / 2, y0 + 0.001, (z0 + z1) / 2], I(rep, box([(x1 - x0) / 2 - 0.02, 0.01, (z1 - z0) / 2])))
    return S(n, k)


def tiles(ext, pitch=0.5, k=FLOOR):
    """A grid of hairline strips in both directions (a tiled floor) (5 nodes)."""
    (x0, x1), (y0, _), (z0, z1) = ext
    grid = U(repeat([0, 0, pitch], 0, box([(x1 - x0) / 2, 0.006, 0.006])), repeat([pitch, 0, 0], 0, box([0.006, 0.006, (z1 - z0) / 2])))
    return S(T([(x0 + x1) / 2, y0 + 0.001, (z0 + z1) / 2], I(grid, box([(x1 - x0) / 2 - 0.01, 0.01, (z1 - z0) / 2 - 0.01]))), k)


def wall_band(ext, y_lo, y_hi, proud=0.012, k=FILL):
    """A band round the room's walls between two heights, `proud` of the wall (a skirting, a dado rail, a
    picture rail): a tall prism's shell clipped to the band, so it never closes over the floor (5 nodes)."""
    (x0, x1), _, (z0, z1) = ext
    prism = T([(x0 + x1) / 2, 0.0, (z0 + z1) / 2],
              {"kind": "shell", "offset": proud * 2, "children": [box([(x1 - x0) / 2, 50.0, (z1 - z0) / 2])]})
    return S(I(prism, X(((x0 - 1, x1 + 1), (y_lo, y_hi), (z0 - 1, z1 + 1)))), k)


def skirting(ext, height=0.1, inset=0.012, k=FILL):
    """A skirting board round the room's walls (5 nodes): its top edge is a line at `height`."""
    (_, _), (y0, _), _ = ext
    return wall_band(ext, y0, y0 + height, inset, k)


def door_cut(wall, ext, along, width=0.9, height=2.05):
    """An opening through one wall of a room ('+x', '-x', '+z', '-z') at `along` (2 nodes)."""
    (x0, x1), (y0, _), (z0, z1) = ext
    t = 0.6
    y = (y0 - 0.01, y0 + height)
    if wall == "+x":
        return X(((x1 - t, x1 + t), y, (along - width / 2, along + width / 2)))
    if wall == "-x":
        return X(((x0 - t, x0 + t), y, (along - width / 2, along + width / 2)))
    if wall == "+z":
        return X(((along - width / 2, along + width / 2), y, (z1 - t, z1 + t)))
    return X(((along - width / 2, along + width / 2), y, (z0 - t, z0 + t)))


def door_frame(wall, ext, along, width=0.9, height=2.05, k=FILL):
    """An architrave round a door opening on the room side (3 nodes via a hollowed slab)."""
    (x0, x1), (y0, _), (z0, z1) = ext
    a = 0.07
    if wall in ("+z", "-z"):
        z = z1 if wall == "+z" else z0
        s = -1 if wall == "+z" else 1
        outer = X(((along - width / 2 - a, along + width / 2 + a), (y0, y0 + height + a), (z + s * 0.0 - 0.02, z + s * 0.025 + 0.02)))
        inner = X(((along - width / 2, along + width / 2), (y0 - 0.1, y0 + height), (z - 0.2, z + 0.2)))
    else:
        x = x1 if wall == "+x" else x0
        s = -1 if wall == "+x" else 1
        outer = X(((x + s * 0.0 - 0.02, x + s * 0.025 + 0.02), (y0, y0 + height + a), (along - width / 2 - a, along + width / 2 + a)))
        inner = X(((x - 0.2, x + 0.2), (y0 - 0.1, y0 + height), (along - width / 2, along + width / 2)))
    return S(D(outer, inner), k)


# =============================================================================================================
# OUTDOORS: primitive 3D nature (cones, spheres, boxes), never the GV3 assets' look.
# =============================================================================================================

def pine(h=3.0, k=FILL, leaves=ACCENT):
    """A conifer: a trunk and three stacked cones (about 8 nodes)."""
    return U(S(CY([0, h * 0.12, 0], h * 0.035, h * 0.24), k),
             S(T([0, h * 0.42, 0], cone(h * 0.24, h * 0.42)), leaves), S(T([0, h * 0.62, 0], cone(h * 0.19, h * 0.36)), leaves),
             S(T([0, h * 0.82, 0], cone(h * 0.13, h * 0.3)), leaves))


def round_tree(h=3.2, k=FILL, leaves=ACCENT):
    """A broadleaf: a trunk and a faceted crown (a rounded box turned 45 degrees, so it has edges to draw)
    (about 7 nodes)."""
    return U(S(CY([0, h * 0.25, 0], h * 0.045, h * 0.5), k),
             S(T([0, h * 0.68, 0], R([35, 45, 0], rbox([h * 0.22, h * 0.22, h * 0.22], h * 0.012))), leaves))


def rock(s=0.6, k=FILL, seed=0):
    """A boulder: two turned boxes (about 6 nodes)."""
    a = 17 + 23 * seed
    return S(U(T([0, s * 0.35, 0], R([a, a * 1.7, a * 0.6], box([s * 0.6, s * 0.4, s * 0.45]))),
               T([s * 0.35, s * 0.25, s * 0.1], R([-a * 0.8, a, a * 1.3], box([s * 0.35, s * 0.3, s * 0.3])))), k)


def grass_tuft(k=ACCENT):
    """Five blades (a polar repeat of a tilted thin box) (about 4 nodes)."""
    return S(polar(5, T([0.05, 0.12, 0], R([0, 0, -18], box([0.008, 0.13, 0.004])))), k)


def pyramid(base=20.0, height=12.0, k=FILL):
    """A mountain: four planes intersected (5 nodes); its ridges are lines."""
    hb = base / 2
    n = math.hypot(height, hb)
    planes = [{"kind": "plane", "axis": [height / n * sx, hb / n, height / n * sz], "offset": height * hb / n}
              for sx, sz in ((1, 0), (-1, 0), (0, 1), (0, -1))]
    planes = [{"kind": "plane", "axis": [ax[0], ax[1], ax[2]], "offset": off}
              for ax, off in (([p["axis"][0], p["axis"][1], p["axis"][2]], p["offset"]) for p in planes)]
    return S(I(*planes, {"kind": "plane", "axis": [0, -1, 0], "offset": 0.0}), k)
