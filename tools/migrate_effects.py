#!/usr/bin/env python3
"""One-shot converter from the pre-ADR-702 effect format to the canonical `effects` array.

Before ADR-702 a scene or project carried two effect lists:

  * `worldEffects`       -- ADR-207's surface waves (camera travel beam, hero pulse), parameters
                            under `worldfx/<name>/<leaf>`;
  * `atmosphericEffects` -- ADR-230's sky and medium effects (comet, aurora, meteor shower, vortex,
                            fog bank, tornado), parameters under `atmos/<name>/<leaf>`.

ADR-702 replaced both with one `effects` array of instances, each with a stable `id`, a `type`, an
`owner` and an `order`, and one parameter prefix, `fx/<id>/<leaf>`. ADR-441 says a format change in
heavy development converts the tracked content rather than keeping a reader for the old shape, so
this script is run once over the repository and the engine reads only the new format (it names the
old keys in a warning rather than guessing at them).

What it does, per file:

  1. Converts every entry of both lists into an instance (see `wave_instance` / `sky_instance`).
  2. Expands a hero pulse -- a `worldEffects` entry whose source is `focusHero` and whose activation
     is `heroFocus` -- into ONE INSTANCE PER HERO, owned by that hero, with an `owner` source. The
     old single record fired on whichever hero the cut held; the per-hero instances fire only for
     their own hero (`resolveActivationWindow`'s named-subject rule), so together they draw exactly
     what the one record drew, while each can be tuned and modulated on its own. ADR-702 §19.
  3. Rewrites every `worldfx/<name>/` and `atmos/<name>/` path anywhere in the document -- a route
     target, a timeline track, a preset member, a macro target, a control binding, a `parameters`
     key -- to `fx/<id>/`. A path that pointed at the expanded hero pulse is duplicated once per
     hero, so a beat route onto the pulse's intensity becomes one route per hero's pulse.
  4. For a project, uses the lists and heroes that are EFFECTIVE for it (its own when it has them,
     its scene's otherwise), writes an explicit `effects` array whenever it overrode either list or
     the heroes (so a hero-pulse expansion follows the project's heroes), and refreshes the scene's
     recorded sha256/size after the scene has been converted.

Idempotent: a file with no old keys and no old paths is left byte-for-byte alone.

    tools/migrate_effects.py [--check] [paths...]     (default: every .json under examples/ and assets/)
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import sys
from collections import OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The old `kind` spelling is already the new `type` spelling for every sky/medium kind: ADR-500's
# schema `key` is what both wrote, and ADR-702 kept the keys.
SKY_TYPES = {"comet", "aurora", "vortex", "meteors", "fog", "tornado"}
GROUND_GLOW_TYPES = {"comet", "aurora", "meteors"}


def slug(text: str) -> str:
    """Mirrors `slug` in src/world/effects/effect_stack.cpp."""
    out = []
    dash = False
    for c in text:
        if c.isascii() and c.isalnum():
            out.append(c.lower())
            dash = False
        elif c in "_.":
            out.append(c)
            dash = False
        elif out and not dash:
            out.append("-")
            dash = True
    s = "".join(out).rstrip("-")
    return s or "effect"


def unique(taken: set, base: str) -> str:
    root = slug(base)
    if root not in taken:
        taken.add(root)
        return root
    n = 2
    while f"{root}-{n}" in taken:
        n += 1
    taken.add(f"{root}-{n}")
    return f"{root}-{n}"


class Converted:
    def __init__(self):
        self.effects = []
        self.taken = set()
        self.order = {}  # owner key -> next order
        # old prefix ("worldfx/Hero Pulse/") -> list of new prefixes ("fx/rook-hero-pulse/", ...)
        self.mapping = OrderedDict()

    def next_order(self, owner: dict) -> int:
        key = (owner["kind"], owner.get("name", ""))
        n = self.order.get(key, 0)
        self.order[key] = n + 1
        return n

    def add(self, inst: dict, old_prefix: str):
        self.effects.append(inst)
        self.mapping.setdefault(old_prefix, []).append(f"fx/{inst['id']}/")


def wave_parameters(e: dict, source: dict) -> dict:
    params = OrderedDict()
    for block in ("propagation", "appearance", "sparkle", "response"):
        if block in e:
            params[block] = copy.deepcopy(e[block])
    params["source"] = source
    if "target" in e:
        params["target"] = copy.deepcopy(e["target"])
    return params


def wave_instance(e: dict, conv: Converted, owner: dict, source: dict, id_base: str) -> dict:
    src_kind = e.get("source", {}).get("kind", "world")
    wave_type = "travelBeam" if src_kind == "camera" else "groundPulse"
    inst = OrderedDict()
    inst["id"] = unique(conv.taken, id_base)
    inst["type"] = wave_type
    inst["name"] = e.get("name", "")
    inst["owner"] = owner
    inst["enabled"] = e.get("enabled", True)
    inst["order"] = conv.next_order(owner)
    if e.get("style"):
        inst["style"] = e["style"]
    inst["activation"] = e.get("activation", "always")
    if "timing" in e:
        inst["timing"] = copy.deepcopy(e["timing"])
    inst["parameters"] = wave_parameters(e, source)
    return inst


def sky_instance(e: dict, conv: Converted) -> dict:
    kind = e.get("kind", "comet")
    if kind not in SKY_TYPES:
        raise SystemExit(f"unknown atmospheric kind '{kind}'")
    owner = OrderedDict([("kind", "world")])
    inst = OrderedDict()
    inst["id"] = unique(conv.taken, e.get("name", kind))
    inst["type"] = kind
    inst["name"] = e.get("name", "")
    inst["owner"] = owner
    inst["enabled"] = e.get("enabled", True)
    inst["order"] = conv.next_order(owner)
    if e.get("style"):
        inst["style"] = e["style"]
    inst["activation"] = e.get("activation", "always")
    if "timing" in e:
        inst["timing"] = copy.deepcopy(e["timing"])
    # The shared rows live at the root and keep their shape. `ground` only for a type that lights
    # the ground; `flow` for every sky/medium type (see `sharedFieldApplies`).
    if kind in GROUND_GLOW_TYPES and "ground" in e:
        inst["ground"] = copy.deepcopy(e["ground"])
    if "flow" in e:
        inst["flow"] = copy.deepcopy(e["flow"])
    # Only the instance's own type's block survives: every old entry carried every kind's payload.
    inst["parameters"] = copy.deepcopy(e.get(kind, {}))
    return inst


def is_hero_pulse(e: dict) -> bool:
    return e.get("source", {}).get("kind") == "focusHero" and e.get("activation") == "heroFocus"


def convert_lists(world: list, atmos: list, heroes: list) -> Converted:
    conv = Converted()
    for e in atmos:
        conv.add(sky_instance(e, conv), f"atmos/{e.get('name', '')}/")
    for e in world:
        name = e.get("name", "")
        old_prefix = f"worldfx/{name}/"
        if is_hero_pulse(e) and heroes:
            src = copy.deepcopy(e.get("source", {}))
            src["kind"] = "owner"
            src.pop("name", None)
            for hero in heroes:
                owner = OrderedDict([("kind", "entity"), ("name", hero["name"])])
                conv.add(wave_instance(e, conv, owner, copy.deepcopy(src), f"{hero['name']} {name}"), old_prefix)
        else:
            owner = OrderedDict([("kind", "world")])
            conv.add(wave_instance(e, conv, owner, copy.deepcopy(e.get("source", {"kind": "world"})), name),
                     old_prefix)
    return conv


OLD_PATH = re.compile(r"^(worldfx|atmos)/")


def new_prefixes_for(value: str, mapping: dict):
    for old, news in mapping.items():
        if value.startswith(old):
            return [n + value[len(old):] for n in news]
    m = re.match(r"^(worldfx|atmos)/([^/]+)/(.*)$", value)
    if m:
        # A path naming an effect this document does not have. It was dead before and stays dead,
        # but in the new namespace, so the warning a load prints names a path somebody can find.
        return [f"fx/{slug(m.group(2))}/{m.group(3)}"]
    return None


def rewrite(node, mapping: dict):
    """Rewrites old paths everywhere, duplicating array elements and object keys that referred to an
    effect that expanded into several instances. Returns the rewritten node."""
    if isinstance(node, str):
        news = new_prefixes_for(node, mapping)
        return news[0] if news else node
    if isinstance(node, list):
        out = []
        for child in node:
            variants = expand(child, mapping)
            out.extend(variants)
        return out
    if isinstance(node, dict):
        out = OrderedDict()
        for key, value in node.items():
            news = new_prefixes_for(key, mapping) if isinstance(key, str) else None
            if news:
                for k in news:
                    out[k] = rewrite(copy.deepcopy(value), mapping)
            else:
                out[key] = rewrite(value, mapping)
        return out
    return node


def references(node, mapping, found):
    if isinstance(node, str):
        for old, news in mapping.items():
            if node.startswith(old) and len(news) > 1:
                found.add(old)
    elif isinstance(node, list):
        for c in node:
            references(c, mapping, found)
    elif isinstance(node, dict):
        for k, v in node.items():
            references(k, mapping, found)
            references(v, mapping, found)


def expand(element, mapping):
    """An array element that names an expanded effect becomes one element per instance."""
    found = set()
    references(element, mapping, found)
    if not found:
        return [rewrite(element, mapping)]
    old = sorted(found)[0]
    out = []
    for new in mapping[old]:
        single = OrderedDict(mapping)
        single[old] = [new]
        out.append(rewrite(copy.deepcopy(element), single))
    return out


def top_level_spans(text: str):
    """(key, key_start, value_start, value_end) for every top-level member, from the raw text, so a
    member that did not change can be written back byte for byte. `key_start` is the opening quote
    of the key; `value_end` is one past the value."""
    spans = []
    i = text.index("{") + 1
    depth = 1
    n = len(text)
    key = None
    key_start = None
    value_start = None
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            if depth == 1 and key is None:
                key = json.loads(text[i:j + 1])
                key_start = i
            i = j + 1
            continue
        if depth == 1 and key is not None and value_start is None and c == ":":
            k = i + 1
            while text[k] in " \t\r\n":
                k += 1
            value_start = k
            i = k
            continue
        if c in "[{":
            depth += 1
        elif c in "]}":
            depth -= 1
            if depth == 0:
                if key is not None:
                    end = i
                    while text[end - 1] in " \t\r\n":
                        end -= 1
                    spans.append((key, key_start, value_start, end))
                break
        elif c == "," and depth == 1:
            end = i
            while text[end - 1] in " \t\r\n":
                end -= 1
            spans.append((key, key_start, value_start, end))
            key = key_start = value_start = None
        i += 1
    return spans


def indent_unit(text: str) -> int:
    for line in text.splitlines()[1:]:
        stripped = line.lstrip(" ")
        if stripped and len(line) != len(stripped):
            return len(line) - len(stripped)
    return 2


def dump_member_value(value, unit: int, sort_keys: bool) -> str:
    body = json.dumps(value, indent=unit, ensure_ascii=False, sort_keys=sort_keys)
    return body.replace("\n", "\n" + " " * unit)


def surgical(original_text: str, new_doc: dict, sort_keys: bool) -> str:
    """Writes `new_doc` by editing `original_text`: members whose value is unchanged keep their
    bytes; changed members are re-dumped in the file's own indent; removed members are cut; an added
    member (`effects`) is placed where the first removed one was, or last."""
    old_doc = json.loads(original_text, object_pairs_hook=OrderedDict)
    spans = top_level_spans(original_text)
    unit = indent_unit(original_text)
    pad = " " * unit
    pieces = []
    added = [k for k in new_doc if k not in old_doc]
    placed = False
    last_end = None
    first_start = spans[0][1] if spans else None
    out_members = []
    for key, kstart, vstart, vend in spans:
        if key not in new_doc:
            if not placed and added:
                for a in added:
                    out_members.append(json.dumps(a) + ": " + dump_member_value(new_doc[a], unit, sort_keys))
                placed = True
            continue
        if new_doc[key] == old_doc[key]:
            out_members.append(original_text[kstart:vend])
        else:
            out_members.append(original_text[kstart:vstart] + dump_member_value(new_doc[key], unit, sort_keys))
    if not placed and added:
        for a in added:
            out_members.append(json.dumps(a) + ": " + dump_member_value(new_doc[a], unit, sort_keys))
    head = original_text[:first_start]
    tail_start = spans[-1][3]
    tail = original_text[tail_start:]
    return head + (",\n" + pad).join(out_members) + tail


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f, object_pairs_hook=OrderedDict)


def dump(doc, path, sort_keys):
    text = json.dumps(doc, indent=2, ensure_ascii=False, sort_keys=sort_keys) + "\n"
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest(), os.path.getsize(path)


def has_old(doc) -> bool:
    if not isinstance(doc, dict):
        return False
    if "worldEffects" in doc or "atmosphericEffects" in doc:
        return True
    return re.search(r'"(worldfx|atmos)/', json.dumps(doc)) is not None


def insert_effects(doc: OrderedDict, effects: list, sort_keys: bool) -> OrderedDict:
    """Replaces the two old keys with `effects`, at the first old key's position."""
    out = OrderedDict()
    placed = False
    for k, v in doc.items():
        if k in ("worldEffects", "atmosphericEffects"):
            if not placed and effects is not None:
                out["effects"] = effects
                placed = True
            continue
        out[k] = v
    if not placed and effects is not None:
        out["effects"] = effects
    return out


def scene_of(project_path: str, project: dict):
    ref = project.get("assets", {}).get("scene", {})
    if not isinstance(ref, dict) or ref.get("kind") != "composition":
        return None
    p = ref.get("path")
    rel = p.get("path") if isinstance(p, dict) else p
    if not rel:
        return None
    return os.path.normpath(os.path.join(os.path.dirname(project_path), rel))


def is_sorted(doc: dict) -> bool:
    keys = list(doc.keys())
    return keys == sorted(keys)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--check", action="store_true", help="report what would change, write nothing")
    args = ap.parse_args()

    paths = args.paths
    if not paths:
        paths = []
        for base in ("examples", "assets"):
            for dirpath, _, files in os.walk(os.path.join(ROOT, base)):
                for f in files:
                    if f.endswith(".json"):
                        paths.append(os.path.join(dirpath, f))
    paths = sorted(os.path.abspath(p) for p in paths)

    docs = {}
    for p in paths:
        try:
            d = load(p)
        except Exception:
            continue
        if isinstance(d, dict):
            docs[p] = d

    originals = {p: copy.deepcopy(d) for p, d in docs.items()}

    def original_scene(path):
        if path in originals:
            return originals[path]
        if os.path.exists(path):
            try:
                return load(path)
            except Exception:
                return None
        return None

    changed = []
    projects = [p for p, d in docs.items() if d.get("format") == "avgen-project"]
    scenes = [p for p, d in docs.items() if d.get("format") != "avgen-project"]

    # Scenes first, in memory: a project's conversion needs its scene's mapping and heroes.
    scene_conv = {}
    for p in scenes:
        d = docs[p]
        if not has_old(d):
            continue
        conv = convert_lists(d.get("worldEffects", []), d.get("atmosphericEffects", []), d.get("heroes", []))
        scene_conv[p] = conv
        out = insert_effects(d, conv.effects if conv.effects else None, False)
        out = rewrite(out, conv.mapping)
        docs[p] = out
        changed.append(p)

    for p in projects:
        d = docs[p]
        if not has_old(d):
            continue
        scene_path = scene_of(p, d)
        scene = original_scene(scene_path) if scene_path else None
        scene = scene if isinstance(scene, dict) else {}
        owns_world = "worldEffects" in d
        owns_atmos = "atmosphericEffects" in d
        owns_heroes = "heroes" in d
        world = d.get("worldEffects", scene.get("worldEffects", []))
        atmos = list(d.get("atmosphericEffects", scene.get("atmosphericEffects", [])))
        heroes = d.get("heroes", scene.get("heroes", []))
        # ADR-387 §19's load-time carry-over, done once here instead of on every load: a project list
        # REPLACES the scene's, so a project saved before the vortex was an effect lost the scene's.
        if owns_atmos and not any(e.get("kind") == "vortex" for e in atmos):
            for e in scene.get("atmosphericEffects", []):
                if e.get("kind") == "vortex":
                    atmos.append(copy.deepcopy(e))
                    break
        conv = convert_lists(world, atmos, heroes)
        explicit = owns_world or owns_atmos or (owns_heroes and any(is_hero_pulse(e) for e in world))
        out = insert_effects(d, conv.effects if explicit else None, is_sorted(d))
        out = rewrite(out, conv.mapping)
        if is_sorted(d):
            out = OrderedDict(sorted(out.items()))
        docs[p] = out
        changed.append(p)

    if args.check:
        for p in changed:
            print("would convert", os.path.relpath(p, ROOT))
        return 0

    for p in changed:
        # Surgically: a member that did not change keeps its bytes, so a hand-formatted scene is not
        # reformatted wholesale. A project the engine wrote has every object's keys sorted
        # (nlohmann's default map), so its re-dumped members are sorted too.
        with open(p, "r", encoding="utf-8") as f:
            text = f.read()
        sort_keys = docs[p].get("format") == "avgen-project" and is_sorted(originals[p])
        if sort_keys:
            docs[p] = OrderedDict(sorted(docs[p].items()))
        with open(p, "w", encoding="utf-8") as f:
            f.write(surgical(text, docs[p], sort_keys))
        print("converted", os.path.relpath(p, ROOT))

    # Refresh the scene hash a project recorded, now that the scene's bytes changed.
    for p in projects:
        d = load(p)
        scene_path = scene_of(p, d)
        ref = d.get("assets", {}).get("scene", {}).get("path")
        if not scene_path or not isinstance(ref, dict) or not os.path.exists(scene_path):
            continue
        if scene_path not in changed:
            continue
        digest, size = sha256(scene_path)
        if ref.get("sha256") != digest or ref.get("size") != size:
            with open(p, "r", encoding="utf-8") as f:
                text = f.read()
            old_digest = re.escape(json.dumps(ref.get("sha256")))
            text = re.sub(old_digest, json.dumps(digest), text, count=1)
            text = re.sub(r'("size":\s*)' + str(ref.get("size")) + r"\b", r"\g<1>" + str(size), text, count=1)
            with open(p, "w", encoding="utf-8") as f:
                f.write(text)
            print("rehashed", os.path.relpath(p, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
