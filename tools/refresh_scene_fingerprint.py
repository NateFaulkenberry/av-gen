#!/usr/bin/env python3
"""Refresh the scene fingerprint every project carries for a scene file.

Surgical text replacement rather than a JSON round trip: the project is 344 KB of floats and
re-dumping it rewrites most of them. Anchored on the scene's own `"path"` line, because the audio
asset a few lines above carries a `sha256`/`size` pair of exactly the same shape -- and a regex that
did not anchor happily stamped the scene's digest onto the audio.

Every project that *references* the scene, not the one beside it. The sibling rule was true for the
four Glowmere valley scenes and false the moment a scene was shared: `glowmere-stylized.scene.json`
backs three projects, and one of them is `examples/composition/glowmere-lyrics.json`, which names it
`"../world/glowmere-stylized.scene.json"` -- another directory and another spelling, so both the
sibling lookup and a regex anchored on the basename walked straight past it. That is this tool's own
argument one level up: a tool that could only refresh the sibling is a tool that guarantees every
other reference goes stale.
"""
import hashlib, json, re, sys, os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT = os.path.join(REPO, 'examples/world/glowmere-valley-2.scene.json')
EXAMPLES = os.path.join(REPO, 'examples')


def projects():
    """(path, document) for every avgen project under examples/, generated benchmarks excluded."""
    for root, _dirs, names in os.walk(EXAMPLES):
        if os.sep + '_bench' in root:
            continue
        for name in sorted(names):
            if not name.endswith('.json') or name.endswith('.scene.json'):
                continue
            path = os.path.join(root, name)
            try:
                with open(path) as f:
                    doc = json.load(f)
            except (ValueError, OSError):
                continue
            if isinstance(doc, dict) and doc.get('format') == 'avgen-project':
                yield path, doc


def scene_reference(doc):
    """The `assets.scene.path` object form (path + sha256 + size), or None.

    A bare string carries no fingerprint, so there is nothing to refresh and nothing to warn about:
    the load resolves it by path and only ever asks for a digest when the file has gone missing.
    """
    ref = doc.get('assets', {}).get('scene', {}).get('path')
    return ref if isinstance(ref, dict) and 'sha256' in ref and 'path' in ref else None


stamped = 0
for scene in (sys.argv[1:] or [DEFAULT]):
    scene = os.path.abspath(scene)
    base = os.path.basename(scene)
    raw = open(scene, 'rb').read()
    digest, size = hashlib.sha256(raw).hexdigest(), len(raw)
    found = 0
    for proj, doc in projects():
        ref = scene_reference(doc)
        if ref is None:
            continue
        if os.path.abspath(os.path.join(os.path.dirname(proj), ref['path'])) != scene:
            continue
        found += 1
        text = open(proj).read()
        # Anchored on the path exactly as that project spells it, which is not always the basename.
        pattern = re.compile(
            r'("path": "' + re.escape(ref['path']) + r'",\s*\n\s*"sha256": ")[0-9a-f]{64}(",\s*\n\s*"size": )\d+')
        text, n = pattern.subn(lambda m: m.group(1) + digest + m.group(2) + str(size), text, count=1)
        if n != 1:
            sys.exit('could not find the scene fingerprint for %s in %s' % (base, proj))
        open(proj, 'w').write(text)
        stamped += 1
        print('%s -> %s: %s %d bytes' % (base, os.path.relpath(proj, REPO), digest[:16], size))
    if found == 0:
        print('%s: no project fingerprints it; nothing to stamp' % base)

print('%d fingerprint(s) refreshed' % stamped)
