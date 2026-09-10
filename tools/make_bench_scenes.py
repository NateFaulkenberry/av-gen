#!/usr/bin/env python3
"""Writes the deterministic benchmark variants into examples/world/_bench.

Run this before tools/bench_ab.sh; the variants are derived rather than committed, because a
benchmark comparing against a stale copy of the world is measuring the copy.

Each variant is the shipped world with exactly one thing changed, so a wall-clock difference between
two of them is attributable to that thing. The scenes live one directory deeper than the one they
were copied from, so every relative asset path is rewritten -- getting that wrong silently produces
a scene with no ecology in it, which benchmarks very well.
"""
import copy, json, os, sys

SRC = 'examples/world/terrain.scene.json'
OUT = 'examples/world/_bench'


def rebase(d):
    for layer in d['nodes'][0].get('scatter', []):
        layer['asset'] = layer['asset'].replace('../../', '../../../')
    if not d['lightRig'].startswith('../../'):
        d['lightRig'] = '../' + d['lightRig']
    return d


def main():
    base = json.load(open(SRC))
    os.makedirs(OUT, exist_ok=True)
    rig = json.load(open('examples/lightrigs/valley-moon.rig.json'))
    noshadow = copy.deepcopy(rig)
    noshadow['lights'][0]['castsShadow'] = False
    json.dump(noshadow, open('examples/lightrigs/_noshadow.rig.json', 'w'), indent=1)

    def write(name, fn):
        d = copy.deepcopy(base)
        fn(d)
        json.dump(rebase(d), open(os.path.join(OUT, name + '.scene.json'), 'w'), indent=1)

    def strip(d):
        d['nodes'][0].pop('scatter', None)

    write('full', lambda d: None)
    write('noeco', strip)
    write('noshadow', lambda d: d.update(lightRig='../../lightrigs/_noshadow.rig.json'))
    write('novol', lambda d: d['environment'].update(volumeDensity=0.0))
    write('nowater', lambda d: d['nodes'][0]['terrain'].update(water={'enabled': False}))
    write('base', strip)
    write('base_novol', lambda d: (strip(d), d['environment'].update(volumeDensity=0.0)))
    write('base_nobloom', lambda d: (strip(d), d.setdefault('post', {}).update(bloomEnabled=False)))
    write('base_bare', lambda d: (strip(d), d['environment'].update(volumeDensity=0.0),
                                  d.setdefault('post', {}).update(bloomEnabled=False)))
    # The layer-count ladder the per-layer slope is measured on. n0 and n11 are the ends of it and
    # duplicate noeco and full, but a sweep reads better when every point is named the same way, and
    # bench_ab.sh takes scene names.
    for n in (0, 1, 3, 6, 11):
        write('n%d' % n, lambda d, n=n: d['nodes'][0].__setitem__('scatter', d['nodes'][0]['scatter'][:n]))
    print('wrote', len(os.listdir(OUT)), 'variants to', OUT)


if __name__ == '__main__':
    sys.exit(main())
