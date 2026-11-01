#!/usr/bin/env python3
"""Writes CHRONOS's scene file and project (docs/prototypes/chronos/, ADR-1933).

The canyon is carved along an authored river (the `chronosCanyon` block's control points); the camera's flight is
authored along the same centreline -- the same Catmull-Rom the engine resamples -- as arc positions with a lateral
offset and a height, so the two cannot disagree. The project carries Feline Footwear and its stems by relative path
(audio is never committed) and drives `camera/splineT` from the timeline.

    python3 examples/chronos/build_scene.py

Gate 1 writes the clean environment only: no temporal effect is on. `--slit vertical|horizontal` writes an audition
project with the slit scan on (chronos-slit-<kind>.json).
"""
import json, math, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SCENE = os.path.join(HERE, 'chronos.scene.json')
SONG = 210.5  # Feline Footwear

# ---- the river: a broad meander, entering from the south-west and leaving to the north-east ------------------------
RIVER = [(-5200, -2600), (-3900, -1700), (-3000, -2050), (-2100, -1150), (-1500, -300), (-600, -150), (200, -700),
         (1100, -500), (1700, 300), (2500, 700), (3400, 350), (4300, 900), (5200, 1700)]

def catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return tuple(0.5 * (2 * p1[i] + (-p0[i] + p2[i]) * t + (2 * p0[i] - 5 * p1[i] + 4 * p2[i] - p3[i]) * t2 +
                        (-p0[i] + 3 * p1[i] - 3 * p2[i] + p3[i]) * t3) for i in range(2))

def centreline(pts, step=4.0):
    dense = []
    n = len(pts)
    for i in range(n - 1):
        p0 = (2 * pts[0][0] - pts[1][0], 2 * pts[0][1] - pts[1][1]) if i == 0 else pts[i - 1]
        p3 = (2 * pts[-1][0] - pts[-2][0], 2 * pts[-1][1] - pts[-2][1]) if i + 2 >= n else pts[i + 2]
        for k in range(64):
            dense.append(catmull(p0, pts[i], pts[i + 1], p3, k / 64))
    dense.append(pts[-1])
    out, arc = [dense[0]], [0.0]
    carry = 0.0
    for i in range(1, len(dense)):
        a, b = dense[i - 1], dense[i]
        seg = math.dist(a, b)
        while carry + seg >= step:
            t = (step - carry) / seg
            a = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
            out.append(a); arc.append(arc[-1] + step)
            seg = math.dist(a, b)
            carry = 0.0
        carry += seg
    return out, arc

LINE, ARC = centreline(RIVER)

def at_arc(s):
    """Position and unit tangent on the centreline at arc length s."""
    i = max(0, min(len(LINE) - 2, int(s / 4.0)))
    a, b = LINE[i], LINE[i + 1]
    t = max(0.0, min(1.0, (s - ARC[i]) / 4.0))
    p = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    j = max(0, min(len(LINE) - 1, i + 6)); k = max(0, i - 6)
    d = (LINE[j][0] - LINE[k][0], LINE[j][1] - LINE[k][1])
    l = math.hypot(*d) or 1.0
    return p, (d[0] / l, d[1] / l)

# ---- the flight: (arc metres, lateral offset metres (+ = left of the flow), height above the water) -----------------
# Act I establishes from above the rim; Act II descends to the river; III-IV travel the gorge low and close to the
# walls and the spires; V lifts a little and carries on.
FLIGHT = [
    (1500, -60, 560), (1900, -40, 470), (2300, -20, 360), (2700, 0, 240),   # I   above the rim, the canyon below
    (3050, 10, 140), (3350, 20, 70), (3650, 15, 34),                        # II  the descent
    (3950, -10, 22), (4300, -35, 16), (4650, -20, 14), (5000, 25, 15),      # III along the river
    (5350, 45, 18), (5700, 15, 13), (6050, -30, 14),
    (6400, -50, 17), (6750, -15, 12), (7100, 30, 14), (7450, 50, 18),       # IV  close to the walls
    (7800, 10, 13), (8150, -35, 15), (8500, -20, 20),
    (8850, 0, 34), (9200, 15, 55), (9550, 0, 80), (9900, -10, 95),          # V   lifting, carrying on
]
WATER = -1.0

def flight_points():
    pts = []
    for s, lateral, h in FLIGHT:
        p, t = at_arc(s)
        left = (-t[1], t[0])
        pts.append([round(p[0] + left[0] * lateral, 2), round(WATER + h, 2), round(p[1] + left[1] * lateral, 2)])
    return pts

# Spires (hoodoos) on the benches beside the river, near the flight.
def spires():
    out = []
    for s, side, off, h, r in [(4150, 1, 130, 75, 20), (4480, -1, 150, 95, 24), (5520, 1, 120, 70, 18),
                               (6250, -1, 135, 110, 26), (6900, 1, 125, 85, 21), (7650, -1, 120, 80, 19),
                               (8300, 1, 140, 100, 24)]:
        p, t = at_arc(s)
        left = (-t[1], t[0])
        out.append({'at': [round(p[0] + left[0] * off * side, 1), round(p[1] + left[1] * off * side, 1)],
                    'height': h, 'radius': r})
    return out

def scene():
    return {
        'format': 'avgen-scene', 'version': 1, 'name': 'chronos',
        '_note': 'CHRONOS (docs/prototypes/chronos/, ADR-1930..1933): a river canyon carved along an authored river, '
                 'drawn as engine geometry; the camera flies a spline authored along the same centreline. Built by '
                 'examples/chronos/build_scene.py; the walls use chronosRock (Strange Attractor\'s canyonRock, retuned).',
        'camera': {'mode': 2, 'spline': 'flight', 'position': [0, 300, 0], 'target': [0, 0, 0], 'fov': 44.0,
                   'near': 0.5, 'far': 30000.0},
        'environment': {
            'intensity': 0.09,
            'background': [0.03, 0.022, 0.09],
            'fogColor': [0.3, 0.17, 0.33],
            'volumeDensity': 6.0e-5,
            'sky': {
                'enabled': True, 'background': True, 'useKeyLight': True,
                'zenithColor': [0.06, 0.05, 0.26], 'horizonColor': [0.85, 0.42, 0.42], 'groundColor': [0.03, 0.02, 0.07],
                'haze': 0.16, 'sunColor': [1.0, 0.62, 0.32], 'sunIntensity': 6.0, 'sunSize': 0.045, 'sunGlow': 0.45,
                'intensity': 1.0,
                'cirrus': {'amount': 1.0, 'coverage': 0.55, 'softness': 0.3, 'altitude': 4200, 'length': 11000,
                           'width': 1500, 'direction': 35, 'litColor': [1.0, 0.66, 0.6], 'shadeColor': [0.34, 0.24, 0.55],
                           'sunScatter': 1.4, 'seed': 3.0, 'reflection': 0.15, 'bands': 1.0, 'bandWidth': 5000.0},
            },
            'shadowRange': 3000.0, 'shadowCascades': 4, 'ambientOcclusion': False, 'shadowCasterReach': 4000.0,
            'contactShadows': False,
        },
        'post': {'bloomIntensity': 0.05, 'tonemap': 0},
        'lights': [
            {'name': 'sun', 'type': 'directional', 'role': 'key', 'direction': sun_direction(14.0, 205.0),
             'color': [1.0, 0.68, 0.4], 'intensity': 7.0, 'castsShadow': True, 'shadowBias': 0.6},
            {'name': 'canyon-bounce', 'type': 'directional', 'role': 'fill', 'direction': [-0.42, -0.2, -0.88],
             'color': [1.0, 0.42, 0.26], 'intensity': 0.45, 'castsShadow': False, 'shadowStrength': 0.0},
            {'name': 'sky-fill', 'type': 'directional', 'role': 'fill', 'direction': [0.2, -1.0, 0.3],
             'color': [0.42, 0.32, 1.0], 'intensity': 0.3, 'castsShadow': False, 'shadowStrength': 0.0},
        ],
        'nodes': [{'name': 'flight', 'kind': 'spline',
                   # 256 samples a segment (about 1.4 m): the camera's arc-length table is piecewise linear, and at the
                   # default density its aim stepped every ~8 frames (irregularity 0.40 in the first clean render)
                   'spline': {'name': 'flight', 'kind': 'catmullRom', 'closed': False, 'samplesPerSegment': 256,
                              'points': [{'position': p, 'scale': 1.0, 'roll': 0.0} for p in flight_points()]}}],
        'chronosCanyon': {
            'seed': 11, 'resolution': 4096, 'extent': 14000.0, 'river': [list(p) for p in RIVER],
            'floorHalfWidth': 48.0, 'rimHalfWidth': 520.0, 'depth': 320.0, 'tierHeight': 48.0, 'riserShare': 0.16,
            'terraces': 0.85, 'widthVariation': 0.4, 'alcoves': 0.6, 'gullies': 0.55, 'plateauRelief': 50.0,
            'rangeHeight': 1300.0, 'rangeStart': 2400.0, 'rangeEnd': 5600.0, 'band': 8.0, 'strataTilt': 0.006,
            'waterLevel': WATER, 'bedDepth': 4.0, 'spires': spires(),
            'meshSpacing': 1.5, 'meshError': 0.003, 'meshMinError': 0.2, 'terrainProgram': 'chronosRock',
            'water': {'shallowColor': [0.035, 0.05, 0.12], 'deepColor': [0.004, 0.006, 0.02], 'roughness': 0.03,
                      'ripple': 0.12, 'rippleScale': 0.08, 'fresnel': 0.35, 'reflection': 1.3,
                      'reflectionTint': [0.9, 0.95, 1.05], 'clarity': 0.6, 'maxOpacity': 0.97, 'edgeFade': 0.4,
                      'specular': 1.0, 'ssr': 1.0, 'ssrDistance': 1400.0, 'ssrSteps': 160.0, 'ssrGain': 1.0,
                      'skyMirror': 0.2, 'foam': 0.0},
        },
    }

def sun_direction(elevation, azimuth):
    """The light's travel direction (from the sun toward the ground)."""
    e, a = math.radians(elevation), math.radians(azimuth)
    to_sun = (math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a))
    return [round(-c, 4) for c in to_sun]

def project(slit=None):
    params = {'camera/lookAhead': 60.0, 'camera/splineBank': 14.0}
    if slit:
        # Gate 1 auditions: the owner-approved ring (128 layers, half resolution live / full offline). Vertical: each
        # column a moment, now at the right edge, 4 s across the frame. Horizontal: each row a moment, now at the
        # top, 3 s down it. Strips: the vertical map quantised into 24 crisp bands.
        params.update({'temporal/history/capacity': 128.0, 'temporal/history/stride': 1.0,
                       'temporal/slit/enabled': True, 'temporal/slit/amount': 1.0, 'temporal/slit/mode': 0.0,
                       'temporal/slit/blend': 1.0})
        params.update({'vertical': {'temporal/slit/span': 120.0, 'temporal/slit/angle': 180.0, 'temporal/slit/strips': 0.0},
                       'horizontal': {'temporal/slit/span': 90.0, 'temporal/slit/angle': 90.0, 'temporal/slit/strips': 0.0},
                       'vertical-long': {'temporal/history/stride': 2.0, 'temporal/slit/span': 250.0,
                                         'temporal/slit/angle': 180.0, 'temporal/slit/strips': 0.0},
                       'strips': {'temporal/slit/span': 120.0, 'temporal/slit/angle': 180.0, 'temporal/slit/strips': 24.0,
                                  'temporal/slit/edge': 0.15, 'temporal/slit/blend': 0.0}}[slit])
    # the camera's progress along the flight: slower over the establishing shot, then the river pace
    keys = [(0.0, 0.0), (20.1, 0.105), (47.85, 0.24), (101.4, 0.5), (166.9, 0.81), (SONG, 1.0)]
    return {
        'format': 'avgen-project', 'version': 4, 'app': {'name': 'CHRONOS'},
        'assets': {
            'scene': {'kind': 'composition', 'path': 'chronos.scene.json'},
            'audio': '../../assets/audio/feline-footwear.wav',
            'stems': [{'key': k.lower(), 'label': k, 'role': k.lower(),
                       'file': f'../../assets/audio/stems/feline-footwear ({k}).wav'} for k in ('Drums', 'Bass', 'Synth')],
        },
        'feeds': {'beat': 'stem.drums'},
        'parameters': params,
        'sources': [], 'routes': [],
        'timeline': {'enabled': True, 'cues': [], 'tracks': [
            {'target': 'camera/splineT', 'component': -1, 'enabled': True, 'timeBase': 'seconds', 'mode': 'replace',
             'loopLength': 0.0, 'keys': [{'time': t, 'value': v, 'interp': 'smooth'} for t, v in keys]}]},
        'render': {'width': 1920, 'height': 1080, 'fps': 30, 'output': 'video', 'path': 'renders/chronos.mp4'},
    }

if __name__ == '__main__':
    with open(SCENE, 'w') as f:
        json.dump(scene(), f, indent=1)
    subprocess.run([sys.executable, os.path.join(HERE, 'build_rock_program.py'), SCENE],
                   check=True)
    with open(os.path.join(HERE, 'chronos.json'), 'w') as f:
        json.dump(project(), f, indent=1)
    for kind in ('vertical', 'vertical-long', 'horizontal', 'strips'):
        with open(os.path.join(HERE, f'chronos-slit-{kind}.json'), 'w') as f:
            json.dump(project(kind), f, indent=1)
    span = ARC[-1]
    print(f'river {span:.0f} m; flight {FLIGHT[0][0]}..{FLIGHT[-1][0]} m of arc ({len(FLIGHT)} points)')
