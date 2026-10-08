"""Contact sheets for the hero look-dev renders (system python3 + Pillow, not Blender).

    python3 sheet.py <render dir> <out.png> <name> [<name> ...]          one row of views per asset
    python3 sheet.py --family <render dir> <out.png> <name> [...]         the hero view of each asset, in a grid

A mild bloom (blurred highlights, 0.25) stands in for AV Gen's; the raw renders stay untouched beside it.
"""
from __future__ import annotations

import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

VIEWS = ("hero", "side", "under", "top")


def bloom(im, k=0.25):
    hi = im.point(lambda v: max(0, v - 150) * 2)
    b = hi.filter(ImageFilter.GaussianBlur(radius=im.width / 60))
    return ImageChops.add(im, b.point(lambda v: int(v * k)))


def label(im, text):
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 8 + 7 * len(text), 18], fill=(0, 0, 0))
    d.text((4, 3), text, fill=(200, 210, 230))
    return im


def main(argv):
    family = argv[0] == "--family"
    if family:
        argv = argv[1:]
    src, out, names = argv[0], argv[1], argv[2:]
    tile = 600
    if family:
        cols = min(4, len(names))
        rows = (len(names) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * tile, rows * tile))
        for i, n in enumerate(names):
            im = bloom(Image.open(os.path.join(src, f"{n}_hero.png")).convert("RGB")).resize((tile, tile))
            sheet.paste(label(im, n), ((i % cols) * tile, (i // cols) * tile))
    else:
        sheet = Image.new("RGB", (len(VIEWS) * tile, len(names) * tile))
        for r, n in enumerate(names):
            for c, v in enumerate(VIEWS):
                p = os.path.join(src, f"{n}_{v}.png")
                if not os.path.exists(p):
                    continue
                im = bloom(Image.open(p).convert("RGB")).resize((tile, tile))
                sheet.paste(label(im, f"{n} {v}"), (c * tile, r * tile))
    sheet.save(out)


if __name__ == "__main__":
    main(sys.argv[1:])
