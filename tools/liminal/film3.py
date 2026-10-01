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
        # the distance keys (every 0.1 s) are joined by the engine's Catmull-Rom ("smooth"), not straight lines: a
        # piecewise-linear distance steps the speed ten times a second through every acceleration (a wobble)
        self.shot(name, times[0], times[-1], eye, None, keys=nodes_keys, extra=extra, look_keys=look_keys, moves=moves,
                  fov=fov, ease_kind="linear", sway=sway, dist_interp="smooth")
        self._angular_gaze(self.f.shots[-1])

    def _angular_gaze(self, shot, dt=0.1):
        """Re-key the shot's gaze every dt seconds so it turns by angle: between two look keys the direction from the
        eye is slerped (with the smoothstep the engine uses) and the distance lerped. A look-at point interpolated in a
        straight line can pass through the camera, which whips the view round in a frame."""
        lk = shot["look_keys"]
        t0, t1 = shot["t0"], shot["t1"]
        eye_at = lambda tt: self.f.camera_at(min(max(tt, t0), t1 - 1e-4))[0]
        dirs = []
        for tk, p in lk:
            e = eye_at(tk)
            d = [a - b for a, b in zip(p, e)]
            L = math.sqrt(sum(v * v for v in d)) or 1.0
            dirs.append((tk, [v / L for v in d], L))
        out = []
        tt = t0
        while tt < t1 - 1e-6:
            if tt <= dirs[0][0]:
                _, d, L = dirs[0]
            elif tt >= dirs[-1][0]:
                _, d, L = dirs[-1]
            else:
                i = max(j for j in range(len(dirs)) if dirs[j][0] <= tt)
                (ta, da, La), (tb, db, Lb) = dirs[i], dirs[min(i + 1, len(dirs) - 1)]
                u = (tt - ta) / max(tb - ta, 1e-9)
                u = u * u * (3 - 2 * u)
                c = max(-1.0, min(1.0, sum(a * b for a, b in zip(da, db))))
                om = math.acos(c)
                if om < 1e-4:
                    d = da
                else:
                    sa, sb = math.sin((1 - u) * om) / math.sin(om), math.sin(u * om) / math.sin(om)
                    d = [sa * a + sb * b for a, b in zip(da, db)]
                L = La + (Lb - La) * u
            e = eye_at(tt)
            out.append((round(tt, 4), tuple(a + b * L for a, b in zip(e, d))))
            tt += dt
        e = eye_at(t1 - 1e-4)
        _, d, L = dirs[-1]
        out.append((round(t1 - 2e-4, 4), tuple(a + b * L for a, b in zip(e, d))))
        shot["look_keys"] = out
        self.f._paths.pop(id(shot), None)

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

    # ---- keys shared between sections (one replace track per target) --------------------------------------
    def key(self, target, tt, v, interp="step"):
        if not hasattr(self, "_keys"):
            self._keys = {}
        self._keys.setdefault(target, []).append((tt, v, interp))

    def write_keys(self, defaults=None):
        """One replace track per target. A replace track holds its first value before its first key, so a target
        whose first key is later than 0 starts from its default (`defaults`, or 1 for a node's `visible`)."""
        for target, ks in sorted(getattr(self, "_keys", {}).items()):
            ks = sorted(ks, key=lambda k: k[0])
            if ks[0][0] > 0.0:
                d = (defaults or {}).get(target, 1.0 if target.endswith("/visible") else None)
                if d is None:
                    raise RuntimeError(f"{target}: first key at {ks[0][0]} s and no default before it")
                ks = [(0.0, d, "step")] + ks
            self.f.track(target, ks)

    # ---- figures -----------------------------------------------------------------------------------------
    def show(self, fig, t0, t1):
        self.fig_spans.setdefault(fig, []).append((t0, t1))

    def _bounds(self, name):
        n = next(n for n in self.f.nodes if n["name"] == name)
        return n["sdf"]["boundsMin"], n["sdf"]["boundsMax"]

    def _walls(self, tt):
        """The SDF trees of the shot's room shells at tt (the occluders that matter for a figure in another room)."""
        cam = self.f.camera_at(tt)
        if cam is None:
            return []
        shot = next(s for s in self.f.shots if s["name"] == cam[3])
        key = ("walls", shot["name"])
        if not hasattr(self, "_wall_cache"):
            self._wall_cache = {}
        if key not in self._wall_cache:
            self._wall_cache[key] = [n["sdf"]["tree"]["root"] for n in self.f.nodes
                                     if n.get("kind") == "sdf" and n["name"] in shot["nodes"] and n["name"].endswith("Shell")]
        return self._wall_cache[key]

    def on_screen(self, name, tt, margin=0.08):
        """Is any part of the object's bounds inside the frame at time tt and not behind a wall (a 3x3x3 sample of
        its box; the shot's room shells occlude)?"""
        import sdf_eval
        cam = self.f.camera_at(tt)
        if cam is None:
            return False
        eye, fwd, right, up, fov = self.f.basis(tt)
        ty = math.tan(math.radians(fov / 2)) * (1 + margin)
        tx = ty * 16 / 9
        lo, hi = self._bounds(name)
        walls = None
        for i in range(3):
            for j in range(3):
                for k in range(3):
                    p = [lo[0] + (hi[0] - lo[0]) * (0.1 + 0.4 * i), lo[1] + (hi[1] - lo[1]) * (0.1 + 0.4 * j),
                         lo[2] + (hi[2] - lo[2]) * (0.1 + 0.4 * k)]
                    d = [a - b for a, b in zip(p, eye)]
                    z = sum(a * b for a, b in zip(d, fwd))
                    if z <= 0.05:
                        continue
                    x = sum(a * b for a, b in zip(d, right)) / z
                    y = sum(a * b for a, b in zip(d, up)) / z
                    if abs(x) >= tx or abs(y) >= ty:
                        continue
                    if walls is None:
                        walls = self._walls(tt)
                    L = math.sqrt(sum(v * v for v in d))
                    u = [v / L for v in d]
                    s_, blocked = 0.1, False
                    while s_ < L - 0.05:
                        q = [e + v * s_ for e, v in zip(eye, u)]
                        dist = min((sdf_eval.evaluate(w, q) for w in walls), default=1e9)
                        if dist < 0.005:
                            blocked = True
                            break
                        s_ += max(dist, 0.02)
                    if not blocked:
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

    def stamp_figure_spans(self):
        """Write each figure's shown span into its entity (t0, t1) for the validator, and mark rooms the kit tagged
        on its own (the lanterns, the tree's rooms, the gallery) as sealed: they are sculptures of rooms, not rooms
        anyone walks into."""
        def walk(n, fn):
            if isinstance(n, dict):
                fn(n)
                for c in n.get("children", []):
                    walk(c, fn)
        for node in self.f.nodes:
            if node.get("kind") != "sdf":
                continue
            root = node["sdf"]["tree"]["root"]
            spans = sorted(self.fig_spans.get(node["name"], []))

            def fix(n, spans=spans):
                e = n.get("entity")
                if not isinstance(e, dict):
                    return
                if e.get("category") == "mannequin" and spans:
                    e["t0"], e["t1"] = round(spans[0][0], 3), round(spans[-1][1], 3)
                if e.get("category") == "room" and e.get("_auto"):
                    e["sealed"] = True
            walk(root, fix)

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
        """The validator's lyric rules (ADR-1051), checked while placing: the text rectangle plus a margin
        (max(0.35 of the cap height, 0.15 m)) must lie on solid wall (no window, door or corner), with nothing
        standing within 0.6 m in front of any part of it (furniture, a frame, a curtain), on screen with its ends
        inside the frame, in clear line of sight, and clear of every other word on the same surface at the same
        time."""
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
        margin = max(0.35 * height, 0.15)
        half_w = 0.42 * height * len(text) * 0.9
        half_h = 0.5 * height
        rect = (pos, right, upv, half_w, half_h, normal)
        grid = [[p + r * (half_w + margin) * u + q * (half_h + margin) * v for p, r, q in zip(pos, right, upv)]
                for u in (-1.0, -0.5, 0.0, 0.5, 1.0) for v in (-1.0, 0.0, 1.0)]
        inner = [[p + r * half_w * u + q * half_h * v for p, r, q in zip(pos, right, upv)]
                 for u in (-1.0, 0.0, 1.0) for v in (-1.0, 1.0)]
        cam_eye, fwd, right_c, up_c, fov = self.f.basis(tt)
        ty = math.tan(math.radians(fov / 2))
        tx = ty * 16 / 9
        for p in inner:
            d = [b - a for a, b in zip(cam_eye, p)]
            z = sum(a * b for a, b in zip(d, fwd))
            if z <= 0.1:
                return False
            if abs(sum(a * b for a, b in zip(d, right_c)) / z) > tx * 0.94 or abs(sum(a * b for a, b in zip(d, up_c)) / z) > ty * 0.94:
                return False
        for other in getattr(self, "_placed", []):
            if self._rect_clash(rect, margin, other, self._cur_span):
                return False
        for p in grid:
            if field([v - n * 0.03 for v, n in zip(p, normal)]) > 0.0:
                return False          # no wall behind this part: a window, a door, the room's corner
        for p in grid:
            for dd in (0.06, 0.15, 0.3, 0.45, 0.6):
                if field([v + n * dd for v, n in zip(p, normal)]) < dd - 0.03:
                    return False      # something stands in front of the wall here (within 0.6 m)
        for p in inner[::2] + [list(pos)]:
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
        self._last_rect = rect
        return True

    @staticmethod
    def _rect_clash(rect, margin, other, span):
        """Two text rectangles on the same surface at overlapping times, closer than the margin."""
        (pos, right, upv, hw, hh, n), (t0, t1) = rect, span
        (opos, oright, oupv, ohw, ohh, on, ot0, ot1) = other
        if ot1 <= t0 or t1 <= ot0:
            return False
        if sum(a * b for a, b in zip(n, on)) < 0.9:
            return False
        if abs(sum((a - b) * c for a, b, c in zip(pos, opos, n))) > 0.15:
            return False
        d = [a - b for a, b in zip(opos, pos)]
        du, dv = abs(sum(a * b for a, b in zip(d, right))), abs(sum(a * b for a, b in zip(d, upv)))
        return du < hw + ohw + margin and dv < hh + ohh + margin

    # ---- clear wall: where a word may go (the validator's lyric rules, precomputed per room) ------------------
    def _room_mask(self, room):
        """For each surface of the room (the four walls, the floor, the ceiling): a grid (0.1 m) of points where the
        wall is solid behind and nothing stands within 0.6 m in front of it. Static geometry (rest poses); figures
        are left out (the line-of-sight test at the word's own time sees them). Computed lazily, a surface at a
        time (`_surface_mask`)."""
        if not hasattr(self, "_masks"):
            self._masks = {}
        rid = room["id"]
        if rid not in self._masks:
            (x0, x1), (y0, y1), (z0, z1) = room["interior"]
            self._masks[rid] = {
                "-x": {"n": (1, 0, 0), "plane": x0, "a": (z0, z1), "b": (y0, y1), "grid": None, "step": 0.1, "room": room, "key": "-x"},
                "+x": {"n": (-1, 0, 0), "plane": x1, "a": (z0, z1), "b": (y0, y1), "grid": None, "step": 0.1, "room": room, "key": "+x"},
                "-z": {"n": (0, 0, 1), "plane": z0, "a": (x0, x1), "b": (y0, y1), "grid": None, "step": 0.1, "room": room, "key": "-z"},
                "+z": {"n": (0, 0, -1), "plane": z1, "a": (x0, x1), "b": (y0, y1), "grid": None, "step": 0.1, "room": room, "key": "+z"},
                "floor": {"n": (0, 1, 0), "plane": y0, "a": (x0, x1), "b": (z0, z1), "grid": None, "step": 0.1, "room": room, "key": "floor"},
                "ceil": {"n": (0, -1, 0), "plane": y1, "a": (x0, x1), "b": (z0, z1), "grid": None, "step": 0.1, "room": room, "key": "ceil"}}
        return self._masks[rid]

    def _surface_mask(self, m):
        import sdf_eval
        if m["grid"] is not None:
            return m["grid"]
        room, key, n, plane, step = m["room"], m["key"], m["n"], m["plane"], m["step"]
        objs = [o for o in room["objects"] if o[2] != "figure"]
        shell = [o[1] for o in objs if o[0].endswith("Shell")]
        (a0, a1), (b0, b1) = m["a"], m["b"]
        nu, nv = int(round((a1 - a0) / step)) + 1, int(round((b1 - b0) / step)) + 1
        grid = []
        for i in range(nu):
            row = []
            for j in range(nv):
                p = self._surface_point(key, plane, a0 + i * step, b0 + j * step)
                behind = [v - nn * 0.03 for v, nn in zip(p, n)]
                ok = min(sdf_eval.evaluate(tr, behind) for tr in shell) < 0.0
                behind_ok = ok
                if ok:
                    seg_lo = [min(v, v + nn * 0.6) - 0.05 for v, nn in zip(p, n)]
                    seg_hi = [max(v, v + nn * 0.6) + 0.05 for v, nn in zip(p, n)]
                    near = [o[1] for o in objs if all(o[3][c] <= seg_hi[c] and o[4][c] >= seg_lo[c] for c in range(3))]
                    # anything within 5 cm of the column straight out from the wall, 0.08-0.6 m (a frame, a
                    # curtain, a shelf, a sofa's back): the dado and skirting (1-2 cm proud) do not count
                    for dd in (0.08, 0.15, 0.22, 0.3, 0.38, 0.46, 0.54, 0.6):
                        q = [v + nn * dd for v, nn in zip(p, n)]
                        if near and min(sdf_eval.evaluate(tr, q) for tr in near) < 0.05:
                            ok = False
                            break
                row.append((ok, behind_ok))
            grid.append(row)
        # a cell next to an opening (no wall behind) is not clear either: a 0.1 m buffer round windows and doors
        clear = []
        for i in range(nu):
            row = []
            for j in range(nv):
                ok = grid[i][j][0]
                if ok:
                    for di in (-1, 0, 1):
                        for dj in (-1, 0, 1):
                            ii, jj = i + di, j + dj
                            if 0 <= ii < nu and 0 <= jj < nv and not grid[ii][jj][1]:
                                ok = False
                row.append(ok)
            clear.append(row)
        m["grid"] = clear
        return clear

    @staticmethod
    def _surface_point(key, plane, a, b):
        if key in ("-x", "+x"):
            return [plane, b, a]
        if key in ("-z", "+z"):
            return [a, b, plane]
        return [a, plane, b]

    def _rect_clear(self, m, ca, cb, ha, hb):
        """Is the rectangle centred (ca, cb) with half sizes (ha, hb), in the surface's own (a, b) coordinates, all
        clear?"""
        step = m["step"]
        a0, b0 = m["a"][0], m["b"][0]
        g = self._surface_mask(m)
        i0, i1 = int(math.floor((ca - ha - a0) / step)), int(math.ceil((ca + ha - a0) / step))
        j0, j1 = int(math.floor((cb - hb - b0) / step)), int(math.ceil((cb + hb - b0) / step))
        if i0 < 0 or j0 < 0 or i1 >= len(g) or j1 >= len(g[0]):
            return False
        return all(g[i][j] for i in range(i0, i1 + 1) for j in range(j0, j1 + 1))

    def word_at(self, text, t0, t1, where, sx, sy, room=None, **kw):
        """A word where the camera looks. In a room ("box", interior) with a known room dict, the word goes to the
        clear stretch of wall, floor or ceiling nearest the view ray through (sx, sy): its rectangle and a margin
        (max(0.35 of the cap height, 0.15 m)) all clear, its ends on screen, in line of sight, and clear of every
        other word on that surface at the same time. Anything else: pass 2's placement."""
        rdict = None
        if where[0] == "box" and room:
            rdict = next((w["room"] for w in self.worlds.values() if w["room"].get("id") == room), None)
        if rdict is None:
            self._cur_span = (t0, t1)
            self._last_rect = None
            e = super().word_at(text, t0, t1, where, sx, sy, **kw)
            return e
        at = kw.pop("at", 0.15)
        k = kw.pop("k", 0.08)
        tilt0 = kw.pop("tilt", 0.0)
        tt = t0 + at
        masks = self._room_mask(rdict)
        eye, fwd, right_c, up_c, fov = self.f.basis(tt)
        ty = math.tan(math.radians(fov / 2))
        tx = ty * 16 / 9
        best = None
        for shrink in (1.0, 0.8, 0.64, 0.5):
            hit = self.f.on_box(tt, sx, sy, rdict["interior"])
            h = kw.get("height") or k * shrink * hit[3]
            margin = max(0.35 * h, 0.18)
            half_w = 0.47 * h * len(text) * 0.9
            cands = []
            for key, m in masks.items():
                n = m["n"]
                if sum(a * b for a, b in zip(n, fwd)) > -0.25:
                    continue                                   # the surface must face the camera
                flat = key in ("floor", "ceil")
                if flat:
                    fx, fz = fwd[0], fwd[2]
                    tilt = math.degrees(math.atan2(-fx, -fz)) if key == "floor" else math.degrees(math.atan2(fx, -fz))
                    rgt = [math.cos(math.radians(tilt)), 0.0, -math.sin(math.radians(tilt))]
                    ha = abs(rgt[0]) * (half_w + margin) + abs(rgt[2]) * (0.5 * h + margin)
                    hb = abs(rgt[2]) * (half_w + margin) + abs(rgt[0]) * (0.5 * h + margin)
                else:
                    tilt = 0.0
                    ha, hb = half_w + margin, 0.5 * h + margin
                (a0, a1), (b0, b1) = m["a"], m["b"]
                stepc = 0.1
                i = 0
                ca = a0 + ha
                while ca <= a1 - ha + 1e-9:
                    cb = b0 + hb
                    while cb <= b1 - hb + 1e-9:
                        p = self._surface_point(key, m["plane"], ca, cb)
                        d = [q - e for q, e in zip(p, eye)]
                        z = sum(q * f_ for q, f_ in zip(d, fwd))
                        if z > 0.3:
                            xs = sum(q * r for q, r in zip(d, right_c)) / z / tx
                            ys = sum(q * u for q, u in zip(d, up_c)) / z / ty
                            if abs(xs) < 0.9 and abs(ys) < 0.9:
                                cands.append(((xs - sx) ** 2 + (ys - sy) ** 2, key, ca, cb, ha, hb, p, list(n), tilt, z))
                        cb += stepc
                    ca += stepc
            cands.sort(key=lambda c: c[0])
            for _, key, ca, cb, ha, hb, p, n, tilt, z in cands[:400]:
                m = masks[key]
                if not self._rect_clear(m, ca, cb, ha, hb):
                    continue
                pos = [v + nn * 0.004 for v, nn in zip(p, n)]
                if key in ("floor", "ceil"):
                    rgt = [math.cos(math.radians(tilt)), 0.0, -math.sin(math.radians(tilt))]
                    upv = [math.sin(math.radians(tilt)) * (1 if key == "floor" else -1), 0.0, math.cos(math.radians(tilt))]
                else:
                    rgt, upv = [n[2], 0.0, -n[0]], [0.0, 1.0, 0.0]
                hw = half_w
                ends = [[q + r * hw * s_ for q, r in zip(pos, rgt)] for s_ in (-1.0, 1.0)]
                ok = True
                for e_ in ends:
                    d = [q - e for q, e in zip(e_, eye)]
                    z_ = sum(q * f_ for q, f_ in zip(d, fwd))
                    if z_ <= 0.2 or abs(sum(q * r for q, r in zip(d, right_c)) / z_) > tx * 0.94 or \
                            abs(sum(q * u for q, u in zip(d, up_c)) / z_) > ty * 0.94:
                        ok = False
                        break
                if not ok:
                    continue
                rect = (pos, rgt, upv, hw, 0.5 * h, n)
                if any(self._rect_clash(rect, margin, o, (t0, t1)) for o in getattr(self, "_placed", [])):
                    continue
                if self._figure_in_front(rdict, pos, rgt, upv, hw + margin, 0.5 * h + margin, n, t0, t1):
                    continue
                if not self._sight(tt, pos, ends):
                    continue
                best = (pos, n, tilt, h, rect, key)
                break
            if best:
                break
        if best is None:
            # no clear wall in view: the words float in the room's air facing the camera (floating text, section 11:
            # an explicit orientation), at a distance where nothing is near them
            import sdf_eval
            trees = self._sdf_fields(tt)
            for dist in (1.6, 1.3, 2.0, 1.0, 2.4):
                for dx, dy in ((0.0, 0.0), (0.0, 0.15), (0.0, -0.15), (0.2, 0.0), (-0.2, 0.0)):
                    p, nrm, _, _ = self.f.in_view(tt, sx + dx, sy + dy, dist)
                    h = k * 0.8 * dist
                    hw = 0.42 * h * len(text) * 0.9
                    pts = [[q + r * hw * u for q, r in zip(p, right_c)] for u in (-1.0, -0.5, 0.0, 0.5, 1.0)]
                    if all(min(sdf_eval.evaluate(tr, q) for tr in trees) > 0.15 for q in pts) and self._sight(tt, p, pts[::4]):
                        kw.pop("height", None)
                        e = self.word(text, t0, t1, p, nrm, h, tilt=tilt0, room=None, **kw)
                        e["category"] = "floatingText"
                        self.floated = getattr(self, "floated", []) + [(round(t0, 2), text)]
                        return e
            self.unplaced = getattr(self, "unplaced", []) + [(round(t0, 2), text)]
            self._cur_span = (t0, t1)
            self._last_rect = None
            return super().word_at(text, t0, t1, where, sx, sy, k=k, at=at, tilt=tilt0, **kw)
        pos, n, tilt, h, rect, key = best
        self._placed = getattr(self, "_placed", []) + [rect + (t0, t1)]
        kw.pop("height", None)
        e = self.word(text, t0, t1, pos, n, h, tilt=tilt + (tilt0 if key not in ("floor", "ceil") else 0.0), room=room, **kw)
        if key in ("floor", "ceil"):
            e.setdefault("category", "floorText")
        return e

    def _figure_in_front(self, rdict, pos, rgt, upv, hw, hh, n, t0, t1):
        """Is a figure shown during [t0, t1) within 0.6 m in front of the text's rectangle (by its bounding box, as
        the validator reads it)?"""
        figs = [o for o in rdict["objects"] if o[2] == "figure" and
                any(a < t1 and t0 < b_ for a, b_ in self.fig_spans.get(o[0], []))]
        corners = [[q + r * hw * u + w * hh * v + nn * dd for q, r, w, nn in zip(pos, rgt, upv, n)]
                   for u in (-1.0, 1.0) for v in (-1.0, 1.0) for dd in (0.0, 0.6)]
        lo = [min(c[i] for c in corners) for i in range(3)]
        hi = [max(c[i] for c in corners) for i in range(3)]
        for o in figs:
            if all(o[3][i] < hi[i] and o[4][i] > lo[i] for i in range(3)):
                return True
        return False

    def _sight(self, tt, pos, ends):
        """A clear line of sight from the eye to the word's centre and ends (every SDF object of the shot)."""
        import sdf_eval
        trees = self._sdf_fields(tt)
        eye = self.f.camera_at(tt)[0]
        for p in [pos] + ends:
            d = [b - a for a, b in zip(eye, p)]
            L = math.sqrt(sum(v * v for v in d))
            u = [v / L for v in d]
            s_ = 0.05
            while s_ < L - 0.1:
                q = [e + v * s_ for e, v in zip(eye, u)]
                f_ = min(sdf_eval.evaluate(tr, q) for tr in trees)
                if f_ < 0.01:
                    return False
                s_ += max(f_ * 0.9, 0.02)
        return True

    # ---- refuse the camera breath (section 20) ----------------------------------------------------------
    def breath_forbidden(self):
        for r in self.f.routes:
            assert not r["target"].startswith("camera/breath"), r
        for tr in self.f.tracks:
            assert not tr["target"].startswith("camera/breath") or all(k["value"] == [0.0] for k in tr["keys"]), tr["target"]
