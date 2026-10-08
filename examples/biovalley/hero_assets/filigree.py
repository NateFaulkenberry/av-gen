"""Archetype 2, the FILIGREE: an intricate branching organism of curved, tapering filaments.

A gnarled, dark holdfast rises into a leaning trunk that forks four times. Each generation is thinner, longer in
proportion and more curved than the last. A third of the last filaments roll into fiddlehead croziers; the rest end in a
small luminous bead. The tissue darkens toward the root and clears toward the tips. A sap channel runs along one side of
every filament and brightens outward, so light travels along the organism instead of sitting on it.

Units metres, Z up in Blender (Y up exported), origin at the base. Height ~0.26 m, spread ~0.3 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, MINT, TISSUE_DARK, Asset, add_branches, col, fbm, grow_branches, lerp, smoothstep)


class Filigree:
    def __init__(self, seed=23, lod=0):
        self.seed = seed
        self.lod = lod

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod == 0
        A = Asset("filigree")
        res = 1.0 if lod0 else 0.5
        sl = A.slot("filament", res=(int(1024 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        br = []
        nstem = 4 if lod0 else 3
        for k in range(nstem):
            ang = 2 * math.pi * k / nstem + rng.uniform(-0.4, 0.4)
            tilt = rng.uniform(0.15, 0.45)
            d = [math.sin(tilt) * math.cos(ang), math.sin(tilt) * math.sin(ang), math.cos(tilt)]
            br += grow_branches(rng, [0.004 * math.cos(ang), 0.004 * math.sin(ang), -0.004], d,
                                rng.uniform(0.055, 0.08), 0.0042, max_gen=5, children=(2, 3),
                                spread=(0.25, 0.6), shrink=0.8, rshrink=0.66, curl=0.28, up=0.22,
                                segs=12 if lod0 else 5, crozier=0.4, crozier_turns=1.2, min_r=0.0003,
                                child_t=(0.4, 1.0))
        if not lod0:  # the same organism, pruned: LOD1 keeps generations 0-3 and ends them in beads
            br = [b for b in br if b.gen <= 3]
            for b in br:
                b.tip = b.tip or b.gen == 3
        # luminous beads on the terminal filaments that do not coil
        for b in br:
            if b.tip:
                t = np.linspace(0, 1, len(b.radii))
                bead = smoothstep(0.86, 0.95, t) * (1 - smoothstep(0.96, 1.0, t)) ** 0.5
                b.radii = b.radii + 0.0009 * bead
                b.radii[-1] = 0.0002
                A.emitters.append(list(b.pts[-2]) + [1.0, 0.5, 0.0014])

        def sides(b):
            return max([12, 9, 7, 6, 5, 4][min(b.gen, 5)] - (0 if lod0 else 2), 3)

        def knot(th, t):  # the holdfast and trunk are gnarled; the rest stays round
            return 1 + 0.12 * (fbm(th[None, :] * 0.8, t[:, None] * 6, 2, self.seed + 3) - 0.5)
        reg = sl.region(0, 1, self.paint, bump=3.0)
        self.dmax = add_branches(sl, reg, br, sides, knot, s_width=0.25, seed=self.seed)
        return A

    def paint(self, S, T):
        sd = self.seed + 50
        up = smoothstep(0.15, 1.0, T)
        tip = smoothstep(0.80, 0.99, T)
        mott = fbm(S * 24, T * 60, 3, sd)
        # one sap channel per tube (each tube spans a quarter of u)
        chan = smoothstep(0.82, 1.0, 0.5 + 0.5 * np.sin(S * 2 * math.pi * 4 + 1.0))
        # faint annular growth scars, irregular
        scar = smoothstep(0.75, 0.95, fbm(S * 2, T * 140, 2, sd + 1))
        base = lerp(TISSUE_DARK, np.array([0.02, 0.05, 0.05]), up ** 2 * (0.6 + 0.4 * mott))
        base = base + col([0.05, 0.12, 0.10], tip)
        hue = lerp(EMERALD, CYAN, smoothstep(0.4, 1.0, T))
        e = (col(hue, chan * (0.005 + 0.35 * up ** 3) * (0.6 + 0.6 * mott))
             + col(lerp(CYAN, MINT, tip), tip ** 2 * 0.75)
             + col(hue, 0.001 + 0.012 * up ** 4))
        height = 0.4 * chan + 0.5 * scar + 0.4 * mott * (1 - up)
        rough = 0.75 - 0.4 * up
        return dict(base=base, emis=e, height=height, rough=rough, ao=0.5 + 0.5 * up)


def make(lod=0, seed=23):
    return Filigree(seed, lod).build()
