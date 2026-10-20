#!/usr/bin/env python3
"""Labelled grid of images: sheet_grid.py out.png "Title" --cols 2 --w 640 img1.png "label 1" img2.png "label 2" ..."""
import argparse

from PIL import Image, ImageDraw, ImageFont


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("title")
    ap.add_argument("pairs", nargs="+")
    ap.add_argument("--cols", type=int, default=2)
    ap.add_argument("--w", type=int, default=640)
    a = ap.parse_args()
    items = list(zip(a.pairs[0::2], a.pairs[1::2]))
    ims = [Image.open(p).convert("RGB") for p, _ in items]
    W = a.w
    H = max(int(im.height * W / im.width) for im in ims)
    font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 14)
    tf = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 18)
    rows = (len(items) + a.cols - 1) // a.cols
    sheet = Image.new("RGB", (a.cols * W, rows * (H + 22) + 34), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    d.text((8, 6), a.title, fill=(240, 240, 240), font=tf)
    for i, (im, (_, label)) in enumerate(zip(ims, items)):
        h = int(im.height * W / im.width)
        x, y = (i % a.cols) * W, 34 + (i // a.cols) * (H + 22)
        sheet.paste(im.resize((W, h), Image.LANCZOS), (x, y + 22))
        d.text((x + 6, y + 3), label, fill=(255, 220, 120), font=font)
    sheet.save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
