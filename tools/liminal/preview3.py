#!/usr/bin/env python3
"""All You Got, art pass 3: stills projects for looking at the kit (render at fps 1, one shot a second).

    python3 tools/liminal/preview3.py tableaux     # every tableau in a plain room, two views each
    python3 tools/liminal/preview3.py rooms        # the pass 3 house, a few views a room

Then, through the GPU lock, with the pinned engine:
    tools/gpu-lock.sh $S/liminal3/avgen.sh --headless --project examples/liminal/all-you-got-pass3-<what>.json \\
        --render <dir> --format png --range 0.5:<n> --size 960x540 --fps 1
"""

from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import kit as K  # noqa: E402
import make_all_you_got_pass2 as P2  # noqa: E402
import tableaux3 as TB  # noqa: E402
import world3 as W  # noqa: E402
from film2 import Film  # noqa: E402

OUT = os.path.join(ROOT, "examples", "liminal")
GRID = P2.GRID


def stills(film, stem, palette="P3night"):
    film.track("palette/position", [(0.0, float(P2.PALETTE_INDEX[palette])), (film.end, float(P2.PALETTE_INDEX[palette]))])
    path = film.write(OUT, stem, GRID, P2.environment(stem=stem), dict(P2.PARAMS, **{"post/motionBlur/amount": 0.0}))
    print("wrote", path, len(film.shots), "views")
    return path


def tableaux(palette="P3night"):
    film = Film(end=60.0)
    P2.palettes(film)
    shots = []
    for i, name in enumerate(TB.TABLEAUX):
        fig, anchor, spec = TB.build(name, f"pv{i}Man")
        if name == "window":
            ext = ((-2.0, 2.0), (0.0, 2.6), (-3.0, 0.5))
            shell = K.D(K.shell(ext, 0.15), K.X(((-0.6, 0.6), (0.9, 2.2), (0.3, 0.9))))
            shell = K.U(shell, K.place(K.window_frame(1.2, 1.3, 0.9, 0.0, 0.0), (0.0, 0.0, 0.5), 180.0))
            furn = None
        else:
            ext = ((-2.2, 2.2), (0.0, 2.6), (-2.2, 2.2))
            shell = K.U(K.shell(ext, 0.15), K.floorboards(ext, 0.2, "x"))
            furn = anchor
        lo, hi = fig.bounds()
        objs = [(f"pv{i}Shell", shell, "wall", (ext[0][0] - 0.3, -0.3, ext[2][0] - 0.3), (ext[0][1] + 0.3, 2.9, ext[2][1] + 0.3)),
                (f"pv{i}Man", fig.tree, "figure", lo, hi)]
        if furn is not None:
            objs.append((f"pv{i}Furn", furn, "furn", (-2.2, -0.05, -2.2), (2.2, 1.6, 2.2)))
        names, lights = W.add_world(film, {"objects": objs, "lights": [(f"pv{i}Lamp", (1.2, 2.0, 1.4), "lamp")]})
        lie = spec["posture"] == "lie"
        focus = (0.0, 0.62, 0.0) if lie else (0.0, 0.85, 0.15)
        if name == "window":
            views = [((1.0, 1.6, -2.4), (0.0, 1.3, 0.3)), ((-1.5, 1.5, -1.0), (0.0, 1.1, 0.0))]
        elif lie:
            views = [((1.4, 1.55, 1.9), focus), ((-0.6, 2.2, 1.6), focus)]
        else:
            views = [((1.3, 1.45, 1.8), focus), ((-1.9, 1.25, 0.6), focus)]
        for j, (eye, look) in enumerate(views):
            shots.append((f"{name}{j}", eye, look, names, lights))
    for i, (nm_, eye, look, names, lights) in enumerate(shots):
        e = list(eye)
        film.shot(nm_, i * 1.0, (i + 1) * 1.0, [e, [e[0] + 0.01, e[1], e[2] - 0.01]], look, nodes=names, lights=lights, fov=58)
    film.end = float(len(shots))
    print("\n".join(f"  {i + 0.5:5.1f} s  {s[0]}" for i, s in enumerate(shots)))
    return stills(film, "all-you-got-pass3-tableaux", palette)


def rooms(palette="P3night", validate=True):
    """The pass 3 house's ground floor, three or four views a room, each showing one tableau."""
    import rooms3 as RM3
    film = Film(end=60.0)
    P2.palettes(film)
    house = {**RM3.ground_floor(), **RM3.basement(), **RM3.upstairs()}
    worlds = {}
    for key, room in house.items():
        worlds[key] = W.add_world(film, room)
    figs = {o[0] for room in house.values() for o in room["objects"] if o[2] == "figure"}
    views = [  # (room keys, figure shown, eye, look)
        (("liv",), "livThinker", (-0.6, 1.55, 1.9), (1.3, 0.85, -1.2)),
        (("liv",), "livLie", (0.6, 1.6, 1.7), (-2.1, 0.6, 0.3)),
        (("liv",), "livHands", (0.2, 1.4, -1.4), (-2.0, 0.8, 0.4)),
        (("liv",), "livWindow", (1.4, 1.55, -1.6), (-0.6, 1.3, 2.0)),
        (("liv", "kit"), "kitMan", (-1.75, 1.55, -1.4), (0.0, 0.9, -4.8)),
        (("kit",), "kitMan", (1.8, 1.5, -3.0), (0.0, 0.9, -4.9)),
        (("hall",), "hallMan", (3.4, 1.6, 1.9), (4.3, 1.4, -2.0)),
        (("hall",), "hallMan", (4.4, 2.6, -2.6), (3.4, 0.8, 0.4)),
        (("lau", "sto"), None, (0.3, -1.4, 1.8), (-2.0, -2.2, -1.0)),
        (("sto", "lau"), None, (4.4, -1.35, 1.8), (1.5, -2.2, -1.5)),
        (("boi",), None, (3.8, -1.3, -2.8), (-0.5, -2.0, -6.0)),
        (("bed",), "bedLie", (0.7, 4.45, 0.9), (-1.6, 3.4, 0.0)),
        (("bed",), "bedEdge", (-0.6, 4.35, 1.9), (-1.7, 3.6, 0.5)),
        (("bath",), "bathMirror", (2.0, 4.4, -0.3), (4.6, 3.9, 0.9)),
        (("bath",), "bathToilet", (2.0, 4.45, 0.0), (3.0, 3.4, 2.0)),
        (("stu",), "stuDesk", (0.6, 4.45, -3.0), (-1.0, 3.7, -6.0)),
        (("stu",), "stuFloor", (0.8, 4.4, -2.9), (-2.4, 3.3, -4.2)),
    ]
    for i, (keys, fig, eye, look) in enumerate(views):
        names, lights = [], []
        for k in keys:
            names += [n for n in worlds[k][0] if n not in figs or n == fig]
            lights += worlds[k][1]
        e = list(eye)
        film.shot(f"v{i}", i * 1.0, (i + 1) * 1.0, [e, [e[0] + 0.01, e[1], e[2] - 0.01]], look, nodes=names, lights=lights, fov=62)
    film.end = float(len(views))
    path = stills(film, "all-you-got-pass3-rooms", palette)
    if validate:
        import json
        import liminal_space as ls
        scene = json.load(open(path.replace(".json", ".scene.json")))
        rep = ls.validate(path)
        print(ls.text(rep))
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["tableaux", "rooms"])
    ap.add_argument("--palette", default="P3night")
    a = ap.parse_args()
    if a.what == "tableaux":
        tableaux(a.palette)
    elif a.what == "rooms":
        rooms(a.palette)


if __name__ == "__main__":
    main()
