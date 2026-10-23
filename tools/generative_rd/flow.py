#!/usr/bin/env python3
"""EXPERIMENT B, flow fields and advection: the generator (docs/prototypes/generative-rd/B-flow.md).

    python3 tools/generative_rd/flow.py                    # every configuration
    python3 tools/generative_rd/flow.py --config meadow    # one
    python3 tools/generative_rd/flow.py --config meadow --variant --out <v.json> [--wav <w>] [--set k=v]

One designed current drives everything. It is the sum of two grids (ADR-032, no new engine code):
  base     a static vector grid with authored values (ADR-1282): the river, designed in numpy -- it follows a meandering
           channel, runs fastest mid-stream, parts around three boulders (potential flow round a cylinder)
  wake     a simulated vector grid: moving vortex sources (and, with music, onset impulses) inject into it, it is
           advected by the whole current (itself included) and fades -- eddies are born, drift downstream and die
The `flow` compound field (base + wake) is read by strands (fibres along streamlines, ADR-1181), drifting motes
(particles in velocity mode, with trails) and the river's skin (a Field deformer). Writes examples/experiments/flow-*.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
from tools.generative_rd import common  # noqa: E402
from tools.generative_rd.common import R, hexrgb  # noqa: E402

# ================================================================================================ the river (metres)
EXTENT = 80.0             # the domain is [-80, 80] on x and z
AMP, WAVE = 18.0, 110.0   # the channel's meander: z_c(x) = AMP sin(2 pi x / WAVE)
HALF = 13.0               # half the channel's width
BANK = 9.0                # the banks' height above the bed
ROCKS = [(-34.0, 4.0, 4.2), (-2.0, -3.0, 4.8), (30.0, 3.0, 3.8)]  # (x, offset from the centreline, radius)
U = 3.0                   # mid-stream speed, m/s


def centre(x):
    return AMP * np.sin(2 * math.pi * x / WAVE)


def centre_slope(x):
    return AMP * 2 * math.pi / WAVE * np.cos(2 * math.pi * x / WAVE)


def bed_height(x, z):
    d = np.abs(z - centre(x)) / HALF
    swell = 1.0 + 0.25 * np.sin(x * 0.11 + 1.3) * np.sin(z * 0.07)       # the banks roll
    return BANK * (1.0 - np.exp(-1.4 * d * d)) * swell


def rock_centres():
    return [(x, float(centre(x)) + off, r) for x, off, r in ROCKS]


def base_current(x, y, z):
    """The designed river: along the channel's tangent, parabolic across it, slower near the bed and parting round
    each boulder (2D potential flow round a cylinder, blended into the channel flow)."""
    tx = np.ones_like(x)
    tz = centre_slope(x)
    n = np.sqrt(tx * tx + tz * tz)
    tx, tz = tx / n, tz / n
    d = np.abs(z - centre(x)) / HALF
    speed = U * np.clip(1.0 - d * d, 0.05, None)
    above = y - bed_height(x, z)
    speed *= np.clip(0.35 + 0.65 * above / 2.0, 0.35, 1.0)
    vx, vz = speed * tx, speed * tz
    for cx, cz, r in rock_centres():
        dx, dz = x - cx, z - cz
        r2 = np.maximum(dx * dx + dz * dz, 1e-6)
        # uniform flow (vx, vz) round a cylinder of radius r: v = V - r^2 (V (dx^2 - dz^2) + 2 ... ) / r^4 (complex form)
        w = vx + 1j * vz
        zc = dx + 1j * dz
        pert = -np.conj(w) * (r * r) / (zc * zc)
        inside = r2 < (r * 1.05) ** 2
        f = np.where(inside, 0.0, 1.0)
        vx = (vx + np.real(np.conj(pert))) * f
        vz = (vz + np.imag(np.conj(pert))) * f
    return vx, np.zeros_like(vx), vz


def grid_points(res, lo, hi):
    axes = [lo[i] + (np.arange(res[i]) + 0.5) / res[i] * (hi[i] - lo[i]) for i in range(3)]
    Z, Y, X = np.meshgrid(axes[2], axes[1], axes[0], indexing="ij")  # storage order: k (z), j (y), i (x)
    return X, Y, Z


def base_grid(res=(96, 3, 96)):
    lo, hi = [-EXTENT, -1.0, -EXTENT], [EXTENT, 11.0, EXTENT]
    X, Y, Z = grid_points(res, lo, hi)
    vx, vy, vz = base_current(X, Y, Z)
    cells = np.stack([vx, vy, vz, np.zeros_like(vx)], axis=-1).reshape(-1)
    return {"name": "base", "mode": "vector", "wrap": "clamp", "resolution": list(res), "boundsMin": lo,
            "boundsMax": hi, "simRate": 60.0, "maxSubSteps": 4, "initial": [round(float(v), 3) for v in cells]}


def bed_grid(res=(128, 1, 128)):
    lo, hi = [-EXTENT, -1.0, -EXTENT], [EXTENT, 1.0, EXTENT]
    X, Y, Z = grid_points(res, lo, hi)
    h = bed_height(X, Z).reshape(-1)
    return {"name": "bed", "mode": "scalar", "wrap": "clamp", "resolution": list(res), "boundsMin": lo,
            "boundsMax": hi, "simRate": 60.0, "maxSubSteps": 4, "initial": [round(float(v), 3) for v in h]}


def channel_grid(res=(128, 1, 128)):
    """1 in the channel, fading to 0 on the banks: where the strands grow."""
    lo, hi = [-EXTENT, -1.0, -EXTENT], [EXTENT, 1.0, EXTENT]
    X, Y, Z = grid_points(res, lo, hi)
    d = np.abs(Z - centre(X)) / HALF
    m = 1.0 - np.clip((d - 0.75) / 0.3, 0.0, 1.0)
    return {"name": "channel", "mode": "scalar", "wrap": "clamp", "resolution": list(res), "boundsMin": lo,
            "boundsMax": hi, "simRate": 60.0, "maxSubSteps": 4, "initial": [round(float(v), 2) for v in m.reshape(-1)]}


# one riverbed mesh, written once and shared by every flow scene (relative to each scene's folder)
BED_ASSET = "../flow-streams/meshes/flow-bed.glb"


def write_bed():
    pos, nrm, uv, idx = bed_mesh()
    common.write_glb(os.path.join(common.EXAMPLES, "flow-streams", "meshes", "flow-bed.glb"), pos, nrm, uv, idx, "bed")


def bed_mesh(n=220):
    pos, nrm, uv, idx = common.plane_mesh(2 * EXTENT, 2 * EXTENT, n, n)
    pos[:, 1] = bed_height(pos[:, 0], pos[:, 2])
    # normals by finite differences of the height function
    e = 0.05
    hx = (bed_height(pos[:, 0] + e, pos[:, 2]) - bed_height(pos[:, 0] - e, pos[:, 2])) / (2 * e)
    hz = (bed_height(pos[:, 0], pos[:, 2] + e) - bed_height(pos[:, 0], pos[:, 2] - e)) / (2 * e)
    nrm = np.stack([-hx, np.ones_like(hx), -hz], axis=1)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    return pos, nrm, uv, idx


PALETTES = {
    "ember": {  # black basalt; amber current lines; a cold rim
        "bed": "#08090b", "bank": "#0d0e10", "strand": "#3a2a18", "strandTip": "#ffd59a", "mote": "#ffb45a",
        "sky": "#010101", "horizon": "#06070a", "key": "#c8d2e8", "keyI": 1.6, "rim": "#4a6cff", "rimI": 1.0,
    },
    "jade": {  # a dark jade river: dark bed, pale-gold strands, warm motes
        "bed": "#050a09", "bank": "#2a2e2a", "strand": "#c9b98a", "strandTip": "#e8f0d0", "mote": "#ffd9a0",
        "sky": "#020605", "horizon": "#0d1d1a", "key": "#fff0d8", "keyI": 4.0, "rim": "#4fb3a0", "rimI": 1.4,
    },
    "ink": {  # black water and ink: silver strands, cold motes
        "bed": "#050608", "bank": "#0b0c10", "strand": "#a8b0c0", "strandTip": "#e0e8ff", "mote": "#9fd8ff",
        "sky": "#010102", "horizon": "#07090f", "key": "#d8e4ff", "keyI": 3.0, "rim": "#5a6cff", "rimI": 1.2,
    },
}


def C(pal, key, s=1.0):
    return hexrgb(pal[key], s)


def environment(s, pal, key_dir, fog=0.0012):
    s.environment = {
        "intensity": 0.08, "background": C(pal, "sky"), "fogColor": C(pal, "horizon"),
        "volumeDensity": fog, "fogSky": 1.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 120.0, "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C(pal, "sky"), "horizonColor": C(pal, "horizon"),
                "groundColor": C(pal, "sky"), "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.light("key", "directional", direction=key_dir, color=C(pal, "key"), intensity=pal["keyI"], castsShadow=False,
            contactShadow=False, softness=0.3)
    s.light("rim", "directional", direction=[-0.5, -0.3, 0.8], color=C(pal, "rim"), intensity=pal["rimI"],
            castsShadow=False, contactShadow=False)


def river(s, wake=True, vortices=True, turbulence=0.0):
    """The shared current: base + wake (+ a two-scale curl-noise turbulence, `turbulence` m/s). Returns the name of
    the field consumers read."""
    s.grid(**base_grid())
    s.grid(**bed_grid())
    s.grid_reader("baseFlow", "base")
    s.grid_reader("bedHeight", "bed")
    s.grid(**channel_grid())
    s.grid_reader("channelMask", "channel")
    children = ["baseFlow"]
    if wake:
        # vortex sources shed behind each boulder, alternating sense (a von Karman street, loosely): each sits a
        # radius and a half downstream and wanders across the wake
        names = []
        for i, (cx, cz, r) in enumerate(rock_centres()):
            n = "shed%d" % i
            s.field(n, {"kind": "vortex", "axis": [0.0, 1.0 if i % 2 else -1.0, 0.0], "strength": 4.0,
                        "falloff": {"kind": "smoothstep", "inner": 0.5, "outer": 3.5 * r}},
                    position=(cx + 2.2 * r, 2.0, cz))
            common.lissajous_track(s, "field/%s/position" % n, (cx + 2.4 * r, 2.0, cz), (0.6 * r, 1.4 * r),
                                   7.0 + 2.3 * i, ratio=(2, 1), phase=(0.1 * i, 0.0))
            names.append(n)
        # a kick: a ring of sideways push from upstream mid-channel (an onset shell read as a vector along its axis),
        # injected into the wake like the shedding -- a travelling disturbance the current then carries
        s.field("kickPush", {"kind": "onset", "onsetSource": "low", "point": [-50.0, 2.0, float(centre(np.array(-50.0)))],
                             "waveGeometry": "spherical", "audioSpeed": 14.0, "onsetWidth": 2.5, "onsetDecay": 0.8,
                             "axis": [0.0, 0.0, 1.0], "strength": 0.0})
        names.append("kickPush")
        s.field("shedding", {"kind": "compound", "combine": "add", "children": names})
        s.grid(name="wake", mode="vector", wrap="clamp", resolution=[256, 1, 256],  # a plane (ADR-1452)
               boundsMin=[-EXTENT, -1.0, -EXTENT], boundsMax=[EXTENT, 11.0, EXTENT],
               injectField="shedding", injectRate=1.6 if vortices else 0.0, velocityField="flowAll", advect=1.0,
               diffusion=0.6, diffuseIterations=2, dissipation=0.35, simRate=60.0, maxSubSteps=4, seed=3,
               checkpointInterval=5.0)
        s.grid_reader("wakeFlow", "wake")
        children.append("wakeFlow")
    if turbulence > 0.0:
        s.field("gust", {"kind": "curlNoise", "frequency": 0.05, "speed": 0.15, "seed": 4, "strength": turbulence})
        children.append("gust")
        s.field("chop", {"kind": "curlNoise", "frequency": 0.22, "speed": 0.5, "seed": 8, "strength": 0.4 * turbulence})
        children.append("chop")
    s.field("flowAll", {"kind": "compound", "combine": "add", "children": children})
    return "flowAll"


def strands_program(pal):
    g = common.Prog("flowStrand")
    g.op(None, "input", 0, inp="uv")
    g.op(None, "swizzle", 0, a=0, k=[1, 1, 1, 1])                        # along the strand, 0 root .. 1 tip
    g.op("root", "constant", 1, k=C(pal, "strand") + [1])
    g.op("tip", "constant", 2, k=C(pal, "strandTip") + [1])
    g.op(None, "mixBy", 3, a=1, b=2, c=0)
    g.op(None, "field", 4, field="wakeFlow")                              # the eddies' speed (a vector: |v|^2)
    g.op(None, "multiply", 4, a=4, b=4)
    g.op(None, "gradient", 4, a=4, k=[1.0, 1.0, 1.0, 0.0], f=1.0 / 16.0)
    g.op(None, "multiply", 4, a=4, b=0)                                   # tips only
    g.op("glow", "constant", 5, k=C(pal, "mote", 0.6) + [1])
    g.op(None, "multiply", 5, a=4, b=5)
    g.base, g.emit = 3, 5
    return g


DESIGNS = {
    "streams": ("Current Lines",
                "A river drawn only by its current: rakes of long lines follow one designed flow downstream, bunch "
                "where it narrows, part round three boulders and curl in their wakes, while pulses of light travel "
                "along them with the water."),
    "meadow": ("Current Meadow",
               "A meandering river seen from inside its channel: sixty thousand strands rooted in the bed are combed by "
               "one designed current, part round three boulders and swirl in the eddies the boulders shed, while warm "
               "motes ride the same current above them."),
}


def design(key):
    return {"category": "experiment", "thesis": DESIGNS[key][1], "experiment": "B flow fields"}


def meadow(pal_name="jade", autonomous=False, turbulence=0.0, sid="flow-meadow", title=None):
    pal = dict(PALETTES[pal_name], name=pal_name)
    s = common.Scene(sid, title or DESIGNS["meadow"][0], design("meadow"))
    flow = river(s, turbulence=turbulence)
    s.proc("bed", {"kind": "mesh", "asset": BED_ASSET},
           material={"baseColor": C(pal, "bank"), "roughness": 0.85, "metallic": 0.0})
    for i, (cx, cz, r) in enumerate(rock_centres()):
        s.proc("rock%d" % i, {"kind": "sphere", "radius": 1.0, "segments": 48, "rings": 24},
               position=(cx, float(bed_height(np.array(cx), np.array(cz))) + 0.2 * r, cz), scale=(r, 0.8 * r, r),
               material={"baseColor": C(pal, "bank"), "roughness": 0.5, "metallic": 0.0})
    # strands: a grid over the channel and its banks, lifted to the bed by the bed-height grid
    g = strands_program(pal)
    s.program(g.doc(base=g.base, emission=g.emit))
    if not autonomous:
        flow_music(s)
    nx, nz = 400, 160
    s.proc("strands", {"kind": "fiber", "fiberLength": 2.6, "fiberWidth": 0.03, "fiberTaper": 0.25,
                       "fiberSegments": 12, "fiberMinPixels": 0.8},
           distribution={"kind": "grid", "gridCount": [nx, 1, nz], "gridSpacing": [155.0 / nx, 1.0, 64.0 / nz]},
           variation={"seed": 11, "position": [0.35, 0.0, 0.35], "rotation": [0.1, 0.0, 0.1], "uniformScale": 0.35},
           material={"program": g.name, "baseColor": C(pal, "strand"), "roughness": 0.35, "metallic": 0.2},
           deformers=[{"kind": "streamline", "space": "world", "field": flow, "amount": 1.0, "falloff": 0.5,
                       "tension": 0.6}],
           extra={"effectors": [{"field": "bedHeight", "op": "positionOffset", "blend": "add", "strength": 1.0,
                                 "axis": [0.0, 1.0, 0.0]},
                                {"field": "channelMask", "op": "scale", "blend": "replace", "strength": 1.0,
                                 "scaleAxis": [1.0, 1.0, 1.0]}],
                  "lod": {"cull": True, "maxDistance": 0.0, "count": 1}})
    # motes riding the current, with trails: persistent trajectories, never reset on a beat
    s.particles("motes", capacity=12000, seed=5, spawnRate=900.0, shape="box", extent=[3.0, 2.0, 10.0],
                lifetimeMin=10.0, lifetimeMax=16.0, spread=0.0, speedMin=0.0, speedMax=0.2,
                gravity=[0, 0, 0], drag=0.2, turbulence=0.0, sizeStart=0.09, sizeEnd=0.05, blend="additive",
                emissive=5.0, softness=0.8, colorStart=C(pal, "mote") + [0.8], colorEnd=C(pal, "mote") + [0.0],
                trailEnabled=True, trailLength=16, trailStride=2, trailWidth=0.7, trailTaper=0.1, trailFade=0.0,
                trailTint=[1.0, 0.8, 0.6],
                # a compound is typed scalar, so a particle force reads the two grids itself (B-flow.md, gaps)
                fieldForces=[{"field": "baseFlow", "mode": "velocity", "strength": 1.0, "mix": 0.25},
                             {"field": "wakeFlow", "mode": "force", "strength": 1.5}])
    s.nodes[-1]["particles"]["position"] = [-74.0, 4.5, float(centre(np.array(-74.0)))]
    environment(s, pal, key_dir=[0.6, -0.55, -0.58])
    cx = 64.0
    s.camera = {"mode": 1, "position": [54.0, 24.0, 44.0], "target": [-6.0, 0.0, -2.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(2.0, 0.8, 2.0), tamp=(3.0, 0.5, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 28.0, "post/bloom/intensity": 0.25, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.06})
    if autonomous:
        s.response = dict(common.RESPONSE)
    return s


def storm(pal_name="ink", autonomous=False):  # noqa: D401
    """B3: the same river under weather: a two-scale curl-noise turbulence over the designed current. The large scale
    keeps the channel legible; the fine scale ruffles the strands' tips."""
    return meadow(pal_name, autonomous, turbulence=1.6, sid="flow-storm", title="Current Meadow, Storm")


MIDI_CC = [
    (1, "flowType", "FLOW TYPE: from the designed river toward weather (the curl-noise gusts)"),
    (2, "strength", "FLOW STRENGTH: the river's speed"),
    (3, "scale", "SPATIAL SCALE: the gusts' size (large and slow to small and quick)"),
    (4, "influence", "AUDIO INFLUENCE: how hard the music drives the current (half at rest)"),
    (5, "persistence", "PERSISTENCE: how long eddies and impulses live in the wake"),
    (6, "turbulence", "TURBULENCE: the fine chop"),
]
MIDI_NOTES = [(36, "eddy0"), (37, "eddy1"), (38, "eddy2")]


def flow_music(s, prog=None):
    """Phase 5. The music shapes the current, never a bank of effect knobs:
      - bass is the river's strength (the designed current speeds and slows as one body);
      - kicks push the water sideways in a ring that the current carries downstream (a travelling disturbance);
      - snares strengthen the boulders' shedding (eddies spin up in the wakes);
      - hats lift the fine chop where there is one, and brighten the light the lines carry;
      - sustained sound raises the gusts (a held passage brings weather; a sparse one lets it settle);
      - MIDI notes 36-38 spin up the eddy behind boulder 1, 2, 3 (velocity = strength, release = how long it lasts)."""
    s.route(common.audio("response.bass", "field/baseFlow/strength", 1.0, op="multiply", gain=0.9, offset=0.7,
                         attackMs=250, decayMs=1500),
            common.audio("signal.kick", "field/kickPush/strength", 1.5, attackMs=5, decayMs=200),
            common.audio("signal.snare", "field/shedding/strength", 0.6, attackMs=30, decayMs=1500))
    names = {f["name"] for f in s.nodes if f.get("kind") == "field"}
    if "gust" in names:
        s.route(common.audio("response.sustain", "field/gust/strength", 1.2, attackMs=3000, decayMs=6000),
                common.audio("signal.hat", "field/chop/strength", 0.8, attackMs=40, decayMs=400))
    if prog is not None and "pulse" in prog.at:
        s.route(common.audio("signal.hat", prog.path("pulse"), 1.0, op="multiply", gain=2.0, offset=1.0,
                             attackMs=30, decayMs=300))
    common.instrument(s, MIDI_CC, MIDI_NOTES)
    s.route(R("control.strength", "field/baseFlow/strength", 1.0, **common.CC),
            R("control.persistence", "grid/wake/dissipation", -0.3, **common.CC))
    if "gust" in names:
        s.route(R("control.flowType", "field/gust/strength", 2.5, **common.CC),
                R("control.scale", "field/gust/frequency", 0.15, **common.CC),
                R("control.turbulence", "field/chop/strength", 1.5, **common.CC))
    for i in range(3):
        s.route(R("control.eddy%d" % i, "field/shed%d/strength" % i, 6.0, attackMs=80, decayMs=2500))


def streams_program(pal):
    """Streamlines lit by pulses that travel along them, downstream (the flow's direction carried as light)."""
    g = common.Prog("flowStreams")
    g.op(None, "input", 0, inp="uv")
    g.op(None, "swizzle", 0, a=0, k=[1, 1, 1, 1])                       # 0 at the rake .. 1 at the far end
    g.op(None, "input", 1, inp="time")
    g.op("pace", "constant", 2, k=[-0.06, -0.06, -0.06, 0])             # pulses move along at ~5 m/s
    g.op(None, "multiply", 1, a=1, b=2)
    g.op("count", "constant", 2, k=[7.0, 7.0, 7.0, 0])                  # seven pulses along a line
    g.op(None, "multiply", 3, a=0, b=2)
    g.op(None, "add", 3, a=3, b=1)
    g.op(None, "palette", 3, a=3, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[1, 1, 1, 0], k4=[0, 0, 0, 0])
    g.op(None, "power", 3, a=3, f=10.0)                                 # narrow pulses
    g.op(None, "field", 4, field="wakeFlow")                            # brighter where the eddies run fast
    g.op(None, "multiply", 4, a=4, b=4)
    g.op(None, "gradient", 4, a=4, k=[1.0, 1.0, 1.0, 0.6], f=1.0 / 9.0)
    g.op(None, "multiply", 3, a=3, b=4)
    g.op(None, "remap", 5, a=0, k=[0.0, 0.08, 0.0, 1.0], f=1.0)         # fade in from the rake
    g.op(None, "multiply", 3, a=3, b=5)
    g.op(None, "add", 3, a=3, b=5)                                      # + the line itself, steady
    g.op("pulse", "constant", 6, k=C(pal, "mote", 1.2) + [1])
    g.op(None, "multiply", 6, a=3, b=6)
    g.op("line", "constant", 7, k=C(pal, "strand") + [1])
    g.base, g.emit = 7, 6
    return g


def streams(pal_name="ember", autonomous=False):
    """B (revised): the current drawn as streamlines. Rakes of long fibres seeded across the channel follow the
    designed current (base + shed eddies) for 70 m: they bunch where the river narrows, part round the boulders, and
    curl in the wakes; light pulses travel along them downstream."""
    pal = dict(PALETTES[pal_name], name=pal_name)
    s = common.Scene("flow-streams", DESIGNS["streams"][0], design("streams"))
    flow = river(s)
    s.proc("bed", {"kind": "mesh", "asset": BED_ASSET},
           material={"baseColor": C(pal, "bank"), "roughness": 0.7, "metallic": 0.0})
    for i, (cx, cz, r) in enumerate(rock_centres()):
        s.proc("rock%d" % i, {"kind": "sphere", "radius": 1.0, "segments": 64, "rings": 32},
               position=(cx, 0.6 * r, cz), scale=(r, 1.1 * r, r),
               material={"baseColor": C(pal, "bed"), "roughness": 0.35, "metallic": 0.3})
    g = streams_program(pal)
    s.program(g.doc(base=g.base, emission=g.emit))
    if not autonomous:
        flow_music(s, g)
    for k, (x0, n_across) in enumerate(((-76.0, 90), (-50.0, 70), (-20.0, 70), (12.0, 70), (40.0, 60))):
        zc = float(centre(np.array(x0)))
        s.proc("rake%d" % k, {"kind": "fiber", "fiberLength": 60.0 - 5.0 * k, "fiberWidth": 0.05, "fiberTaper": 0.6,
                              "fiberSegments": 40, "fiberMinPixels": 0.9},
               distribution={"kind": "grid", "gridCount": [1, 3, n_across], "gridSpacing": [1.0, 1.1, 22.0 / n_across]},
               transform={"position": [x0, 1.6, zc], "rotation": [0, 0, -90], "scale": [1, 1, 1]},
               variation={"seed": 3 + k, "position": [0.4, 0.3, 0.15]},
               material={"program": g.name, "baseColor": C(pal, "strand"), "roughness": 0.3, "metallic": 0.4},
               deformers=[{"kind": "streamline", "space": "world", "field": flow, "amount": 2.5, "falloff": 0.0,
                           "tension": 0.0}],
               extra={"lod": {"cull": True, "maxDistance": 0.0, "count": 1}})
    environment(s, pal, key_dir=[0.5, -0.6, -0.62], fog=0.0)
    s.camera = {"mode": 1, "position": [4.0, 54.0, 46.0], "target": [-24.0, 0.0, 2.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(2.0, 0.6, 1.5), tamp=(1.5, 0.3, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 26.0, "post/bloom/intensity": 0.35, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.12})
    if autonomous:
        s.response = dict(common.RESPONSE)
    return s


CONFIGS = {"meadow": meadow, "storm": storm, "streams": streams}


INDEX = {  # config -> (hidden?, notes): what the Examples > Live > Experiments menu lists
    "streams": (True, "MIDI notes 36-38 spin up the eddies."),  # a tool, not a scene (owner, 2026-10-17)
    "meadow": (True, "MIDI notes 36-38 spin up the eddies."),
    "storm": (True, "MIDI notes 36-38 spin up the eddies."),
}


def register(s, name):
    hidden, notes = INDEX[name]
    text = ("Play live audio (any mix) and, optionally, a MIDI controller: " + common.describe_ccs(MIDI_CC, notes) +
            ". " + s.design["thesis"] + " An R&D experiment: see docs/prototypes/generative-rd/B-flow.md.")
    common.index_entry(s.title, "experiments/%s/%s.json" % (s.id, s.id), text, live=True, hidden=hidden)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=None)
    ap.add_argument("--palette", default=None)
    ap.add_argument("--autonomous", action="store_true")
    ap.add_argument("--variant", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--wav")
    ap.add_argument("--camera")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--track", action="append", default=[])
    a = ap.parse_args()
    write_bed()
    for name in ([a.config] if a.config else list(CONFIGS)):
        kw = {"autonomous": a.autonomous}
        if a.palette:
            kw["pal_name"] = a.palette
        s = CONFIGS[name](**kw)
        pp, sp = common.write_example(s, os.path.join(common.EXAMPLES, s.id))
        print(pp)
        if not a.config and not a.autonomous and not a.palette and not getattr(a, "regime", None):
            register(s, name)
        if a.variant:
            params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
            print(common.variant(pp, a.out, a.wav, a.camera, params, a.track))


if __name__ == "__main__":
    main()
