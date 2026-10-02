"""All You Got, art pass 4: the props the new staging needs (the kit's conventions: metres, base on y = 0, footprint
centred, front facing +Z; sharp edges so the line look draws them). Each prop's docstring gives its node count.

  the kitchen   stove (a range with an oven and four burner rings; the front-left ring glows), pot (a steaming
                pan), range_hood (with the light under it), spoon
  the gym       weight_bench, squat_rack (a barbell racked across two uprights), dumbbell_rack, exercise_mat,
                exercise_bike
  the lounge    bar_counter (with a foot rail), bar_stool, back_bar (shelves of bottles over a strip of coloured
                light), neon_sign, low_table, tv_stand
  the city      car, traffic_light, park_bench, street_lamp, bus_shelter, planter
"""

from __future__ import annotations

import kit as K
from kit import ACCENT, CANVAS, CANVAS2, FILL, GLASS, GLOW, R, S, SCREEN, T, U, X, box
from kit import FLOOR as FLOOR_


# =============================================================================================================
# THE KITCHEN
# =============================================================================================================

def stove(w=0.76, h=0.9, d=0.62, k=FILL, metal=ACCENT, ring=GLOW, name_ring=None):
    """A freestanding range: the body on a toe kick, an oven door with its window and a bar handle, a control strip
    with four knobs, a cooktop with four burner rings (the front-left one on GLOW: lit), a low backsplash
    (about 20 nodes). The front-left burner's centre is (-0.17, 0.9, 0.12)."""
    body = K.D(X(((-w / 2, w / 2), (0.06, h - 0.02), (-d / 2, d / 2))),
               X(((-w / 2 + 0.05, w / 2 - 0.05), (0.14, 0.62), (d / 2 - 0.02, d / 2 + 0.1))))
    door = U(X(((-w / 2 + 0.05, w / 2 - 0.05), (0.14, 0.62), (d / 2 - 0.04, d / 2 - 0.02))),
             X(((-w / 2 + 0.06, w / 2 - 0.06), (0.66, 0.68), (d / 2 + 0.02, d / 2 + 0.045))))           # the handle
    window = X(((-w / 2 + 0.14, w / 2 - 0.14), (0.26, 0.5), (d / 2 - 0.025, d / 2 - 0.015)))
    knobs = T([0.0, 0.79, d / 2 + 0.01], K.I(K.repeat([0.13, 0, 0], 0, R([90, 0, 0], K.cyl(0.022, 0.03))),
                                              box([0.25, 0.05, 0.05])))
    top = X(((-w / 2, w / 2), (h - 0.03, h), (-d / 2, d / 2)))
    rings_off = U(T([0.17, h, 0.12], K.tor(0.075, 0.009)), T([-0.17, h, -0.14], K.tor(0.075, 0.009)),
                  T([0.17, h, -0.14], K.tor(0.075, 0.009)))
    lit = T([-0.17, h, 0.12], K.tor(0.08, 0.011), name=name_ring)
    splash = X(((-w / 2, w / 2), (h, h + 0.12), (-d / 2, -d / 2 + 0.03)))
    kick = X(((-w / 2 + 0.03, w / 2 - 0.03), (0.0, 0.06), (-d / 2 + 0.03, d / 2 - 0.06)))
    return U(S(body, k), S(door, metal), S(window, GLASS), S(knobs, metal), S(top, metal), S(rings_off, metal),
             S(lit, ring), S(U(splash, kick), k))


def pot(r=0.13, h=0.15, k=ACCENT):
    """A cooking pot (its base on y = 0): a hollow cylinder, a rim, two side handles (about 8 nodes)."""
    body = K.D(K.CY([0, h / 2, 0], r, h), K.CY([0, h / 2 + 0.02, 0], r - 0.012, h))
    rim = T([0, h, 0], K.tor(r - 0.004, 0.007))
    handles = K.mirror([1, 0, 0], X(((r - 0.005, r + 0.05), (h - 0.035, h - 0.02), (-0.025, 0.025))))
    soup = K.CY([0, h - 0.035, 0], r - 0.014, 0.01)
    return U(S(body, k), S(rim, k), S(handles, FILL), S(soup, GLOW))


def range_hood(w=0.8, d=0.5, k=FILL, light=GLOW):
    """A chimney hood (its back on z = 0, facing +Z; origin at the hood's lower edge): a canopy tapering into a
    duct, a light strip under its front edge (about 7 nodes)."""
    canopy = K.I(X(((-w / 2, w / 2), (0.0, 0.32), (0.0, d))),
                 {"kind": "plane", "axis": [0.0, 0.7071, 0.7071], "offset": 0.7071 * (0.32 + 0.12)})
    duct = X(((-0.15, 0.15), (0.3, 1.0), (0.0, 0.26)))
    lamp = X(((-w / 2 + 0.08, w / 2 - 0.08), (-0.012, 0.0), (d - 0.16, d - 0.06)))
    return U(S(canopy, k), S(duct, k), S(lamp, light))


def spoon(L=0.34, k=ACCENT):
    """A wooden spoon along +Y from its handle end at the origin to its bowl (about 4 nodes)."""
    return S(U(X(((-0.008, 0.008), (0.0, L - 0.06), (-0.005, 0.005))), K.CY([0, L - 0.03, 0], 0.025, 0.012)), k)


# =============================================================================================================
# THE GYM
# =============================================================================================================

def weight_bench(L=1.2, k=FILL, pad=ACCENT):
    """A flat bench: a padded top at 0.44 m on two T-feet (about 8 nodes)."""
    return U(S(X(((-0.15, 0.15), (0.38, 0.44), (-L / 2, L / 2))), pad),
             S(K.mirror([0, 0, 1], U(X(((-0.03, 0.03), (0.05, 0.38), (L / 2 - 0.2, L / 2 - 0.14))),
                                    X(((-0.25, 0.25), (0.0, 0.05), (L / 2 - 0.22, L / 2 - 0.12))))), k))


def squat_rack(w=1.25, h=2.1, k=FILL, metal=ACCENT):
    """A rack: two uprights on a base frame, a barbell racked across them at 1.35 m with a plate either end (about
    14 nodes)."""
    up = K.mirror([1, 0, 0], X(((w / 2 - 0.04, w / 2 + 0.04), (0.0, h), (-0.04, 0.04))))
    base = K.mirror([1, 0, 0], X(((w / 2 - 0.05, w / 2 + 0.05), (0.0, 0.05), (-0.5, 0.5))))
    hooks = K.mirror([1, 0, 0], X(((w / 2 - 0.05, w / 2 + 0.05), (1.3, 1.34), (0.04, 0.12))))
    bar = T([0, 1.37, 0.08], R([0, 0, 90], K.cyl(0.016, w + 0.6)))
    plates = K.mirror([1, 0, 0], T([w / 2 + 0.17, 1.37, 0.08], R([0, 0, 90], K.cyl(0.22, 0.05))))
    return U(S(U(up, base), k), S(hooks, metal), S(bar, metal), S(plates, k))


def dumbbell_rack(w=1.1, k=FILL, metal=ACCENT):
    """A two-tier rack of dumbbells (about 12 nodes)."""
    frame = U(K.mirror([1, 0, 0], X(((w / 2 - 0.04, w / 2), (0.0, 0.75), (-0.2, 0.2)))),
              X(((-w / 2, w / 2), (0.4, 0.43), (-0.2, 0.2))), X(((-w / 2, w / 2), (0.7, 0.73), (-0.2, 0.2))))
    bell = U(T([0, 0, -0.1], R([90, 0, 0], K.cyl(0.05, 0.06))), T([0, 0, 0.1], R([90, 0, 0], K.cyl(0.05, 0.06))),
             R([90, 0, 0], K.cyl(0.015, 0.2)))
    row = K.I(T([-w / 2 + 0.12, 0.0, 0.0], K.repeat([0.18, 0.3, 0], 0, bell)), X(((-w / 2 + 0.05, w / 2 - 0.05), (0.43, 0.84), (-0.3, 0.3))))
    return U(S(frame, k), S(T([0, 0.05, 0], row), metal))


def exercise_mat(w=0.7, L=1.8, k=ACCENT):
    """A rolled-out mat (2 nodes)."""
    return S(X(((-w / 2, w / 2), (0.0, 0.015), (-L / 2, L / 2))), k)


def exercise_bike(k=FILL, metal=ACCENT):
    """A stationary bike: a base, a frame post with the saddle, the handlebar post, a flywheel (about 10 nodes)."""
    return U(S(X(((-0.25, 0.25), (0.0, 0.05), (-0.55, 0.55))), k),
             S(K.SEG([0, 0.05, -0.3], [0, 0.8, -0.38], 0.03, box_section=(0.03, 0.03)), k),
             S(X(((-0.12, 0.12), (0.8, 0.86), (-0.5, -0.26))), metal),
             S(K.SEG([0, 0.05, 0.25], [0, 1.05, 0.35], 0.03, box_section=(0.03, 0.03)), k),
             S(X(((-0.25, 0.25), (1.03, 1.07), (0.32, 0.4))), metal),
             S(T([0, 0.35, 0.22], R([0, 0, 90], K.cyl(0.24, 0.06))), metal))


# =============================================================================================================
# THE LOUNGE AND ITS BAR
# =============================================================================================================

def bar_counter(L=2.6, h=1.08, d=0.6, k=FILL, top=ACCENT, glow=GLOW):
    """A home bar along x: a panelled front (+Z), an overhanging top at 1.08 m, a foot rail, and a light strip
    under the top's front lip (about 12 nodes). The customer side is +Z."""
    body = K.D(X(((-L / 2, L / 2), (0.0, h - 0.04), (-d / 2, d / 2 - 0.08))),
               T([0.0, 0.55, d / 2 - 0.08], K.I(K.repeat([0.65, 0, 0], 0, box([0.26, 0.4, 0.015])), box([L / 2 - 0.1, 0.45, 0.05]))))
    worktop = X(((-L / 2 - 0.05, L / 2 + 0.05), (h - 0.04, h), (-d / 2, d / 2 + 0.06)))
    rail = T([0.0, 0.22, d / 2 + 0.02], R([0, 0, 90], K.cyl(0.02, L - 0.2)))
    strip = X(((-L / 2 + 0.05, L / 2 - 0.05), (h - 0.07, h - 0.05), (d / 2 - 0.08, d / 2 - 0.06)))
    return U(S(body, k), S(worktop, top), S(rail, ACCENT), S(strip, glow))


def bar_stool(seat=0.76, k=FILL, cushion=ACCENT):
    """A bar stool: a round seat at 0.76 m on a post, a foot ring, a weighted base (about 7 nodes)."""
    return U(S(K.CY([0, seat - 0.03, 0], 0.19, 0.06), cushion), S(K.CY([0, seat / 2, 0], 0.025, seat - 0.06), k),
             S(T([0, 0.28, 0], K.tor(0.15, 0.012)), ACCENT), S(K.CY([0, 0.02, 0], 0.2, 0.04), k))


def back_bar(L=2.4, h=1.9, k=FILL, bottles=CANVAS, bottles2=SCREEN, strip=CANVAS2, neon=FLOOR_):
    """The bar's back wall (its back on z = 0, facing +Z): a low cabinet, two glass shelves of bottles (two colours)
    over strips of coloured light, a mirror panel behind them, a neon tube across the top (about 20 nodes)."""
    cab = X(((-L / 2, L / 2), (0.0, 0.92), (0.0, 0.45)))
    shelves = U(X(((-L / 2, L / 2), (1.25, 1.27), (0.0, 0.22))), X(((-L / 2, L / 2), (1.6, 1.62), (0.0, 0.22))))
    bottle = U(K.CY([0, 0.11, 0], 0.035, 0.22), K.CY([0, 0.26, 0], 0.012, 0.09))
    row1 = K.I(T([0, 0.92, 0.25], K.repeat([0.13, 0, 0], 0, bottle)), X(((-L / 2 + 0.06, L / 2 - 0.06), (0.9, 1.25), (0.15, 0.4))))
    row2 = K.I(T([0.065, 1.27, 0.1], K.repeat([0.15, 0.35, 0], 0, bottle)), X(((-L / 2 + 0.06, L / 2 - 0.06), (1.25, 1.95), (0.0, 0.2))))
    lights = U(X(((-L / 2, L / 2), (1.23, 1.25), (0.02, 0.2))), X(((-L / 2, L / 2), (1.58, 1.6), (0.02, 0.2))),
               X(((-L / 2, L / 2), (0.92, 0.94), (0.05, 0.42))))
    panel = X(((-L / 2, L / 2), (0.92, h), (0.0, 0.02)))
    tube = K.D(X(((-L / 2 + 0.2, L / 2 - 0.2), (h + 0.12, h + 0.4), (0.03, 0.06))), X(((-L / 2 + 0.24, L / 2 - 0.24), (h + 0.16, h + 0.36), (0.0, 0.1))))
    return U(S(cab, k), S(shelves, ACCENT), S(row1, bottles), S(row2, bottles2), S(lights, strip), S(panel, GLASS), S(tube, neon))


def neon_sign(w=0.9, h=0.32, k=GLOW):
    """A neon sign for a wall (its back on z = 0): a rounded frame tube and a wave stroke across it (about 8 nodes)."""
    frame = K.D(X(((-w / 2, w / 2), (-h / 2, h / 2), (0.03, 0.05))), X(((-w / 2 + 0.03, w / 2 - 0.03), (-h / 2 + 0.03, h / 2 - 0.03), (0.0, 0.1))))
    wave = T([0, 0, 0.045], K.wave(box([w / 2 - 0.1, 0.012, 0.012]), 0.05, 9.0, [0.0, 1.0, 0.0]))
    return U(S(frame, k), S(wave, k), S(X(((-w / 2, w / 2), (-h / 2, h / 2), (0.0, 0.012))), FILL))


def low_table(w=1.0, d=0.55, h=0.4, k=FILL):
    """A low coffee table on a plinth (about 4 nodes)."""
    return S(U(X(((-w / 2, w / 2), (h - 0.04, h), (-d / 2, d / 2))), X(((-w / 2 + 0.06, w / 2 - 0.06), (0.0, h - 0.04), (-d / 2 + 0.06, d / 2 - 0.06)))), k)


def tv_stand(w=1.2, k=FILL, screen=SCREEN):
    """A flat television on a low stand (the screen shows static) (about 7 nodes). The screen's centre is at 0.86 m."""
    return U(S(X(((-w / 2, w / 2), (0.0, 0.45), (-0.2, 0.2))), k),
             S(K.D(X(((-0.55, 0.55), (0.52, 1.2), (-0.05, 0.03))), X(((-0.52, 0.52), (0.55, 1.17), (0.0, 0.1)))), k),
             S(X(((-0.52, 0.52), (0.55, 1.17), (-0.01, 0.005))), screen), S(X(((-0.05, 0.05), (0.45, 0.52), (-0.04, 0.0))), k))


# =============================================================================================================
# THE CITY
# =============================================================================================================

def car(L=4.3, w=1.8, k=FILL, cabin=ACCENT, head=GLOW, tail=CANVAS, open_cabin=False, roof=1.45, seat=0.4):
    """A small sedan facing +Z: the body, the cabin, four wheels, headlights and tail lights (about 14 nodes solid).
    `open_cabin`: the cabin is a shell with its windows open (a driver inside can be seen), with seats and a
    steering wheel (about 30 nodes). Left-hand drive (facing +Z the driver's left is +X): the driver's seat centre is
    (0.38, seat, -0.25)."""
    hl, hw = L / 2, w / 2
    front = {"kind": "plane", "axis": [0.0, 0.8, 0.6], "offset": 0.8 * roof + 0.6 * (hl - 1.6)}
    rear = {"kind": "plane", "axis": [0.0, 0.8, -0.6], "offset": 0.8 * roof + 0.6 * (hl - 1.25)}
    cab = K.I(X(((-hw + 0.08, hw - 0.08), (0.85, roof), (-hl + 0.9, hl - 1.25))), front, rear)
    wheels = K.mirror([1, 0, 1], T([hw - 0.12, 0.31, hl - 0.8], R([0, 0, 90], K.cyl(0.31, 0.22))))
    lights = K.mirror([1, 0, 0], X(((hw - 0.42, hw - 0.12), (0.62, 0.74), (hl - 0.02, hl + 0.01))))
    tails = K.mirror([1, 0, 0], X(((hw - 0.38, hw - 0.1), (0.64, 0.76), (-hl - 0.01, -hl + 0.02))))
    if not open_cabin:
        body = X(((-hw, hw), (0.3, 0.85), (-hl, hl)))
        return U(S(body, k), S(cab, cabin), S(wheels, FILL), S(lights, head), S(tails, tail))
    floor = seat - 0.06
    body = K.D(X(((-hw, hw), (0.3, 0.85), (-hl, hl))), X(((-hw + 0.1, hw - 0.1), (floor, 0.95), (-hl + 0.95, hl - 1.28))))
    inner = K.I(X(((-hw + 0.13, hw - 0.13), (0.8, roof - 0.05), (-hl + 0.95, hl - 1.3))),
                {"kind": "plane", "axis": [0.0, 0.8, 0.6], "offset": front["offset"] - 0.05},
                {"kind": "plane", "axis": [0.0, 0.8, -0.6], "offset": rear["offset"] - 0.05})
    windows = U(X(((-hw - 0.2, hw + 0.2), (0.93, roof - 0.09), (-hl + 1.05, -0.2))), X(((-hw - 0.2, hw + 0.2), (0.93, roof - 0.09), (-0.1, hl - 1.42))),
                X(((-hw + 0.2, hw - 0.2), (0.93, roof - 0.08), (-hl, hl))))
    shell = K.D(cab, inner, windows)
    seats = U(X(((-hw + 0.18, -0.1), (0.3, seat), (-0.6, 0.05))), X(((-hw + 0.18, -0.1), (seat, 1.1), (-0.7, -0.58))),
              X(((0.1, hw - 0.18), (0.3, seat), (-0.6, 0.05))), X(((0.1, hw - 0.18), (seat, 1.1), (-0.7, -0.58))))
    dash = X(((-hw + 0.1, hw - 0.1), (0.75, 0.95), (hl - 1.45, hl - 1.25)))
    wheel = T([0.38, 0.92, hl - 1.6], R([-60, 0, 0], K.tor(0.17, 0.016)))
    return U(S(body, k), S(shell, cabin), S(wheels, FILL), S(lights, head), S(tails, tail), S(U(seats, dash), k), S(wheel, ACCENT))


def traffic_light(h=3.2, arm=2.6, k=FILL, lamps=False, red=CANVAS, amber=CANVAS2, green=SCREEN):
    """A signal on a pole with an arm over the road (+Z) and a signal head at its end facing +X (towards traffic
    coming from +X) (about 9 nodes). `lamps`: the three lamps on the head too (red CANVAS, amber CANVAS2, green
    SCREEN; the city keeps them in a separate object so the film can key them) (about 18 nodes)."""
    pole = U(K.CY([0, h / 2, 0], 0.07, h), K.CY([0, 0.05, 0], 0.16, 0.1))
    beam = X(((-0.04, 0.04), (h - 0.08, h), (0.0, arm)))
    head = X(((-0.12, 0.12), (h - 0.92, h - 0.08), (arm - 0.24, arm + 0.04)))
    out = [S(pole, k), S(beam, k), S(head, k)]
    if lamps:
        f = R([0, 0, 90], K.cyl(0.085, 0.02))
        for dy, kk in ((0.27, red), (0.0, amber), (-0.27, green)):
            out.append(S(T([0.13, h - 0.5 + dy, arm - 0.1], f), kk))
    return U(*out)


def park_bench(L=1.7, k=FILL, slats=ACCENT):
    """A park bench (front +Z): two cast ends, three seat slats at 0.45 m, two back slats (about 12 nodes)."""
    ends = K.mirror([1, 0, 0], U(X(((L / 2 - 0.06, L / 2), (0.0, 0.45), (-0.25, 0.25))),
                                 T([L / 2 - 0.03, 0.62, -0.27], R([-12, 0, 0], box([0.03, 0.3, 0.03])))))
    seat = T([0.0, 0.45, 0.0], K.I(K.repeat([0, 0, 0.16], 1, box([L / 2, 0.02, 0.06])), box([L / 2 + 0.1, 0.1, 0.3])))
    back = T([0.0, 0.0, -0.3], R([-12, 0, 0], K.I(T([0, 0.62, 0], K.repeat([0, 0.18, 0], 0, box([L / 2, 0.06, 0.02]))),
                                                   X(((-L / 2, L / 2), (0.52, 0.92), (-0.1, 0.1))))))
    return U(S(ends, k), S(seat, slats), S(back, slats))


def street_lamp(h=4.6, reach=1.1, k=FILL, glow=GLOW):
    """A city street lamp: a pole, an arm out over the road (+Z), a glowing head (about 6 nodes)."""
    return U(S(K.CY([0, h / 2, 0], 0.08, h), k), S(X(((-0.05, 0.05), (h - 0.1, h), (0.0, reach))), k),
             S(X(((-0.2, 0.2), (h - 0.24, h - 0.08), (reach - 0.45, reach + 0.05))), glow))


def bus_shelter(w=3.0, k=FILL, glass=GLASS, ad=CANVAS2):
    """A bus shelter open to +Z: a roof, a back glass wall, side panels, a bench, a lit advert panel (about 12
    nodes)."""
    return U(S(X(((-w / 2, w / 2), (2.4, 2.5), (-0.75, 0.75))), k),
             S(X(((-w / 2, w / 2), (0.1, 2.4), (-0.75, -0.7))), glass),
             S(K.mirror([1, 0, 0], X(((w / 2 - 0.05, w / 2), (0.0, 2.4), (-0.75, 0.6)))), k),
             S(X(((-w / 2 + 0.3, w / 2 - 0.3), (0.42, 0.47), (-0.7, -0.35))), ACCENT),
             S(X(((w / 2 - 0.04, w / 2 - 0.02), (0.5, 2.1), (-0.55, 0.45))), ad))


def planter(w=1.2, h=0.5, k=FILL, leaves=ACCENT):
    """A street planter with a low hedge (about 4 nodes)."""
    return U(S(X(((-w / 2, w / 2), (0.0, h), (-w / 4, w / 4))), k), S(X(((-w / 2 + 0.05, w / 2 - 0.05), (h, h + 0.35), (-w / 4 + 0.05, w / 4 - 0.05))), leaves))


def street_tree(h=4.2, k=FILL, leaves=ACCENT):
    """A street tree for the line look, standing level on its foot: a square trunk and two flat octagonal crowns,
    the upper one smaller and turned 22.5 degrees (pass 3's round tree was a cube balanced on a corner on a stick,
    which read as a die tumbling, not a tree) (about 14 nodes)."""
    def octo(r, hh, yaw=0.0):
        o = K.I(box([r, hh, r]), R([0, 45, 0], box([r, hh, r])))
        return R([0, yaw, 0], o) if yaw else o
    trunk = S(T([0, h * 0.27, 0], box([h * 0.024, h * 0.27, h * 0.024])), k)
    low = S(T([0, h * 0.6, 0], octo(h * 0.25, h * 0.13)), leaves)
    top = S(T([0, h * 0.84, 0], octo(h * 0.16, h * 0.11, 22.5)), leaves)
    return U(trunk, low, top)


def picket_fence(x0=-4.0, x1=4.0, gap=(-0.7, 0.7), h=0.8, pitch=0.5, k=FILL):
    """A front garden's picket fence along x (its face at z = 0, 3 cm thick), a gap for the path; the hero house's
    fence's proportions (rails at 0.28 and 0.58 of a 0.8 m fence, pickets every 0.5 m) (about 14 nodes)."""
    n = int((x1 - x0) / pitch / 2) + 2
    pickets = T([0.0, h / 2, 0.0], K.repeat([pitch, 0, 0], n, box([0.03, h / 2, 0.03])))
    pickets = K.I(pickets, X(((x0, x1), (0.0, h + 0.1), (-0.1, 0.1))))
    rails = U(X(((x0, x1), (0.25, 0.31), (-0.03, 0.03))), X(((x0, x1), (0.55, 0.62), (-0.03, 0.03))))
    return S(K.D(U(pickets, rails), X(((gap[0], gap[1]), (-0.5, h + 0.5), (-0.2, 0.2)))), k)
