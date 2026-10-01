"""All You Got, art pass 2: the intro's worlds -- the seed (owner bars 0-8: the cursor that becomes a cube,
divides, grows a frame and a ring) and the house that assembles around the living room during the riser (bars
9-16). Every moving part is a named node (`sdf/<object>/node/<name>/...`) keyed on the beat in the generator.
"""

from __future__ import annotations

import kit as K
from kit import ACCENT, FILL, GLOW, R, S, T, U, X, box


def wire_cube(h, t, name):
    """A wireframe cube: a box with three orthogonal through-cuts, so only its twelve edges remain (5 nodes).
    Named: `<name>` (the box, half size h), `<name>X|Y|Z` (the cuts, half sizes (h - t, h - t, h + 1) etc.)."""
    c = max(h - t, 0.0)
    return K.D(box([h, h, h], name=name), box([c, c, h + 1.0], name=name + "Z"),
               box([c, h + 1.0, c], name=name + "Y"), box([h + 1.0, c, c], name=name + "X"))


def wire_keys(h, t):
    """The four size values a wire cube's named boxes need for half size h and bar thickness t."""
    return {"": [h, h, h], "Z": [h - t, h - t, h + 1.0], "Y": [h - t, h + 1.0, h - t], "X": [h + 1.0, h - t, h - t]}


def seed():
    """The seed, centred on the origin (about 40 nodes):
      core      a solid cube, `core` (half size), turned by `coreRot`
      frame     a wire cube `frame*`, turned by `frameRot`
      inner     a second wire cube `inner*` (nested cubes: the first room)
      cells     a 3x3x3 block of cubes: `cells` (repeat; size = spacing), `cell` (half size)
      rays      a crown of 12 thin rays: `ray` (half extents; x is the length), turned by `rayRot`
      ring      8 small cubes on a ring: `ringAt` (translation x = radius), `ringCube`, turned by `ringRot`
      floors    stacked plates (the lattice organising into storeys): `floors` (repeat y), `floor` (half size)
    """
    core = S(R([0, 0, 0], box([0.0, 0.0, 0.0], name="core"), name="coreRot"), FILL)
    frame = S(R([0, 0, 0], wire_cube(0.0, 0.03, "frame"), name="frameRot"), FILL)
    inner = S(R([0, 0, 0], wire_cube(0.0, 0.02, "inner"), name="innerRot"), ACCENT)
    cells = S(R([0, 0, 0], {"kind": "repeat", "size": [0.5, 0.5, 0.5], "count": 1, "name": "cells",
                            "children": [box([0.0, 0.0, 0.0], name="cell")]}, name="cellsRot"), FILL)
    rays = S(R([90, 0, 0], K.polar(12, T([0.0, 0.0, 0.0], box([0.0, 0.0, 0.0], name="ray"), name="rayAt")),
               name="rayRot"), GLOW)
    ring = S(R([0, 0, 0], K.polar(8, T([0.0, 0.0, 0.0], box([0.0, 0.0, 0.0], name="ringCube"), name="ringAt")),
               name="ringRot"), ACCENT)
    floors = S(T([0, 0, 0], {"kind": "repeat", "size": [0.0, 0.6, 0.0], "count": 3, "name": "floors",
                             "children": [box([0.0, 0.0, 0.0], name="floor")]}, name="floorsAt"), FILL)
    return U(core, frame, inner, cells, rays, ring, floors)


# ---- the house (intro bars 9-16): the living room's walls seen from outside, and the parts that fly in ------

HOUSE_PARTS = [
    # name, builder (in place), the offset it flies in from (metres)
    ("roofL", lambda: T([0, 2.85 + 0.75, -1.15], R([-33, 0, 0], box([2.95, 0.06, 1.42]))), (-9.0, 6.0, 0.0)),
    ("roofR", lambda: T([0, 2.85 + 0.75, 1.15], R([33, 0, 0], box([2.95, 0.06, 1.42]))), (9.0, 6.0, 0.0)),
    ("gableF", lambda: T([0, 2.85, 2.3], R([0, 0, 45], K.D(box([1.7, 1.7, 0.05]), X(((-3, 3), (-3, -0.02), (-1, 1)))
                                                         if False else box([0.0, 0.0, 0.0])))), (0.0, 0.0, 0.0)),
    ("chimney", lambda: X(((1.5, 1.95), (2.9, 4.4), (-1.3, -0.85))), (0.0, 9.0, 0.0)),
    ("porch", lambda: U(X(((-1.25, 0.05), (0.0, 0.12), (2.35, 3.2))), X(((-1.2, 0.0), (2.32, 2.4), (3.05, 3.15)))), (0.0, -4.0, 6.0)),
    ("fenceL", lambda: U(X(((-6.0, -2.5), (0.55, 0.62), (4.5, 4.56))), X(((-6.0, -2.5), (0.25, 0.31), (4.5, 4.56))),
                         T([-4.25, 0.4, 4.53], K.repeat([0.5, 0, 0], 3, box([0.03, 0.4, 0.03])))), (-12.0, 0.0, 0.0)),
    ("fenceR", lambda: U(X(((1.3, 6.0), (0.55, 0.62), (4.5, 4.56))), X(((1.3, 6.0), (0.25, 0.31), (4.5, 4.56))),
                         T([3.65, 0.4, 4.53], K.repeat([0.5, 0, 0], 4, box([0.03, 0.4, 0.03])))), (12.0, 0.0, 0.0)),
    ("path", lambda: U(*[X(((-0.95 + 0.06 * i, -0.35 + 0.06 * i), (0.0, 0.03), (3.4 + 0.7 * i, 3.9 + 0.7 * i))) for i in range(4)]), (0.0, 0.0, 10.0)),
]


def house_parts():
    """The parts that fly in (a roof, a chimney, a porch, fences, a path): each under a named translate
    `<name>` that the generator keys from its offset to zero on its eighth note."""
    parts = []
    for name, builder, _off in HOUSE_PARTS:
        if name == "gableF":
            continue
        parts.append(T([0.0, 0.0, 0.0], builder(), name=name))
    return S(U(*parts), FILL)


def house_tree():
    """A tree in the front garden and its grass (about 12 nodes): it grows on the riser (`hTree` scale)."""
    return T([3.6, 0.0, 3.4], {"kind": "scale", "scale": 1.0, "name": "hTree", "children": [K.round_tree(3.4)]})


def ground_plane(size=40.0, pitch=2.0):
    """Flat ground with a line grid around the house (8 nodes)."""
    slab = X(((-size, size), (-0.5, -0.02), (-size, size)))
    grid = U(K.repeat([0, 0, pitch], 0, box([size, 0.02, 0.025])), K.repeat([pitch, 0, 0], 0, box([0.025, 0.02, size])))
    return S(U(slab, K.T([0, -0.02, 0], K.I(grid, box([size, 0.1, size])))), K.FLOOR)
