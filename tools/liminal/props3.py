"""All You Got, art pass 3: the props the tableaux and the new rooms need (the kit's conventions: metres, base on
y = 0, footprint centred, front facing +Z; sharp edges so the line look draws them). Every prop can carry an
`entity` annotation for the spatial validator (ADR-1051): `entity(node, id, category, room, **fields)`.

  toilet, vanity (a sink cabinet), crt_monitor and keyboard (for a desk), pillow, washing_machine, dryer,
  water_heater, shelving (a steel rack of boxes), workbench, laundry_basket, office_chair, front_door.
"""

from __future__ import annotations

import kit as K
from kit import ACCENT, CANVAS, FILL, GLASS, GLOW, R, S, SCREEN, T, U, X, box


def entity(node, eid, category, room=None, **fields):
    """Annotate an SDF node as one entity for the spatial validator: its subtree is the entity's geometry, in
    the frame the node sits in (front +Z, up +Y). Extra fields: anchor, pose, hip, normal, interior, t0, t1."""
    e = {"id": eid, "category": category}
    if room:
        e["room"] = room
    for k, v in fields.items():
        if v is not None:
            e[k] = v
    node["entity"] = e
    return node


def toilet(k=FILL, seat=ACCENT):
    """A toilet: a pedestal, a bowl, a seat ring at 0.42 m, a raised lid, a cistern with a flush button
    (about 16 nodes). The seat's centre is at z = 0.06."""
    return U(
        S(K.D(X(((-0.16, 0.16), (0.0, 0.3), (-0.2, 0.2))), X(((-0.11, 0.11), (0.05, 0.4), (0.0, 0.3)))), k),   # pedestal
        S(K.D(K.CY([0, 0.34, 0.06], 0.2, 0.1), K.CY([0, 0.4, 0.06], 0.14, 0.1)), k),                       # bowl rim
        S(K.D(K.CY([0, 0.405, 0.06], 0.205, 0.03), K.CY([0, 0.405, 0.06], 0.125, 0.06)), seat),            # the seat
        S(T([0, 0.62, -0.17], R([-12, 0, 0], box([0.19, 0.2, 0.015]))), seat),                               # the raised lid
        S(X(((-0.21, 0.21), (0.42, 0.8), (-0.3, -0.12))), k),                                                # cistern
        S(X(((-0.23, 0.23), (0.8, 0.83), (-0.31, -0.11))), k),
        S(K.CY([0, 0.835, -0.21], 0.025, 0.012), ACCENT),
    )


def vanity(w=0.7, k=FILL):
    """A sink cabinet: doors, a worktop with a basin cut, a tap (about 12 nodes). Worktop at 0.86 m; the rim's
    front edge is z = +0.23."""
    return U(
        S(K.D(X(((-w / 2, w / 2), (0.08, 0.82), (-0.22, 0.22))), X(((-0.004, 0.004), (0.14, 0.78), (0.2, 0.3)))), k),
        S(K.D(X(((-w / 2 - 0.02, w / 2 + 0.02), (0.82, 0.86), (-0.24, 0.24))), K.CY([0, 0.86, 0.02], 0.17, 0.08)), ACCENT),
        S(K.CY([0, 0.8, 0.02], 0.17, 0.02), GLASS),
        S(K.SEG([0, 0.86, -0.19], [0, 1.0, -0.19], 0.014), ACCENT),
        S(K.SEG([0, 1.0, -0.19], [0, 0.97, -0.08], 0.012), ACCENT),
    )


def crt_monitor(k=FILL):
    """An old CRT monitor on a foot (its base on y = 0, the desk's top): a deep body, a bezel and a screen
    (SCREEN, where the static goes) facing +Z (about 12 nodes). The screen's centre is (0, 0.24, 0.17)."""
    return U(
        S(K.D(X(((-0.21, 0.21), (0.06, 0.42), (-0.1, 0.17))), X(((-0.17, 0.17), (0.09, 0.39), (0.155, 0.3)))), k),
        S(T([0, 0.24, -0.15], R([90, 0, 0], K.cone(0.16, 0.18))), k),                  # the tube's back
        S(X(((-0.17, 0.17), (0.09, 0.39), (0.15, 0.156))), SCREEN),
        S(X(((-0.08, 0.08), (0.0, 0.06), (-0.1, 0.08))), k),                              # the foot
        S(K.CY([0.17, 0.075, 0.172], 0.008, 0.006), GLOW),                                 # the power light
    )


def keyboard(k=FILL):
    """A keyboard lying on a desk (about 6 nodes)."""
    return S(U(X(((-0.22, 0.22), (0.0, 0.025), (-0.08, 0.08))),
               T([0, 0.03, 0.0], K.I(K.repeat([0.04, 0, 0.035], 0, box([0.016, 0.006, 0.014])),
                                     box([0.2, 0.02, 0.06])))), k)


def pillow(w=0.5, d=0.34, h=0.12, k=ACCENT):
    """A pillow: a box with chamfered edges (about 4 nodes)."""
    return S(K.I(X(((-w / 2, w / 2), (0.0, h), (-d / 2, d / 2))),
                 T([0, h / 2, 0], R([45, 0, 0], box([w, (h + d) * 0.36, (h + d) * 0.36])))), k)


def washing_machine(k=FILL, glass=GLASS):
    """A front loader: a cube, a porthole door ring and its glass, a control strip (about 10 nodes)."""
    return U(
        S(K.D(X(((-0.3, 0.3), (0.0, 0.85), (-0.3, 0.3))), X(((-0.28, 0.28), (0.7, 0.71), (0.29, 0.35)))), k),
        S(T([0, 0.4, 0.3], R([90, 0, 0], K.D(K.cyl(0.21, 0.04), K.cyl(0.15, 0.1)))), ACCENT),
        S(T([0, 0.4, 0.29], R([90, 0, 0], K.cyl(0.15, 0.02))), glass),
        S(T([0.2, 0.78, 0.3], R([90, 0, 0], K.cyl(0.03, 0.02))), ACCENT),
    )


def dryer(k=FILL):
    return U(washing_machine(k), S(X(((-0.25, 0.25), (0.86, 0.87), (-0.25, 0.25))), k))


def water_heater(k=FILL):
    """A tall tank with bands and two pipes up into the ceiling (about 10 nodes)."""
    return U(S(K.CY([0, 0.8, 0], 0.28, 1.6), k),
             S(K.I(T([0, 0.4, 0], K.repeat([0, 0.5, 0], 1, K.cyl(0.29, 0.03))), K.CY([0, 0.8, 0], 0.3, 1.5)), ACCENT),
             S(K.SEG([-0.1, 1.6, 0], [-0.1, 2.6, 0], 0.025), ACCENT), S(K.SEG([0.1, 1.6, 0], [0.1, 2.6, 0], 0.025), ACCENT))


def shelving(w=1.4, h=1.9, d=0.45, k=FILL, boxes_k=ACCENT):
    """A steel rack: four posts, four shelves, and boxes on them (about 22 nodes)."""
    posts = K.mirror([1, 0, 1], X(((w / 2 - 0.03, w / 2), (0.0, h), (d / 2 - 0.03, d / 2))))
    shelves = K.I(T([0, 0.1, 0], K.repeat([0, (h - 0.15) / 3, 0], 0, box([w / 2, 0.012, d / 2]))),
                  X(((-w, w), (0.05, h - 0.02), (-d, d))))
    bx = U(X(((-0.6, -0.15), (0.11, 0.4), (-0.18, 0.18))), T([0.25, 0.25, 0.0], R([0, 8, 0], box([0.25, 0.14, 0.17]))),
           X(((-0.55, -0.05), (0.69, 0.95), (-0.17, 0.17))), T([0.3, 0.84, 0.0], R([0, -6, 0], box([0.22, 0.15, 0.18]))),
           X(((-0.62, -0.22), (1.27, 1.6), (-0.18, 0.18))), X(((0.05, 0.6), (1.27, 1.45), (-0.16, 0.16))))
    return U(S(posts, k), S(shelves, k), S(bx, boxes_k))


def workbench(k=FILL):
    """A workbench with a pegboard and a vice (about 14 nodes)."""
    return U(S(K.table(1.5, 0.6, 0.9, 0.05), k),
             S(X(((-0.7, 0.7), (0.3, 0.33), (-0.25, 0.25))), k),
             S(K.D(X(((-0.75, 0.75), (1.05, 1.85), (-0.3, -0.28))),
                   T([0, 1.45, -0.29], K.I(K.repeat([0.1, 0.1, 0], 0, box([0.012, 0.012, 0.03])), box([0.7, 0.38, 0.1])))), ACCENT),
             S(U(X(((0.45, 0.62), (0.9, 1.0), (0.18, 0.3))), X(((0.5, 0.57), (1.0, 1.06), (0.18, 0.3)))), ACCENT))


def laundry_basket(k=ACCENT):
    return S(K.D(X(((-0.25, 0.25), (0.0, 0.42), (-0.18, 0.18))), X(((-0.22, 0.22), (0.03, 0.6), (-0.15, 0.15))),
                 T([0, 0.25, 0.18], K.repeat([0.07, 0.07, 0], 0, box([0.015, 0.015, 0.05])))), k)


def office_chair(k=FILL):
    """A desk chair: a five-star base, a column, a seat at 0.47 m and a back (about 12 nodes)."""
    return S(U(K.polar(5, K.SEG([0.0, 0.05, 0.0], [0.3, 0.04, 0.0], 0.018, box_section=(0.022, 0.012))),
               K.CY([0, 0.24, 0], 0.025, 0.36),
               X(((-0.23, 0.23), (0.42, 0.47), (-0.22, 0.24))),
               X(((-0.21, 0.21), (0.6, 1.0), (-0.26, -0.22))),
               K.SEG([0, 0.47, -0.2], [0, 0.62, -0.24], 0.02, box_section=(0.02, 0.012))), k)


def front_door(w=0.95, h=2.1, k=FILL, glass=GLASS, name=None):
    """A panelled front door in its frame (its back on z = 0, facing +Z), hinged on its left edge: a named
    rotate at the hinge swings it open (about 10 nodes)."""
    leaf = U(K.D(X(((0.0, w), (0.0, h), (-0.025, 0.025))), X(((0.12, w - 0.12), (1.3, h - 0.15), (0.0, 0.1))),
                 X(((0.12, w - 0.12), (0.2, 1.15), (0.0, 0.1)))),
             S(X(((0.14, w - 0.14), (1.32, h - 0.17), (-0.01, 0.01))), glass),
             S(K.SP([w - 0.1, 1.0, 0.04], 0.025), ACCENT))
    hinge = R([0, 0, 0], T([0, 0, 0], leaf), name=name)
    return S(T([-w / 2, 0.0, 0.0], hinge), k)


# ---- the tableau furniture: sharp-edged, with the dimensions the poses are written against ------------------
ARMCHAIR = {"seat": 0.42, "seat_front": 0.4, "back_front": -0.27, "inner": 0.28, "arm_top": 0.62}
SOFA = {"seat": 0.42, "seat_front": 0.42, "back_front": -0.26, "inner": 0.83, "arm_top": 0.62}
BED = {"mattress": 0.52, "blanket": 0.55, "pillow_top": 0.64, "pillow_z": -0.72}


def armchair3(k=FILL, cushion=ACCENT):
    """An armchair: a plinth, a seat cushion (top 0.42), a back, two arms (top 0.62), four feet (about 14 nodes)."""
    return U(S(X(((-0.45, 0.45), (0.06, 0.26), (-0.425, 0.425))), k),
             S(X(((-0.28, 0.28), (0.26, 0.42), (-0.27, 0.4))), cushion),
             S(X(((-0.45, 0.45), (0.26, 0.88), (-0.425, -0.27))), k),
             S(K.mirror([1, 0, 0], X(((0.28, 0.45), (0.26, 0.62), (-0.425, 0.425)))), k),
             S(K.mirror([1, 0, 1], X(((0.36, 0.42), (0.0, 0.06), (0.33, 0.39)))), k))


def sofa3(k=FILL, cushion=ACCENT):
    """A three-seat sofa: seat top 0.42 between the arms (x within 0.83), back cushions, arms (top 0.62)
    (about 16 nodes)."""
    return U(S(X(((-1.0, 1.0), (0.06, 0.26), (-0.44, 0.44))), k),
             S(T([0, 0.34, 0.08], K.repeat([0.555, 0, 0], 1, box([0.272, 0.08, 0.34]))), cushion),
             S(T([0, 0.61, -0.35], K.repeat([0.555, 0, 0], 1, box([0.272, 0.19, 0.09]))), cushion),
             S(X(((-1.0, 1.0), (0.26, 0.42), (-0.44, -0.26))), k),
             S(K.mirror([1, 0, 0], X(((0.83, 1.0), (0.06, 0.62), (-0.44, 0.44)))), k),
             S(K.mirror([1, 0, 1], X(((0.9, 0.96), (0.0, 0.06), (0.36, 0.42)))), k))


def bed3(w=1.5, l=2.05, k=FILL, soft=ACCENT):
    """A bed: a frame, a mattress (top 0.52), a headboard at -z, two pillows (top 0.64), a blanket over the foot
    half (top 0.55), four legs (about 18 nodes)."""
    hw, hl = w / 2, l / 2
    return U(S(X(((-hw, hw), (0.1, 0.32), (-hl, hl))), k),
             S(K.mirror([1, 0, 1], X(((hw - 0.08, hw), (0.0, 0.1), (hl - 0.08, hl)))), k),
             S(X(((-hw + 0.03, hw - 0.03), (0.32, 0.52), (-hl + 0.035, hl - 0.025))), soft),
             S(X(((-hw - 0.03, hw + 0.03), (0.0, 1.05), (-hl - 0.065, -hl))), k),
             S(K.mirror([1, 0, 0], T([w / 4, 0.52, -0.72], pillow(w / 2 - 0.12, 0.34, 0.12, soft))), soft),
             S(X(((-hw + 0.01, hw - 0.01), (0.52, 0.55), (0.1, hl - 0.015))), k))
