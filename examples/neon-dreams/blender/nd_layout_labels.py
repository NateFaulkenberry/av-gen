"""Draw the labels of nd_layout_views.py on its renders (PIL: /usr/bin/python3 nd_layout_labels.py DIR [title])."""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont
d = sys.argv[1]; title = sys.argv[2] if len(sys.argv) > 2 else ""
try:
    F = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 30); FT = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 40)
except Exception:
    F = FT = ImageFont.load_default()
for v in ("top", "oblique"):
    im = Image.open(os.path.join(d, v + ".png")).convert("RGB"); dr = ImageDraw.Draw(im)
    for k, (x, y) in json.load(open(os.path.join(d, v + "_labels.json"))).items():
        if not (0 <= x < im.width and 0 <= y < im.height):
            continue
        col = (255, 240, 120) if k.startswith("journey") else ((150, 255, 190) if "(new" in k else (255, 255, 255))
        w = dr.textlength(k, font=F)
        dr.ellipse([x - 6, y - 6, x + 6, y + 6], fill=col)
        dr.rectangle([x + 10, y - 20, x + 18 + w, y + 18], fill=(0, 0, 0))
        dr.text((x + 14, y - 18), k, fill=col, font=F)
    dr.rectangle([0, 0, im.width, 60], fill=(0, 0, 0))
    dr.text((20, 8), "%s  %s  %s  yellow = camera path 0-47 s; green = new" % (title, v.upper(), "north up" if v == "top" else "from the south-west"), fill=(255, 255, 255), font=FT)
    if v == "top":
        for k in range(0, 701, 100):
            x = k / 760.0 * im.width
        dr.line([40, im.height - 40, 40 + im.width / 760.0 * 100, im.height - 40], fill=(255, 255, 255), width=6)
        dr.text((40, im.height - 90), "100 m", fill=(255, 255, 255), font=F)
    im.save(os.path.join(d, v + "_labelled.png"))
print("ok")
