"""CP-C fixture (engineering, Phase 6): the art's CP-B r2 day scene plus six Glowmere aliens on the hop behaviour
(ADR-1958) beside the lit path, where the flight passes at 10 m. The aliens are referenced from assets/aliens, never
copied (commercial assets).

    python3 build.py --state day --flight --out DIR      # in the art worktree's examples/musical-geography
    python3 examples/musical-geography/eng/make_cpc.py DIR
"""
import json, math, os, sys
S = sys.argv[1]
p = S + "/musical-geography.scene.json"
d = json.load(open(p))
ALIENS = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "assets", "aliens")) + "/"
# beside the path (world.py's polyline), alternating sides, where the flight passes at ~10 m
path = [(40, -610), (-40, -690), (-150, -745), (-260, -800), (-360, -880)]
spots = [(0, 0.45, 9), (1, 0.15, -11), (1, 0.6, 13), (2, 0.25, -8), (2, 0.7, 12), (3, 0.4, -14)]
kinds = ["scout", "ranger", "elder", "pilot", "diver", "trooper"]
d.setdefault("entities", [])
for i, (seg, f, side) in enumerate(spots):
    (x0, z0), (x1, z1) = path[seg], path[seg + 1]
    dx, dz = x1 - x0, z1 - z0
    L = math.hypot(dx, dz)
    nx, nz = -dz / L, dx / L
    x = x0 + dx * f + nx * side
    z = z0 + dz * f + nz * side
    name = "hopper%d" % i
    d["nodes"].append({
        "name": name, "kind": "gltf", "asset": ALIENS + "alien-%s.glb" % kinds[i],
        "position": [round(x, 2), 0.0, round(z, 2)], "rotation": [0.0, 0.0, 0.0], "scale": [1.94, 1.94, 1.94],
        "animation": {"state": "Idle", "blend": 0.25, "speed": 1.0, "updateHz": 30, "nearDistance": 40.0,
                      "farHz": 20.0, "cullDistance": 600.0},
    })
    d["entities"].append({
        "name": name, "node": name, "seed": 7001 + 97 * i, "tags": ["alien"],
        "clips": {"idle": "Idle", "walk": "Walking", "run": "Running", "turn": "Idle_turn", "observe": "Idle",
                  "react": "Fight_idle", "jump": "Jumping", "fall": "Floating", "land": "Landing"},
        "gait": {"walkSpeed": 1.7, "runSpeed": 3.6},
        "behaviors": [{"kind": "hop", "hopChance": 0.45, "homeRadius": 28.0, "goalMin": 3, "goalMax": 7,
                       "pauseMin": 0.8, "pauseMax": 2.6, "airTime": 1.2, "apex": 2.4}],
    })
d["environment"]["tileDynamics"]["spring"] = {"depth": 0.7, "frequency": 1.6, "damping": 0.18,
                                              "rippleSpeed": 12.0, "rippleRadius": 5.0}
json.dump(d, open(p, "w"), indent=1)
print([(e["name"], n["position"]) for e, n in zip(d["entities"], d["nodes"][-6:])])
