#!/usr/bin/env python3
"""Derive the Tree of Life comparison arms from the deliverable project and scene.

    python3 tools/make_treeisland_arms.py

Every file it writes begins with `_` and is a *derivative*: it is the deliverable
(`examples/treeisland/tree-of-life-floating-island.{json,scene.json}`) with one thing changed.
Regenerate them after any edit to the deliverable, or the comparison stops comparing what is
shipping to what is shipping.

The arms, and what each is evidence for:

  _compare-original       ADR-338 as merged -- the one arm that is NOT derived, because "before"
                          has to be the actual before. Written once, by hand, from the merge
                          commit; this script only checks it is still there.
  _glow-off               brief §11 "structural only" and §29 #2: every emissiveBoost at 0.
  _glow-low / _glow-high  brief §11 low and high, at 0.4x and 3x the shipping values.
  _ctl-no-cosmos-shader   ADR-182 control: the cosmos background shader removed and nothing else,
                          so "the shader does something" is measured rather than asserted.
  _ctl-no-stars           ADR-182 control for the star shells' effect on the shadow cascades: the
                          far plane follows the scene radius, and the stars are what sets it.
  _view-low-angle         brief §29 #6: a camera under the plateau, to show no root escapes.
  _view-contact           the root/soil transition close up, for judging §12 and §15.

The shipping emissive boosts are read out of the project rather than written here, so there is
one place to change them.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
EX = HERE / "examples" / "treeisland"
PROJECT = EX / "tree-of-life-floating-island.json"
SCENE = EX / "tree-of-life-floating-island.scene.json"

CHANNELS = ["tree-tracery", "tree-twigs", "tree-foliage", "tree-lumens"]


def boost_of(project: str, node: str) -> float:
    m = re.search(rf'"nodes/{re.escape(node)}/emissiveBoost":\s*([0-9.]+)', project)
    if not m:
        raise SystemExit(f"{PROJECT}: no emissiveBoost parameter for node '{node}'")
    return float(m.group(1))


def with_boosts(project: str, scale: float) -> str:
    """Scale every Glowmere channel. Textual, never json.load/json.dump: a round trip through
    Python's json rewrites ~140 unrelated float literals in this file and buries the edit."""
    out = project
    for node in CHANNELS:
        value = boost_of(project, node) * scale
        out = re.sub(rf'("nodes/{re.escape(node)}/emissiveBoost":\s*)[0-9.]+',
                     lambda m, v=value: f"{m.group(1)}{v:g}", out)
    return out


def retitle(text: str, suffix: str) -> str:
    return text.replace('"name": "Tree of Life - Floating Island"',
                        f'"name": "Tree of Life - Floating Island ({suffix})"', 1)


def point_at(scene: str, position: str, target: str, fov: str) -> str:
    out = re.sub(r'"position": \[[-0-9., ]+\],\n(\s*)"target": \[[-0-9., ]+\],\n\s*"fov": [0-9.]+',
                 lambda m: f'"position": {position},\n{m.group(1)}"target": {target},\n{m.group(1)}"fov": {fov}',
                 scene, count=1)
    if out == scene:
        raise SystemExit("camera block not found in the scene file")
    return out


def write(name: str, project: str, scene: str | None) -> None:
    if scene is not None:
        (EX / f"{name}.scene.json").write_text(scene)
        project = project.replace('"path": "tree-of-life-floating-island.scene.json"',
                                  f'"path": "{name}.scene.json"')
    (EX / f"{name}.json").write_text(project)
    for suffix in ((".json", ".scene.json") if scene is not None else (".json",)):
        path = EX / f"{name}{suffix}"
        json.loads(path.read_text())          # parse-check only; never re-serialised
    print(f"  {name}")


def drop_nodes(scene: str, names: list[str]) -> str:
    """Remove whole node objects from the scene's `nodes` array, textually."""
    for name in names:
        i = scene.index(f'"name": "{name}"')
        start = scene.rindex("{", 0, i)
        depth = 0
        end = None
        for k in range(start, len(scene)):
            if scene[k] == "{":
                depth += 1
            elif scene[k] == "}":
                depth -= 1
                if depth == 0:
                    end = k + 1
                    break
        j = end
        while j < len(scene) and scene[j] in " \n\r\t":
            j += 1
        if j < len(scene) and scene[j] == ",":
            end = j + 1
        else:
            # Removing the *last* element leaves the comma that preceded it dangling before the
            # closing bracket, which is valid-looking and is not valid JSON.
            k = start - 1
            while k >= 0 and scene[k] in " \n\r\t":
                k -= 1
            if k >= 0 and scene[k] == ",":
                start = k
        scene = scene[:start] + scene[end:]
        scene = re.sub(r"\n\s*\n\s*\n", "\n", scene)
    return scene


def main() -> int:
    project = PROJECT.read_text()
    scene = SCENE.read_text()
    shipping = {n: boost_of(project, n) for n in CHANNELS}
    print("shipping Glowmere channels: " + ", ".join(f"{k}={v:g}" for k, v in shipping.items()))
    print("writing arms:")

    write("_glow-off", retitle(with_boosts(project, 0.0), "Glowmere off -- structural only"), None)
    write("_glow-low", retitle(with_boosts(project, 0.4), "Glowmere low"), None)
    write("_glow-high", retitle(with_boosts(project, 3.0), "Glowmere high"), None)

    no_shader = re.sub(r'"shaders": \[.*?\],\n', '"shaders": [],\n', project, flags=re.S)
    if '"shaders": [],' not in no_shader:
        raise SystemExit("could not empty the shaders array")
    write("_ctl-no-cosmos-shader",
          retitle(no_shader, "control: no cosmos shader"),
          retitle(scene, "control: no cosmos shader"))

    write("_ctl-no-stars",
          retitle(project, "control: no star shells"),
          retitle(drop_nodes(scene, ["cosmos-stars-near", "cosmos-stars-mid",
                                     "cosmos-stars-far", "cosmos-motes"]),
                  "control: no star shells"))

    write("_view-low-angle",
          retitle(project, "low angle"),
          retitle(point_at(scene, "[132.0, -46.0, 127.0]", "[0.0, 6.0, 0.0]", "40.0"), "low angle"))

    write("_view-contact",
          retitle(project, "root contact"),
          retitle(point_at(scene, "[86.0, 18.0, 83.0]", "[0.0, 2.0, 0.0]", "30.0"), "root contact"))

    original = EX / "_compare-original.json"
    print(f"  _compare-original: {'present' if original.exists() else 'MISSING -- the before arm is gone'}")
    return 0 if original.exists() else 1


if __name__ == "__main__":
    sys.exit(main())
