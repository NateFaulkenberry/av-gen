"""Neon Dreams iteration 13 (owner msg 178: "there are some glitches in the camera path near where the band is"): the
journey camera, baked C2-smooth (imported by build.py).

What it12 did, and what each part cost (measured per frame from `avgen_cast_trace --camera` on the it12 build):
- the spline camera (mode 2) sat at `splineT` keyed LINEARLY every 0.25 s, so its speed was a staircase: acceleration
  spikes of 30-90 m/s^2 every 0.25 s wherever the speed changed (89 m/s^2 at 44.53 s as the camera braked for the band);
- its aim was the point `lookAhead` metres further along the path. Over the band the art keyed 1.5 m, so the lens
  swung with every bend of the path under it: through the turn-and-lift after the dwell (48.3-49.1 s) the yaw rate
  reached -192 deg/s and the yaw acceleration 1000 deg/s^2 -- the camera whipped round;
- the distance-over-time curve (monotone cubic) is only C1, so the acceleration also stepped at each waypoint, and the
  engine's spline table is linear between 1 m samples (the tangent turns in steps);
- the fov track was linear (its rate stepped at each key).

What this does instead: the path is the same centripetal Catmull-Rom through the layout's waypoints, resampled every
0.25 m and smoothed along its length (Gaussian, sigma 1 m: corners become arcs, nothing moves more than a few
centimetres on the straights); the distance over time is the same monotone cubic smoothed in time (sigma 0.3 s), so
speed and acceleration are continuous; the aim is the direction toward the path `look_ahead` metres on (the look-ahead
itself smoothed), and that direction is smoothed in time again (sigma 0.7 s) so no bend can turn the lens faster than
the camera can turn; the bank is the engine's (ADR-1166: 5 deg per radian of turn over the look-ahead) on the smoothed
path; the fov is smoothed (sigma 0.4 s). All of it is sampled at every frame of a 30 fps film into free-camera
(mode 1) tracks: camera/position, camera/target, camera/roll, camera/fov, keyed at each frame's instant, so the render
reads exactly the baked pose. Pure Python (the build runs without numpy).
"""
from __future__ import annotations

import math

FPS = 30.0
PATH_STEP = 0.25        # m between path samples
PATH_SIGMA = 1.0        # m, the path's smoothing along its length (3 m turned the lens off the band 1.2 s early)
S_HZ = 120.0            # samples a second of the distance curve
S_SIGMA = 0.3           # s, the distance curve's smoothing
AIM_SIGMA = 0.7         # s, the aim direction's smoothing
LA_SIGMA = 0.4          # s, the look-ahead's smoothing
FOV_SIGMA = 0.4         # s
BANK = 5.0              # deg of roll per radian the heading turns over the look-ahead (the engine's camera/splineBank)
AIM_DIST = 60.0         # m from the eye to the target point (only its direction matters)


def smooth(xs, sigma_samples):
    """Gaussian smoothing of a list of floats; the ends are extended by odd reflection, so a straight-line trend passes
    unchanged and the ends keep their values."""
    n = len(xs)
    if sigma_samples <= 0 or n < 3:
        return list(xs)
    r = min(int(math.ceil(3.5 * sigma_samples)), n - 1)
    k = [math.exp(-0.5 * (i / sigma_samples) ** 2) for i in range(-r, r + 1)]
    sk = sum(k)
    k = [v / sk for v in k]
    ext = [2 * xs[0] - xs[i] for i in range(r, 0, -1)] + list(xs) + [2 * xs[-1] - xs[n - 2 - i] for i in range(r)]
    return [sum(k[j] * ext[i + j] for j in range(2 * r + 1)) for i in range(n)]


def smooth3(ps, sigma_samples):
    cols = [smooth([p[c] for p in ps], sigma_samples) for c in range(3)]
    return [[cols[0][i], cols[1][i], cols[2][i]] for i in range(len(ps))]


def lerp_table(xs, ys, x):
    """ys at x on a table with ascending, evenly spaced xs."""
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    h = xs[1] - xs[0]
    i = min(int((x - xs[0]) / h), len(xs) - 2)
    u = (x - xs[i]) / h
    a, b = ys[i], ys[i + 1]
    if isinstance(a, list):
        return [a[c] + (b[c] - a[c]) * u for c in range(len(a))]
    return a + (b - a) * u


def piecewise(keys, t):
    if t <= keys[0][0]:
        return keys[0][1]
    for (a, va), (b, vb) in zip(keys, keys[1:]):
        if a <= t <= b:
            return va + (vb - va) * (t - a) / max(b - a, 1e-9)
    return keys[-1][1]


class Camera:
    """The baked journey. `dense`, `total`, `s_at` are build.py's journey(): the Catmull-Rom points every ~4 m, their
    length and the monotone cubic s(t). `look_ahead` and `fov` are [[t, value], ...] (linear keys)."""

    def __init__(self, dense, total, s_at, look_ahead, fov, song, roll_extra=None):
        self.song = song
        # the path, every PATH_STEP metres along the Catmull-Rom polyline, smoothed along its length
        cum = [0.0]
        for k in range(1, len(dense)):
            cum.append(cum[-1] + math.dist(dense[k - 1], dense[k]))
        n = int(total / PATH_STEP) + 1
        self.ss = [i * PATH_STEP for i in range(n)]
        pts, j = [], 0
        for s in self.ss:
            while j < len(cum) - 2 and cum[j + 1] < s:
                j += 1
            u = (s - cum[j]) / max(cum[j + 1] - cum[j], 1e-9)
            pts.append([dense[j][c] + (dense[j + 1][c] - dense[j][c]) * min(max(u, 0.0), 1.0) for c in range(3)])
        self.path = smooth3(pts, PATH_SIGMA / PATH_STEP)
        # distance over time, smoothed in time
        m = int(song * S_HZ) + 1
        self.ts = [i / S_HZ for i in range(m)]
        self.s = smooth([s_at(t) for t in self.ts], S_SIGMA * S_HZ)
        la = smooth([piecewise(look_ahead, t) if look_ahead else 45.0 for t in self.ts], LA_SIGMA * S_HZ)
        # the aim: toward the path look-ahead metres on, smoothed in time
        dirs, turns = [], []
        for i, t in enumerate(self.ts):
            s = self.s[i]
            e = self.pos_s(s)
            a = self.pos_s(s + max(la[i], 0.5))
            if s + la[i] > self.ss[-1]:   # past the end: straight on along the last tangent
                tail = self.tangent_s(self.ss[-1])
                over = s + la[i] - self.ss[-1]
                a = [a[c] + tail[c] * over for c in range(3)]
            d = [a[c] - e[c] for c in range(3)]
            ln = math.sqrt(sum(v * v for v in d)) or 1.0
            dirs.append([v / ln for v in d])
            ta, tb = self.tangent_s(s), self.tangent_s(s + la[i])
            ha, hb = (ta[0], ta[2]), (tb[0], tb[2])
            turn = -math.atan2(ha[0] * hb[1] - ha[1] * hb[0], ha[0] * hb[0] + ha[1] * hb[1])
            turns.append(max(-45.0, min(45.0, -BANK * turn)))
        dirs = smooth3(dirs, AIM_SIGMA * S_HZ)
        self.dirs = []
        for d in dirs:
            ln = math.sqrt(sum(v * v for v in d)) or 1.0
            self.dirs.append([v / ln for v in d])
        # it14 (msg 195): an authored roll added to the bank (the dive banks with the vortex's turn)
        if roll_extra:
            turns = [turns[i] + piecewise(roll_extra, t) for i, t in enumerate(self.ts)]
        self.roll = smooth(turns, AIM_SIGMA * S_HZ)
        self.fov = smooth([piecewise(fov, t) for t in self.ts], FOV_SIGMA * S_HZ)

    def pos_s(self, s):
        return lerp_table(self.ss, self.path, s)

    def tangent_s(self, s, h=1.0):
        a, b = self.pos_s(s - h), self.pos_s(s + h)
        d = [b[c] - a[c] for c in range(3)]
        ln = math.sqrt(sum(v * v for v in d)) or 1.0
        return [v / ln for v in d]

    def at(self, t):
        """(eye, unit aim direction, roll deg, fov deg) at timeline second t."""
        t = min(max(t, 0.0), self.song)
        s = lerp_table(self.ts, self.s, t)
        return self.pos_s(s), lerp_table(self.ts, self.dirs, t), lerp_table(self.ts, self.roll, t), \
            lerp_table(self.ts, self.fov, t)

    def tracks(self):
        """Free-camera tracks keyed at every frame of a 30 fps film (and the song's end)."""
        kp, kt, kr, kf = [], [], [], []
        n = int(self.song * FPS)
        times = [i / FPS for i in range(n + 1)] + ([self.song] if n / FPS < self.song else [])
        for t in times:
            e, d, r, f = self.at(t)
            tt = round(t, 5)
            kp.append({"time": tt, "value": [round(v, 4) for v in e], "interp": "linear"})
            kt.append({"time": tt, "value": [round(e[c] + d[c] * AIM_DIST, 4) for c in range(3)], "interp": "linear"})
            kr.append({"time": tt, "value": round(r, 4), "interp": "linear"})
            kf.append({"time": tt, "value": round(f, 4), "interp": "linear"})

        def track(target, keys):
            return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
                    "loopLength": 0.0, "keys": keys}
        return [track("camera/position", kp), track("camera/target", kt), track("camera/roll", kr),
                track("camera/fov", kf)]
