"""All You Got, art pass 3: the film's builder (pass 2's film_build.Builder, extended).

What pass 3 adds:
  * figures (the tableaux) shown one at a time by `nodes/<name>/visible` tracks; a swap is only ever scheduled when
    neither figure is on screen (`swap()` searches the camera for the moment and fails loudly if there is none);
  * shots whose speed is a smooth curve through timed points (`glide()`: a monotone cubic through (time, path
    metres), sampled densely), so a camera can accelerate into the house's window and decelerate into a room without
    stopping at every point;
  * words tagged with their room for the spatial validator, and a stricter placement test: the whole text rectangle
    plus a margin must have solid wall behind it and a clear line of sight (section 10's breathing room);
  * no camera breathing anywhere (section 20): the builder refuses `camera/breath/*` targets.
"""

from __future__ import annotations

import bisect
import math

import film_build as FB
import pass2_grid as G
from film2 import EnginePath

t = G.t
BEAT1 = G.BAR1 / 4.0
BEAT2 = G.BAR2 / 4.0


def pchip(xs, ys):
    """A monotone cubic (Fritsch-Carlson) through (xs, ys): returns f(x). Monotone data stays monotone, so a
    camera never runs backwards between keys."""
    n = len(xs)
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0:
            m[i] = 0.0
        else:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])

    def f(x):
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        i = min(bisect.bisect_right(xs, x) - 1, n - 2)
        u = (x - xs[i]) / h[i]
        h00, h10, h01, h11 = 2 * u ** 3 - 3 * u ** 2 + 1, u ** 3 - 2 * u ** 2 + u, -2 * u ** 3 + 3 * u ** 2, u ** 3 - u ** 2
        return h00 * ys[i] + h10 * h[i] * m[i] + h01 * ys[i + 1] + h11 * h[i] * m[i + 1]
    return f


class Builder3(FB.Builder):
    def __init__(self, film, add_world, palette_index):
        super().__init__(film, add_world, palette_index)
        self.fig_spans = {}        # figure object -> [(t0, t1)]
        self.figures = set()
        self.swaps = []

    # ---- worlds ---------------------------------------------------------------------------------------
    def room_world(self, key, room):
        w = self.world(key, room)
        for o in room["objects"]:
            if o[2] == "figure":
                self.figures.add(o[0])
        return w

    # ---- camera ----------------------------------------------------------------------------------------
    def glide(self, name, keys, look_keys, nodes_keys=(), extra=(), fov=62.0, sway=0.0):
        """A shot through timed eye points: keys [(time, (x, y, z))]. The speed is a monotone cubic through the
        points' arc lengths, sampled every 0.05 s, so the motion is continuous through every point."""
        times = [k[0] for k in keys]
        eye = [list(map(float, k[1])) for k in keys]
        path = EnginePath(self._padded(eye))
        s = [path.at_point[1 + i] for i in range(len(eye))]
        s0, s1 = s[0], s[-1]
        f = pchip(times, s)
        moves = []
        tt = times[0]
        while tt < times[-1] - 1e-6:
            moves.append((tt, (f(tt) - s0) / (s1 - s0) if s1 > s0 else 0.0))
            tt += 0.05
        moves.append((times[-1], 1.0))
        self.shot(name, times[0], times[-1], eye, None, keys=nodes_keys, extra=extra, look_keys=look_keys, moves=moves,
                  fov=fov, ease_kind="linear", sway=sway)

    @staticmethod
    def _padded(eye):
        """The path film2._build_journey builds for a shot's eye points (a lead-in and a tail), so arc lengths here
        are the engine's."""
        d0 = [b - a for a, b in zip(eye[0], eye[1])]
        L0 = math.sqrt(sum(v * v for v in d0)) or 1.0
        d1 = [b - a for a, b in zip(eye[-2], eye[-1])]
        L1 = math.sqrt(sum(v * v for v in d1)) or 1.0
        return [[eye[0][j] - d0[j] / L0 * 2.0 for j in range(3)]] + eye + \
               [[eye[-1][j] + d1[j] / L1 * L for j in range(3)] for L in (6.0, 14.0)]

    # ---- figures -----------------------------------------------------------------------------------------
    def show(self, fig, t0, t1):
        self.fig_spans.setdefault(fig, []).append((t0, t1))

    def _bounds(self, name):
        n = next(n for n in self.f.nodes if n["name"] == name)
        return n["sdf"]["boundsMin"], n["sdf"]["boundsMax"]

    def on_screen(self, name, tt, margin=0.08):
        """Is any part of the object's bounds inside the frame at time tt (a 3x3x3 sample of its box)?"""
        cam = self.f.camera_at(tt)
        if cam is None:
            return False
        eye, fwd, right, up, fov = self.f.basis(tt)
        ty = math.tan(math.radians(fov / 2)) * (1 + margin)
        tx = ty * 16 / 9
        lo, hi = self._bounds(name)
        for i in range(3):
            for j in range(3):
                for k in range(3):
                    p = [lo[0] + (hi[0] - lo[0]) * i / 2, lo[1] + (hi[1] - lo[1]) * j / 2, lo[2] + (hi[2] - lo[2]) * k / 2]
                    d = [a - b for a, b in zip(p, eye)]
                    z = sum(a * b for a, b in zip(d, fwd))
                    if z <= 0.05:
                        continue
                    x = sum(a * b for a, b in zip(d, right)) / z
                    y = sum(a * b for a, b in zip(d, up)) / z
                    if abs(x) < tx and abs(y) < ty:
                        return True
        return False

    def swap(self, out_fig, in_fig, around, reach=1.6, step=0.02):
        """The moment nearest `around` (within +-reach s) at which neither figure is on screen; the outgoing figure
        hides and the incoming one appears there. Returns the time."""
        cands = []
        k = 0
        while k * step <= reach:
            for tt in ((around + k * step,) if k == 0 else (around + k * step, around - k * step)):
                if not self.on_screen(out_fig, tt) and not self.on_screen(in_fig, tt) and \
                        not self.on_screen(out_fig, tt + 0.034) and not self.on_screen(in_fig, tt + 0.034):
                    cands.append(tt)
            if cands:
                break
            k += 1
        if not cands:
            raise RuntimeError(f"no moment near {around:.2f} s with both {out_fig} and {in_fig} off screen")
        tt = round(cands[0], 3)
        self.swaps.append((tt, out_fig, in_fig))
        return tt

    def write_figures(self, end):
        for name in sorted(self.figures):
            spans = sorted(self.fig_spans.get(name, []))
            keys = [(0.0, 0.0, "step")]
            for t0, t1 in spans:
                keys += [(t0, 1.0, "step"), (t1, 0.0, "step")]
            keys.append((end, 0.0 if not spans or spans[-1][1] < end else 1.0, "step"))
            self.f.track(f"nodes/{name}/visible", keys)

    def report_figures(self):
        rows = []
        for name in sorted(self.figures):
            for t0, t1 in sorted(self.fig_spans.get(name, [])):
                seen = [round(tt / 10, 2) for tt in range(int(t0 * 10), int(t1 * 10)) if self.on_screen(name, tt / 10)]
                first = seen[0] if seen else None
                rows.append(f"  {name:<12} shown {t0:7.2f}-{t1:7.2f}  first on screen {first}  on screen {len(seen) / 10:.1f} s")
        for tt, a, b in sorted(self.swaps):
            rows.append(f"  swap {tt:7.3f}: {a} -> {b}  (both off screen)")
        return rows

    # ---- words ---------------------------------------------------------------------------------------
    def word(self, text, t0, t1, pos, normal, height, room=None, **kw):
        e = super().word(text, t0, t1, pos, normal, height, **kw)
        if room:
            e["room"] = room
        return e

    def _placement_ok(self, tt, pos, normal, height, text):
        """Pass 2's test, stricter: a grid over the whole text rectangle and a margin round it (0.35 of the cap
        height) must have solid wall behind it, and the centre, the ends and the corners a clear line of sight."""
        import sdf_eval
        trees = self._sdf_fields(tt)
        if not trees:
            return True

        def field(p):
            return min(sdf_eval.evaluate(tr, p) for tr in trees)
        eye = self.f.camera_at(tt)[0]
        if abs(normal[1]) < 0.5:
            right = [normal[2], 0.0, -normal[0]]
            upv = [0.0, 1.0, 0.0]
        else:
            right, upv = [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]
        half_w = 0.42 * height * len(text) * 0.9 + 0.35 * height
        half_h = 0.5 * height + 0.35 * height
        grid = [[p + r * half_w * u + q * half_h * v for p, r, q in zip(pos, right, upv)]
                for u in (-1.0, -0.5, 0.0, 0.5, 1.0) for v in (-1.0, 0.0, 1.0)]
        cam_eye, fwd, right_c, up_c, fov = self.f.basis(tt)
        ty = math.tan(math.radians(fov / 2))
        tx = ty * 16 / 9
        for p in grid:
            d = [b - a for a, b in zip(cam_eye, p)]
            z = sum(a * b for a, b in zip(d, fwd))
            if z <= 0.1:
                return False
            if abs(sum(a * b for a, b in zip(d, right_c)) / z) > tx * 0.94 or abs(sum(a * b for a, b in zip(d, up_c)) / z) > ty * 0.94:
                return False
        for p in grid:
            behind = [v - n * 0.03 for v, n in zip(p, normal)]
            if field(behind) > 0.0:
                return False
            if field([v + n * 0.03 for v, n in zip(p, normal)]) < 0.01:
                return False          # something mounted on the wall there (a frame, a clock, a curtain)
        for p in grid[::2]:
            d = [b - a for a, b in zip(eye, p)]
            L = math.sqrt(sum(v * v for v in d))
            u = [v / L for v in d]
            s_ = 0.05
            while s_ < L - 0.12:
                q = [e + v * s_ for e, v in zip(eye, u)]
                f_ = field(q)
                if f_ < 0.01:
                    return False
                s_ += max(f_ * 0.9, 0.02)
        return True

    def word_at(self, text, t0, t1, where, sx, sy, room=None, **kw):
        e = super().word_at(text, t0, t1, where, sx, sy, **kw)
        if room and where[0] == "box":
            e["room"] = room
        return e

    # ---- refuse the camera breath (section 20) ----------------------------------------------------------
    def breath_forbidden(self):
        for r in self.f.routes:
            assert not r["target"].startswith("camera/breath"), r
        for tr in self.f.tracks:
            assert not tr["target"].startswith("camera/breath") or all(k["value"] == [0.0] for k in tr["keys"]), tr["target"]
