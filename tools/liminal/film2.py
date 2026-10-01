"""All You Got, art pass 2: the film's machinery -- SDF objects with the line look, shots (one journey chapter
each, cut exactly), timeline keys, routes, the beat grid, the palette -- so the generator
(make_all_you_got_pass2.py) reads as a shot list.

Shots. Every shot is its own journey chapter (ADR-1042), `start` 1000 m after the previous one, with its own
path in world coordinates (all worlds sit at the origin; pass 1 found that far offsets jitter). The camera's
`distance` is keyed shot by shot: inside a shot it runs along the shot's path; at a cut it jumps to the next
chapter on an exact key (the last key of a shot is a `step` held until the cut's instant). The path here is the
engine's own spline, replicated (centripetal Catmull-Rom, periodic under the chapter's screw, 128 samples a
segment: src/scene/journey.cpp), so a distance computed here lands the camera where it is meant to.

Eye points are the camera's actual eye (journey `height` 0). The gaze is a look-at point with weight 1 (keyed
per shot), so every shot is framed on purpose.
"""

from __future__ import annotations

import bisect
import json
import math
import os
from typing import Optional

import kit as K

SCREW_T = (5000.0, 0.0, 0.0)
CHAPTER_STRIDE = 1000.0


# ---- the engine's journey spline, replicated -----------------------------------------------------------

def _knot(a, b):
    return max(math.dist(a, b) ** 0.5, 1e-3)


class EnginePath:
    """A journey path exactly as `JourneyPath::build` makes it (screw a pure translation)."""

    def __init__(self, points, T=SCREW_T, samples=128):
        self.points = [list(map(float, p)) for p in points]
        n = len(self.points)

        def Q(i):
            j = math.floor(i / n)
            p = self.points[i - j * n]
            return [p[0] + j * T[0], p[1] + j * T[1], p[2] + j * T[2]]
        self.segs = []
        for i in range(n):
            p0, p1, p2, p3 = Q(i - 1), Q(i), Q(i + 1), Q(i + 2)
            t0 = 0.0
            t1 = t0 + _knot(p0, p1)
            t2 = t1 + _knot(p1, p2)
            t3 = t2 + _knot(p2, p3)
            self.segs.append((p0, p1, p2, p3, t0, t1, t2, t3))
        self.S, self.Uv = [0.0], [0.0]
        prev = self._pt(0, 0.0)
        tot = 0.0
        self.at_point = [0.0]
        for i in range(n):
            for k in range(1, samples + 1):
                u = k / samples
                p = self._pt(i, u)
                tot += math.dist(p, prev)
                prev = p
                self.S.append(tot)
                self.Uv.append(i + u)
            self.at_point.append(tot)
        self.length = tot

    def _pt(self, i, u):
        p0, p1, p2, p3, t0, t1, t2, t3 = self.segs[i]
        t = t1 + (t2 - t1) * u

        def lerp(a, b, ta, tb):
            return [(tb - t) / (tb - ta) * x + (t - ta) / (tb - ta) * y for x, y in zip(a, b)]
        a1 = lerp(p0, p1, t0, t1)
        a2 = lerp(p1, p2, t1, t2)
        a3 = lerp(p2, p3, t2, t3)
        b1 = lerp(a1, a2, t0, t2)
        b2 = lerp(a2, a3, t1, t3)
        return lerp(b1, b2, t1, t2)

    def point(self, s):
        s = max(0.0, min(s, self.length))
        hi = min(bisect.bisect_right(self.S, s), len(self.S) - 1)
        lo = max(hi - 1, 0)
        span = self.S[hi] - self.S[lo]
        f = (s - self.S[lo]) / span if span > 0 else 0.0
        g = self.Uv[lo] + (self.Uv[hi] - self.Uv[lo]) * f
        seg = min(int(math.floor(g)), len(self.segs) - 1)
        return self._pt(seg, g - seg)


# ---- easing -----------------------------------------------------------------------------------------------

def ease(u, kind="inout"):
    u = max(0.0, min(1.0, u))
    if kind == "linear":
        return u
    if kind == "in":
        return u * u
    if kind == "out":
        return 1 - (1 - u) * (1 - u)
    return u * u * (3 - 2 * u)


def srgb(h: str, gain: float = 1.0) -> list:
    h = h.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        c = c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
        out.append(round(c * gain, 5))
    return out


def mix(a, b, t):
    return [round(x + (y - x) * t, 5) for x, y in zip(a, b)]


def mul(a, k):
    return [round(x * k, 5) for x in a]


# ---- the film ---------------------------------------------------------------------------------------------

class Film:
    def __init__(self, end: float):
        self.end = end
        self.nodes: list[dict] = []
        self.lights: list[dict] = []
        self.tracks: list[dict] = []
        self.routes: list[dict] = []
        self.sources: list[dict] = []
        self.events: list[dict] = []          # beat grid events
        self.shots: list[dict] = []
        self.node_names: set = set()
        self.palette_states: list[dict] = []
        self.bindings: list[dict] = []
        self.report: list[str] = []
        self._paths = {}

    # ---- SDF objects with the line look ---------------------------------------------------------------
    def sdf(self, name, tree, *, edge=(1.0, 1.0, 1.0), edge_intensity=4.0, edge_width=0.012, surfaces=None,
            bmin=(-8, -1, -8), bmax=(8, 4, 8), max_distance=40.0, ao=0.25, step_scale=0.85, roughness=0.8,
            visible=True, epsilon=0.0009, edge_pixels=2.2, edge_softness=0.14, edge_threshold=0.02, look_extra=None):
        n, ops, depth = K.count(tree)
        assert n <= 96, (name, n)
        assert depth <= 16, (name, depth)
        assert name not in self.node_names, name
        self.node_names.add(name)
        surfs = surfaces or default_surfaces()
        node = {"name": name, "kind": "sdf", "visible": visible,
                "sdf": {"tree": {"root": tree}, "renderMode": "raymarch", "compile": True,
                        "material": {"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                                     "roughness": roughness, "metallic": 0.0},
                        "surfaces": surfs,
                        "boundsMin": list(map(float, bmin)), "boundsMax": list(map(float, bmax)),
                        "look": {"aoStrength": ao, "aoDistance": 0.5, "edgeIntensity": edge_intensity,
                                 "edgeWidth": edge_width, "edgeColor": list(map(float, edge)),
                                 "edgePixels": edge_pixels, "edgeSoftness": edge_softness,
                                 "edgeThreshold": edge_threshold, **(look_extra or {})},
                        "castShadows": False, "depthPrepass": False, "maxSteps": 160, "epsilon": epsilon,
                        "stepScale": step_scale, "normalEpsilon": 0.002, "maxDistance": max_distance},
                "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0]}
        self.nodes.append(node)
        self.report.append(f"sdf {name}: {n} nodes, {ops} nested ops, depth {depth}")
        return name

    def point_light(self, name, pos, color, intensity, rng, vol=0.0):
        self.lights.append({"name": name, "type": "point", "position": list(map(float, pos)), "color": list(map(float, color)),
                            "intensity": float(intensity), "range": float(rng), "castsShadow": False, "volumetric": vol})
        return name

    # ---- tracks and routes -----------------------------------------------------------------------------
    def track(self, target, keys, interp="smooth", mode="replace", component=-1):
        ks = []
        for k in keys:
            t, v = k[0], k[1]
            ip = k[2] if len(k) > 2 else interp
            ks.append({"time": round(float(t), 5), "value": v if isinstance(v, list) else [float(v)], "interp": ip})
        ks.sort(key=lambda k: k["time"])
        self.tracks.append({"target": target, "component": component, "timeBase": "seconds", "mode": mode,
                            "loopLength": 0.0, "enabled": True, "keys": ks})

    def route(self, source, target, amount=1.0, component=-1, depth=None, polarity="unipolar", op="add", **chain):
        r = {"source": source, "target": target, "amount": float(amount), "op": op, "polarity": polarity,
             "component": component, "enabled": True, "chain": chain}
        if depth:
            r["depthSource"] = depth
            r["depthMin"] = 0.0
            r["depthMax"] = 1.0
        self.routes.append(r)

    def event(self, channel, time=None, at=None, **kw):
        e = {"channel": channel}
        if at is not None:
            e["at"] = at
        else:
            e["time"] = round(float(time), 5)
            e.setdefault("units", "seconds")
        e.update(kw)
        if "units" not in e:
            e["units"] = "seconds" if at is None else "beats"
        self.events.append(e)

    # ---- shots ---------------------------------------------------------------------------------------------
    def shot(self, name, t0, t1, eye, look, *, nodes=(), lights=(), moves=None, look_keys=None, fov=None,
             sway=0.0, ease_kind="inout"):
        """A shot from t0 to t1 (seconds). `eye`: the eye's path points (world). `look`: the look-at point, or
        `look_keys` [(t, point), ...]. `moves`: [(t, fraction 0..1 of the eye path), ...] (default: the whole
        path, eased over the shot). `fov`: a number or [(t, fov)]."""
        self.shots.append({"name": name, "t0": float(t0), "t1": float(t1), "eye": [list(map(float, p)) for p in eye],
                           "look": list(map(float, look)) if look is not None else None, "look_keys": look_keys,
                           "nodes": list(nodes), "lights": list(lights), "moves": moves, "fov": fov, "sway": sway,
                           "ease": ease_kind})

    def _build_journey(self):
        chapters, dist, lookat, fovk, sway = [], [], [], [], []
        for i, s in enumerate(self.shots):
            eye = s["eye"]
            if len(eye) == 1:
                eye = [eye[0], [eye[0][0] + 0.001, eye[0][1], eye[0][2]]]
            d0 = [b - a for a, b in zip(eye[0], eye[1])]
            L0 = math.sqrt(sum(v * v for v in d0)) or 1.0
            d0 = [v / L0 for v in d0]
            d1 = [b - a for a, b in zip(eye[-2], eye[-1])]
            L1 = math.sqrt(sum(v * v for v in d1)) or 1.0
            d1 = [v / L1 for v in d1]
            lead = [eye[0][j] - d0[j] * 2.0 for j in range(3)]
            tail = [[eye[-1][j] + d1[j] * L for j in range(3)] for L in (6.0, 14.0)]
            pts = [lead] + eye + tail
            path = EnginePath(pts)
            s_a = path.at_point[1]
            s_b = path.at_point[len(eye)]
            base = CHAPTER_STRIDE * (i + 1)
            chapters.append({"name": s["name"], "start": base, "from": 0.0,
                             "path": [[round(v, 5) for v in p] for p in pts],
                             "screw": {"translation": list(SCREW_T), "count": 0}, "collide": "", "radius": 0.0,
                             "offset": [0.0, 0.0, 0.0], "yaw": 0.0, "nodes": s["nodes"], "lights": s["lights"]})
            t0, t1 = s["t0"], s["t1"]
            moves = s["moves"] or [(t0, 0.0), (t1, 1.0)]
            n_dense = max(2, int((t1 - t0) / 0.1))
            # dense keys of the eased move (linear interp between them, so the speed curve is exact)
            for k in range(n_dense + 1):
                t = t0 + (t1 - t0) * k / n_dense
                # piecewise eased fraction
                f = moves[0][1]
                for (ta, fa), (tb, fb) in zip(moves, moves[1:]):
                    if ta - 1e-9 <= t <= tb + 1e-9:
                        f = fa + (fb - fa) * ease((t - ta) / max(tb - ta, 1e-9), s["ease"])
                        break
                    if t > tb:
                        f = fb
                dist.append([t, base + s_a + (s_b - s_a) * f, "linear"])
            # hold the last value as a step until the cut
            dist[-1][2] = "step"
            if s["look_keys"]:
                for t, p in s["look_keys"]:
                    lookat.append([t, list(map(float, p)), "smooth"])
                lookat.append([t1 - 1e-4, lookat[-1][1], "step"])
            else:
                lookat.append([t0, s["look"], "step"])
                lookat.append([t1 - 1e-4, s["look"], "step"])
            f = s["fov"] if s["fov"] is not None else 60.0
            if isinstance(f, (int, float)):
                fovk += [[t0, float(f), "step"], [t1 - 1e-4, float(f), "step"]]
            else:
                fovk += [[t, float(v), "smooth"] for t, v in f]
                fovk.append([t1 - 1e-4, float(f[-1][1]), "step"])
            sway += [[t0, float(s["sway"]), "step"], [t1 - 1e-4, float(s["sway"]), "step"]]
        return chapters, dist, lookat, fovk, sway

    def camera_at(self, tt):
        """(eye, target, vertical fov degrees, shot name) at time tt, as the journey will place it (no breathing)."""
        for s in self.shots:
            if s["t0"] - 1e-6 <= tt < s["t1"]:
                break
        else:
            return None
        eye = s["eye"] if len(s["eye"]) > 1 else [s["eye"][0], [s["eye"][0][0] + 0.001, s["eye"][0][1], s["eye"][0][2]]]
        moves = s["moves"] or [(s["t0"], 0.0), (s["t1"], 1.0)]
        f = moves[0][1]
        for (ta, fa), (tb, fb) in zip(moves, moves[1:]):
            if ta - 1e-9 <= tt <= tb + 1e-9:
                f = fa + (fb - fa) * ease((tt - ta) / max(tb - ta, 1e-9), s["ease"])
                break
            if tt > tb:
                f = fb
        key = id(s)
        if key not in self._paths:
            d0 = [b - a for a, b in zip(eye[0], eye[1])]
            L0 = math.sqrt(sum(v * v for v in d0)) or 1.0
            d1 = [b - a for a, b in zip(eye[-2], eye[-1])]
            L1 = math.sqrt(sum(v * v for v in d1)) or 1.0
            pts = [[eye[0][j] - d0[j] / L0 * 2.0 for j in range(3)]] + eye + \
                  [[eye[-1][j] + d1[j] / L1 * L for j in range(3)] for L in (6.0, 14.0)]
            self._paths[key] = EnginePath(pts)
        path = self._paths[key]
        sa, sb = path.at_point[1], path.at_point[len(eye)]
        pos = path.point(sa + (sb - sa) * f)
        if s["look_keys"]:
            lk = s["look_keys"]
            tgt = lk[0][1]
            for (ta, pa), (tb, pb) in zip(lk, lk[1:]):
                if ta <= tt <= tb:
                    u = (tt - ta) / max(tb - ta, 1e-9)
                    u = u * u * (3 - 2 * u)
                    tgt = [x + (y - x) * u for x, y in zip(pa, pb)]
                    break
                if tt > tb:
                    tgt = pb
        else:
            tgt = s["look"]
        fov = s["fov"] if s["fov"] is not None else 60.0
        if not isinstance(fov, (int, float)):
            fv = fov[0][1]
            for (ta, va), (tb, vb) in zip(fov, fov[1:]):
                if ta <= tt <= tb:
                    fv = va + (vb - va) * (tt - ta) / max(tb - ta, 1e-9)
                    break
                if tt > tb:
                    fv = vb
            fov = fv
        return pos, list(tgt), float(fov), s["name"]

    def basis(self, tt):
        eye, tgt, fov, shot = self.camera_at(tt)
        fwd = [b - a for a, b in zip(eye, tgt)]
        L = math.sqrt(sum(v * v for v in fwd)) or 1.0
        fwd = [v / L for v in fwd]
        right = [fwd[1] * 0.0 - fwd[2] * 1.0, fwd[2] * 0.0 - fwd[0] * 0.0, fwd[0] * 1.0 - fwd[1] * 0.0]
        right = [-fwd[2], 0.0, fwd[0]]
        Lr = math.sqrt(sum(v * v for v in right)) or 1.0
        right = [v / Lr for v in right]
        up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]]
        return eye, fwd, right, up, fov

    def ray(self, tt, sx, sy, aspect=16 / 9):
        """The eye and the unit direction through screen point (sx, sy) in [-1, 1] (x right, y up) at time tt."""
        eye, fwd, right, up, fov = self.basis(tt)
        ty = math.tan(math.radians(fov / 2))
        tx = ty * aspect
        d = [f + r * sx * tx + u * sy * ty for f, r, u in zip(fwd, right, up)]
        L = math.sqrt(sum(v * v for v in d))
        return eye, [v / L for v in d], fwd

    @staticmethod
    def flat_tilt(normal, fwd):
        """The in-plane turn that makes floor or ceiling text read the right way up for a camera facing fwd."""
        fx, fz = fwd[0], fwd[2]
        if normal[1] > 0.5:       # floor: the text's top away from the camera
            return math.degrees(math.atan2(-fx, -fz))
        if normal[1] < -0.5:      # ceiling: the text's top towards the camera's back
            return math.degrees(math.atan2(fx, -fz))
        return 0.0

    def on_box(self, tt, sx, sy, ext, inset=0.004):
        """Where the view ray through (sx, sy) at tt meets the inside of the room box `ext`: (point, inward
        normal, tilt for flat surfaces, distance)."""
        eye, d, fwd = self.ray(tt, sx, sy)
        # the slab method: where the ray LEAVES the box is a wall, floor or ceiling seen from inside, whether the
        # eye is inside the room or still in its doorway
        t_exit, n_exit = None, None
        for ax in range(3):
            if abs(d[ax]) < 1e-9:
                continue
            lo, hi = ext[ax]
            t_far = (hi - eye[ax]) / d[ax] if d[ax] > 0 else (lo - eye[ax]) / d[ax]
            if t_exit is None or t_far < t_exit:
                t_exit = t_far
                n_exit = [0.0, 0.0, 0.0]
                n_exit[ax] = -1.0 if d[ax] > 0 else 1.0
        dist, n = t_exit, n_exit
        p = [e + v * dist + nn * inset for e, v, nn in zip(eye, d, n)]
        return p, n, self.flat_tilt(n, fwd), dist

    def on_ground(self, tt, sx, sy, ground, inset=0.06, far=120.0):
        """Where the view ray meets the terrain `ground(x, z)` (a march then a bisection): (point, up)."""
        eye, d, fwd = self.ray(tt, sx, sy)
        prev = 0.0
        step = 0.25
        s_ = 0.5
        while s_ < far:
            p = [e + v * s_ for e, v in zip(eye, d)]
            if p[1] <= ground(p[0], p[2]):
                lo, hi = prev, s_
                for _ in range(30):
                    m = (lo + hi) / 2
                    q = [e + v * m for e, v in zip(eye, d)]
                    if q[1] <= ground(q[0], q[2]):
                        hi = m
                    else:
                        lo = m
                q = [e + v * hi for e, v in zip(eye, d)]
                q[1] += inset
                return q, [0.0, 1.0, 0.0], self.flat_tilt([0, 1, 0], fwd), hi
            prev = s_
            s_ += step
            step = min(step * 1.08, 3.0)
        return None

    def in_view(self, tt, sx, sy, dist):
        """A point `dist` metres out along the view ray through (sx, sy), facing back at the camera."""
        eye, d, fwd = self.ray(tt, sx, sy)
        p = [e + v * dist for e, v in zip(eye, d)]
        return p, [-v for v in d], 0.0, dist

    def check_words(self, words, aspect=16 / 9):
        """Words that are off screen, behind the camera or facing away at the moment they appear (+0.15 s)."""
        bad = []
        for w in words:
            cam = self.camera_at(w["t0"] + 0.15)
            if cam is None:
                bad.append((w["t0"], w["text"], "no shot"))
                continue
            eye, tgt, fov, shot = cam
            fwd = [b - a for a, b in zip(eye, tgt)]
            L = math.sqrt(sum(v * v for v in fwd)) or 1.0
            fwd = [v / L for v in fwd]
            up0 = [0.0, 1.0, 0.0]
            right = [fwd[1] * up0[2] - fwd[2] * up0[1], fwd[2] * up0[0] - fwd[0] * up0[2], fwd[0] * up0[1] - fwd[1] * up0[0]]
            Lr = math.sqrt(sum(v * v for v in right)) or 1.0
            right = [v / Lr for v in right]
            up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]]
            d = [b - a for a, b in zip(eye, w["position"])]
            z = sum(a * b for a, b in zip(d, fwd))
            if z <= 0.05:
                bad.append((w["t0"], w["text"], f"behind the camera in {shot}"))
                continue
            x = sum(a * b for a, b in zip(d, right)) / z
            y = sum(a * b for a, b in zip(d, up)) / z
            ty = math.tan(math.radians(fov / 2))
            tx = ty * aspect
            n = w.get("normal", [0, 0, 1])
            facing = -sum(a * b for a, b in zip(d, n))
            why = []
            if abs(x) > tx * 1.02 or abs(y) > ty * 1.02:
                why.append(f"off screen ({x / tx:+.2f}, {y / ty:+.2f}) in {shot}")
            if "rotation" not in w and facing <= 0:
                why.append(f"faces away in {shot}")
            if why:
                bad.append((round(w["t0"], 2), w["text"], "; ".join(why)))
        return bad

    # ---- palette --------------------------------------------------------------------------------------------
    def palette(self, name, **roles):
        self.palette_states.append({"name": name, "colors": {k: list(map(float, v)) for k, v in roles.items()}})

    def bind(self, role, target, mode=None):
        b = {"role": role, "target": target}
        if mode:
            b["mode"] = mode
        self.bindings.append(b)

    # ---- output ---------------------------------------------------------------------------------------------
    def write(self, out_dir, stem, grid_settings, env, params, rig_ambient=(0.02, 0.02, 0.03)):
        chapters, dist, lookat, fovk, sway = self._build_journey()
        T = list(self.tracks)
        T.insert(0, {"target": "camera/journey/distance", "component": -1, "timeBase": "seconds", "mode": "replace",
                     "loopLength": 0.0, "enabled": True,
                     "keys": [{"time": round(t, 5), "value": [round(v, 5)], "interp": ip} for t, v, ip in dist]})
        T.insert(1, {"target": "camera/journey/lookAt", "component": -1, "timeBase": "seconds", "mode": "replace",
                     "loopLength": 0.0, "enabled": True,
                     "keys": [{"time": round(t, 5), "value": [round(x, 5) for x in v], "interp": ip} for t, v, ip in lookat]})
        T.insert(2, {"target": "camera/fov", "component": -1, "timeBase": "seconds", "mode": "replace", "loopLength": 0.0,
                     "enabled": True, "keys": [{"time": round(t, 5), "value": [v], "interp": ip} for t, v, ip in fovk]})
        T.insert(3, {"target": "camera/journey/sway", "component": -1, "timeBase": "seconds", "mode": "replace",
                     "loopLength": 0.0, "enabled": True,
                     "keys": [{"time": round(t, 5), "value": [v], "interp": ip} for t, v, ip in sway]})
        for target, v in (("camera/journey/lookAtWeight", 1.0), ("camera/journey/height", 0.0),
                          ("camera/journey/bob", 0.0), ("camera/journey/pitch", 0.0), ("camera/journey/yaw", 0.0),
                          ("camera/journey/radius", 0.0), ("camera/journey/lookAhead", 2.0),
                          ("camera/journey/swayRate", 0.08)):
            T.append({"target": target, "component": -1, "timeBase": "seconds", "mode": "replace", "loopLength": 0.0,
                      "enabled": True, "keys": [{"time": 0.0, "value": [v], "interp": "step"},
                                                {"time": round(self.end, 3), "value": [v], "interp": "step"}]})
        grid = dict(grid_settings)
        grid["events"] = self.events
        sources = [{"kind": "beatgrid", "name": "song", "settings": grid}] + self.sources
        scene = {"format": "avgen-scene", "version": 1, "name": "All You Got (pass 2)",
                 "camera": {"mode": 3, "fov": 60.0, "journey": {"chapters": chapters}},
                 "environment": env, "nodes": self.nodes, "lights": self.lights}
        if getattr(self, "effects", None):
            scene["effects"] = self.effects
        rig = {"format": "avgen-lightrig", "version": 1, "name": "AllYouGot2",
               "description": "Dark: a faint ambient so the fill reads as planes; the lines and lamps carry the image.",
               "keyIntensity": 0.0, "ambientIntensity": 1.0, "ambientColor": list(rig_ambient), "ambientTemperature": 6500,
               "lights": [{"name": "key", "type": "directional", "role": "key", "azimuth": 120.0, "elevation": 40.0,
                           "distance": 2.0, "intensity": 0.0, "color": [1.0, 1.0, 1.0], "temperature": 6500,
                           "castsShadow": False, "volumetric": 0.0, "followCamera": False}]}
        project = {"format": "avgen-project", "version": 4, "app": {"name": "avgen", "version": "0.1.0"},
                   "assets": {"audio": {"path": "~/Desktop/All You Got.wav"},
                              "scene": {"kind": "composition", "path": f"{stem}.scene.json"}},
                   "parameters": params, "sources": sources, "routes": self.routes,
                   "timeline": {"enabled": True, "cues": [], "tracks": T},
                   "palette": {"states": self.palette_states, "bindings": self.bindings, "position": 0.0,
                               "saturation": 1.0, "value": 1.0},
                   "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "startSeconds": 0.0,
                              "endSeconds": self.end}}
        os.makedirs(out_dir, exist_ok=True)
        shots = [{"id": f"s{i + 1:02d}", "name": sh["name"], "start": round(sh["t0"], 3), "end": round(sh["t1"], 3)}
                 for i, sh in enumerate(self.shots)]
        with open(os.path.join(out_dir, f"{stem}.shots.json"), "w") as fh:
            json.dump(shots, fh, indent=1)
            fh.write("\n")
        for fname, doc in ((f"{stem}.scene.json", scene), (f"{stem}.rig.json", rig), (f"{stem}.json", project)):
            with open(os.path.join(out_dir, fname), "w") as f:
                json.dump(doc, f, indent=1)
                f.write("\n")
        return os.path.join(out_dir, f"{stem}.json")


def default_surfaces():
    """FILL, FLOOR, GLOW, SCREEN, CANVAS, CANVAS2, ACCENT, GLASS (kit.py). Colours and emissions are bound
    to the palette or keyed; these are starting values."""
    return [{"color": [0.02, 0.02, 0.025]}, {"color": [0.015, 0.015, 0.02]},
            {"color": [0.0, 0.0, 0.0], "emission": [2.0, 1.4, 0.7]}, {"color": [0.0, 0.0, 0.0], "emission": [0.4, 0.7, 1.0]},
            {"color": [0.0, 0.0, 0.0], "emission": [0.22, 0.22, 0.22]}, {"color": [0.0, 0.0, 0.0], "emission": [0.45, 0.45, 0.45]},
            {"color": [0.03, 0.025, 0.03]}, {"color": [0.0, 0.0, 0.0], "emission": [0.08, 0.12, 0.25]}]
