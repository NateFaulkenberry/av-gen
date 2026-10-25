#!/usr/bin/env python3
"""EXPERIMENT A, coupled oscillators and phase fields: the generator (docs/prototypes/generative-rd/A-oscillators.md).

    python3 tools/generative_rd/oscillators.py                     # every configuration
    python3 tools/generative_rd/oscillators.py --config membrane   # one
    python3 tools/generative_rd/oscillators.py --config membrane --variant --out <v.json> [--wav <w>] [--set k=v]

An oscillator grid (ADR-1450) on the GPU holds the network: a Kuramoto-Sakaguchi lattice whose cells carry
(cos theta, sin theta, local order R, activity). The scenes read those four channels as geometry and light.
Writes examples/experiments/osc-<config>/.
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

# ================================================================================================ network regimes
# Surveyed in numpy (A-oscillators.md §3) with the kernel's own model. K in rad/s, lag in rad, r in cells.
REGIMES = {
    # concentric waves from pacemakers (cells tuned faster by the frequency field), colliding in cusps
    "targets": {"coupling": 10.0, "phaseLag": 1.0, "couplingRadius": 3, "spread": 0.03, "seedAmount": 0.0},
    # rotating phase vortices: curled spiral arms, a few dozen cores
    "spirals": {"coupling": 8.0, "phaseLag": 0.6, "couplingRadius": 4, "spread": 0.05, "seedAmount": 1.0},
    # partial synchrony: islands with phase defects between them (no lag)
    "islands": {"coupling": 4.0, "phaseLag": 0.3, "couplingRadius": 4, "spread": 0.15, "seedAmount": 1.0},
    # global synchrony (the whole field breathes as one)
    "unison": {"coupling": 12.0, "phaseLag": 0.0, "couplingRadius": 4, "spread": 0.03, "seedAmount": 0.5},
}

PALETTES = {
    "bronze": {  # bronze crests over a teal-black ground; the disagreement lights in cold cyan
        "trough": "#04100f", "crest": "#9c7a4e", "accent": "#5ff2e0", "accentGain": 0.45, "defect": "#ff7a3d",
        "defectGain": 0.0, "sky": "#020506", "horizon": "#081614", "key": "#ffe0b8", "keyI": 5.0, "rim": "#3fa7b8",
        "rimI": 1.2, "rough": 0.34, "metal": 0.7,
    },
    "nacre": {  # pale nacre on blue-grey, non-luminous
        "trough": "#2a313c", "crest": "#e8e4dc", "accent": "#000000", "accentGain": 0.0, "defect": "#000000",
        "defectGain": 0.0, "sky": "#0d1014", "horizon": "#2a3038", "key": "#fff3e3", "keyI": 5.0, "rim": "#8fa8c8",
        "rimI": 1.0, "rough": 0.5, "metal": 0.1,
    },
    "nocturne": {  # black lacquer; only the phase defects glow (amber) and the fronts (violet)
        "trough": "#030305", "crest": "#16131f", "accent": "#7a5cff", "accentGain": 0.9, "defect": "#ffb04a",
        "defectGain": 2.0, "sky": "#010102", "horizon": "#05040a", "key": "#c8c4e0", "keyI": 2.0, "rim": "#6a52d8",
        "rimI": 0.8, "rough": 0.2, "metal": 0.4,
    },
}


def C(pal, key, s=1.0):
    return hexrgb(pal[key], s)


def osc_grid(s, name, size, res, regime, seed=7, frequency=0.25, inject=None, inject_rate=0.0, freq_field=None,
             mask=None, links=0, extra=None):
    sx, sz = (size, size) if not isinstance(size, tuple) else size
    rx, rz = (res, res) if not isinstance(res, tuple) else res
    g = {"name": name, "mode": "oscillator", "wrap": "clamp", "resolution": [rx, 1, rz],
         "boundsMin": [-sx / 2, -40.0, -sz / 2], "boundsMax": [sx / 2, 40.0, sz / 2],
         "frequency": frequency, "simRate": 60.0, "maxSubSteps": 4, "seed": seed, "persistence": 0.6,
         "links": links, "linkRange": 48.0, "checkpointInterval": 5.0}
    g.update(REGIMES[regime])
    if inject:
        g.update({"injectField": inject, "injectRate": inject_rate})
    if freq_field:
        g["frequencyField"] = freq_field
    if mask:
        g["conductivityField"] = mask
    if extra:
        g.update(extra)
    s.grid(**g)
    return g


def readers(s, grid, prefix="", position=None, rotation=None):
    for name, ch in (("phase", 1), ("order", 2), ("activity", 3)):  # sin theta, local order R, strain + kicks
        n = s.grid_reader(prefix + (name.capitalize() if prefix else name), grid, channel=ch)
        if position is not None:
            n["position"] = [float(v) for v in position]
            n["field"]["position"] = [0.0, 0.0, 0.0]
        if rotation is not None:
            n["rotation"] = [float(v) for v in rotation]


def pacemakers(s, centres, gain=0.22, radius=5.0, period=0.0):
    """Cells tuned faster in a few small disks: each becomes the source of a train of concentric waves."""
    names = []
    for i, c in enumerate(centres):
        n = "pace%d" % i
        s.field(n, {"kind": "radial", "radius": radius, "strength": gain}, position=c)
        names.append(n)
        if period:
            common.lissajous_track(s, "field/%s/position" % n, c, (6.0, 5.0), period * (1 + 0.37 * i), ratio=(1, 1),
                                   phase=(0.13 * i, 0.25 + 0.21 * i))
    # a gentle tilt of the natural frequency across the field (0.02 Hz over 100 m): in unison the whole field still
    # breathes as one long, slow wave instead of a blank sheet, and the pacemakers' rings ride on it
    s.field("tilt", {"kind": "linearGradient", "axis": [0.8, 0.0, 0.6], "length": 100.0, "strength": 0.02})
    names.append("tilt")
    s.field("tune", {"kind": "compound", "combine": "add", "children": names[:4]})
    return "tune"


def phase_program(pal, name, prefix=""):
    g = common.Prog(name)
    g.op("phase", "field", 0, field=prefix + "phase")                    # r0 = sin theta
    g.op(None, "smoothstep", 1, a=0, k=[-0.7, 0.9, 0, 0])                # crest mask
    g.op("trough", "constant", 2, k=C(pal, "trough") + [1])
    g.op("crest", "constant", 3, k=C(pal, "crest") + [1])
    g.op(None, "mixBy", 4, a=2, b=3, c=1)                                # r4 = albedo
    g.op("activity", "field", 5, field=prefix + "activity")              # r5 = activity
    g.op(None, "smoothstep", 5, a=5, k=[0.8, 3.0, 0, 0])               # only real strain: defects, kicks, cusps
    g.op("accent", "constant", 6, k=C(pal, "accent", pal["accentGain"]) + [1])
    g.op(None, "multiply", 6, a=5, b=6)                                  # r6 = front light
    g.op("order", "field", 5, field=prefix + "order")                    # r5 = R
    g.op(None, "remap", 5, a=5, k=[0.85, 0.2, 0.0, 1.0], f=1.0)          # 0 when in step, 1 at a defect
    g.op(None, "power", 5, a=5, f=3.0)
    g.op("defect", "constant", 7, k=C(pal, "defect", pal["defectGain"]) + [1])
    g.op(None, "multiply", 5, a=5, b=7)
    g.op(None, "add", 6, a=6, b=5)                                       # r6 = emission
    g.op("rough", "constant", 7, k=[pal["rough"]] * 3 + [1])
    g.base, g.emit, g.rough = 4, 6, 7
    return g


def environment(s, pal, key_dir, fog=0.0):
    s.environment = {
        "intensity": 0.06, "background": C(pal, "sky"), "fogColor": C(pal, "horizon"),
        "volumeDensity": fog, "fogSky": 1.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 120.0, "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C(pal, "sky"), "horizonColor": C(pal, "horizon"),
                "groundColor": C(pal, "sky"), "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.light("key", "directional", direction=key_dir, color=C(pal, "key"), intensity=pal["keyI"], castsShadow=True,
            contactShadow=False, softness=0.3)
    s.light("rim", "directional", direction=[0.5, -0.35, 0.8], color=C(pal, "rim"), intensity=pal["rimI"],
            castsShadow=False, contactShadow=False)


def common_palette(name):
    return dict(PALETTES[name], name=name)


DESIGNS = {
    "flaps": ("Phase Wall",
              "A kinetic wall of nearly ten thousand metal flaps, each turned by its own oscillator: when they fall into step "
              "the whole wall turns and flashes as one; as the coupling loosens it breaks into islands that turn "
              "against each other, and travelling waves sweep bands of light across it."),
    "membrane": ("Phase Membrane",
                 "An amphitheatre whose floor is a living membrane of coupled oscillators: a few pacemakers send "
                 "concentric waves across it, the waves meet in cusps and annihilate, and wherever neighbours "
                 "disagree the surface lights."),
}


def design(key):
    return {"category": "experiment", "thesis": DESIGNS[key][1], "experiment": "A coupled oscillators"}


MIDI_CC = [
    (1, "coupling", "COUPLING: pulls the network toward unison (more K)"),
    (2, "complexity", "COMPLEXITY: widens the spread of natural frequencies (more islands, more defects)"),
    (3, "motion", "MOTION INTENSITY: the relief's depth and the network's speed"),
    (4, "influence", "AUDIO INFLUENCE: how hard the music drives the network (half at rest)"),
    (5, "persistence", "PERSISTENCE: how long strain and coherence linger"),
    (6, "mode", "VISUAL MODE: the Sakaguchi lag, from rings (0) toward spirals (full)"),
]
MIDI_NOTES = [(36, "pace0"), (37, "pace1"), (38, "pace2")]


def music(s, grid, centre, relief_target=None, kick_rate=2.5, pace=True):
    """Phase 5. The music changes the network's dynamics, not an object's size:
      - onsets are phase kicks: a kick launches a ring of advanced phase from `centre`, a snare a faster, thinner one
        from elsewhere (onset fields, ADR-1116, as the grid's injection) -- a new front the coupling then carries;
      - sustained sound raises the coupling (held chords pull the field toward unison; a sparse passage lets it
        loosen into islands);
      - bass speeds the whole field (its natural frequency), hats add phase noise (fine-scale disorder);
      - a section's intensity slowly turns the lag (rings toward spirals): a build reorganises the field;
      - MIDI notes 36-38 hold pacemakers (velocity = how much faster; release = how long they linger)."""
    G = "grid/%s/" % grid
    s.field("kickFront", {"kind": "onset", "onsetSource": "low", "point": list(centre), "waveGeometry": "spherical",
                          "audioSpeed": 16.0, "onsetWidth": 3.0, "onsetDecay": 1.0})
    s.field("snareFront", {"kind": "onset", "onsetSource": "mid", "point": [centre[0] + 26.0, centre[1],
                                                                             centre[2] - 14.0],
                           "waveGeometry": "spherical", "audioSpeed": 24.0, "onsetWidth": 1.6, "onsetDecay": 1.8})
    s.field("kicks", {"kind": "compound", "combine": "add", "children": ["kickFront", "snareFront"]})
    for g in s.grids:
        if g["name"] == grid:
            g["injectField"] = "kicks"
            g["injectRate"] = kick_rate
    s.route(common.audio("response.sustain", G + "coupling", 6.0, attackMs=3000, decayMs=6000),
            common.audio("response.bass", G + "frequency", 0.25, attackMs=300, decayMs=1500),
            common.audio("response.intensity", G + "phaseLag", 0.35, attackMs=6000, decayMs=12000))
    if relief_target:
        s.route(common.audio("response.bass", relief_target, 0.6, attackMs=200, decayMs=900))
    common.instrument(s, MIDI_CC, MIDI_NOTES if pace else ())
    s.route(R("control.coupling", G + "coupling", 12.0, **common.CC),
            R("control.complexity", G + "spread", 0.2, **common.CC),
            R("control.motion", G + "speed", 1.5, **common.CC),
            R("control.persistence", G + "persistence", 3.0, **common.CC),
            R("control.mode", G + "phaseLag", 1.0, **common.CC))
    if relief_target:
        s.route(R("control.motion", relief_target, 1.2, **common.CC))
    if pace:
        for i in range(3):
            s.route(R("control.pace%d" % i, "field/pace%d/strength" % i, 0.35, attackMs=80, decayMs=2500))


def membrane(pal_name="bronze", regime="islands", autonomous=False):
    """A1, the phase-wave sculpture: sin theta raises the floor of a bowl; the fronts and the defects light."""
    pal = common_palette(pal_name)
    s = common.Scene("osc-membrane", DESIGNS["membrane"][0], design("membrane"))
    radius, height = 52.0, 22.0
    tune = pacemakers(s, [(-14.0, 0.0, -8.0), (12.0, 0.0, 6.0), (-2.0, 0.0, 24.0)], period=70.0)
    osc_grid(s, "net", 2 * radius, 384, regime, freq_field=tune, extra={"noise": 0.02})
    readers(s, "net")
    mesh_dir = os.path.join(common.EXAMPLES, "osc-membrane", "meshes")
    pos, nrm, uv, idx = common.bowl_mesh(radius, height, 220, 360)
    common.write_glb(os.path.join(mesh_dir, "osc-bowl.glb"), pos, nrm, uv, idx, "bowl")
    g = phase_program(pal, "oscMembrane")
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    s.proc("membrane", {"kind": "mesh", "asset": "meshes/osc-bowl.glb"},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"],
                     "metallic": pal["metal"], "doubleSided": True},
           deformers=[{"kind": "field", "field": "phase", "amount": 1.1, "space": "world", "alongNormal": True}],
           extra={"normalStep": 0.1})
    if not autonomous:
        music(s, "net", (-6.0, 0.0, 4.0), relief_target="procedural/membrane/deform/1/amount")
    environment(s, pal, key_dir=[0.7, -0.45, -0.55])
    s.camera = {"mode": 1, "position": [6.0, 22.0, 32.0], "target": [-2.0, 0.0, -8.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(4.0, 1.0, 2.0), tamp=(2.0, 0.5, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 24.0, "post/bloom/intensity": 0.2, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.08})
    if autonomous:
        s.response = dict(common.RESPONSE)
    return s


def flaps(pal_name="bronze", regime="islands", autonomous=False):
    """A2, the synchronisation sculpture: a kinetic wall of 9,600 metal flaps, each turned about its vertical axis by
    its oscillator's phase. In step, the wall turns as one and flashes; in islands, patches turn together and
    disagree at their seams; travelling waves sweep bands of light across it. Autonomously the coupling breathes
    (a slow timeline sweep) so the wall passes through disorder, islands, synchrony and back."""
    pal = common_palette(pal_name)
    s = common.Scene("osc-flaps", DESIGNS["flaps"][0], design("flaps"))
    W, H, nx, ny = 150.0, 64.0, 150, 64
    osc_grid(s, "net", (W, H), (300, 128), regime, seed=3, frequency=0.18, extra={"noise": 0.05})
    # the readers turn the grid's XZ plane up into the wall's XY plane (world y = grid z), centred on the wall
    readers(s, "net", position=(0.0, H / 2, 0.0), rotation=(-90.0, 0.0, 0.0))
    g = phase_program(pal, "oscFlaps")
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    sp = W / nx
    s.proc("flaps", {"kind": "box", "size": [0.86 * sp, 0.86 * sp, 0.035]},
           distribution={"kind": "grid", "gridCount": [nx, ny, 1], "gridSpacing": [sp, sp, 1.0]},
           position=(0.0, H / 2, 0.0),
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"], "metallic": pal["metal"]},
           extra={"effectors": [{"field": "phase", "op": "rotation", "blend": "add", "strength": 0.9,
                                 "axis": [0.0, 1.0, 0.0]}]})
    # the coupling breathes: disorder (K 1.5) -> islands -> unison (K 14) and back, over 96 s
    s.track("grid/net/coupling", [{"time": 0.0, "value": [1.5], "interp": "smooth"},
                                  {"time": 40.0, "value": [14.0], "interp": "smooth"},
                                  {"time": 56.0, "value": [14.0], "interp": "smooth"},
                                  {"time": 96.0, "value": [1.5], "interp": "smooth"}], loop=96.0)
    if not autonomous:
        # in the grid's frame (x, z) = the wall's (x, y - H/2): the kick from the lower centre
        music(s, "net", (0.0, 0.0, -18.0), pace=False, kick_rate=2.0)
    environment(s, pal, key_dir=[0.35, -0.4, -0.85])
    s.environment["shadowRange"] = 160.0
    # inside the wall's span, close and looking up along it: the wall fills the frame and recedes to the right
    s.camera = {"mode": 1, "position": [-30.0, 14.0, 30.0], "target": [10.0, 34.0, 0.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=110.0, amp=(5.0, 1.5, 2.0), tamp=(3.0, 1.0, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 24.0, "post/bloom/intensity": 0.22, "post/output/vignette": 0.4,
               "post/grade/contrast": 1.08})
    if autonomous:
        s.response = dict(common.RESPONSE)
    return s


# The loom (two networks on nine ribbons, A3/A4) was tried and dropped after review: see A-oscillators.md.
CONFIGS = {"membrane": membrane, "flaps": flaps}


INDEX = {  # config -> (hidden?, notes): what the Examples > Live > Experiments menu lists
    "membrane": (False, "MIDI notes 36-38 hold pacemakers."),
    "flaps": (False, ""),
}


def register(s, name):
    hidden, notes = INDEX[name]
    text = ("Play live audio (any mix) and, optionally, a MIDI controller: " + common.describe_ccs(MIDI_CC, notes) +
            ". " + s.design["thesis"] + " An R&D experiment: see docs/prototypes/generative-rd/A-oscillators.md.")
    common.index_entry(s.title, "experiments/%s/%s.json" % (s.id, s.id), text, live=True, hidden=hidden)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=None)
    ap.add_argument("--palette", default=None)
    ap.add_argument("--regime", default=None)
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
        if a.regime:
            kw["regime"] = a.regime
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
