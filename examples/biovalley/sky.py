"""The night sky as a generated HDR environment (equirectangular, Radiance .hdr), built from noise, not painted
and not taken from the reference (spec §38): an indigo gradient, a violet-to-rose glow low over the far end of the
valley, banks of cloud lit silver near the moon and rose near the glow, a faint galactic band and dust.

Mapping (shaders/environment.wgsl `equirectUv`): u = 0.5 + atan2(z, x) / 2pi, v = acos(y) / pi, row 0 = zenith.
The moon itself is a mesh (sharp at any resolution); the map carries only its halo, so clouds near it are lit.
Point stars are the engine's `stars` effect, not texels.
"""
from __future__ import annotations

import math

import numpy as np

from geom import value_noise2


def write_hdr(path, img):
    """Write a float32 (h, w, 3) image as Radiance RGBE (uncompressed scanlines)."""
    img = np.maximum(np.asarray(img, np.float32), 0.0)
    h, w, _ = img.shape
    m = img.max(-1)
    e = np.where(m > 1e-32, np.floor(np.log2(np.maximum(m, 1e-32))) + 1, 0)
    scale = np.where(m > 1e-32, np.ldexp(1.0, -e.astype(np.int32)) * 256.0, 0.0)
    rgbe = np.zeros((h, w, 4), np.uint8)
    rgbe[..., :3] = np.clip(img * scale[..., None], 0, 255).astype(np.uint8)
    rgbe[..., 3] = np.where(m > 1e-32, e + 128, 0).astype(np.uint8)
    with open(path, "wb") as f:
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
        f.write(f"-Y {h} +X {w}\n".encode())
        f.write(rgbe.tobytes())


def fbm(x, z, seed, octaves, lac=2.0, gain=0.5):
    s, a, f, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        s = s + a * value_noise2(x * f + o * 13.1, z * f - o * 7.7, seed + o * 31)
        norm += a
        a *= gain
        f *= lac
    return s / norm


def build(path, moon_dir, glow_dir, width=4096, seed=11):
    h = width // 2
    v = (np.arange(h) + 0.5) / h
    u = (np.arange(width) + 0.5) / width
    theta = v * math.pi
    phi = (u - 0.5) * 2 * math.pi
    T, Ph = np.meshgrid(theta, phi, indexing="ij")
    d = np.stack([np.sin(T) * np.cos(Ph), np.cos(T), np.sin(T) * np.sin(Ph)], -1)
    y = d[..., 1]
    elev = np.arcsin(np.clip(y, -1, 1))
    moon_dir = np.asarray(moon_dir, float) / np.linalg.norm(moon_dir)
    glow_dir = np.asarray(glow_dir, float) / np.linalg.norm(glow_dir)

    # the gradient: indigo zenith, blue-violet toward the horizon
    zen = np.array([0.003, 0.008, 0.06])
    hor = np.array([0.014, 0.026, 0.115])
    t = np.clip(1.0 - np.maximum(elev, 0) / (math.pi / 2), 0, 1) ** 2.4
    sky = zen + (hor - zen) * t[..., None]

    # the glow low over the far valley: violet that warms to rose at the horizon, wide in azimuth
    gaz = np.einsum("ijk,k->ij", d * np.array([1, 0, 1]), glow_dir * np.array([1, 0, 1]))
    gaz = gaz / np.maximum(np.linalg.norm(d[..., [0, 2]], axis=-1), 1e-6) / np.linalg.norm(glow_dir[[0, 2]])
    lobe = np.exp(-((1.0 - gaz) / 0.22) ** 1.2)
    low = np.exp(-np.maximum(elev, 0) / 0.16)
    band = np.exp(-((np.maximum(elev, 0) - 0.07) / 0.07) ** 2)      # a raised band, clear of the far hills
    violet = np.array([0.03, 0.03, 0.13])
    rose = np.array([0.48, 0.16, 0.18])
    sky += lobe[..., None] * (low[..., None] * violet + band[..., None] * rose)

    # the moon's halo
    cm = np.einsum("ijk,k->ij", d, moon_dir)
    ang = np.arccos(np.clip(cm, -1, 1))
    halo = 0.05 * np.exp(-ang / 0.10) + 0.012 * np.exp(-ang / 0.45)
    sky += halo[..., None] * np.array([0.45, 0.60, 1.0])

    # a faint galactic band and dust (tilted great circle)
    gal_n = np.array([0.35, 0.55, -0.76])
    gal_n /= np.linalg.norm(gal_n)
    gd = np.abs(np.einsum("ijk,k->ij", d, gal_n))
    gx = Ph * 6.0
    dust = fbm(gx, T * 12.0, seed + 40, 5)
    gal = np.exp(-(gd / 0.12) ** 2) * (0.4 + 0.6 * dust) * np.clip(elev / 0.3, 0, 1)
    sky += gal[..., None] * np.array([0.010, 0.010, 0.022])

    # clouds: a layer projected onto a plane (perspective toward the horizon), banked in drifts
    above = y > 0.0
    k = 1.0 / np.maximum(y + 0.06, 0.06)
    px, pz = d[..., 0] * k, d[..., 2] * k
    warp = fbm(px * 0.35, pz * 0.35, seed + 3, 3)
    c = fbm(px * 0.9 + 1.7 * warp, pz * 0.9 - 1.3 * warp, seed + 7, 6)
    banks = fbm(px * 0.22, pz * 0.22, seed + 9, 3)
    dens = np.clip((c - 0.46 + 0.35 * (banks - 0.5)) / 0.16, 0, 1)
    dens *= np.clip((elev - 0.015) / 0.05, 0, 1) * np.clip(1.0 - (elev - 0.55) / 0.5, 0, 1)
    dens *= above
    # shading: dark bodies, lit edges toward the moon and over the glow
    edge = np.clip(1.0 - np.abs(dens - 0.45) / 0.45, 0, 1) ** 2
    near_moon = np.exp(-ang / 0.38)
    body = np.array([0.006, 0.011, 0.046]) + 0.6 * lobe[..., None] * low[..., None] * np.array([0.06, 0.03, 0.08])
    lit = (near_moon[..., None] * np.array([0.09, 0.13, 0.30]) * (0.35 + edge[..., None]) +
           (lobe * low)[..., None] * np.array([0.12, 0.05, 0.12]) * (0.4 + edge[..., None]))
    cloud = body + lit
    sky = sky * (1 - dens[..., None] * 0.9) + cloud * dens[..., None]

    # below the horizon: the horizon colour, darker (the ground hides it; the fog map reads it)
    below = elev < 0
    sky[below] = (hor * 0.6 + violet * 0.3)[None, :] * np.ones((below.sum(), 1))
    write_hdr(path, sky)
    return sky
