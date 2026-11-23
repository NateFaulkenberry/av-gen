"""Neon Dreams iteration 14 (owner msg 206: "we're missing audio reactivity on the crash hits -- add a fireworks animation
that shoots up from places in the city on the crash notes: big vibrant fireworks that rain down rainbow particles,
illuminating the environment").

What AV Gen has: Rainbow World's fireworks (examples/rainbow-world/build_world.py, gates 5-7) are a technique, not a
system -- instanced spark meshes pushed out and down by scene fields that ride its scan front. Here the shells are
events in time, not a front in space, so the same recipe (a white-hot ignition, sparks flying out and slowing (drag),
arcing down (gravity), cooling from white through the look's colours, crackling as they die) is built on the engine's
GPU particle systems, which already have every term: a one-frame `burst`, full-sphere `spread`, `drag`, `gravity`,
colour and size over life (`colorCurve`, `sizeCurve`), `pulseRate` (the crackle) and `velocityStretch` (streaks).

Per crash (the Crash stem's onsets at their attacks, AU.EVENTS): ONE shell (owner msg 218), BURSTING on the crash, of
its own type (msg 220: peony, chrysanthemum, willow, crossette, strobe, palm, ring -- TYPES); the rocket
leaves the town (the highest layout instance under it) 1.05 s before, its emitter rising on an ease-out with a trail
of sparks. Each shell is three star systems (three hue runs through the rainbow, so one shell holds every colour, and
each star's own colour walks the spectrum as it falls), a white flash core, and a point light at the burst (a fast
attack, a 1.6 s decay) that lights the roofs, decks, clouds and crowd below. The sites are chosen where the lens looks
at that second; it16 (msg 243: "some of the fireworks are clipped behind clouds") each is tested against the cloud
lobes, the mist sheets and the town's boxes along the sight lines over the stars' life, and the clearest wins
(sites_for, CloudModel).

Everything is a timeline track keyed on the measured times: the render steps the particle simulation frame by frame
from the start, so a play and a full render agree; a still started in the middle of a shell's life does not have the
earlier frames' stars (as for every particle system in the scene).
"""
from __future__ import annotations

import math
import random

import numpy as np

import nd_audio as AU

RISE = 1.05            # s from launch to burst
STARS_PER_SYSTEM = 420  # three systems a shell
LIGHT = {"intensity": 4000.0, "range": 200.0}
STAR = {
    "capacity": 2048, "shape": "sphere", "extent": [0.4, 0.4, 0.4], "spawnRate": 0.0, "burst": 0.0,
    "lifetimeMin": 1.6, "lifetimeMax": 2.7, "direction": [0.0, 1.0, 0.0], "spread": 1.0,
    "speedMin": 15.0, "speedMax": 27.0, "gravity": [0.0, -7.5, 0.0], "drag": 1.15, "turbulence": 0.6,
    "turbulenceScale": 0.08, "turbulenceSpeed": 0.4, "sizeStart": 0.7, "sizeEnd": 0.12, "emissive": 7.0,
    "blend": "additive", "softness": 0.0, "pulseRate": 7.0, "pulseDepth": 0.55, "velocityStretch": 0.8,
    "stretchMax": 3.5, "stretchMin": 0.05}
ROCKET = {
    "capacity": 1024, "shape": "sphere", "extent": [0.15, 0.15, 0.15], "spawnRate": 0.0, "burst": 0.0,
    "lifetimeMin": 0.35, "lifetimeMax": 0.7, "direction": [0.0, -1.0, 0.0], "spread": 0.25,
    "speedMin": 1.5, "speedMax": 4.0, "gravity": [0.0, -3.0, 0.0], "drag": 1.5, "turbulence": 0.4,
    "turbulenceScale": 0.5, "turbulenceSpeed": 0.5, "sizeStart": 0.45, "sizeEnd": 0.05,
    "colorStart": [1.0, 0.82, 0.55, 1.0], "colorEnd": [1.0, 0.35, 0.1, 0.0], "emissive": 14.0,
    "blend": "additive", "softness": 0.0, "pulseRate": 14.0, "pulseDepth": 0.6}
FLASH = {
    "capacity": 256, "shape": "sphere", "extent": [0.5, 0.5, 0.5], "spawnRate": 0.0, "burst": 0.0,
    "lifetimeMin": 0.08, "lifetimeMax": 0.16, "direction": [0.0, 1.0, 0.0], "spread": 1.0,
    "speedMin": 0.5, "speedMax": 2.0, "gravity": [0.0, 0.0, 0.0], "drag": 0.5, "turbulence": 0.0,
    "sizeStart": 2.6, "sizeEnd": 4.5, "colorStart": [1.0, 0.97, 0.92, 0.8], "colorEnd": [1.0, 0.8, 0.9, 0.0],
    "emissive": 5.0, "blend": "additive", "softness": 0.0}


# it16 (owner msg 243: "some of the fireworks are clipped behind clouds"; at 58.0 s "firework rendering behind cloud"):
# a shell's site is tested against the cloud it would be seen through. The clouds are static lobes (centre, radius,
# per-axis scale, density, a soft edge fraction) and the mist sheets (slabs over a disc); the march's noise and
# erosion only ever eat the lobes inward, so a lobe body is an upper bound of what hides a burst. The optical depth of
# a sight line is the sum over lobes of density x the chord through the lobe's body (radius x (1 - soft / 2)) plus a
# thin halo to 1.5 radii, at most HALO_MAX past the body (the ADR-2123 mist), x the block's extinction; a mist sheet adds its density x coverage over
# its chord. A burst is seen when the sight lines to its heart and to six points 16 m round it all stay under TAU_MAX.
EXTINCTION = 0.15
TAU_MAX = 0.35          # transmittance >= 0.70 to the burst's heart
RING_TAU_MAX = 0.9      # and >= 0.4 to its sides and top
BURST_R = 16.0          # the stars' reach (speed / drag ~ 15-20 m) for the ring of sight lines
MIST_SHARE = 0.15       # the lobes' mist (lobeMist) between the body and the halo
HALO_MAX = 12.0         # m: the halo reaches 1.5 radii, but a big lobe's mist is patches near its skin, not a shell
SHEET_SHARE = 0.04      # a mist sheet's tattered share of its density (the sheets read as thin threads in the film: the it16 shells seen through them)


def _chords(a, d, ln, c, rad):
    """Per segment (rows of d, lengths ln) and ellipsoid (rows of c, rad): the length of a + s d, s in 0..ln, inside."""
    o = (a[None, None, :] - c[None, :, :]) / rad[None, :, :]          # (1, N, 3)
    dd = d[:, None, :] / rad[None, :, :]                                # (M, N, 3)
    A = (dd * dd).sum(-1)
    B = 2.0 * (o * dd).sum(-1)
    C = (o * o).sum(-1) - 1.0
    disc = B * B - 4.0 * A * C
    q = np.sqrt(np.maximum(disc, 0.0))
    s0 = (-B - q) / (2.0 * A)
    s1 = (-B + q) / (2.0 * A)
    L = np.minimum(s1, ln[:, None]) - np.maximum(s0, 0.0)
    return np.where(disc > 0.0, np.maximum(L, 0.0), 0.0)


class CloudModel:
    def __init__(self, clouds):
        c, body, halo, dens = [], [], [], []
        for l in list(clouds.get("lobes", [])) + list(clouds.get("farLobes", [])):
            sc = l.get("scale", [1.0, 1.0, 1.0])
            r = l["r"]
            bd = r * (1.0 - 0.5 * l.get("soft", 0.0))
            hl = bd + min(0.5 * r, HALO_MAX)
            c.append(l["c"])
            body.append([bd * v for v in sc])
            halo.append([hl * v for v in sc])
            dens.append(l.get("density", 1.0))
        self.c, self.body, self.halo = np.array(c, float), np.array(body, float), np.array(halo, float)
        self.dens = np.array(dens, float)
        self.bound = self.halo.max(1)
        w = clouds.get("wisps") or {}
        self.sheets = [l for l in w.get("layers", [])] if w.get("enabled", True) else []

    def taus(self, a, pts):
        """The optical depth of the sight lines from a to each of pts."""
        a = np.asarray(a, float)
        P = np.asarray(pts, float)
        d = P - a
        ln = np.linalg.norm(d, axis=1) + 1e-6
        d = d / ln[:, None]
        # only the lobes whose bound reaches the segments' hull (a sphere round them)
        mid = (P.mean(0) + a) * 0.5
        rad = np.linalg.norm(P - mid, axis=1).max() + np.linalg.norm(a - mid) * 0.0 + 0.5 * ln.max()
        keep = np.linalg.norm(self.c - mid, axis=1) < rad + self.bound
        t = np.zeros(len(P))
        if keep.any():
            cb = _chords(a, d, ln, self.c[keep], self.body[keep])
            ch = _chords(a, d, ln, self.c[keep], self.halo[keep])
            t += EXTINCTION * ((cb + MIST_SHARE * np.maximum(ch - cb, 0.0)) * self.dens[keep][None, :]).sum(1)
        for j in range(len(P)):
            t[j] += self._sheets(a, P[j])
        return t

    def _sheets(self, a, b):
        """A mist sheet: a slab over a disc; the chord in the slab, counted where it is over the disc."""
        d = b - a
        ln = float(np.linalg.norm(d)) or 1e-6
        d = d / ln
        t = 0.0
        for sh in self.sheets:
            lo, hi = sh["base"], sh["top"]
            if abs(d[1]) < 1e-6:
                s0, s1 = (0.0, ln) if lo <= a[1] <= hi else (0.0, 0.0)
            else:
                s0, s1 = sorted(((lo - a[1]) / d[1], (hi - a[1]) / d[1]))
                s0, s1 = max(s0, 0.0), min(s1, ln)
            if s1 <= s0:
                continue
            n = 6
            inside = 0
            for k in range(n):
                s = s0 + (s1 - s0) * (k + 0.5) / n
                x, z = a[0] + d[0] * s, a[2] + d[2] * s
                if math.hypot(x - sh["centre"][0], z - sh["centre"][1]) < sh["radius"]:
                    inside += 1
            t += EXTINCTION * sh["density"] * SHEET_SHARE * (s1 - s0) * inside / n
        return t

    def tau(self, a, b):
        return float(self.taus(a, [b])[0])

    def inside(self, p):
        """True when p is in a lobe's body (the lens inside cloud sees nothing, whatever the site)."""
        q = (np.asarray(p, float)[None, :] - self.c) / self.body
        return bool(((q * q).sum(1) < 1.0).any())

    def sight(self, eye, burst):
        """The worst optical depth over the sight lines to the burst's heart and six points BURST_R round it."""
        return float(self.taus(eye, [burst] + _ring(burst)).max())


def hue(h, s=1.0, v=1.0):
    h = (h % 1.0) * 6.0
    i = int(h)
    f = h - i
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    return [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6]


def star_curves(h0):
    """White-hot at ignition, then a run through a third of the spectrum (each system its own third), fading."""
    col = [{"t": 0.0, "color": [1.0, 0.97, 0.94]}, {"t": 0.08, "color": hue(h0, 0.35)}]
    for k in range(1, 6):
        col.append({"t": round(0.08 + 0.92 * k / 5, 3), "color": hue(h0 + 0.075 * k, 0.95)})
    opa = [{"t": 0.0, "value": 1.0}, {"t": 0.55, "value": 0.9}, {"t": 1.0, "value": 0.0}]
    size = [{"t": 0.0, "value": 0.9}, {"t": 0.12, "value": 0.6}, {"t": 1.0, "value": 0.1}]
    return {"colorCurve": col, "opacityCurve": opa, "sizeCurve": size}


def ramp(*cols):
    """A colour curve: white-hot only at ignition (to 6 % of the life), then `cols[1:]` evenly over the rest."""
    rest = cols[1:]
    keys = [{"t": 0.0, "color": list(cols[0])}]
    for i, c in enumerate(rest):
        keys.append({"t": round(0.06 + 0.94 * i / max(len(rest) - 1, 1), 3), "color": list(c)})
    return keys


WHITE = (1.0, 0.97, 0.94)
# it14 (owner msg 220: "make the fireworks different colours too -- they all look too similar"): seven shell types,
# one per crash. Each: its star systems (overrides on STAR, a colour curve, `count` stars, `delay` s after the burst),
# the burst light's colour, and a name. Deterministic: the order is a seeded shuffle (Random(220)).
TYPES = [
    {"name": "magenta peony", "light": [1.0, 0.25, 0.75], "systems": [
        {"count": 900, "colorCurve": ramp(WHITE, (1.0, 0.45, 0.85), (1.0, 0.1, 0.65), (0.75, 0.0, 0.45)),
         "speedMin": 18.0, "speedMax": 24.0, "velocityStretch": 0.3}]},
    {"name": "electric-blue chrysanthemum", "light": [0.3, 0.55, 1.0], "systems": [
        {"count": 1100, "colorCurve": ramp(WHITE, (0.55, 0.8, 1.0), (0.15, 0.45, 1.0), (0.05, 0.2, 0.9)),
         "speedMin": 20.0, "speedMax": 28.0, "velocityStretch": 1.6, "stretchMax": 6.0, "drag": 0.9}]},
    {"name": "gold willow", "light": [1.0, 0.72, 0.3], "systems": [
        {"count": 700, "colorCurve": ramp(WHITE, (1.0, 0.85, 0.45), (1.0, 0.6, 0.15), (0.7, 0.3, 0.05)),
         "speedMin": 12.0, "speedMax": 17.0, "lifetimeMin": 3.2, "lifetimeMax": 4.4, "drag": 2.0,
         "gravity": [0.0, -5.0, 0.0], "velocityStretch": 2.5, "stretchMax": 8.0, "pulseRate": 3.0, "pulseDepth": 0.2,
         "sizeCurve": [{"t": 0.0, "value": 0.8}, {"t": 1.0, "value": 0.35}]}]},
    {"name": "violet-to-pink crossette", "light": [0.8, 0.4, 1.0], "systems": [
        {"count": 420, "colorCurve": ramp(WHITE, (0.7, 0.45, 1.0), (0.55, 0.2, 1.0)), "speedMin": 22.0,
         "speedMax": 26.0, "lifetimeMin": 0.5, "lifetimeMax": 0.75, "drag": 0.6},
        {"count": 900, "delay": 0.55, "colorCurve": ramp(WHITE, (1.0, 0.55, 0.85), (1.0, 0.3, 0.7), (0.8, 0.15, 0.5)),
         "speedMin": 9.0, "speedMax": 16.0, "extent": [9.0, 9.0, 9.0], "lifetimeMin": 1.4, "lifetimeMax": 2.2}]},
    {"name": "cyan/white strobe crackle", "light": [0.6, 0.95, 1.0], "systems": [
        {"count": 1000, "colorCurve": ramp(WHITE, (0.85, 1.0, 1.0), (0.4, 0.95, 1.0), (0.9, 1.0, 1.0)),
         "speedMin": 16.0, "speedMax": 24.0, "pulseRate": 16.0, "pulseDepth": 1.0, "pulseSharpness": 4.0,
         "velocityStretch": 0.1}]},
    {"name": "red/amber palm", "light": [1.0, 0.35, 0.12], "systems": [
        {"count": 90, "colorCurve": ramp(WHITE, (1.0, 0.65, 0.2), (1.0, 0.25, 0.08), (0.8, 0.1, 0.03)),
         "speedMin": 26.0, "speedMax": 32.0, "spread": 0.75, "lifetimeMin": 2.2, "lifetimeMax": 2.8, "drag": 1.4,
         "velocityStretch": 2.2, "stretchMax": 9.0, "sizeCurve": [{"t": 0.0, "value": 1.6}, {"t": 1.0, "value": 0.5}]},
        {"count": 500, "colorCurve": ramp(WHITE, (1.0, 0.5, 0.15), (0.9, 0.18, 0.05)), "speedMin": 6.0,
         "speedMax": 12.0, "lifetimeMin": 1.2, "lifetimeMax": 1.8}]},
    {"name": "rainbow ring", "light": [0.9, 0.8, 1.0], "systems": [
        dict({"count": 500, "speedMin": 21.0, "speedMax": 23.0, "spread": 0.06, "direction": [0.0, 0.0, 1.0]},
             **star_curves(0.0)),
        dict({"count": 500, "speedMin": 21.0, "speedMax": 23.0, "spread": 0.06, "direction": [0.0, 0.0, -1.0]},
             **star_curves(0.33)),
        dict({"count": 500, "speedMin": 15.0, "speedMax": 23.0, "spread": 1.0}, **star_curves(0.66))]},
]


def _track(target, keys):
    return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "keys": keys}


# it16 (owner msg 243: "a cymbal hit / firework is missed here" at 24.5 s): the Crash stem holds eleven hits, the
# analysis's onsets only seven. The other four are a second cymbal (peaks -15 dBFS against -8..-11 for the section
# crashes, a longer ring: -26 dB a second later where a crash is down to -33) on beats 35, 65 and 83, and a full crash
# 0.71 s after the 47.02 s one (a 20 ms-hop rise of 10 dB in the stem: 24.56, 45.62, 47.74, 58.24 s; the attack read
# at 10 % of the hit's peak, as the analysis reads its onsets). Each gets its own shell.
EXTRA_CRASHES = [(24.567, 0.6), (45.622, 0.6), (47.73, 1.0), (58.255, 0.6)]


def crashes():
    return sorted([(t, v) for t, v in AU.EVENTS["stems"]["crash"]["onsets"]] + EXTRA_CRASHES)


def ground_at(layout, x, z, r=26.0):
    """The highest layout instance base within r metres of (x, z), or None over open cloud."""
    best = None
    for inst in layout["instances"]:
        p = inst["pos"]
        if abs(p[0] - x) < r and abs(p[2] - z) < r and math.hypot(p[0] - x, p[2] - z) < r:
            best = p[1] if best is None else max(best, p[1])
    return best


# it16 render check: the 2.81 s shell hid behind a gondola pylon (street/) and the 35.79 s one behind the street's
# overhead gangway (decks/): those are solid too
SOLID = ("buildings/", "landmarks/", "condensery/", "far/", "decks/", "street/")
OCCLUDED_MAX = 3        # a town box may cut 3 of the 5 sight lines round the heart (a street's walls frame a burst), never the heart's (it counts 10)


def occluders(layout, kit):
    """The town as oriented boxes: each solid layout instance's kit bounds (GLB: Y up), scaled, at its position and
    yaw. Returns (positions (N, 3), yaw cos, yaw sin, local lo (N, 3), local hi (N, 3))."""
    bounds = {a["asset"]: a["bounds_m"] for a in kit.get("assets", []) if a.get("bounds_m")}
    P, C, S, LO, HI = [], [], [], [], []
    for inst in layout["instances"]:
        b = bounds.get(inst["asset"])
        if not b or not inst["asset"].startswith(SOLID):   # trees, umbels and props are see-through at 100 m
            continue
        sc = inst.get("scale", [1.0, 1.0, 1.0])
        yaw = math.radians(inst.get("rot_y_deg", 0.0))
        P.append(inst["pos"])
        C.append(math.cos(yaw))
        S.append(math.sin(yaw))
        LO.append([b[0] * sc[0], b[1] * sc[1], b[2] * sc[2]])
        HI.append([b[3] * sc[0], b[4] * sc[1], b[5] * sc[2]])
    return tuple(np.array(v, float).reshape(-1, 3) if k in (0, 3, 4) else np.array(v, float)
                 for k, v in enumerate((P, C, S, LO, HI)))


def _local(boxes, q):
    """World point q in every box's own frame (N, 3)."""
    P, C, S = boxes[0], boxes[1], boxes[2]
    v = np.asarray(q, float)[None, :] - P
    return np.stack([C * v[:, 0] - S * v[:, 2], v[:, 1], S * v[:, 0] + C * v[:, 2]], 1)


def _hits_boxes(a, b, boxes, keep=None):
    """The segment a -> b crosses any of the oriented boxes (slab test in each box's frame)."""
    if boxes is None or len(boxes[0]) == 0:
        return False
    la, lb = _local(boxes, a), _local(boxes, b)
    lo, hi = boxes[3], boxes[4]
    if keep is not None:
        la, lb, lo, hi = la[keep], lb[keep], lo[keep], hi[keep]
    d = lb - la
    d = np.where(np.abs(d) < 1e-9, 1e-9, d)
    u0 = (lo - la) / d
    u1 = (hi - la) / d
    t0 = np.maximum(np.minimum(u0, u1).max(1), 0.0)
    t1 = np.minimum(np.maximum(u0, u1).min(1), 1.0)
    return bool((t0 <= t1).any())


SAMPLES = (0.1, 0.5, 0.9, 1.3, 1.7, 2.1, 2.5)   # s after the burst: the stars' life the lens must see
FRAME_KEEP = 0.8        # the burst's heart inside 80 % of the frame's half-width and half-height, its 16 m ring inside it
                        # (behind the band, msg 248: the heart only -- a near shell may fill the sky over the stage)


def _view(cam_at, t, p):
    """(eye, |x| / half-width, |y| / half-height) of world point p in the lens at t; inf behind the lens."""
    eye, d, roll, fov = cam_at(t)
    v = [p[i] - eye[i] for i in range(3)]
    z = sum(v[i] * d[i] for i in range(3))
    if z <= 1.0:
        return eye, float("inf"), float("inf")
    rgt = [-d[2], 0.0, d[0]]
    ln = math.hypot(rgt[0], rgt[2]) or 1.0
    rgt = [c / ln for c in rgt]
    up = [rgt[1] * d[2] - rgt[2] * d[1], rgt[2] * d[0] - rgt[0] * d[2], rgt[0] * d[1] - rgt[1] * d[0]]
    x = sum(v[i] * rgt[i] for i in range(3)) / z
    y = sum(v[i] * up[i] for i in range(3)) / z
    r = math.radians(roll)   # the bank turns the frame
    x, y = x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r)
    tv = math.tan(math.radians(fov) * 0.5)
    return eye, abs(x) / (tv * 16.0 / 9.0), abs(y) / tv


UP_RING = ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, -1))   # the stars fall: below the heart may be cloud


def _ring(burst, dirs=UP_RING):
    return [[burst[0] + BURST_R * o[0], burst[1] + BURST_R * o[1], burst[2] + BURST_R * o[2]] for o in dirs]


def visibility(t, burst, cam_at, clouds, boxes, heart_only=False):
    """Per sample second after the burst: (dt, the optical depth to the heart, the worst over the five points BURST_R
    round it (sides and top: the stars rain down, into cloud if they like), how many of the sight lines a town box
    cuts (the heart's counts 10), the frame fraction the burst and its ring (below too) reach, the lens inside cloud)."""
    rows = []
    pts = [burst] + _ring(burst)
    for dt in SAMPLES:
        eye, fx, fy = _view(cam_at, t + dt, burst)
        ring = [_view(cam_at, t + dt, q) for q in _ring(burst, UP_RING + ((0, -1, 0),))]
        frame = max([fx / FRAME_KEEP, fy / FRAME_KEEP] + ([] if heart_only else [max(r[1], r[2]) for r in ring]))
        taus = clouds.taus(eye, pts) if clouds else np.zeros(len(pts))
        # a box round the lens itself is the street it flies down (a kit's bounds hold its signs and balconies), not a wall: it does not count
        out = None
        if boxes is not None and len(boxes[0]):
            le = _local(boxes, eye)
            out = ~((boxes[3] <= le).all(1) & (le <= boxes[4]).all(1))
        cut = sum((10 if j == 0 else 1) for j, q in enumerate(pts) if _hits_boxes(eye, q, boxes, out))
        rows.append((dt, float(taus[0]), float(taus[1:].max()), cut, frame, clouds.inside(eye) if clouds else False))
    return rows


def seen(rows):
    return [r for r in rows if r[1] <= TAU_MAX and r[2] <= RING_TAU_MAX and r[3] <= OCCLUDED_MAX and r[4] <= 1.0]


CANDIDATES = 500


# msg 248 ("make sure the fireworks are in the background of the band when we focus on them"): a crash in the band
# dwell bursts BEHIND the stage from the lens -- on the lens-to-stage bearing (within 20 % of the half-width; 22 m apart, not 35), 15-80 m
# past the stage, over the halo in the picture (its heart in the frame; the stars may fill it) -- so it blooms in the sky over the band and lights it from behind
BAND_DWELL = (43.6, 51.2)


def sites_for(t, cam_at, layout, rng, clouds=None, boxes=None, taken=(), focus=None):
    """it16 (msg 243): the burst point in clear air and in the lens's view. Candidates spread over the frame (azimuth
    within 62 % of the half-width, 10-85 % of the half-height over the lens's pitch, 55-240 m out, a few to 340 m); each is scored by
    the samples SAMPLES after the burst in which every sight line to the burst and its ring is clear of cloud and town
    and inside the frame (the samples with the lens itself in cloud do not count against it). The first sample's
    clearance weighs most; a tie goes to the nearer burst. Never within 35 m of another shell in the air (`taken`)."""
    # the views the candidates are spread over: the lens at the burst and a second later, or, with the lens still in
    # cloud at the burst (the plug at 56.85 s), the first samples it is out
    views = [t, t + 1.0]
    if clouds is not None and clouds.inside(cam_at(t)[0]):
        out = [t + dt for dt in SAMPLES + (2.9, 3.3) if not clouds.inside(cam_at(t + dt)[0])]
        views = out[:2] or views
    best = None
    for k in range(CANDIDATES):
        eye, d, _roll, fov = cam_at(views[k % len(views)])
        yaw = math.atan2(d[2], d[0])
        pitch = math.asin(max(-1.0, min(1.0, d[1])))
        half_v = math.radians(fov) * 0.5
        half_h = math.atan(math.tan(half_v) * 16.0 / 9.0)
        if focus is not None:   # behind the band: on the bearing to the stage, past it, over its halo
            fx, fz = focus[0] - eye[0], focus[2] - eye[2]
            fd = math.hypot(fx, fz)
            b = math.atan2(fz, fx) + rng.uniform(-0.2, 0.2) * half_h
            horiz = fd + rng.uniform(15.0, 80.0)
            dist = horiz
            # in the frame: from just over the halo's own height in the picture to 90 % of the half-height
            u0 = math.atan2(focus[1] - eye[1], fd) - pitch
            el = pitch + rng.uniform(u0 / half_v + 0.1, 0.9) * half_v
            p = [eye[0] + math.cos(b) * horiz, eye[1] + horiz * math.tan(el), eye[2] + math.sin(b) * horiz]
        else:
            az = rng.uniform(-0.62, 0.62) * half_h
            el = pitch + rng.uniform(0.1, 0.85) * half_v
            dist = rng.uniform(40.0, 240.0) if k < 400 else rng.uniform(240.0, 340.0)
            hx, hz = math.cos(yaw + az), math.sin(yaw + az)
            horiz = dist * math.cos(el)
            p = [eye[0] + hx * horiz, eye[1] + dist * math.sin(el), eye[2] + hz * horiz]
        if any(math.dist(p, q) < (22.0 if focus is not None else 35.0) for q in taken):
            continue
        g = ground_at(layout, p[0], p[2])
        base = (g + 4.0) if g is not None else p[1] - 75.0
        if p[1] - base < 30.0:   # at least a 30 m climb
            p[1] = base + 30.0 + rng.uniform(0.0, 20.0)
        rows = visibility(t, p, cam_at, clouds, boxes, heart_only=focus is not None)
        counted = [r for r in rows if not r[5]] or rows
        ok = seen(counted)
        frames = [r[4] for r in counted if r[4] != float("inf")]
        # seen samples first; among equals the least cloud and town in the way (a shell nothing clears still goes to
        # the clearest air), the frame's middle, then the nearer burst
        haze = sum(min(r[1], 5.0) + 0.5 * min(r[2], 5.0) + 0.5 * r[3] + 2.0 * min(max(r[4] - 1.0, 0.0), 3.0)
                   for r in counted) / len(counted)
        score = len(ok) / len(counted) + (0.5 if ok and ok[0] is counted[0] else 0.0) - 0.2 * haze \
            - dist / 4000.0 - (0.1 * max(frames) if frames else 9.0)
        if best is None or score > best[0]:
            best = (score, p, base, rows)
    _, p, base, rows = best
    return {"burst": [round(v, 2) for v in p],
            "launch": [round(p[0] + rng.uniform(-4, 4), 2), round(base, 2), round(p[2] + rng.uniform(-4, 4), 2)],
            "visibility": [[r[0], round(r[1], 3), round(r[2], 3), r[3], round(min(r[4], 99.0), 3), r[5]] for r in rows]}


def build(cam_at, layout, song=70.18, clouds=None, kit=None):
    """(nodes, lights, tracks, info). it14 (owner msg 218: "only shoot up ONE firework each cymbal hit, and time the
    EXPLOSION with the cymbal hit"): one shell per crash, bursting on its attack; msg 220: each its own type."""
    rng = random.Random(206)
    order = list(range(len(TYPES)))
    random.Random(220).shuffle(order)
    nodes, lights, tracks, shells = [], [], [], []
    # a burst window holding exactly one 30 fps frame's instant: [tb - 0.5 ms, tb - 0.5 ms + 1/30) (it14-16a used
    # [tb, tb + 1/30 - 2 ms), which held none when tb sat less than 2 ms after an instant -- the 24.567 s shell never
    # burst: 24.5667 < tb and 24.6 past the window)
    one = 1.0 / 30.0
    lead = 0.0005
    model = CloudModel(clouds) if clouds else None
    boxes = occluders(layout, kit) if kit else None
    for k, (t, v) in enumerate(crashes()):
        taken = [q["burst"] for q in shells if abs(q["t"] - t) < 4.0]
        st = (layout.get("band") or {}).get("stage")
        focus = None
        if st and BAND_DWELL[0] <= t <= BAND_DWELL[1]:
            focus = [st["centre"][0], st.get("halo_y", st["centre"][1] + 8.0), st["centre"][2]]
        site = sites_for(t, cam_at, layout, rng, model, boxes, taken, focus)
        ty = TYPES[order[k % len(TYPES)]]
        tag = "fw%d" % k
        L, B, tb = site["launch"], site["burst"], round(t, 4)
        shells.append(dict(site, t=tb, type=ty["name"]))
        t0 = tb - RISE
        rocket = tag + "_rocket"
        nodes.append({"name": rocket, "kind": "particles", "position": L, "particles": dict(ROCKET, seed=11 + k)})
        rp = []
        for i in range(13):
            x = i / 12.0
            e = 1.0 - (1.0 - x) ** 2   # fast off the roof, slowing into the burst
            rp.append({"time": round(t0 + RISE * x, 4), "value": [round(L[c] + (B[c] - L[c]) * e, 3) for c in range(3)],
                       "interp": "linear"})
        tracks.append(_track("nodes/%s/position" % rocket, rp))
        tracks.append(_track("particles/%s/spawnRate" % rocket, [
            {"time": 0.0, "value": 0.0, "interp": "step"}, {"time": round(t0, 4), "value": 260.0, "interp": "step"},
            {"time": round(tb - 0.02, 4), "value": 0.0, "interp": "step"}]))
        flash = tag + "_flash"
        nodes.append({"name": flash, "kind": "particles", "position": B,
                      "particles": dict(FLASH, seed=13 + k, colorStart=[min(1.0, 0.6 + 0.4 * c) for c in ty["light"]] + [0.8])})
        tracks.append(_track("particles/%s/burst" % flash, [
            {"time": 0.0, "value": 0.0, "interp": "step"}, {"time": round(tb - lead, 5), "value": 12.0, "interp": "step"},
            {"time": round(tb - lead + one, 5), "value": 0.0, "interp": "step"}]))
        for j, sysd in enumerate(ty["systems"]):
            nm = "%s_star%d" % (tag, j)
            ps = dict(STAR, seed=101 + 7 * k + j)
            ps.update({kk: vv for kk, vv in sysd.items() if kk not in ("count", "delay")})
            ps.setdefault("opacityCurve", [{"t": 0.0, "value": 1.0}, {"t": 0.6, "value": 0.85}, {"t": 1.0, "value": 0.0}])
            if "sizeCurve" not in ps:
                ps["sizeCurve"] = [{"t": 0.0, "value": 0.9}, {"t": 0.12, "value": 0.6}, {"t": 1.0, "value": 0.12}]
            ps["capacity"] = max(2048, int(sysd["count"] * 1.5))
            nodes.append({"name": nm, "kind": "particles", "position": B, "particles": ps})
            tj = round(tb + sysd.get("delay", 0.0), 4)
            tracks.append(_track("particles/%s/burst" % nm, [
                {"time": 0.0, "value": 0.0, "interp": "step"},
                {"time": round(tj - lead, 5), "value": float(sysd["count"]), "interp": "step"},
                {"time": round(tj - lead + one, 5), "value": 0.0, "interp": "step"}]))
        light = tag + "_light"
        I = LIGHT["intensity"]
        lights.append({"name": light, "type": "point", "position": B, "color": ty["light"], "intensity": I,
                       "range": LIGHT["range"], "castsShadow": False, "volumetric": 0.0})
        tracks.append(_track("lights/%s/intensity" % light, [
            {"time": 0.0, "value": 0.0, "interp": "linear"}, {"time": round(tb - 0.002, 4), "value": 0.0, "interp": "linear"},
            {"time": round(tb + 0.05, 4), "value": I, "interp": "linear"},
            {"time": round(tb + 0.35, 4), "value": round(0.45 * I, 1), "interp": "linear"},
            {"time": round(tb + 1.0, 4), "value": round(0.15 * I, 1), "interp": "linear"},
            {"time": round(tb + 1.9, 4), "value": 0.0, "interp": "linear"}]))
    return nodes, lights, tracks, {"shells": shells}
