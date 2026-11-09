"""Pass 3: a labelled contact sheet (or a before/after pair sheet) from camera-set renders.

  python3 sheet.py <out.png> <dir> <id-glob> [--cols 4] [--width 480] [--before <dir>]
With --before, each tile is the before (left) and the render in <dir> (right) of the same camera id."""
import argparse, fnmatch, os
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument("out"); ap.add_argument("dir"); ap.add_argument("glob")
ap.add_argument("--cols", type=int, default=4); ap.add_argument("--width", type=int, default=480)
ap.add_argument("--before")
a = ap.parse_args()
ids = sorted(f[:-4] for f in os.listdir(a.dir) if f.endswith(".png") and fnmatch.fnmatch(f[:-4], a.glob))
w = a.width; h = w * 9 // 16; lab = 22
tiles = []
for i in ids:
    im = Image.open(os.path.join(a.dir, i + ".png")).convert("RGB").resize((w, h), Image.LANCZOS)
    if a.before:
        b = Image.open(os.path.join(a.before, i + ".png")).convert("RGB").resize((w, h), Image.LANCZOS)
        t = Image.new("RGB", (2 * w + 4, h + lab), (20, 20, 20)); t.paste(b, (0, lab)); t.paste(im, (w + 4, lab))
        ImageDraw.Draw(t).text((6, 4), f"{i}   before | after", fill=(235, 235, 235))
    else:
        t = Image.new("RGB", (w, h + lab), (20, 20, 20)); t.paste(im, (0, lab))
        ImageDraw.Draw(t).text((6, 4), i, fill=(235, 235, 235))
    tiles.append(t)
cols = min(a.cols, len(tiles)); rows = (len(tiles) + cols - 1) // cols
tw, th = tiles[0].size
sheet = Image.new("RGB", (cols * tw + (cols - 1) * 4, rows * th + (rows - 1) * 4), (0, 0, 0))
for k, t in enumerate(tiles):
    sheet.paste(t, ((k % cols) * (tw + 4), (k // cols) * (th + 4)))
sheet.save(a.out)
print(a.out, len(tiles))
