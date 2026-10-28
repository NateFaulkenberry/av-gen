#!/usr/bin/env python3
"""Writes strange-attractor.json: the flythrough scene with Feline Footwear and its stems, and the canyon's
audio routes (canyon brief §6; ADR-1862). The audio is the owner's (gitignored under assets/audio; never committed).

    python3 examples/strange-attractor/build_project.py

Signals: the current system only -- response.* (onset, bass, mid, high), beat.* with beat.source from the drums
stem (feeds), stem roles. No drum classifier.

The mapping (continuous signals for broad things, events for sparse reveals; brief §6.1/6.2):
  response.onsetLow (event) -> meshWire/wave     a travelling front of revealed triangles, 2 s, at most every 3 s
  response.bass  (cont.)  -> meshWire/intensity   sustained bass phrases bring the triangle topology up (slow)
  response.high  (cont.)  -> attractorWorld/contour  the contour lines, fine and fading
"""
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
STEMS = '../../assets/audio/stems'


def route(source, target, out_lo, out_hi, **chain):
    c = {'remapEnabled': True, 'remapInMin': 0.0, 'remapInMax': 1.0, 'remapOutMin': out_lo, 'remapOutMax': out_hi,
         'clampEnabled': True, 'clampMin': 0.0, 'clampMax': 1.0}
    c.update(chain)
    return {'source': source, 'target': target, 'amount': 1.0, 'op': 'replace', 'polarity': 'unipolar', 'chain': c}


routes = [
    # the revelation front: a low-band onset strong enough (a big accent, not every hit) fires; the envelope then falls
    # linearly over 2 s and the front's radius is (1 - envelope) x waveRadius. Refractory 3 s, re-armed below 0.3.
    route('response.onsetLow', 'meshWire/wave', 0.0, 1.0, threshold='binary', thresholdLevel=0.8, envelope='linearfall',
          envelopeFallPerSecond=0.5, envelopeRefractoryMs=3000.0, envelopeRearm=0.3),
    # this track's bass sits high almost throughout (mean 0.78): its top quarter is the phrase that brings the
    # topology up, slowly, and lets it go more slowly still
    route('response.bass', 'meshWire/intensity', 0.0, 0.45, remapInMin=0.75, remapInMax=1.0, attackMs=1200.0, decayMs=3000.0),
    route('response.high', 'attractorWorld/contour', 0.0, 0.7, remapInMin=0.4, remapInMax=1.0, attackMs=250.0, decayMs=1200.0),
]

project = {
    'format': 'avgen-project',
    'version': 4,
    'app': {'name': 'The Strange Attractor'},
    'assets': {
        'scene': {'kind': 'composition', 'path': 'strange-attractor-flythrough.scene.json'},
        'audio': '../../assets/audio/feline-footwear.wav',
        'stems': [
            {'key': 'drums', 'label': 'Drums', 'role': 'drums', 'file': f'{STEMS}/feline-footwear (Drums).wav'},
            {'key': 'bass', 'label': 'Bass', 'role': 'bass', 'file': f'{STEMS}/feline-footwear (Bass).wav'},
            {'key': 'synth', 'label': 'Synth', 'role': 'synth', 'file': f'{STEMS}/feline-footwear (Synth).wav'},
        ],
    },
    'sonic': {'response': {'sensitivity': 0.5, 'transient': 0.5, 'sustain': 0.5, 'attack': 1.0, 'release': 1.0}},
    'feeds': {'beat': 'stem.drums'},
    'sources': [],
    'routes': routes,
    'render': {'width': 1920, 'height': 1080, 'fps': 30, 'output': 'video', 'path': 'renders/strange-attractor.mp4'},
}
path = os.path.join(HERE, 'strange-attractor.json')
json.dump(project, open(path, 'w'), indent=2)
print(path)
