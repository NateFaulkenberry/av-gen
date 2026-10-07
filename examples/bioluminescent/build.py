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
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp1  # noqa: E402  (placement helpers: candidates, records, value_noise, basis, quat_from_matrix)
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
PEN = [0.05, 0.62, 1.0]
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
def place(g):
    """The ecosystem along the whole reach (cp1.place's rules, over REGION)."""
    R = REGION
    P = {}
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
    P["crinoid"] = cp1.records(g, X, Z, S, 0.0, 102, facing=lambda x, z: np.array([land.river_x(z) - x, 0, 0.01]),
                               sink=0.3)

    # sea-pen meadows on the floor and lower slopes, in drifts
    X, Z = cp1.candidates(g, 1.8, 201, R)
    sl = g.slope(X, Z)
    cl = cp1.value_noise(X, Z, 22.0, 7) * 0.7 + cp1.value_noise(X, Z, 7.0, 8) * 0.3
    keep = (sl < 0.45) & (~g.wet(X, Z)) & (np.abs(dxc(X, Z)) < 60) & (cl > 0.42)
    X, Z = X[keep], Z[keep]
    P["seapen"] = cp1.records(g, X, Z, np.random.default_rng(202).uniform(0.6, 1.5, len(X)), 0.3, 203, sink=0.02)

    # polyp mats on gentle ground (the walls belong to the crust)
    X, Z = cp1.candidates(g, 0.45, 301, R)
    sl = g.slope(X, Z)
    d = np.abs(dxc(X, Z))
    p = np.clip(1.3 - d / 160.0, 0, 1) * (cp1.value_noise(X, Z, 15.0, 9) * 0.6 + 0.5)
    keep = (sl < 1.1) & (~g.wet(X, Z)) & (np.random.default_rng(302).random(len(X)) < p)
    X, Z = X[keep], Z[keep]
    P["mat"] = cp1.records(g, X, Z, np.random.default_rng(303).uniform(0.7, 1.8, len(X)), 0.85, 304, sink=0.03)

    # sea fans on the walls, face-on to the canyon's axis; fewer than CP2 (their violet must not own the walls)
    X, Z = cp1.candidates(g, 0.018, 401, R)
    sl = g.slope(X, Z)
    d = np.abs(dxc(X, Z))
    keep = (sl > 0.35) & (sl < 3.0) & (d > 18) & (d < 120) & (cp1.value_noise(X, Z, 30.0, 10) > 0.42)
    X, Z = X[keep], Z[keep]
    P["fan"] = cp1.records(g, X, Z, np.random.default_rng(402).uniform(0.6, 1.9, len(X)), 0.35, 403,
                           facing=cp1.toward_centre, sink=0.05)

    # whip tufts along the banks
    X, Z = cp1.candidates(g, 0.15, 501, R)
    dr = np.abs(dxr(X, Z))
    keep = (dr > 7) & (dr < 26) & (~g.wet(X, Z)) & (g.slope(X, Z) < 0.8) & \
        (cp1.value_noise(X, Z, 12.0, 11) > 0.35)
    X, Z = X[keep], Z[keep]
    P["whips"] = cp1.records(g, X, Z, np.random.default_rng(502).uniform(0.7, 1.4, len(X)), 0.2, 503)

    # lanterns: rare clusters in hollows
    X, Z = cp1.candidates(g, 0.05, 601, R)
    keep = (~g.wet(X, Z)) & (g.slope(X, Z) < 1.0) & (np.abs(dxc(X, Z)) < 70) & \
        (cp1.value_noise(X, Z, 9.0, 12) > 0.64)
    X, Z = X[keep], Z[keep]
    P["lanterns"] = cp1.records(g, X, Z, np.random.default_rng(602).uniform(0.8, 1.6, len(X)), 0.5, 603)

    # wall crust patches (emitters only): steep rock, clumped along ledges
    X, Z = cp1.candidates(g, 0.09, 801, R)
    sl = g.slope(X, Z)
    d = np.abs(dxc(X, Z))
    cl = cp1.value_noise(X, Z, 18.0, 13) * 0.6 + cp1.value_noise(X, Z, 5.0, 14) * 0.4
    keep = (sl > 0.6) & (d > 15) & (d < 150) & (cl > 0.45)
    X, Z = X[keep], Z[keep]
    P["crust"] = cp1.records(g, X, Z, np.random.default_rng(802).uniform(1.0, 2.6, len(X)), 0.95, 803)

    # embers: deep in the wall's gullies (the rarest accent)
    X, Z = cp1.candidates(g, 0.004, 851, R)
    sl = g.slope(X, Z)
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
    return P


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
    """The organisms' bodies. No decimated LOD levels: vertex clustering turns a feathery sea pen or a fan's lattice
    into blocky blobs (seen in v1). Bodies are culled by distance instead; past it the emitters carry the far field."""
    N = []
    N.append(mesh_node("crinoidStalk", "crinoid_stalk", P["crinoid"], m(BODY, VIOLET_DIM, 0.01, 0.6),
                       max_distance=1200.0))
    N.append(mesh_node("crinoidArms", "crinoid_arms", P["crinoid"],
                       m([0.05, 0.04, 0.08], VIOLET_DIM, 0.06, 0.5, double=True), max_distance=900.0))
    for i, part in enumerate(chunks(P["seapen"])):
        N.append(mesh_node(f"seapen{i}", "seapen", part, m([0.03, 0.05, 0.07], PEN, 0.04, 0.45, double=True),
                           max_distance=110.0))
    for i, part in enumerate(chunks(P["mat"])):
        N.append(mesh_node(f"mat{i}", "mat_cushion", part, m([0.006, 0.008, 0.012], None, 0.0, 0.95),
                           max_distance=70.0))
    N.append(mesh_node("fanBody", "fan_body", P["fan"], m([0.03, 0.025, 0.05], VIOLET_DIM, 0.01, 0.6),
                       max_distance=220.0))
    N.append(mesh_node("whipsBody", "whips_body", P["whips"], m([0.03, 0.04, 0.05], CYAN, 0.05, 0.5),
                       max_distance=150.0))
    N.append(mesh_node("lanternStalks", "lantern_stalks", P["lanterns"], m([0.05, 0.03, 0.02]), max_distance=120.0))
    N.append(mesh_node("lanternPods", "lantern_pods", P["lanterns"], m([0.3, 0.12, 0.03], AMBER, 14.0, 0.25),
                       max_distance=500.0))
    N.append(host_node("crustHost", P["crust"]))
    N.append(host_node("emberHost", P["ember"]))
    N.append(host_node("planktonHost", P["plankton"]))
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
    return {"spriteRadius": 1.5, "maxSprites": 65536, "layers": [
        # the canopy answers late: energy, not excitation; light runs out along the arms behind it
        layer("crinoidBeads", ["crinoidStalk"], "crinoid_beads", CYAN, 1.2, 10.0, "propE", sparsity=0.3,
              flicker=0.1, flickerRate=1.5, breath=0.5, breathRate=0.04, wakeGain=1.5, maxDistance=1000,
              responseGain=1.6, nearFade=5.0),
        layer("crinoidChains", ["crinoidStalk"], "crinoid_chains", PALE, 0.8, 6.0, "highs", breath=0.4,
              breathRate=0.09, wakeGain=1.5, maxDistance=1000, responseThreshold=0.15, responseGain=1.4,
              nearFade=7.0),
        layer("matPolyps", mats, "mat_polyps", BLUE, 0.5, 10.0, "propU", sparsity=0.55, pulseRate=1.5,
              pulseDecay=0.8, wakeGain=2.0, maxDistance=320),
        layer("fanPolyps", ["fanBody"], "fan_polyps", VIOLET_DIM, 0.15, 4.0, "propU", excited_color=MAGENTA,
              sparsity=0.45, wakeGain=1.0, maxDistance=700, nearFade=2.0),
        layer("whipsTips", ["whipsBody"], "whips_tips", PALE, 1.2, 5.0, "highs", sparsity=0.3, flicker=0.6,
              flickerRate=7.0, wakeGain=1.0, maxDistance=260, responseThreshold=0.15, responseGain=1.4,
              nearFade=1.5),
        layer("seapenPolyps", pens, "seapen", PEN, 0.4, 9.0, "propU", sparsity=0.3, pulseRate=0.8, pulseDecay=1.0,
              wakeGain=2.0, maxDistance=300),
        layer("crust", ["crustHost"], "crust", DEEP_BLUE, 0.25, 6.0, "propU", sparsity=0.6, pulseRate=2.0,
              pulseDecay=1.5, breath=0.4, breathRate=0.03, size=0.8, wakeGain=3.0, maxDistance=700),
        layer("embers", ["emberHost"], "crust", EMBER, 0.6, 0.0, "", sparsity=0.85, breath=0.6, breathRate=0.02,
              size=0.7, maxDistance=400, wakeField=""),
        layer("plankton", ["planktonHost"], "plankton", BLUE, 0.15, 14.0, "propU", sparsity=0.7, pulseRate=4.0,
              pulseDecay=0.6, flicker=0.3, flickerRate=3.0, wakeGain=2.0, maxDistance=320),
    ]}


# ============================================================================================ fields & the medium
PROP = {
    "name": "prop", "mode": "excitable", "wrap": "clamp",
    "resolution": [256, 1, 1024], "boundsMin": [-200.0, -400.0, Z0], "boundsMax": [200.0, 400.0, Z1],
    "injectField": "ignite", "injectRate": 1.0, "conductivityField": "conduct",
    "threshold": 0.5, "coupling": 1.0, "waveSpeed": 14.0, "riseRate": 10.0, "excitationDecay": 0.7,
    "refractoryTime": 2.0, "refractoryStrength": 4.0, "energyTime": 3.0, "wakeTime": 30.0, "noise": 0.35,
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


def scene(P):
    nodes = list(fields())
    nodes.append({"name": "land", "kind": "terrain", "position": [0, 0, 0], "world": land.world(),
                  "terrain": {"chunkSize": 40.0, "resolution": 40, "lodLevels": 4, "lodDistance": 140.0,
                              "viewDistance": 1600.0, "shadowDistance": 150.0, "skirtDepth": 2.0,
                              "groundMottle": False,
                              "water": {"shallowColor": [0.004, 0.008, 0.014], "deepColor": [0.001, 0.003, 0.008],
                                        "reflection": 0.7, "reflectionTint": [0.3, 0.45, 0.8], "specular": 0.6,
                                        "fresnel": 0.3, "foam": 0.0, "ripple": 0.45}},
                  "material": {"program": "rock", "baseColor": [0.022, 0.026, 0.036], "roughness": 0.8,
                               "metallic": 0.0}})
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
            "volumeLocalLights": 0.0, "volumeNoise": 0.6, "volumeNoiseScale": 0.03,
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
                    "castsShadow": False}],
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
    "canopy":  (27.0, 2.0, 45.0, 56.0, 10.0),   # through the crinoid crowns
    "wall":    (16.0, 9.0, 35.0, 50.0, 8.0),    # along the wall of fans and crust
    "climb":   (34.0, 0.0, 60.0, 48.0, 4.0),    # the build: rising into the crowns, anticipation
    "dive":    (5.0, 0.0, 40.0, 66.0, 16.0),    # the drop: down to the water, fast
    "reveal":  (70.0, -10.0, 120.0, 58.0, 4.0), # high: the canyon's length
    "after":   (9.0, 4.0, 50.0, 40.0, 2.0),     # the aftermath: slow, pulling up
}

# Per state: camera behaviour, pace (m/s), ignition gain, wave speed (m/s), rock glow, haze glow,
# spores (per s), swarm (per s), transition (s, easing).
STAGES = {
    "Dark":      ("drift",  3.0, 0.0, 10.0, 0.0, 0.0, 250.0, 0.0, (4.0, "smooth")),
    "Stirring":  ("river",  5.0, 1.0, 12.0, 0.0, 0.0, 400.0, 0.0, (6.0, "smooth")),
    "Breath":    ("river",  1.5, 0.0, 12.0, 0.0, 0.0, 300.0, 0.0, (1.0, "smooth")),
    "Awake":     ("canopy", 8.0, 1.3, 16.0, 0.12, 0.0, 600.0, 0.0, (6.0, "smooth")),
    "Awake 2":   ("wall",   8.0, 1.4, 16.0, 0.15, 0.0, 600.0, 0.0, (10.0, "smooth")),
    "Build":     ("climb",  4.0, 0.5, 16.0, 0.05, 0.0, 3500.0, 0.0, (4.0, "easeInOut")),
    "Drop":      ("dive",  24.0, 3.0, 34.0, 1.4, 0.05, 2500.0, 9000.0, (1.2, "easeOut")),
    "Body":      ("canopy", 17.0, 2.0, 24.0, 0.6, 0.02, 1500.0, 3000.0, (6.0, "smooth")),
    "Body 2":    ("river",  20.0, 2.2, 26.0, 0.7, 0.025, 1500.0, 4000.0, (6.0, "smooth")),
    "Body 3":    ("wall",   15.0, 2.0, 24.0, 0.6, 0.02, 1500.0, 3000.0, (6.0, "smooth")),
    "Body 4":    ("reveal", 12.0, 2.4, 26.0, 0.8, 0.03, 1500.0, 2000.0, (8.0, "smooth")),
    "Aftermath": ("after",  2.5, 0.0, 12.0, 0.0, 0.0, 600.0, 0.0, (4.0, "smooth")),
}
BODY_CYCLE = ["Body", "Body 2", "Body 3", "Body 4"]
PADS = {"Dark": 36, "Stirring": 37, "Awake": 38, "Build": 39, "Drop": 40, "Body": 41, "Aftermath": 42}


def preset(name, length):
    cam, pace, ignite, speed, rock, haze, spores, swarm, _t = STAGES[name]
    up, lat, look, fov, bank = CAM[cam]
    return {"name": name.lower(), "values": {
        "macros/flight": [pace / 20.0],
        "camera/splineOffset": [lat, up, 0.0], "camera/lookAhead": [look], "camera/fov": [fov],
        "camera/splineBank": [bank],
        "grid/prop/injectRate": [ignite],
        "grid/prop/waveSpeed": [speed],
        "material/rock/emissionIntensity": [rock],
        "scene/volumeEmission": [haze],
        "particles/spores/spawnRate": [spores],
        "particles/swarm/spawnRate": [swarm],
    }}


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
        if name in BODY_CYCLE or name in ("Awake 2",):
            transition["quantize"] = "beat"
        S.append({"name": name, "preset": name.lower(), "transition": transition, "triggers": trig})
    return {"initial": "Dark", "states": S}


def macros():
    names = ("energy", "bassSlow", "flight", "sensitivity")
    return [{"name": n, "label": n.upper(), "default": 0.25 if n == "sensitivity" else 0.0, "targets": []}
            for n in names]


def routes(length):
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
    r.append({"source": "audio.bassLevel", "target": "grid/prop/injectRate", "op": "add", "amount": 0.8,
              "chain": {"attackMs": 60, "decayMs": 400}})
    # highs are small life: spores rise with the treble
    r.append({"source": "audio.trebleLevel", "target": "particles/spores/spawnRate", "op": "add", "amount": 1600.0,
              "chain": {"attackMs": 80, "decayMs": 900}})
    # the snare throws a burst of the swarm (only where the stage lets the swarm fly at all: it multiplies)
    r.append({"source": "audio.onsetMid", "target": "particles/swarm/spawnRate", "op": "add", "amount": 6000.0,
              "chain": {"envelope": "peakhold", "envelopeHoldMs": 40, "envelopeFallPerSecond": 4.0},
              "depthSource": "macro.energy", "depthMin": 0.0, "depthMax": 1.0})
    # mids breathe the siphonophore chains
    r.append({"source": "audio.mid", "target": "ecosystem/crinoidChains/intensity", "op": "add", "amount": 1.2,
              "chain": {"attackMs": 200, "decayMs": 1200}})
    # the camera floats a little off its path, more as the music grows
    for src, comp, amt in (("driftA", 0, 2.0), ("driftC", 1, 1.0)):
        r.append({"source": f"lfo.{src}.bipolar", "target": "camera/splineOffset", "op": "add", "amount": amt,
                  "component": comp, "depthSource": "macro.energy", "depthMin": 0.4, "depthMax": 1.0})
    return r


POST = {
    "post/bloom/enabled": True, "post/bloom/threshold": 0.9, "post/bloom/intensity": 0.32,
    "post/bloom/emissionWeight": 0.75, "post/tonemap/chroma-retention": 0.6, "post/output/vignette": 0.28,
    "post/grade/contrast": 1.08, "post/grade/saturation": 1.05,
    "camera/exposure/mode": 0, "camera/mode": 2,
    "sources/driftA/rate": 0.031, "sources/driftC/rate": 0.047,
}


def project(name, audio, length, live=False):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "rift.scene.json"}},
        "live": {"qualityStrategy": "effects_first", "targetFps": 60},
        "parameters": dict(POST),
        "sources": [{"kind": "lfo", "name": n, "settings": {"shape": "sine"}} for n in ("driftA", "driftC")],
        "sonic": {},
        "routes": routes(length),
        "worldMacros": macros(),
        "presets": [preset(n, length) for n in STAGES],
        "states": states(),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/rift.mp4"},
    }
    if audio:
        p["assets"]["audio"] = {"path": audio}
    if live:
        p["sonic"] = {"live": True}
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


def main():
    organisms.build_all()
    print("ground...")
    g = land.Ground(land.world(), *REGION, step=2.0)
    P = place(g)
    for k, v in P.items():
        print(f"  {k}: {len(v)}")
    sc, length = scene(P)
    (HERE / "rift.scene.json").write_text(json.dumps(sc, separators=(",", ":")))
    projects = {
        "rift.json": project("THE RIFT / Trench", "../../assets/audio/trench.wav", length),
        "rift-live.json": project("THE RIFT LIVE", None, length, live=True),
    }
    for n, p in projects.items():
        (HERE / n).write_text(json.dumps(p, indent=1) + "\n")
    trace_dir = HERE.parents[1] / "build" / "biolum"
    trace_dir.mkdir(parents=True, exist_ok=True)
    (trace_dir / "rift-trace.json").write_text(json.dumps(probe_project(projects["rift.json"]), indent=1))
    print(f"wrote rift.scene.json ({(HERE / 'rift.scene.json').stat().st_size / 1e6:.1f} MB), {', '.join(projects)};"
          f" flight {length:.0f} m")


if __name__ == "__main__":
    main()
