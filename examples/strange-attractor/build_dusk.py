"""Writes strange-attractor-dusk.scene.json: the canyon at dusk (dimensional portals, Gate C; ADR-1884), the
destination of the Moon's crater-floor portal.

    python3 examples/strange-attractor/build_dusk.py

It is the flythrough scene's canyon -- the same `attractorWorld` block, so the generation is shared (cached by hash)
and the terrain is identical -- with the corruption, the scan and the portal taken out and a dusk light: the sun just
under 3 degrees, ahead of the river run so the water mirrors the bright western sky; a deep violet-indigo zenith, an
indigo-violet horizon with only a narrow warm band at the sun (not pink); warm light on the high strata only; and the
gorge held up by fills -- a cool sky fill from above and a violet bounce -- so it never crushes to black (the Nocturne
lesson, plan risk 8). Its camera is driven by the Moon's portal: the dive out of the sky, then the canyon's own
flythrough, joined on the river run.
"""
import copy, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
src = json.load(open(os.path.join(HERE, 'strange-attractor-flythrough.scene.json')))

scene = copy.deepcopy(src)
for key in ('corruption', 'meshWire', 'portal'):
    scene.pop(key, None)
scene['name'] = 'the-strange-attractor-dusk'
scene['_note'] = ('The Strange Attractor canyon at dusk (dimensional portals, Gate C; ADR-1884), written by build_dusk.py '
                  'from the flythrough scene; the destination of the Moon\'s crater-floor portal, whose block drives its camera.')

aw = scene['attractorWorld']
aw['look'].update({'sunAzimuth': 172.0, 'sunElevation': 2.8})
aw['sunLux'] = 4.5

env = scene['environment']
env['intensity'] = 0.16
env['background'] = [0.012, 0.010, 0.055]
env['fogColor'] = [0.10, 0.075, 0.22]
sky = env['sky']
sky.update({'zenithColor': [0.022, 0.020, 0.115], 'horizonColor': [0.36, 0.22, 0.46], 'groundColor': [0.012, 0.010, 0.045],
            'haze': 0.18, 'sunColor': [1.0, 0.46, 0.22], 'sunIntensity': 7.0, 'sunSize': 0.05, 'sunGlow': 0.55})
sky['cirrus'].update({'litColor': [1.0, 0.58, 0.42], 'shadeColor': [0.22, 0.18, 0.48], 'sunScatter': 1.6})

scene['lights'] = [
    # the sky's own light into the gorge: cool, from above, so the shadowed walls read as violet stone, not black
    {"name": "dusk-sky", "type": "directional", "role": "fill", "direction": [0.1, -1.0, 0.15], "color": [0.42, 0.40, 0.95],
     "intensity": 0.7, "castsShadow": False, "shadowStrength": 0.0},
    # the lit western wall bouncing warm-violet light across the gorge
    {"name": "dusk-bounce", "type": "directional", "role": "fill", "direction": [-0.9, -0.15, 0.4], "color": [0.62, 0.40, 0.85],
     "intensity": 0.45, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "floor-bounce", "type": "directional", "role": "fill", "direction": [0.0, 1.0, 0.0], "color": [0.55, 0.38, 0.62],
     "intensity": 0.25, "castsShadow": False, "shadowStrength": 0.0},
]

out = os.path.join(HERE, 'strange-attractor-dusk.scene.json')
json.dump(scene, open(out, 'w'), indent=2)
print(out)
