"""Two more hero mushrooms for Glowmere Valley 3 (the art pass, item 5: docs/glowmere-valley-3/art-pass/00-brief.md).

"Use the existing procedural mushroom generator to create two additional hero mushrooms ... visually unique from
the existing hero mushrooms ... designated/configured as hero mushrooms so they participate in the existing hero
systems ... integrate them naturally into the current environment."

**Where they come from.** The Phase 4 search itself (`tests/rendering/test_mushroom_search.cpp`, 800 Sobol
candidates of the `mushroom` generator, farthest-point selection at alpha 0.45), asked for twelve winners instead
of ten. The selection is greedy and deterministic, so the first ten are the ten the valley already has, byte for
byte, and the eleventh and twelfth are the two morphologies farthest from all ten -- not a tweak of one of them:

  opal   #251  the thickest cap the search kept (0.346), a squat dome over a slim stem, and its light in the
               CAP (emission structure 2): a lamp-like glowing dome, where the ten glow from their gills
  sail   #707  a thin flat plate tilted 16 degrees on a curved stem, one lobe, its light UNDER the cap
               (structure 1): a leaning sail that glows from beneath

Both are recorded in `examples/organisms/glowmere2-heroes.json` (entries 11 and 12). Each is built exactly as
Glowmere Valley 2 built its ten (`tools/make_glowmere_valley_2.py`, Phase 5): four generated parts (cap, under,
stem, gills) from the 18 recorded values, GV2's materials for a cool hero (the elder keeps the one warm light),
a spore-fall parented to the cap at the mesh-derived gill anchor, and a hero record pointing at the crown. The
anchors are the generator's own, read off the meshes by `avgen_tests "probe: hero spore anchors"`.

**Where they stand.** The central east of the valley had no hero: the ten stand along the river's west bank, in
the north-east and on the far terraces. `hero_sites.py` in the pass's notes scored every open site by the shots
that see it at 25-130 m and by the one rule the cut already applies to heroes (`look.hero_pulse_plan`: the two
nearest heroes in frame within 160 m pulse), so a new hero adds a pulse to a shot rather than taking one from
the hero that shot is about. The opal stands at the east meadow's north edge, where the herd grazes and Vane
walks (seen in s15, s36, s42, s64, s65); the sail on the east flats across the river from the elder (s02, s09,
s12, s24, s38, s43, s58, s59, s71). No tall plant stands under either cap (`avgen_scatter_probe`), and they are
seated on the ground by `world.seat_heroes` with the other ten.

**What makes them heroes.** A `heroes` entry in the scene and in the project (ranked last of the featured
mushrooms, `reactivity.FEATURED`, so the proposal's layers stay where they were and these two relight on the
section changes); a Hero Pulse that the cut fires on its downbeats wherever they are in frame
(`look.apply_hero_pulses`), except in a shot whose camera passes within `look.NEW_HERO_LENS_CLEARANCE` of them
(the Critic found the opal's ring washing s12 and s36, shots about the herd); the drop ring on their glowing
parts (`reactivity.prepare`); and, as heroes, they are obstacles the walkers go round (ADR-193).
"""

import copy
import json
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
RECORD = ROOT / "examples" / "organisms" / "glowmere2-heroes.json"

# GV2's palette and its order of the 18 generator values (make_glowmere_valley_2.py, Phase 5).
PALETTE = {
    "shadow": [0.008, 0.016, 0.048],
    "secondary": [0.3085, 0.1208, 1.0],
    "primary": [0.06, 0.82, 1.0],
    "foliage": [0.02, 1.0, 0.58],
}
ORDER = ["aspect", "rimTangentDeg", "centreTangentDeg", "capThickness", "stemCurvature", "stemTaper",
         "stemBulgePosition", "stemBulgeWidth", "lobeCount", "lobeDepth", "capTiltDeg", "edgeWaviness",
         "surfaceNoiseAmp", "surfaceNoiseScale", "gillCount", "gillDepth", "emissionStructure", "emissionIntensity"]
PART_ROLES = ["cap", "under", "stem", "gills"]

# name, the record's candidate index, where (x, z), height in metres, the unit gill anchor, the unit total height
# and the unit gill radius (the probe's numbers). Each faces the elder, the valley's centre.
NEW_HEROES = [
    ("opal", 251, (80.0, -5.0), 6.8, (0.0996, 0.9927, 0.0649), 1.3407, 0.4087),
    ("sail", 707, (42.0, 43.0), 7.4, (0.3524, 0.9872, 0.1921), 1.3778, 0.6879),
]
FACING = (-12.0, 52.0)
# The project dims every cool hero's underside and gills to a fifth of their material's light (its
# `emissiveBoost`), which the ten already carry; the two new ones match them.
UNDER_BOOST = 0.2
# Ranked after the ten (reactivity.FEATURED, 0.5 - 0.0215 i).
FOCAL = {"opal": 0.4, "sail": 0.35}


def mul(c, k):
    return [round(v * k, 5) for v in c]


def hero_materials(structure, emission):
    """GV2's `hero_materials` for a cool hero (index > 0)."""
    glow = PALETTE["primary"] if structure % 2 == 0 else PALETTE["foliage"]
    cap = {"program": "glowmere2Cap", "baseColor": mul(PALETTE["secondary"], 0.30),
           "roughness": 0.44, "emissiveColor": mul(glow, 0.10), "emissiveIntensity": 0.30}
    under = {"program": "glowmere2TissueCool", "baseColor": mul(PALETTE["secondary"], 0.18),
             "roughness": 0.52, "emissiveColor": glow, "emissiveIntensity": 0.18}
    stem = {"baseColor": [0.24, 0.12, 0.25], "roughness": 0.62,
            "emissiveColor": mul(glow, 0.25), "emissiveIntensity": 0.06}
    gills = {"program": "glowmere2TissueCool", "baseColor": mul(PALETTE["shadow"], 1.0),
             "roughness": 0.40, "emissiveColor": glow, "emissiveIntensity": emission}
    if structure == 1:
        under["emissiveIntensity"] = emission * 0.8
        gills["emissiveIntensity"] = emission * 0.25
    elif structure == 2:
        cap["emissiveIntensity"] = emission * 0.45
        cap["emissiveColor"] = mul(glow, 0.5)
    return [cap, under, stem, gills], glow


def build(name, index, where, height, anchor, unit_h, gill_r, ground, seed):
    """The nodes, the scene hero record and the spore budget for one hero, as GV2 made its ten."""
    record = json.loads(RECORD.read_text())
    rec = next(h for h in record["heroes"] if h["index"] == index)
    values = [float(rec["parameters"][k]) for k in ORDER]
    structure = int(round(values[16]))
    emission = float(values[17])
    mats, glow = hero_materials(structure, emission)
    x, z = where
    yaw = math.atan2(FACING[0] - x, FACING[1] - z)  # local +z toward the elder
    scale = round(height / unit_h, 4)
    base_y = round(ground.height(x, z) - 0.12, 3)
    nodes = []
    for part in range(4):
        nodes.append({
            "name": f"{name}-{PART_ROLES[part]}",
            "kind": "procedural",
            "position": [x, base_y, z],
            "rotation": [0.0, round(math.degrees(yaw), 2), 0.0],
            "procedural": {
                "source": {"kind": "generated",
                           "generated": {"generator": record["generator"],
                                         "generatorVersion": record["generatorVersion"],
                                         "schemaHash": record["schemaHash"], "index": index, "values": values},
                           "generatedPart": part},
                "sourceTransform": {"scale": [scale, scale, scale]},
                "distribution": {"kind": "single"},
                "material": mats[part],
                "motion": {"stiffness": 18.0 if part != 3 else 0.8, "mass": 120.0 if part != 3 else 0.5,
                           "damping": 0.95, "windSensitivity": 0.16 if part != 3 else 0.5,
                           "bendLimit": 0.015 if part != 3 else 0.09, "tipAmplitude": 0.01 if part != 3 else 0.06},
            },
        })
    ax, ay, az = anchor
    spore_radius = round(gill_r * scale, 3)
    spore_rate = round(min(24.0, max(3.0, math.pi * spore_radius * spore_radius * 0.3)), 1)
    spore_capacity = min(1024, 1 << int(math.ceil(math.log2(max(64.0, spore_rate * 22.0 * 1.35)))))
    nodes.append({
        "name": f"{name}-spores", "kind": "particles", "parent": f"{name}-cap",
        "position": [round(ax * scale, 3), round(ay * scale, 3), round(az * scale, 3)], "visible": True,
        "particles": {
            "position": [0.0, 0.0, 0.0], "shape": "disc", "seed": seed,
            "extent": [spore_radius, 0.15, spore_radius], "capacity": spore_capacity, "spawnRate": spore_rate,
            "lifetimeMin": 16.0, "lifetimeMax": 22.0, "speedMin": 0.10, "speedMax": 0.20,
            "direction": [0.0, -1.0, 0.0], "spread": 0.22, "drag": 0.08, "gravity": [0.0, -0.05, 0.0],
            "sizeStart": 0.07, "sizeEnd": 0.025, "blend": "additive",
            "colorStart": list(glow) + [0.0], "colorEnd": list(glow) + [0.0], "emissive": 3.936,
            "opacityCurve": [{"t": 0.0, "value": 0.0}, {"t": 0.18, "value": 0.75},
                             {"t": 0.7, "value": 0.55}, {"t": 1.0, "value": 0.0}],
            "turbulence": 0.09, "turbulenceScale": 0.6, "turbulenceSpeed": 0.1, "softness": 0.18,
            "fogCoupling": 1.0,
        },
    })
    # The crown, where the camera frames a hero: the gill anchor turned by the node's yaw (the engine's
    # R_y), and a little over it.
    ca, sa = math.cos(yaw), math.sin(yaw)
    hero = {
        "name": f"{name}-cap",
        "position": [round(x + (ax * ca + az * sa) * scale, 3), round(base_y + ay * scale + height * 0.06, 3),
                     round(z + (-ax * sa + az * ca) * scale, 3)],
        "yaw": round(yaw, 3), "scale": 1.0,
        "radius": round(height * 0.42, 2), "height": round(height, 2),
        "importance": 0.25, "focalWeight": FOCAL[name],
        "preferredCameraDistance": round(height * 3.1, 1), "preferredCameraElevation": 8.0,
        "activationRadius": round(height * 9.0, 1), "colorAccent": PALETTE["primary"],
        "reactionProfile": "organism",
    }
    return nodes, hero, (spore_radius, spore_rate, spore_capacity), structure


def apply(project, scene, ground, report):
    names = [f"{n}-{p}" for n, *_ in NEW_HEROES for p in PART_ROLES + ["spores"]]
    scene["nodes"] = [n for n in scene["nodes"] if n.get("name") not in names]
    params = project["parameters"]
    effects = project.setdefault("effects", [])
    template = next(e for e in effects if e.get("id") == "lantern-cap-hero-pulse")
    for i, (name, index, where, height, anchor, unit_h, gill_r) in enumerate(NEW_HEROES):
        nodes, hero, spores, structure = build(name, index, where, height, anchor, unit_h, gill_r, ground,
                                               seed=4100 + (10 + i) * 17)
        scene["nodes"].extend(nodes)
        for holder in (scene, project):
            heroes = holder.setdefault("heroes", [])
            heroes[:] = [h for h in heroes if h.get("name") != hero["name"]] + [copy.deepcopy(hero)]
        for part in ("under", "gills"):
            params[f"nodes/{name}-{part}/emissiveBoost"] = UNDER_BOOST
        pulse = copy.deepcopy(template)
        pulse["id"] = f"{name}-cap-hero-pulse"
        pulse["owner"] = {"kind": "entity", "name": f"{name}-cap"}
        project["effects"] = [e for e in project["effects"] if e.get("id") != pulse["id"]] + [pulse]
        report.append(f"hero {name}: search pick #{index} (emission structure {structure}), {height} m at "
                      f"({where[0]:.1f}, {where[1]:.1f}); spores {spores[0]} m, {spores[1]}/s, capacity {spores[2]}")
