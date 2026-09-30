"""The liminal architectural vocabulary (ADR-1040): plain rooms, corridors, doorways, stairways, landings
and platforms as SDF JSON for AV Gen's `sdf` composition node, plus the screw that repeats a cell forever
and the warp that lets it breathe.

Import it from a generator (see tools/make_liminal_example.py):

    import sys; sys.path.insert(0, "tools")
    from liminal_sdf import *

Conventions: metres, +Y up. A box is given by its EXTENTS ((x0, x1), (y0, y1), (z0, z1)), which is how
architecture is drawn; every helper returns a plain dict (an SDF node) that composes with the others.
Name a node (`name=`) and every one of its fields becomes a parameter the art direction can key or route:
`sdf/<object>/node/<name>/<field>` (e.g. `sdf/world/node/breath/amount`). Names must be unique per tree.

Walls are solids and openings are cut out of them with `difference`; nothing here is ornamented.

Repetition rule (the screw): only the point's own cell is evaluated, so keep a cell's content inside its
slab (|dot(p, T)| <= |T|^2 / 2 for a translation screw), and keep the seam guard on (`seam` > 0).
"""

from __future__ import annotations

import math
from typing import Iterable, Optional, Sequence

Vec3 = Sequence[float]
Extents = Sequence[Sequence[float]]  # ((x0, x1), (y0, y1), (z0, z1))


def _named(node: dict, name: Optional[str]) -> dict:
    if name:
        node["name"] = name
    return node


# ---- primitives and operators -------------------------------------------------------------------

def box(half: Vec3, name: Optional[str] = None) -> dict:
    return _named({"kind": "box", "size": [float(v) for v in half]}, name)


def translate(t: Vec3, child: dict, name: Optional[str] = None) -> dict:
    return _named({"kind": "translate", "translation": [float(v) for v in t], "children": [child]}, name)


def rotate(degrees: Vec3, child: dict, name: Optional[str] = None) -> dict:
    return _named({"kind": "rotate", "rotation": [float(v) for v in degrees], "children": [child]}, name)


def union(*children: dict, name: Optional[str] = None) -> dict:
    kids = [c for c in children if c is not None]
    if len(kids) == 1 and not name:
        return kids[0]
    return _named({"kind": "union", "children": kids}, name)


def difference(solid: dict, *cuts: dict, name: Optional[str] = None) -> dict:
    kids = [solid] + [c for c in cuts if c is not None]
    if len(kids) == 1 and not name:
        return solid
    return _named({"kind": "difference", "children": kids}, name)


def morph(*children: dict, amount: float = 0.0, name: Optional[str] = None) -> dict:
    """A continuous blend between structures: `amount` 0 is the first, 1 the second, 1.5 halfway to the
    third. Drive it with a smooth timeline curve, never a step (the owner's §4)."""
    return _named({"kind": "morph", "amount": float(amount), "children": list(children)}, name)


def slab(ext: Extents, name: Optional[str] = None) -> dict:
    """An axis-aligned solid from its extents."""
    (x0, x1), (y0, y1), (z0, z1) = ext
    c = [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]
    h = [abs(x1 - x0) / 2, abs(y1 - y0) / 2, abs(z1 - z0) / 2]
    return translate(c, box(h, name=name))


# ---- architecture -------------------------------------------------------------------------------

def opening(ext: Extents, name: Optional[str] = None) -> dict:
    """A cut (a doorway, a window, an open end): a slab you pass to `difference`."""
    return slab(ext, name=name)


def doorway(wall: str, room: Extents, along: float = 0.0, width: float = 1.2, height: float = 2.2,
            sill: float = 0.0, wall_thickness: float = 0.3, depth: float = 1.0, name: Optional[str] = None) -> dict:
    """A doorway through one wall of a room given by its INTERIOR extents. `wall` is '+x', '-x', '+z' or
    '-z'; `along` is the door's centre along that wall (x for a z wall, z for an x wall) and `sill` its
    height above the room's floor.

    The cut reaches `depth` metres past both faces of the wall. That matters: a CSG difference is only a
    distance BOUND near the cut's faces, so an eye passing through a shallow cut reads the field as
    nearly touching a surface that is not there (the journey's collision guard would flinch). Deep inside
    the cut the field is accurate. The cut only removes this room's own shell, so depth is free."""
    (x0, x1), (y0, _), (z0, z1) = room
    t = wall_thickness + depth
    y = (y0 + sill, y0 + sill + height)
    if wall == "+x":
        ext = ((x1 - depth, x1 + t), y, (along - width / 2, along + width / 2))
    elif wall == "-x":
        ext = ((x0 - t, x0 + depth), y, (along - width / 2, along + width / 2))
    elif wall == "+z":
        ext = ((along - width / 2, along + width / 2), y, (z1 - depth, z1 + t))
    elif wall == "-z":
        ext = ((along - width / 2, along + width / 2), y, (z0 - t, z0 + depth))
    else:
        raise ValueError(f"doorway wall must be +x, -x, +z or -z, not {wall!r}")
    return opening(ext, name=name)


def shell(child: dict, thickness: float, name: Optional[str] = None) -> dict:
    """A hollow shell `thickness` thick centred on the child's surface (exact)."""
    return _named({"kind": "shell", "offset": float(thickness), "children": [child]}, name)


def room(interior: Extents, wall: float = 0.3, openings: Iterable[dict] = (), name: Optional[str] = None) -> dict:
    """A plain room: one box made hollow (`shell`) around its interior extents, walls `wall` thick, minus
    its openings (doorways, windows, open ends; cut a floor or ceiling away with an opening too).

    With a name, two parameters shape it: `node/<name>/size` (the box's half extents, which are the
    interior's half extents plus wall/2) and `node/<name>At/translation` (its centre). Key them to grow
    or shrink the room continuously -- to raise the ceiling with the floor fixed, key size.y and
    translation.y together (floor = translation.y - size.y + wall/2)."""
    (x0, x1), (y0, y1), (z0, z1) = interior
    center = [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]
    half = [(x1 - x0) / 2 + wall / 2, (y1 - y0) / 2 + wall / 2, (z1 - z0) / 2 + wall / 2]
    body = translate(center, shell(box(half, name=name), wall), name=(name + "At") if name else None)
    return difference(body, *openings)


def corridor(start: float, end: float, width: float = 3.0, height: float = 3.2, wall: float = 0.3,
             floor_y: float = 0.0, z: float = 0.0, open_start: bool = True, open_end: bool = True,
             openings: Iterable[dict] = (), name: Optional[str] = None) -> dict:
    """A corridor along +X from x = start to x = end, interior `width` x `height`, with open ends."""
    interior = ((start, end), (floor_y, floor_y + height), (z - width / 2, z + width / 2))
    cuts = list(openings)
    depth = 1.5  # deep cuts keep the field accurate where the eye passes through an open end (see doorway)
    if open_start:
        cuts.append(opening(((start - wall - depth, start + depth), (floor_y, floor_y + height),
                             (z - width / 2, z + width / 2))))
    if open_end:
        cuts.append(opening(((end - depth, end + wall + depth), (floor_y, floor_y + height),
                             (z - width / 2, z + width / 2))))
    return room(interior, wall=wall, openings=cuts, name=name)


def stairway(origin: Vec3, steps: int, run: float = 0.3, rise: float = 0.18, width: float = 1.4,
             direction: str = "+x", thickness: float = 0.0, name: Optional[str] = None) -> dict:
    """A straight flight climbing from `origin` (the foot of the first riser, centre of its width).
    `direction` '+x', '-x', '+z' or '-z'. thickness 0 is a block stair (solid to origin's height); > 0 a
    floating flight with a sloped underside. The top tread is at origin.y + steps * rise, reached
    steps * run along the direction."""
    s = _named({"kind": "stairs", "size": [float(run), float(rise), float(width) / 2], "count": int(steps),
                "height": float(thickness)}, name)
    yaw = {"+x": 0.0, "-z": 90.0, "-x": 180.0, "+z": -90.0}[direction]
    body = s if yaw == 0.0 else rotate([0.0, yaw, 0.0], s)
    return translate(origin, body)


def landing(center: Vec3, size_x: float, size_z: float, thickness: float = 0.3,
            name: Optional[str] = None) -> dict:
    """A flat platform whose TOP is at center.y."""
    cx, cy, cz = center
    return slab(((cx - size_x / 2, cx + size_x / 2), (cy - thickness, cy), (cz - size_z / 2, cz + size_z / 2)),
                name=name)


def platform(center: Vec3, size_x: float, size_z: float, thickness: float = 0.3, pier: float = 0.0,
             pier_depth: float = 12.0, name: Optional[str] = None) -> dict:
    """A landing, optionally on one plain square pier of side `pier` falling `pier_depth` into the void."""
    top = landing(center, size_x, size_z, thickness, name=name)
    if pier <= 0.0:
        return top
    cx, cy, cz = center
    support = slab(((cx - pier / 2, cx + pier / 2), (cy - thickness - pier_depth, cy - thickness),
                    (cz - pier / 2, cz + pier / 2)))
    return union(top, support)


# ---- the world: repetition and continuous deformation --------------------------------------------

def screw(cell: dict, translation: Vec3, count: int = 0, seam: float = 0.3, name: Optional[str] = None) -> dict:
    """Repeat `cell` forever. count 0: every cell is the last moved by `translation` (a corridor, or a
    climb when translation has a y). count n: a helix of n cells per turn about +Y, each turned by 360/n
    degrees and raised by translation[1] (a Penrose-like loop for n = 4). `seam` is the seam guard's
    margin. It MUST exceed the object's epsilon x maxDistance (0.0012 x 200 = 0.24 with sdf_node's
    defaults): the march counts d < epsilon * t as a hit, and at a seam the guard reports d = seam, so a
    smaller margin draws the seam planes as surfaces far away (they look like dense stripes). 0 disables
    the guard (then content must stay well inside its cell)."""
    return _named({"kind": "screw", "translation": [float(v) for v in translation], "count": int(count),
                   "offset": float(seam), "children": [cell]}, name)


def breathing(child: dict, amount: float = 0.1, frequency: float = 0.08, axes: Vec3 = (1.0, 0.0, 1.0),
              phase: Vec3 = (0.0, 0.0, 0.0), seed: int = 1, name: Optional[str] = "breath") -> dict:
    """A smooth domain warp: walls bow by up to `amount` metres over a wavelength of about 1/frequency.
    axes (1, 0, 1) keeps floors flat (props stay grounded). Drive `amount` with a spring route and the
    phase (`translation`) with an integrating route so the flow's speed, not its position, follows the
    music."""
    return _named({"kind": "warp", "amount": float(amount), "frequency": float(frequency),
                   "size": [float(v) for v in axes], "translation": [float(v) for v in phase], "seed": int(seed),
                   "children": [child]}, name)


def screw_apply(point: Vec3, translation: Vec3, count: int, k: int) -> list:
    """S^k(point): where cell k puts a cell-0 point (for placing props and lights in every cell)."""
    x, y, z = point
    if count <= 0:
        return [x + k * translation[0], y + k * translation[1], z + k * translation[2]]
    a = 2.0 * math.pi / count * (k % count)
    c, s = math.cos(a), math.sin(a)
    return [x * c - z * s, y + k * translation[1], x * s + z * c]


def sdf_node(name: str, tree: dict, material: dict, bounds: float = 2000.0, max_distance: float = 220.0,
             step_scale: float = 0.8, look: Optional[dict] = None) -> dict:
    """The composition node for a world tree, compiled, with sane march settings for large interiors."""
    return {
        "name": name, "kind": "sdf",
        "sdf": {
            "tree": {"root": tree}, "material": material, "renderMode": "raymarch",
            "boundsMin": [-bounds, -bounds / 4, -bounds], "boundsMax": [bounds, bounds / 4, bounds],
            "look": look or {"aoStrength": 0.6, "aoDistance": 1.5},
            "castShadows": False, "depthPrepass": False, "compile": True,
            "maxSteps": 192, "epsilon": 0.0012, "stepScale": step_scale, "normalEpsilon": 0.004,
            "maxDistance": max_distance,
        },
    }
