#!/usr/bin/env python3
"""A live Sonic VFX scene as a FILE project: the same scene and instrument, driven by a recording (a WAV and a MIDI
file) instead of the live inputs, for review renders and the test matrix (ADR-1025: the file path computes the same
character and context as live).

    python3 tools/sonic_vfx/variant.py examples/sonic-vfx/event-horizon.json --wav a.wav --mid n.mid --out v.json

The scene file is referenced in place (absolute path), so a variant never copies the world.
"""
import argparse
import json
import os


def make_variant(project, wav, mid, out, duration=None):
    with open(project) as f:
        doc = json.load(f)
    base = os.path.dirname(os.path.abspath(project))
    scene = doc["assets"]["scene"]["path"]
    if isinstance(scene, dict):
        scene = scene["path"]
    doc["assets"]["scene"] = {"kind": "composition", "path": os.path.join(base, scene)}
    if wav:
        doc["assets"]["audio"] = {"path": os.path.abspath(wav)}
    sonic = doc.setdefault("sonic", {})
    sonic.pop("live", None)
    if mid:
        sonic["notes"] = os.path.abspath(mid)
    if duration:
        doc.setdefault("render", {})["end"] = float(duration)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("--wav")
    ap.add_argument("--mid")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    print(make_variant(a.project, a.wav, a.mid, a.out))


if __name__ == "__main__":
    main()
