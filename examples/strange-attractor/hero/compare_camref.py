"""The owner's reference camera and close cameras for the hero compares (the art agent's camref recipe: night look from 0,
compare copy in ../av-gen-sa-compare/examples/sa-compare-<v>/. Writes tmp-camref[-<shot>].{scene.json,json} there."""
import json, os, sys
v, shot = sys.argv[1], sys.argv[2]
E = f'/Users/natefaulkenberry/Documents/GitHub/av-gen-sa-compare/examples/sa-compare-{v}/'
CAMS = {'ref': ([67.956, 4.16, 1356.186], [74.965, 3.06, 1349.764], 58.0),        # the owner's screenshot camera
        'alien': ([76.0, 3.05, 1344.93], [72.9, 2.8, 1348.0], 36.0),           # the alien by the fire, close
        'fire': ([71.9, 2.75, 1351.9], [74.26, 1.95, 1349.62], 34.0),            # the campfire, close
        'ufo': ([80.5, 4.2, 1349.0], [88.9, 4.0, 1354.9], 40.0),                 # the craft
        'sas': ([70.6, 2.9, 1351.2], [76.0, 2.4, 1350.4], 40.0),
        'seat': ([77.35, 2.45, 1351.45], [75.0, 2.05, 1352.45], 42.0)}               # Sasquatch C on its stone, from the side                 # the three Sasquatches
pos, tgt, fov = CAMS[shot]
s = json.load(open(E + 'strange-attractor-flythrough.scene.json'))
for k in ('portal', 'portals', 'corruption', 'meshWire'):
    s.pop(k, None)
night = [l for l in s['looks'] if l['name'] == 'night'][0]
night['from'] = 0
s['looks'] = [night]
s['attractorWorld']['camera'] = 'scene'
s['camera'] = {'mode': 1, 'position': pos, 'target': tgt, 'fov': fov, 'orbitSpeed': 0.0}
json.dump(s, open(E + f'tmp-camref-{shot}.scene.json', 'w'), indent=1)
p = json.load(open(E + 'strange-attractor.json'))
p['assets']['scene']['path'] = f'tmp-camref-{shot}.scene.json'
p['timeline']['tracks'] = [t for t in p['timeline']['tracks'] if not t['target'].startswith(('camera', 'journey'))]
json.dump(p, open(E + f'tmp-camref-{shot}.json', 'w'), indent=1)
