#!/usr/bin/env python3
"""Drop a project's saved transforms for bodies its scenario owns.

## The rule

A project's `parameters` block is applied **over** the values its scene registers. Glowmere Valley
2's carries 5,489 of them, including every node's position, rotation, scale and visibility -- so a
scene file is not the state that runs, it is the state that runs before the project has had its say.
That is fine for set dressing: an author who drags a mushroom in the editor means it.

It is never fine for a body a **scenario** owns *when it contradicts the scene*. The director moves the saucer, lights the beam and
retires the animals; a saved value for any of those is a photograph of a run, not authorship, and
applying it at load does one of three things, all of them wrong:

  * moves the craft to wherever it had drifted, which the director then spends the first beat undoing
  * lights a beam the scene deliberately authored dark
  * hides animals a previous run abducted, so the cast shrinks every time anybody saves

So: **for a scene carrying `staging`, its project may not *contradict* the scene about the transform
or visibility of the scenario's own bodies** -- the actors, their parts, and any entity carrying a
tag one of the scenario's queries filters on. A saved value equal to the scene's is a no-op and is
left alone; only the ones that differ are residue, and only those are dropped.
`tests/unit/test_beam_lab.cpp` asserts it; this removes what is there.

## What this cost, measured (ADR-263)

`glowmere-valley-2-multicam.json` was carrying, against a scene that authors none of it:

  nodes/visitor/position      [17.35, 29.85, 178.54]   the scene says [20, 29.5, 150]
  nodes/visitor-beam/scale    [1, 0.061, 1]            the scene authors no scale at all
  nodes/visitor-beam/visible  true                     the scene authors false
  nodes/{chicken-17,goat-14,pig-15}/visible  false     three animals a previous run abducted

The scale is the expensive one. `applyParameters` scales a particle system's `extent` by
`cbrt(|sx*sy*sz|)` -- `cbrt(0.061)` is 0.394 -- so the beam's emitter radius ran at **3.07 m against
the 7.8 m the scene authors**, and its mouth sat 0.12 m under the hull instead of 2.05 m. Every
large animal was wider than the beam lifting it, in every render, for as long as the file has
existed. No amount of alignment fixes a beam that is two and a half times too narrow.

Usage:  tools/clean_staged_body_overrides.py <project.json> [...]
        tools/clean_staged_body_overrides.py            # every project beside a staged scene
"""
import collections
import glob
import json
import os
import sys

od = collections.OrderedDict
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELDS = ("position", "rotation", "scale", "visible")


def indent_of(path):
    with open(path) as f:
        f.readline()
        second = f.readline()
    width = len(second) - len(second.lstrip(" "))
    return width if width > 0 else 1


def staged_bodies(scene):
    """Every node a scenario owns: its actors, their parts, and everything its queries can bind."""
    staging = scene.get("staging") or {}
    entities = {e["name"]: e for e in scene.get("entities", [])}
    node_of = lambda name: entities.get(name, {}).get("node", name)

    owned, tags = set(), set()
    for actor in staging.get("actors", []):
        owned.add(actor.get("body") or actor["name"])
        for part in actor.get("parts", []):
            owned.add(part["entity"])
    for scenario in staging.get("scenarios", []):
        for beat in scenario.get("beats", []):
            for query in beat.get("find", []):
                if query.get("tag"):
                    tags.add(query["tag"])
                if query.get("name"):
                    owned.add(query["name"])
    for name, e in entities.items():
        if tags & set(e.get("tags", [])):
            owned.add(name)
    return {node_of(n) for n in owned}


DEFAULTS = {"position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0], "visible": True}


def contradicts(saved, authored):
    if isinstance(saved, list) and isinstance(authored, list) and len(saved) == len(authored):
        return max(abs(float(a) - float(b)) for a, b in zip(saved, authored)) > 1e-3
    return saved != authored


def clean(project):
    scene_path = project.replace(".json", ".scene.json")
    if not os.path.isfile(scene_path):
        return None
    scene = json.load(open(scene_path))
    if "staging" not in scene:
        return None
    bodies = staged_bodies(scene)
    if not bodies:
        return None
    nodes = {n["name"]: n for n in scene.get("nodes", [])}
    width = indent_of(project)
    doc = json.load(open(project), object_pairs_hook=od)
    saved = doc.get("parameters")
    if not isinstance(saved, dict):
        return None
    dropped = []
    for path in [k for k in saved]:
        parts = path.split("/")
        if len(parts) != 3 or parts[0] != "nodes" or parts[2] not in FIELDS:
            continue
        node = nodes.get(parts[1])
        if parts[1] not in bodies or node is None:
            continue
        authored = node.get(parts[2], DEFAULTS[parts[2]])
        if contradicts(saved[path], authored):
            dropped.append("%s = %s   (the scene says %s)"
                           % (path, json.dumps(saved[path]), json.dumps(authored)))
            del saved[path]
    if not dropped:
        return None
    with open(project, "w") as out:
        json.dump(doc, out, indent=width)
    return dropped


if __name__ == "__main__":
    targets = sys.argv[1:]
    if not targets:
        targets = [p for p in sorted(glob.glob(os.path.join(REPO, "examples/**/*.json"), recursive=True))
                   if not p.endswith(".scene.json")]
    touched = 0
    for project in targets:
        dropped = clean(project)
        if dropped is None:
            continue
        touched += 1
        print("%s: dropped %d override(s) of bodies its scenario owns" %
              (os.path.relpath(project, REPO), len(dropped)))
        for line in dropped:
            print("    %s" % line)
    if touched == 0:
        print("nothing to drop")
