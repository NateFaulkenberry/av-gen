#!/usr/bin/env python3
"""Refresh the scene fingerprint glowmere-valley-2.json carries for its scene file.

Surgical text replacement rather than a JSON round trip: the project is 344 KB of floats and
re-dumping it rewrites most of them. Anchored on the scene's own `"path"` line, because the audio
asset a few lines above carries a `sha256`/`size` pair of exactly the same shape -- and a regex that
did not anchor happily stamped the scene's digest onto the audio.
"""
import hashlib, re, sys, os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT = os.path.join(REPO, 'examples/world/glowmere-valley-2.scene.json')

# Every scene named on the command line, and each one's own project beside it. There are four
# Glowmere scenes carrying the abduction and there always were; a tool that could only refresh one
# of them is a tool that guarantees the other three go stale.
for scene in (sys.argv[1:] or [DEFAULT]):
    scene = os.path.abspath(scene)
    proj = scene.replace('.scene.json', '.json')
    base = os.path.basename(scene)
    if not os.path.isfile(proj):
        print('%s: no project beside it; nothing to stamp' % base)
        continue
    raw = open(scene, 'rb').read()
    digest, size = hashlib.sha256(raw).hexdigest(), len(raw)
    text = open(proj).read()
    pattern = re.compile(
        r'("path": "' + re.escape(base) + r'",\s*\n\s*"sha256": ")[0-9a-f]{64}(",\s*\n\s*"size": )\d+')
    text, n = pattern.subn(lambda m: m.group(1) + digest + m.group(2) + str(size), text, count=1)
    if n != 1:
        sys.exit('could not find the scene fingerprint for %s in %s' % (base, proj))
    open(proj, 'w').write(text)
    print('%s: %s %d bytes' % (base, digest[:16], size))
