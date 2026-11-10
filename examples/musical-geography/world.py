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
    "continentWavelength": 1100.0, "landThreshold": 0.45, "shoreBand": 0.035, "inlandBand": 0.07,   # land2: 0.15
    "seaLevel": 0.0, "seaDepth": 0.25, "shoreRise": 0.5,
    "hillWavelength": 300.0, "hillHeight": 30.0, "hillOctaves": 2, "hillWarp": 70.0, "warpWavelength": 420.0,
    "hillLow": 0.12, "hillHigh": 0.82,   # land2: 0.22 (more of the land stands up)
    "crownThreshold": 0.88, "crownFlatten": 0.7,
    "regionWavelength": 2400.0, "regionVariation": 0.25,
    "pathWidth": 2.0, "pathLift": 0.3,
    # CP-D r2 land (ADR-1965; the owner: "terrain needs more variety: hills, valleys, rivers and ponds"). A distinct
    # landform on every leg of the flight, at its song time (04- §13, CP-D r2 land; the map is cpd-r2-land-map):
    #   0-20 s   the bay and the west-shore hills, a stream falling off them into the bay   (westStream)
    #   56-80 s  the lit path through rolling hills                                           (path)
    #   83-92 s  sunset ponds in the lowland under the crowns                                 (ponds low1, low2)
    #   96-108 s a dry valley the flight runs down through the sweep                          (valleys sweep)
    #   108-128  the river valley, at dusk                                                    (rivers[0] "river")
    #   ~140 s   a tarn on the plateau where the night comes                                  (ponds tarn)
    #   145-160  a chain of ponds along the night coast                                       (ponds coast1-3)
    #   160-172  the inlet's sea
    #   185-210  a stream down the peninsula at dawn, into the northern sea                   (dawnStream)
    "rivers": [
        # CP-A (r2's valley): a river meandering north from the bay's north shore, in a smooth valley with no walls
        {"name": "river", "points": [[270.0, -530], [311.3, -592], [324.5, -654], [300.7, -716], [255.9, -778],
                                     [220.8, -840], [219.1, -902], [252.0, -964], [297.2, -1026], [323.9, -1088],
                                     [313.9, -1150], [274.1, -1212], [231.5, -1274], [215.1, -1336], [236.0, -1398],
                                     [280.0, -1460]], "width": 16.0, "valleyWidth": 240.0},
        {"name": "westStream", "points": [[-770, -95], [-700, -65], [-625, -45], [-545, -28], [-455, -15]],
         "width": 8.0, "valleyWidth": 44.0, "bedDepth": 1.2, "sourceLevel": "auto"},
        {"name": "dawnStream", "points": [[1592, -1700], [1590, -1820], [1556, -1950], [1530, -2060], [1486, -2200],
                                          [1440, -2330]], "width": 10.0, "valleyWidth": 54.0, "bedDepth": 1.5,
         "sourceLevel": "auto"},
    ],
    "ponds": [
        # clouds2: every pond sits on its valley's FLOOR, on the flight line. Auto-levelled on a valley wall (land2 put
        # them there), a pond's flat water stood up the slope as one glowing slab -- the "white block" at 148 s was a
        # coast pond, not contour bunching (measured: gone with the ponds removed, cpd-r2-clouds2-148s-*).
        {"name": "low1", "centre": [-316, -1043], "radius": 30, "basinWidth": 34, "level": "auto", "shoreNoise": 0.35},
        {"name": "low2", "centre": [-218, -1166], "radius": 26, "basinWidth": 30, "level": "auto", "shoreNoise": 0.4},
    ],
    # land3 (ADR-1992; the owner: "no long endless horizons"): raised land where the route needs a wall or the sea
    # needs a far side -- headlands across the bay's mouth, a spit and a north-shore ridge inside the bay, islands
    # beside the 30-50 s sea legs, a ridge north of the sweep, a ridge behind the plateau tarn (its core makes a
    # seaward headland), ridgelines down both sides of the dawn peninsula, an island in the inlet. Metres, unscaled.
    "ridges": [   # land4: noise 0.5 on every ridge (ADR-1994: the hills' own fBm); the spit and the north shore 42 m
        {"name": "westHead", "points": [[-470, 120], [-330, 260], [-180, 330]], "height": 45, "width": 160, "noise": 0.5},
        {"name": "eastHead", "points": [[620, 80], [480, 240], [320, 330]], "height": 45, "width": 160, "noise": 0.5},
        {"name": "northShore", "points": [[-330, -560], [-200, -600], [-90, -610]], "height": 42, "width": 120, "noise": 0.5},
        {"name": "baySpit", "points": [[-470, -420], [-330, -445], [-200, -430], [-90, -395]], "height": 42, "width": 110, "noise": 0.5},
        {"name": "sweepNorth", "points": [[-360, -1210], [-220, -1280], [-70, -1260]], "height": 40, "width": 150, "noise": 0.5},
        {"name": "tarnRidge", "points": [[250, -1700], [420, -1740], [560, -1745], [700, -1755]], "height": 36, "width": 120, "noise": 0.5},
        {"name": "capeEast", "points": [[1640, -1550], [1680, -1750], [1660, -1950], [1620, -2150]], "height": 48, "width": 150, "noise": 0.5},
        {"name": "capeWest", "points": [[1430, -1700], [1440, -1900], [1420, -2080]], "height": 32, "width": 120, "noise": 0.5},
    ],
    # land4 (owner: "some of these lumps look odd"): each island composed of overlapping rises -- a peak, a saddle and a
    # long low tail, elongated across the 30-35 s view, a steep short-edged side and a gentle wide one -- lower and
    # wider, sizes varied (a big three-part island, a knoll with a shoulder, a small stack, a broad low north isle, the
    # inlet's long island), all with the hills' own noise (ADR-1994) and the soft coast (ADR-1993)
    "rises": [
        {"name": "bayPeak", "centre": [205, -130], "radius": 30, "height": 38, "edge": 46, "noise": 0.6},
        {"name": "baySaddle", "centre": [228, -200], "radius": 36, "height": 28, "edge": 70, "noise": 0.6},
        {"name": "bayTail", "centre": [262, -268], "radius": 32, "height": 17, "edge": 85, "noise": 0.6},
        {"name": "southKnoll", "centre": [100, -62], "radius": 26, "height": 31, "edge": 44, "noise": 0.6},
        {"name": "southShoulder", "centre": [70, 0], "radius": 36, "height": 19, "edge": 80, "noise": 0.6},
        {"name": "stack", "centre": [-20, -330], "radius": 14, "height": 20, "edge": 26, "noise": 0.4},
        {"name": "northIsle", "centre": [160, -450], "radius": 40, "height": 32, "edge": 65, "noise": 0.6},
        {"name": "inletIsle", "centre": [1160, -1610], "radius": 46, "height": 18, "edge": 80, "noise": 0.6},
    ],
    # land2: the flight runs DOWN valleys, so their walls frame it and cut the horizon (cpd-r2-land2-*): a dale under
    # the path, the sweep (now from the lowland ponds through to the river), a vale along the night coast, and the
    # cape's vale the dawn stream runs in. The sweep runs on into the river's valley: ending at (13, -1108), it left the
    # flight climbing a 60 m flank head-on (a wall filling the frame, 104-106 s). Each follows the flight line (the scratch mirror's track), 150 m wall to wall.
    "valleys": [
        {"name": "pathVale", "points": [[35, -615], [-30, -680], [-114, -727], [-200, -770], [-284, -819], [-360, -876]],
         "valleyWidth": 150.0, "depthFraction": 0.85},
        {"name": "sweep", "points": [[-372, -905], [-352, -975], [-316, -1043], [-275, -1109], [-218, -1166], [-139, -1175], [-61, -1145], [13, -1108], [74, -1078], [150, -1062], [235, -1072]],
         "valleyWidth": 150.0, "depthFraction": 0.85},
        # cpd-r3 (owner: the night vale "feels too claustrophobic ... just ends with a giant mound"): a wider floor
        # (150 -> 190 m), the tarn ridge lowered 38 -> 36 and set back, and it runs on out onto the inlet instead of ending against the hill (v2 target 0.6-0.75)
        {"name": "coastVale", "points": [[221, -1313], [232, -1396], [251, -1493], [320, -1571], [422, -1611], [531, -1636],
                                         [643, -1649], [760, -1640], [880, -1620], [990, -1605]],
         "valleyWidth": 190.0, "depthFraction": 0.85},
        {"name": "capeVale", "points": [[1290, -1493], [1396, -1480], [1481, -1541], [1533, -1633], [1561, -1733], [1553, -1835], [1526, -1930], [1499, -2023]],
         "valleyWidth": 150.0, "depthFraction": 0.85},
    ],
    # taller land so the top of the colour ramp (orange, yellow; 20-30 m) shows on the crowns (04- §3)
    "heightScale": 2.2,   # land2 (owner: "no long endless horizons"): 1.25 -> 2.2, hills to ~65 m
    # from the bay's north shore up into the north-west hills (terrain_mirror.py --map shows the layout)
    "path": [[40, -610], [-40, -690], [-150, -745], [-260, -800], [-360, -880], [-470, -940], [-590, -975],
             [-720, -1010], [-830, -1090]],
}

# The rings (ADR-1976 B), as engineering's fixture sets them (eng/make_ground.py, fd110d88): 1x slabs with the 8.5%
# gap near; 2x and 4x blocks beyond with NO geometric gap (box = cell), each the mean of its children, painting the fine
# lattice with tileBorder's subdivisions (look.tile_program); the dark underlay under the fine ring only, 4 m blocks
# 2.2 below, so a block's mean never pokes through a slope.
FINE_VIEW = 270.0   # 4 m tiles (owner, 2026-11-09): the fine ring reaches 270 m so the 8/16 m rings sit in the haze
RINGS = [
    # name, cellSize, viewDistance, align, hole (finer ring), thickness, castsShadow, gap (tileSize / cell)
    ("mgTiles1", 1.0, FINE_VIEW, 4, None, 0.30, True, 0.915),
    ("mgTiles2", 2.0, FINE_VIEW * 2, 2, {"cellSize": 1.0, "viewDistance": FINE_VIEW, "align": 4}, 0.36, True, 1.0),
    ("mgTiles4", 4.0, FINE_VIEW * 4, 4, {"cellSize": 2.0, "viewDistance": FINE_VIEW * 2, "align": 2}, 0.72, False, 1.0),
]
RING_MORPH = {"mgTiles1": {"width": 64, "ratio": 2}, "mgTiles2": {"width": 128, "ratio": 2},
              "mgTiles4": {"width": 256, "ratio": 4, "pitch": 16}}   # eng's values (02-requests, ADR-1995)
UNDERLAY = dict(cellSize=4.0, viewDistance=FINE_VIEW, y=-2.2, thickness=3.0)

# Kept for reference: the flats of the mock-up (04- §5). Engineering's set has no terrace term; ask only if CP-A
# shows the coast needs one.
FLATS_WISH = {"step": 0.32, "rise": 0.05, "floodEvery": 3}


def ring_nodes(program_for_cell, under_rgb, under_program=None, side_px=0.0, scale=1.0, far_rgb=(0.6, 0.45, 0.7)):
    """`scale` multiplies the tile (pitch, size, thickness, skirt) -- the land and the ring distances stay in metres."""
    S = scale
    nodes = []
    for name, cell, view, align, hole, thick, shadow, gap in RINGS:
        tiles = dict(TILES, pathWidth=max(TILES["pathWidth"], 1.5 * S))
        gen = {"name": "tiles", "version": 1, "cellSize": cell * S, "viewDistance": view, "align": align,
               # ADR-1995: over each ring's last quarter its tiles ease into the next ring's cells (no swap pop)
               "morph": dict(RING_MORPH[name]),
               "finePitch": S if cell > 1.0 else 0.0, "tiles": tiles,
               "skirt": 3 * S}   # 32a7e743: closes the cracks under thin slabs at height steps (rings, streaks)
        if hole:
            gen["hole"] = dict(hole, cellSize=hole["cellSize"] * S)
        proc = {"source": {"kind": "box", "size": [cell * S * gap, thick * S, cell * S * gap]},
                "distribution": {"kind": "generator", "generator": gen},
                "material": {"baseColor": [1, 1, 1], "roughness": 0.62, "metallic": 0.0,
                             "program": program_for_cell(cell)},
                "lod": {"cull": True}, "slabs": True}
        if side_px:
            proc["slabSidePixels"] = side_px
        if not shadow:
            proc["castsShadow"] = False
        nodes.append({"name": name, "kind": "procedural", "position": [0, 0, 0], "procedural": proc})
    # ADR-1962: one far ring of 64 m cells out to 4.5 km, where the fog is opaque, so the ground reaches the horizon.
    # Plain material (it is all fog out there); its hole is the coarsest ring's window.
    far_cell, coarse = 64.0, RINGS[-1]
    nodes.append({"name": "mgTiles64", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "box", "size": [far_cell, 6.0, far_cell]}, "slabs": True, "castsShadow": False,
        "lod": {"cull": True}, "material": {"baseColor": list(far_rgb), "roughness": 0.8},
        "distribution": {"kind": "generator", "generator": {
            "name": "tiles", "version": 1, "cellSize": far_cell, "viewDistance": 4480.0, "align": 4,
            "finePitch": coarse[1] * S, "skirt": 40.0,
            "hole": {"cellSize": coarse[1] * S, "viewDistance": coarse[2], "align": coarse[3]},
            "tiles": dict(TILES, pathWidth=max(TILES["pathWidth"], 1.5 * S))}}}})
    u = UNDERLAY
    gen = {"name": "tiles", "version": 1, "cellSize": u["cellSize"] * S, "viewDistance": u["viewDistance"], "align": 1,
           "finePitch": S, "tiles": dict(TILES, pathWidth=max(TILES["pathWidth"], 1.5 * S)),
           "childrenMin": True}   # ADR-1953: stands at the lowest fine tile, so moving troughs never uncover it
    nodes.append({"name": "mgUnderlay", "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "box", "size": [u["cellSize"] * S, u["thickness"] * S, u["cellSize"] * S]},
        "distribution": {"kind": "generator", "generator": gen},
        "distributionTransform": {"position": [0, u["y"] * S, 0]},
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
