# Generates examples/labs/character/pasture.scene.json: Phase D §77's second creature in a new world.
# No alien, no mushroom, no saucer, no Glowmere word: cows, grass, a hay bale, and a dog that barks.
import json, copy
import os
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..') + '/'
base = json.load(open(root + 'examples/labs/character/guard-post.scene.json'))
hero_t = base['heroes'][0]
def hero(name, pos, r):
    h = copy.deepcopy(hero_t); h['name'] = name; h['position'] = pos; h['radius'] = r; return h
def gltf(name, asset, pos, scale, yaw=0.0):
    return {'name': name, 'kind': 'gltf', 'asset': asset, 'position': pos, 'rotation': [0.0, yaw, 0.0], 'scale': [scale]*3,
            'animation': {'state': 'Walk', 'blend': 0.3}}
d = {k: base[k] for k in ('format', 'version', 'lightRig', 'environment', 'navBodyRadius', 'post')}
d['name'] = 'Pasture (Phase D second creature)'
d['camera'] = {'mode': 1, 'position': [0.0, 40.0, -70.0], 'target': [0.0, 0.0, 0.0], 'fov': 50.0, 'orbitSpeed': 0.0}
ground = copy.deepcopy(base['nodes'][0]); ground['world']['name'] = 'pasture-flat'
d['heroes'] = [hero('fence-post', [25.0, 0.0, 25.0], 1.0), hero('trough', [-25.0, 0.0, 20.0], 1.5)]
d['nodes'] = [ground,
    gltf('bess', '../../../assets/farm/cow.glb', [-10.0, 0.0, -10.0], 1.94, 30.0),
    gltf('daisy', '../../../assets/farm/cow.glb', [-4.0, 0.0, -14.0], 1.94, -20.0),
    gltf('grass-1', '../../../assets/kenney/rock_smallFlatA.glb', [8.0, 0.0, 6.0], 1.0),
    gltf('grass-2', '../../../assets/kenney/rock_smallFlatB.glb', [-18.0, 0.0, 8.0], 1.0),
    gltf('rex', '../../../assets/farm/goat.glb', [30.0, 0.0, -20.0], 1.5)]
cow_clips = {'idle': 'Walk', 'run': 'Walk', 'turn': 'Walk', 'walk': 'Walk', 'graze': 'Walk'}
def cow(name, seed, personality):
    return {'name': name, 'node': name, 'seed': seed, 'tags': ['animal', 'cow'], 'capabilities': ['can_walk', 'can_graze'],
            'clips': cow_clips, 'gait': {'walkSpeed': 1.4, 'runSpeed': 3.0, 'accel': 2.5, 'decel': 3.5},
            'personality': personality,
            'perception': {'range': 35.0, 'fieldOfView': 300.0, 'proximityRange': 5.0, 'capacity': 12, 'hertz': 3.0,
                           'tags': {'grass': 2.0, 'cow': 1.5}},
            'behaviors': [
                {'kind': 'decide', 'hertz': 1.5, 'dwellTicks': 2.0, 'margin': 0.08, 'memorySeconds': 6.0, 'visitedCapacity': 4.0,
                 'mind': {'memory': {'recoverSeconds': 60.0, 'habituationSeconds': 8.0}, 'attention': {'tags': {'grass': 1.0, 'dog': 2.0}}},
                 'considerers': [
                    {'kind': 'investigate', 'name': 'graze', 'tags': ['grass'], 'affordance': 'graze', 'weight': 1.2,
                     'approach': 1.5, 'dwell': 4.0, 'maxRange': 35.0, 'traits': {'curiosity': 0.5}},
                    {'kind': 'react', 'name': 'spook', 'events': ['bark'], 'weight': 2.0, 'approach': 6.0, 'flee': 12.0,
                     'dwell': 2.0, 'fadeSeconds': 10.0},
                    {'kind': 'social', 'name': 'herd', 'tags': ['cow'], 'weight': 0.6, 'distance': 4.0, 'personalSpace': 2.5},
                    {'kind': 'holdPost', 'name': 'home', 'weight': 0.1, 'post': '', 'tolerance': 20.0, 'pull': 0.03},
                    {'kind': 'idle', 'name': 'idle', 'weight': 0.05}]},
                {'kind': 'ground', 'slopeAlign': 0.55, 'bodyRadius': 1.2}]}
d['entities'] = [
    cow('bess', 101, {'curiosity': 0.6, 'caution': 0.9, 'sociability': 0.8}),
    cow('daisy', 202, {'curiosity': 0.4, 'caution': 0.95, 'sociability': 0.9}),
    {'name': 'grass-1', 'node': 'grass-1', 'tags': ['grass', 'flora'],
     'interactions': [{'name': 'graze', 'activity': 'graze', 'duration': 5.0, 'range': 2.5, 'exclusive': True, 'requires': ['can_graze']}]},
    {'name': 'grass-2', 'node': 'grass-2', 'tags': ['grass', 'flora'],
     'interactions': [{'name': 'graze', 'activity': 'graze', 'duration': 5.0, 'range': 2.5, 'exclusive': True, 'requires': ['can_graze']}]},
    {'name': 'rex', 'node': 'rex', 'tags': ['animal', 'dog'],
     'actions': [{'kind': 'wait', 'duration': 40.0}, {'kind': 'set', 'onComplete': 'bark'}]},
]
d['worldEvents'] = [{'name': 'bark', 'radius': 80.0, 'magnitude': 1.0}]
json.dump(d, open(root + 'examples/labs/character/pasture.scene.json', 'w'), indent=1)
print('ok')
