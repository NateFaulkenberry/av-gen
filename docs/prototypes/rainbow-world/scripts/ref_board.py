#!/usr/bin/env python3
"""Annotated reference board: each image with what to TAKE and what to LEAVE.

  ref_board.py <out.png> <spec.json>
spec.json: {"title": "...", "cols": 2, "w": 720, "items": [{"img": path, "title": "...", "take": [...], "leave": [...]}]}
The images are the owner's references (not committed); the spec lives in build/ beside them.
"""
import json
import sys
import textwrap

from PIL import Image, ImageDraw, ImageFont


def main():
    out, spec = sys.argv[1], json.load(open(sys.argv[2]))
    cols, W = spec.get("cols", 2), spec.get("w", 720)
    font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 14)
    hf = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 16)
    tf = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 22)
    cells = []
    for it in spec["items"]:
        im = Image.open(it["img"]).convert("RGB")
        h = int(im.height * W / im.width)
        im = im.resize((W, h), Image.LANCZOS)
        lines = [("h", it["title"])]
        for t in it.get("take", []):
            for k, l in enumerate(textwrap.wrap(t, 84)):
                lines.append(("t", ("+ " if k == 0 else "  ") + l))
        for t in it.get("leave", []):
            for k, l in enumerate(textwrap.wrap(t, 84)):
                lines.append(("l", ("- " if k == 0 else "  ") + l))
        cells.append((im, lines))
    rows = [cells[i:i + cols] for i in range(0, len(cells), cols)]
    heights = [max(c[0].height + 12 + 19 * len(c[1]) + 16 for c in r) for r in rows]
    board = Image.new("RGB", (cols * (W + 16) + 16, 50 + sum(heights)), (22, 22, 24))
    d = ImageDraw.Draw(board)
    d.text((16, 12), spec["title"], fill=(245, 245, 245), font=tf)
    y = 50
    for r, rh in zip(rows, heights):
        for i, (im, lines) in enumerate(r):
            x = 16 + i * (W + 16)
            board.paste(im, (x, y))
            ty = y + im.height + 8
            for kind, text in lines:
                col = {"h": (255, 220, 120), "t": (170, 230, 160), "l": (240, 150, 140)}[kind]
                d.text((x, ty), text, fill=col, font=hf if kind == "h" else font)
                ty += 19
        y += rh
    board.save(out)
    print(out)


if __name__ == "__main__":
    main()
