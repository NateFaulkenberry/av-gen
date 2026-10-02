"""All You Got, art pass 4: adding worlds to a Film (pass 3's world3.add_world, extended).

An object is (name, tree, role, bmin, bmax[, options]). Options beyond world3's:
  `emission`  {surface index: [r, g, b]}: that surface's emission is the object's own colour, not the palette's
              (the lounge's coloured bar, the traffic lights, car lights): its palette binding is dropped
  `edges`     {surface index: [r, g, b]}: a per-surface line colour multiplier (ADR-1047's surface/<k>/edge)
"""

from __future__ import annotations

import world3 as W3
from film2 import default_surfaces


def add_world(film, room: dict, edge_width=0.011, edge_intensity=4.0, max_distance=30.0):
    plain = {"objects": [], "lights": room.get("lights", [])}
    special = {}
    for obj in room["objects"]:
        if len(obj) > 5 and ("emission" in obj[5] or "edges" in obj[5]):
            opts = dict(obj[5])
            emission = opts.pop("emission", {})
            edges = opts.pop("edges", {})
            surfaces = opts.get("surfaces")
            if surfaces is None:
                surfaces = W3.figure_surfaces() if (opts.get("figure") or obj[2] == "figure") else W3.screen_surfaces()
            surfaces = [dict(s) for s in surfaces]
            for k, rgb in emission.items():
                surfaces[k] = dict(surfaces[k], emission=list(map(float, rgb)))
            for k, rgb in edges.items():
                surfaces[k] = dict(surfaces[k], edge=list(map(float, rgb)))
            opts["surfaces"] = surfaces
            special[obj[0]] = set(emission)
            plain["objects"].append(tuple(obj[:5]) + (opts,))
        else:
            plain["objects"].append(obj)
    names, lights = W3.add_world(film, plain, edge_width=edge_width, edge_intensity=edge_intensity, max_distance=max_distance)
    if special:
        drop = set()
        for name, ks in special.items():
            for k in ks:
                drop.add(f"sdf/{name}/surface/{k}/emission")
        film.bindings = [b for b in film.bindings if b["target"] not in drop]
    return names, lights


__all__ = ["add_world", "default_surfaces"]
