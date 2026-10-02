#!/usr/bin/env python3
"""The room / spatial validator's Python hook (ADR-1051).

The validator itself is the engine's (`avgen --validate-space`, headless, no GPU): it reads a scene file's semantic
annotations and answers "is this spatial arrangement actually valid?" before anything is rendered. This module is
the art side's handle on it:

    import sys; sys.path.insert(0, "tools"); sys.path.insert(0, "tools/liminal")
    import kit, liminal_space as ls
    ls.instrument_kit(kit)                  # every kit prop now tags itself: chair, table, couch, mannequin + head...
    ...build the scene as usual...
    report = ls.validate(scene)             # a scene dict, or a path to a scene / project file
    print(ls.text(report))                  # the section 14 report
    applied = ls.apply_fixes(scene, report, rules={"floor", "lyricPlacement"})   # the safe, deterministic ones
    report = ls.validate(scene)             # and again

Tagging by hand (anything that is not a kit prop, or to name things):

    ls.tag(node, "chair", id="KitchenChair_01", anchor="KitchenTable_01")
    ls.tag(room_shell_node, "room", id="kitchen", interior=ext)   # ext = ((x0, x1), (y0, y1), (z0, z1))
    ls.tag(figure, "mannequin", id="Man", pose="thinker", anchor="LivingChair_01", hip=[0, 0.47, 0])
    ls.part(head_node, "head")
    kit.chair(entity={"id": "DeskChair_01", "anchor": "Desk_01"})  # any instrumented kit call takes `entity=`

An annotation is a dict under the node's "entity" key; the engine ignores it, so annotated scenes render unchanged.
The entity's frame is the frame its node sits in (the kit's convention: base on y = 0, front facing +Z), so tag the
prop node *inside* `place()`, which the instrumented kit does. Lyrics: give `liminal_text.place_words` entries a
`room` (and optionally `category`) and they are checked as wall text against that room's walls.

The rules (categories, affordances, poses, tolerances) are data: `avgen --validate-space --dump-rules` prints them;
pass `rules=` (a dict deep-merged over them) to change any of it, e.g. {"lyric": {"margin": 0.25}}.

CLI:  python3 tools/liminal_space.py <scene-or-project.json> [--json out.json] [--margin m] [--fix out.scene.json]
"""

from __future__ import annotations

import copy
import functools
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_AVGEN = os.path.join(ROOT, "build", "release", "src", "avgen")

# kit function -> category (the rules' names). Functions not listed are left alone.
KIT_CATEGORIES = {
    "couch": "couch", "armchair": "armchair", "chair": "chair", "table": "table", "round_table": "table",
    "coffee_table": "coffeeTable", "desk": "desk", "bed": "bed", "nightstand": "nightstand",
    "wardrobe": "wardrobe", "bookshelf": "shelf", "tv": "television", "cabinet": "cabinet",
    "floor_lamp": "lamp", "table_lamp": "tableLamp", "hanging_lamp": "hangingLamp", "window_frame": "window",
    "curtains": "curtains", "painting": "painting", "rug": "rug", "mirror_frame": "mirror", "plant": "plant",
    "wall_clock": "clock", "globe": "prop", "fridge": "fridge", "counter": "counter", "kettle": "prop",
    "plate_and_cup": "prop", "boxes": "prop", "fireplace": "fireplace", "phone_table": "table",
    "ceiling_fan": "ceilingFan", "stack_of_books": "prop", "bathtub": "bathtub", "coat_hooks": "coatHooks",
    "mannequin": "mannequin", "shell": "room", "door_frame": "door", "toilet": "toilet", "sink": "sink",
    "monitor": "monitor", "computer": "monitor", "stairway": "stairs",
    # ADR-1056: trim is checked against doors and windows (it must stop at them)
    "wall_band": "trim", "skirting": "trim",
}

_WALL_NORMALS = {"-z": [0, 0, 1], "+z": [0, 0, -1], "-x": [1, 0, 0], "+x": [-1, 0, 0]}
_counters: dict = {}


def tag(node: dict, category: str, id: str | None = None, **fields) -> dict:
    """Annotate an SDF (or composition) node as an entity. Returns the node."""
    ent = dict(node.get("entity", {}))
    if ent.get("_auto") and ent.get("category") != category:
        ent.pop("id", None)  # an inner kit call's generated id (armchair() is a couch()): name it for what it is
    ent["category"] = category
    if id is not None:
        ent["id"] = id
        ent.pop("_auto", None)
    elif "id" not in ent:
        n = _counters.get(category, 0) + 1
        _counters[category] = n
        ent["id"] = f"{category[0].upper()}{category[1:]}_{n:02d}"
        ent["_auto"] = True
    for k, v in fields.items():
        if v is not None:
            ent[k] = [list(x) if isinstance(x, tuple) else x for x in v] if isinstance(v, (list, tuple)) else v
    node["entity"] = ent
    return node


def part(node: dict, name: str) -> dict:
    """Name a component of an entity (the validator's integrity check: e.g. a mannequin's 'head')."""
    node["part"] = name
    return node


def _find_named(node, name):
    if isinstance(node, dict):
        if node.get("name") == name:
            return node
        for c in node.get("children", []):
            r = _find_named(c, name)
            if r is not None:
                return r
    return None


def instrument_kit(kit) -> None:
    """Wrap the kit's prop functions so each result is tagged with its category (idempotent). Every wrapped call
    also takes `entity={...}` (merged into the annotation: id, anchor, pose, room, ...)."""
    if getattr(kit, "_space_instrumented", False):
        return
    for fname, category in KIT_CATEGORIES.items():
        fn = getattr(kit, fname, None)
        if fn is None or not callable(fn):
            continue

        def make(fn=fn, fname=fname, category=category):
            @functools.wraps(fn)
            def wrapped(*args, entity=None, **kwargs):
                node = fn(*args, **kwargs)
                if not isinstance(node, dict):
                    return node
                extra = dict(entity or {})
                if fname == "shell":
                    ext = args[0] if args else kwargs.get("ext")
                    wall = args[1] if len(args) > 1 else kwargs.get("wall", 0.15)
                    extra.setdefault("interior", [list(ext[0]), list(ext[1]), list(ext[2])])
                    extra.setdefault("wall", wall)
                elif fname == "door_frame":
                    wall = args[0] if args else kwargs.get("wall")
                    if wall in _WALL_NORMALS:
                        extra.setdefault("normal", _WALL_NORMALS[wall])
                elif fname == "mannequin":
                    pose = args[0] if args else kwargs.get("pose", "stand")
                    scale = args[1] if len(args) > 1 else kwargs.get("scale", 1.0)
                    name = args[2] if len(args) > 2 else kwargs.get("name", "man")
                    extra.setdefault("pose", pose)
                    poses = getattr(kit, "POSES", {})
                    if pose in poses and "hip_y" in poses[pose]:
                        extra.setdefault("hip", [0.0, poses[pose]["hip_y"] * scale, 0.0])
                    head = _find_named(node, name + "Head")
                    if head is not None:
                        part(head, "head")
                    extra.setdefault("id", name)
                elif fname == "window_frame":
                    # ADR-1056: the frame and the sill are measured against the wall's opening
                    kids = node.get("children", [])
                    if len(kids) >= 3:
                        part(kids[0], "frame")
                        part(kids[2], "sill")
                    if "name" in kwargs and kwargs["name"]:
                        extra.setdefault("id", kwargs["name"])
                elif "name" in kwargs and kwargs["name"]:
                    extra.setdefault("id", kwargs["name"])
                tag(node, extra.pop("category", category), id=extra.pop("id", None), **extra)
                return node
            return wrapped

        setattr(kit, fname, make())
    kit._space_instrumented = True


def strip(scene: dict) -> dict:
    """A copy of the scene without annotations (they are harmless; this is for diffing)."""
    def walk(n):
        if isinstance(n, dict):
            n.pop("entity", None)
            n.pop("part", None)
            for v in n.values():
                walk(v)
        elif isinstance(n, list):
            for v in n:
                walk(v)
    out = copy.deepcopy(scene)
    walk(out)
    return out


def validate(scene, rules: dict | None = None, margin: float | None = None, eye: float | None = None,
             title: str | None = None, camera: bool = True, avgen: str | None = None, film: bool = False,
             fps: float | None = None, range_: str | None = None, motion: bool = True, md: str | None = None) -> dict:
    """Run the validator on a scene dict or a scene/project path. Returns the report dict (see ADR-1051):
    report["summary"] {errors, warnings, infos, pass}, report["violations"] [{severity, tier, rule, entities, message,
    measured, expected, suggestion, fix, groups}], report["entities"], report["groups"].

    film=True (a project PATH only; ADR-1057) also plays the film offline and checks the camera as placed and the
    build-lock of structural transforms; `fps`, `range_` ("a:b" seconds) and `motion` tune it. `md` writes the
    Markdown Scene Validation Report (ADR-1056) to that path."""
    exe = avgen or os.environ.get("AVGEN") or DEFAULT_AVGEN
    with tempfile.TemporaryDirectory() as d:
        if isinstance(scene, dict):
            path = os.path.join(d, "scene.json")
            with open(path, "w") as f:
                json.dump(scene, f)
        else:
            path = str(scene)
        out = os.path.join(d, "report.json")
        cmd = [exe, "--validate-space", path, "--json", out, "--text", os.path.join(d, "report.txt")]
        if rules:
            rp = os.path.join(d, "rules.json")
            with open(rp, "w") as f:
                json.dump(rules, f)
            cmd += ["--rules", rp]
        if margin is not None:
            cmd += ["--margin", str(margin)]
        if eye is not None:
            cmd += ["--eye", str(eye)]
        if title:
            cmd += ["--title", title]
        if not camera:
            cmd += ["--no-camera"]
        if film:
            if isinstance(scene, dict):
                raise ValueError("film=True needs a project path (it plays the film)")
            cmd += ["--film"]
            if fps:
                cmd += ["--fps", str(fps)]
            if range_:
                cmd += ["--range", range_]
            if not motion:
                cmd += ["--no-motion"]
        if md:
            cmd += ["--md", md]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            raise RuntimeError(f"avgen --validate-space failed ({r.returncode}): {r.stderr.strip()}")
        with open(out) as f:
            report = json.load(f)
        with open(os.path.join(d, "report.txt")) as f:
            report["text"] = f.read()
        return report


def text(report: dict) -> str:
    """The human-readable report (section 14's form)."""
    return report.get("text", "")


def _find_entity(scene: dict, eid: str):
    """(node, kind, holder, key) for the entity id: kind "sdf" (inside an SDF tree; holder[key] is the node, so it
    can be wrapped) or "node" (a composition node)."""
    def walk(holder, key):
        n = holder[key]
        if isinstance(n, dict):
            if isinstance(n.get("entity"), dict) and n["entity"].get("id") == eid:
                return n, holder, key
            for i in range(len(n.get("children", []))):
                r = walk(n["children"], i)
                if r is not None:
                    return r
        return None
    for node in scene.get("nodes", []):
        if node.get("name") == eid or (isinstance(node.get("entity"), dict) and node["entity"].get("id") == eid):
            return node, "node", None, None
        if node.get("kind") == "sdf":
            tree = node["sdf"]["tree"]
            r = walk(tree, "root") if "root" in tree else None
            if r is not None:
                return r[0], "sdf", r[1], r[2]
    return None, None, None, None


def _wrap(holder, key, kind: str, field: str, value) -> None:
    """Put a translate/rotate ABOVE the entity's node, so its frame (and the validator's reading of it) moves.
    Costs one SDF node (the limit is 96 per object)."""
    holder[key] = {"kind": kind, field: list(value), "children": [holder[key]]}


def apply_fixes(scene: dict, report: dict, rules=("floor", "lyricPlacement", "lyricClearance", "support",
                                                 "integrity", "clipped")) -> list:
    """Apply the report's machine fixes for the given rules, in place. Returns what was applied. Safe ones only:
    a translation of the offending entity, a word moved to the suggested clear wall spot, an object's march
    bounds grown. Rotations (orientation) and intersections are left to the author by default (pass
    rules={"intersection", "orientation", ...} to include them)."""
    applied = []
    moved = set()
    for v in report.get("violations", []):
        fix = v.get("fix")
        if not fix or v.get("rule") not in rules:
            continue
        if "object" in fix and "boundsMin" in fix:
            for node in scene.get("nodes", []):
                if node.get("name") == fix["object"] and node.get("kind") == "sdf":
                    bmin, bmax = node["sdf"].get("boundsMin", fix["boundsMin"]), node["sdf"].get("boundsMax", fix["boundsMax"])
                    node["sdf"]["boundsMin"] = [min(a, b) for a, b in zip(bmin, fix["boundsMin"])]
                    node["sdf"]["boundsMax"] = [max(a, b) for a, b in zip(bmax, fix["boundsMax"])]
                    applied.append({"rule": v["rule"], "object": fix["object"], "bounds": [node["sdf"]["boundsMin"], node["sdf"]["boundsMax"]]})
            continue
        eid = fix.get("id")
        if not eid or eid in moved:
            continue
        node, kind, holder, key = _find_entity(scene, eid)
        if node is None:
            continue
        if "translate" in fix:
            if kind == "node":
                node["position"] = [a + b for a, b in zip(node.get("position", [0, 0, 0]), fix["translate"])]
            else:
                _wrap(holder, key, "translate", "translation", fix["translate"])
        elif "position" in fix and kind == "node":
            node["position"] = list(fix["position"])
            if "normal" in fix:
                n = fix["normal"]
                import math
                node["rotation"] = [0.0, math.degrees(math.atan2(n[0], n[2])), 0.0]
        elif "yaw" in fix and kind == "sdf":
            _wrap(holder, key, "rotate", "rotation", [0.0, fix["yaw"], 0.0])
        else:
            continue
        moved.add(eid)
        applied.append({"rule": v["rule"], "id": eid, "fix": fix})
    return applied


def selftest() -> int:
    """The instrumented kit tags what the validator measures: window frame and sill parts, trim, rooms."""
    sys.path.insert(0, os.path.join(ROOT, "tools", "liminal"))
    import importlib
    kit = importlib.import_module("kit")
    instrument_kit(kit)
    w = kit.window_frame(1.2, 1.4, 0.9)
    parts = [c.get("part") for c in w.get("children", [])]
    assert w["entity"]["category"] == "window", w.get("entity")
    assert parts[0] == "frame" and parts[2] == "sill", parts
    ext = ((-2.0, 2.0), (0.0, 2.7), (-1.5, 1.5))
    band = kit.wall_band(ext, 0.88, 0.92)
    assert band["entity"]["category"] == "trim", band.get("entity")
    sk = kit.skirting(ext)
    assert sk["entity"]["category"] == "trim", sk.get("entity")
    room = kit.shell(ext, 0.15, entity={"id": "lab"})
    assert room["entity"]["interior"] == [list(e) for e in ext] and room["entity"]["wall"] == 0.15
    print("liminal_space selftest: ok (window frame/sill parts, trim, room interior)")
    return 0


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scene", nargs="?")
    ap.add_argument("--json")
    ap.add_argument("--margin", type=float)
    ap.add_argument("--eye", type=float)
    ap.add_argument("--rules")
    ap.add_argument("--fix", help="write the scene with the safe fixes applied to this path")
    ap.add_argument("--film", action="store_true", help="also play the film (a project): camera and build-lock (ADR-1057)")
    ap.add_argument("--fps", type=float)
    ap.add_argument("--range", dest="range_")
    ap.add_argument("--md", help="write the Markdown Scene Validation Report here")
    ap.add_argument("--selftest", action="store_true", help="check the kit instrumentation (no avgen needed)")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.scene:
        ap.error("a scene or project path is required")
    rules = json.load(open(a.rules)) if a.rules else None
    report = validate(a.scene, rules=rules, margin=a.margin, eye=a.eye, film=a.film, fps=a.fps, range_=a.range_, md=a.md)
    print(text(report))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(report, f, indent=1)
    if a.fix:
        src = a.scene
        doc = json.load(open(src))
        if doc.get("format") == "avgen-project":
            src = os.path.join(os.path.dirname(src), doc["assets"]["scene"]["path"])
            doc = json.load(open(src))
        applied = apply_fixes(doc, report)
        with open(a.fix, "w") as f:
            json.dump(doc, f, indent=1)
        print(f"applied {len(applied)} fix(es) -> {a.fix}")
    return 1 if report["summary"]["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
