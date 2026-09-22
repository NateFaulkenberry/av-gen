# Generates examples/labs/character/autonomy-demo.scene.json (Phase D Success Demonstration fixture).
import json, copy, sys
import os
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..') + '/'
base = json.load(open(root + 'examples/labs/character/river-crossing.scene.json'))
glow = json.load(open(root + 'examples/world/glowmere-valley-2-multicam.scene.json'))
rook_anim = next(n for n in glow['nodes'] if n['name'] == 'rook')['animation']
hero_t = json.load(open(root + 'examples/labs/character/guard-post.scene.json'))['heroes'][0]

def hero(name, pos, radius, height):
    h = copy.deepcopy(hero_t); h['name'] = name; h['position'] = pos; h['radius'] = radius; h['height'] = height
    return h

def gltf(name, asset, pos, scale, yaw=0.0, anim=None):
    n = {'name': name, 'kind': 'gltf', 'asset': asset, 'position': pos, 'rotation': [0.0, yaw, 0.0], 'scale': [scale]*3}
    if anim: n['animation'] = anim
    return n

d = {k: base[k] for k in ('format', 'version', 'camera', 'lightRig', 'environment', 'navBodyRadius', 'navWadeDepth', 'post')}
d['name'] = 'Autonomy Demo (Phase D)'
d['camera'] = {'mode': 1, 'position': [-40.0, 45.0, -95.0], 'target': [-20.0, 1.5, -5.0], 'fov': 50.0, 'orbitSpeed': 0.0}
ground = copy.deepcopy(base['nodes'][0])
ground['world']['name'] = 'autonomy-demo-flat'
d['heroes'] = [hero('rock-1', [-48.0, 0.0, -20.0], 3.2, 3.0),
               hero('cairn-east', [40.0, 0.0, -30.0], 1.0, 3.0),
               hero('cairn-west', [-95.0, 0.0, -50.0], 1.0, 3.0),
               hero('cairn-north', [-20.0, 0.0, 45.0], 1.0, 3.0)]
alien_anim = copy.deepcopy(rook_anim)
d['nodes'] = [ground,
    gltf('scout', '../../../assets/aliens/alien-scout.glb', [-74.0, 0.0, -30.0], 1.94, 90.0, copy.deepcopy(alien_anim)),
    gltf('warden', '../../../assets/aliens/alien-pilot.glb', [-6.0, 0.0, -44.0], 1.94, -60.0, copy.deepcopy(alien_anim)),
    gltf('mushroom-1', '../../../assets/kenney/mushroom_redTall.glb', [-26.0, 0.0, -10.0], 3.0),
    gltf('mushroom-2', '../../../assets/kenney/mushroom_tanTall.glb', [22.0, 0.0, 6.0], 3.0),
    gltf('rock-1', '../../../assets/kenney/rock_largeA.glb', [-48.0, 0.0, -20.0], 4.0),
    gltf('old-tree', '../../../assets/kenney/tree_tall.glb', [-2.0, 0.0, -18.0], 5.0),
    {'name': 'saucer', 'kind': 'procedural', 'position': [60.0, 22.0, -5.0], 'rotation': [0.0, 0.0, 0.0], 'scale': [1.0, 1.0, 1.0],
     'procedural': {'source': {'kind': 'mesh', 'asset': '../../../assets/imported/ufo.gltf'}, 'sourceTransform': {'scale': [0.0347]*3},
                    'distribution': {'kind': 'single'},
                    'material': {'baseColor': [0.052, 0.058, 0.076], 'roughness': 0.3, 'metallic': 0.88, 'emissiveColor': [0.1, 0.52, 1.0], 'emissiveIntensity': 1.5}},
     'visible': True}]

clips = {'idle': 'Idle', 'walk': 'Walking', 'run': 'Running', 'turn': 'Idle_turn', 'observe': 'Idle', 'react': 'Fight_idle', 'fall': 'Fall_loop'}
gait = {'walkSpeed': 1.8, 'runSpeed': 4.2, 'accel': 3.0, 'decel': 4.0}
perception = {'range': 45.0, 'fieldOfView': 220.0, 'proximityRange': 6.0, 'capacity': 16, 'hertz': 4.0, 'occlusionTestsPerSecond': 0.0,
              'tags': {'ufo': 4.0, 'glowing': 2.0, 'alien': 1.5}}

def mind():
    return {'attention': {'tags': {'ufo': 3.0, 'glowing': 1.2, 'alien': 0.8}, 'maxHoldSeconds': 5.0, 'minDwellSeconds': 0.8},
            'memory': {'recoverSeconds': 150.0, 'habituationSeconds': 6.0, 'failSeconds': 30.0, 'eventSeconds': 12.0},
            'hearing': 1.0}

def considerers():
    return [
        {'kind': 'interest', 'name': 'roam', 'weight': 0.45, 'source': 'perceived',
         'weights': {'vista': 1.2, 'landmark': 1.0, 'glow': 1.0, 'character': 0.0, 'water': 0.9},
         'approach': 4.0, 'dwell': 2.0, 'minRange': 10.0, 'maxRange': 60.0, 'noveltyRadius': 14.0, 'noveltyPenalty': 0.15,
         'traits': {'wanderFrequency': 1.0}},
        {'kind': 'investigate', 'name': 'investigate', 'tags': ['glowing'], 'weight': 1.6, 'approach': 2.2, 'dwell': 5.0,
         'maxRange': 45.0, 'staleSeconds': 6.0, 'affordance': 'inspect', 'traits': {'curiosity': 1.5}},
        {'kind': 'investigate', 'name': 'watch-sky', 'tags': ['ufo'], 'intent': 'observe', 'weight': 4.0, 'approach': 16.0,
         'dwell': 10.0, 'maxRange': 60.0, 'staleSeconds': 4.0, 'activity': 'observe', 'traits': {'curiosity': 1.0}},
        {'kind': 'react', 'name': 'startle', 'events': ['crack', 'bloom', 'beam'], 'weight': 2.5, 'fadeSeconds': 20.0, 'approach': 5.0, 'flee': 16.0, 'dwell': 3.0},
        {'kind': 'social', 'name': 'company', 'tags': ['alien'], 'weight': 1.0, 'distance': 3.5, 'dwell': 3.0},
        {'kind': 'holdPost', 'name': 'home', 'weight': 0.08, 'post': '', 'tolerance': 45.0, 'pull': 0.02},
        {'kind': 'idle', 'name': 'idle', 'weight': 0.05}]

def alien(name, seed, personality):
    return {'name': name, 'node': name, 'seed': seed, 'tags': ['alien'], 'capabilities': ['can_walk', 'can_inspect'], 'clips': clips, 'gait': gait,
            'personality': personality,
            'behaviors': [
                {'kind': 'decide', 'hertz': 2.0, 'dwellTicks': 2.0, 'margin': 0.08, 'memorySeconds': 6.0,
                 'memoryCapacity': 16.0, 'visitedCapacity': 6.0, 'stallSeconds': 10.0, 'stallDistance': 1.5,
                 'mind': mind(), 'considerers': considerers()},
                {'kind': 'lookAt', 'turnRate': 110.0, 'weight': 1.0},
                {'kind': 'ground', 'slopeAlign': 0.5, 'bodyRadius': 0.9}],
            'perception': perception}

d['entities'] = [
    alien('scout', 424242, {'curiosity': 0.9, 'caution': 0.2, 'sociability': 0.7, 'eventSensitivity': 0.7, 'attentionSpan': 0.5}),
    alien('warden', 77777, {'curiosity': 0.3, 'caution': 0.85, 'sociability': 0.4, 'eventSensitivity': 0.8, 'attentionSpan': 0.3}),
    {'name': 'mushroom-1', 'node': 'mushroom-1', 'tags': ['mushroom', 'glowing', 'flora'],
     'interactions': [{'name': 'inspect', 'activity': 'observe', 'duration': 4.0, 'range': 3.0, 'exclusive': False, 'requires': ['can_inspect'], 'onComplete': 'inspected'}]},
    {'name': 'mushroom-2', 'node': 'mushroom-2', 'tags': ['mushroom', 'glowing', 'flora'],
     'interactions': [{'name': 'inspect', 'activity': 'observe', 'duration': 4.0, 'range': 3.0, 'exclusive': False, 'requires': ['can_inspect'], 'onComplete': 'inspected'}],
     'actions': [{'kind': 'wait', 'duration': 95.0}, {'kind': 'set', 'onComplete': 'bloom'}]},
    {'name': 'old-tree', 'node': 'old-tree', 'tags': ['tree', 'landmark'],
     'actions': [{'kind': 'wait', 'duration': 60.0}, {'kind': 'set', 'onComplete': 'crack'}]},
    {'name': 'saucer', 'node': 'saucer', 'tags': ['ufo', 'craft', 'world_effect'],
     'behaviors': [{'kind': 'orbit', 'authority': 'simulation', 'radius': 105.0, 'rate': 1.5, 'phase': -45.0}],
     'actions': [{'kind': 'wait', 'duration': 150.0}, {'kind': 'set', 'onComplete': 'beam'}, {'kind': 'wait', 'duration': 5.0}, {'kind': 'set', 'onComplete': 'beam'}]},
]
d['worldEvents'] = [{'name': 'inspected', 'radius': 0.0, 'magnitude': 0.0}, {'name': 'crack', 'radius': 80.0, 'magnitude': 1.0}, {'name': 'bloom', 'radius': 45.0, 'magnitude': 0.8}, {'name': 'beam', 'radius': 70.0, 'magnitude': 0.7}]
json.dump(d, open(root + 'examples/labs/character/autonomy-demo.scene.json', 'w'), indent=1)
print('ok')
