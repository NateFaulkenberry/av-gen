#!/usr/bin/env python3
"""Splice the farm-animal fragment `avgen_place_farm_animals` produced into a scene file.

Order- and format-preserving, for the reason `refresh_scene_fingerprint.py` gives: reading the
scene into a JSON library and writing it back reorders every key and reformats every float, which
turns an eighteen-node addition into a nine-thousand-line diff nobody can review. `object_pairs_hook`
keeps the order, and Python's float repr round-trips every number it did not touch, so the diff is
exactly the nodes and entities this adds.

Idempotent: it removes whatever a previous run added -- matched on the name, which is
`<species>-<n>` and is the same rule the placer uses -- before appending, so re-placing is a replace
rather than a pile. Matched on the name rather than on a marker key because a marker key on a node
is a key the scene parser would have to be taught about, and a format grows a field for the benefit
of a tool exactly once before it grows a second one.

    tools/place_farm_animals.sh          # does both halves
    tools/splice_farm_animals.py <fragment.json> [scene.json]
"""
import collections
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SCENE = os.path.join(REPO, 'examples/world/glowmere-valley-2.scene.json')

fragment_path = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
scene_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_SCENE

od = collections.OrderedDict
fragment = json.load(open(fragment_path), object_pairs_hook=od)
scene = json.load(open(scene_path), object_pairs_hook=od)

SPECIES = ('bull', 'cow', 'horse', 'pig', 'sheep', 'goat', 'chicken', 'rooster', 'chick')


def is_farm(item):
    name = item.get('name', '')
    head, _, tail = name.rpartition('-')
    return head in SPECIES and tail.isdigit()


for key in ('nodes', 'entities'):
    existing = [item for item in scene.get(key, []) if not is_farm(item)]
    added = fragment.get(key, [])
    scene[key] = existing + added
    print('%s: %d kept, %d farm' % (key, len(existing), len(added)))

with open(scene_path, 'w') as out:
    json.dump(scene, out, indent=1)
print('wrote %s' % scene_path)
