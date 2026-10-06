#!/usr/bin/env python3
"""Contact sheet from a clip directory: sheet.py DIR OUT.png FPS FROM [--every N] [--cols C] [--grey]."""
import sys, os, glob
from PIL import Image, ImageDraw, ImageOps
d, out, fps, t0 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
every = int(sys.argv[sys.argv.index('--every') + 1]) if '--every' in sys.argv else 1
cols = int(sys.argv[sys.argv.index('--cols') + 1]) if '--cols' in sys.argv else 4
grey = '--grey' in sys.argv
files = sorted(glob.glob(os.path.join(d, 'f*.png')))[::every]
w, h = 480, 270
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w, rows * h), (20, 20, 20))
dr = ImageDraw.Draw(sheet)
for k, f in enumerate(files):
    im = Image.open(f).convert('RGB').resize((w, h), Image.LANCZOS)
    if grey: im = ImageOps.grayscale(im).convert('RGB')
    x, y = (k % cols) * w, (k // cols) * h
    sheet.paste(im, (x, y))
    idx = int(os.path.basename(f)[1:6])
    dr.text((x + 6, y + 4), f"t={t0 + idx / fps:.2f}", fill=(255, 255, 0))
sheet.save(out)
print(out, len(files))
