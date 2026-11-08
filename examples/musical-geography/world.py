"""Musical Geography: the land as data -- the layout of sea, shore, rolling hills, crowns and the path, in the
parameter names engineering's TileTerrain exposes (03-technical-plan.md §5). Values come from the owner-approved
r2 mock-up (04-art-direction.md §5, docs/prototypes/musical-geography/gate1-mockup.py) translated to engine axes:
y is up, the ground is x/z, north is -z, one unit is one tile pitch.

GEOGRAPHY is staged: build.py writes it into the scene only with --geography, once the engine reads it (a key the
engine does not read is a silent no-op, docs/visual-quality.md §7). Until then build.py --interim stands a plain
procedural slab grid on existing fields, so sky, fog, post and the tile program can be judged on the GPU.
"""
from __future__ import annotations

import math

# ---- the layout (04- §5, r2) ---------------------------------------------------------------------------------------
# The coast runs east-west; the sea is to the south (+z), the land swells inland to the north (-z).
SHORE = dict(
    baseZ=38.0,                                  # the mean shoreline, +z of the origin (mock-up: y = -38)
    wiggle=[(34.0, 95.0, 0.4), (16.0, 41.0, 1.7), (9.0, 17.0, 0.2)],   # (amplitude, wavelength/2pi, phase): bays
    ramp=22.0,                                   # tiles over which land rises out of the sea
)

GEOGRAPHY = {
    "lattice": {"spacing": 1.0, "tileSize": 0.915, "thickness": 0.30, "windowRadius": 260.0, "lodDistance": 140.0},
    "seed": 1,
    "sea": {"level": -0.4, "flatten": 1.0, "shore": {"distance": 9.0, "ramp": SHORE["ramp"]}},
    # The rolling hills are the main landform: broad uplands on a 260-tile wavelength that swell with distance
    # inland (rise over 240 tiles), secondary hills on 85, domain-warped by 50. maxStep holds the 99th-percentile
    # neighbour step at the slab's thickness (04- §4: no see-through cracks, no staircase).
    "hills": {"wavelength": 260.0, "height": 30.0, "warp": 50.0, "octaves": 3, "roughness": 0.3,
              "secondary": {"wavelength": 85.0, "height": 9.0},
              "inlandRise": 240.0, "base": 1.5, "maxStep": 0.30},
    # Crowns: the flattest high tops, where the aliens gather and the pads light at night (04- §6).
    "crowns": {"flatThreshold": 0.25, "flatten": 0.6, "minHeight": 20.0},
    # The path: a one-tile causeway, raised 0.3, climbing from the shore into the hills on its own meander.
    "path": {"width": 1.0, "lift": 0.3, "route": []},          # route filled by path_route()
    # How far the hill and sea parameters wander along the endless world (biomes, 04- §5).
    "region": {"scale": 1600.0, "variation": 0.25},
    # Coastal flats (04- §5): shallow steps no taller than a slab, every third flooded. Engineering's formation set
    # has no terrace term (the plan dropped stepped terms with the cliffs); this asks for one only if CP-A misses it.
    "flats": {"step": 0.32, "rise": 0.05, "floodEvery": 3, "belt": [15.0, 90.0]},
}


def shore_z(x: float) -> float:
    """The shoreline (z) at x: positive z is seaward."""
    return SHORE["baseZ"] - sum(a * math.sin(x / w + p) for a, w, p in SHORE["wiggle"])


def path_x(north: float) -> float:
    """The path's centreline x at a distance `north` (tiles, = -z) from the origin (mock-up: path_x(y))."""
    return -95.0 + 38.0 * math.sin(north / 74.0 + 0.9) + 9.0 * math.sin(north / 31.0)


def path_route(start=-20.0, end=330.0, step=10.0):
    """The path's control points, [x, z], from the shore up into the hills."""
    pts = []
    n = start
    while n <= end:
        pts.append([round(path_x(n), 2), round(-n, 2)])
        n += step
    return pts


GEOGRAPHY["path"]["route"] = path_route()


# ---- interim terrain (existing fields only; throwaway until TileTerrain lands) --------------------------------------
def interim_nodes(grid=256):
    """A slab grid on today's procedural path with two smooth noise swells for hills. Not the formations (no sea, no
    shore, no path): enough to judge the sky, the fog, the post, the slab and the tile program in each state."""
    def field(name, f):
        return {"name": name, "kind": "field", "position": [0, 0, 0],
                "field": dict({"strength": 1.0, "falloff": {"kind": "none"}, "position": [0, 0, 0]}, **f)}
    t = GEOGRAPHY["lattice"]
    return [
        field("mgUplands", {"kind": "noise", "frequency": 1.0 / 260.0, "speed": 0.0, "seed": 3, "strength": 30.0}),
        field("mgHills", {"kind": "noise", "frequency": 1.0 / 85.0, "speed": 0.0, "seed": 9, "strength": 9.0}),
        {"name": "mgTiles", "kind": "procedural", "position": [0, 0, 0], "procedural": {
            "source": {"kind": "box", "size": [t["tileSize"], t["thickness"], t["tileSize"]]},
            "distribution": {"kind": "grid", "gridCount": [grid, 1, grid], "gridSpacing": [1, 1, 1]},
            "material": {"baseColor": [1, 1, 1], "roughness": 0.62, "metallic": 0.0, "program": "mgTile"},
            "lod": {"cull": True, "maxDistance": 0, "count": 1},
            "effectors": [
                {"field": "mgUplands", "op": "positionOffset", "blend": "add", "strength": 1.0, "axis": [0, 1, 0]},
                {"field": "mgHills", "op": "positionOffset", "blend": "add", "strength": 1.0, "axis": [0, 1, 0]}]}},
    ]
