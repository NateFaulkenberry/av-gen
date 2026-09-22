#!/usr/bin/env python3
"""Phase C §67: generate the multi-character motion-matching scenes (10, 50 and 100 matched aliens).

Each scene is the ADR-623 match lab (examples/labs/motionmatch/alien-match-lab.scene.json) with its
two aliens replaced by N matched scouts on a grid in the same valley. Every scout wanders on its own
seed, and they all share one database: the composition builds it once per skeleton and config
(ADR-650).

    python3 tools/gen_match_crowd.py            # writes the three scenes next to the lab
"""

import copy
import json
import math
import pathlib

HERE = pathlib.Path(__file__).resolve().parent.parent / "examples" / "labs" / "motionmatch"
CENTRE = (-184.96, -161.0)
SPACING = 5.0


def crowd(lab, n):
    scene = copy.deepcopy(lab)
    scene["name"] = f"glowmere-match-crowd-{n}"
    node_template = next(x for x in lab["nodes"] if x["name"] == "alien-match")
    entity_template = next(x for x in lab["entities"] if x["name"] == "alien-match")
    scene["nodes"] = [x for x in lab["nodes"] if x["kind"] != "gltf"]
    scene["entities"] = []
    side = math.ceil(math.sqrt(n))
    for i in range(n):
        row, col = divmod(i, side)
        name = f"scout-{i:03d}"
        node = copy.deepcopy(node_template)
        node["name"] = name
        node["position"] = [
            round(CENTRE[0] + (col - (side - 1) / 2) * SPACING, 3),
            node_template["position"][1],
            round(CENTRE[1] + (row - (side - 1) / 2) * SPACING, 3),
        ]
        node["rotation"] = [0.0, round((i * 137.5) % 360.0 - 180.0, 1), 0.0]
        scene["nodes"].append(node)
        entity = copy.deepcopy(entity_template)
        entity["name"] = name
        entity["node"] = name
        entity["seed"] = 6700 + i
        # A home per scout, so the crowd spreads over the grid rather than converging on one point.
        for behaviour in entity["behaviors"]:
            if behaviour["kind"] == "wander":
                behaviour["homeRadius"] = SPACING * 1.5
        entity["motionMatching"]["clips"] = ["Idle", "Walking", "Running"]
        entity["motionMatching"]["weights"] = {
            "version": 1,
            "jointPosition": 0.3,
            "jointVelocity": 0.4,
            "trajectoryPosition": 3.0,
            "trajectoryFacing": 0.5,
            "rootVelocity": 3.0,
        }
        scene["entities"].append(entity)
    # Pull the camera back to frame the grid.
    extent = side * SPACING
    scene["camera"]["position"] = [CENTRE[0] + extent * 0.9, 41.0 + extent * 0.45, CENTRE[1] + extent * 0.9]
    scene["camera"]["target"] = [CENTRE[0], 40.0, CENTRE[1]]
    return scene


def main():
    lab = json.loads((HERE / "alien-match-lab.scene.json").read_text())
    for n in (10, 50, 100):
        out = HERE / f"glowmere-match-crowd-{n}.scene.json"
        out.write_text(json.dumps(crowd(lab, n), indent=1) + "\n")
        print(out)


if __name__ == "__main__":
    main()
