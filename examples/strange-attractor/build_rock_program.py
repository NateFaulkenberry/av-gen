#!/usr/bin/env python3
"""Writes the canyonRock material program into strange-attractor-flythrough.scene.json (ADR-1857, 1859, 1860, 1862).

The terrain mesh carries, per vertex:
  uv.x  the strata coordinate in bands, before the band warp (linear up a wall, so interpolation is exact)
  uv.y  a whole playa level 0..16 plus 0.05 x the baked ambient visibility
The program applies the band warp per pixel, colours the bands, darkens with the baked occlusion and writes the
ledges, laminations and relief as a tangent-space detail normal along the band coordinate's gradient.

    python3 examples/strange-attractor/build_rock_program.py [scene.json]
"""
import json, math, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
SCENE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'strange-attractor-flythrough.scene.json')

LEDGE = 1.5        # tilt of the under-lip at a band's foot (the head's lip is 0.55 of it)
LAM = 0.22         # lamination tilt, five per band
RELIEF = 0.15      # low-frequency triplanar relief tilt
BREAKUP = (0.35, 1.45)  # the ledges' strength along the wall, from the same relief noise (across-strata breakup)
FADE_FAR = 700.0   # laminations and relief are gone by here (metres); they start fading at 0.4 of it
AO_AMBIENT, AO_ALBEDO = 0.85, 0.45
SAND = [0.22, 0.10, 0.045]
FLAT_BAND = 0.25   # how much of the band colour the flats take
FLAT_RELIEF = 0.35 # the relief normal's share on flats
STOPS = [[0.66, 0.44, 0.25, 1], [0.64, 0.31, 0.12, 1], [0.38, 0.11, 0.065, 1], [0.40, 0.22, 0.24, 1]]
TAU = 2.0 * math.pi
CONTOUR = 15.0              # metres between contour lines (every fifth is a major line)
CONTOUR_PHASE = 6.0         # metres: keeps a line off the water line (lines at y = 15k - 6, majors at 75k - 6)
CONTOUR_SHARP = 0.985       # cos threshold: the line's width (about 0.4 m at 15 m spacing)
CONTOUR_FADE = 900.0        # metres: the contours are gone by here (from 0.3 of it)
CONTOUR_COLOR = [1.0, 0.42, 0.14]

ops = []
def op(kind, dst, **kw):
    o = {'kind': kind, 'dst': dst}; o.update(kw); ops.append(o)

# the band coordinate: T = S + 0.32 sin(S / 2.3 + 0.7) + 0.18 sin(S / 0.93 + 1.7) (attractor_world.cpp `bandWarp`)
op('input', 0, input='uv')
op('swizzle', 1, srcA=0, constant=[0, 0, 0, 0])
op('palette', 2, srcA=1, constant=[0, 0, 0, 0], constant2=[0.32, 0.18, 0, 0],
   constant3=[1 / (2.3 * TAU), 1 / (0.93 * TAU), 0, 0],
   constant4=[(0.7 - math.pi / 2) / TAU, (1.7 - math.pi / 2) / TAU, 0, 0], value=0.0)
op('add', 1, srcA=1, srcB=2)
op('swizzle', 2, srcA=2, constant=[1, 1, 1, 1])
op('add', 1, srcA=1, srcB=2)
op('swizzle', 1, srcA=1, constant=[0, 0, 0, 0])            # r1 = T (all lanes: quantize hashes the whole cell)
op('quantize', 2, srcA=1, value=1.0)                         # r2 = band centre, w = per-band random
op('swizzle', 5, srcA=2, constant=[0, 0, 0, 0])
op('remap', 5, srcA=5, constant=[0, 1, 0, -1])
op('add', 5, srcA=1, srcB=5)                                 # r5 = g, the position within the band (-0.5 .. 0.5)
# the band's colour
op('noise', 4, srcA=2, value=0.23, seed=7)
op('input', 6, input='worldPosition')
op('gradient', 6, srcA=6, constant=[0, 1, 0, 0], value=1 / 700)
op('remap', 6, srcA=6, constant=[0, 1, 0.3, -0.3])
op('add', 4, srcA=4, srcB=6)
op('remap', 4, srcA=4, constant=[0.28, 0.80, 0, 1], value=1)
op('oklabRamp', 4, srcA=4, constant=STOPS[0], constant2=STOPS[1], constant3=STOPS[2], constant4=STOPS[3], value=1)
op('swizzle', 3, srcA=2, constant=[3, 3, 3, 3])
op('remap', 3, srcA=3, constant=[0, 1, 0.74, 1.12])
op('multiply', 4, srcA=4, srcB=3)
op('smoothstep', 6, srcA=5, constant=[-0.5, -0.45, 0, 0])   # the bedding plane at the band's foot
op('remap', 6, srcA=6, constant=[0, 1, 0.72, 1])
op('multiply', 4, srcA=4, srcB=6)
# walls (1) against flats (0)
op('input', 7, input='normal')
op('swizzle', 7, srcA=7, constant=[1, 1, 1, 1])
op('smoothstep', 7, srcA=7, constant=[0.66, 0.4, 0, 0])
# flats: warm sand toward the band colour, broken up
op('constant', 6, constant=SAND + [1])
op('mix', 6, srcA=6, srcB=4, value=FLAT_BAND)
op('input', 3, input='worldPosition')
op('noise', 3, srcA=3, value=1 / 380, seed=61)
op('remap', 3, srcA=3, constant=[0, 1, 0.6, 1.3])
op('multiply', 6, srcA=6, srcB=3)
op('mixBy', 4, srcA=6, srcB=4, srcC=7)
# baked occlusion out of uv.y: occlusion output (r0) and a little on the albedo
op('swizzle', 3, srcA=0, constant=[1, 1, 1, 1])
op('quantize', 6, srcA=3, value=1.0)
op('remap', 6, srcA=6, constant=[0, 1, 0, -1])
op('add', 3, srcA=3, srcB=6)                                 # 0.05 x visibility - 0.5
op('remap', 0, srcA=3, constant=[-0.5, -0.45, 1 - AO_AMBIENT, 1], value=1)
op('remap', 3, srcA=3, constant=[-0.5, -0.45, 1 - AO_ALBEDO, 1], value=1)
op('multiply', 4, srcA=4, srcB=3)
# detail normal: relief noise first (it also breaks the ledges up along the wall)
op('input', 1, input='worldPosition')
op('triplanar', 1, srcA=1, value=0.09, constant=[0, 0, 0, 0], constant2=[4, 0, 0, 0], seed=23)
op('smoothstep', 6, srcA=5, constant=[-0.5, -0.4, 0, 0])   # under-lip, facing down
op('remap', 6, srcA=6, constant=[0, 1, -LEDGE, 0])
op('smoothstep', 3, srcA=5, constant=[0.36, 0.5, 0, 0])    # the head's lip, facing up
op('remap', 3, srcA=3, constant=[0, 1, 0, LEDGE * 0.55])
op('add', 6, srcA=6, srcB=3)
op('remap', 3, srcA=1, constant=[0.3, 0.7, BREAKUP[0], BREAKUP[1]], value=1)
op('multiply', 6, srcA=6, srcB=3)
op('palette', 3, srcA=5, constant=[0, 0, 0, 0], constant2=[LAM, LAM, LAM, 0], constant3=[5, 5, 5, 0],
   constant4=[0, 0, 0, 0], value=0.0)
op('input', 2, input='depth')
op('remap', 2, srcA=2, constant=[FADE_FAR * 0.4, FADE_FAR, 1, 0], value=1)
op('multiply', 3, srcA=3, srcB=2)
op('add', 6, srcA=6, srcB=3)
op('multiply', 6, srcA=6, srcB=7)                            # ledges and laminations: walls only
op('remap', 1, srcA=1, constant=[0.25, 0.75, -RELIEF, RELIEF])
op('multiply', 1, srcA=1, srcB=2)
op('remap', 3, srcA=7, constant=[0, 1, FLAT_RELIEF, 1])       # flats take less: a grazing sun turns it to blotches
op('multiply', 1, srcA=1, srcB=3)
op('add', 6, srcA=6, srcB=1)                                 # relief: everywhere (the flats were glassy)
op('constant', 3, constant=[1, 0, 0, 0])
op('multiply', 6, srcA=6, srcB=3)
op('constant', 3, constant=[0, 0, 1, 0])
op('add', 6, srcA=6, srcB=3)                                 # (s, 0, 1)

# ---- canyon Phase 2 (ADR-1862): contour tracing and the wall glow, as emission -----------------------------------
# Their strengths arrive on the material's emission (the composition writes rgb = contour, glow from the
# attractorWorld/contour and /glow parameters, which routes drive); the program writes its own emission.
op('input', 1, input='worldPosition')
op('swizzle', 1, srcA=1, constant=[1, 1, 1, 1])               # height
op('palette', 2, srcA=1, constant=[0, 0, 0, 0], constant2=[1, 1, 1, 0], constant3=[1 / CONTOUR, 1 / CONTOUR, 1 / CONTOUR, 0],
   constant4=[CONTOUR_PHASE / CONTOUR] * 3 + [0], value=0.0)   # cos(2 pi (y + phase) / spacing)
op('smoothstep', 2, srcA=2, constant=[CONTOUR_SHARP, 1.0, 0, 0])
op('palette', 3, srcA=1, constant=[0, 0, 0, 0], constant2=[1, 1, 1, 0],
   constant3=[1 / (CONTOUR * 5), 1 / (CONTOUR * 5), 1 / (CONTOUR * 5), 0],
   constant4=[CONTOUR_PHASE / (CONTOUR * 5)] * 3 + [0], value=0.0)
op('smoothstep', 3, srcA=3, constant=[1 - (1 - CONTOUR_SHARP) / 5, 1.0, 0, 0])  # every fifth line, as wide
op('remap', 2, srcA=2, constant=[0, 1, 0, 0.5])
op('add', 2, srcA=2, srcB=3)                                  # minor lines at half the major's strength
op('input', 3, input='depth')
op('remap', 3, srcA=3, constant=[CONTOUR_FADE * 0.3, CONTOUR_FADE, 1, 0], value=1)
op('multiply', 2, srcA=2, srcB=3)                             # gone before they could alias
op('input', 3, input='materialEmission')
op('swizzle', 1, srcA=3, constant=[0, 0, 0, 0])               # contour strength
op('multiply', 2, srcA=2, srcB=1)
op('constant', 1, constant=CONTOUR_COLOR + [1])
op('multiply', 2, srcA=2, srcB=1)
op('swizzle', 1, srcA=3, constant=[1, 1, 1, 1])               # glow strength
op('multiply', 1, srcA=1, srcB=4)                             # the rock's own colour, lifted
op('multiply', 1, srcA=1, srcB=7)                             # walls
op('add', 2, srcA=2, srcB=1)

assert len(ops) <= 96, len(ops)
prog = {'name': 'canyonRock', 'ops': ops, 'baseColor': 4, 'metallic': -1, 'roughness': -1, 'emission': 2,
        'opacity': -1, 'normal': 6, 'occlusion': 0}
d = json.load(open(SCENE))
d['materialPrograms'] = [p for p in d.get('materialPrograms', []) if p['name'] != 'canyonRock'] + [prog]
json.dump(d, open(SCENE, 'w'), indent=2)
print(f'canyonRock: {len(ops)} ops -> {SCENE}')
