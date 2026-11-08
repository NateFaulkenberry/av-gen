# Throwaway tile-instancing spike: N x N thin sharp slabs (0.9 x 0.15 x 0.9, pitch 1), per-instance height from field effectors.
# Usage: python3 make_spike.py <N> <shadows 0|1> <cull 0|1> <outdir>. Writes spike.scene.json + spike.json (Phase 1, throwaway).
import json, sys, os
N = int(sys.argv[1]); shadow = sys.argv[2] == "1"; cull = sys.argv[3] == "1"; out = sys.argv[4]
spacing = 1.0; tile = 0.9
def field(name, f): return {"name": name, "kind": "field", "position": [0,0,0], "field": dict({"strength": 1.0, "falloff": {"kind": "none"}, "position": [0,0,0]}, **f)}
nodes = [
 field("hills", {"kind": "noise", "frequency": 0.008, "speed": 0.02, "seed": 7, "strength": 14.0}),
 field("swellA", {"kind": "wave", "waveGeometry": "planar", "waveShape": "sine", "axis": [1,0,0.3], "point": [0,0,0], "wavelength": 60.0, "waveSpeed": 6.0, "waveWidth": 0.0, "amplitude": 1.0, "strength": 2.5}),
 field("swellB", {"kind": "wave", "waveGeometry": "planar", "waveShape": "sine", "axis": [-0.4,0,1], "point": [0,0,0], "wavelength": 37.0, "waveSpeed": 4.0, "waveWidth": 0.0, "amplitude": 1.0, "strength": 1.5}),
 field("tint", {"kind": "gradient", "axis": [0,1,0], "length": 20.0, "colorA": [0.1,0.2,0.8,1], "colorB": [1.0,0.5,0.4,1]}),
 {"name": "tiles", "kind": "procedural", "position": [0,0,0], "procedural": {
   "source": {"kind": "box", "size": [tile, 0.15, tile]},
   "distribution": {"kind": "grid", "gridCount": [N,1,N], "gridSpacing": [spacing,1,spacing]},
   "material": {"baseColor": [0.6,0.65,0.9], "roughness": 0.45, "metallic": 0.0},
   "lod": {"cull": cull, "maxDistance": 0, "count": 1},
   "effectors": [
     {"field": "hills", "op": "positionOffset", "blend": "add", "strength": 1.0, "axis": [0,1,0]},
     {"field": "swellA", "op": "positionOffset", "blend": "add", "strength": 1.0, "axis": [0,1,0]},
     {"field": "swellB", "op": "positionOffset", "blend": "add", "strength": 1.0, "axis": [0,1,0]},
     {"field": "tint", "op": "color", "blend": "add", "strength": 1.0}]}}]
fogc = [0.55,0.6,0.85]
scene = {"format": "avgen-scene", "version": 1, "name": "geo-spike",
 "camera": {"mode": 1, "position": [0.0, 9.0, N*0.45], "target": [0.0, 3.0, -N*0.1], "fov": 45.0, "orbitSpeed": 0.0},
 "environment": {"intensity": 0.5, "background": fogc, "fogColor": fogc, "fogDensity": 0.004, "volumeDensity": 0.0, "shadowRange": 200.0,
   "ambientOcclusion": False, "contactShadows": False, "depthPrepass": True,
   "sky": {"enabled": True, "zenithColor": [0.25,0.35,0.8], "horizonColor": fogc, "groundColor": fogc, "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False}},
 "nodes": nodes,
 "lights": [{"id": "key", "name": "key", "type": "directional", "direction": [0.5,-0.45,-0.6], "color": [1,0.85,0.7], "intensity": 3.0, "castsShadow": shadow, "contactShadow": False, "softness": 0.3}]}
os.makedirs(out, exist_ok=True)
json.dump(scene, open(os.path.join(out, "spike.scene.json"), "w"), indent=1)
proj = {"format": "avgen-project", "version": 4, "assets": {"scene": {"kind": "composition", "path": "spike.scene.json"}},
 "render": {"width": 1920, "height": 1080, "fps": 30}}
json.dump(proj, open(os.path.join(out, "spike.json"), "w"), indent=1)
