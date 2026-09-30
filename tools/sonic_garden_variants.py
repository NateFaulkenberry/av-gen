#!/usr/bin/env python3
"""Sonic Garden variants (docs/prototypes/sonic-garden): one master project, one file per test sound.

The master is examples/sonic-garden/sonic-garden.json: its scene, interpreter mappings, routes, `sonic` block and
parameters are the Sonic Garden. Every variant is that document with only the audio and the notes swapped, so the
§34 comparison is honest by construction: the four sounds cannot drift into four different gardens. Edit the
master, then run this.

    python3 tools/sonic_garden_variants.py [--width 1280 --height 720]

Writes examples/sonic-garden/variants/<name>.json for the phrase through each sound (pad, bell, bass, perc), the
§35 context sequences (context-pad, context-bell) and the §36 morph. The audio comes from
tools/make_sonic_material.py.
"""
import argparse
import copy
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GARDEN = os.path.join(REPO, "examples", "sonic-garden")

VARIANTS = {
    "pad": ("sonic-pad.wav", "phrase.mid"),
    "bell": ("sonic-bell.wav", "phrase.mid"),
    "bass": ("sonic-bass.wav", "phrase.mid"),
    "perc": ("sonic-perc.wav", "phrase.mid"),
    "context-pad": ("sonic-context-pad.wav", "context.mid"),
    "context-bell": ("sonic-context-bell.wav", "context.mid"),
    "morph": ("sonic-morph.wav", "morph.mid"),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--width", type=int, default=None)
    parser.add_argument("--height", type=int, default=None)
    parser.add_argument("--out", default=os.path.join(GARDEN, "variants"))
    args = parser.parse_args()

    with open(os.path.join(GARDEN, "sonic-garden.json")) as f:
        master = json.load(f)
    os.makedirs(args.out, exist_ok=True)
    rel = os.path.relpath(GARDEN, args.out)
    audio_rel = os.path.relpath(os.path.join(REPO, "assets", "audio"), args.out)
    for name, (wav, mid) in VARIANTS.items():
        doc = copy.deepcopy(master)
        doc["app"] = {"name": "Sonic Garden: " + name}
        doc["assets"]["audio"] = {"path": os.path.join(audio_rel, wav)}
        doc["assets"]["scene"]["path"] = os.path.join(rel, master["assets"]["scene"]["path"])
        doc["sonic"]["notes"] = os.path.join(rel, "notes", mid)
        render = doc.setdefault("render", {})
        if args.width:
            render["width"] = args.width
        if args.height:
            render["height"] = args.height
        render["path"] = os.path.join("renders", "sonic-garden-" + name)
        with open(os.path.join(args.out, name + ".json"), "w") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
        print(os.path.join(args.out, name + ".json"))


if __name__ == "__main__":
    main()
