#!/usr/bin/env python3
"""TEST 06 conductor timeline: conductor_chart.py conductor.csv structure.txt out.png"""
import csv, re, sys
from PIL import Image, ImageDraw
rows = list(csv.DictReader(open(sys.argv[1])))
secs, phr = [], []
for l in open(sys.argv[2]):
    m = re.match(r'\s*section\s+([\d.]+)-\s*([\d.]+)\s+group\s+(-?\d+)\s+(\S+)', l)
    if m: secs.append((float(m.group(1)), m.group(4)))
    m = re.match(r'\s*phrase\s+\d+\s+([\d.]+)-\s*([\d.]+)\s+sec\s+\d+\s+kickOpens\s+(\d)', l)
    if m: phr.append((float(m.group(1)), m.group(3) == '1'))
W, H, L, R, T, B = 2400, 620, 70, 30, 44, 90
t0, t1 = float(rows[0]['songT']), float(rows[-1]['songT'])
X = lambda t: L + (t - t0) / (t1 - t0) * (W - L - R)
Y = lambda v: T + (1 - v) * (H - T - B)
im = Image.new('RGB', (W, H), (16, 16, 18)); d = ImageDraw.Draw(im)
col = {0: (150, 150, 150), 1: (200, 210, 255), 2: (150, 60, 170), 3: (200, 120, 60), 4: (160, 200, 200), 5: (220, 190, 120), 6: (120, 120, 120)}
name = {0: 'MASK', 1: 'SERAPH', 2: 'ABYSS', 3: 'CHIMERA', 4: 'MACHINE GOD', 5: 'CHOIR', 6: 'HORNS'}
for v in [0, 0.25, 0.5, 0.75, 1.0]:
    d.line([L, Y(v), W - R, Y(v)], fill=(40, 40, 46)); d.text((10, Y(v) - 6), f"{v:.2f}", fill=(150, 150, 150))
prev, start = None, t0
for r in rows + [None]:
    a = int(float(r['arch'])) if r else None; t = float(r['songT']) if r else t1
    if prev is not None and a != prev:
        d.rectangle([X(start), H - B + 8, X(t), H - B + 28], fill=col[prev]); d.text((X(start) + 4, H - B + 12), name[prev], fill=(0, 0, 0)); start = t
    if prev is None: start = t
    prev = a
for t, kick in phr:
    if t0 <= t <= t1: d.line([X(t), T, X(t), H - B], fill=(200, 60, 60) if kick else (70, 70, 80), width=3 if kick else 1)
for t, lab in secs:
    if t0 <= t <= t1: d.line([X(t), T - 30, X(t), H - B], fill=(240, 220, 60), width=2); d.text((X(t) + 4, T - 30), lab, fill=(240, 220, 60))
d.line([(X(float(r['songT'])), Y(min(1, float(r['flash']) * 2))) for r in rows], fill=(80, 200, 230), width=1)
d.line([(X(float(r['songT'])), Y(float(r['C']))) for r in rows], fill=(235, 235, 235), width=3)
for r in rows:
    if float(r['strobe']) > 0.5: d.line([X(float(r['songT'])), T, X(float(r['songT'])), H - B], fill=(255, 120, 40), width=2)
for s in range(int(t0) + 1, int(t1) + 1, 4): d.text((X(s) - 8, H - B + 34), f"{s}s", fill=(150, 150, 150))
d.text((L, H - 30), "white: coherence C | cyan: snare face-flash | red: phrase opened by a strong kick (violent collapse) | grey: soft phrase start | orange: collapse strobe | yellow: section boundary | bar: archetype (song seconds)", fill=(200, 200, 200))
im.save(sys.argv[3])
