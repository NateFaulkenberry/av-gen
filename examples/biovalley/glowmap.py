"""CP4: where the light pools. A glow map is a static, authored grid (ADR-1282) holding how much living light is
near each place: every glowing organism's host splats its weight into it, and the result is blurred so a cluster
becomes a soft pool. The map is read by:

  the mist     the volumetric medium's emission colour (`volumeColorField`): ground mist lit from within over
               glowing ground, dark over the dark forest (spec §15's "illuminated pockets")
  surfaces     the ground, rock, crown and moss programs add a faint pool of the same light (spec §11)

Three maps at three scales: the whole valley floor (for the vista), the riverbank and macro, and the meadow.
"""
from __future__ import annotations

import numpy as np

import land

RES = 128


def _blur(a, passes=2):
    for _ in range(passes):
        a = (a + np.roll(a, 1, 0) + np.roll(a, -1, 0) + np.roll(a, 1, 1) + np.roll(a, -1, 1)) / 5.0
    return a


def build(name, bounds, weighted, blur=3, gain=1.0, floor=0.0, power=0.8):
    """`weighted`: list of (records, weight). Returns (grid JSON, field name)."""
    x0, x1, z0, z1 = bounds
    a = np.zeros((RES, RES))          # [k (z), i (x)]
    for recs, w in weighted:
        if not recs:
            continue
        P = np.array([[r[0], r[2]] for r in recs])
        i = np.floor((P[:, 0] - x0) / (x1 - x0) * RES).astype(int)
        k = np.floor((P[:, 1] - z0) / (z1 - z0) * RES).astype(int)
        ok = (i >= 0) & (i < RES) & (k >= 0) & (k < RES)
        np.add.at(a, (k[ok], i[ok]), w)
    a = _blur(a, blur)
    ref = np.percentile(a[a > 0], 97) if np.any(a > 0) else 1.0
    a = np.clip(a / max(ref, 1e-9), 0, 1.6) ** power * gain + floor
    grid = {"name": name, "mode": "scalar", "resolution": [RES, 1, RES],
            "boundsMin": [x0, -200.0, z0], "boundsMax": [x1, 1200.0, z1], "diffusion": 0.0, "dissipation": 0.0,
            "injectRate": 0.0, "seedAmount": 0.0, "initial": [round(float(v), 4) for v in a.ravel()]}
    return grid


def field(name, grid, color, strength=1.0):
    return {"name": name, "kind": "field", "position": [0, 0, 0],
            "field": {"kind": "grid", "reference": grid, "channel": 0, "strength": strength,
                      "colorA": [0.0, 0.0, 0.0, 1.0], "colorB": [*color, 1.0], "falloff": {"kind": "none"}}}


def maps(carpet, groves_glow, eyes):
    """The three glow maps and their fields, and a compound of all three (the mist's colour, the programs' pool)."""
    teal = [0.18, 0.55, 1.0]     # the mist's light: blue, as the reference's lit haze

    def flat(d):
        return [r for v in d.values() for r in v]

    glade, tuft, moss = flat(carpet["glade"]), flat(carpet["tuft"]), flat(carpet["moss"])
    f3d = flat(carpet["f3d_small"]) + flat(carpet["f3d_bank"]) + flat(carpet["f3d_hero"])
    under = flat(groves_glow)
    grids = [
        build("glowFloor", (-900.0, 900.0, -1500.0, 4400.0), [(glade, 1.0), (under, 4.0), (tuft, 0.3)], blur=2),
        build("glowBank", (land.BANK_X0 - 60.0, land.BANK_X0 + 260.0, land.BANK_Z - 130.0, land.BANK_Z + 130.0),
              [(tuft, 1.0), (moss, 0.5), (f3d, 2.0)], blur=3, power=2.2),
        build("glowMeadow", (land.MEADOW_X - 220.0, land.MEADOW_X + 220.0, land.MEADOW_Z - 120.0,
                             land.MEADOW_Z + 380.0), [(tuft, 1.0), (moss, 0.3)], blur=3, power=2.2, gain=0.45),
    ]
    fields = [field("glowFloorF", "glowFloor", teal), field("glowBankF", "glowBank", teal),
              field("glowMeadowF", "glowMeadow", teal),
              # the mist glows only low down: a height mask (1 below 25 m above the datum, 0 above 110 m)
              {"name": "lowAir", "kind": "field", "position": [0, 25.0, 0],
               "field": {"kind": "plane", "axis": [0, 1, 0], "softness": 85.0, "invert": True, "strength": 1.0,
                         "colorA": [0.0, 0.0, 0.0, 1.0], "colorB": [1.0, 1.0, 1.0, 1.0], "falloff": {"kind": "none"}}},
              *[{"name": f"mist{k}", "kind": "field", "position": [0, 0, 0],
                 "field": {"kind": "compound", "children": [src, "lowAir"], "combine": "multiply", "strength": 1.0,
                           "falloff": {"kind": "none"}}}
                for k, src in (("Floor", "glowFloorF"), ("Bank", "glowBankF"), ("Meadow", "glowMeadowF"))],
              # brightness clumps for the emitters (wakeField): some drifts brighter, most dimmer
              {"name": "clumps", "kind": "field", "position": [0, 0, 0],
               "field": {"kind": "noise", "frequency": 0.035, "seed": 61, "strength": 1.0,
                         "falloff": {"kind": "none"}}}]
    return grids, fields
