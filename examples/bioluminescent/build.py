"""THE RIFT: the first Environment, as one generated scene and three projects.

    python3 examples/bioluminescent/build.py

writes, beside this file:
    rift.scene.json      the world (generated; never edit it by hand)
    rift.json            Trench, offline (the music video)
    rift-live.json       live input, LIVE AUTO, MIDI (the instrument)
and the organisms under meshes/ (organisms.py).

How the music becomes the world (docs/prototypes/bioluminescent/04-design.md):

  kick ----------x seed mask (where the ground is disturbed) x wake front (how far down the canyon the music
                 has reached) = ignition  -->  PROPAGATION GRID (ADR-1201): fronts at waveSpeed m/s through a
                 patchy medium; refractory, so fronts are rings that collide and annihilate
                     u  excitation  -> mats, sea pens, crust, plankton flash; fans FLUORESCE magenta under it
                     e  energy      -> the canopy (crinoid photophores) answers later; walls and haze catch it
                     w  wake        -> an awakened reach keeps glowing between events (every layer's rest)
  highs ------------------------------> whip tips and siphonophore chains (spectrum fields), spores
  the arc (states) -------------------> how sensitive the medium is, how far the wake front runs ahead, how much
                                        collective light reaches the walls and the haze, the camera's behaviour
"""
from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(1, str(HERE.parent.parent / "tools"))
import drum_signals  # noqa: E402  (ADR-1620: the snare as a signal: a drum source's, else the backbeat)
import cp1  # noqa: E402  (placement helpers: candidates, records, value_noise, basis, quat_from_matrix)
import grounding  # noqa: E402  (seating on the drawn ground; the camera's corridor)
import land  # noqa: E402
import organisms  # noqa: E402

# ============================================================================================ the world's extent
Z0, Z1 = -1400.0, 1400.0           # the reach the flight covers
REGION = (-260.0, 260.0, Z0, Z1)
FLIGHT_Z = (-1300.0, 1300.0)

# ============================================================================================ palette (linear)
BLUE = [0.04, 0.38, 1.0]           # dinoflagellate, ~475 nm
DEEP_BLUE = [0.03, 0.22, 1.0]
CYAN = [0.0, 0.85, 1.0]
PEN = [0.0, 0.9, 0.85]            # turquoise-emerald: the meadows by the river (species separate by hue)
FIREFLY = [1.0, 0.82, 0.5]         # the whip tips: warm gold-white twinkles (firefly ~560 nm, softened)
VIOLET = [0.38, 0.14, 1.0]
PALE = [0.72, 0.92, 1.0]
VIOLET_DIM = [0.30, 0.14, 1.0]     # fans at rest
MAGENTA = [1.0, 0.08, 0.62]        # fans fluorescing under the wave
AMBER = [1.0, 0.52, 0.12]          # lanterns: rare, warm, the brightest per unit
EMBER = [1.0, 0.12, 0.04]          # crevice embers: barely there
BODY = [0.035, 0.045, 0.06]
GLOW = [0.06, 0.40, 1.0]           # the haze's colour where the medium is lit
WALL_GLOW = [0.02, 0.20, 1.0]      # the rock under a passing front: saturated deep blue, never pale

ZERO_DEFAULTS = [
    {"source": "audio.bass", "target": "root/scale", "op": "add", "amount": 0.0},
    {"source": "audio.mid", "target": "root/rotationSpeed", "op": "add", "amount": 0.0},
    {"source": "audio.rms", "target": "scene/brightness", "op": "add", "amount": 0.0},
    {"source": "audio.onset", "target": "root/impulse", "op": "add", "amount": 0.0},
]


def field(name, position=None, **f):
    f.setdefault("falloff", {"kind": "none"})
    return {"name": name, "kind": "field", "position": position or [0, 0, 0], "field": f}


def op(kind, dst, **kw):
    o = {"kind": kind, "dst": dst}
    o.update(kw)
    return o


# ============================================================================================ placement
def place(g, L=None):
    """The ecosystem along the whole reach (cp1.place's rules, over REGION).

    `L` is the LAYOUT ground: the world as it was before the floodplain (land.world(floodplain=False)). Where things
    grow -- the slope and wetness masks that shape the meadows' drifts -- is read from it, so the approved pattern is
    kept; how high they stand is read from `g`, the ground that is drawn. The floodplain flattened the floor by the
    river, and reading the masks from it tripled the sea pens (25,640 -> 83,578) and doubled the whips and lanterns.
    A wall organism must still be on a wall: its slope mask holds on both grounds."""
    L = L or g
    R = REGION
    P = {}
    dry = lambda X, Z: ~(g.wet(X, Z) | L.wet(X, Z))  # noqa: E731
    dxc = lambda X, Z: X - np.array([land.centre_x(z) for z in Z])  # noqa: E731
    dxr = lambda X, Z: X - np.array([land.river_x(z) for z in Z])  # noqa: E731

    # crinoids: two loose rows on the banks leaning over the river (the canopy), and a scatter on the lower walls
    r = np.random.default_rng(101)
    X, Z, S = [], [], []
    zs = np.arange(R[2] + 10, R[3] - 10, 14.0)
    zs = zs + r.uniform(-5, 5, len(zs))
    for k, z in enumerate(zs):
        side = 1 if k % 2 else -1
        X.append(land.river_x(z) + side * r.uniform(9, 22))
        Z.append(z)
        S.append(r.uniform(0.75, 1.55))
    for z in np.arange(R[2] + 20, R[3] - 20, 30.0):
        X.append(land.centre_x(z) + r.choice([-1, 1]) * r.uniform(35, 75))
        Z.append(z + r.uniform(-8, 8))
        S.append(r.uniform(0.45, 0.9))
    X, Z, S = np.array(X), np.array(Z), np.array(S)
    crinoids = lambda X, Z: cp1.records(g, X, Z, S, 0.0, 102, sink=SINK["crinoid"],  # noqa: E731
                                        facing=lambda x, z: np.array([land.river_x(z) - x, 0, 0.01]))
    P["crinoid"] = crinoids(X, Z)
    # the camera never passes through a trunk: a stalk inside the eye's corridor steps out sideways until it clears
    # by CAMERA_MARGIN (grounding.py), and is placed again from there (its height and its lean toward the river)
    start = np.asarray(P["crinoid"])[:, [0, 2]]
    for _ in range(4):
        moved, _far = grounding.clear_corridor(FLIGHT, EYE_HULL, P, CAMERA_MARGIN)
        if not moved:
            break
        a = np.asarray(P["crinoid"])
        P["crinoid"] = crinoids(a[:, 0], a[:, 2])
    shift = np.linalg.norm(np.asarray(P["crinoid"])[:, [0, 2]] - start, axis=1)
    GROUNDING["corridor/crinoid"] = {"moved": int((shift > 0).sum()), "maxShift": float(shift.max())}

    # sea-pen meadows on the floor and lower slopes, in drifts
    X, Z = cp1.candidates(g, 1.8, 201, R)
    sl = L.slope(X, Z)
    cl = cp1.value_noise(X, Z, 22.0, 7) * 0.7 + cp1.value_noise(X, Z, 7.0, 8) * 0.3
    keep = (sl < 0.45) & dry(X, Z) & (np.abs(dxc(X, Z)) < 60) & (cl > 0.42)
    X, Z = X[keep], Z[keep]
    P["seapen"] = cp1.records(g, X, Z, np.random.default_rng(202).uniform(0.6, 1.5, len(X)), 0.3, 203,
                               sink=SINK["seapen"])

    # polyp mats on gentle ground (the walls belong to the crust)
    X, Z = cp1.candidates(g, 0.45, 301, R)
    sl = L.slope(X, Z)
    d = np.abs(dxc(X, Z))
    p = np.clip(1.3 - d / 160.0, 0, 1) * (cp1.value_noise(X, Z, 15.0, 9) * 0.6 + 0.5)
    keep = (sl < 1.1) & dry(X, Z) & (np.random.default_rng(302).random(len(X)) < p)
    X, Z = X[keep], Z[keep]
    P["mat"] = cp1.records(g, X, Z, np.random.default_rng(303).uniform(0.7, 1.8, len(X)), 0.85, 304,
                            sink=SINK["mat"])

    # sea fans on the walls, face-on to the canyon's axis; fewer than CP2 (their violet must not own the walls)
    X, Z = cp1.candidates(g, 0.032, 401, R)
    sl = np.minimum(L.slope(X, Z), g.slope(X, Z))
    d = np.abs(dxc(X, Z))
    keep = (sl > 0.35) & (np.maximum(L.slope(X, Z), g.slope(X, Z)) < 3.0) & (d > 16) & (d < 120) &\
        (cp1.value_noise(X, Z, 30.0, 10) > 0.38)
    X, Z = X[keep], Z[keep]
    P["fan"] = cp1.records(g, X, Z, np.random.default_rng(402).uniform(0.6, 1.9, len(X)), 0.35, 403,
                           facing=cp1.toward_centre, sink=SINK["fan"])

    # whip tufts along the banks
    X, Z = cp1.candidates(g, 0.15, 501, R)
    dr = np.abs(dxr(X, Z))
    keep = (dr > 7) & (dr < 26) & dry(X, Z) & (L.slope(X, Z) < 0.8) & \
        (cp1.value_noise(X, Z, 12.0, 11) > 0.35)
    X, Z = X[keep], Z[keep]
    P["whips"] = cp1.records(g, X, Z, np.random.default_rng(502).uniform(0.7, 1.4, len(X)), 0.2, 503)

    # lanterns: rare clusters in hollows
    X, Z = cp1.candidates(g, 0.09, 601, R)
    keep = dry(X, Z) & (L.slope(X, Z) < 1.0) & (np.abs(dxc(X, Z)) < 70) & \
        (cp1.value_noise(X, Z, 9.0, 12) > 0.64)
    X, Z = X[keep], Z[keep]
    P["lanterns"] = cp1.records(g, X, Z, np.random.default_rng(602).uniform(0.8, 1.6, len(X)), 0.5, 603)

    # wall crust patches (emitters only): steep rock, clumped along ledges
    X, Z = cp1.candidates(g, 0.09, 801, R)
    sl = np.minimum(L.slope(X, Z), g.slope(X, Z))
    d = np.abs(dxc(X, Z))
    cl = cp1.value_noise(X, Z, 18.0, 13) * 0.6 + cp1.value_noise(X, Z, 5.0, 14) * 0.4
    keep = (sl > 0.6) & (d > 15) & (d < 150) & (cl > 0.45)
    X, Z = X[keep], Z[keep]
    P["crust"] = cp1.records(g, X, Z, np.random.default_rng(802).uniform(1.0, 2.6, len(X)), 0.95, 803)

    # embers: deep in the wall's gullies (the rarest accent)
    X, Z = cp1.candidates(g, 0.004, 851, R)
    sl = np.minimum(L.slope(X, Z), g.slope(X, Z))
    keep = (sl > 0.8) & (cp1.value_noise(X, Z, 6.0, 15) > 0.7)
    X, Z = X[keep], Z[keep]
    P["ember"] = cp1.records(g, X, Z, np.random.default_rng(852).uniform(0.6, 1.2, len(X)), 0.95, 853)

    # river plankton patches at the water surface
    r = np.random.default_rng(901)
    out = []
    for z in np.arange(R[2], R[3], 5.0):
        for _ in range(3):
            zz = z + r.uniform(-2, 2)
            x = land.river_x(zz) + r.uniform(-6, 6)
            wl = float(g.water_level(np.array([x]), np.array([zz]))[0])
            gh = float(g.height(np.array([x]), np.array([zz]))[0])
            if wl > gh + 0.1:
                a = r.uniform(0, 6.28)
                out.append([round(x, 3), round(wl, 3), round(zz, 3), 0, round(math.sin(a / 2), 5), 0,
                            round(math.cos(a / 2), 5), 1, 1, 1])
    P["plankton"] = out

    # comb jellies drifting over the river and up into the canopy
    r = np.random.default_rng(951)
    out = []
    for _ in range(900):
        z = r.uniform(R[2], R[3])
        x = land.river_x(z) + r.normal(0, 12)
        y = float(g.height(np.array([x]), np.array([z]))[0])
        y = max(y, land.FLOOR - 1.0) + r.uniform(2.0, 30.0)
        a = r.uniform(0, 6.28)
        q = cp1.quat_from_matrix(cp1.basis([math.sin(a) * 0.4, 1, math.cos(a) * 0.4], [1, 0, 0]))
        s = r.uniform(0.6, 1.6)
        out.append([round(x, 3), round(y, 3), round(z, 3), *[round(v, 5) for v in q], s, s, s])
    P["comb"] = out
    seat(g, P)
    return P


# ============================================================================================ grounding (2026-10)
# docs/prototypes/bioluminescent/grounding-pass-2026-10.md. How deep each kind's base is meant to go (x its scale): a
# trunk's holdfast is set into the rock, a cushion settles a little; the rest stand on the surface.
SINK = {"crinoid": 0.3, "seapen": 0.02, "mat": 0.03, "fan": 0.05, "whips": 0.0, "lanterns": 0.0, "crust": 0.0,
        "ember": 0.0}
CAMERA_MARGIN = 2.5      # metres between the eye's corridor and a trunk's surface
BODY_MARGIN = 1.0        # ... and the bulk of a fan, whip tuft or lantern cluster
GROUNDING = {}           # filled by seat(): the check's numbers, printed and kept for the report


def seat(g, P):
    """Seat every grounded kind on the drawn surface (grounding.fit), keep the bodies out of the camera's corridor,
    then check the result -- a failure stops the build, so a regression cannot ship a floating world."""
    for kind in grounding.CONTACT:
        P[kind], shrunk, dropped = grounding.fit(g, kind, P[kind], SINK[kind])
        GROUNDING[f"fit/{kind}"] = {"shrunk": shrunk, "dropped": dropped}
    for kind in grounding.BODY_SPHERES:
        before = len(P[kind])
        P[kind] = grounding.outside_corridor(FLIGHT, EYE_HULL, P, kind, BODY_MARGIN)
        GROUNDING[f"corridor/{kind}"] = {"removed": before - len(P[kind])}
    report = grounding.check(g, P, SINK)
    free = grounding.check_free(g, P)
    GROUNDING.update(report)
    GROUNDING.update(free)
    GROUNDING["river"] = grounding.check_river(g)
    GROUNDING["camera"] = grounding.camera_clearance(FLIGHT, EYE_HULL, P, g)
    grounding.assert_grounded(report, free)
    grounding.assert_river(GROUNDING["river"])
    cam = GROUNDING["camera"]
    if cam["crinoid"] < CAMERA_MARGIN - 0.05 or min(cam[k] for k in grounding.BODY_SPHERES) < BODY_MARGIN - 0.05:
        raise SystemExit(f"camera clearance check failed: {cam}")


def chunks(lst, n=60000):
    return [lst[i:i + n] for i in range(0, len(lst), n)] or [[]]


# ============================================================================================ nodes
def mesh_node(name, part, pts, mat, lod=(), max_distance=0.0, visible=True, program=None):
    proc = {
        "source": {"kind": "mesh", "asset": f"meshes/{part}.glb"},
        "distribution": {"kind": "points", "points": pts},
        "castsShadow": False,
        "lod": {"cull": True, "count": 1 + len(lod), "byScreenSize": False, "maxDistance": max_distance,
                **{f"distance{i + 1}": d for i, d in enumerate(lod)}},
        "material": dict(mat, program=program) if program else mat,
    }
    if not visible:
        proc["visible"] = False
    return {"name": name, "kind": "procedural", "procedural": proc}


def host_node(name, pts):
    """An invisible host: its instances carry an emitter template and nothing else."""
    return {"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [0.01, 0.01, 0.01]}, "distribution": {"kind": "points", "points": pts},
        "castsShadow": False, "visible": False, "lod": {"cull": True, "count": 1},
        "material": {"baseColor": [0, 0, 0]}}}


def m(base, emit=None, intensity=0.0, rough=0.55, double=False):
    d = {"baseColor": base, "roughness": rough, "metallic": 0.0}
    if emit is not None:
        d.update({"emissiveColor": emit, "emissiveIntensity": intensity})
    if double:
        d["doubleSided"] = True
    return d


def bodies(P):
    """The organisms' bodies, culled by distance; past it the emitters carry the far field.

    Live only (docs/prototypes/bioluminescent/live-30fps-report.md): an offline render lifts both the distance culls and
    the LOD rungs (ADR-186), so what follows shapes the live picture and nothing else.
    - The sea pens' bodies stop at 30 m. Past that they are dark, double-sided fronds a few pixels tall whose light is
      the seapenPolyps layer (which keeps its own 300 m), and drawing them from 30 to 80 m was 19 ms of a 51 ms frame
      at High (1080p, the drop). Matched stills at 40/96/150 s: identical.
    - The fans, whips and crinoids carry meshoptimizer LOD rungs (35% / 12% of the triangles, ADR-078). An offline
      render keeps LOD rungs (ADR-191), so the scene's thresholds are 0 -- a ladder that ends at LOD0, the mesh as it
      was -- and only the live project sets them (LIVE_LOD). v1's blobs were the old grid clustering; these were
      checked with matched stills (differences only at emitter occlusion edges)."""
    N = []
    N.append(mesh_node("crinoidStalk", "crinoid_stalk", P["crinoid"], m(BODY, VIOLET_DIM, 0.01, 0.6),
                       lod=(0.0, 0.0), max_distance=600.0))
    N.append(mesh_node("crinoidArms", "crinoid_arms", P["crinoid"],
                       m([0.05, 0.04, 0.08], VIOLET_DIM, 0.06, 0.5, double=True), lod=(0.0, 0.0),
                       max_distance=450.0))
    for i, part in enumerate(chunks(P["seapen"])):
        N.append(mesh_node(f"seapen{i}", "seapen", part, m([0.03, 0.05, 0.07], PEN, 0.04, 0.45, double=True),
                           max_distance=30.0))
    for i, part in enumerate(chunks(P["mat"])):
        N.append(mesh_node(f"mat{i}", "mat_cushion", part, m([0.006, 0.008, 0.012], None, 0.0, 0.95),
                           max_distance=70.0))
    N.append(mesh_node("fanBody", "fan_body", P["fan"], m([0.03, 0.025, 0.05], VIOLET_DIM, 0.01, 0.6),
                       lod=(0.0, 0.0), max_distance=140.0))
    N.append(mesh_node("whipsBody", "whips_body", P["whips"], m([0.03, 0.04, 0.05], CYAN, 0.05, 0.5),
                       lod=(0.0, 0.0), max_distance=150.0))
    N.append(mesh_node("lanternStalks", "lantern_stalks", P["lanterns"], m([0.05, 0.03, 0.02]), max_distance=120.0))
    N.append(mesh_node("lanternPods", "lantern_pods", P["lanterns"], m([0.3, 0.12, 0.03], AMBER, 14.0, 0.25),
                       max_distance=500.0))
    N.append(host_node("crustHost", P["crust"]))
    N.append(host_node("emberHost", P["ember"]))
    N.append(host_node("planktonHost", P["plankton"]))
    N.append(host_node("combHost", P["comb"]))
    return N


def layer(name, hosts, template, color, intensity, excited, field_="", excited_color=None, **kw):
    d = {"name": name, "hosts": hosts, "template": f"meshes/{template}.emit.json", "color": color,
         "excitedColor": excited_color or color, "intensity": intensity, "excitedIntensity": excited,
         "responseField": field_, "wakeField": "propW"}
    d.update(kw)
    return d


def ecosystem(P, sc_nodes):
    pens = [n["name"] for n in sc_nodes if n["name"].startswith("seapen")]
    mats = [n["name"] for n in sc_nodes if n["name"].startswith("mat")]
    eco = {"spriteRadius": 1.5, "maxSprites": 65536, "layers": [
        # the canopy answers late: energy, not excitation; light runs out along the arms behind it
        layer("crinoidBeads", ["crinoidStalk"], "crinoid_beads", CYAN, 5.0, 10.0, "propE", sparsity=0.3,
              flicker=0.1, flickerRate=1.5, breath=0.5, breathRate=0.04, wakeGain=1.5, maxDistance=1000,
              responseGain=1.6, nearFade=5.0),
        layer("crinoidChains", ["crinoidStalk"], "crinoid_chains", PALE, 4.0, 6.0, "highs", breath=0.4,
              breathRate=0.09, wakeGain=1.5, maxDistance=1000, responseThreshold=0.15, responseGain=1.4,
              nearFade=7.0),
        layer("matPolyps", mats, "mat_polyps", BLUE, 3.5, 10.0, "propU", excited_color=PALE, sparsity=0.45, pulseRate=1.5,
              pulseDecay=0.8, wakeGain=2.0, maxDistance=320),
        layer("fanPolyps", ["fanBody"], "fan_polyps", VIOLET, 3.0, 8.0, "propU", excited_color=MAGENTA,
              sparsity=0.3, size=1.5, wakeGain=1.0, maxDistance=700, nearFade=2.0),
        layer("whipsTips", ["whipsBody"], "whips_tips", FIREFLY, 3.0, 5.0, "highs", sparsity=0.3, flicker=0.6,
              flickerRate=7.0, wakeGain=1.0, maxDistance=260, responseThreshold=0.15, responseGain=1.4,
              nearFade=1.5),
        layer("seapenPolyps", pens, "seapen", PEN, 2.5, 9.0, "propU", sparsity=0.3, pulseRate=0.8, pulseDecay=1.0,
              wakeGain=2.0, maxDistance=300),
        layer("crust", ["crustHost"], "crust", DEEP_BLUE, 0.6, 9.0, "propU", excited_color=CYAN, sparsity=0.65, pulseRate=2.0,
              pulseDecay=1.5, breath=0.4, breathRate=0.03, size=0.8, wakeGain=1.5, maxDistance=700),
        layer("crustViolet", ["crustHost"], "crust_b", VIOLET, 0.35, 10.0, "propU", excited_color=MAGENTA,
              sparsity=0.75, pulseRate=1.0, pulseDecay=1.5, breath=0.5, breathRate=0.02, size=0.8, wakeGain=1.5,
              maxDistance=700),
        layer("embers", ["emberHost"], "crust", EMBER, 1.4, 0.0, "", sparsity=0.75, breath=0.6, breathRate=0.02,
              size=0.7, maxDistance=400, wakeField=""),
        layer("combRows", ["combHost"], "comb_rows", PALE, 1.2, 3.0, "highs", iridescence=0.55, size=0.8, iridescenceScale=1.5,
              iridescenceSpeed=0.8, bob=1.2, bobRate=0.05, flicker=0.2, flickerRate=4.0, nearFade=4.0,
              maxDistance=260, responseThreshold=0.2),
        layer("combGlow", ["combHost"], "comb_glow", [0.35, 0.3, 1.0], 0.6, 0.0, "", bob=1.2, bobRate=0.05,
              sparsity=0.2, nearFade=4.0, maxDistance=200),
        layer("plankton", ["planktonHost"], "plankton", BLUE, 0.8, 7.0, "propU", sparsity=0.7, pulseRate=4.0,
              pulseDecay=0.6, flicker=0.3, flickerRate=3.0, wakeGain=2.0, maxDistance=320, nearFade=3.0),
    ]}
    for l in eco["layers"]:
        LAYER_BASE[l["name"]] = (l["intensity"], l["excitedIntensity"])
    return eco


# ============================================================================================ fields & the medium
PROP = {
    "name": "prop", "mode": "excitable", "wrap": "clamp",
    "resolution": [256, 1, 1024], "boundsMin": [-200.0, -400.0, Z0], "boundsMax": [200.0, 400.0, Z1],
    "injectField": "ignite", "injectRate": 1.0, "conductivityField": "conduct",
    "threshold": 0.5, "coupling": 1.0, "waveSpeed": 14.0, "riseRate": 10.0, "excitationDecay": 0.4,
    "refractoryTime": 2.5, "refractoryStrength": 8.0, "energyTime": 3.0, "wakeTime": 30.0, "noise": 0.35,
    "ceiling": 0.0, "simRate": 30, "maxSubSteps": 4, "seed": 11, "checkpointInterval": 5,
}


def fields():
    return [
        # the disturbance: every kick launches a front across the canyon from just behind the camera, racing down
        # it (planar, +z). Its origin rides with the camera (routed). The seed mask breaks it into arcs; the medium
        # carries, branches and stops them. The music radiates out from the listener into the world.
        field("kickFront", position=[0.0, 0.0, FLIGHT_Z[0] - 8.0], kind="onset", onsetSource="low",
              onsetDecay=0.45, onsetWidth=5.0, audioSpeed=38.0, waveGeometry="planar", axis=[0, 0, 1],
              strength=1.0),
        field("seeds", kind="noise", frequency=0.035, seed=31, strength=1.0),
        field("ignite", kind="compound", children=["kickFront", "seeds"], combine="multiply", strength=1.0),
        # the medium: patchy, so fronts branch round barren rock
        field("conduct", kind="noise", frequency=0.045, seed=47, strength=1.6),
        # the medium as everything reads it
        field("propU", kind="grid", reference="prop", channel=0, strength=1.0),
        field("propE", kind="grid", reference="prop", channel=2, strength=1.0),
        field("propW", kind="grid", reference="prop", channel=3, strength=1.0),
        # the haze's colour where the canyon is lit (scalar as colour: mix(A, B, s))
        field("propGlow", kind="grid", reference="prop", channel=2, strength=1.0, colorA=[0, 0, 0, 1],
              colorB=GLOW + [1]),
        # the highs, each element its own band in the top of the spectrum
        field("highs", kind="spectrum", audioBand="element", bandLow=0.62, bandHigh=0.98, audioDelay=0.0,
              audioSpeed=0.0, strength=1.0),
    ]


def rock_program():
    """The canyon's rock: dark, mottled, wet. Where a front passes it catches the organisms' light, in patches:
    emission = WALL_GLOW x u x patch x mottle -- 0 until the arc raises `material/rock/emissionIntensity`."""
    return {"name": "rock", "ops": [
        op("input", 0, input="worldPosition"),
        op("noise", 1, srcA=0, value=0.08, seed=5),
        op("remap", 1, srcA=1, value=1, constant=[0.2, 0.8, 0.55, 1.25]),
        op("constant", 6, constant=[0.022, 0.026, 0.036, 1.0]),
        op("multiply", 6, srcA=6, srcB=1),
        op("field", 2, field="propU"),
        op("noise", 4, srcA=0, value=0.35, seed=9),                         # patches: light pools, not a wash
        op("smoothstep", 4, srcA=4, constant=[0.45, 0.7, 0, 0]),
        op("multiply", 2, srcA=2, srcB=4),
        op("constant", 7, constant=WALL_GLOW + [1.0]),
        op("multiply", 7, srcA=7, srcB=2),
        op("multiply", 7, srcA=7, srcB=1),
    ], "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 0.0, "opacity": -1}


def flight_points():
    zs = np.arange(FLIGHT_Z[0], FLIGHT_Z[1] + 1, 25.0)
    return [[round(land.river_x(z) * 0.85 + land.centre_x(z) * 0.15, 2), round(land.FLOOR - 1.5 - z * 0.004, 2),
             round(float(z), 2)] for z in zs]


def flight_length(pts):
    a = np.array(pts)
    return float(np.sum(np.linalg.norm(np.diff(a, axis=0), axis=1)))


CROWN = [0.10, 0.55, 1.0]


def crown_lights(P):
    """The hero scale lights the haze: the larger bank crinoids each pool their photophores' light in the air under
    their crowns (the owner liked CP1's radiant canopy). Micro emitters never do (01-research.md); only the giants."""
    out = []
    for k, c in enumerate(P["crinoid"]):
        s = c[7]
        if s < 1.15 or len(out) >= 72:
            continue
        x, y, z = c[:3]
        CROWN_IDS.append(f"crown{k}")
        out.append({"name": f"crown{k}", "id": f"crown{k}", "type": "point", "position": [x, y + 21.0 * s, z],
                    "color": CROWN, "intensity": 220.0, "range": 26.0 * s, "radius": 2.0, "castsShadow": False,
                    "volumetric": 0.4})
    return out


def scene(P):
    nodes = list(fields())
    nodes.append({"name": "land", "kind": "terrain", "position": [0, 0, 0], "world": land.world(),
                  # one level: the drawn ground is the 2 m mesh the organisms are seated on, everywhere
                  # (grounding-pass-2026-10.md: the 4/8/16 m rungs moved it up to 6-10 m off them)
                  "terrain": {"chunkSize": 80.0, "resolution": 40, "lodLevels": 1, "lodDistance": 70.0,
                              "viewDistance": 1000.0, "shadowDistance": 150.0, "skirtDepth": 2.0,
                              "groundMottle": False,
                              "water": {"shallowColor": [0.004, 0.008, 0.014], "deepColor": [0.001, 0.003, 0.008],
                                        "reflection": 0.7, "reflectionTint": [0.3, 0.45, 0.8], "specular": 0.6,
                                        "fresnel": 0.3, "foam": 0.0, "ripple": 0.45}},
                  "material": {"program": "rock", "baseColor": [0.022, 0.026, 0.036], "roughness": 0.8,
                               "metallic": 0.0}})
    grounding.assert_terrain_full_resolution(nodes[-1]["terrain"])
    grounding.assert_world_covers(nodes[-1]["world"], nodes[-1]["terrain"], REGION, flight_points())
    body = bodies(P)
    nodes += body
    pts = flight_points()
    nodes.append({"name": "flight", "kind": "spline", "spline": {
        "name": "flight", "kind": "catmullRom", "closed": False, "tension": 0.5, "generator": "points",
        "samplesPerSegment": 16, "up": [0, 1, 0],
        "points": [{"position": p, "roll": 0.0, "scale": 1.0} for p in pts]}})
    nodes.append({"name": "spores", "kind": "particles", "particles": {
        "capacity": 160000, "seed": 4, "shape": "spline", "spline": "flight", "extent": [40, 6, 40],
        "direction": [0, 1, 0], "spread": 0.6, "spawnRate": 800, "speedMin": 0.1, "speedMax": 0.7,
        "gravity": [0, 0.12, 0], "drag": 0.3, "turbulence": 0.7, "turbulenceScale": 0.07, "turbulenceSpeed": 0.2,
        "sizeStart": 0.045, "sizeEnd": 0.015, "sizeVariance": 0.7, "lifetimeMin": 10.0, "lifetimeMax": 20.0,
        "colorStart": PALE + [1.0], "colorEnd": [0.3, 0.4, 1.0, 0.0], "emissive": 2.0, "blend": "additive",
        "softness": 0.6}})
    nodes.append({"name": "swarm", "kind": "particles", "particles": {
        "capacity": 120000, "seed": 8, "shape": "spline", "spline": "flight", "extent": [22, 14, 22],
        "direction": [0, 0.2, 1], "spread": 0.35, "spawnRate": 0, "speedMin": 6.0, "speedMax": 14.0,
        "gravity": [0, 0.0, 0], "drag": 0.05, "turbulence": 2.2, "turbulenceScale": 0.05, "turbulenceSpeed": 0.6,
        "sizeStart": 0.06, "sizeEnd": 0.03, "sizeVariance": 0.5, "lifetimeMin": 4.0, "lifetimeMax": 8.0,
        "colorStart": [0.6, 0.95, 1.0, 1.0], "colorEnd": [0.1, 0.4, 1.0, 0.0], "emissive": 8.0, "blend": "additive",
        "softness": 0.4}})
    sc = {
        "format": "avgen-scene", "version": 1, "name": "THE RIFT",
        "_note": "Generated by examples/bioluminescent/build.py; edit that, not this file.",
        "camera": {"mode": 2, "spline": "flight", "fov": 52},
        "environment": {
            "background": [0.003, 0.004, 0.010], "intensity": 0.06,
            "fogColor": [0.004, 0.007, 0.018], "volumeDensity": 0.006, "volumeScattering": 0.9,
            "volumeAbsorption": 0.3, "volumeAnisotropy": 0.35, "volumeSteps": 32, "volumeMaxDistance": 900.0,
            "volumeLocalLights": 1.0, "volumeNoise": 0.0, "volumeNoiseScale": 0.03,
            "volumeEmission": 0.0, "volumeColorField": "propGlow",
            "fogHeight": land.FLOOR + 30.0, "fogHeightFalloff": 0.02, "fogUpperDensity": 0.12,
            "fogSky": 1.0, "fogSkyDistance": 1400.0,
            "sky": {"enabled": True, "background": True, "useKeyLight": False, "sunDirection": [0.2, 0.3, 0.9],
                    "zenithColor": [0.0015, 0.003, 0.009], "horizonColor": [0.012, 0.016, 0.04],
                    "groundColor": [0.002, 0.002, 0.004], "sunColor": [0.6, 0.7, 1.0], "haze": 0.1,
                    "sunIntensity": 0.0, "sunSize": 0.01, "sunGlow": 0.0, "intensity": 1.0},
        },
        "lights": [{"name": "moon", "id": "moon", "type": "directional", "role": "key",
                    "direction": [0.25, -0.92, 0.3], "color": [0.55, 0.65, 1.0], "intensity": 0.05,
                    "castsShadow": False}] + crown_lights(P),
        "grids": [PROP],
        "materialPrograms": [rock_program()],
        "ecosystem": ecosystem(P, body),
        "nodes": nodes,
    }
    return sc, flight_length(pts)


# ============================================================================================ the arc
# Camera behaviours: (up, lateral, lookAhead, fov, bank). `up` is metres above the river, `lateral` across it.
CAM = {
    "river":   (2.6, 0.0, 30.0, 42.0, 6.0),     # low over the water, under the canopy
    "drift":   (4.0, -3.0, 40.0, 36.0, 3.0),    # slow, long lens
    "canopy":  (27.0, -6.0, 45.0, 56.0, 10.0),  # through the crinoid crowns
    "wall":    (11.0, 10.0, 28.0, 46.0, -6.0),  # along the wall of fans and crust, banking the other way
    "climb":   (34.0, 0.0, 60.0, 48.0, 4.0),    # the build: rising into the crowns, anticipation
    "dive":    (5.0, 0.0, 40.0, 66.0, 16.0),    # down to the water, fast
    "surge":   (21.0, 0.0, 70.0, 72.0, 12.0),   # the drop: level through the crowns, fast and wide
    "reveal":  (70.0, -10.0, 120.0, 58.0, 4.0), # high: the canyon's length
    "after":   (9.0, 4.0, 50.0, 40.0, 2.0),     # the aftermath: slow, pulling up
}

# The eye's corridor (grounding.py): every offset the stages can blend between, grown by the LFO drift (routes():
# driftA +-2 m lateral, driftC +-1 m up), swept along the flight.
EYE_HULL = grounding.eye_hull(CAM, drift=(2.0, 1.0))
FLIGHT = None  # set in main(), after flight_points() is defined

# Per state: camera behaviour, pace (m/s), ignition gain, wave speed (m/s), rock glow, haze glow,
# spores (per s), swarm (per s), transition (s, easing), medium threshold, kick-front decay (1/s), kick-front speed.
# The paces must fit the flight into the path: on Trench the body at 17-20 m/s ran off the end of the open spline at
# 197 s, and the parked camera lost its aim (test_rift.cpp, "the flight does not run out of path").
# The medium's CHARACTER is per stage: before the drop a high threshold (fronts survive only in rich patches) and a
# fast-dying ignition (a kick lights local pockets that ripple and stop); at the drop a low threshold (the whole canyon
# conducts) and a slow, fast front that runs kilometres. Without that contrast the drop has nothing to break (v2).
STAGES = {
    "Dark":      ("drift",  3.0, 0.0, 10.0, 0.0, 0.0, 250.0, 0.0, (4.0, "smooth"), 0.95, 4.0, 30.0),
    "Stirring":  ("river",  5.0, 1.0, 10.0, 0.0, 0.0, 400.0, 0.0, (6.0, "smooth"), 0.9, 4.0, 30.0),
    "Breath":    ("river",  1.5, 0.0, 10.0, 0.0, 0.0, 300.0, 0.0, (1.0, "smooth"), 0.95, 4.0, 30.0),
    "Awake":     ("canopy", 8.0, 1.3, 14.0, 0.12, 0.0, 600.0, 0.0, (6.0, "smooth"), 0.75, 3.0, 34.0),
    "Awake 2":   ("wall",   8.0, 1.4, 14.0, 0.15, 0.0, 600.0, 0.0, (10.0, "smooth"), 0.75, 3.0, 34.0),
    "Build":     ("climb",  4.0, 0.5, 16.0, 0.0, 0.0, 3500.0, 0.0, (4.0, "easeInOut"), 0.95, 4.0, 30.0),
    "Drop":      ("surge", 26.0, 3.0, 28.0, 0.35, 0.025, 2500.0, 9000.0, (1.2, "easeOut"), 0.35, 0.25, 28.0),
    "Body":      ("canopy", 14.0, 2.0, 22.0, 0.25, 0.06, 1500.0, 3000.0, (6.0, "smooth"), 0.62, 1.5, 40.0),
    "Body 2":    ("dive",   17.0, 2.2, 24.0, 0.3, 0.06, 1500.0, 4000.0, (6.0, "smooth"), 0.6, 1.5, 40.0),
    "Body 3":    ("wall",   12.0, 2.0, 22.0, 0.25, 0.05, 1500.0, 3000.0, (6.0, "smooth"), 0.62, 1.5, 40.0),
    "Body 4":    ("reveal",  9.5, 2.4, 24.0, 0.3, 0.07, 1500.0, 2000.0, (8.0, "smooth"), 0.55, 1.2, 45.0),
    "Aftermath": ("after",  2.5, 0.0, 12.0, 0.0, 0.0, 600.0, 0.0, (4.0, "smooth"), 0.95, 4.0, 30.0),
}
# How brightly the whole ecosystem burns, per stage: (rest x, excited x, crown light cd). The drop surges every layer at
# once; the body settles above the rest state; the aftermath drains below it.
GLOWS = {"Dark": (0.75, 1.0, 180.0), "Stirring": (1.0, 1.0, 220.0), "Breath": (0.6, 1.0, 120.0),
         "Awake": (1.15, 1.1, 240.0), "Awake 2": (1.15, 1.1, 240.0), "Build": (0.35, 0.6, 60.0),
         "Drop": (2.4, 2.1, 560.0), "Body": (1.45, 1.5, 340.0), "Body 2": (1.5, 1.5, 340.0),
         "Body 3": (1.45, 1.5, 340.0), "Body 4": (1.5, 1.6, 360.0), "Aftermath": (0.6, 0.8, 150.0)}
LAYER_BASE = {}   # filled by ecosystem(): name -> (intensity, excitedIntensity)
CROWN_IDS = []    # filled by crown_lights()

BODY_CYCLE = ["Body", "Body 2", "Body 3", "Body 4"]
PADS = {"Dark": 36, "Stirring": 37, "Awake": 38, "Build": 39, "Drop": 40, "Body": 41, "Aftermath": 42}


def preset(name, length):
    cam, pace, ignite, speed, rock, haze, spores, swarm, _t, threshold, decay, front = STAGES[name]
    up, lat, look, fov, bank = CAM[cam]
    return {"name": name.lower(), "values": {
        "macros/flight": [pace / 20.0],
        "camera/splineOffset": [lat, up, 0.0], "camera/lookAhead": [look], "camera/fov": [fov],
        "camera/splineBank": [bank],
        "grid/prop/injectRate": [ignite],
        "grid/prop/waveSpeed": [speed],
        "grid/prop/threshold": [threshold],
        "field/kickFront/onsetDecay": [decay],
        "field/kickFront/audioSpeed": [front],
        "material/rock/emissionIntensity": [rock],
        "scene/volumeEmission": [haze],
        "particles/spores/spawnRate": [spores],
        "particles/swarm/spawnRate": [swarm],
        **glow_values(name),
        # the body is the brightest stretch of the piece: expose for it (the Critic: clipped, washed out at the drop)
        "camera/exposure/compensation": [-0.4 if name in ("Drop", "Body", "Body 2", "Body 3", "Body 4") else 0.0],
    }}


def glow_values(name):
    rest, excited, crown = GLOWS[name]
    v = {}
    for layer, (i, e) in LAYER_BASE.items():
        v[f"ecosystem/{layer}/intensity"] = [round(i * rest, 4)]
        v[f"ecosystem/{layer}/excitedIntensity"] = [round(e * excited, 4)]
    for lid in CROWN_IDS:
        v[f"lights/{lid}/intensity"] = [crown]
    dense = name in ("Drop", "Body", "Body 2", "Body 3", "Body 4")
    # The drop is also a synchronised ignition ("thousands of organisms illuminate"): every organism that answers the
    # medium answers as if a front were on it, landing with the drop's ease-out and easing back over the body's
    # transition while the real fronts keep travelling. Negative thresholds = a standing response.
    flash = {"Drop": -0.25, "Body": -0.06, "Body 2": -0.06, "Body 3": -0.06, "Body 4": -0.08}.get(name, 0.0)
    for layer in ("matPolyps", "seapenPolyps", "crust", "crustViolet", "fanPolyps", "plankton"):
        v[f"ecosystem/{layer}/responseThreshold"] = [flash]
    v["ecosystem/crust/sparsity"] = [0.4 if name == "Drop" else (0.5 if dense else 0.65)]
    v["ecosystem/crustViolet/sparsity"] = [0.45 if name == "Drop" else (0.6 if dense else 0.75)]
    return v


def states():
    def held(signal, threshold, frm, falling=False):
        t = {"kind": "signal", "signal": signal, "threshold": threshold, "from": frm, "hold": True}
        if falling:
            t["falling"] = True
        return t

    def after(seconds, frm):
        return {"kind": "elapsed", "threshold": seconds, "from": frm}

    def pad(name):
        return [{"kind": "signal", "signal": f"control.pad{name}", "threshold": 0.3}] if name in PADS else []

    lull_from = ["Stirring", "Awake", "Awake 2"]
    S = []
    for name, v in STAGES.items():
        secs, easing = v[8]
        trig = []
        if name == "Stirring":
            trig.append(after(6.0, "Dark"))
        elif name == "Breath":
            trig += [held("macro.bassSlow", 0.2, f, falling=True) for f in lull_from]
        elif name == "Awake":
            # idle: a returning bass must not pre-empt a Build that is already on its way (ADR-1164)
            trig.append(dict(held("macro.bassSlow", 0.35, "Breath"), idle=True))
            trig.append({"kind": "phrase", "every": 2, "from": "Stirring", "idle": True})
            trig.append({"kind": "phrase", "every": 1, "from": "Awake 2", "idle": True})
        elif name == "Awake 2":
            trig.append({"kind": "phrase", "every": 1, "from": "Awake", "idle": True})
        elif name == "Build":
            trig.append(after(2.0, "Breath"))
        elif name == "Drop":
            trig.append(held("macro.bassSlow", 0.42, "Build"))
        elif name in BODY_CYCLE:
            k = BODY_CYCLE.index(name)
            if k == 0:
                trig.append(after(14.0, "Drop"))
            trig.append({"kind": "phrase", "every": 1, "from": BODY_CYCLE[k - 1], "idle": True})
        elif name == "Aftermath":
            trig += [held("macro.energy", 0.62, f, falling=True) for f in BODY_CYCLE]
        trig += pad(name)
        transition = {"seconds": secs, "easing": easing}
        if name in BODY_CYCLE or name in ("Stirring", "Awake", "Awake 2", "Aftermath"):
            transition["quantize"] = "beat"   # a camera move starts on the beat (the Critic: cuts off the beat)
        S.append({"name": name, "preset": name.lower(), "transition": transition, "triggers": trig})
    return {"initial": "Dark", "states": S}


def macros():
    names = ("energy", "bassSlow", "flight", "sensitivity")
    return [{"name": n, "label": n.upper(), "default": 0.25 if n == "sensitivity" else 0.0, "targets": []}
            for n in names]


# ============================================================================================ the response (ADR-1361)
# LIVE ONLY. The performer's Response controls (the Live panel: Sensitivity, Transients, Sustain, Attack, Release =
# `sonic/response/*`, ADR-1062) reach THE RIFT LIVE twice:
#   the routes  the snare is the response model's (Transients decides how many fire and how hard); an auto-gained
#               band or the analyser's energy keeps its shape (what the music is doing) and is multiplied by the
#               response level it is heard at (against the music's own loudness, ADR-1510), so a breakdown wakes the
#               canyon less and Sensitivity lifts it. The arc's two listeners (macro.energy, macro.bassSlow), and so the stage
#               machine, hear the same.
#   the fields  the kick front (an onset field) and the highs (a spectrum field) read the audio history, which the
#               engine hears through the same controls (AudioHistory::setResponse): a kick under the hit floor
#               launches no front, the highs are curved by the level sensitivity, Release lengthens the front's
#               decay.
# Each gain is 1 / the response level's mean on Trench at the neutral controls, so the neutral picture is the old one
# (docs/development/live-response-everywhere.md). ADR-1510: the levels now read against the music's own loudness
# (level 0.60, bass 0.63 mean on Trench, from 0.83 and 0.77 on the old -44..-2 dB window), so the gains are the old
# ones times those ratios (1.38, 1.21). The offline project keeps its audio.* routes.
RESPONSE = {}  # ADR-1510: the levels read against the adaptive loudness reference (was a -44/42 dB window)
LIVE_SOURCES = {"audio.energy": "visual.hEnergy", "audio.bass": "visual.hBass", "audio.trebleLevel": "visual.hTreble",
                "audio.mid": "visual.hMid"}
# ADR-1620: the mix has no snare (ADR-898's band onsets left the bus, the response model's snare comes only from a
# drum source). Both projects read tools/drum_signals.py's `signal.snare` -- a drums source's snare, else the beat
# clock's backbeat -- at the old live amount (the onset's x 0.78: a response snare's mean strength was 0.95 to the
# onset's 0.745, and the drum signal keeps the response scale).


def heard(name, band, level, gain):
    """An auto-gained band (its shape) times the response level it is heard at."""
    return {"name": name, "combine": "product", "inputs": [{"signal": band, "weight": 1.0},
                                                          {"signal": level, "weight": 1.0}], "gain": gain}


LIVE_HEARD = {"kind": "interpret", "name": "heard", "settings": {"mappings": [
    heard("hEnergy", "audio.energy", "response.level", 1.65),
    heard("hBass", "audio.bass", "response.bass", 1.48),
    heard("hTreble", "audio.trebleLevel", "response.level", 1.64),
    heard("hMid", "audio.mid", "response.level", 1.64),
]}}


def respond(route):
    src = route["source"]
    if route.get("amount", 0.0) == 0.0 or src not in LIVE_SOURCES:
        return route  # the zero defaults, and everything that does not listen
    route["source"] = LIVE_SOURCES[src]
    return route


def routes(length, live=False):
    r = list(ZERO_DEFAULTS)
    sens = {"depthSource": "macro.sensitivity", "depthMin": 0.0, "depthMax": 4.0}
    # the arc's two slow listeners
    r.append(dict({"source": "audio.energy", "target": "macros/energy", "op": "add", "amount": 1.0,
                   "chain": {"attackMs": 1500, "decayMs": 3000}}, **sens))
    # the bass band's presence (audio.bass, not the loudness-adaptive bassLevel, which hides a breakdown)
    r.append(dict({"source": "audio.bass", "target": "macros/bassSlow", "op": "add", "amount": 1.0,
                   "chain": {"attackMs": 150, "decayMs": 500}}, **sens))
    # the flight, and the kick front's origin with it (z only: the path's length is a little more than its z span)
    integ = {"integrate": True}
    zscale = (FLIGHT_Z[1] - FLIGHT_Z[0]) / length
    for target, scale, comp in (("camera/splineT", 1.0 / length, None), ("field/kickFront/position", zscale, 2)):
        for src, amt in (("macro.flight", 20.0), ("macro.energy", 3.0)):
            route = {"source": src, "target": target, "op": "add", "amount": amt * scale, "chain": dict(integ)}
            if comp is not None:
                route["component"] = comp
            r.append(route)
    # bass is large-scale energy: it sharpens the ignition (how much of the disturbed ground fires)
    # (audio.bass, not bassLevel: the adaptive level sits near 0.8 and would add a constant to the stage's gain)
    r.append({"source": "audio.bass", "target": "grid/prop/injectRate", "op": "add", "amount": 0.6,
              "chain": {"attackMs": 60, "decayMs": 400}})
    # highs are small life: spores rise with the treble
    r.append({"source": "audio.trebleLevel", "target": "particles/spores/spawnRate", "op": "add", "amount": 1600.0,
              "chain": {"attackMs": 80, "decayMs": 900}})
    # the snare throws a burst of the swarm (only where the stage lets the swarm fly at all: it multiplies)
    r.append({"source": "signal.snare", "target": "particles/swarm/spawnRate", "op": "add", "amount": 4680.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 40, "envelopeFallPerSecond": 4.0},
              "depthSource": "macro.energy", "depthMin": 0.0, "depthMax": 1.0})
    # mids breathe the siphonophore chains
    r.append({"source": "audio.mid", "target": "ecosystem/crinoidChains/intensity", "op": "add", "amount": 1.2,
              "chain": {"attackMs": 200, "decayMs": 1200}})
    # the camera floats a little off its path, more as the music grows
    for src, comp, amt in (("driftA", 0, 2.0), ("driftC", 1, 1.0)):
        r.append({"source": f"lfo.{src}.bipolar", "target": "camera/splineOffset", "op": "add", "amount": amt,
                  "component": comp, "depthSource": "macro.energy", "depthMin": 0.4, "depthMax": 1.0})
    return [respond(x) for x in r] if live else r


POST = {
    "post/bloom/enabled": True, "post/bloom/threshold": 0.9, "post/bloom/intensity": 0.32,
    "post/bloom/emissionWeight": 0.75, "post/tonemap/chroma-retention": 0.8, "post/output/vignette": 0.28,
    "post/grade/contrast": 1.08, "post/grade/saturation": 1.05,
    "camera/exposure/mode": 0, "camera/mode": 2,
    "sources/driftA/rate": 0.031, "sources/driftC/rate": 0.047,
}


# The scene asks for clear (noise-free) air, which is what live runs; the offline project asks for the noise. A project
# may raise its scene's noise but never zero it (test_volumetric_noise_authoring.cpp: a zero override is how a saved
# session's accident looks), so the zero lives in the scene.
LIVE_PARAMETERS = {"scene/volumeSteps": 16}
# Live only: where the bodies step down their LOD rungs (metres; the scene's own thresholds are 0, so offline draws LOD0).
# docs/prototypes/bioluminescent/live-30fps-report.md, ADR-1344.
LIVE_LOD = {"fanBody": (35.0, 70.0), "whipsBody": (35.0, 70.0), "crinoidArms": (120.0, 240.0),
            "crinoidStalk": (150.0, 300.0)}
for _body, (_d1, _d2) in LIVE_LOD.items():
    LIVE_PARAMETERS[f"procedural/{_body}/lod/distance1"] = _d1
    LIVE_PARAMETERS[f"procedural/{_body}/lod/distance2"] = _d2
OFFLINE_PARAMETERS = {"scene/volumeNoise": 0.6}


def project(name, audio, length, live=False):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "rift.scene.json"}},
        "live": {"qualityStrategy": "effects_first"},
        "parameters": dict(POST),
        "sources": [{"kind": "lfo", "name": n, "settings": {"shape": "sine"}} for n in ("driftA", "driftC")],
        "sonic": {},
        "routes": routes(length, live),
        "worldMacros": macros(),
        "presets": [preset(n, length) for n in STAGES],
        "states": states(),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/rift.mp4"},
    }
    p["sources"] = drum_signals.sources(drum_signals.used(p["routes"])) + p["sources"]  # ADR-1620
    if audio:
        p["assets"]["audio"] = {"path": audio}
    if not live:
        p["parameters"].update(OFFLINE_PARAMETERS)
    if live:
        # Live: the same world, cheaper medium. The march costs ~1 ms per step at 1080p (03-architecture.md §6);
        # 16 steps halve it. LIVE AUTO's render scale takes the pixel-bound rest. Offline keeps 32.
        p["parameters"].update(LIVE_PARAMETERS)
        p["sonic"] = {"live": True, "response": dict(RESPONSE)}
        p["sources"].append(LIVE_HEARD)
        bindings = [
            {"source": "*", "channel": -1, "kind": "cc", "number": 1, "parameter": "macros/sensitivity",
             "component": 0, "min": 0.0, "max": 1.0},
            {"source": "*", "channel": -1, "kind": "cc", "number": 2, "parameter": "grid/prop/waveSpeed",
             "component": 0, "min": 4.0, "max": 40.0},
            {"source": "*", "channel": -1, "kind": "cc", "number": 3, "parameter": "grid/prop/threshold",
             "component": 0, "min": 0.15, "max": 1.2},
            {"source": "*", "channel": -1, "kind": "cc", "number": 4, "parameter": "material/rock/emissionIntensity",
             "component": 0, "min": 0.0, "max": 6.0},
        ]
        bindings += [{"source": "*", "channel": -1, "kind": "noteEvent", "number": n, "signal": f"pad{st}"}
                     for st, n in PADS.items()]
        p["control"] = {"midi": {"enabled": True, "filter": "*", "bindings": bindings}}
    return p


def probe_project(p):
    """A trace-only copy (`--sonic-trace`): an interpret source echoes the arc's macros and the state index (/ 20)
    as visual.* columns. Written to the build dir, never shipped."""
    q = json.loads(json.dumps(p))
    maps = [("pEnergy", "macro.energy", 1.0), ("pBass", "macro.bassSlow", 1.0), ("pState", "state.index", 0.05),
            ("pBassRaw", "audio.bassLevel", 1.0), ("pTreble", "audio.trebleLevel", 1.0)]
    q["sources"].append({"kind": "interpret", "name": "probe", "settings": {"mappings": [
        {"name": n, "combine": "mean", "inputs": [{"signal": sig, "weight": 1.0}], "bias": 0.0, "gain": g,
         "curve": 1.0} for n, sig, g in maps]}})
    q["assets"]["scene"]["path"] = str(HERE / "rift.scene.json")
    q["assets"]["audio"]["path"] = str((HERE / p["assets"]["audio"]["path"]).resolve())
    return q


def generator_stamp():
    """What rift.scene.json is made from: this folder's Python and its committed meshes. Written beside the world as
    rift.scene.json.stamp, so a world made by an older generator is known to be stale -- a test that only checked
    the file existed passed or failed on whatever an earlier build.py had left (2026-10-15, after ADR-1344)."""
    import hashlib
    h = hashlib.sha256()
    for f in sorted(list(HERE.glob("*.py")) + list((HERE / "meshes").glob("*"))):
        if f.is_file():
            h.update(f.relative_to(HERE).as_posix().encode())
            h.update(f.read_bytes())
    return h.hexdigest()


def main():
    # --scene-only: write just the generated world (rift.scene.json, gitignored) from the committed meshes and leave
    # every committed file alone. tests/integration/test_rift.cpp runs it on a clean checkout.
    # --if-stale (with --scene-only): do nothing when the world exists and was made by these exact sources.
    scene_only = "--scene-only" in sys.argv[1:]
    stamp_path = HERE / "rift.scene.json.stamp"
    stamp = generator_stamp()
    if scene_only and "--if-stale" in sys.argv[1:]:
        if (HERE / "rift.scene.json").exists() and stamp_path.exists() and stamp_path.read_text().strip() == stamp:
            print("rift.scene.json is up to date")
            return
    if not scene_only:
        organisms.build_all()
    print("ground...")
    global FLIGHT
    FLIGHT = grounding.Flight(flight_points())
    g = land.Ground(land.world(), *REGION, step=2.0)
    P = place(g, land.Ground(land.world(floodplain=False), *REGION, step=2.0))
    for k, v in P.items():
        print(f"  {k}: {len(v)}")
    print("grounding (metres, along the surface normal; + = off the ground):")
    for k, v in GROUNDING.items():
        print(f"  {k}: " + ", ".join(f"{a} {b:.3f}" if isinstance(b, float) else f"{a} {b}" for a, b in v.items()))
    sc, length = scene(P)
    # written beside and renamed into place, so a reader never sees a half-written world
    tmp = HERE / f"rift.scene.json.{os.getpid()}.tmp"
    tmp.write_text(json.dumps(sc, separators=(",", ":")))
    os.replace(tmp, HERE / "rift.scene.json")
    stamp_path.write_text(stamp + "\n")
    if scene_only:
        print(f"wrote rift.scene.json ({(HERE / 'rift.scene.json').stat().st_size / 1e6:.1f} MB)")
        return
    projects = {
        "rift.json": project("THE RIFT / Trench", "../../assets/audio/trench.wav", length),
        "rift-live.json": project("THE RIFT LIVE", None, length, live=True),
    }
    for n, p in projects.items():
        (HERE / n).write_text(json.dumps(p, indent=1) + "\n")
    trace_dir = HERE.parents[1] / "build" / "biolum"
    trace_dir.mkdir(parents=True, exist_ok=True)
    (trace_dir / "rift-trace.json").write_text(json.dumps(probe_project(projects["rift.json"]), indent=1))
    # the live project's settings on the Trench file, for profiling live cost without a live input
    lp = project("THE RIFT LIVE (Trench file)", "../../assets/audio/trench.wav", length)
    lp["parameters"].update(LIVE_PARAMETERS)
    lp["parameters"].pop("scene/volumeNoise")  # live's clear air: the scene's own zero
    (HERE / "diag-live-trench.json").write_text(json.dumps(lp, indent=1))
    print(f"wrote rift.scene.json ({(HERE / 'rift.scene.json').stat().st_size / 1e6:.1f} MB), {', '.join(projects)};"
          f" flight {length:.0f} m")


if __name__ == "__main__":
    main()
