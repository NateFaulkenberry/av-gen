"""Cursed Slammers POC 2, Test A + C: a lenticular print built from N rendered views, and a viewer moving in front of
it. A CACHED, OFFLINE simulation (numpy): the N views are rendered once by AV Gen (harness.py --mode rail; the face
is static, so they never change), interlaced into a print, and every output frame is what one lens sheet shows a
viewer at that position. It simulates how a print LOOKS through a lens; it is not a physical optics solver (no
refraction ray-trace, no binocular disparity: a monitor shows one image to both eyes).

    python3 lenticular.py --views <dir-with-v00..v(N-1)>/ --prefix b1-rail --out <dir> [--pitch 5] [--zone 6]
        [--viewer-dist 2.5] [--seconds 16] [--period 8] [--lens 1] [--print-only]

The model, per output frame:
1. PRINT. The print is W px wide, lenses `pitch` px wide (W / pitch lenses, vertical lenticules). Under lens j sit N
   strips, one per view, in REVERSED order (a cylindrical lens inverts left and right), each sampled from that view
   at lens j's position. `interlaced.png` is that print, column by column, exactly as a printer would lay it down.
2. LENS LOOKUP. A viewer at horizontal offset X, distance D (both in print widths) sees lens j at angle
   theta_j = atan((X - x_j) / D). The lens maps an angle in +-zone degrees onto its strips: s = N (0.5 + tan(theta_j) /
   (2 tan(zone))) - 0.5. Outside the zone the strip index wraps (the lens's repeating viewing zones and the jump
   between them, a real lenticular artefact). Because theta varies across the print, the view changes sweep across
   the image instead of switching all at once (the print's "curtain").
3. WHAT THE LENS SHOWS. The lens magnifies its one strip to the lens's full width, so the shown image is view s
   sampled once per lens (horizontal resolution = lens count) and constant across the lens. Neighbouring views blend
   over a narrow band (`--crisp`), plus a small constant ghost of the two neighbours (`--ghost`): crosstalk.
4. LENS SURFACE (--lens 1, Test C). Each lenticule is a cylindrical cap. A small light upper-left of the viewer
   reflects in it: the highlight sits where the cap's normal bisects light and eye, a thin vertical glint at the same
   place in every lens, inside a soft window where the light's mirror image falls on the sheet; it slides as the
   viewer moves. Restrained (additive, peak --gloss). No stripes are painted elsewhere.
"""
import argparse
import math
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("--views", required=True)
ap.add_argument("--prefix", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--name", default="")
ap.add_argument("--pitch", type=int, default=5)
ap.add_argument("--zone", type=float, default=6.0, help="the lens's half viewing angle, degrees")
ap.add_argument("--viewer-dist", type=float, default=2.5, help="print widths")
ap.add_argument("--seconds", type=float, default=16.0)
ap.add_argument("--period", type=float, default=8.0)
ap.add_argument("--fps", type=int, default=30)
ap.add_argument("--crisp", type=float, default=0.25, help="half-width of the blend between neighbouring views (0..0.5)")
ap.add_argument("--ghost", type=float, default=0.06)
ap.add_argument("--lens", type=int, default=1)
ap.add_argument("--gloss", type=float, default=0.22)
ap.add_argument("--print-only", action="store_true")
ap.add_argument("--light", default="-0.9,0.7,1.6", help="the glint light, print widths (x, y up, distance)")
ap.add_argument("--window", default="0.28,0.30", help="the glint window's half-widths (x, y), print widths")
ap.add_argument("--glint-width", type=float, default=0.07, help="the glint's angular width inside a lens, radians")
ap.add_argument("--curtain", type=float, default=1.0,
                help="1 = interlaced for a viewer at infinity (the view changes sweep across the print at distance D);"
                     " < 1 = interlacing corrected toward the design distance D (a printer's pitch correction), so"
                     " the whole print shows nearly one view and only a residual curtain drifts")
ap.add_argument("--sweep", type=float, default=0.0, help="viewer sweep half-amplitude, print widths (0 = D tan zone)")
ap.add_argument("--anaglyph", type=float, default=0.0,
                help="DIAGNOSTIC: > 0 renders each eye at X -+ sep/2 (print widths) and writes a red/cyan anaglyph")
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
name = a.name or a.prefix + ("-lens" if a.lens else "-nolens")

# ---- the views (sRGB 8-bit PNGs from AV Gen); blend in linear light
files = []
i = 0
while True:
    f = os.path.join(a.views, "%s-v%02d" % (a.prefix, i), "frame_000000.png")
    if not os.path.exists(f):
        break
    files.append(f)
    i += 1
N = len(files)
if N < 2:
    sys.exit("lenticular: no views under " + a.views)
srgb = np.stack([np.asarray(Image.open(f).convert("RGB"), dtype=np.float32) / 255.0 for f in files])
lin = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
_, H, W, _ = lin.shape
P = a.pitch
L = W // P
Wp = L * P
lin = lin[:, :, :Wp, :]
# each view sampled once per lens (the lens magnifies one strip): box-average the view over the lens's width
per_lens = lin.reshape(N, H, L, P, 3).mean(axis=3)                    # N x H x L x 3
print("views", N, "size", W, "x", H, "lenses", L, "pitch", P)

# ---- 1. the print itself (what a printer lays down): under lens j, column k of P shows view N-1-floor(k N / P)
cols = np.arange(Wp)
k = cols % P
vidx = (N - 1) - np.minimum(N - 1, (k * N) // P)
printed = lin[vidx, :, cols, :]                                       # Wp x H x 3
printed = np.transpose(printed, (1, 0, 2))


def to_srgb8(x):
    x = np.clip(x, 0.0, 1.0)
    s = np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055)
    return (s * 255.0 + 0.5).astype(np.uint8)


Image.fromarray(to_srgb8(printed)).save(os.path.join(a.out, name.split("-lens")[0].split("-nolens")[0]
                                                     + "-interlaced-print.png"))
crop = printed[H // 2 - 90:H // 2 + 90, Wp // 2 - 160:Wp // 2 + 160]
Image.fromarray(to_srgb8(crop)).resize((320 * 4, 180 * 4), Image.NEAREST).save(
    os.path.join(a.out, name.split("-lens")[0].split("-nolens")[0] + "-interlaced-print-zoom4x.png"))
if a.print_only:
    sys.exit(0)

# ---- 2-4. a viewer sweeping in front of the print
aspect = W / H
xj = ((np.arange(L) + 0.5) / L - 0.5)                                 # lens centres, print widths (x), -0.5..0.5
tz = math.tan(math.radians(a.zone))
D = a.viewer_dist
Xmax = a.sweep if a.sweep > 0 else D * tz                             # default: the centre lens sees all N views
SS = 16                                                                # sub-samples per pixel column (a glint is narrower than a pixel)
u = (np.arange(P * SS) + 0.5) / (P * SS)                               # position inside a lens, 0..1
kcurv = 0.85                                                           # cap: normal angle asin((u - .5) 2 k)
lens_phi = np.arcsin((u - 0.5) * 2 * kcurv)                            # P * SS
yrow = (np.arange(H) + 0.5) / H - 0.5                                  # print heights (y up = negative row)
light = np.array([float(c) for c in a.light.split(",")])               # print widths
WX, WY = (float(c) for c in a.window.split(","))


def frame(X):
    theta = np.arctan((X - a.curtain * xj) / D)                        # L
    s = N * (0.5 + np.tan(theta) / (2 * tz)) - 0.5
    s = np.mod(s, N)                                                   # repeating zones
    f0 = np.floor(s).astype(int) % N
    fr = s - np.floor(s)
    c = max(1e-3, a.crisp)
    w1 = np.clip((fr - (0.5 - c)) / (2 * c), 0.0, 1.0)
    w1 = w1 * w1 * (3 - 2 * w1)
    wts = np.zeros((L, N), dtype=np.float32)
    idx = np.arange(L)
    wts[idx, f0] += (1 - w1)
    wts[idx, (f0 + 1) % N] += w1
    g = a.ghost
    wts = wts * (1 - 2 * g) + g * (np.roll(wts, 1, axis=1) + np.roll(wts, -1, axis=1))
    img = np.einsum("ln,nhlc->hlc", wts, per_lens)                     # H x L x 3
    img = np.repeat(img, P, axis=1)                                    # constant across each lens
    if a.lens:
        eye = np.array([X, 0.0, D])
        # per lens: horizontal angle of the half vector between light and eye at that lens -> where in the cap it glints
        pj = np.stack([xj, np.zeros(L), np.zeros(L)], axis=1)
        lv = light[None, :] - pj
        ev = eye[None, :] - pj
        lv /= np.linalg.norm(lv, axis=1, keepdims=True)
        ev /= np.linalg.norm(ev, axis=1, keepdims=True)
        hv = lv + ev
        hv /= np.linalg.norm(hv, axis=1, keepdims=True)
        phi_h = np.arctan2(hv[:, 0], hv[:, 2])                         # L
        glint = np.exp(-((lens_phi[None, :] - phi_h[:, None]) / a.glint_width) ** 2)   # L x P*SS
        glint = glint.reshape(L, P, SS).mean(axis=2)                   # each pixel column's coverage
        # the window: the light's mirror image on the sheet (where the eye sees the light reflected), soft and broad
        xm = X + (light[0] - X) * D / (D + light[2])
        ym = (light[1] * D / (D + light[2]))
        win_x = np.exp(-((xj - xm) / WX) ** 2)                       # L
        win_y = np.exp(-((-yrow / aspect - ym) / WY) ** 2)                # H
        hl = (glint * win_x[:, None]).reshape(Wp)                      # Wp
        img = img + a.gloss * win_y[:, None, None] * hl[None, :, None] * np.array([0.9, 0.92, 1.0])
    return img


n = int(a.seconds * a.fps)
outp = os.path.join(a.out, name + ".mp4")
ff = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
                       "%dx%d" % (Wp, H), "-r", str(a.fps), "-i", "-", "-c:v", "libx264", "-crf", "16", "-pix_fmt",
                       "yuv420p", outp], stdin=subprocess.PIPE)
t0 = time.time()
for fi in range(n):
    t = fi / a.fps
    X = -Xmax * math.cos(2 * math.pi * t / a.period)                  # the same sweep as harness.py --mode sweep
    if a.anaglyph > 0:
        # each eye sees its own strip of the print: red = left eye, green and blue = right eye (luminance each)
        lw = np.array([0.2126, 0.7152, 0.0722])
        le = (frame(X - a.anaglyph / 2) @ lw)[..., None]
        re = (frame(X + a.anaglyph / 2) @ lw)[..., None]
        ff.stdin.write(to_srgb8(np.concatenate([le, re, re], axis=2)).tobytes())
        continue
    ff.stdin.write(to_srgb8(frame(X)).tobytes())
    if fi == int(a.period * a.fps / 4):
        Image.fromarray(to_srgb8(frame(X))).save(os.path.join(a.out, name + "-centre.png"))
ff.stdin.close()
ff.wait()
dt = time.time() - t0
print("wrote", outp, "%d frames, %.1f ms/frame (numpy, CPU)" % (n, 1000 * dt / n))
