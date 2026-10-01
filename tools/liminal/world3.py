"""All You Got, art pass 3: adding worlds to a Film (pass 2's `add_world`, extended).

An object is (name, tree, role, bmin, bmax[, options]). Options beyond Film.sdf's: `own_line` (do not bind the
line colour), `figure` (a mannequin: its head, surface GLASS, glows faintly in the figure's colour and draws
brighter lines -- section 32 -- and its body lines are the figure role's).
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools"))

import kit as K  # noqa: E402
from film2 import default_surfaces  # noqa: E402

LINE_ROLE = {"wall": "wall", "furn": "furn", "furn2": "furn2", "figure": "figure",
             **{f"jewel{i}": f"jewel{i}" for i in range(1, 7)}}
SURFACE_ROLE = {K.GLOW: "glow", K.SCREEN: "screen", K.CANVAS: "canvas", K.CANVAS2: "canvas2", K.GLASS: "glass"}
HEAD_GLOW = 0.16      # the head surface's emission before the figure colour multiplies it
HEAD_EDGE = 1.9       # the head's line brightness against the body's


def figure_surfaces():
    s = default_surfaces()
    s[K.GLASS] = {"color": [0.03, 0.03, 0.035], "emission": [HEAD_GLOW] * 3, "edge": [HEAD_EDGE] * 3}
    s[K.ACCENT] = {"color": [0.03, 0.028, 0.032], "edge": [1.25] * 3}
    return s


def add_world(film, room: dict, edge_width=0.011, edge_intensity=4.0, max_distance=30.0):
    names = []
    for obj in room["objects"]:
        name, tree, role, bmin, bmax = obj[:5]
        opts = dict(edge_width=edge_width, edge_intensity=edge_intensity, max_distance=max_distance)
        if len(obj) > 5:
            opts.update(obj[5])
        own_line = opts.pop("own_line", False)
        is_figure = opts.pop("figure", False) or role == "figure"
        if is_figure:
            opts.setdefault("surfaces", figure_surfaces())
        film.sdf(name, tree, bmin=bmin, bmax=bmax, **opts)
        if not own_line:
            film.bind(LINE_ROLE[role], f"sdf/{name}/look/edge/color")
        film.bind("fill", f"sdf/{name}/surface/{K.FILL}/color")
        film.bind("fill", f"sdf/{name}/surface/{K.FLOOR}/color")
        if not is_figure:
            film.bind("fill", f"sdf/{name}/surface/{K.ACCENT}/color")
        for k, r in SURFACE_ROLE.items():
            if is_figure and k == K.GLASS:
                film.bind("figure", f"sdf/{name}/surface/{k}/emission", "multiply")
            else:
                film.bind(r, f"sdf/{name}/surface/{k}/emission", "multiply")
        names.append(name)
    lights = []
    for lname, pos, kind in room.get("lights", []):
        color = [1.0, 0.72, 0.42] if kind == "lamp" else [0.5, 0.8, 1.0]
        film.point_light(lname, pos, color, 2.5 if kind == "lamp" else 1.2, 3.5 if kind == "lamp" else 2.2)
        film.bind("glow" if kind == "lamp" else "screen", f"lights/{lname}/color")
        lights.append(lname)
    return names, lights
