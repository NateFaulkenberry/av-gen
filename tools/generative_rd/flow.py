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
ROCKS = [(-34.0, 6.5, 2.6), (-2.0, -3.0, 3.2), (30.0, 4.0, 2.4)]  # (x, offset from the centreline, radius)
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


def bed_mesh(n=300):
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
    "ink": ("Ink River",
            "The same river seen from above its skin: ink from three springs is carried by the current, split by the "
            "boulders and curled into marbling by the eddies they shed, while petals ride the surface and gather "
            "where the eddies hold them."),
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
    mesh_dir = os.path.join(common.EXAMPLES, sid, "meshes")
    pos, nrm, uv, idx = bed_mesh()
    common.write_glb(os.path.join(mesh_dir, "flow-bed.glb"), pos, nrm, uv, idx, "bed")
    s.proc("bed", {"kind": "mesh", "asset": "meshes/flow-bed.glb"},
           material={"baseColor": C(pal, "bank"), "roughness": 0.85, "metallic": 0.0})
    for i, (cx, cz, r) in enumerate(rock_centres()):
        s.proc("rock%d" % i, {"kind": "sphere", "radius": 1.0, "segments": 48, "rings": 24},
               position=(cx, float(bed_height(np.array(cx), np.array(cz))) + 0.2 * r, cz), scale=(r, 0.8 * r, r),
               material={"baseColor": C(pal, "bank"), "roughness": 0.5, "metallic": 0.0})
    # strands: a grid over the channel and its banks, lifted to the bed by the bed-height grid
    g = strands_program(pal)
    s.program(g.doc(base=g.base, emission=g.emit))
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
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


WATER = 2.4  # the river's surface (m above the bed's lowest point)


def ink(pal_name="jade", autonomous=False):
    """B2 + B3: the river's skin. A dye grid is advected by the same current (semi-Lagrangian, a little diffusion, no
    dissipation to speak of): three springs upstream feed it, the boulders split it, their eddies curl it into
    marbling. Petals float on the surface and gather where the eddies hold them. The water is a sheet at WATER that
    the banks rise through, so the frame has no edge."""
    pal = dict(PALETTES[pal_name], name=pal_name)
    s = common.Scene("flow-ink", DESIGNS["ink"][0], design("ink"))
    flow = river(s)
    # springs: three small spheres upstream, each wandering across a third of the channel
    springs = []
    for i, off in enumerate((ROCKS[0][1] - 1.0, ROCKS[0][1] + 4.0, -6.0)):  # two aimed at the first boulder
        n = "spring%d" % i
        x0 = -70.0
        c = (x0, WATER, float(centre(np.array(x0))) + off)
        s.field(n, {"kind": "sphere", "radius": 0.9, "softness": 0.4}, position=c)
        common.lissajous_track(s, "field/%s/position" % n, c, (1.5, 2.5), 9.0 + 3.1 * i, ratio=(1, 1),
                               phase=(0.2 * i, 0.0))
        # each spring pulses (on 1.6 s, off 1.6 s, out of step with the others): ink in bands, so the current's
        # shear and the eddies show as marbling rather than a uniform stain
        per = 3.2 + 0.7 * i
        s.track("field/%s/strength" % n, [{"time": 0.0, "value": [1.0], "interp": "smooth"},
                                          {"time": per * 0.45, "value": [1.0], "interp": "smooth"},
                                          {"time": per * 0.5, "value": [0.0], "interp": "smooth"},
                                          {"time": per * 0.95, "value": [0.0], "interp": "smooth"},
                                          {"time": per, "value": [1.0], "interp": "smooth"}], loop=per)
        springs.append(n)
    s.field("springs", {"kind": "compound", "combine": "add", "children": springs})
    s.grid(name="dye", mode="scalar", wrap="clamp", resolution=[512, 1, 512],  # a plane (ADR-1452)
           boundsMin=[-EXTENT, WATER - 1.0, -EXTENT], boundsMax=[EXTENT, WATER + 1.0, EXTENT],
           injectField="springs", injectRate=40.0, velocityField=flow, advect=1.0, diffusion=0.0,
           diffuseIterations=0, dissipation=0.0, ceiling=1.0, simRate=10.0, maxSubSteps=4, seed=1,
           checkpointInterval=5.0)
    s.grid_reader("ink", "dye")
    mesh_dir = os.path.join(common.EXAMPLES, "flow-ink", "meshes")
    pos, nrm, uv, idx = bed_mesh(200)
    common.write_glb(os.path.join(mesh_dir, "flow-bed.glb"), pos, nrm, uv, idx, "bed")
    s.proc("bed", {"kind": "mesh", "asset": "meshes/flow-bed.glb"},
           material={"baseColor": C(pal, "bank"), "roughness": 0.85, "metallic": 0.0})
    for i, (cx, cz, r) in enumerate(rock_centres()):
        s.proc("rock%d" % i, {"kind": "sphere", "radius": 1.0, "segments": 48, "rings": 24},
               position=(cx, WATER - 0.2 * r, cz), scale=(r, 0.9 * r, r),
               material={"baseColor": C(pal, "bank", 1.6), "roughness": 0.45, "metallic": 0.0})
    pw, nw, uw, iw = common.plane_mesh(2 * EXTENT, 2 * EXTENT, 320, 320, y=WATER)
    common.write_glb(os.path.join(mesh_dir, "flow-water.glb"), pw, nw, uw, iw, "water")
    g = common.Prog("flowInk")
    g.op("ink", "field", 0, field="ink")
    g.op(None, "smoothstep", 0, a=0, k=[0.004, 0.25, 0, 0])
    g.op("water", "constant", 1, k=C(pal, "bed") + [1])
    g.op("dye", "constant", 2, k=C(pal, "strand") + [1])
    g.op(None, "mixBy", 3, a=1, b=2, c=0)
    g.op(None, "field", 4, field="wakeFlow")                      # eddies: a faint sheen of their own
    g.op(None, "multiply", 4, a=4, b=4)                           # a vector field reads (x, y, z, 0):
    g.op(None, "gradient", 4, a=4, k=[1.0, 1.0, 1.0, 0.0], f=1.0 / 25.0)  # |v|^2 / 25, saturated
    g.op("eddy", "constant", 5, k=C(pal, "mote", 0.35) + [1])
    g.op(None, "multiply", 5, a=4, b=5)
    g.op(None, "multiply", 6, a=0, b=0)
    g.op("inkGlow", "constant", 7, k=C(pal, "strandTip", 0.25) + [1])
    g.op(None, "multiply", 6, a=6, b=7)
    g.op(None, "add", 5, a=5, b=6)
    g.base, g.emit = 3, 5
    s.program(g.doc(base=g.base, emission=g.emit))
    s.proc("water", {"kind": "mesh", "asset": "meshes/flow-water.glb"},
           material={"program": g.name, "baseColor": C(pal, "bed"), "roughness": 0.12, "metallic": 0.0},
           deformers=[{"kind": "field", "field": "wakeFlow", "amount": 0.06, "space": "world", "alongNormal": True}],
           extra={"normalStep": 0.1})
    s.particles("petals", capacity=6000, seed=9, spawnRate=260.0, shape="box", extent=[2.0, 0.0, 9.0],
                position=[-72.0, WATER + 0.05, float(centre(np.array(-72.0)))],
                lifetimeMin=18.0, lifetimeMax=24.0, spread=0.0, speedMin=0.0, speedMax=0.05, gravity=[0, 0, 0],
                drag=0.2, turbulence=0.0, sizeStart=0.22, sizeEnd=0.18, blend="alpha", emissive=0.4, softness=0.3,
                shape2d="leaf", colorStart=C(pal, "mote") + [1.0], colorEnd=C(pal, "mote") + [0.0],
                fieldForces=[{"field": "baseFlow", "mode": "velocity", "strength": 1.0, "mix": 0.3},
                             {"field": "wakeFlow", "mode": "force", "strength": 1.5}])
    environment(s, pal, key_dir=[0.4, -0.6, -0.7])
    s.camera = {"mode": 1, "position": [-14.0, 62.0, 34.0], "target": [-14.0, WATER, -4.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(3.0, 1.0, 2.0), tamp=(3.0, 0.5, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 30.0, "post/bloom/intensity": 0.2, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.08})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


def storm(pal_name="ink", autonomous=False):
    """B3: the same river under weather: a two-scale curl-noise turbulence over the designed current. The large scale
    keeps the channel legible; the fine scale ruffles the strands' tips."""
    return meadow(pal_name, autonomous, turbulence=1.6, sid="flow-storm", title="Current Meadow, Storm")


CONFIGS = {"meadow": meadow, "ink": ink, "storm": storm}


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
    for name in ([a.config] if a.config else list(CONFIGS)):
        kw = {"autonomous": a.autonomous}
        if a.palette:
            kw["pal_name"] = a.palette
        s = CONFIGS[name](**kw)
        pp, sp = common.write_example(s, os.path.join(common.EXAMPLES, s.id))
        print(pp)
        if a.variant:
            params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
            print(common.variant(pp, a.out, a.wav, a.camera, params, a.track))


if __name__ == "__main__":
    main()
