"""Scene support 3b, the RIVERROCKS: three riverbank boulders with moss and glowing lichen.

Each is a lumpy, water-worn boulder: rounded below, with chunkier, fractured planes on top, sitting a little sunk
into the ground. A dark wet band runs along the waterline, with a glossy finish there. Velvet moss caps the top,
following the upward-facing surface and creeping down the cracks. Ring lichens grow in clustered rosettes on the
flanks, their rims glowing faint emerald and cyan. A few brighter specks mark where the light gathers.

All painted detail is driven by the boulder's own 3D surface (worley3 / triplanar noise), so it does not stretch at
the poles of the UV sphere.

Variants: riverrock_a (1.4 m wide, low and long), riverrock_b (0.9 m, taller), riverrock_c (0.55 m, rounded).
Units metres, Z up in Blender (Y up exported), origin at the base centre on the ground.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, Asset, col, fbm, lerp, smoothstep, worley3)

TAU = 2 * math.pi

VARIANTS = {
    "riverrock_a": dict(seed=101, R=1.0, scale=(1.0, 0.66, 0.6), chunk=0.12),   # 2 m, a long low slab
    "riverrock_b": dict(seed=102, R=0.6, scale=(0.9, 0.8, 0.9), chunk=0.16),    # 1.1 m, tall and blocky
    "riverrock_c": dict(seed=103, R=0.36, scale=(1.0, 0.85, 0.75), chunk=0.10), # 0.7 m
    "riverrock_d": dict(seed=104, R=0.16, scale=(1.0, 0.8, 0.7), chunk=0.08),   # 0.3 m, a stone
}


def noise3(x, y, z, scale, seed, octaves=4):
    """Cheap 3D noise: fbm on the three axis planes, averaged."""
    return (fbm(x * scale, y * scale, octaves, seed) + fbm(y * scale + 3.1, z * scale, octaves, seed + 1)
            + fbm(z * scale + 7.7, x * scale, octaves, seed + 2)) / 3.0


class Rock:
    def __init__(self, name, seed, R, scale, chunk, lod=0):
        self.name, self.seed, self.R, self.scale, self.chunk = name, seed, R, np.array(scale), chunk
        self.lod = lod
        self.lod0 = lod == 0

    def dirs(self, S, T):
        th = -TAU * S
        ph = -math.pi / 2 + math.pi * T
        return np.cos(ph) * np.cos(th), np.cos(ph) * np.sin(th), np.sin(ph)

    def radius(self, dx, dy, dz):
        sd = self.seed
        n1 = noise3(dx, dy, dz, 1.3, sd, 3)
        n15 = noise3(dx, dy, dz, 2.6, sd + 20, 2)
        n2 = noise3(dx, dy, dz, 4.0, sd + 10, 3)
        # fractured planes on the upper half: a few large flat cuts
        cut = 1.0
        rng = np.random.default_rng(sd)
        for _ in range(8):
            nrm = rng.normal(size=3)
            nrm[2] = abs(nrm[2]) * 0.8 + 0.1
            nrm /= np.linalg.norm(nrm)
            off = rng.uniform(0.72, 0.9)
            proj = dx * nrm[0] + dy * nrm[1] + dz * nrm[2]
            cut = np.minimum(cut, np.where(proj > 0, off / np.maximum(proj, 1e-3), 10.0))
        crack = self.cracks(dx, dy, dz)
        # crags: sharp ridged relief (the creases of a mid-frequency noise, stepped into ledges)
        rn = noise3(dx, dy, dz, 4.0, sd + 40, 3)
        crag = (1 - np.abs(2 * rn - 1)) ** 4
        ledge = np.floor((n15 * 6.0)) / 6.0
        r = (1.0 + 0.45 * (n1 - 0.5) + 0.10 * (n15 - 0.5) + 0.08 * (ledge - 0.5) + 0.06 * (n2 - 0.5)
             + 0.14 * crag - 0.07 * crack)
        up = smoothstep(-0.3, 0.2, dz)
        r = np.minimum(r, r * (1 - up) + cut * up * (1 + self.chunk * (n2 - 0.5)))
        return r

    def cracks(self, dx, dy, dz):
        """Fissures: the creases of a ridged 3D noise, sharpened. 1 in a crack."""
        n = noise3(dx, dy, dz, 2.2, self.seed + 30, 3)
        return smoothstep(0.86, 0.97, 1 - np.abs(2 * n - 1))

    def position(self, S, T):
        dx, dy, dz = self.dirs(S, T)
        r = self.radius(dx, dy, dz)
        x = dx * r * self.scale[0] * self.R
        y = dy * r * self.scale[1] * self.R
        z = dz * r * self.scale[2] * self.R
        # sink and flatten the bottom into the ground
        z = np.where(z < 0, z * 0.25, z) - 0.06 * self.R
        return x, y, z

    def build(self):
        A = Asset(self.name)
        self.A = A
        res = 1.0 if self.lod0 else 0.5
        sl = A.slot("rock", res=(int(2048 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        nth, nph = (220, 110) if self.lod0 else (44, 22)
        s = np.linspace(0, 1, nth, endpoint=False)
        t = np.linspace(0, 1, nph)
        S, T = np.meshgrid(s, t)
        x, y, z = self.position(S, T)
        P = np.stack([x, y, z], -1)
        # rows rise from the bottom pole, u runs clockwise: that faces inward, so flip u
        sl.add_grid(P[:, ::-1], sl.region(0, 1, self.paint, bump=6.0), wrap_u=True)
        # glints on the brightest lichen
        rng = np.random.default_rng(self.seed + 5)
        for _ in range(int(14 * self.R / 0.45)):
            ss, tt = rng.uniform(0, 1), rng.uniform(0.45, 0.85)
            px, py, pz = self.position(np.array(ss), np.array(tt))
            A.emitters.append([float(px), float(py), float(pz), tt, ss, 0.004])
        return A

    def paint(self, S, T):
        sd = self.seed + 50
        S = 1 - S  # the grid's u was flipped
        dx, dy, dz = self.dirs(S, T)
        x, y, z = self.position(S, T)
        # surface position in metres for the detail
        mott = noise3(x, y, z, 6.0, sd, 4)
        grain = noise3(x, y, z, 40.0, sd + 1, 2)
        strata = 0.5 + 0.5 * np.sin(z * 28 + 6 * noise3(x, y, z, 2.0, sd + 2, 2))
        height_norm = (z + 0.06 * self.R) / (self.scale[2] * self.R * 1.2)
        # moss: on what faces up, creeping down in tongues
        crack = self.cracks(dx, dy, dz)
        moss = smoothstep(0.55, 0.8, dz + 0.35 * (noise3(x, y, z, 5.0, sd + 3, 3) - 0.5) * 2)
        moss = np.maximum(moss, crack * smoothstep(-0.4, 0.2, dz))  # moss fills the crevices
        mossn = noise3(x, y, z, 60.0, sd + 4, 2)
        # the wet band at the waterline
        wet = 1 - smoothstep(0.08, 0.22, height_norm)
        # ring lichens: rosettes in clusters on the flanks
        zone = smoothstep(0.55, 0.75, noise3(x, y, z, 3.0, sd + 5, 3)) * (1 - moss) * (1 - wet)
        F1, h = worley3(x * 30, y * 30, z * 30, sd + 6, jitter=0.9)
        rad = 0.18 + 0.2 * h
        ring = smoothstep(rad - 0.06, rad - 0.02, F1) * (1 - smoothstep(rad, rad + 0.04, F1))
        disc = 1 - smoothstep(rad - 0.04, rad, F1)
        lichen = (h < 0.55) * zone
        speck = (1 - smoothstep(0.0, 0.08, F1)) * (h > 0.93) * zone
        rock = lerp(np.array([0.03, 0.032, 0.038]), np.array([0.085, 0.085, 0.095]), 0.5 * mott + 0.3 * strata)
        rock = rock * (0.8 + 0.4 * grain)[..., None] * (1 - 0.55 * wet)[..., None] * (1 - 0.6 * crack)[..., None]
        mosscol = lerp(np.array([0.015, 0.05, 0.022]), np.array([0.045, 0.12, 0.055]), mossn)
        base = rock * (1 - moss)[..., None] + mosscol * moss[..., None]
        base = base + col([0.04, 0.06, 0.055], disc * lichen * 0.6)
        hue = lerp(EMERALD, CYAN, h)
        G1, gh = worley3(x * 90, y * 90, z * 90, sd + 7, jitter=0.95)
        mdots = (1 - smoothstep(0.08, 0.18, G1)) * (gh < 0.3) * moss * smoothstep(0.5, 0.7, noise3(x, y, z, 4.0, sd + 8, 2))
        # light lives only in the moss: a faint speckle of tiny spores
        e = col(lerp(EMERALD, CYAN, np.clip(gh * 3, 0, 1)), mdots * 0.5)
        base = base + col([0.03, 0.05, 0.04], ring * lichen * 0.5)  # pale lichen rings, unlit
        height = 0.4 * mott + 0.25 * grain + 0.25 * strata + 0.4 * moss * mossn + 0.3 * disc * lichen - 0.6 * crack
        rough = np.clip(0.85 - 0.6 * wet + 0.08 * moss, 0.15, 1.0)
        return dict(base=base, emis=e, height=height, rough=rough, ao=0.55 + 0.45 * smoothstep(0.0, 0.6, height_norm))


def make_variant(name):
    def make(lod=0):
        return Rock(name, lod=lod, **VARIANTS[name]).build()
    return make


make_a = make_variant("riverrock_a")
make_b = make_variant("riverrock_b")
make_c = make_variant("riverrock_c")
make_d = make_variant("riverrock_d")
