"""The astronaut musicians of Glowmere Valley 3 (the art pass's addendum, docs/glowmere-valley-3/art-pass/00-brief.md).

Two performers, the validated prototype's (`tools/make_astronaut_musicians.py`, docs/prototypes/astronaut-musicians):
the keyboardist (astronaut + keyboard, folding stand and bench, the Mixamo piano clip) and the drummer (astronaut +
drum kit, the Mixamo drum clip, and the build's procedural `Flail` for the abduction). Their GLBs are generated
and never committed (their licences); a checkout without them loads the scene with each node skipped and a
warning.

**Where.** By the river in the middle of the valley, under the elder's cap, where the riser's abduction happens
(E5): the drummer stands where the saucer took the horse, so the set piece and its shots keep their place, and the
keyboardist faces him 11 m north, on the elder's side. Each group's spot was searched (`place_search.py` in the
pass's notes): no fern, bush, fungus or boulder inside either footprint (`avgen_scatter_probe`), at least 4 m of
air between each footprint and every camera the cut flies, and the drummer within 3.2 m of E5's old lift point.
Each group is one transform -- its performer and its props share the position, the facing, GV3's cast scale (1.94,
cast.py) and a tilt fitted to the ground under the footprint (2-4 degrees), so nothing floats or sinks. The grass
is cleared under each footprint and nothing else: a clearing the size of the footprint, softened over 1.5 m.

**Heroes.** Each performer is an entity (the drummer must be one: a set piece lifts entities) and a hero record in
the scene and the project, so the cut's hero pulse fires for them wherever they are in frame, and the walkers go
round them (ADR-193).

**The aura.** "Outward-radiating rainbow bioluminescent energy that pulses to the music ... bioluminescent aura /
energy field, rather than rainbow outline / neon cartoon glow." Two existing effect types, both of the cheapest
classes, and no new shader:
  * their hero pulse is a Ground Pulse in rainbow mode: rings that run out from each performer over the ground
    and light the plants they cross (the foliage response is the valley's own bioluminescence answering), fired
    by the cut on the downbeats of the shots they are in, pumped by the scored kicks like every hero's;
  * their suits carry a Bioluminescence pattern: scattered photophores whose hues wander across the wheel, each
    breathing on its own phase, with a slow wave rolling up the body.
  Paired and complementary: the same downbeats, the drummer's light answering the kick and the keyboardist's the
  lead (the mid band), both swelling with the section's energy; their rainbows run at different speeds and start
  apart on the wheel.

**The abduction.** E5 (ufo.plan.json) now lifts the drummer and gives him back (ADR-984): he rises from behind his
kit on bar 93's lift, flailing (the gait plays `Flail` while he is carried), dissolves into the saucer on the drop,
and is put back behind the kit, playing, six seconds later, during a shot that does not see it. His pulse is held
off from the beam to his return (`look.hero_pulse_plan`), so no ring runs out from a body in the beam or from an
empty seat.
"""

import copy
import math

import numpy as np

SCALE = 1.94  # GV3's cast scale (cast.py): the aliens and the animals stand at it, and so do the performers
ASSETS = "../../assets/musicians/"
# Each group: its performer (an entity), its props, where its origin stands and whom it faces. Footprints are the
# group's extent in its own frame at 1x (x right, z forward: the way the performer faces), padded.
GROUPS = [
    {"name": "drummer", "glb": "astronaut_drums.glb", "clip": "Drums", "at": (-1.7, 70.9), "faces": "keyboardist",
     "props": [("drum-kit", "drum_kit.glb")], "footprint": (-1.10, 0.80, -0.50, 1.20)},
    {"name": "keyboardist", "glb": "astronaut_keys.glb", "clip": "Piano", "at": (-1.7, 59.9), "faces": "drummer",
     "props": [("keyboard-rig", "keyboard_set.glb")], "footprint": (-0.95, 0.95, -0.50, 1.05)},
]
CLIPS = {
    # The drummer's carried gait plays the flail: the lift hands the gait a speed (the set piece's `animalGait`)
    # and a performer's walk is his flail. Everything standing is the drum clip.
    "drummer": {"idle": "Drums", "turn": "Drums", "observe": "Drums", "react": "Drums", "walk": "Flail",
                "run": "Flail", "fall": "Flail"},
    "keyboardist": {"idle": "Piano", "turn": "Piano", "observe": "Piano", "react": "Piano", "walk": "Piano",
                    "run": "Piano"},
}
GAIT = {"matchRate": False, "walkSpeed": 1.0, "runSpeed": 40.0, "rateMin": 1.0, "rateMax": 1.0,
        "runEnter": 60.0, "runExit": 50.0, "moveEnter": 0.2, "moveExit": 0.1, "accel": 100.0, "decel": 100.0,
        "idleRate": 1.0}
CLEAR_SOFTNESS = 1.5

# ---- the aura --------------------------------------------------------------------------------------
# The hero pulse, in rainbow: the cut's own pulse (look.HERO_PULSE_*) sets its trigger and timing; these are its
# look. Gentler than a mushroom's (intensity 5, 58 m): a performer's rings stay near the stage.
PULSE_LOOK = {
    "appearance": {"intensity": 3.2, "edgeIntensity": 5.5, "rainbow": True, "rainbowSaturation": 0.62,
                   "rainbowBrightness": 0.9, "rainbowScale": 0.035},
    "propagation": {"speed": 10.0, "range": 34.0, "frontWidth": 3.0, "trailLength": 12.0, "ringCount": 2.0,
                    "verticalExtent": 6.0},
    "response": {"ground": 0.8, "foliage": 1.7, "surface": 0.6, "emissive": 1.2},
    "sparkle": {"enabled": True, "density": 1.1, "intensity": 2.0},
}
PULSE_VARIATION = {"drummer": {"rainbowSpeed": 0.34}, "keyboardist": {"rainbowSpeed": 0.26}}
# The suit's photophores: a Glowmere cyan-green at the centre of a wide hue spread, each cell on its own breath.
SUIT = {"pattern": 0.0, "color": [0.15, 0.95, 0.78], "intensity": 3.0, "scale": 22.0, "coverage": 0.34,
        "colorVariation": 0.5, "breatheRate": 0.3, "breatheDepth": 0.7, "waveSpeed": 0.45, "waveInterval": 5.54,
        "waveGain": 1.6}
SUIT_VARIATION = {"drummer": {"color": [0.95, 0.35, 0.85]}, "keyboardist": {}}
# What each suit answers: the drummer the scored kick, the keyboardist the lead; both the section's energy.
SUIT_ROUTES = {
    "drummer": ("timeline.kick", 5.0, 6.0, 280.0),
    "keyboardist": ("audio.mid", 4.0, 60.0, 520.0),
}


def _tilt_rotation(yaw_deg, gx, gz):
    """The engine's node Euler (degrees; glm's Rz * Ry * Rx) for a node turned `yaw_deg` about +Y and then tilted
    so its up is the normal of the plane h = h0 + gx x + gz z."""
    a = math.radians(yaw_deg)
    ry = np.array([[math.cos(a), 0.0, math.sin(a)], [0.0, 1.0, 0.0], [-math.sin(a), 0.0, math.cos(a)]])
    n = np.array([-gx, 1.0, -gz])
    n /= np.linalg.norm(n)
    up = np.array([0.0, 1.0, 0.0])
    axis = np.cross(up, n)
    s = np.linalg.norm(axis)
    if s < 1e-9:
        rt = np.eye(3)
    else:
        axis /= s
        ang = math.acos(max(-1.0, min(1.0, float(n @ up))))
        k = np.array([[0.0, -axis[2], axis[1]], [axis[2], 0.0, -axis[0]], [-axis[1], axis[0], 0.0]])
        rt = np.eye(3) + math.sin(ang) * k + (1.0 - math.cos(ang)) * (k @ k)
    r = rt @ ry
    beta = math.asin(max(-1.0, min(1.0, -r[2, 0])))
    alpha = math.atan2(r[2, 1], r[2, 2])
    gamma = math.atan2(r[1, 0], r[0, 0])

    def wrap(v):
        return (v + math.pi) % (2.0 * math.pi) - math.pi
    # Both Euler triples that make this rotation; the one whose pitch and roll are small reads as what it is
    # in the Parameters panel (a yaw of 180 and a two-degree tilt, not 180/0/180), and it is the one the
    # entity's yaw offsets add to as a turn about +Y.
    other = (wrap(alpha + math.pi), wrap(math.pi - beta), wrap(gamma + math.pi))
    first = (alpha, beta, gamma)
    best = min((first, other), key=lambda t: abs(wrap(t[0])) + abs(wrap(t[2])))
    return [round(math.degrees(best[0]), 3), round(math.degrees(best[1]), 3), round(math.degrees(best[2]), 3)]


def _yaw_to(src, dst):
    """Degrees about +Y that turn a model's +z toward `dst`: forward = (sin, cos)."""
    return math.degrees(math.atan2(dst[0] - src[0], dst[1] - src[1]))


def _footprint_world(g, yaw_deg):
    x0, x1, z0, z1 = [v * SCALE for v in g["footprint"]]
    a = math.radians(yaw_deg)
    cx, cz = g["at"]

    def world(lx, lz):
        return (cx + lx * math.cos(a) + lz * math.sin(a), cz - lx * math.sin(a) + lz * math.cos(a))
    return world, (x0, x1, z0, z1)


def place(group, yaw_deg, ground):
    """The group's origin height and rotation: a plane fitted to the ground under its footprint."""
    world, (x0, x1, z0, z1) = _footprint_world(group, yaw_deg)
    pts = []
    for i in range(7):
        for j in range(7):
            pts.append(world(x0 + (x1 - x0) * i / 6, z0 + (z1 - z0) * j / 6))
    pts = [(round(x, 2), round(z, 2)) for x, z in pts]
    hs = [h for h, _ in ground.probe(pts)]
    a = np.array([[x, z, 1.0] for x, z in pts])
    gx, gz, c0 = np.linalg.lstsq(a, np.array(hs), rcond=None)[0]
    cx, cz = group["at"]
    y0 = float(gx * cx + gz * cz + c0)
    resid = float(np.max(np.abs(a @ np.array([gx, gz, c0]) - np.array(hs))))
    return round(y0, 3), _tilt_rotation(yaw_deg, float(gx), float(gz)), resid, world, (x0, x1, z0, z1)


def apply(project, scene, ground, report):
    by = {g["name"]: g for g in GROUPS}
    names = {g["name"] for g in GROUPS} | {p for g in GROUPS for p, _ in g["props"]}
    scene["nodes"] = [n for n in scene["nodes"] if n.get("name") not in names]
    scene["entities"] = [e for e in scene.get("entities", []) if e.get("name") not in names]
    terrain = next(n for n in scene["nodes"] if n.get("kind") == "terrain")
    clearings = terrain.setdefault("clearings", [])
    params = project["parameters"]
    for g in GROUPS:
        yaw = _yaw_to(g["at"], by[g["faces"]]["at"])
        y0, rot, resid, world, (x0, x1, z0, z1) = place(g, yaw, ground)
        x, z = g["at"]
        anim = {"state": g["clip"], "blend": 0.35, "speed": 1.0, "updateHz": 30, "nearDistance": 25.0,
                "farHz": 15.0, "cullDistance": 360.0}
        nodes = [{"name": g["name"], "kind": "gltf", "asset": ASSETS + g["glb"], "position": [x, y0, z],
                  "rotation": rot, "scale": [SCALE] * 3, "visible": True, "animation": anim}]
        for prop, glb in g["props"]:
            nodes.append({"name": prop, "kind": "gltf", "asset": ASSETS + glb, "position": [x, y0, z],
                          "rotation": rot, "scale": [SCALE] * 3, "visible": True})
        scene["nodes"].extend(nodes)
        # The project restates node transforms over the scene's (ADR-264); these are new, so it states them
        # once, as the scene does, and nothing else can drift from them.
        for n in nodes:
            params[f"nodes/{n['name']}/position"] = list(n["position"])
            params[f"nodes/{n['name']}/rotation"] = list(n["rotation"])
            params[f"nodes/{n['name']}/scale"] = list(n["scale"])
        scene.setdefault("entities", []).append({
            "name": g["name"], "node": g["name"], "seed": 20260928 + len(scene["entities"]),
            "tags": ["musician", "astronaut"], "gait": dict(GAIT, blend=0.35), "clips": dict(CLIPS[g["name"]]),
            "fullDetailDistance": 120.0, "coarseInterval": 0.1, "cullDistance": 360.0,
            "behaviors": [], "reactions": [],
        })
        # The grass under the footprint, and nothing past it: the footprint's half-diagonal, softened.
        cx, cz = world(0.5 * (x0 + x1), 0.5 * (z0 + z1))
        clearings.append({"center": [round(cx, 2), round(cz, 2)],
                          "radius": round(0.5 * math.hypot(x1 - x0, z1 - z0), 2), "softness": CLEAR_SOFTNESS,
                          "strength": 1.0, "minHeight": 0.0})
        hero = {"name": g["name"], "position": [x, round(y0 + 2.0, 3), z], "yaw": round(math.radians(yaw), 3),
                "scale": 1.0, "radius": 2.2, "height": 3.4, "importance": 0.24 if g["name"] == "drummer" else 0.23,
                "focalWeight": 0.6, "preferredCameraDistance": 12.0, "preferredCameraElevation": 2.0,
                "activationRadius": 60.0, "colorAccent": [0.62, 0.45, 1.0], "reactionProfile": ""}
        for holder in (scene, project):
            heroes = holder.setdefault("heroes", [])
            heroes[:] = [h for h in heroes if h.get("name") != g["name"]] + [copy.deepcopy(hero)]
        report.append(f"musician {g['name']}: at ({x:.1f}, {y0:.2f}, {z:.1f}) facing {yaw:.0f} deg, tilted "
                      f"{rot[0]:.1f}/{rot[2]:.1f} deg to the ground (fit within {resid * 100:.1f} cm), x{SCALE}")
    effects(project)


def effects(project):
    """Each performer's rainbow hero pulse (a Ground Pulse) and the photophores on its suit."""
    fx = project.setdefault("effects", [])
    template = next(e for e in fx if e.get("id") == "lantern-cap-hero-pulse")
    ids = set()
    added = []
    for g in GROUPS:
        name = g["name"]
        pulse = copy.deepcopy(template)
        pulse["id"] = f"{name}-hero-pulse"
        pulse["name"] = "Hero Pulse (rainbow)"
        pulse["owner"] = {"kind": "entity", "name": name}
        pulse["style"] = ""
        for block, values in PULSE_LOOK.items():
            pulse["parameters"].setdefault(block, {}).update(copy.deepcopy(values))
        pulse["parameters"]["appearance"].update(PULSE_VARIATION[name])
        suit = {"id": f"{name}-aura", "type": "bioluminescence", "name": "Rainbow photophores",
                "owner": {"kind": "entity", "name": name}, "enabled": True, "order": 0, "style": "",
                "activation": "always",
                "timing": {"delay": 0.0, "lifetime": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "windowStart": 0.0,
                           "windowSeconds": 6.0, "repeatSeconds": 0.0},
                "parameters": dict(SUIT, **SUIT_VARIATION[name])}
        ids |= {pulse["id"], suit["id"]}
        added += [pulse, suit]
    project["effects"] = [e for e in fx if e.get("id") not in ids] + added


def routes(project, route):
    """The suits' music, after the reactivity proposal (it leaves alone a target something already routes)."""
    out = []
    for g in GROUPS:
        name = g["name"]
        source, amount, attack, decay = SUIT_ROUTES[name]
        target = f"fx/{name}-aura/intensity"
        out.append(route(source, target, amount, "add", {"attackMs": attack, "decayMs": decay}))
        out.append(route("section.energy", target, 1.0, "multiply",
                         {"attackMs": 800.0, "decayMs": 2400.0, "clampEnabled": True, "clampMin": 0.0,
                          "clampMax": 1.0, "remapEnabled": True, "remapInMin": 0.2, "remapInMax": 1.0,
                          "remapOutMin": 0.6, "remapOutMax": 1.35}))
    targets = {r["target"] for r in out}
    project["routes"] = [r for r in project.get("routes", []) if r.get("target") not in targets] + out
    return len(out)
