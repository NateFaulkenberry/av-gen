"""Hybrid 2.5D (the art pass's Approach C): background layers as alpha-cutout cards.

Far organic detail -- tree trunks with buttress roots, hanging strands, fern silhouettes -- costs a great deal as
geometry and is seen only as silhouette against the haze. Here it is painted (procedurally, deterministically; no
reference pixels) into RGBA textures and drawn on upright quads at a few depths, so the haze separates the layers
and the camera still gets parallax. The texture's alpha is the cutout (`alphaCutoff`), its RGB a dark tissue colour
with a faint cool rim the moon would give.
"""
from __future__ import annotations

import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from geom import Mesh


def _trunk(d, rnd, W, H, x0, wid):
    """A trunk with flaring buttress roots and a few limbs, as a filled silhouette."""
    pts_l, pts_r = [], []
    lean = rnd.uniform(-0.08, 0.08)
    for i in range(41):
        t = i / 40
        y = H - t * H * 1.02
        flare = 1.0 + 2.6 * max(0.0, 0.18 - t) / 0.18 * 0 + 2.4 * math.exp(-((1 - t) / 0.08))
        w = wid * (1.0 + 0.08 * math.sin(t * 9 + x0)) * flare
        x = x0 + lean * (H - y)
        pts_l.append((x - w / 2, y))
        pts_r.append((x + w / 2, y))
    d.polygon(pts_l + pts_r[::-1], fill=255)
    # limbs
    for k in range(rnd.randint(2, 4)):
        t = rnd.uniform(0.2, 0.6)
        y = H - t * H
        side = rnd.choice((-1, 1))
        L = rnd.uniform(0.15, 0.35) * W
        x = x0 + lean * (H - y)
        d.line([(x, y), (x + side * L * 0.5, y - L * 0.3), (x + side * L, y - L * 0.25)], fill=255,
               width=max(2, int(wid * 0.25)))


def _strands(d, rnd, W, H, n):
    for _ in range(n):
        x = rnd.uniform(0, W)
        y0 = rnd.uniform(0, H * 0.2)
        L = rnd.uniform(0.2, 0.6) * H
        xs = [x + 6 * math.sin(i * 0.3 + x) for i in range(20)]
        d.line([(xs[i], y0 + L * i / 19) for i in range(20)], fill=255, width=2)


def _ferns(d, rnd, W, H, n):
    for _ in range(n):
        bx = rnd.uniform(0, W)
        for f in range(rnd.randint(3, 6)):
            a = rnd.uniform(-1.2, 1.2)
            L = rnd.uniform(0.08, 0.2) * H
            pts = []
            for i in range(16):
                t = i / 15
                ang = -math.pi / 2 + a * t + 0.6 * a * t * t
                pts.append((bx + L * t * math.cos(ang) * 1.6, H - L * t * -math.sin(ang)))
            for i in range(1, 16):
                x, y = pts[i]
                w = (1 - i / 15) * L * 0.12
                d.ellipse([x - w, y - w * 0.5, x + w, y + w * 0.5], fill=255)
            d.line(pts, fill=255, width=3)


def _canopy(d, rnd, W, H, cover):
    """Masses of foliage hanging from the top of the card: lumpy blobs overlapping into a ragged lower edge."""
    for _ in range(int(60 * cover)):
        x = rnd.uniform(-0.05, 1.05) * W
        y = rnd.uniform(-0.1, 0.35 * cover) * H
        r = rnd.uniform(0.04, 0.12) * W
        d.ellipse([x - r, y - r * 0.8, x + r, y + r * 0.8], fill=255)


def card_texture(path, seed, W=1024, H=1024, trunks=3, strands=30, ferns=6, canopy=0.0, glints=0):
    rnd = __import__("random").Random(seed)
    a = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(a)
    if canopy > 0:
        _canopy(d, rnd, W, H, canopy)
    for k in range(trunks):
        _trunk(d, rnd, W, H, rnd.uniform(0.1, 0.9) * W, rnd.uniform(0.04, 0.09) * W)
    _strands(d, rnd, W, H, strands)
    _ferns(d, rnd, W, H, ferns)
    a = a.filter(ImageFilter.GaussianBlur(1.2))
    # colour: dark blue-black tissue, a faint cool rim where the silhouette's edge faces up-right
    edge = np.asarray(a.filter(ImageFilter.FIND_EDGES), float) / 255.0
    rgb = np.zeros((H, W, 3))
    rgb[...] = [6, 9, 18]
    rgb += edge[..., None] * np.array([30, 60, 110]) * 0.6
    # a few soft luminous specks inside the silhouettes (distant organisms; out of focus they read as bokeh)
    if glints:
        gl = Image.new("L", (W, H), 0)
        gd = ImageDraw.Draw(gl)
        A = np.asarray(a)
        for _ in range(glints):
            x, y = rnd.randrange(W), rnd.randrange(H)
            if A[y, x] > 128:
                r = rnd.uniform(2, 6)
                gd.ellipse([x - r, y - r, x + r, y + r], fill=255)
        g = np.asarray(gl.filter(ImageFilter.GaussianBlur(2.5)), float)[..., None] / 255.0
        hue = np.array([[40, 200, 255], [60, 255, 150], [90, 120, 255]])[seed % 3]
        rgb = rgb * (1 - g) + hue * g
    img = np.dstack([np.clip(rgb, 0, 255), np.asarray(a)]).astype(np.uint8)
    Image.fromarray(img, "RGBA").save(path)


def card_mesh(path, w=1.0, h=1.0):
    """A unit upright quad (x across, y up), uv (0,0) top-left as the image is stored."""
    m = Mesh()
    # subdivided (a two-triangle mesh is below what the LOD chain will keep), with a hair of depth so its
    # bounds are not degenerate
    n = 8
    xs = np.linspace(-w / 2, w / 2, n + 1)
    ys = np.linspace(0, h, n + 1)
    X, Y = np.meshgrid(xs, ys, indexing="ij")
    P = np.stack([X, Y, 0.002 * np.sin(X * 7 + Y * 5)], -1).reshape(-1, 3)
    UV = np.stack([(X + w / 2) / w, 1.0 - Y / h], -1).reshape(-1, 2)
    from geom import grid_faces
    m.add(P, grid_faces(n + 1, n + 1, False), UV)
    m.write(path, "card", outward_from=lambda Q: np.tile([0, 0, -1.0], (len(Q), 1)))
