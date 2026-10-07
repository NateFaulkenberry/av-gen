"""Checkpoint 2: the Rift with its micro-scale light moved to the ecosystem (ADR-1200).

Same world, organisms, cameras and audio fields as CP1 (cp1.py). The difference:
  - photophores, polyps, chain beads and whip tips are EMITTER LAYERS on the bodies' instances (points accumulated
    in compute), not tessellated beads;
  - two layers only the point representation can afford: a wall crust (600 micro-organisms per 3 m patch) and
    river plankton (900 per 8 m patch);
  - `--lights pools` keeps CP1's 139 light pools; `--lights none` drops them (the glow field replaces them later).

  python3 examples/bioluminescent/cp2.py [--lights pools|none]
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp1  # noqa: E402
import land  # noqa: E402
import organisms  # noqa: E402

OUT = HERE / "cp2"
EMITTER_PARTS = ("crinoidBeads", "crinoidChains", "matPolyps", "fanPolyps", "whipsTips")


def hosts_for(sc, prefix):
    return [n["name"] for n in sc["nodes"] if n["name"].startswith(prefix) and n["kind"] == "procedural"]


def invisible_host(name, pts):
    return {"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": [0.01, 0.01, 0.01]},
        "distribution": {"kind": "points", "points": pts}, "castsShadow": False, "visible": False,
        "lod": {"cull": True, "count": 1}, "material": {"baseColor": [0, 0, 0]}}}


def crust_points(g):
    """Wall crust patches: on the walls (steep), oriented to the rock, clumped along ledges."""
    X, Z = cp1.candidates(g, 0.05, 801)
    sl = g.slope(X, Z)
    d = np.abs(X - np.array([land.centre_x(z) for z in Z]))
    cl = cp1.value_noise(X, Z, 18.0, 13) * 0.6 + cp1.value_noise(X, Z, 5.0, 14) * 0.4
    keep = (sl > 0.6) & (d > 15) & (d < 140) & (cl > 0.45)
    X, Z = X[keep], Z[keep]
    return cp1.records(g, X, Z, np.random.default_rng(802).uniform(0.7, 1.6, len(X)), 0.95, 803, sink=0.0)


def plankton_points(g):
    r = np.random.default_rng(901)
    out = []
    for z in np.arange(cp1.REGION[2], cp1.REGION[3], 5.0):
        for k in range(3):
            zz = z + r.uniform(-2, 2)
            x = land.river_x(zz) + r.uniform(-6, 6)
            wl = float(g.water_level(np.array([x]), np.array([zz]))[0])
            gh = float(g.height(np.array([x]), np.array([zz]))[0])
            if wl > gh + 0.1:
                a = r.uniform(0, 6.28)
                out.append([round(x, 3), round(wl, 3), round(zz, 3), 0, math.sin(a / 2), 0, math.cos(a / 2),
                            1, 1, 1])
    return out


def layer(name, hosts, template, color, intensity, excited, field="", gain=1.0, excited_color=None, **kw):
    d = {"name": name, "hosts": hosts, "template": f"../meshes/{template}.emit.json", "color": color,
         "excitedColor": excited_color or color, "intensity": intensity, "excitedIntensity": excited,
         "responseField": field, "responseGain": gain}
    d.update(kw)
    return d


def ecosystem_scene(sc, P, W, g):
    """CP1's scene with the emitting mesh parts replaced by ecosystem layers."""
    sc = copy.deepcopy(sc)
    sc["nodes"] = [n for n in sc["nodes"] if not n["name"].startswith(EMITTER_PARTS)]
    # the sea pen's body is dark now: its light comes from its leaf polyps
    for n in sc["nodes"]:
        if n["name"].startswith("seapen"):
            mat = n["procedural"]["material"]
            mat["emissiveIntensity"] = 0.06 * W
            n["procedural"].pop("effectors", None)
    sc["nodes"].append(invisible_host("crustHost", crust_points(g)))
    sc["nodes"].append(invisible_host("planktonHost", plankton_points(g)))
    mats = hosts_for(sc, "matCushion")
    pens = hosts_for(sc, "seapen")
    sc["ecosystem"] = {"spriteRadius": 1.5, "maxSprites": 65536, "layers": [
        layer("crinoidBeads", ["crinoidStalk"], "crinoid_beads", cp1.CYAN, 9.0 * W, 6.0 * W, "kick",
              sparsity=0.25, flicker=0.15, flickerRate=2.0, breath=0.35, breathRate=0.05, maxDistance=900),
        layer("crinoidChains", ["crinoidStalk"], "crinoid_chains", cp1.PALE, 7.0 * W, 2.5 * W, "bands",
              breath=0.3, breathRate=0.11, maxDistance=900),
        layer("matPolyps", mats, "mat_polyps", cp1.BLUE, 7.0 * W, 6.0 * W, "kick", sparsity=0.35,
              pulseRate=1.5, pulseDecay=0.8, maxDistance=320),
        layer("fanPolyps", ["fanBody"], "fan_polyps", cp1.MAGENTA, 6.0 * W, 2.5 * W, "bands", maxDistance=700),
        layer("whipsTips", ["whipsBody"], "whips_tips", cp1.PALE, 12.0 * W, 2.5 * W, "bands", sparsity=0.3,
              flicker=0.5, flickerRate=7.0, maxDistance=260),
        layer("seapenPolyps", pens, "seapen", cp1.TURQ, 3.0 * W, 6.0 * W, "kick", sparsity=0.15,
              pulseRate=0.8, pulseDecay=1.0, maxDistance=300),
        layer("crust", ["crustHost"], "crust", cp1.BLUE, 2.0 * W, 6.0 * W, "kick", sparsity=0.5,
              pulseRate=2.0, pulseDecay=1.5, breath=0.4, breathRate=0.03, maxDistance=600),
        layer("plankton", ["planktonHost"], "plankton", cp1.BLUE, 1.5 * W, 8.0 * W, "kick", sparsity=0.6,
              pulseRate=4.0, pulseDecay=0.6, flicker=0.3, flickerRate=3.0, maxDistance=300),
    ]}
    return sc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lights", default="pools", choices=("pools", "none"))
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    organisms.build_all()
    g = land.Ground(land.world(), *cp1.REGION, step=2.0)
    P = cp1.place(g)
    routes = [{"source": s, "target": t, "op": "add", "amount": 0.0} for s, t in
              (("audio.bass", "root/scale"), ("audio.mid", "root/rotationSpeed"), ("audio.rms", "scene/brightness"),
               ("audio.onset", "root/impulse"))]
    post = {"post/bloom/enabled": True, "post/bloom/threshold": 0.9, "post/bloom/intensity": 0.32,
            "post/bloom/emissionWeight": 0.75, "post/tonemap/chroma-retention": 0.6, "post/output/vignette": 0.28,
            "post/grade/contrast": 1.08, "post/grade/saturation": 1.05}
    for name, (eye, tgt, fov, wake, t) in cp1.STILLS.items():
        sc = cp1.scene(P, wake=wake)
        sc["camera"] = {"mode": 1, "position": cp1.resolve(g, eye), "target": cp1.resolve(g, tgt), "fov": fov,
                        "orbitSpeed": 0}
        sc = ecosystem_scene(sc, P, wake, g)
        if args.lights == "none":
            sc["lights"] = sc["lights"][:1]
        sc["name"] = "The Rift (CP2, ecosystem emitters)"
        (OUT / f"{name}.scene.json").write_text(json.dumps(sc, separators=(",", ":")))
        proj = {"format": "avgen-project", "version": 4, "app": {"name": f"Rift CP2 {name}"},
                "assets": {"scene": {"kind": "composition", "path": f"{name}.scene.json"},
                           "audio": {"path": "../../../assets/audio/trench.wav"}},
                "parameters": dict(post), "routes": routes, "_t": t}
        (OUT / f"{name}.json").write_text(json.dumps(proj, indent=1))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
