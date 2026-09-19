#!/usr/bin/env python3
"""Report three things about `examples/` that nothing else reports.

`refresh_scene_fingerprint.py` stamps; it never tells you a fingerprint has drifted, and it defaults
to a single scene, so "1 fingerprint(s) refreshed" is compatible with three projects staying stale.
And nothing at all checks that a `worldfx/<name>/...` parameter names an effect that exists -- the
failure that once left 94 orphaned `atmos/*` parameters, and that a rename can recreate in one pass.

A **modulation route** can name the same missing effect, and it is the quieter half of the same
defect: an orphaned parameter is refused once at load with its own line, while an orphaned route is
one line among fifty and then simply never fires. `glowmere-valley-2-multicam` carries
`beat.pulse -> atmos/Bioluminescent Comet/coreIntensity` against a project whose only atmospheric
effect is called `Aurora`; it has been reporting "1 modulation route(s) could not be bound" on
every load, headless and windowed, and nothing was watching.

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


# The two parameter groups whose path is `<group>/<effect name>/<property>`, and the document key
# each group's effects are declared under. `nodes/...` is deliberately not here: it resolves against
# scene node ids on a different path, and a removed node is a legitimate authored edit.
GROUPS = {'worldfx': 'worldEffects', 'atmos': 'atmosphericEffects'}


def effect_names(node, key, out):
    """Every `<key>[].name` anywhere in a document. They are not always at the top level, and a
    project that declares none may still inherit them from its scene."""
    if isinstance(node, dict):
        for entry in node.get(key) or []:
            if isinstance(entry, dict) and entry.get('name'):
                out.add(entry['name'])
        for value in node.values():
            effect_names(value, key, out)
    elif isinstance(node, list):
        for value in node:
            effect_names(value, key, out)


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

        # An effect's name is the prefix its parameters hang off, so renaming or deleting one
        # orphans the other, and the loader then refuses every orphaned value as an unknown path.
        params = doc.get('parameters') or {}
        counted = False
        for group, key in sorted(GROUPS.items()):
            prefixes = {k.split('/')[1] for k in params
                        if k.startswith(group + '/') and k.count('/') >= 2}
            if not prefixes:
                continue
            if not counted:
                checked_fx += 1
                counted = True
            names = set()
            effect_names(doc, key, names)
            scene, _ = scene_path(path, doc)
            if scene and os.path.exists(scene):
                try:
                    effect_names(json.load(open(scene)), key, names)
                except Exception:
                    pass
            for orphan in sorted(prefixes - names):
                n = sum(1 for k in params if k.startswith('%s/%s/' % (group, orphan)))
                problems.append("%s: %d parameter(s) under '%s/%s/' name no effect"
                                % (rel, n, group, orphan))

        # The same question asked of the routes. A route's target is a parameter path, so an
        # effect that is not there fails in exactly the way an orphaned parameter does -- except
        # that `Modulator::bind` reports it once per load and then the route is silently inert for
        # the rest of the session. Only the two effect groups are checked, for the reason GROUPS
        # gives: a `nodes/...` target resolves against scene node ids and a removed node is an
        # authored edit rather than a mistake.
        for route in doc.get('routes') or []:
            if not isinstance(route, dict):
                continue
            target = route.get('target')
            if not isinstance(target, str) or target.count('/') < 2:
                continue
            group, effect = target.split('/')[0], target.split('/')[1]
            key = GROUPS.get(group)
            if key is None:
                continue
            names = set()
            effect_names(doc, key, names)
            scene, _ = scene_path(path, doc)
            if scene and os.path.exists(scene):
                try:
                    effect_names(json.load(open(scene)), key, names)
                except Exception:
                    pass
            if effect not in names:
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
