"""Musical Geography: the land as data -- the layout of sea, shore, rolling hills, crowns and the path, in the
parameter names engineering's TileTerrain exposes (03-technical-plan.md §5). Values come from the owner-approved
r2 mock-up (04-art-direction.md §5, docs/prototypes/musical-geography/gate1-mockup.py) translated to engine axes:
y is up, the ground is x/z, north is -z, one unit is one tile pitch.

The ground is engineering's "tiles" generator (ADR-1950/1959/1976): TILES is its TileTerrainSpec, RINGS its rings.
interim_nodes() is the pre-generator stand-in (existing noise fields), kept only to compare against.
"""
from __future__ import annotations


# ---- the ground: TileTerrainSpec (ADR-1950), the "tiles" object of a "tiles" generator ------------------------------
# Engineering's formation set (sea, shore, rolling hills, crowns, path; a region field) with the art values of 04- §5:
# hills are the main landform, broad (wavelength 220) and tall (22 at full height), rising over the inland band; crowns
# flatten the tops for the pads; the path climbs from the shore into the hills. The shoreline is the continent field's
# threshold (engineering's design), not the mock-up's hand-drawn bays; seed and threshold are chosen so the opening
# low pass has sea in front and hills behind (see CAMERAS in build.py).
TILES = {
    "seed": 3,
    "continentWavelength": 1100.0, "landThreshold": 0.45, "shoreBand": 0.035, "inlandBand": 0.15,
    "seaLevel": 0.0, "seaDepth": 0.25, "shoreRise": 0.5,
    "hillWavelength": 300.0, "hillHeight": 30.0, "hillOctaves": 2, "hillWarp": 70.0, "warpWavelength": 420.0,
    "hillLow": 0.22, "hillHigh": 0.82,
    "crownThreshold": 0.88, "crownFlatten": 0.7,
    "regionWavelength": 2400.0, "regionVariation": 0.25,
    "pathWidth": 2.0, "pathLift": 0.3,
    # CP-A (r2's valley): a river meandering north from the bay's north shore, in a smooth valley with no walls
    "river": [[270.0, -530], [311.3, -592], [324.5, -654], [300.7, -716], [255.9, -778], [220.8, -840], [219.1, -902], [252.0, -964], [297.2, -1026], [323.9, -1088], [313.9, -1150], [274.1, -1212], [231.5, -1274], [215.1, -1336], [236.0, -1398], [280.0, -1460]],
    "riverWidth": 8.0, "valleyWidth": 240.0,
    # taller land so the top of the colour ramp (orange, yellow; 20-30 m) shows on the crowns (04- §3)
    "heightScale": 1.25,
    # from the bay's north shore up into the north-west hills (terrain_mirror.py --map shows the layout)
    "path": [[40, -610], [-40, -690], [-150, -745], [-260, -800], [-360, -880], [-470, -940], [-590, -975],
             [-720, -1010], [-830, -1090]],
}

# The rings (ADR-1976 B), as engineering's fixture sets them (eng/make_ground.py, fd110d88): 1x slabs with the 8.5%
# gap near; 2x and 4x blocks beyond with NO geometric gap (box = cell), each the mean of its children, painting the fine
# lattice with tileBorder's subdivisions (look.tile_program); the dark underlay under the fine ring only, 4 m blocks
# 2.2 below, so a block's mean never pokes through a slope.
FINE_VIEW = 120.0   # 160 -> 120 (engineering, CP-A r4): the composed reveal fits 33.3 ms
RINGS = [
    # name, cellSize, viewDistance, align, hole (finer ring), thickness, castsShadow, gap (tileSize / cell)
    ("mgTiles1", 1.0, FINE_VIEW, 4, None, 0.30, True, 0.915),
    ("mgTiles2", 2.0, FINE_VIEW * 2, 2, {"cellSize": 1.0, "viewDistance": FINE_VIEW, "align": 4}, 0.36, True, 1.0),
    ("mgTiles4", 4.0, FINE_VIEW * 4, 1, {"cellSize": 2.0, "viewDistance": FINE_VIEW * 2, "align": 2}, 0.72, False, 1.0),
]
UNDERLAY = dict(cellSize=4.0, viewDistance=FINE_VIEW, y=-2.2, thickness=3.0)

# Kept for reference: the flats of the mock-up (04- §5). Engineering's set has no terrace term; ask only if CP-A
# shows the coast needs one.
FLATS_WISH = {"step": 0.32, "rise": 0.05, "floodEvery": 3}


def ring_nodes(program_for_cell, under_rgb, under_program=None):
    nodes = []
    for name, cell, view, align, hole, thick, shadow, gap in RINGS:
        gen = {"name": "tiles", "version": 1, "cellSize": cell, "viewDistance": view, "align": align,
               "finePitch": 1.0 if cell > 1.0 else 0.0, "tiles": TILES,
               "skirt": 3}   # 32a7e743: closes the cracks under thin slabs at height steps (rings, streaks)
        if hole:
            gen["hole"] = hole
        proc = {"source": {"kind": "box", "size": [cell * gap, thick, cell * gap]},
                "distribution": {"kind": "generator", "generator": gen},
                "material": {"baseColor": [1, 1, 1], "roughness": 0.62, "metallic": 0.0,
                             "program": program_for_cell(cell)},
                "lod": {"cull": True}, "slabs": True}
        if not shadow:
            proc["castsShadow"] = False
        nodes.append({"name": name, "kind": "procedural", "position": [0, 0, 0], "procedural": proc})
    u = UNDERLAY
    gen = {"name": "tiles", "version": 1, "cellSize": u["cellSize"], "viewDistance": u["viewDistance"], "align": 1,
           "finePitch": 1.0, "tiles": TILES}
    nodes.append({"name": "mgUnderlay", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "box", "size": [u["cellSize"], u["thickness"], u["cellSize"]]},
        "distribution": {"kind": "generator", "generator": gen},
        "distributionTransform": {"position": [0, u["y"], 0]},
        "material": dict({"baseColor": list(under_rgb), "roughness": 0.9, "metallic": 0.0},
                         **({"program": under_program} if under_program else {})),
        "lod": {"cull": True}, "slabs": True, "castsShadow": False}})
    return nodes


# ---- interim terrain (existing fields only; throwaway until TileTerrain lands) --------------------------------------
def interim_nodes(grid=256):
    """A slab grid on today's procedural path with two smooth noise swells for hills. Not the formations (no sea, no
    shore, no path): enough to judge the sky, the fog, the post, the slab and the tile program in each state."""
    def field(name, f):
        return {"name": name, "kind": "field", "position": [0, 0, 0],
                "field": dict({"strength": 1.0, "falloff": {"kind": "none"}, "position": [0, 0, 0]}, **f)}
    t = {"tileSize": 0.915, "thickness": 0.30}
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
