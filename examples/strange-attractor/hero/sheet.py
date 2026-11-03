"""Contact sheet: python3 sheet.py <out.png> <cols> <tile_px> <img...> (labels are the file names)."""
import os
import sys

from PIL import Image, ImageDraw

out, cols, tile = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
paths = sys.argv[4:]
rows = (len(paths) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tile, rows * tile), (12, 12, 14))
d = ImageDraw.Draw(sheet)
for i, p in enumerate(paths):
    im = Image.open(p).convert("RGB")
    im.thumbnail((tile, tile))
    x, y = (i % cols) * tile, (i // cols) * tile
    sheet.paste(im, (x + (tile - im.width) // 2, y + (tile - im.height) // 2))
    d.text((x + 6, y + 4), os.path.basename(p), fill=(230, 230, 230))
sheet.save(out)
