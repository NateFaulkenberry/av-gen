#!/usr/bin/env python3
"""Refresh the scene fingerprint glowmere-valley-2.json carries for its scene file.

Surgical text replacement rather than a JSON round trip: the project is 344 KB of floats and
re-dumping it rewrites most of them. Anchored on the scene's own `"path"` line, because the audio
asset a few lines above carries a `sha256`/`size` pair of exactly the same shape -- and a regex that
did not anchor happily stamped the scene's digest onto the audio.
"""
import hashlib, re, sys, os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
scene = os.path.join(REPO, 'examples/world/glowmere-valley-2.scene.json')
proj = os.path.join(REPO, 'examples/world/glowmere-valley-2.json')
raw = open(scene, 'rb').read()
digest, size = hashlib.sha256(raw).hexdigest(), len(raw)

text = open(proj).read()
pattern = re.compile(
    r'("path": "glowmere-valley-2\.scene\.json",\s*\n\s*"sha256": ")[0-9a-f]{64}(",\s*\n\s*"size": )\d+')
text, n = pattern.subn(lambda m: m.group(1) + digest + m.group(2) + str(size), text, count=1)
if n != 1:
    sys.exit("could not find the scene fingerprint in %s" % proj)
open(proj, 'w').write(text)
print("glowmere-valley-2.scene.json: %s %d bytes" % (digest[:16], size))
