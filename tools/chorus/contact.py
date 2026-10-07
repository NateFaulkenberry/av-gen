#!/usr/bin/env python3
"""contact.py <out.jpg> <img>... : a labelled contact sheet (3 per row)."""
import sys, os
from PIL import Image, ImageDraw
out, paths = sys.argv[1], sys.argv[2:]
cols = 3 if len(paths) > 4 else 2 if len(paths) > 1 else 1
w = 640
ims = [Image.open(p).convert("RGB") for p in paths]
h = int(w * ims[0].height / ims[0].width)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * w, rows * (h + 22)), (12, 12, 14))
d = ImageDraw.Draw(sheet)
for i, (p, im) in enumerate(zip(paths, ims)):
    x, y = (i % cols) * w, (i // cols) * (h + 22)
    sheet.paste(im.resize((w, h), Image.LANCZOS), (x, y))
    d.text((x + 6, y + h + 4), os.path.splitext(os.path.basename(p))[0], fill=(220, 220, 220))
sheet.save(out, quality=90)
print(out)
