#!/usr/bin/env python3
"""Structural complexity report for one or more avgen projects (QA pass W1, deliverable 4).

Reads each project and its scene statically -- no GPU, no engine -- and counts what the brief's
hypothesis B is about: how much the project holds, how much of it is hidden or disabled, what is
duplicated, and which references point at nothing. Optionally folds in the engine's own runtime
counters from an `avgen --bench-json` record (triangles, draws, visible instances, shaded lights),
because a static file cannot say how many triangles a glTF holds or what a camera sees.

    python3 tools/scene_complexity_report.py examples/world/glowmere-valley-2-multicam.json \
        examples/world/_qa-gv3-r7b.json --json out.json --markdown out.md \
        [--bench NAME=bench.json ...]

Output: one JSON object keyed by project name, and a markdown table with one column per project.
Read-only. It never writes a project.
"""
import argparse, collections, hashlib, json, os, sys


def load(path):
    with open(path) as f:
        return json.load(f)


def scene_path(project_path, project):
    ref = project.get('assets', {}).get('scene', {}).get('path')
    if isinstance(ref, dict):
        ref = ref.get('path')
    if not ref:
        return None
    return os.path.normpath(os.path.join(os.path.dirname(project_path), ref))


def canon(x):
    return hashlib.sha1(json.dumps(x, sort_keys=True).encode()).hexdigest()


def walk(node, path=''):
    """Yield (path, dict) for every dict in a JSON tree."""
    if isinstance(node, dict):
        yield path, node
        for k, v in node.items():
            yield from walk(v, f'{path}/{k}')
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from walk(v, f'{path}[{i}]')


def hidden_items(doc, root):
    """Every object that says it is switched off: enabled=false, visible=false, hidden=true."""
    out = []
    for path, d in walk(doc, root):
        if d.get('enabled') is False or d.get('visible') is False or d.get('hidden') is True:
            label = d.get('name') or d.get('id') or d.get('target') or d.get('label') or ''
            out.append(f'{path} {label}'.strip())
    return out


def asset_refs(doc, base):
    """Every string that looks like a file path, resolved against `base`, with whether it exists."""
    refs = {}
    for _, d in walk(doc):
        for k, v in d.items():
            if isinstance(v, str) and '{' not in v and ('/' in v or '.' in v) and v.rsplit('.', 1)[-1].lower() in (
                    'gltf', 'glb', 'json', 'hdr', 'exr', 'png', 'jpg', 'mp3', 'wav', 'wgsl', 'ktx2'):
                p = v if os.path.isabs(v) else os.path.normpath(os.path.join(base, v))
                refs[v] = os.path.exists(p)
    return refs


def report(project_path):
    p = load(project_path)
    sp = scene_path(project_path, p)
    s = load(sp) if sp and os.path.exists(sp) else {}
    base = os.path.dirname(sp or project_path)
    r = collections.OrderedDict()
    r['project'] = os.path.relpath(project_path)
    r['scene'] = os.path.relpath(sp) if sp else None
    r['bytes'] = {'project': os.path.getsize(project_path), 'scene': os.path.getsize(sp) if s else 0}

    # ---- the world --------------------------------------------------------------------------------
    nodes = s.get('nodes', [])
    r['nodes'] = len(nodes)
    r['nodesByKind'] = dict(collections.Counter(n.get('kind', '?') for n in nodes))
    scatter = [l for n in nodes for l in (n.get('scatter') or [])]
    r['scatterLayers'] = len(scatter)
    r['scatterMaxInstances'] = sum(int(l.get('maxInstances', 0)) for l in scatter)
    r['scatterShadowCasting'] = sum(1 for l in scatter if l.get('castsShadow'))
    r['scatterMaxViewDistance'] = max([float(l.get('viewDistance', 0)) for l in scatter] or [0])
    r['particleCapacity'] = sum(int((n.get('particles') or {}).get('capacity', 0)) for n in nodes)
    gltf = [n.get('asset') for n in nodes if n.get('kind') == 'gltf']
    proc_assets = [((n.get('procedural') or {}).get('source') or {}).get('asset') for n in nodes
                   if n.get('kind') == 'procedural']
    meshes = [a for a in gltf + proc_assets + [l.get('asset') for l in scatter] if a]
    r['meshReferences'] = len(meshes)
    r['uniqueMeshAssets'] = len(set(meshes))
    r['entities'] = len(s.get('entities', []))
    r['heroesScene'] = len(s.get('heroes', []))
    r['heroesProject'] = len(p.get('heroes', []))
    r['materialPrograms'] = len(s.get('materialPrograms', []))
    r['worldEvents'] = len(s.get('worldEvents', []))
    rig = s.get('lightRig')
    rig_doc = {}
    if isinstance(rig, str):
        rp = os.path.normpath(os.path.join(base, rig))
        rig_doc = load(rp) if os.path.exists(rp) else {}
    elif isinstance(rig, dict):
        rig_doc = rig
    r['lightRigLights'] = len(rig_doc.get('lights', []))
    r['sceneLights'] = len(s.get('lights', []))
    env = s.get('environment', {})
    r['environment'] = {k: env.get(k) for k in ('volumeDensity', 'volumeSteps', 'volumeMaxDistance',
                                                'shadowCascades', 'ecologyLightRange', 'volumeLocalLights')}

    # ---- effects ----------------------------------------------------------------------------------
    for where, effects in (('sceneEffects', s.get('effects', [])), ('projectEffects', p.get('effects', []))):
        r[where] = len(effects)
        r[where + 'ByType'] = dict(collections.Counter(e.get('type', '?') for e in effects))
        r[where + 'Enabled'] = sum(1 for e in effects if e.get('enabled', True))
        body = [canon({k: v for k, v in e.items() if k not in ('id', 'name', 'owner')}) for e in effects]
        r[where + 'IdenticalBodies'] = sum(c - 1 for c in collections.Counter(body).values() if c > 1)

    # ---- modulation, timeline, sources ------------------------------------------------------------
    routes = p.get('routes', [])
    r['routes'] = len(routes)
    r['routesEnabled'] = sum(1 for x in routes if x.get('enabled', True))
    r['routesByTargetGroup'] = dict(collections.Counter(x.get('target', '').split('/')[0] for x in routes))
    r['routeDuplicates'] = sum(c - 1 for c in collections.Counter(canon(x) for x in routes).values() if c > 1)
    r['routeSameSourceTarget'] = sum(c - 1 for c in collections.Counter(
        (x.get('source'), x.get('target'), x.get('component')) for x in routes).values() if c > 1)
    tracks = p.get('timeline', {}).get('tracks', [])
    r['timelineTracks'] = len(tracks)
    r['timelineKeys'] = sum(len(t.get('keys', [])) for t in tracks)
    r['timelineTracksByGroup'] = dict(collections.Counter(t.get('target', '').split('/')[0] for t in tracks))
    r['timelineTrackSameTarget'] = sum(c - 1 for c in collections.Counter(
        (t.get('target'), t.get('component')) for t in tracks).values() if c > 1)
    r['sources'] = len(p.get('sources', []))
    r['sourcesByKind'] = dict(collections.Counter(x.get('kind', '?') for x in p.get('sources', [])))
    params = p.get('parameters', {})
    r['parameters'] = len(params)
    r['parametersByGroup'] = dict(collections.Counter(k.split('/')[0] for k in params))

    # ---- cameras and direction --------------------------------------------------------------------
    cd = s.get('cameraDirection', {})
    cams = cd.get('cameras', [])
    shots = cd.get('shots', [])
    r['cameras'] = len(cams)
    r['shots'] = len(shots)
    cam_ids = {c.get('id') for c in cams}
    used = {sh.get('camera') for sh in shots}
    r['camerasUnusedByShots'] = len(cam_ids - used)
    r['shotsWithMissingCamera'] = sum(1 for sh in shots if sh.get('camera') not in cam_ids)
    seq = p.get('sequence', {})
    r['sequenceShots'] = len(seq.get('shots', []))
    r['cameraShotSpans'] = len(p.get('cameraShotSpans', []))
    r['cameraAimFollow'] = len(p.get('cameraAimFollow', []))
    st = p.get('staging', {})
    r['stagingActors'] = len(st.get('actors', []))
    r['stagingScenarios'] = len(st.get('scenarios', []))
    sst = s.get('staging', {})
    r['sceneStagingActors'] = len(sst.get('actors', []))
    r['sceneStagingScenarios'] = len(sst.get('scenarios', []))
    dps = p.get('directingPlans', [])
    r['directingPlans'] = len(dps)
    r['directingPlanItems'] = {d.get('id', '?'): {k: len(v) for k, v in d.items() if isinstance(v, list) and v}
                               for d in dps}
    r['songPlanSections'] = len(p.get('songPlan', {}).get('sections', []))

    # ---- hidden, duplicated, dead -----------------------------------------------------------------
    hid = hidden_items(s, 'scene') + hidden_items(p, 'project')
    r['hiddenOrDisabled'] = len(hid)
    r['hiddenOrDisabledItems'] = hid[:200]
    node_names = [n.get('name') for n in nodes]
    r['duplicateNodeNames'] = [k for k, c in collections.Counter(node_names).items() if c > 1]
    node_set = set(node_names)
    effect_ids = {e.get('id') for e in s.get('effects', []) + p.get('effects', [])}
    # An effect's `entity` owner resolves against entities, heroes or nodes (a mushroom cap is a
    # hero node, not an EntityWorld entity), so all three count as a live owner.
    entity_names = ({e.get('name') for e in s.get('entities', [])} | {h.get('name') for h in s.get('heroes', [])}
                    | {h.get('name') for h in p.get('heroes', [])} | node_set)
    dead = []
    for e in s.get('entities', []):
        if e.get('node') and e['node'] not in node_set:
            dead.append(f"entity {e.get('name')} -> node {e['node']}")
    for e in s.get('effects', []) + p.get('effects', []):
        own = e.get('owner') or {}
        if own.get('kind') == 'entity' and own.get('name') not in entity_names:
            dead.append(f"effect {e.get('id')} -> entity {own.get('name')}")
    for label, targets in (('route', [x.get('target', '') for x in routes]),
                           ('track', [t.get('target', '') for t in tracks]),
                           ('parameter', list(params))):
        for t in targets:
            parts = t.split('/')
            if parts[0] == 'fx' and len(parts) > 1 and parts[1] not in effect_ids:
                dead.append(f'{label} {t} -> missing effect')
            if parts[0] in ('nodes', 'procedural', 'particles') and len(parts) > 1 and parts[1] not in node_set:
                dead.append(f'{label} {t} -> missing node')
    for label, doc, b in (('scene', s, base), ('project', p, os.path.dirname(project_path))):
        for ref, ok in asset_refs(doc, b).items():
            if not ok:
                dead.append(f'{label} asset {ref} -> not on disk')
    r['deadReferences'] = len(dead)
    r['deadReferenceItems'] = dead[:200]
    return r


BENCH_KEYS = [
    ('wallMs.p50', lambda b: b['wallMs']['p50']), ('gpuMs.p50', lambda b: b['gpuMs']['p50']),
    ('draws', lambda b: b['counters'].get('drawCalls')), ('triangles', lambda b: b['counters'].get('triangles')),
    ('visibleInstances', lambda b: b['counters'].get('visibleInstances')),
    ('shadedLights', lambda b: b['counters'].get('shadedLights')),
    ('shadowCasters', lambda b: b['counters'].get('shadowCasters')),
]


def bench_summary(path):
    doc = load(path)
    rec = doc['records'][0] if isinstance(doc, dict) and 'records' in doc else doc[0]
    out = {}
    for k, f in BENCH_KEYS:
        try:
            out[k] = f(rec)
        except Exception:
            out[k] = None
    return out


def markdown(reports):
    names = list(reports)
    skip = {'hiddenOrDisabledItems', 'deadReferenceItems', 'project', 'scene'}
    keys = [k for k in reports[names[0]] if k not in skip]
    lines = ['| metric | ' + ' | '.join(names) + ' |', '|---|' + '---:|' * len(names)]
    for k in keys:
        cells = []
        for n in names:
            v = reports[n].get(k)
            if isinstance(v, dict):
                v = ', '.join(f'{a}={b}' for a, b in sorted(v.items(), key=lambda kv: str(kv[0])))
            elif isinstance(v, list):
                v = ', '.join(map(str, v)) if v else '-'
            cells.append(str(v).replace('|', '/'))
        lines.append(f'| {k} | ' + ' | '.join(cells) + ' |')
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('projects', nargs='+')
    ap.add_argument('--json')
    ap.add_argument('--markdown')
    ap.add_argument('--bench', action='append', default=[],
                    help='NAME=bench.json: fold a --bench-json record into project NAME (its file stem)')
    a = ap.parse_args()
    reports = collections.OrderedDict()
    for path in a.projects:
        name = os.path.basename(path).rsplit('.json', 1)[0]
        reports[name] = report(path)
    for spec in a.bench:
        name, path = spec.split('=', 1)
        if name in reports:
            reports[name]['runtime'] = bench_summary(path)
    text = json.dumps(reports, indent=2)
    if a.json:
        open(a.json, 'w').write(text + '\n')
    md = markdown(reports)
    if a.markdown:
        open(a.markdown, 'w').write(md)
    if not a.json and not a.markdown:
        print(md)


if __name__ == '__main__':
    sys.exit(main())
