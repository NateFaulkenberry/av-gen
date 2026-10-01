"""All You Got, art pass 2: the open -- a primitive 3D landscape (a grid-lined rolling terrain, cone and faceted
trees, boxy rocks, grass, pyramid mountains), the house's rooms floating as lanterns, the staircase into the
sky, the summit; and bridge 1's tree of rooms. Line-drawn like the interiors (the SDF edge term), never the GV3
assets' look (the brief, section 5).

The terrain is a slab with two crossed sine waves (`displaceWave`) applied to it AND its grid lines together, so
the lines lie on the hills. `ground(x, z)` is the same formula in Python, to stand trees and the camera on it.
"""

from __future__ import annotations

import math

import kit as K
from kit import ACCENT, CANVAS, CANVAS2, FILL, FLOOR, GLASS, GLOW, R, S, T, U, X, box, place

HILL_A, HILL_FX = 1.6, 0.11     # amplitude (m), frequency (rad/m) of the wave along x
HILL_B, HILL_FZ = 1.2, 0.083    # ... along z


def ground(x: float, z: float) -> float:
    """The terrain's height at (x, z): the surface of `terrain()` (the displacement is subtracted from the slab's
    distance, so a positive wave raises the ground)."""
    return -(HILL_A * math.sin(x * HILL_FX) + HILL_B * math.sin(z * HILL_FZ))


def terrain(size=160.0, pitch=3.0, k=FLOOR):
    """The ground: a slab (top at y = 0) with raised grid ribs every `pitch` metres, both displaced by the two
    waves (9 nodes). The ribs are big enough (16 cm wide, 9 cm proud) for the march to find them on the hills."""
    slab = X(((-size, size), (-4.0, 0.0), (-size, size)))
    grid = U(K.repeat([0, 0, pitch], 0, box([size, 0.09, 0.08])), K.repeat([pitch, 0, 0], 0, box([0.08, 0.09, size])))
    body = U(slab, K.I(grid, box([size, 0.5, size])))
    w1 = K.wave(body, HILL_A, HILL_FX, [1.0, 0.0, 0.0])
    w2 = K.wave(w1, HILL_B, HILL_FZ, [0.0, 0.0, 1.0])
    return S(w2, k)


def mountains(k=FILL):
    """A range of pyramids on the horizon (-z), their ridges drawn (about 24 nodes)."""
    peaks = [(-70.0, -120.0, 46.0, 30.0), (-20.0, -140.0, 60.0, 42.0), (35.0, -125.0, 50.0, 34.0), (85.0, -135.0, 56.0, 38.0)]
    return U(*[S(T([x, -6.0, z], R([0, 20 * i, 0], K.pyramid(b, h))), k) for i, (x, z, b, h) in enumerate(peaks)])


def grove(spots, k=FILL, leaves=ACCENT):
    """Trees standing on the terrain: spots [(kind, x, z, height)] (kind 'pine' or 'round')."""
    parts = []
    for kind, x, z, h in spots:
        tree = K.pine(h, k, leaves) if kind == "pine" else K.round_tree(h, k, leaves)
        parts.append(place(tree, (x, ground(x, z) - 0.05, z)))
    return U(*parts)


def stones(spots, k=FILL):
    return U(*[place(K.rock(s, k, i), (x, ground(x, z) - 0.1, z), 30.0 * i) for i, (x, z, s) in enumerate(spots)])


def tufts(spots, k=ACCENT):
    return U(*[place(K.grass_tuft(k), (x, ground(x, z) - 0.02, z), 40.0 * i) for i, (x, z) in enumerate(spots)])


def lantern_room(k=FILL, name=None):
    """A small floating room: a box shell with a door and a window, a lamp glowing inside, a floor line
    (about 13 nodes). Its origin is its floor's centre."""
    shell = K.D(K.shell(((-1.0, 1.0), (0.0, 1.8), (-1.0, 1.0)), 0.08),
                X(((-0.35, 0.35), (0.0, 1.45), (0.6, 1.4))), X(((-1.4, -0.6), (0.7, 1.3), (-0.35, 0.35))))
    lamp = S(K.SP([0.2, 1.2, -0.2], 0.16), GLOW)
    body = U(S(shell, k), lamp)
    return R([0, 0, 0], body, name=name) if name else body


def lantern_ring(radius=14.0, count=7, y=10.0, name="ring", k=FILL):
    """A ring of floating rooms about the y axis, turning together (`<name>` rotation y) (about 18 nodes)."""
    mod = T([radius, 0.0, 0.0], R([0, 0, 8], lantern_room(k)))
    return T([0, y, 0], R([0, 0, 0], K.polar(count, mod), name=name))


def sky_stair(steps=36, run=0.42, rise=0.26, width=2.2, k=FILL):
    """The staircase into the sky: a floating flight (its foot at the origin, climbing +x) with a rail line
    along each side (8 nodes)."""
    flight = {"kind": "stairs", "size": [run, rise, width / 2], "count": steps, "height": 0.35}
    L = steps * run
    H = steps * rise
    rails = K.mirror([0, 0, 1], K.SEG([0.0, 1.0, width / 2], [L, H + 1.0, width / 2], 0.025))
    return U(S(flight, k), S(rails, ACCENT))


def summit(k=FILL):
    """The platform at the top of the stair: a disc with concentric rings and a low wall (about 10 nodes)."""
    return U(S(K.CY([0, -0.2, 0], 6.0, 0.4), k), S(K.T([0, 0.0, 0], K.tor(4.5, 0.05)), ACCENT),
             S(K.T([0, 0.0, 0], K.tor(2.5, 0.05)), ACCENT), S(K.D(K.CY([0, 0.5, 0], 6.0, 1.0), K.CY([0, 0.5, 0], 5.8, 2.0)), k))


def sun_disc(r=6.0, k=GLOW):
    """A line-drawn sun: a glowing disc and two halo rings, facing +z (about 8 nodes)."""
    return U(S(R([90, 0, 0], K.cyl(r, 0.3)), k), S(R([90, 0, 0], K.tor(r * 1.35, 0.12)), ACCENT),
             S(R([90, 0, 0], K.tor(r * 1.75, 0.08)), ACCENT))


# ---- the tree of rooms (bridge 1) ---------------------------------------------------------------------------
TREE_ROOMS = 14
TREE_R = 3.4          # branch length: trunk axis to a room's centre
TREE_STEP = 2.15      # rise per room
TREE_TURN = 137.5     # degrees between rooms (the golden angle: no room hides another)


def tree_room_pose(i):
    a = math.radians(TREE_TURN * i)
    return (TREE_R * math.cos(a), 1.2 + TREE_STEP * i, -TREE_R * math.sin(a)), TREE_TURN * i


def tree_trunk(k=FILL):
    """The trunk: a tall square column with a band at every storey (8 nodes)."""
    H = 1.2 + TREE_STEP * TREE_ROOMS + 2.0
    return U(S(X(((-0.6, 0.6), (-2.0, H), (-0.6, 0.6))), k),
             S(T([0, 1.2, 0], K.I(K.repeat([0, TREE_STEP, 0], 0, box([0.66, 0.03, 0.66])), box([1, H, 1]))), ACCENT))


def tree_branch_group(indices, k=FILL):
    """Rooms on the trunk, each on a branch, each a named uniform scale (`gr<i>`, 0 -> 1 is its bloom)."""
    parts = []
    for i in indices:
        (x, y, z), yaw = tree_room_pose(i)
        branch = S(K.SEG([0.0, y - 0.1, 0.0], [x * 0.72, y - 0.1, z * 0.72], 0.08, box_section=(0.07, 0.07)), k)
        room = T([x, y, z], {"kind": "scale", "scale": 1.0, "name": f"gr{i}",
                             "children": [R([0, yaw + 90.0, 0], lantern_room(k))]})
        parts += [branch, room]
    return U(*parts)


def sky_furniture(k=K.FILL):
    """The house's furniture adrift in the sky (the final chorus): a couch, an armchair, a lamp, a TV, a chair and
    a table, each turning on its own named node (`sf<i>`), scattered over the hills (about 90 nodes)."""
    pieces = [(K.couch(2.0, 0.88, 0.82), (-9.0, 11.0, -6.0)), (K.armchair(), (7.0, 13.5, -12.0)),
              (K.floor_lamp(1.6), (-3.0, 15.0, -18.0)), (K.chair(), (12.0, 9.5, -3.0)),
              (K.round_table(0.45, 0.72), (-14.0, 8.5, -16.0))]
    parts = []
    for i, (piece, at) in enumerate(pieces):
        parts.append(K.T(at, K.R([0, 0, 0], K.T([0, -0.5, 0], piece), name=f"sf{i}")))
    return K.U(*parts)


def tree_canopy(k=K.GLOW):
    """Leaves of light at the top of the tree of rooms: a dome of small glowing cubes (about 10 nodes)."""
    top = 1.2 + TREE_STEP * TREE_ROOMS + 1.5
    ring1 = K.T([0, top, 0], K.polar(9, K.T([2.4, 0, 0], K.R([30, 0, 30], K.box([0.28, 0.28, 0.28])))))
    ring2 = K.T([0, top + 1.6, 0], K.R([0, 20, 0], K.polar(6, K.T([1.4, 0, 0], K.R([10, 0, 45], K.box([0.24, 0.24, 0.24]))))))
    crown = K.T([0, top + 2.8, 0], K.R([45, 0, 45], K.box([0.35, 0.35, 0.35])))
    return K.S(K.U(ring1, ring2, crown), k)
