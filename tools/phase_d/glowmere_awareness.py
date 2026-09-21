# Phase D, owner ruling 2026-09-21: turn the awareness layer on for Glowmere's five aliens, and make
# every roam's minRange consistent with its approach (Q2). A TEXT edit with a minimal diff -- the
# JSON is never re-serialised, because other branches edit this file too.
#
#   python3 tools/phase_d/glowmere_awareness.py            # applies once; refuses to apply twice
import os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
path = os.path.join(root, 'examples', 'world', 'glowmere-valley-2-multicam.scene.json')
text = open(path).read()
if '"mind": {' in text:
    sys.exit('already applied')

# Why each alien got its personality -- and how it should visibly behave -- is in
# docs/design/autonomous-character-architecture.md ("Glowmere's cast").
PERSONALITY = {
    'rook':  dict(curiosity=0.85, caution=0.15, sociability=0.35, eventSensitivity=0.7, attentionSpan=0.4),
    'tide':  dict(curiosity=0.6, caution=0.45, sociability=0.4, eventSensitivity=0.5, attentionSpan=0.95),
    'sage':  dict(curiosity=0.3, caution=0.9, sociability=0.5, eventSensitivity=0.8, attentionSpan=0.6),
    'ember': dict(curiosity=0.55, caution=0.3, sociability=0.95, eventSensitivity=0.5, attentionSpan=0.5),
    'vane':  dict(curiosity=0.75, caution=0.4, sociability=0.4, eventSensitivity=0.95, attentionSpan=0.8),
}

def fmt(v):
    return repr(float(v))

MIND = '''            "mind": {
              "attention": {
                "tags": {
                  "alien": 0.8
                },
                "maxHoldSeconds": 5.0
              },
              "memory": {
                "recoverSeconds": 150.0,
                "habituationSeconds": 6.0
              }
            },
'''
NEW_CONSIDERERS = '''              {
                "kind": "react",
                "name": "beam",
                "events": [
                  "abduction/beam"
                ],
                "weight": 1.5,
                "approach": 18.0,
                "flee": 10.0,
                "dwell": 4.0,
                "fadeSeconds": 12.0
              },
              {
                "kind": "social",
                "name": "company",
                "tags": [
                  "alien"
                ],
                "weight": 0.5,
                "distance": 5.0,
                "personalSpace": 3.0,
                "dwell": 3.0
              },
'''

for name, traits in PERSONALITY.items():
    start = text.index('        "name": "%s",\n        "node": "%s",\n' % (name, name))
    seed = re.compile(r'        "seed": \d+,\n').search(text, start)
    lines = ['        "tags": [\n          "alien"\n        ],\n', '        "personality": {\n']
    items = list(traits.items())
    for i, (k, v) in enumerate(items):
        lines.append('          "%s": %s%s\n' % (k, fmt(v), ',' if i + 1 < len(items) else ''))
    lines.append('        },\n')
    text = text[:seed.end()] + ''.join(lines) + text[seed.end():]
    decide = text.index('            "kind": "decide",\n', start)
    considerers = text.index('            "considerers": [\n', decide)
    text = text[:considerers] + MIND + text[considerers:]
    considerers = text.index('            "considerers": [\n', decide) + len('            "considerers": [\n')
    text = text[:considerers] + NEW_CONSIDERERS + text[considerers:]
    # Q2: the roam's minRange becomes its approach (the stand-off it stops at), so the errand's
    # target cannot drop out of range before the body arrives.
    roam = text.index('                "kind": "interest",\n', considerers)
    approach = re.compile(r'                "approach": ([0-9.]+),\n').search(text, roam)
    minrange = re.compile(r'                "minRange": ([0-9.]+),\n').search(text, roam)
    text = text[:minrange.start()] + '                "minRange": %s,\n' % approach.group(1) + text[minrange.end():]

staging = text.index('\n  "staging": {')
text = text[:staging] + '''
  "worldEvents": [
    {
      "name": "abduction/beam",
      "radius": 60.0,
      "magnitude": 1.0
    }
  ],''' + text[staging:]
open(path, 'w').write(text)
print('applied')
