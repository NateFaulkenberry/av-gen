#!/usr/bin/env python3
"""Report three things about `examples/` that nothing else reports.

`refresh_scene_fingerprint.py` stamps; it never tells you a fingerprint has drifted, and it defaults
to a single scene, so "1 fingerprint(s) refreshed" is compatible with three projects staying stale.
And nothing at all checks that an `fx/<id>/...` parameter names an effect that exists -- the
failure that once left 94 orphaned `atmos/*` parameters (ADR-702 folded `worldfx/<name>/` and
`atmos/<name>/` into `fx/<id>/`, keyed by the effect's stable id, so a RENAME can no longer recreate
it; a deletion, or an id edited by hand, still can).

A **modulation route** can name the same missing effect, and it is the quieter half of the same
defect: an orphaned parameter is refused once at load with its own line, while an orphaned route is
one line among fifty and then simply never fires. `glowmere-valley-2-multicam` once carried
`beat.pulse -> atmos/Bioluminescent Comet/coreIntensity` against a project whose only atmospheric
effect was called `Aurora`; it reported "1 modulation route(s) could not be bound" on every load,
headless and windowed, and nothing was watching.

Read-only. Exits non-zero if anything is wrong, so it can gate a commit.

    python3 tools/check_project_integrity.py
"""
import glob, hashlib, json, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def projects():
    """Every project document under examples/, paired with its path. Scenes and the index are not
    projects; a file that will not parse is reported rather than skipped silently."""
    for path in sorted(glob.glob(os.path.join(REPO, 'examples', '**', '*.json'), recursive=True)):
        if path.endswith('.scene.json') or os.path.basename(path) == 'index.json':
            continue
        try:
            doc = json.load(open(path))
        except Exception as exc:
            yield path, exc
            continue
        if isinstance(doc, dict):
            yield path, doc


# ADR-702: the one parameter group whose path is `<group>/<effect id>/<property>`. `nodes/...` is
# deliberately not checked: it resolves against scene node ids on a different path, and a removed
# node is a legitimate authored edit.
GROUP = 'fx'


def effect_ids(effects, out):
    """The ids of an `effects` array (ADR-702's canonical entries: `id` and `type`)."""
    for entry in effects or []:
        if isinstance(entry, dict) and entry.get('id') and entry.get('type'):
            out.add(entry['id'])


def effective_ids(path, doc):
    """The effects a project actually runs with. ADR-702: a project's own `effects` array REPLACES
    its scene's list, so when it has one only its ids count; otherwise the scene's -- inline, or
    the referenced file."""
    ids = set()
    if 'effects' in doc:
        effect_ids(doc['effects'], ids)
        return ids
    inline = doc.get('assets', {}).get('scene', {}).get('inline')
    if isinstance(inline, dict):
        effect_ids(inline.get('effects'), ids)
        return ids
    scene, _ = scene_path(path, doc)
    if scene and os.path.exists(scene):
        try:
            effect_ids(json.load(open(scene)).get('effects'), ids)
        except Exception:
            pass
    return ids


def scene_path(path, doc):
    ref = doc.get('assets', {}).get('scene', {}).get('path')
    if isinstance(ref, dict) and 'sha256' in ref and 'path' in ref:
        return os.path.join(os.path.dirname(path), ref['path']), ref
    return None, None


def main():
    problems, checked_fx, checked_fp = [], 0, 0
    for path, doc in projects():
        rel = os.path.relpath(path, REPO)
        if isinstance(doc, Exception):
            problems.append('%s: will not parse: %s' % (rel, doc))
            continue

        # An effect's id is the prefix its parameters hang off, so deleting one orphans the other,
        # and the loader then refuses every orphaned value as an unknown path.
        params = doc.get('parameters') or {}
        ids = effective_ids(path, doc)
        prefixes = {k.split('/')[1] for k in params if k.startswith(GROUP + '/') and k.count('/') >= 2}
        if prefixes:
            checked_fx += 1
        for orphan in sorted(prefixes - ids):
            n = sum(1 for k in params if k.startswith('%s/%s/' % (GROUP, orphan)))
            problems.append("%s: %d parameter(s) under '%s/%s/' name no effect" % (rel, n, GROUP, orphan))

        # The same question asked of the routes. A route's target is a parameter path, so an
        # effect that is not there fails in exactly the way an orphaned parameter does -- except
        # that `Modulator::bind` reports it once per load and then the route is silently inert for
        # the rest of the session. Only the effect group is checked, for the reason GROUP gives.
        for route in doc.get('routes') or []:
            if not isinstance(route, dict):
                continue
            target = route.get('target')
            if not isinstance(target, str) or target.count('/') < 2:
                continue
            group, effect = target.split('/')[0], target.split('/')[1]
            if group != GROUP:
                continue
            if effect not in ids:
                problems.append("%s: modulation route '%s -> %s' names no effect"
                                % (rel, route.get('source', '?'), target))

        scene, ref = scene_path(path, doc)
        if ref is None:
            continue
        checked_fp += 1
        if not os.path.exists(scene):
            problems.append('%s: fingerprints a scene that is not there: %s' % (rel, ref['path']))
            continue
        raw = open(scene, 'rb').read()
        digest, size = hashlib.sha256(raw).hexdigest(), len(raw)
        if digest != ref['sha256'] or size != ref.get('size'):
            problems.append('%s: scene fingerprint is stale (stored %s %s, actual %s %d) -- '
                            'refresh_scene_fingerprint.py %s'
                            % (rel, ref['sha256'][:16], ref.get('size'), digest[:16], size, ref['path']))

    print('%d project(s) carrying effect parameters, %d fingerprinting a scene' % (checked_fx, checked_fp))
    for problem in problems:
        print('  ' + problem)
    print('%d problem(s)' % len(problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
