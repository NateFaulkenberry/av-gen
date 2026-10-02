"""All You Got, art pass 4: the city (04-art-pass-4.md, PARTS 8-10 and 13: house -> neighbourhood -> city).

One city in the house frame (rooms3/intro3): the hero house stands at x -2.75..5.1 with its front at z = 2.35, the
neighbourhood's main street runs along x at z = 12. The city is a grid of 40 m blocks around the neighbourhood:

    x-roads (along x) at z = 12 + 40k        (k = 0 is the main street)
    z-roads (along z) at x = HX + 20 + 40i   (HX = the hero lot's centre, 1.175)
    blocks centred at (HX + 40i, -8 + 40k), 28 m across inside 12 m roads (a 9 m carriageway, 1.5 m pavements)

The neighbourhood is a superblock (x -98.8..101.2, z -22..46) the city leaves alone. Everything else grows:

    Low     a podium on every block: three storeys, lit shopfronts at street level
    Mid     a nine-storey slab on every block's west half
    Tower   a 66 m tower on every other block (a checkerboard), east half
    Sky     a 120 m tower on every third block (the downtown core)

Each set is split into an INNER object (the blocks the intro's camera can see, between the neighbourhood and the far
skyline: they grow storey by storey in the intro, build -> lock) and an OUTER one (the rest: it grows ring by ring
outward from the neighbourhood in 'let it grow', bars 67-72). Every set has two named clips:

    <obj>Rise    a box from the ground up (its half height is the current height): one storey per beat
    <obj>Ring    a box about the hero lot whose half size grows a block (40 m) at a time: the city spreads

The set pieces of the 'is that all you' bridge stand at the downtown intersection (x = HX + 20, z = -108):
the park (the block south-west of it), the bar (the corner unit of the block south-east of it), traffic lights
on its corners and crosswalks on its four approaches. The generic sets leave those two blocks alone.
"""

from __future__ import annotations

import math

import kit as K
import props4 as P4
from kit import ACCENT, CANVAS, CANVAS2, FILL, FLOOR, GLASS, GLOW, R, S, SCREEN, T, U, X, box

HX = 1.175
PITCH = 40.0
ROAD_HALF = 6.0          # carriageway 4.5 + pavement 1.5
LANE = 2.0               # a lane's centre from the road's centre
MAIN_Z = 12.0
XI = HX + 20.0           # the z-road through the downtown intersection
ZK = -108.0              # the x-road through it
NEIGH = ((-92.8, 95.2), (-22.0, 46.0))     # the neighbourhood superblock (x, z) the city leaves alone
INNER = ((-100.0, 140.0), (-235.0, -22.0))  # the blocks the intro's camera sees (x, z)
SET_PIECE_BLOCKS = [(HX, -88.0), (HX + 40.0, -88.0)]   # the park (SW of the intersection), the bar's block (SE)
STOREY = 3.2


def block_centre(i, k):
    return (HX + PITCH * i, -8.0 + PITCH * k)


def zone(ext, y=(-1.0, 400.0)):
    (x0, x1), (z0, z1) = ext
    return X(((x0, x1), y, (z0, z1)))


def holes():
    """What the generic sets leave alone: the neighbourhood and the set-piece blocks."""
    hs = [zone(NEIGH)]
    for cx, cz in SET_PIECE_BLOCKS:
        hs.append(X(((cx - 15.0, cx + 15.0), (-1.0, 400.0), (cz - 15.0, cz + 15.0))))
    return hs


# ---- buildings -------------------------------------------------------------------------------------------------
def building(w, d, h, floor=STOREY, k=FILL, bands=CANVAS2, shop=None, crown=None):
    """A box building on y = 0: storey bands 4 cm proud every `floor` metres (lit: CANVAS2), an optional lit shop band
    at street level (`shop`: a surface), an optional crown light on the roof (`crown`: a surface) (about 9-13 nodes)."""
    body = X(((-w / 2, w / 2), (0.0, h), (-d / 2, d / 2)))
    band = K.I(T([0.0, floor, 0.0], K.repeat([0.0, floor, 0.0], 0, box([w / 2 + 0.05, 0.07, d / 2 + 0.05]))),
               X(((-w / 2 - 0.2, w / 2 + 0.2), (floor * 0.6, h - floor * 0.4), (-d / 2 - 0.2, d / 2 + 0.2))))
    parts = [S(body, k), S(band, bands)]
    if shop is not None:
        parts.append(S(K.D(X(((-w / 2 - 0.06, w / 2 + 0.06), (0.4, 2.9), (-d / 2 - 0.06, d / 2 + 0.06))),
                           X(((-w / 2 + 0.3, w / 2 - 0.3), (0.0, 3.5), (-d / 2 + 0.3, d / 2 - 0.3)))), shop))
    if crown is not None:
        parts.append(S(X(((-w / 2 + 0.6, w / 2 - 0.6), (h, h + 0.6), (-d / 2 + 0.6, d / 2 - 0.6))), crown))
    return U(*parts)


SETS = {
    # name: (footprint w, d, height, offset in the block (x, z), repeat pitch (x, z), the grid origin's block (i, k))
    "Low": (26.0, 26.0, 3 * STOREY, (0.0, 0.0), (PITCH, PITCH), (0, 0)),
    "Mid": (13.0, 20.0, 9 * STOREY, (-6.5, 1.0), (PITCH, PITCH), (0, 0)),
    "TowerA": (12.0, 12.0, 21 * STOREY, (6.5, -5.0), (2 * PITCH, 2 * PITCH), (0, 0)),
    "TowerB": (12.0, 12.0, 21 * STOREY, (6.5, -5.0), (2 * PITCH, 2 * PITCH), (1, 1)),
    "Sky": (16.0, 16.0, 38 * STOREY, (6.0, 5.0), (3 * PITCH, 3 * PITCH), (0, 0)),
}


def set_tree(name, region, extra_cut=None):
    """One building set: the building repeated on its grid, minus the holes (and `extra_cut`), inside `region`
    (a (x, z) extent), under the set's two growth clips `<name>Ring` and `<name>Rise`."""
    w, d, h, (ox, oz), (px, pz), (bi, bk) = SETS[name]
    cx, cz = block_centre(bi, bk)
    shop = CANVAS if name == "Low" else None
    crown = GLOW if name in ("TowerA", "TowerB", "Sky") else None
    b = building(w, d, h, shop=shop, crown=crown, floor=STOREY if name in ("Low", "Mid") else STOREY * 2)
    grid = T([cx + ox, 0.0, cz + oz], K.repeat([px, 0.0, pz], 0, b))
    cuts = holes() + ([extra_cut] if extra_cut is not None else [])
    body = K.D(grid, U(*cuts))
    ring = T([HX, 0.0, 2.0], box([0.0, 500.0, 0.0], name=f"{name}Ring"))
    rise = T([0.0, -2.0, 0.0], box([2000.0, 0.5, 2000.0], name=f"{name}Rise"))
    return K.I(body, zone(region), ring, rise)


def rise_size(height):
    """The `<name>Rise` box's half height that reveals a set up to `height` metres (the box is centred 2 m below the
    ground, so its closed state, 0.5, lies wholly underground: a zero-height box would still cut a slice at y = 0)."""
    return height + 2.0 if height > 0 else 0.5


# ---- the ground: roads, pavements, crossings -----------------------------------------------------------------------
def pavements(region):
    """Every block raised 12 cm (pavement and lot): its edge is the kerb, drawn as a line (5 nodes + holes)."""
    cx, cz = block_centre(0, 0)
    slab = T([cx, 0.06, cz], K.repeat([PITCH, 0.0, PITCH], 0, box([PITCH / 2 - ROAD_HALF + 1.5, 0.06, PITCH / 2 - ROAD_HALF + 1.5])))
    ring = T([HX, 0.0, 2.0], box([0.0, 5.0, 0.0], name="kerbsRing"))
    return K.I(K.D(slab, zone(NEIGH)), zone(region), ring)


def markings(region):
    """Lane dashes on every road, crossings at every junction (about 16 nodes)."""
    dash_x = T([HX, 0.0, MAIN_Z], K.repeat([5.0, 0.0, PITCH], 0, box([1.1, 0.012, 0.07])))
    dash_z = T([XI, 0.0, MAIN_Z], K.repeat([PITCH, 0.0, 5.0], 0, box([0.07, 0.012, 1.1])))
    # keep dashes off the junctions: only where the other road is not
    junction = T([XI, 0.0, MAIN_Z], K.repeat([PITCH, 0.0, PITCH], 0, box([ROAD_HALF + 1.0, 1.0, ROAD_HALF + 1.0])))
    stripes_x = K.I(T([XI, 0.0, MAIN_Z], K.repeat([PITCH, 0.0, 0.9], 0, box([0.5, 0.012, 0.22]))),
                    T([XI, 0.0, MAIN_Z], K.repeat([PITCH, 0.0, PITCH], 0, K.mirror([1, 0, 0], T([ROAD_HALF + 1.2, 0, 0], box([1.0, 0.1, 4.4]))))))
    stripes_z = K.I(T([XI, 0.0, MAIN_Z], K.repeat([0.9, 0.0, PITCH], 0, box([0.22, 0.012, 0.5]))),
                    T([XI, 0.0, MAIN_Z], K.repeat([PITCH, 0.0, PITCH], 0, K.mirror([0, 0, 1], T([0, 0, ROAD_HALF + 1.2], box([4.4, 0.1, 1.0]))))))
    lines = U(K.D(U(dash_x, dash_z), junction), stripes_x, stripes_z)
    ring = T([HX, 0.0, 2.0], box([0.0, 5.0, 0.0], name="marksRing"))
    return K.I(K.D(lines, zone(NEIGH)), zone(region), ring)


def street_lights(region):
    """Lamps on both pavements of every road, every 20 m (about 20 nodes)."""
    lamp = P4.street_lamp(5.0, 1.3)
    on_x = T([HX + 10.0, 0.0, MAIN_Z], K.repeat([20.0, 0.0, PITCH], 0, K.mirror([0, 0, 1], T([0.0, 0.0, -ROAD_HALF + 0.6], lamp))))
    on_z = T([XI, 0.0, MAIN_Z + 10.0], K.repeat([PITCH, 0.0, 20.0], 0, K.mirror([1, 0, 0], T([-ROAD_HALF + 0.6, 0.0, 0.0], R([0, 90, 0], lamp)))))
    clear = T([XI, 0.0, MAIN_Z], K.repeat([PITCH, 0.0, PITCH], 0, box([ROAD_HALF + 0.5, 10.0, ROAD_HALF + 0.5])))
    ring = T([HX, 0.0, 2.0], box([0.0, 50.0, 0.0], name="lightsRing"))
    return K.I(K.D(U(on_x, on_z), U(zone(NEIGH), clear)), zone(region), ring)


# ---- traffic -----------------------------------------------------------------------------------------------------------
def traffic(axis, region, gap=24.0, name="traffic", cut=None):
    """Two lanes of cars on every road along `axis` ('x' or 'z'), one stream each way: each stream is a named
    translate (`<name>Fwd`, `<name>Back`) the film keys at a steady speed. Clipped to `region`, minus the
    neighbourhood's houses' gardens (the main street keeps its own lanes) and `cut` (about 50 nodes)."""
    car = P4.car()
    if axis == "x":
        fwd = T([0.0, 0.0, 0.0], T([HX, 0.0, MAIN_Z - LANE], K.repeat([gap, 0.0, PITCH], 0, R([0, 90, 0], car))), name=f"{name}Fwd")
        back = T([0.0, 0.0, 0.0], T([HX + gap / 2, 0.0, MAIN_Z + LANE], K.repeat([gap, 0.0, PITCH], 0, R([0, -90, 0], car))), name=f"{name}Back")
    else:
        fwd = T([0.0, 0.0, 0.0], T([XI + LANE, 0.0, 0.0], K.repeat([PITCH, 0.0, gap], 0, R([0, 180, 0], car))), name=f"{name}Fwd")
        back = T([0.0, 0.0, 0.0], T([XI - LANE, 0.0, gap / 2], K.repeat([PITCH, 0.0, gap], 0, car)), name=f"{name}Back")
    ring = T([HX, 0.0, 2.0], box([0.0, 20.0, 0.0], name=f"{name}Ring"))
    cuts = [X(((-92.8, 95.2), (-1.0, 10.0), (-22.0, MAIN_Z - 4.4))), X(((-92.8, 95.2), (-1.0, 10.0), (MAIN_Z + 4.4, 46.0)))]
    if axis == "z":
        cuts = [zone(NEIGH)]
    if cut is not None:
        cuts.append(cut)
    return K.I(K.D(U(fwd, back), U(*cuts)), zone(region, (-1.0, 10.0)), ring)


# ---- the set pieces at the downtown intersection -----------------------------------------------------------------------
def intersection():
    """The four traffic lights on its corners, a bus shelter, kerbside planters (about 70 nodes). The signal facing
    the northbound lane (his) is `sigN*`; the cross street's `sigE*`."""
    c = 6.4
    sig = []
    # NE, NW, SE, SW corners; each light's arm reaches over the road it controls
    sig.append(T([XI + c, 0.12, ZK + c], R([0, 180, 0], P4.traffic_light(3.6, 3.2))))      # SE corner: for the northbound lane
    sig.append(T([XI - c, 0.12, ZK - c], P4.traffic_light(3.6, 3.2)))                     # NW corner: southbound
    sig.append(T([XI - c, 0.12, ZK + c], R([0, 90, 0], P4.traffic_light(3.6, 3.2))))       # SW corner: eastbound
    sig.append(T([XI + c, 0.12, ZK - c], R([0, -90, 0], P4.traffic_light(3.6, 3.2))))      # NE corner: westbound
    shelter = T([XI + 12.0, 0.12, ZK + 7.4], R([0, 180, 0], P4.bus_shelter(3.2)))
    planters = U(T([XI + 9.0, 0.12, ZK + 13.0], P4.planter()), T([XI + 9.0, 0.12, ZK + 18.0], P4.planter()))
    return U(*sig, shelter, planters)


def signal_heads():
    """The lit lamps of the four signals as their own objects, so the film can key red / amber / green: surfaces
    CANVAS (red), CANVAS2 (amber), SCREEN (green); 'NS' (his: north- and southbound) and 'EW' (the cross street).
    Each head sits where traffic_light(3.6, 3.2) puts its housing, turned with its pole."""
    c = 6.4
    h, arm = 3.6, 3.2

    def heads(at, yaw):
        lamp = lambda dy, k: S(T([0.13, h - 0.5 + dy, arm - 0.1], R([0, 0, 90], K.cyl(0.085, 0.03))), k)   # noqa: E731
        return T(at, R([0, yaw, 0], U(lamp(0.27, CANVAS), lamp(0.0, CANVAS2), lamp(-0.27, SCREEN))))
    ns = U(heads([XI + c, 0.12, ZK + c], 180.0), heads([XI - c, 0.12, ZK - c], 0.0))
    ew = U(heads([XI - c, 0.12, ZK + c], 90.0), heads([XI + c, 0.12, ZK - c], -90.0))
    return ns, ew


def park():
    """The block south-west of the intersection (x -12.8..15.2, z -102..-74): a lawn, an east-west path and a
    north-south one, trees, two lamps, three benches facing the path (the lead's is `ParkBench`), a low hedge
    round it (about 80 nodes)."""
    cx, cz = HX, -88.0
    lawn = X(((cx - 14.0, cx + 14.0), (0.0, 0.14), (cz - 14.0, cz + 14.0)))
    paths = U(X(((cx - 14.0, cx + 14.0), (0.14, 0.16), (cz - 1.3, cz + 1.3))), X(((cx - 1.3, cx + 1.3), (0.14, 0.16), (cz - 14.0, cz + 14.0))))
    hedge = K.D(X(((cx - 13.8, cx + 13.8), (0.14, 0.75), (cz - 13.8, cz + 13.8))), X(((cx - 13.2, cx + 13.2), (0.0, 1.0), (cz - 13.2, cz + 13.2))),
                X(((cx - 1.5, cx + 1.5), (0.0, 1.0), (cz - 15.0, cz + 15.0))), X(((cx - 15.0, cx + 15.0), (0.0, 1.0), (cz - 1.5, cz + 1.5))))
    trees = U(*[K.place(K.round_tree(h), (cx + x, 0.14, cz + z)) for x, z, h in
                ((-8.0, -7.5, 6.5), (7.5, -8.0, 7.0), (-8.5, 7.0, 6.0), (8.0, 7.5, 6.8), (-3.5, -10.5, 5.5), (10.5, 3.0, 5.8))])
    lamps = U(K.place(P4.street_lamp(4.2, 0.4), (cx + 4.2, 0.14, cz + 2.1), 0.0), K.place(P4.street_lamp(4.2, 0.4), (cx - 4.6, 0.14, cz - 2.1), 180.0))
    return U(S(lawn, FLOOR), S(paths, ACCENT), S(hedge, ACCENT), trees, lamps)


BENCH_AT = (HX + 3.6, 0.14, -88.0 + 2.3)      # the lead's bench, facing the path (its front -Z)
BENCH_YAW = 180.0


def benches():
    return U(K.place(P4.park_bench(), BENCH_AT, BENCH_YAW),
             K.place(P4.park_bench(), (HX - 5.5, 0.14, -88.0 - 2.3), 0.0),
             K.place(P4.park_bench(), (HX + 2.3, 0.14, -88.0 - 6.5), -90.0))


# the bar: the corner unit of the block south-east of the intersection
BAR = {"x0": XI + 6.0 + 0.4, "x1": XI + 6.0 + 11.0, "z0": ZK + 6.0 + 0.4, "z1": ZK + 6.0 + 8.6, "h": 3.6}


def bar_building():
    """The low-rise on the block south-east of the intersection: four storeys, its ground-floor corner unit the bar
    (big windows on its two street sides, a door on the corner), the rest of the ground floor dark shops (about 30
    nodes). The bar's interior is `bar_room()`."""
    cx, cz = HX + 40.0, -88.0
    b = BAR
    body = X(((cx - 13.0, cx + 13.0), (0.12, 4 * STOREY + 0.12), (cz - 13.0, cz + 13.0)))
    hollow = X(((b["x0"], b["x1"]), (0.12, b["h"] + 0.12), (b["z0"], b["z1"])))
    win_w = X(((b["x0"] - 0.6, b["x0"] + 0.2), (0.9, 3.0), (b["z0"] + 1.0, b["z1"] - 1.4)))
    win_n = X(((b["x0"] + 1.4, b["x1"] - 1.0), (0.9, 3.0), (b["z0"] - 0.6, b["z0"] + 0.2)))
    door = X(((b["x0"] - 0.6, b["x0"] + 0.2), (0.12, 2.3), (b["z1"] - 1.2, b["z1"] - 0.3)))
    shell = K.D(body, hollow, win_w, win_n, door)
    bands = K.I(T([0.0, STOREY + 0.12, 0.0], K.repeat([0.0, STOREY, 0.0], 0, box([13.05, 0.07, 13.05]))),
                X(((cx - 14, cx + 14), (2.0, 4 * STOREY - 1.0), (cz - 14, cz + 14))))
    bands = T([cx, 0.0, cz], T([-cx, 0.0, -cz], bands))
    awning = X(((b["x0"] - 1.0, b["x0"]), (3.3, 3.4), (b["z0"], b["z1"])))
    return U(S(shell, FILL), S(bands, CANVAS2), S(awning, ACCENT))


def ground_tiles(ext, pitch=0.6):
    return K.tiles(ext, pitch)


def objects():
    """[(name, tree, role, bmin, bmax, opts)] for the city's world (the generic sets and the street furniture; the
    set pieces' rooms and figures are added by the film)."""
    far = {"max_distance": 420.0}
    out = []
    reg_all = ((-420.0, 420.0), (-420.0, 300.0))
    inner_cut = zone(INNER)
    for name in ("Low", "Mid", "TowerA", "TowerB", "Sky"):
        w, d, h, *_ = SETS[name]
        role = {"Low": "furn2", "Mid": "furn", "TowerA": "wall", "TowerB": "wall", "Sky": "wall"}[name]
        out.append((f"cityIn{name}", set_tree(name, INNER), role, (INNER[0][0], -0.5, INNER[1][0]), (INNER[0][1], h + 1.5, INNER[1][1]),
                    dict(far, edge_pixels=1.5)))
        out.append((f"cityOut{name}", set_tree(name, reg_all, extra_cut=inner_cut), role, (-420.0, -0.5, -420.0), (420.0, h + 1.5, 300.0),
                    dict(far, edge_pixels=1.4)))
    out.append(("cityKerbs", pavements(reg_all), "wall", (-420.0, -0.2, -420.0), (420.0, 0.3, 300.0), dict(far, edge_pixels=1.3)))
    out.append(("cityMarks", markings(reg_all), "furn", (-420.0, -0.2, -420.0), (420.0, 0.3, 300.0), dict(far, edge_pixels=1.2)))
    out.append(("cityLamps", street_lights(reg_all), "furn", (-420.0, -0.2, -420.0), (420.0, 6.0, 300.0), dict(far, edge_pixels=1.3)))
    return out
