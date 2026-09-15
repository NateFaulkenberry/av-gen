#!/usr/bin/env python3
"""Write the four world-effect measurement arms beside the Glowmere scene, then delete them.

The arms ADR-205 measures: the shipped scene with no effects declared, with the beam only, with the
pulse only, and with both. Generated rather than committed for the reason `make_bench_scenes.py`
gives -- four near-copies of a 128 KB scene is half a megabyte of duplicate in exchange for one
script -- and written into `examples/world/` rather than a subdirectory because the scene's light
rig, material programs and entity profiles are all `../`-relative and a scene one level deeper
resolves none of them.

Each arm pins its effects to `window` activation over the whole timeline and to the scene's own
(static) camera, so every measured frame has the effect **actually on screen**. An arm whose effect
never activates would produce a plausible number and measure nothing (ADR-182).

    tools/make_fx_bench_scenes.py          # write examples/world/_fx{none,beam,pulse,both}.scene.json
    tools/make_fx_bench_scenes.py --clean  # remove them again
"""
import collections
import copy
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD = os.path.join(REPO, 'examples', 'world')
SRC = os.path.join(WORLD, 'glowmere-valley-2.scene.json')
ARMS = ('none', 'beam', 'pulse', 'both')


def path_for(arm):
    return os.path.join(WORLD, '_fx%s.scene.json' % arm)


def main():
    if '--clean' in sys.argv:
        for arm in ARMS:
            if os.path.exists(path_for(arm)):
                os.remove(path_for(arm))
        print('removed %d arm(s)' % len(ARMS))
        return

    doc = json.load(open(SRC), object_pairs_hook=collections.OrderedDict)
    beam = copy.deepcopy(next(e for e in doc['worldEffects'] if e['name'].startswith('Camera')))
    pulse = copy.deepcopy(next(e for e in doc['worldEffects'] if e['name'].startswith('Hero')))

    beam['activation'] = 'window'
    beam['timing'] = {'delay': 0.0, 'lifetime': 0.0, 'fadeIn': 0.0, 'fadeOut': 0.0,
                      'windowStart': 0.0, 'windowSeconds': 600.0, 'repeatSeconds': 3.0}
    beam['source'] = {'kind': 'camera', 'position': [0.0, 0.0, 0.0]}
    beam.pop('target', None)
    beam['propagation']['direction'] = 'cameraForward'

    pulse['activation'] = 'window'
    pulse['source'] = {'kind': 'node', 'name': 'elder-2-cap', 'groundOffset': 0.0}
    pulse['timing'] = {'delay': 0.0, 'lifetime': 0.0, 'fadeIn': 0.0, 'fadeOut': 0.0,
                       'windowStart': 0.0, 'windowSeconds': 600.0, 'repeatSeconds': 4.5}

    effects = {'none': [], 'beam': [beam], 'pulse': [pulse], 'both': [beam, pulse]}
    for arm in ARMS:
        out = collections.OrderedDict()
        for key, value in doc.items():
            if key == 'worldEffects':
                continue
            out[key] = value
            if key == 'heroes' and effects[arm]:
                out['worldEffects'] = effects[arm]
        out['name'] = 'fx-' + arm
        json.dump(out, open(path_for(arm), 'w'), indent=1)
        print('%s: %d effect(s)' % (os.path.basename(path_for(arm)), len(effects[arm])))


if __name__ == '__main__':
    main()
