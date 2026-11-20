#!/usr/bin/env python3
"""Neon Dreams -- the surface atlas (iteration 8): one shared texture set for every wall, deck, slab and rail of the kit.

    /usr/bin/python3 examples/neon-dreams/blender/nd_surface_atlas.py --out examples/neon-dreams/meshes/tex [--scale 1]

Writes nd_surface_base.png (sRGB, a MULTIPLIER on each material's base colour factor: the hue of a building is its
factor, the atlas carries value, grime and relative tint), nd_surface_normal.png (tangent space, glTF convention),
nd_surface_orm.png (R occlusion, G roughness, B metallic: multipliers on the factors, as glTF reads them) and
nd_surface.json (the regions, in pixels and metres, that nd_kitcore maps UVs into). Original, generated from noise and
rules; no photograph, no third-party texture. Needs numpy and PIL (not Blender).

Layout at scale 1: 2048 x 1024 px, 80 px per metre.
  left 960 x 1024: FACADE CELL TILES, 5 columns x 4 rows of one facade cell (2.4 m bay x 3.2 m floor = 192 x 256 px).
      rows (finish):   0 light render, 1 dark painted render, 2 corrugated sheet, 3 glazed tile
      columns:         0, 1 a windowed cell (drips under the sill ends), 2 a blank cell,
                       3 a windowed cell on a block's lowest floor (grime rising from the slab), 4 the same, blank
  right 1024 x 1024:  deck planks (1024 x 512: 32 planks of 0.2 m, 12.8 m long), two 3.2 m deck plates, ring pavers
                      (512 x 256: 6.4 m of arc x 3.2 m radial), and 256 px patches: trim (concrete), metal, roof, neutral.
Nothing tiles: every region is used inside its own rectangle, so mip levels only ever mix like with like at a seam.
"""
import json, os, sys, math
import numpy as np
from PIL import Image

argv = sys.argv[1:]
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
OUT = arg("--out", os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "tex"))
S = int(arg("--scale", "1"))
PPM = 80 * S
W, H = 2048 * S, 1024 * S
TW, TH = 192 * S, 256 * S                       # one facade cell
NRM_GAIN = 3.5                                  # true slopes used 35 of the normal map's 127 levels
os.makedirs(OUT, exist_ok=True)

BASE = np.ones((H, W, 3), np.float32)           # linear multiplier, written as sRGB
HGT = np.zeros((H, W), np.float32)              # height in millimetres (for the normal map)
RGH = np.ones((H, W), np.float32)
AO = np.ones((H, W), np.float32)
MET = np.ones((H, W), np.float32)
NRM = np.zeros((H, W, 3), np.float32); NRM[..., 2] = 1.0
REG = {}


def noise(h, w, cell, rng, octaves=3, stretch=(1.0, 1.0)):
    """Value noise 0..1 (mean 0.5): random grids of `cell` pixels (stretched per axis), bicubic, summed in octaves."""
    out = np.zeros((h, w), np.float32); amp, tot = 1.0, 0.0
    for o in range(octaves):
        ch = max(2, int(h / (cell * stretch[1]) * 2 ** o) + 2); cw = max(2, int(w / (cell * stretch[0]) * 2 ** o) + 2)
        a = rng.random((ch, cw)).astype(np.float32)
        out += amp * np.asarray(Image.fromarray(a).resize((w, h), Image.BICUBIC), np.float32)
        tot += amp; amp *= 0.5
    return np.clip(out / tot, 0.0, 1.0)


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a + 1e-9), 0.0, 1.0)
    return t * t * (3 - 2 * t)


class Tile:
    """A rectangle of the atlas being painted: base (multiplier), height (mm), roughness, occlusion, metal."""

    def __init__(self, name, x, y, w, h, size_m, seed):
        self.name, self.x, self.y, self.w, self.h = name, x, y, w, h
        self.rng = np.random.default_rng(seed)
        self.b = np.ones((h, w, 3), np.float32); self.z = np.zeros((h, w), np.float32)
        self.r = np.ones((h, w), np.float32); self.ao = np.ones((h, w), np.float32); self.m = np.ones((h, w), np.float32)
        self.yy, self.xx = np.mgrid[0:h, 0:w].astype(np.float32)
        self.mx = self.xx / PPM; self.mv = (h - 1 - self.yy) / PPM          # metres from the left / from the BOTTOM
        REG[name] = {"px": [x, y, w, h], "m": list(size_m)}

    def tint(self, rgb, mask=None, k=1.0):
        c = np.asarray(rgb, np.float32)
        if mask is None:
            self.b *= c
        else:
            mk = (np.clip(mask, 0, 1) * k)[..., None]
            self.b = self.b * (1 - mk) + self.b * c * mk

    def blend(self, rgb, mask):
        mk = np.clip(mask, 0, 1)[..., None]
        self.b = self.b * (1 - mk) + np.asarray(rgb, np.float32) * mk

    def commit(self, nrm_k=1.0):
        x, y, w, h = self.x, self.y, self.w, self.h
        BASE[y:y + h, x:x + w] = np.clip(self.b, 0.0, 1.0); HGT[y:y + h, x:x + w] = self.z
        RGH[y:y + h, x:x + w] = np.clip(self.r, 0.04, 1.0); AO[y:y + h, x:x + w] = np.clip(self.ao, 0.0, 1.0); MET[y:y + h, x:x + w] = np.clip(self.m, 0, 1)
        gy, gx = np.gradient(self.z)                                         # mm per pixel; image y runs down
        k = nrm_k * NRM_GAIN * PPM / 1000.0                                  # slope = mm / (1000 / PPM mm), exaggerated: relief has to read in lamplight at 5 m
        n = np.stack([-gx * k, gy * k, np.ones_like(gx)], -1)                # glTF: +x right, +y up (toward the image top)
        NRM[y:y + h, x:x + w] = n / np.linalg.norm(n, axis=-1, keepdims=True)


def drips(t, xs_m, top_m, len_m, width_m, dark, rgb=(0.55, 0.5, 0.6), gloss=0.0):
    """Streaks running DOWN the wall from top_m (metres above the cell's foot)."""
    for x0 in xs_m:
        L = len_m * (0.6 + 0.8 * t.rng.random()); wd = width_m * (0.6 + 0.9 * t.rng.random())
        wob = 0.012 * np.sin(t.mv * 9.0 + t.rng.random() * 6.28)
        prof = np.exp(-((t.mx - x0 - wob) / wd) ** 2)
        fall = np.clip((top_m - t.mv) / L, 0.0, 1.0)
        along = np.where((t.mv <= top_m) & (fall < 1.0), (1.0 - fall) ** 1.4, 0.0)
        mk = prof * along * dark * (0.75 + 0.5 * t.rng.random())
        t.tint(rgb, mk)
        if gloss:
            t.r *= 1.0 - gloss * np.clip(mk, 0, 1)


def joints(t, top=1.0, bottom=1.0, sides=0.5, px=None):
    """The panel joint round a facade cell: a groove in height, a dark line in colour and occlusion."""
    px = px or max(1, int(round(1.6 * S)))
    for (mk, k) in ((t.yy < px, top), (t.yy >= t.h - px, bottom), (t.xx < px, sides), (t.xx >= t.w - px, sides)):
        if k <= 0:
            continue
        t.b[mk] *= 1.0 - 0.55 * k; t.z[mk] -= 3.0 * k; t.ao[mk] *= 1.0 - 0.5 * k; t.r[mk] = np.minimum(1.0, t.r[mk] * 1.1)
    soft = np.minimum.reduce([t.yy / (5.0 * S) if top > 0 else t.yy * 0 + 9, (t.h - 1 - t.yy) / (5.0 * S) if bottom > 0 else t.yy * 0 + 9])
    t.b *= (0.84 + 0.16 * np.clip(soft, 0, 1))[..., None]                   # dirt gathered along the horizontal joints


def rising_grime(t, strength=1.0, rgb=(0.28, 0.26, 0.36)):
    """Grime climbing from the slab a block stands on: a ragged tide line, darkest at the foot."""
    edge = 0.55 + 0.75 * noise(t.h, t.w, 60 * S, t.rng, 3, (1.0, 2.5))       # tide line height in metres, varying along the wall
    mk = (1.0 - smooth(t.mv, 0.02, 1.25) ** 0.8) * 0.85 + 0.4 * (1.0 - smooth(t.mv, edge * 0.5, edge * 1.7))
    mk *= 0.75 + 0.5 * noise(t.h, t.w, 14 * S, t.rng, 3, (0.5, 3.0))
    t.tint(rgb, mk, strength)
    t.r *= 1.0 - 0.18 * np.clip(mk * strength, 0, 1)                         # damp: a little glossier
    splash = noise(t.h, t.w, 5 * S, t.rng, 2) * (1.0 - smooth(t.mv, 0.0, 0.45))
    t.tint((0.5, 0.48, 0.55), smooth(splash, 0.55, 0.8), strength)


def patches(t, n, light=(1.12, 1.1, 1.06), dark=(0.8, 0.8, 0.84), avoid=None):
    for _ in range(n):
        pw, ph = t.rng.uniform(0.3, 0.9), t.rng.uniform(0.25, 0.8)
        px, pv = t.rng.uniform(0.1, 2.4 - pw - 0.1), t.rng.uniform(0.15, 3.2 - ph - 0.15)
        if avoid and px < avoid[1] and px + pw > avoid[0] and pv < avoid[3] and pv + ph > avoid[2]:
            continue
        e = 0.02
        mk = smooth(t.mx, px, px + e) * (1 - smooth(t.mx, px + pw - e, px + pw)) * smooth(t.mv, pv, pv + e) * (1 - smooth(t.mv, pv + ph - e, pv + ph))
        t.tint(light if t.rng.random() < 0.5 else dark, mk)
        t.z += mk * 0.6


def cracks(t, n):
    """Hairline cracks: nearly straight, wandering a little, mostly downward."""
    for _ in range(n):
        x, v = t.rng.uniform(0.2, 2.2), t.rng.uniform(1.4, 3.15); ang = t.rng.uniform(-1.9, -1.25); dark = t.rng.uniform(0.55, 0.8)
        for _s in range(int(t.rng.uniform(90, 260))):
            ang += t.rng.normal(0, 0.05) + (0.5 if t.rng.random() < 0.02 else 0.0) * t.rng.choice([-1, 1])
            x += math.cos(ang) * 0.5 / PPM * S; v += math.sin(ang) * 0.5 / PPM * S
            ix, iy = int(x * PPM), t.h - 1 - int(v * PPM)
            if 2 <= ix < t.w - 2 and 2 <= iy < t.h - 2:
                t.b[iy, ix] *= dark; t.z[iy, ix] -= 0.8; t.ao[iy, ix] *= 0.85


WIN = (0.575, 1.825, 0.95, 2.30)                # the kit's standard window in its cell (metres)


def facade_tile(row, col):
    t = Tile("cell_%d_%d" % (row, col), col * TW, row * TH, TW, TH, (2.4, 3.2), 1000 + row * 37 + col * 101)
    windowed, base = col in (0, 1, 3), col in (3, 4)
    lo = noise(TH, TW, 90 * S, t.rng, 3); fine = noise(TH, TW, 3 * S, t.rng, 2)
    if row in (0, 1):                                                        # ---- render / painted render
        t.b *= (0.84 + 0.26 * lo + 0.07 * (fine - 0.5))[..., None]
        t.z += 0.35 * (fine - 0.5) + 0.5 * (noise(TH, TW, 10 * S, t.rng, 2) - 0.5)
        t.r *= 0.9 + 0.1 * fine
        if row == 0:
            t.tint((1.0, 0.965, 0.91))
            patches(t, int(t.rng.integers(0, 3)), avoid=WIN if windowed else None)
        else:
            t.tint((0.60, 0.575, 0.62))                                      # paint over render: peeled where the wet sits (near neutral: the hue is the building's factor)
            peel = noise(TH, TW, 9 * S, t.rng, 4) + 0.30 * (1.0 - smooth(t.mv, 0.0, 0.9)) + 0.12 * smooth(t.mv, 2.85, 3.2)     # flakes, most of them low on the wall
            mk = smooth(peel, 0.76, 0.80)
            t.blend(np.array([0.86, 0.80, 0.78]) * (0.85 + 0.2 * lo[..., None]), mk * 0.5)
            t.z -= mk * 0.5; t.r *= 0.82 + 0.18 * mk
        cracks(t, int(t.rng.integers(0, 3)))
        drips(t, t.rng.uniform(0.1, 2.3, int(t.rng.integers(3, 7))), 3.2, 1.2, 0.045, 0.5)                    # from the joint above
        if windowed:
            drips(t, (WIN[0] - 0.04, WIN[1] + 0.04), WIN[2] - 0.05, 0.8, 0.035, 0.68, gloss=0.2)               # off the sill ends
            drips(t, t.rng.uniform(WIN[0] + 0.1, WIN[1] - 0.1, 3), WIN[2] - 0.07, 0.3, 0.05, 0.22)
        joints(t, 1.0, 1.0, 0.45)
    elif row == 2:                                                           # ---- corrugated sheet, painted, rusting at the laps
        rib = np.sin(t.mx * 2 * math.pi / 0.075)
        t.z += 2.2 * rib
        t.b *= (0.82 + 0.07 * rib + 0.22 * lo)[..., None]
        t.tint((0.74, 0.76, 0.80))                                           # near neutral: the hue is the building's factor
        lap_v = 1.62 + 0.0 * t.mx                                           # sheets overlap at mid height
        lap = np.exp(-((t.mv - lap_v) / 0.012) ** 2)
        t.b *= (1.0 - 0.5 * lap)[..., None]; t.z -= 1.5 * lap; t.ao *= 1.0 - 0.4 * lap
        for sx in (0.8, 1.6):                                                # vertical overlaps of 0.8 m sheets
            ln = np.exp(-((t.mx - sx) / 0.008) ** 2); t.b *= (1.0 - 0.35 * ln)[..., None]; t.ao *= 1.0 - 0.3 * ln
        rust = noise(TH, TW, 22 * S, t.rng, 4, (0.45, 2.2))
        below = np.clip((lap_v - t.mv) / 0.9, 0, 1); below = np.where(t.mv < lap_v, 1.0 - below, 0.0)
        foot = 1.0 - smooth(t.mv, 0.0, 0.5 if not base else 1.1)
        head = np.where(t.mv > lap_v, np.clip((3.2 - t.mv) / 0.5, 0, 1), 1.0); head = 1.0 - np.minimum(head, 1.0)
        mk = smooth(rust + (0.36 if not windowed else 0.22) * below + 0.5 * foot + 0.25 * head, 0.80, 0.95)   # the windowed tile repeats most: least rust
        t.blend(np.array([0.70, 0.42, 0.28]) * (0.7 + 0.5 * noise(TH, TW, 4 * S, t.rng, 2)[..., None]), mk * 0.75)
        t.r *= 0.62 + 0.38 * mk + 0.08 * fine; t.z += mk * 0.4 * (fine - 0.3)
        for fx in np.arange(0.2, 2.4, 0.4):                                  # fixings along the lap and the head
            for fv in (lap_v + 0.05, 3.08, 0.12):
                d2 = (t.mx - fx) ** 2 + (t.mv - fv) ** 2
                dot = np.exp(-d2 / (0.011 ** 2)); t.b *= (1.0 - 0.45 * dot)[..., None]; t.z += 1.2 * dot
        if windowed:
            drips(t, (WIN[0] - 0.04, WIN[1] + 0.04), WIN[2] - 0.05, 0.7, 0.03, 0.5, rgb=(0.72, 0.5, 0.4))
        joints(t, 0.7, 0.9, 0.0)
    else:                                                                    # ---- glazed tile, 0.3 x 0.2 m, stack bond
        tx, tv = 0.3, 0.2
        fx_, fv_ = (t.mx / tx) % 1.0, (t.mv / tv) % 1.0
        ix, iv = np.floor(t.mx / tx), np.floor(t.mv / tv)
        g = 0.035                                                            # grout half-width as a share of a tile
        gx_ = np.minimum(fx_, 1 - fx_) * tx; gv_ = np.minimum(fv_, 1 - fv_) * tv
        grout = 1.0 - smooth(np.minimum(gx_, gv_), 0.004, 0.011)
        hsh = np.sin(ix * 12.9898 + iv * 78.233 + row * 3.1 + col * 7.7) * 43758.5453; hsh = hsh - np.floor(hsh)
        t.b *= (0.86 + 0.14 * hsh + 0.05 * (lo - 0.5))[..., None]
        t.tint((0.86, 0.84, 0.88))
        band = (iv == 4) | (iv == 5)                                         # a dado band of two courses in a deeper glaze
        t.tint((0.50, 0.26, 0.34), band.astype(np.float32))
        acc = (hsh > 0.972) & ~band
        t.tint((0.62, 0.42, 0.50), acc.astype(np.float32))
        t.r *= 0.22 + 0.10 * hsh                                             # glossy
        bev = smooth(np.minimum(gx_, gv_), 0.004, 0.02); t.z += 1.2 * bev
        miss = (hsh < 0.014) & (grout < 0.5)                                 # a tile gone: the bed shows
        t.blend((0.42, 0.40, 0.42), miss.astype(np.float32) * 0.95); t.z -= 1.2 * miss; t.r = np.where(miss, 0.95, t.r)
        t.blend((0.50, 0.48, 0.50), grout * 0.9); t.r = t.r * (1 - grout) + 0.9 * grout; t.ao *= 1.0 - 0.35 * grout
        drips(t, t.rng.uniform(0.1, 2.3, 3), 3.2, 0.8, 0.03, 0.22)
        if windowed:
            drips(t, (WIN[0] - 0.04, WIN[1] + 0.04), WIN[2] - 0.05, 0.6, 0.03, 0.3)
        joints(t, 0.5, 0.8, 0.0)
    if base:
        rising_grime(t, 1.0 if row != 3 else 0.7)
    t.commit(1.0 if row != 2 else 0.8)


def planks():
    w, h = 1024 * S, 512 * S
    t = Tile("planks", 1024 * S, 0, w, h, (12.8, 6.4), 7)
    ph = 16 * S                                                              # one plank: 0.2 m
    row = np.floor(t.yy / ph); fy = (t.yy % ph) / ph
    rr = np.random.default_rng(71).random(64)
    tone = 0.74 + 0.26 * rr[row.astype(int)]
    grain = noise(h, w, 5 * S, t.rng, 3, (14.0, 0.5))
    t.b *= (tone * (0.86 + 0.24 * grain))[..., None]
    t.tint((0.98, 0.93, 0.88))
    gap = 1.0 - smooth(np.minimum(fy, 1 - fy) * 0.2, 0.002, 0.007)
    t.b *= (1.0 - 0.8 * gap)[..., None]; t.z -= 5.0 * gap; t.ao *= 1.0 - 0.7 * gap
    t.z += 1.2 * (0.5 - np.abs(fy - 0.5)) + 0.5 * (grain - 0.5)             # each plank a little crowned
    r2 = np.random.default_rng(72)
    for k in range(32):                                                      # butt joints with two nail heads
        for _ in range(3):
            jx = int(r2.uniform(0.03, 0.97) * w); y0 = k * ph
            t.b[y0:y0 + ph, jx:jx + max(1, S)] *= 0.45; t.z[y0:y0 + ph, jx:jx + max(1, S)] -= 3.0
            for off in (-0.05, 0.05):
                for fv in (0.25, 0.75):
                    cx, cy = jx + off * PPM, y0 + fv * ph
                    d2 = (t.xx - cx) ** 2 + (t.yy - cy) ** 2
                    dot = np.exp(-d2 / (1.3 * S) ** 2); t.b *= (1.0 - 0.5 * dot)[..., None]; t.z -= 0.8 * dot
    wet = smooth(noise(h, w, 150 * S, t.rng, 3) + 0.25 * (rr[row.astype(int)] - 0.5), 0.42, 0.62)      # long wet patches, plank by plank
    t.b *= (1.0 - 0.16 * wet)[..., None]
    t.r *= (0.92 - 0.14 * grain) * (1.0 - 0.62 * wet)
    t.r = np.where(gap > 0.5, 1.0, t.r)
    t.commit(0.7)


def plate(name, x, y, seed):
    w = h = 256 * S
    t = Tile(name, x, y, w, h, (3.2, 3.2), seed)
    lo = noise(h, w, 80 * S, t.rng, 3); fine = noise(h, w, 3 * S, t.rng, 2)
    t.b *= (0.84 + 0.2 * lo + 0.06 * (fine - 0.5))[..., None]
    t.tint((0.92, 0.93, 0.98))
    d = 0.1                                                                  # grip studs every 10 cm, staggered
    u_, v_ = t.mx / d, t.mv / d
    u2 = u_ + 0.5 * (np.floor(v_) % 2)
    stud = np.exp(-(((u2 % 1.0) - 0.5) ** 2 + ((v_ % 1.0) - 0.5) ** 2) / 0.035)
    t.z += 1.6 * stud; t.b *= (1.0 + 0.10 * stud)[..., None]
    edge = np.minimum.reduce([t.mx, 3.2 - t.mx, t.mv, 3.2 - t.mv])
    seam = 1.0 - smooth(edge, 0.006, 0.02)
    t.b *= (1.0 - 0.7 * seam)[..., None]; t.z -= 4.0 * seam; t.ao *= 1.0 - 0.6 * seam
    wear = smooth(edge, 0.02, 0.25)
    t.b *= (0.82 + 0.18 * wear)[..., None]                                   # dirt along the seams
    for bx in np.arange(0.2, 3.2, 0.4):                                      # bolts along two edges
        for (cx, cv) in ((bx, 0.09), (bx, 3.11), (0.09, bx), (3.11, bx)):
            dot = np.exp(-((t.mx - cx) ** 2 + (t.mv - cv) ** 2) / 0.016 ** 2); t.b *= (1.0 - 0.4 * dot)[..., None]; t.z += 2.0 * dot
    wet = smooth(noise(h, w, 70 * S, t.rng, 3), 0.48, 0.66)
    rust = smooth(noise(h, w, 30 * S, t.rng, 4) + 0.3 * (1 - wear), 0.78, 0.92)
    t.blend(np.array([0.70, 0.38, 0.24]) * (0.7 + 0.5 * fine[..., None]), rust * 0.8)
    t.b *= (1.0 - 0.14 * wet)[..., None]
    t.r *= (0.9 - 0.12 * fine) * (1.0 - 0.6 * wet) * (1.0 - 0.15 * stud); t.r = np.maximum(t.r, rust)
    t.m *= 1.0 - rust
    t.commit(0.7)


def pavers():
    w, h = 512 * S, 256 * S
    t = Tile("pavers", 1536 * S, 512 * S, w, h, (6.4, 3.2), 31)
    lo = noise(h, w, 70 * S, t.rng, 3); fine = noise(h, w, 3 * S, t.rng, 2)
    course = 0.4                                                             # four rings of 0.8 m ... as eight courses of 0.4 m slabs, 0.8 m long, half bond
    iv = np.floor(t.mv / course); fv = (t.mv / course) % 1.0
    u_ = t.mx / 0.8 + 0.5 * (iv % 2); iu = np.floor(u_); fu = u_ % 1.0
    hsh = np.sin(iu * 12.9898 + iv * 78.233) * 43758.5453; hsh = hsh - np.floor(hsh)
    t.b *= (0.78 + 0.22 * hsh + 0.14 * (lo - 0.5) + 0.05 * (fine - 0.5))[..., None]
    t.tint((0.95, 0.93, 0.97))
    gx_ = np.minimum(fu, 1 - fu) * 0.8; gv_ = np.minimum(fv, 1 - fv) * course
    grout = 1.0 - smooth(np.minimum(gx_, gv_), 0.004, 0.014)
    t.b *= (1.0 - 0.72 * grout)[..., None]; t.z -= 3.0 * grout; t.ao *= 1.0 - 0.5 * grout
    t.z += 0.5 * (fine - 0.5) + 0.8 * (hsh - 0.5)
    wet = smooth(noise(h, w, 90 * S, t.rng, 3) + 0.2 * (hsh - 0.5), 0.45, 0.62)
    t.b *= (1.0 - 0.14 * wet)[..., None]
    t.r *= (0.9 - 0.1 * fine) * (1.0 - 0.6 * wet); t.r = np.where(grout > 0.5, 1.0, t.r)
    moss = smooth(noise(h, w, 18 * S, t.rng, 3) * grout + 0.25 * noise(h, w, 40 * S, t.rng, 2), 0.5, 0.7)
    t.tint((0.62, 0.74, 0.70), moss, 0.7)
    t.commit(0.7)


def patch(name, x, y, seed, kind):
    w = h = 256 * S
    t = Tile(name, x, y, w, h, (3.2, 3.2), seed)
    lo = noise(h, w, 70 * S, t.rng, 3); fine = noise(h, w, 3 * S, t.rng, 2); mid = noise(h, w, 16 * S, t.rng, 3)
    if kind == "trim":                                                       # cast concrete / painted timber: slabs, frames, sills
        t.b *= (0.86 + 0.16 * lo + 0.08 * (fine - 0.5))[..., None]
        streak = noise(h, w, 9 * S, t.rng, 3, (0.4, 6.0))
        t.b *= (0.9 + 0.12 * streak)[..., None]
        t.z += 0.4 * (fine - 0.5) + 0.5 * (mid - 0.5)
        pit = smooth(fine * mid, 0.42, 0.5); t.b *= (1.0 - 0.3 * pit)[..., None]; t.z -= 0.6 * pit
        t.r *= 0.86 + 0.14 * fine
    elif kind == "metal":                                                    # galvanised and painted steel: rails, stilts, pipes, tanks
        t.b *= (0.84 + 0.2 * lo + 0.1 * (fine - 0.5))[..., None]
        streak = noise(h, w, 7 * S, t.rng, 3, (0.35, 8.0))
        t.b *= (0.88 + 0.16 * streak)[..., None]
        rust = smooth(mid * 0.6 + streak * 0.5 + lo * 0.25, 0.80, 0.92)
        t.blend(np.array([0.85, 0.42, 0.22]) * (0.7 + 0.5 * fine[..., None]), rust * 0.85)
        t.r *= (0.72 + 0.2 * fine) * (1 - rust) + rust; t.m *= 1.0 - 0.9 * rust
        t.z += 0.3 * (fine - 0.5) + 0.5 * rust
    elif kind == "roof":                                                     # felt and gravel
        t.b *= (0.72 + 0.2 * lo + 0.22 * (fine - 0.5))[..., None]
        t.tint((0.9, 0.9, 1.0))
        lap = np.exp(-(((t.mx % 1.0) - 0.5) / 0.012) ** 2); t.b *= (1.0 - 0.4 * lap)[..., None]; t.z += 1.2 * lap
        pond = smooth(noise(h, w, 60 * S, t.rng, 2), 0.55, 0.7); t.b *= (1.0 - 0.3 * pond)[..., None]; t.r *= 1.0 - 0.55 * pond
        t.z += 1.0 * (fine - 0.5)
    else:                                                                    # neutral: white, flat, the factors untouched
        pass
    t.commit(0.8)


def srgb(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055)


for r_ in range(4):
    for c_ in range(5):
        facade_tile(r_, c_)
# the spare 64 px column beside the cell tiles carries each row's blank tile on (so a mip never pulls in white)
for r_ in range(4):
    x0 = 5 * TW; ww = W // 2 - x0
    for arr in (BASE, HGT, RGH, AO, MET, NRM):
        arr[r_ * TH:(r_ + 1) * TH, x0:x0 + ww] = arr[r_ * TH:(r_ + 1) * TH, 2 * TW + 8 * S:2 * TW + 8 * S + ww]
planks()
plate("plate_a", 1024 * S, 512 * S, 41); plate("plate_b", 1280 * S, 512 * S, 42)
pavers()
patch("trim", 1024 * S, 768 * S, 51, "trim"); patch("metal", 1280 * S, 768 * S, 52, "metal")
patch("roof", 1536 * S, 768 * S, 53, "roof"); patch("neutral", 1792 * S, 768 * S, 54, "neutral")

Image.fromarray((srgb(BASE) * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, "nd_surface_base.png"), optimize=True)
Image.fromarray(((NRM * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, "nd_surface_normal.png"), optimize=True)
Image.fromarray((np.stack([AO, RGH, MET], -1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, "nd_surface_orm.png"), optimize=True)
doc = {"size": [W, H], "px_per_m": PPM, "cell_px": [TW, TH], "cell_m": [2.4, 3.2],
       "rows": ["render_light", "render_dark", "sheet", "tile"], "cols": ["win_a", "win_b", "blank", "base_win", "base_blank"],
       "regions": REG, "files": {"base": "nd_surface_base.png", "normal": "nd_surface_normal.png", "orm": "nd_surface_orm.png"}}
json.dump(doc, open(os.path.join(OUT, "nd_surface.json"), "w"), indent=1)
print("ATLAS", W, H, "regions", len(REG), "->", OUT, {k: os.path.getsize(os.path.join(OUT, v)) for k, v in doc["files"].items()})
