#!/usr/bin/env python3
"""Palette board: the references' measured colours beside the proposed first-pass palette.

  palette_board.py <out.png> <ref1> [<ref2> ...]

For each reference, a small k-means (k=10, on a 96 px thumbnail, deterministic seed) gives its
dominant colours sorted by hue, with their share of the image. The proposed palette is defined
below in sRGB hex; each swatch also prints its linear-RGB triple (what a scene file wants).
"""
import colorsys
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

PROPOSED = [
    ("Sky", [("zenith cobalt", "#1C4BB8"), ("upper sky", "#2F74D6"), ("mid sky", "#4C9BE8"),
             ("horizon haze", "#A8D8F0"), ("violet high-sky tint", "#4A3FA6")]),
    ("Clouds", [("sunlit top", "#FFF7EA"), ("silver rim", "#FFFFFF"), ("core shadow", "#8F9CC9"),
                ("deep shadow", "#5D6AA3"), ("warm underside bounce", "#E9B79E")]),
    ("Greens", [("sunlit meadow", "#A6CF3A"), ("meadow", "#56A62E"), ("deep grass", "#2F7D35"),
                ("cool shadow green", "#1F5E58"), ("olive moss", "#7C912B"), ("canopy lit", "#71B83F"),
                ("canopy core", "#1E4E2D")]),
    ("Earth and stone", [("warm stone", "#CBA37A"), ("cool stone", "#808A98"), ("stone shadow", "#4A5068"),
                         ("bark", "#5B3E2C"), ("soil", "#7B5536")]),
    ("Light", [("sun key", "#FFE2AE"), ("bounce green", "#9FCB6A"), ("shadow fill", "#3B5196")]),
    ("Rainbow (emissive, focal only)", [("red", "#FF2B1C"), ("orange", "#FF8A00"), ("yellow", "#FFE600"),
                                         ("green", "#2BD94A"), ("blue", "#1E7BFF"), ("violet", "#7B2CFF")]),
    ("Flower accents (small doses)", [("magenta", "#E23E9C"), ("coral", "#FF6F59"), ("lilac", "#B07CFF"),
                                       ("buttercup", "#FFD23A")]),
]


def hex2rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def kmeans(px, k=10, it=25, seed=3):
    rng = np.random.default_rng(seed)
    cen = px[rng.choice(len(px), k, replace=False)]
    for _ in range(it):
        d = ((px[:, None, :] - cen[None]) ** 2).sum(-1)
        lab = d.argmin(1)
        for j in range(k):
            if (lab == j).any():
                cen[j] = px[lab == j].mean(0)
    share = np.bincount(lab, minlength=k) / len(px)
    return cen, share


def main():
    out, refs = sys.argv[1], sys.argv[2:]
    font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 13)
    tf = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 18)
    W = 1500
    rowh = 92
    H = 40 + len(refs) * rowh + 40 + sum(1 for _ in PROPOSED) * 112 + 20
    img = Image.new("RGB", (W, H), (24, 24, 26))
    d = ImageDraw.Draw(img)
    d.text((10, 10), "Measured reference colours (k-means, k=10, sorted by hue; width = share)", fill=(240, 240, 240), font=tf)
    y = 40
    for r in refs:
        im = Image.open(r).convert("RGB")
        th = im.copy()
        th.thumbnail((96, 96))
        px = np.asarray(th, dtype=np.float32).reshape(-1, 3)
        cen, share = kmeans(px)
        order = sorted(range(len(cen)), key=lambda j: colorsys.rgb_to_hsv(*(cen[j] / 255))[0])
        prev = im.copy()
        prev.thumbnail((140, rowh - 12))
        img.paste(prev, (10, y))
        x = 170
        for j in order:
            w = int(1200 * share[j])
            d.rectangle([x, y, x + w, y + rowh - 30], fill=tuple(int(v) for v in cen[j]))
            if w > 60:
                d.text((x + 3, y + rowh - 26), "#%02X%02X%02X" % tuple(int(v) for v in cen[j]), fill=(220, 220, 220), font=font)
            x += w
        y += rowh
    y += 10
    d.text((10, y), "Proposed first-pass palette (sRGB hex; linear RGB below each)", fill=(240, 240, 240), font=tf)
    y += 30
    for group, sw in PROPOSED:
        d.text((10, y + 30), group, fill=(255, 220, 120), font=font)
        x = 250
        for name, hx in sw:
            c = hex2rgb(hx)
            d.rectangle([x, y, x + 160, y + 52], fill=c)
            d.text((x, y + 55), name[:22], fill=(230, 230, 230), font=font)
            d.text((x, y + 70), hx, fill=(160, 160, 160), font=font)
            d.text((x, y + 85), "%.3f %.3f %.3f" % tuple(lin(v) for v in c), fill=(160, 160, 160), font=font)
            x += 175
        y += 112
    img.save(out)
    print(out)


if __name__ == "__main__":
    main()
