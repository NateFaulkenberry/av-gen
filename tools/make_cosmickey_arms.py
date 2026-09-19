#!/usr/bin/env python3
"""Derive the cosmic-key lighting arms from the deliverable project and scene.

    python3 tools/make_cosmickey_arms.py

Every file it writes begins with `_ck-` and is a *derivative*: the deliverable
(`examples/treeisland/tree-of-life-floating-island.{json,scene.json}`) with one thing changed.
Regenerate after any edit to the deliverable, or the comparison stops comparing what ships.

Camera arms -- the same lighting, seen from somewhere else (brief §15):

  _ck-view-wide     the deliverable's own camera, named so the set reads as a set.
  _ck-view-hero     the tree dominates the frame.
  _ck-view-under    below the plateau looking up: §8's underside and §15's low angle.
  _ck-view-orbit    a quarter turn round, to show the key is a world source and not the camera's.
  _ck-view-shadow   the camera on the key's own side of the world, so the frame is mostly the
                    SHADOW side of the tree: §6 lives or dies here.

Control arms -- ADR-182. Each removes exactly one thing, so a probe that passes on the
deliverable has something it must fail on:

  _ck-ctl-nokey     the key light disabled. Every key-to-shadow ratio must collapse toward 1.
  _ck-ctl-noshadow  the key still lit, `castsShadow` off. The shadow AOV must go uniformly white,
                    which is what tells a shadow measurement from a shading measurement.
  _ck-ctl-noglow    every emissiveBoost at 0. What the geometry looks like under the key alone --
                    brief §12's question, asked directly.
"""

from __future__ import annotations

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
EX = HERE / "examples" / "treeisland"
PROJECT = EX / "tree-of-life-floating-island.json"
SCENE = EX / "tree-of-life-floating-island.scene.json"

# position, target, fov. Chosen against the real bounds: the island is about 170 units across and
# hangs some 170 below its plateau; the canopy is 116 across and tops out near y = 78.
CAMERAS = {
    "wide":   None,  # the deliverable's own
    "hero":   ([104.0, 52.0, 100.0], [0.0, 46.0, 0.0], 34.0),
    "under":  ([104.0, -86.0, 100.0], [0.0, -18.0, 0.0], 44.0),
    "orbit":  ([152.0, 18.0, -158.0], [0.0, 13.0, 0.0], 36.0),
    "shadow": ([-138.0, 30.0, 168.0], [0.0, 22.0, 0.0], 36.0),
}

GLOW_NODES = ["tree-tracery", "tree-twigs", "tree-foliage", "tree-lumens"]


def write(stem: str, project: dict, scene: dict) -> None:
    project = json.loads(json.dumps(project))
    project["assets"]["scene"]["path"] = f"{stem}.scene.json"
    project["app"]["name"] = f"cosmickey {stem}"
    (EX / f"{stem}.json").write_text(json.dumps(project, indent=2) + "\n")
    (EX / f"{stem}.scene.json").write_text(json.dumps(scene, indent=2) + "\n")


def main() -> None:
    project = json.loads(PROJECT.read_text())
    scene = json.loads(SCENE.read_text())

    for name, cam in CAMERAS.items():
        s = json.loads(json.dumps(scene))
        if cam is not None:
            s["camera"]["position"], s["camera"]["target"], s["camera"]["fov"] = cam
        write(f"_ck-view-{name}", project, s)

    s = json.loads(json.dumps(scene))
    for light in s["lights"]:
        if light["role"] == "key":
            light["enabled"] = False
    write("_ck-ctl-nokey", project, s)

    s = json.loads(json.dumps(scene))
    for light in s["lights"]:
        if light["role"] == "key":
            light["castsShadow"] = False
    write("_ck-ctl-noshadow", project, s)

    p = json.loads(json.dumps(project))
    for node in GLOW_NODES:
        p["parameters"][f"nodes/{node}/emissiveBoost"] = 0.0
    write("_ck-ctl-noglow", p, json.loads(json.dumps(scene)))

    print(f"wrote {len(CAMERAS) + 3} arms to {EX}")


if __name__ == "__main__":
    main()
