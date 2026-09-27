"""EVIDENCE ONLY: the world gv3-cast is building, applied to a scratch copy, so gv3-cut's framing
of E1-E5 can be rendered and measured before gv3-cast merges. Not part of the generator; delete it
once gv3-cast's cast.py and UFO plan are merged into gv3/production.

  * the characters stream's recommended cast (its make_variant.py "tuned": the flank animals
    re-homed onto the meadows, slope and turn radii, the aliens' still limits and paces);
  * the scout craft (the visitor model at 0.6 scale, with a beam), the hand-written abduction and
    the horse-light keys removed;
  * the E1-E5 plan compiled into it by the engine (`avgen_cast_trace --plan --save-project`).

    python3 tools/gv3/preview_world.py SRC_PROJECT OUT_DIR [--trace-seconds N]

writes OUT_DIR/ufo.json (+ ufo.scene.json), renderable and traceable; `--trace-seconds 1` compiles
the plan without tracing the film.
"""
import copy, hashlib, json, os, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
W = ROOT / 'examples/world'
PLAN = ROOT / 'docs/glowmere-valley-3/revision/audit/data/setpieces/gv3-ufo.plan.json'
TRACE = ROOT / 'build/release/tools/avgen_cast_trace'


def absolutize(node, base):
    if isinstance(node, dict):
        return {k: absolutize(v, base) for k, v in node.items()}
    if isinstance(node, list):
        return [absolutize(v, base) for v in node]
    if isinstance(node, str) and node.startswith('../'):
        return os.path.normpath(str(base / node))
    return node


def tune_cast(project, scene):
    """The characters stream's recommended GV3 cast settings, as its make_variant.py 'tuned' applies them
    (~/Desktop/av-gen-review/18-glowmere-valley-3/revision/characters-work/make_variant.py)."""
    ents = {e['name']: e for e in scene['entities']}
    params = project['parameters']
    def behaviour(ent, kind):
        return next(b for b in ent['behaviors'] if b['kind'] == kind)
    def considerer(ent, cname):
        return next(c for c in behaviour(ent, 'decide')['considerers'] if c.get('name') == cname)
    nodes = {n['name']: n for n in scene['nodes']}
    homes = {'horse-2': [74.0, 7.99, 13.0], 'horse-20': [90.0, 9.04, 26.0], 'horse-22': [60.0, 8.83, 0.0],
             'cow-12': [-66.0, 7.25, 0.0], 'cow-23': [-78.0, 7.30, 6.0]}
    for n, pos in homes.items():
        nodes[n]['position'] = pos
        params.pop(f'nodes/{n}/position', None)
        for key, v in (('homeRadius', 16.0), ('maxRange', 12.0)):
            behaviour(ents[n], 'wander')[key] = v
            params[f'entity/{n}/wander/{key}'] = v
    radius = {'horse': 2.2, 'cow': 1.8, 'bull': 2.0}
    pivot = {'horse': 1.2, 'cow': 1.0, 'bull': 1.1}
    for n, e in ents.items():
        species = n.split('-')[0]
        if species in radius:
            w = behaviour(e, 'wander')
            w['maxSlope'] = 10.0
            w['turnRadius'] = radius[species]
            e.setdefault('gait', {})['pivotRadius'] = pivot[species]
    still = {'rook': 7.0, 'tide': 8.0, 'sage': 10.0, 'ember': 6.0, 'vane': 9.0}
    pace = {'rook': [0.9, 1.3], 'tide': [0.8, 1.2], 'sage': [0.7, 1.05], 'ember': [0.9, 1.35], 'vane': [0.8, 1.15]}
    for n in still:
        e = ents[n]
        e['gait']['turnRadius'] = 1.5
        e['gait']['turnRate'] = 100.0
        d = behaviour(e, 'decide')
        d['maxStillSeconds'] = still[n]
        d.setdefault('mind', {}).setdefault('memory', {})['eventSeconds'] = 20.0
        for c in d['considerers']:
            if c['kind'] == 'interest':
                c['speedRange'] = pace[n]
            if c['kind'] == 'react':
                c['speedRange'] = [0.9, 1.1]
                # the set pieces' beams, as well as the old scenario's (gv3-cast's to decide)
                c['events'] = sorted(set(c.get('events', [])) | {'setpiece/e3-far-lift/beam', 'setpiece/e4-river-pair/beam',
                                                                  'setpiece/e5-centrepiece/beam'})
        clips = e['clips']
        clips['inspect'] = 'Take_from_floor'
        clips['tinker'] = 'Button_push'
    for n in ('sage', 'tide'):
        ents[n]['gait']['runEnter'] = 4.8
        ents[n]['gait']['runExit'] = 3.1
    params['entity/sage/decide/grove/weight'] = 0.1
    considerer(ents['sage'], 'grove')['weight'] = 0.1
    considerer(ents['sage'], 'grove')['duration'] = 4.0
    considerer(ents['sage'], 'graze')['homeRadius'] = 30.0
    considerer(ents['tide'], 'roam')['minRange'] = 14.0
    considerer(ents['tide'], 'roam')['dwell'] = 3.5
    considerer(ents['vane'], 'watch')['dwell'] = 3.5
    considerer(ents['vane'], 'watch')['maxRange'] = 60.0
    for ev in scene.get('worldEvents', []):
        if ev['name'] == 'abduction/beam':
            ev['radius'] = 250.0
    considerer(ents['rook'], 'roam')['activity'] = 'inspect'
    considerer(ents['rook'], 'roam')['dwell'] = 2.4
    considerer(ents['ember'], 'roam')['activity'] = 'tinker'
    considerer(ents['ember'], 'roam')['dwell'] = 1.6


def transform(project, scene):
    tune_cast(project, scene)
    nodes = {n['name']: n for n in scene['nodes']}
    scout = copy.deepcopy(nodes['visitor'])
    scout['name'] = 'scout'
    scout['position'] = [-140.0, 170.0, -360.0]
    scout['scale'] = [0.6, 0.6, 0.6]
    beam = copy.deepcopy(nodes['visitor-beam'])
    beam['name'] = 'scout-beam'
    beam['parent'] = 'scout'
    beam['visible'] = False
    scene['nodes'] += [scout, beam]
    ents = {e['name']: e for e in scene['entities']}
    se = copy.deepcopy(ents['visitor']); se['name'] = 'scout'; se['node'] = 'scout'; se['seed'] = 20260927
    sb = copy.deepcopy(ents['visitor-beam']); sb['name'] = 'scout-beam'; sb['node'] = 'scout-beam'; sb['seed'] = 771156
    scene['entities'] += [se, sb]
    scene['staging']['actors'].append({"name": "scout", "body": "scout", "parts": [{"name": "beam", "entity": "scout-beam"}]})
    scene['staging']['scenarios'] = []
    tl = project['timeline']['tracks']
    project['timeline']['tracks'] = [t for t in tl if not t['target'].startswith('fx/horse-light/')]


def main():
    src, out = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
    seconds = float(sys.argv[sys.argv.index('--trace-seconds') + 1]) if '--trace-seconds' in sys.argv else 226.0
    out.mkdir(parents=True, exist_ok=True)
    p = json.loads(src.read_text())
    scene_path = pathlib.Path(p['assets']['scene']['path']['path'])
    if not scene_path.is_absolute():
        scene_path = src.parent / scene_path
    s = json.loads(scene_path.read_text())
    p = absolutize(p, src.parent)
    s = absolutize(s, scene_path.parent)
    transform(p, s)
    sp = out / 'ufo.scene.json'
    sp.write_text(json.dumps(s, indent=1))
    p['assets']['scene']['path'].update(path=str(sp), sha256=hashlib.sha256(sp.read_bytes()).hexdigest(), size=sp.stat().st_size)
    pp = out / 'ufo-src.json'
    pp.write_text(json.dumps(p, indent=1))
    cmd = [str(TRACE), '--project', str(pp), '--plan', str(PLAN), '--save-project', str(out / 'ufo.json'),
           '--seconds', str(seconds), '--hz', '20', '--out', str(out / 'cast-ufo.json')]
    print(' '.join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    (out / 'ufo-trace.log').write_text(r.stdout + r.stderr)
    print('exit', r.returncode)
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
