#!/usr/bin/env python3
"""Write the hypothesis-B arms of the QA pass (docs/qa-pass/perf.md): copies of a Glowmere Valley 3
project with one category of project state removed each, so each category's frame cost can be
measured against the untouched copy.

    tools/make_gv3_state_arms.py examples/world/_qa-gv3-r7b.json        # every arm, beside it
    tools/make_gv3_state_arms.py examples/world/_qa-gv3-r7b.json --clean

Arms are written as `_qa-b-<arm>.json` + `_qa-b-<arm>.scene.json` next to the source project, each
pointing at its own scene with the fingerprint recomputed. They are scratch: never commit them (the
source project names a song by absolute path). The source files are only read.

Every arm removes the category *and every reference to it* (parameters, routes and timeline tracks
naming a removed effect, node or entity), so a load does not refuse orphans and the arm measures the
category rather than a broken project.
"""
import argparse, copy, hashlib, json, os, sys

ARMS = {}


def arm(fn):
    ARMS[fn.__name__.replace('_', '-')] = fn
    return fn


def drop_refs(p, pred):
    """Remove parameters, routes and timeline tracks whose target path satisfies pred(path)."""
    p['parameters'] = {k: v for k, v in p.get('parameters', {}).items() if not pred(k)}
    p['routes'] = [r for r in p.get('routes', []) if not pred(r.get('target', ''))]
    tl = p.get('timeline', {})
    tl['tracks'] = [t for t in tl.get('tracks', []) if not pred(t.get('target', ''))]


@arm
def control(p, s):
    """Nothing removed: the arm-generation round trip itself, which must measure as the source."""


@arm
def no_groundpulse(p, s):
    """The hero-pulse groundPulse effects (15 in the project, 11 in the scene)."""
    gone = {e['id'] for e in p['effects'] + s['effects'] if e.get('type') == 'groundPulse'}
    p['effects'] = [e for e in p['effects'] if e.get('type') != 'groundPulse']
    s['effects'] = [e for e in s['effects'] if e.get('type') != 'groundPulse']
    drop_refs(p, lambda t: t.startswith('fx/') and t.split('/')[1] in gone)


@arm
def no_effects(p, s):
    """Every effect instance, project and scene."""
    p['effects'] = []
    s['effects'] = []
    drop_refs(p, lambda t: t.startswith('fx/'))


@arm
def no_staging(p, s):
    """Project staging, scene staging and the two directing plans."""
    p.pop('staging', None)
    p.pop('directingPlans', None)
    s['staging'] = {'actors': [], 'scenarios': []}
    drop_refs(p, lambda t: t.startswith('staging/'))


@arm
def no_routes(p, s):
    """Every modulation route."""
    p['routes'] = []


@arm
def no_automation(p, s):
    """Every timeline track that is not a camera track (camera tracks are the cut itself)."""
    tl = p.get('timeline', {})
    tl['tracks'] = [t for t in tl.get('tracks', []) if t.get('target', '').split('/')[0] in ('cameras', 'camera')]


@arm
def no_scout(p, s):
    """GV3's additions to GV2's world: the scout, its beam, the two spatial fields."""
    names = {'scout', 'scout-beam', 'elder-rings', 'drop-ring'}
    s['nodes'] = [n for n in s['nodes'] if n['name'] not in names]
    s['entities'] = [e for e in s['entities'] if e['name'] not in names]
    for n in s['nodes']:
        pr = n.get('procedural') or {}
        if pr.get('emissiveField') in names:
            pr.pop('emissiveField', None)
            pr.pop('emissiveFieldAmount', None)
    for key in ('heroes',):
        s[key] = [h for h in s.get(key, []) if h.get('name') not in names]
        p[key] = [h for h in p.get(key, []) if h.get('name') not in names]
    p['effects'] = [e for e in p['effects'] if (e.get('owner') or {}).get('name') not in names]
    s['effects'] = [e for e in s['effects'] if (e.get('owner') or {}).get('name') not in names]
    if 'staging' in p:
        p['staging']['actors'] = [a for a in p['staging'].get('actors', []) if a.get('name') not in names and a.get('body') not in names]
        p['staging']['scenarios'] = [x for x in p['staging'].get('scenarios', []) if x.get('actor') not in names]
    s['staging'] = {'actors': [a for a in s.get('staging', {}).get('actors', [])
                               if a.get('name') not in names and a.get('body') not in names], 'scenarios': []}
    drop_refs(p, lambda t: any(f'/{n}/' in t + '/' or t.startswith(f'{n}.') for n in names))


@arm
def no_entities(p, s):
    """Every EntityWorld entity (the nodes stay and render; nothing is simulated or published)."""
    s['entities'] = []
    drop_refs(p, lambda t: t.startswith('entity/'))


@arm
def small_beams(p, s):
    """The two hidden beam pools (visitor-beam, scout-beam: 32,768 each) at 256 particles (W3 audit #3)."""
    for n in s['nodes']:
        if n['name'] in ('visitor-beam', 'scout-beam'):
            n['particles']['capacity'] = 256
    for k in list(p.get('parameters', {})):
        if k.startswith(('particles/visitor-beam/capacity', 'particles/scout-beam/capacity')):
            p['parameters'][k] = 256


@arm
def eco_gv2(p, s):
    """The ground pools at GV2 multicam's defaults: GV3's ecologyLightRange/PoolReach/PoolFaintest removed (W3 #5)."""
    for k in ('ecologyLightRange', 'ecologyPoolReach', 'ecologyPoolFaintest'):
        s['environment'].pop(k, None)
    drop_refs(p, lambda t: t.split('/')[-1] in ('ecologyLightRange', 'ecologyPoolReach', 'ecologyPoolFaintest')
              or t.startswith(('scene/glowPools', 'scene/glow-pools')))


def write(src, name, p, s):
    base = os.path.join(os.path.dirname(src), f'_qa-b-{name}')
    scene_text = json.dumps(s, indent=1)
    with open(base + '.scene.json', 'w') as f:
        f.write(scene_text)
    data = scene_text.encode()
    p['assets']['scene']['path'] = {'path': os.path.basename(base) + '.scene.json',
                                    'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}
    with open(base + '.json', 'w') as f:
        json.dump(p, f, indent=1)
    return base + '.json'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('project')
    ap.add_argument('--clean', action='store_true')
    ap.add_argument('--only', help='comma-separated arm names')
    a = ap.parse_args()
    d = os.path.dirname(a.project)
    if a.clean:
        for f in os.listdir(d):
            if f.startswith('_qa-b-'):
                os.remove(os.path.join(d, f))
        return 0
    p0 = json.load(open(a.project))
    ref = p0['assets']['scene']['path']
    ref = ref['path'] if isinstance(ref, dict) else ref
    s0 = json.load(open(os.path.join(d, ref)))
    names = a.only.split(',') if a.only else list(ARMS)
    for name in names:
        p, s = copy.deepcopy(p0), copy.deepcopy(s0)
        ARMS[name](p, s)
        print(write(a.project, name, p, s), '-', ARMS[name].__doc__ or '')
    return 0


if __name__ == '__main__':
    sys.exit(main())
