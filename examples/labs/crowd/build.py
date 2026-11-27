#!/usr/bin/env python3
"""The Crowd Navigation Lab's environments (115, Agent 3; ADR-2229). Stdlib only, deterministic.

    python3 examples/labs/crowd/build.py            # write eNN-*.json and eNN-*.scene.json beside this file
    python3 examples/labs/crowd/build.py --check    # compare with the committed files byte for byte (a CPU test runs this)

Edit this script, not the JSON. Every environment is a flat procedural terrain (the navigation grid's
heightfield world), authored walls from the scene's `crowd` block, and the CC0 Mixamo alien
(`assets/imported/alien.gltf`, tracked) as every agent. No third-party or commercial content.

Environments (the spec's list order, 115 spec section 2, "Crowd navigation test lab"):
  e01 flat plaza: two groups crossing between four destination areas, three times (at 0.5, 35 and 70 s)
  e02 obstacles and multiple paths: a block field and a long wall with two openings
  e03 narrow passage / bottleneck: a 24 x 20 m room, a 2.0 m gap, 200 agents (tests take subsets)
  e08 deterministic replay: wandering groups with staggered departures, for seek = play and threads
(e04-e07 arrive with Stage B phase B3.)
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ALIEN = "../../../assets/imported/alien.gltf"
SCALE = 0.02
# The CC0 alien's walk and run, in metres per second at SCALE: 53.12 and 114.27 model units a second
# (measured off the clip's toes, tests/unit/test_stylized_wanderer.cpp) x 0.02.
WALK = round(53.12 * SCALE, 4)
RUN = round(114.27 * SCALE, 4)


def r(v, n=3):
    return round(float(v), n)


def terrain(name, size):
    return {"name": "ground", "kind": "terrain", "position": [0.0, 0.0, 0.0],
            "terrain": {"chunkSize": 32.0, "resolution": 16, "lodLevels": 2, "lodDistance": 120.0,
                        "viewDistance": 400.0, "shadowDistance": 120.0, "skirtDepth": 0.5,
                        "groundMottle": False, "groundGlow": 0.0},
            "world": {"name": name, "seed": 2220, "size": [size, size], "baseHeight": 0.0, "erosion": 0.0,
                      "seaLevel": -1000.0, "layers": [{"frequency": 0.004, "amplitude": 0.0}], "features": []},
            "material": {"baseColor": [0.18, 0.19, 0.21], "roughness": 0.95, "metallic": 0.0}}


def wall_nodes(walls, tag):
    """The walls, drawn: one box per segment (the `crowd` block's walls are what navigation reads)."""
    out = []
    for wi, w in enumerate(walls):
        pts = w["points"]
        for k in range(1, len(pts)):
            (ax, az), (bx, bz) = pts[k - 1], pts[k]
            length = math.hypot(bx - ax, bz - az)
            yaw = math.degrees(math.atan2(bx - ax, bz - az))
            out.append({"name": "%s-wall%d-%d" % (tag, wi, k), "kind": "procedural",
                        "position": [r((ax + bx) / 2), r(w["height"] / 2), r((az + bz) / 2)],
                        "rotation": [0.0, r(yaw), 0.0],
                        "procedural": {"source": {"kind": "box", "size": [r(w["thickness"]), r(w["height"]), r(length)],
                                                  "subdivisions": 1},
                                       "distribution": {"kind": "single"}, "castsShadow": True,
                                       "material": {"baseColor": [0.55, 0.52, 0.48], "roughness": 0.85}}})
    return out


def agent(name, group, x, z, yaw):
    return {"name": name, "kind": "gltf", "asset": ALIEN, "position": [r(x), 0.0, r(z)], "rotation": [0.0, r(yaw), 0.0],
            "scale": [SCALE, SCALE, SCALE],
            "animation": {"state": "Idle", "blend": 0.2, "cullDistance": 0.0, "crowd": group}}


def group(name, seed, speed=(0.95, 1.15)):
    return {"name": name, "radius": 0.3, "speed": list(speed), "speedBuckets": 3, "maxSpeed": 1.6,
            "neighbourDist": 3.0, "maxNeighbours": 10, "timeHorizon": 2.0, "turnRate": 6.0, "seed": seed,
            "gait": {"walkSpeed": WALK, "runSpeed": RUN, "matchRate": True, "rateMin": 0.5, "rateMax": 1.6,
                     "blend": 0.2},
            "clips": {"idle": "Idle", "walk": "Walk", "run": "Run"}}


def scene(name, size, nodes, crowd, cam_pos, cam_target):
    return {"format": "avgen-scene", "version": 1, "name": name,
            "camera": {"mode": 1, "position": cam_pos, "target": cam_target, "fov": 50.0, "orbitSpeed": 0.0},
            "environment": {"background": [0.03, 0.035, 0.05], "skyIntensity": 0.0, "lightFromEnvironment": False,
                            "volumeDensity": 0.0, "shadowCascades": 2},
            "lights": [{"name": "sun", "type": "directional", "direction": [-0.4, -1.0, -0.3], "color": [1.0, 0.97, 0.92],
                        "intensity": 3.0}],
            "navCellSize": 0.5, "navBodyRadius": 0.3,
            "nodes": [terrain(name, size)] + nodes,
            "crowd": crowd}


def project(title, scene_file, seconds):
    return {"format": "avgen-project", "version": 4, "app": {"name": title},
            "assets": {"scene": {"kind": "composition", "path": scene_file}},
            "parameters": {}, "sources": [], "routes": [], "presets": [],
            "timeline": {"enabled": False, "tracks": [], "cues": []},
            "render": {"width": 960, "height": 540, "fps": 30, "output": "sequence", "duration": seconds}}


def lattice(n, x0, z0, cols, dx, dz):
    return [(x0 + (i % cols) * dx, z0 + (i // cols) * dz) for i in range(n)]


def e01():
    nodes, half = [], 25
    for i, (x, z) in enumerate(lattice(half, -16.0, -16.0, 5, 1.2, 1.2)):
        nodes.append(agent("a%02d" % i, "west", x, z, 45.0))
    for i, (x, z) in enumerate(lattice(half, 11.0, -16.0, 5, 1.2, 1.2)):
        nodes.append(agent("b%02d" % i, "east", x, z, -45.0))
    ne, nw, se, sw = [12.0, 12.0], [-12.0, 12.0], [12.0, -12.0], [-12.0, -12.0]
    area = lambda c: {"center": c, "halfExtents": [3.5, 3.5]}
    crowd = {"groups": [group("west", 101), group("east", 102)],
             "schedule": [
                 {"at": 0.5, "group": "west", "command": "goto", "area": area(ne), "stagger": 1.5, "face": [0.0, 0.0]},
                 {"at": 0.5, "group": "east", "command": "goto", "area": area(nw), "stagger": 1.5, "face": [0.0, 0.0]},
                 {"at": 35.0, "group": "west", "command": "goto", "area": area(sw), "stagger": 1.0},
                 {"at": 35.0, "group": "east", "command": "goto", "area": area(se), "stagger": 1.0},
                 {"at": 70.0, "group": "west", "command": "goto", "area": area(se), "stagger": 1.0},
                 {"at": 70.0, "group": "east", "command": "goto", "area": area(sw), "stagger": 1.0}]}
    return scene("Crowd Lab e01: flat plaza", 100.0, nodes, crowd, [0.0, 38.0, 26.0], [0.0, 0.0, 0.0]), 105.0


def e02():
    walls = []
    # A long wall across the middle with two openings (3 m and 2 m), so there are two routes.
    walls.append({"points": [[-20.0, 0.0], [-9.5, 0.0]], "thickness": 0.4, "height": 2.2})
    walls.append({"points": [[-6.5, 0.0], [6.0, 0.0]], "thickness": 0.4, "height": 2.2})
    walls.append({"points": [[8.0, 0.0], [20.0, 0.0]], "thickness": 0.4, "height": 2.2})
    # Blocks on the south side: a field of 2 x 2 m pillars.
    for bx in (-12.0, -4.0, 4.0, 12.0):
        for bz in (-7.0, -13.0):
            x0, z0, h = bx - 1.0, bz - 1.0, 2.0
            walls.append({"points": [[x0, z0], [x0 + 2.0, z0], [x0 + 2.0, z0 + 2.0], [x0, z0 + 2.0], [x0, z0]],
                          "thickness": 0.3, "height": h})
    nodes = [agent("p%02d" % i, "crossers", x, z, 0.0) for i, (x, z) in enumerate(lattice(40, -12.0, -18.5, 10, 2.6, 1.3))]
    crowd = {"walls": walls, "groups": [group("crossers", 201)],
             "schedule": [{"at": 0.5, "group": "crossers", "command": "goto",
                           "area": {"center": [0.0, 13.0], "halfExtents": [10.0, 3.0]}, "stagger": 2.0}]}
    return scene("Crowd Lab e02: obstacles and multiple paths", 100.0, wall_nodes(walls, "e02") + nodes, crowd,
                 [0.0, 40.0, 24.0], [0.0, 0.0, -1.0]), 60.0


def e03():
    gap = 1.0 + 0.15  # wall half-thickness 0.15: the clear gap is 2.0 m
    walls = [{"points": [[0.0, -10.0], [0.0, -gap]], "thickness": 0.3, "height": 2.5},
             {"points": [[0.0, gap], [0.0, 10.0]], "thickness": 0.3, "height": 2.5},
             {"points": [[0.0, -10.0], [-24.0, -10.0], [-24.0, 10.0], [0.0, 10.0]], "thickness": 0.3, "height": 2.5}]
    nodes = []
    for i in range(200):
        cx, cy = i % 19, i // 19
        jx = ((i * 7919) % 100) / 100.0 - 0.5
        jz = ((i * 104729) % 100) / 100.0 - 0.5
        x = -3.0 - cx * 1.0 + jx * 0.3 - (0.5 if cy >= 19 else 0.0)
        z = -9.0 + (cy % 19) * 1.0 + jz * 0.3
        nodes.append(agent("q%03d" % i, "evac", x, z, 90.0))
    crowd = {"walls": walls, "groups": [group("evac", 301)],
             "schedule": [{"at": 0.5, "group": "evac", "command": "goto",
                           "area": {"center": [15.0, 0.0], "halfExtents": [8.0, 12.0]}, "stagger": 0.5}]}
    return scene("Crowd Lab e03: bottleneck", 100.0, wall_nodes(walls, "e03") + nodes, crowd,
                 [-6.0, 34.0, 18.0], [-4.0, 0.0, 0.0]), 120.0


def e08():
    nodes = []
    for i, (x, z) in enumerate(lattice(30, -14.0, -6.0, 6, 1.4, 1.4)):
        nodes.append(agent("w%02d" % i, "wanderA", x, z, 0.0))
    for i, (x, z) in enumerate(lattice(30, 6.0, -6.0, 6, 1.4, 1.4)):
        nodes.append(agent("v%02d" % i, "wanderB", x, z, 180.0))
    crowd = {"walls": [{"points": [[-3.0, -4.0], [3.0, -4.0], [3.0, 4.0], [-3.0, 4.0], [-3.0, -4.0]],
                        "thickness": 0.4, "height": 2.0}],
             "groups": [group("wanderA", 801), group("wanderB", 802, (0.9, 1.3))],
             "schedule": [
                 {"at": 0.2, "group": "wanderA", "command": "wander",
                  "area": {"center": [0.0, 0.0], "halfExtents": [18.0, 14.0]}, "stagger": 3.0, "dwell": [1.0, 4.0]},
                 {"at": 1.0, "group": "wanderB", "command": "wander",
                  "area": {"center": [0.0, 0.0], "halfExtents": [18.0, 14.0]}, "stagger": 3.0, "dwell": [0.5, 3.0]},
                 {"at": 30.0, "group": "wanderA", "command": "goto", "area": {"center": [-12.0, 10.0], "halfExtents": [4.0, 3.0]},
                  "stagger": 1.0, "face": [0.0, 0.0]},
                 {"at": 34.0, "group": "wanderB", "command": "hold"}]}
    return scene("Crowd Lab e08: deterministic replay", 100.0, nodes, crowd, [0.0, 40.0, 24.0], [0.0, 0.0, 0.0]), 40.0


ENVIRONMENTS = [("e01-flat-plaza", "Crowd Lab e01: flat plaza", e01),
                ("e02-obstacles", "Crowd Lab e02: obstacles and multiple paths", e02),
                ("e03-bottleneck", "Crowd Lab e03: bottleneck", e03),
                ("e08-replay", "Crowd Lab e08: deterministic replay", e08)]


def dump(obj):
    return json.dumps(obj, indent=1, sort_keys=False) + "\n"


def outputs():
    files = {}
    for slug, title, fn in ENVIRONMENTS:
        sc, seconds = fn()
        files[slug + ".scene.json"] = dump(sc)
        files[slug + ".json"] = dump(project(title, slug + ".scene.json", seconds))
    return files


def main():
    files = outputs()
    if "--check" in sys.argv:
        bad = []
        for name, text in sorted(files.items()):
            path = os.path.join(HERE, name)
            if not os.path.exists(path) or open(path, encoding="utf-8").read() != text:
                bad.append(name)
        if bad:
            print("crowd lab: out of date (run build.py): " + ", ".join(bad))
            return 1
        print("crowd lab: %d files match" % len(files))
        return 0
    for name, text in sorted(files.items()):
        with open(os.path.join(HERE, name), "w", encoding="utf-8") as f:
            f.write(text)
    print("crowd lab: wrote %d files" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
