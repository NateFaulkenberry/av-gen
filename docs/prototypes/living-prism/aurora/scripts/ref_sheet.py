#!/usr/bin/env python3
"""The Living Prism (aurora revision), Phase 1: the annotated reference sheet (spec 3.1).

    python3 docs/prototypes/living-prism/aurora/scripts/ref_sheet.py <ref.jpg> <out dir>

Crops the concept art (1024 x 1024) into the regions the spec names, samples its palette (median-cut, 12 colours,
with each colour's share of the image), and writes reference-sheet.jpg and palette.json. The concept art itself is
not committed (provenance unknown, public repository); only this script is.
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ref, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
im = Image.open(ref).convert("RGB")
W, H = im.size
try:
    F = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 15)
except OSError:
    F = ImageFont.load_default()
# (name, box in 1024 coordinates, note)
CROPS = [
    ("crystals: left cluster", (20, 170, 400, 820),
     "tall leaning shards, 6-10 tips; clear bodies with pink/cyan interiors; bright facet edges; dark crack lines"),
    ("crystal detail", (225, 175, 385, 450),
     "facet boundaries as pale lines; internal fractures; aurora green on the upper faces, pink below; speckles"),
    ("crystals: right", (715, 320, 975, 830),
     "two large prisms, wide, tilted; mint edges, pink cores; smaller tips recede up the valley"),
    ("aurora", (0, 0, 1024, 460),
     "curtains: green-teal bases, violet and pink upper/mid; vertical striation; dark gaps between folds; stars"),
    ("horizon and mountains", (360, 350, 720, 560),
     "dark layered mountains; a warm pink-peach horizon glow under the aurora; distant crystals small"),
    ("river", (300, 520, 720, 1024),
     "S-curve from bottom centre to the horizon; long-exposure streaks: pink, mint, pearl; dark banks"),
    ("circuits", (600, 650, 1024, 1024),
     "mint lines on the banks, right-angle branches, small node rings; thin; follow the slope"),
    ("valley", (0, 640, 330, 1024),
     "dark moss and grass, near-black rock; the green is deep, not bright"),
]
TW, TH = 380, 300
sheet = Image.new("RGB", (TW * 4, (TH + 64) * 2 + 120), (14, 14, 18))
d = ImageDraw.Draw(sheet)
s = W / 1024.0
for i, (name, box, note) in enumerate(CROPS):
    c = im.crop(tuple(int(v * s) for v in box))
    c.thumbnail((TW, TH))
    x, y = (i % 4) * TW, (i // 4) * (TH + 64)
    sheet.paste(c, (x + (TW - c.width) // 2, y))
    d.text((x + 6, y + TH + 2), name, fill=(240, 240, 245), font=F)
    words, line, ly = note.split(), "", y + TH + 20
    for w in words:
        if d.textlength(line + " " + w, font=F) > TW - 12:
            d.text((x + 6, ly), line, fill=(180, 180, 190), font=F)
            line, ly = w, ly + 16
        else:
            line = (line + " " + w).strip()
    d.text((x + 6, ly), line, fill=(180, 180, 190), font=F)
q = im.resize((256, 256)).quantize(colors=12, method=Image.Quantize.MEDIANCUT)
pal = q.getpalette()[:36]
counts = sorted(q.getcolors(), reverse=True)
total = sum(n for n, _ in counts)
cols = []
x = 10
y0 = (TH + 64) * 2 + 20
for n, idx in counts:
    rgb = tuple(pal[idx * 3: idx * 3 + 3])
    w = max(8, int((TW * 4 - 20) * n / total))
    d.rectangle([x, y0, x + w - 2, y0 + 70], fill=rgb)
    cols.append({"srgb": rgb, "share": round(n / total, 3)})
    x += w
d.text((10, y0 - 18), "palette (median cut, 12; width = share of the image)", fill=(220, 220, 230), font=F)
sheet.save(os.path.join(out, "reference-sheet.jpg"), quality=88)
json.dump(cols, open(os.path.join(out, "palette.json"), "w"), indent=1)
print(json.dumps(cols))
