#!/usr/bin/env python3
"""All You Got, art pass 4: stills projects for looking at the new pieces (render at fps 1, one view a second).

    python3 tools/liminal/preview4.py tableaux    # the new tableaux in a plain room, two views each
    python3 tools/liminal/preview4.py rooms       # the new kitchen and the basement, a few views a room
    python3 tools/liminal/preview4.py city        # the city: aerials, streets, the intersection, the park, the bar

Then, through the GPU lock, with the pinned engine:
    tools/gpu-lock.sh $S/liminal3/avgen.sh --headless --project examples/liminal/all-you-got-pass4-<what>.json \\
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
import tableaux4 as T4  # noqa: E402
import world4 as W  # noqa: E402
from film2 import Film  # noqa: E402

OUT = os.path.join(ROOT, "examples", "liminal")
GRID = P2.GRID


def stills(film, stem, palette="P3night", extra_params=None):
    film.track("palette/position", [(0.0, float(P2.PALETTE_INDEX[palette])), (film.end, float(P2.PALETTE_INDEX[palette]))])
    params = dict(P2.PARAMS, **{"post/motionBlur/amount": 0.0})
    params.update(extra_params or {})
    path = film.write(OUT, stem, GRID, P2.environment(stem=stem), params)
    print("wrote", path, len(film.shots), "views")
    return path


def add_shots(film, shots, fov=60.0):
    for i, (nm_, eye, look, names, lights) in enumerate(shots):
        e = list(eye)
        film.shot(nm_, i * 1.0, (i + 1) * 1.0, [e, [e[0] + 0.01, e[1], e[2] - 0.01]], look, nodes=names, lights=lights, fov=fov)
    film.end = float(len(shots))
    print("\n".join(f"  {i + 0.5:5.1f} s  {s[0]}" for i, s in enumerate(shots)))


def tableaux(palette="P3night"):
    film = Film(end=60.0)
    P2.palettes(film)
    shots = []
    for i, name in enumerate(T4.NEW):
        spec = TB.TABLEAUX[name]()
        tree, lo, hi = T4.placed(name, f"pv{i}Man")
        _, anchor, _ = TB.build(name, "m")
        ext = ((-2.2, 2.2), (0.0, 2.6), (-2.2, 2.2))
        shell = K.U(K.shell(ext, 0.15), K.floorboards(ext, 0.2, "x"))
        objs = [(f"pv{i}Shell", shell, "wall", (-2.5, -0.3, -2.5), (2.5, 2.9, 2.5)), (f"pv{i}Man", tree, "figure", lo, hi),
                (f"pv{i}Furn", anchor, "furn", (-2.2, -0.05, -2.2), (2.2, 1.9, 2.2))]
        names, lights = W.add_world(film, {"objects": objs, "lights": [(f"pv{i}Lamp", (1.2, 2.0, 1.4), "lamp")]})
        focus = (0.0, 0.95, 0.2)
        views = [((1.4, 1.55, 1.9), focus), ((-1.9, 1.35, 0.4), focus)]
        if name == "car_sit":
            views = [((-2.0, 1.4, 0.9), (0.2, 0.9, 0.2)), ((0.4, 1.9, 2.1), (0.0, 0.9, 0.0))]
        for j, (eye, look) in enumerate(views):
            shots.append((f"{name}{j}", eye, look, names, lights))
    add_shots(film, shots, 58.0)
    return stills(film, "all-you-got-pass4-tableaux", palette)


def rooms(palette="P6violet"):
    import rooms4 as R4
    film = Film(end=60.0)
    P2.palettes(film)
    house = {"kit": R4.kitchen(), **R4.basement()}
    worlds = {k: W.add_world(film, room) for k, room in house.items()}
    figs = {o[0] for room in house.values() for o in room["objects"] if o[2] == "figure"}
    views = [
        (("kit",), "kitStove", (-0.7, 1.55, -4.5), (-2.0, 1.1, -5.9)),
        (("kit",), "kitStove", (-1.2, 1.7, -3.2), (-2.0, 1.0, -5.9)),
        (("kit",), "kitMan", (1.8, 1.5, -3.0), (0.0, 0.9, -4.9)),
        (("lau",), "lauFold", (0.3, -1.4, 1.6), (-2.0, -2.0, 0.2)),
        (("lau",), "lauFold", (-0.4, -1.3, -1.5), (-2.1, -2.0, 0.4)),
        (("gym",), "gymMan", (1.6, -1.4, 1.6), (4.0, -1.9, -0.8)),
        (("gym",), "gymMan", (2.4, -1.5, -1.9), (4.4, -1.9, 0.4)),
        (("lng",), "barMan", (1.0, -1.4, -3.0), (3.6, -1.9, -4.5)),
        (("lng",), "barMan", (2.3, -1.3, -6.0), (3.3, -1.9, -4.0)),
        (("lng",), "loungeMan", (0.3, -1.4, -3.0), (-2.1, -2.1, -4.6)),
        (("lng",), "loungeMan", (-0.6, -1.6, -5.9), (-2.2, -2.1, -4.0)),
    ]
    shots = []
    for i, (keys, fig, eye, look) in enumerate(views):
        names, lights = [], []
        for k in keys:
            names += [n for n in worlds[k][0] if n not in figs or n == fig]
            lights += worlds[k][1]
        shots.append((f"r{i}", eye, look, names, lights))
    add_shots(film, shots, 62.0)
    return stills(film, "all-you-got-pass4-rooms", palette)


def city(palette="P3night"):
    import city4 as C
    import intro4 as IN
    film = Film(end=60.0)
    P2.palettes(film)
    w = W.add_world(film, {"objects": C.objects() + [
        ("cityPark", C.park(), "furn", (C.HX - 15, -0.2, -103), (C.HX + 15, 8, -73), {"max_distance": 300.0}),
        ("cityParkTrees", C.park_trees(), "furn", (C.HX - 15, -0.2, -103), (C.HX + 15, 7.5, -73), {"max_distance": 300.0}),
        ("cityBenches", C.benches(), "furn2", (C.HX - 8, -0.2, -96), (C.HX + 8, 2, -80), {"max_distance": 300.0}),
        ("cityBar", C.bar_building(), "furn2", (C.HX + 26, -0.2, -102), (C.HX + 55, 14, -74), {"max_distance": 300.0}),
        ("cityCross", C.intersection(), "furn", (C.XI - 9, -0.2, C.ZK - 9), (C.XI + 15, 5, C.ZK + 20), {"max_distance": 300.0}),
        ("cityTrafX", C.traffic("x", ((-420, 420), (-420, 300))), "furn", (-420, -0.2, -420), (420, 2, 300), {"max_distance": 300.0}),
        ("cityTrafZ", C.traffic("z", ((-420, 420), (-420, 300))), "furn", (-420, -0.2, -420), (420, 2, 300), {"max_distance": 300.0}),
    ] + IN.objects(), "lights": []})
    names, lights = w
    # all clips open: the city complete
    open_ = []
    for o in C.objects():
        for nm_ in ("Low", "Mid", "TowerA", "TowerB", "Sky"):
            if o[0].endswith(nm_):
                h = C.SETS[nm_][2]
                film.track(f"sdf/{o[0]}/node/{nm_}Ring/size", [(0.0, [600.0, 500.0, 600.0], "step"), (60.0, [600.0, 500.0, 600.0], "step")])
                film.track(f"sdf/{o[0]}/node/{nm_}Rise/size", [(0.0, [2000.0, C.rise_size(h + 2), 2000.0], "step"), (60.0, [2000.0, C.rise_size(h + 2), 2000.0], "step")])
    for nm_ in ("kerbs", "marks", "lights"):
        o = {"kerbs": "cityKerbs", "marks": "cityMarks", "lights": "cityLamps"}[nm_]
        film.track(f"sdf/{o}/node/{nm_}Ring/size", [(0.0, [600.0, 50.0, 600.0], "step"), (60.0, [600.0, 50.0, 600.0], "step")])
    for o in ("cityTrafX", "cityTrafZ"):
        film.track(f"sdf/{o}/node/trafficRing/size", [(0.0, [600.0, 20.0, 600.0], "step"), (60.0, [600.0, 20.0, 600.0], "step")])
    # the intro's neighbourhood complete too
    film.track("sdf/street/node/gridClip/size", [(0.0, [400.0, 0.5, 400.0], "step"), (60.0, [400.0, 0.5, 400.0], "step")])
    film.track("sdf/street/node/dashClip/size", [(0.0, [600.0, 0.5, 0.5], "step"), (60.0, [600.0, 0.5, 0.5], "step")])
    film.track("sdf/row/node/rowClip/size", [(0.0, [120.0, 8.0, 30.0], "step"), (60.0, [120.0, 8.0, 30.0], "step")])
    film.track("sdf/parked/node/carClip/size", [(0.0, [120.0, 3.0, 30.0], "step"), (60.0, [120.0, 3.0, 30.0], "step")])
    film.track("sdf/lamps/node/lampClip/size", [(0.0, [120.0, 6.0, 30.0], "step"), (60.0, [120.0, 6.0, 30.0], "step")])
    film.track("sdf/heroShell/node/heroRise/size", [(0.0, [6.0, 6.2, 6.0], "step"), (60.0, [6.0, 6.2, 6.0], "step")])
    film.track("sdf/heroShell/node/heroRiseAt/translation", [(0.0, [1.175, 6.2, -2.1], "step"), (60.0, [1.175, 6.2, -2.1], "step")])
    film.track("nodes/faller/visible", [(0.0, 0.0, "step"), (60.0, 0.0, "step")])
    film.track("scene/volumeDensity", [(0.0, 0.006, "step"), (60.0, 0.006, "step")])
    views = [
        ("aerialN", (C.HX - 30.0, 140.0, 120.0), (C.HX + 20.0, 0.0, -110.0)),
        ("aerialHouse", (C.HX - 20.0, 45.0, 40.0), (C.HX, 0.0, -10.0)),
        ("aerialHigh", (C.HX + 160.0, 230.0, 160.0), (C.HX, 0.0, -60.0)),
        ("avenue", (C.XI + 1.0, 12.0, -30.0), (C.XI, 20.0, -200.0)),
        ("street", (C.HX + 60.0, 1.7, -108.0 + 3.0), (C.HX - 40.0, 6.0, -108.0)),
        ("crossing", (C.XI - 9.0, 2.2, C.ZK + 12.0), (C.XI + 2.0, 1.5, C.ZK)),
        ("parkBench", (C.BENCH_AT[0] - 2.5, 1.6, C.BENCH_AT[2] - 4.0), (C.BENCH_AT[0], 0.8, C.BENCH_AT[2])),
        ("bar", (C.XI - 2.0, 1.7, C.ZK + 14.0), (C.BAR["x0"], 1.6, (C.BAR["z0"] + C.BAR["z1"]) / 2)),
        ("rooftops", (C.HX + 40.0, 70.0, -60.0), (C.HX + 60.0, 30.0, -160.0)),
        ("intro", (-30.0, 7.5, 18.0), (0.0, 3.5, 4.0)),
    ]
    shots = [(nm_, eye, look, names, lights) for nm_, eye, look in views]
    add_shots(film, shots, 60.0)
    return stills(film, "all-you-got-pass4-city", palette)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["tableaux", "rooms", "city"])
    ap.add_argument("--palette", default=None)
    a = ap.parse_args()
    fn = {"tableaux": tableaux, "rooms": rooms, "city": city}[a.what]
    fn(a.palette) if a.palette else fn()


if __name__ == "__main__":
    main()
