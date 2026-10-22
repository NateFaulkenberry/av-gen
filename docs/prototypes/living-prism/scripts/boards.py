#!/usr/bin/env python3
"""The Living Prism, Stage 1: the palette board and the reference sheet (PIL only).

    python3 docs/prototypes/living-prism/scripts/boards.py <out dir> [--refs "~/Desktop/living prism"]

palette.png: the six looks of examples/living-prism/palette.py, each role a swatch sized by the screen area it is meant
to hold (shadow rock largest, tips smallest), so the board reads as the hierarchy, not as a list.
references.jpg: the owner's eight references with the notes of 05-stage1-plan.md section 2 (kept out of the
repository: their source and licence are unknown).
"""
import argparse
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "examples", "living-prism"))
import palette as P  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--refs", default=os.path.expanduser("~/Desktop/living prism"))
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
try:
    FONT = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 18)
    SMALL = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 13)
except OSError:
    FONT = SMALL = ImageFont.load_default()

# share of a representative frame each role is meant to hold (relative widths on the board)
AREA = [("shadow", 30), ("rockLit", 10), ("haze", 8), ("water", 12), ("primary", 9), ("secondary", 7),
        ("tertiary", 5), ("core", 4), ("fill", 3), ("seam", 2.5), ("accent", 2.5), ("key", 2), ("tip", 1.5)]
W, ROW = 1600, 150
img = Image.new("RGB", (W, 40 + ROW * len(P.LOOKS)), (12, 12, 16))
d = ImageDraw.Draw(img)
tot = sum(w for _, w in AREA)
for i, (name, roles) in enumerate(P.looks_linear()):
    y = 40 + i * ROW
    d.text((10, y + 4), name, fill=(235, 235, 240), font=FONT)
    x = 10.0
    for role, w in AREA:
        ww = (W - 20) * w / tot
        d.rectangle([x, y + 30, x + ww - 2, y + ROW - 22], fill=P.linear_to_srgb8(roles[role]))
        if ww > 38:
            d.text((x + 2, y + ROW - 20), role, fill=(170, 170, 180), font=SMALL)
        x += ww
d.text((10, 10), "The Living Prism: palette looks (swatch width = intended share of the frame)", fill=(200, 200, 210),
       font=FONT)
img.save(os.path.join(a.out, "palette.png"))

NOTES = {
    "3AJSV": "macro quartz: thin-film iridescence, blue/violet/peach on clear bodies; sharp tips. TAKE: tip highlights, cool-warm split inside one crystal",
    "HV6gk": "prism garden: hex columns, pointed terminations, colour graded along each column, glowing tips. TAKE: silhouette, heights varied. LEAVE: jungle",
    "LZ626": "aurora valley: faceted shards with cracks, iridescent river. TAKE: shard facets, mirrored colour in water. LEAVE: circuits, aurora, scale",
    "URF5R": "macro amethyst/coral: stepped growth terraces, dark matrix rock, warm-cool. TAKE: the ember's terraces, the warm accent",
    "XQBew": "neon cluster + rings: druse burst around a gem. TAKE: rings as a pool-ripple idea. LEAVE: green neon, space void",
    "_awgA": "painterly cave: dark faceted boulders, magenta glow at crystal bases, blue haze, pool. TAKE: the grotto's values and base glow",
    "abm62": "crystal arch over a mirror pool, spire beyond. TAKE: the focal structure and the composition through it",
    "yzHmv": "cave stream: violet hex clusters with lit tips on the banks, blue haze back, speckle. TAKE: banks, depth by haze",
}
files = sorted(glob.glob(os.path.join(a.refs, "*.jp*g")))
if files:
    TW, TH = 480, 360
    sheet = Image.new("RGB", (TW * 4, (TH + 70) * 2), (12, 12, 16))
    sd = ImageDraw.Draw(sheet)
    for i, f in enumerate(files[:8]):
        im = Image.open(f).convert("RGB")
        im.thumbnail((TW, TH))
        x, y = (i % 4) * TW, (i // 4) * (TH + 70)
        sheet.paste(im, (x + (TW - im.width) // 2, y))
        note = next((v for k, v in NOTES.items() if os.path.basename(f).startswith(k)), "")
        words, line, ly = note.split(), "", y + TH + 4
        for w_ in words:
            if sd.textlength(line + " " + w_, font=SMALL) > TW - 10:
                sd.text((x + 5, ly), line, fill=(210, 210, 215), font=SMALL)
                line, ly = w_, ly + 15
            else:
                line = (line + " " + w_).strip()
        sd.text((x + 5, ly), line, fill=(210, 210, 215), font=SMALL)
    sheet.save(os.path.join(a.out, "references.jpg"), quality=88)
print("wrote", a.out)
