"""Writes strange-attractor-dusk.scene.json: the canyon at dusk (dimensional portals, Gate C; ADR-1884), the
destination of the Moon's crater-floor portal.

    python3 examples/strange-attractor/build_dusk.py

It is the flythrough scene's canyon -- the same `attractorWorld` block, so the generation is shared (cached by hash)
and the terrain is identical -- with the corruption, the scan and the portal taken out and a dusk light: the sun just
under 3 degrees, ahead of the river run so the water mirrors the bright western sky; a deep violet-indigo zenith, an
indigo-violet horizon with only a narrow warm band at the sun (not pink); warm light on the high strata only; and the
gorge held up by fills -- a cool sky fill from above and a violet bounce -- so it never crushes to black (the Nocturne
lesson, plan risk 8). Its camera is driven by the Moon's portal: out of a torn opening in the east wall, then low over
the water down the canyon (Gate C 2).
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
aw['look'].update({'sunAzimuth': -130.0, 'sunElevation': 8.0})  # low, from behind-left of the exit: it rakes the west wall and the far walls
aw['sunLux'] = 8.0

env = scene['environment']
env['intensity'] = 0.13
env['background'] = [0.012, 0.010, 0.055]
env['fogColor'] = [0.10, 0.075, 0.22]
sky = env['sky']
sky.update({'zenithColor': [0.022, 0.020, 0.115], 'horizonColor': [0.36, 0.22, 0.46], 'groundColor': [0.012, 0.010, 0.045],
            'haze': 0.18, 'sunColor': [1.0, 0.52, 0.26], 'sunIntensity': 7.0, 'sunSize': 0.05, 'sunGlow': 0.55})
sky['cirrus'].update({'litColor': [1.0, 0.58, 0.42], 'shadeColor': [0.22, 0.18, 0.48], 'sunScatter': 1.6})

scene['lights'] = [
    # the sky's own light into the gorge: cool, from above, so the shadowed walls read as violet stone, not black
    {"name": "dusk-sky", "type": "directional", "role": "fill", "direction": [0.1, -1.0, 0.15], "color": [0.36, 0.38, 1.0],
     "intensity": 0.6, "castsShadow": False, "shadowStrength": 0.0},
    # the lit western wall bouncing warm-violet light across the gorge
    {"name": "dusk-bounce", "type": "directional", "role": "fill", "direction": [-0.9, -0.15, 0.4], "color": [0.55, 0.42, 0.95],
     "intensity": 0.3, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "floor-bounce", "type": "directional", "role": "fill", "direction": [0.0, 1.0, 0.0], "color": [0.55, 0.38, 0.62],
     "intensity": 0.25, "castsShadow": False, "shadowStrength": 0.0},
]

# Gate C 2: the exit is torn in the east wall, as portal 1 is torn in its wall (the same corruption, its axis into the
# rock): the field's slips around the exit, but no tear and no code on this side. The camera looks out through the
# wall (its rock behind the exit plane is clipped, ADR-1883), so a tear here is never seen from the front; its throat
# stretched the wall's foot into dark slivers in front of the camera, and code on the foot filled the first frames
scene['corruption'] = {"targets": ["attractor.terrain"], "focus": [166.5, 43.0, 3427.3], "axis": [0.426, 0.0, 0.905],
                       "amount": 1.0, "radius": 14.0, "inner": 0.4, "fieldNear": 30.0, "fieldFar": 320.0,
                       "openNear": 0.5, "openFar": 0.6, "seam": 0.3, "width": 18.0, "height": 14.0, "depth": 12.0,
                       "block": 5.0, "share": 0.1, "slip": 2.0, "rate": 0.5, "strip": 4.0, "stripSlip": 1.5, "slide": 0.6,
                       "pull": 4.0, "lift": 1.0, "fold": 1.0, "lag": 0.7, "glyph": 0.0, "glyphSize": 1.8, "glow": 4.0,
                       "echoes": 0, "echoOffset": 1.0, "glowColor": [1.0, 0.55, 0.18], "light": 0.0, "floor": -0.8,
                       "front": 0.5}  # the wall slopes (56 degrees): its foot stands in front of the plane; leave it whole

out = os.path.join(HERE, 'strange-attractor-dusk.scene.json')
json.dump(scene, open(out, 'w'), indent=2)
print(out)
