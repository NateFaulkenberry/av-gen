"""The Sonic VFX scene kit: authored live scenes as data (02-brief-vfx-expansion.md, deliverables 10 and 17).

Every scene in `tools/sonic_vfx/scenes/` builds one `Scene` with this kit and writes two files beside each other:

    examples/sonic-vfx/<id>.json        the project: `sonic.live`, the interpret mappings, the routes, the effects,
                                        the post and camera parameters, the camera's drift, and the scene's design
                                        record (`sonicScene`: thesis, composition layers, palette, motion tiers,
                                        modulation vocabulary) for the evaluator and for a person reading it
    examples/sonic-vfx/<id>.scene.json  what is in the world: nodes (procedural, sdf, particles, text), lights,
                                        material programs, entities, the environment, composition data (ADR-038)

Nothing here reads a file analysis: every scene runs on the live bus (signals.py), so it plays the same from a
keyboard and synth as from a replayed recording.

Conventions: metres, +Y up, colours linear RGB. A route is `R(source, target, amount, ...)` with the chain's fields as
keyword arguments; an interpret mapping is `M(name, inputs, ...)` with inputs `(signal, weight[, invert])`.
"""
from __future__ import annotations

import copy
import json
import math
import os
import zlib

from . import signals as sig
from .signals import S

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(REPO, "examples", "sonic-vfx")
GARDEN_LIVE = os.path.join(REPO, "examples", "sonic-garden", "sonic-live.json")


# ================================================================================================ colour
def hexrgb(h, scale=1.0):
    """sRGB hex -> linear RGB (what the engine's colours are)."""
    h = h.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        c = c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
        out.append(round(c * scale, 5))
    return out


def mix(a, b, t):
    return [round(x + (y - x) * t, 5) for x, y in zip(a, b)]


def scale3(a, s):
    return [round(x * s, 5) for x in a]


# ================================================================================================ routes, mappings
def R(src, target, amount, op="add", comp=None, depth=None, polarity="unipolar", enabled=True, **chain):
    """One modulation route. `src` may be a signal class (signals.py) or a bus id. Chain keys are the project
    format's (attackMs, decayMs, gain, offset, curve, curveAmount, threshold, thresholdLevel, envelope,
    envelopeHoldMs, envelopeFallPerSecond, clampEnabled, clampMin, clampMax, remap*, springHz, springDamping,
    integrate)."""
    r = {"source": S(src), "target": target, "amount": float(amount), "op": op, "polarity": polarity}
    if comp is not None:
        r["component"] = int(comp)
    if depth:
        r["depthSource"] = S(depth)
    if not enabled:
        r["enabled"] = False
    if chain:
        r["chain"] = chain
    return r


def M(name, inputs, combine="mean", bias=0.0, gain=1.0, curve=1.0, group=None):
    """One interpret mapping: `combine` (mean, sum, product, max, min) of weighted inputs, then x * gain + bias,
    clamped to 0..1, then raised to `curve`. Published as visual.<name>."""
    ins = []
    for item in inputs:
        s, w = item[0], item[1]
        inv = len(item) > 2 and item[2]
        d = {"signal": S(s), "weight": float(w)}
        if inv:
            d["invert"] = True
        ins.append(d)
    o = {"name": name, "combine": combine, "inputs": ins}
    if group:
        o["group"] = group
    if bias:
        o["bias"] = float(bias)
    if gain != 1.0:
        o["gain"] = float(gain)
    if curve != 1.0:
        o["curve"] = float(curve)
    return o


def bump(name, signal, centre, width, invert_ok=True):
    """Two mappings and a min: visual.<name> is 1 where `signal` == centre, falling to 0 at centre +- width. The
    interpret source's shaping is x * gain + bias; the rising edge is (x - (c - w)) / w and the falling edge
    ((1 - x) - (1 - c - w)) / w."""
    g = 1.0 / width
    up = M(name + "_up", [(signal, 1.0)], "mean", -(centre - width) * g, g)
    dn = M(name + "_dn", [(signal, 1.0, True)], "mean", -(1.0 - centre - width) * g, g)
    both = M(name, [("visual." + name + "_up", 1.0), ("visual." + name + "_dn", 1.0)], "min")
    return [up, dn], [both]


def place_bumps(prefix, signal, centres, width, event=None):
    """One responder per place. Returns (stage-1, stage-2, stage-3) mappings for three interpret sources (each source
    reads the earlier sources' outputs of the same frame):

      visual.<prefix><k>     1 where `signal` (usually notes.lastPitch) sits at centres[k], 0 at +- width;
      visual.<prefix>Hit<k>  with `event` (e.g. "noteOn"): that event's strength on the frame it fires, but only at
                             its place -- route this, not the place, when a note should leave a decaying mark (a
                             route's depth scales its output every frame, so a flash routed through a place depth is
                             cut off when the next note moves the pitch).

    Built within the interpret source's hard ranges (bias +-4, gain +-16): stage 1 maps the signal to a local
    coordinate that is 0.5 at the place (slope S), stage 2 cuts the rising and falling edges round 0.5, stage 3 takes
    their min. Peaks are 1."""
    S = min(16.0, min(4.4 / max(c, 1e-3) for c in centres), 3.4 / max(width, 1e-3) * 0.5 * 2)
    S = min(S, 0.45 / max(width, 1e-3))     # the edges' half-width is width, in signal units: 0.45 local
    stage1, stage2, stage3 = [], [], []
    edge_gain = 1.0 / (width * S)            # local units -> 0..1 over one width
    for k, c in enumerate(centres):
        loc = "%s%d_loc" % (prefix, k)
        stage1.append(M(loc, [(signal, 1.0)], "mean", 0.5 - S * c, S))
        # rising edge: 0 at local 0.5 - width*S, 1 at 0.5; falling edge mirrored
        b = -(0.5 - width * S) * edge_gain
        stage2.append(M("%s%d_up" % (prefix, k), [("visual." + loc, 1.0)], "mean", b, edge_gain))
        stage2.append(M("%s%d_dn" % (prefix, k), [("visual." + loc, 1.0, True)], "mean", b, edge_gain))
        stage3.append(M("%s%d" % (prefix, k), [("visual.%s%d_up" % (prefix, k), 1.0),
                                               ("visual.%s%d_dn" % (prefix, k), 1.0)], "min"))
        if event:
            stage3.append(M("%sHit%d" % (prefix, k), [("visual.%s%d_up" % (prefix, k), 1.0),
                                                      ("visual.%s%d_dn" % (prefix, k), 1.0), (event, 1.0)], "min"))
    for m in stage1 + stage2:
        assert -4.0 <= m.get("bias", 0.0) <= 4.0 and -16.0 <= m.get("gain", 1.0) <= 16.0, m
    return stage1, stage2, stage3


def lathe(profile, sides=32, samples=6, twist=0.0):
    """A radially symmetric form (a cap, a bowl, a coil, a vase): `profile` is [(height, radius), ...] from bottom to
    top. A tube along +Y whose cross-section radius follows the profile (the curve points' `scale`)."""
    r0 = max(r for _, r in profile) or 1.0
    pts = [{"position": [0.0, float(h), 0.0], "scale": round(float(r) / r0, 5), "roll": 0.0} for h, r in profile]
    return {"kind": "tube", "tubeRadius": float(r0), "tubeTaper": 1.0, "tubeSides": int(sides),
            "tubeSegments": max(2, len(profile) * 2), "tubeTwist": float(twist), "tubeCaps": True,
            "curve": {"kind": "catmullRom", "generator": "points", "points": pts, "samplesPerSegment": int(samples)}}


def strand(start, end, radius, taper=0.15, seed=1, noise=0.3, noise_scale=0.9, count=8, sides=8, segments=30):
    """A hanging or rising organic strand: a tube along a noisy Catmull-Rom curve from start to end."""
    return {"kind": "tube", "tubeRadius": float(radius), "tubeTaper": float(taper), "tubeSides": int(sides),
            "tubeSegments": int(segments), "tubeTwist": 0.0, "tubeCaps": True,
            "curve": {"kind": "catmullRom", "generator": "noise", "count": int(count),
                      "start": [float(v) for v in start], "end": [float(v) for v in end],
                      "noiseAmount": float(noise), "noiseScale": float(noise_scale), "seed": int(seed),
                      "samplesPerSegment": 8}}


# Chain presets, by the motion tier they serve (the brief's §13). Each is attack/decay in ms.
VERY_SLOW = {"attackMs": 2500, "decayMs": 5000}
SLOW = {"attackMs": 900, "decayMs": 2400}
MEDIUM = {"attackMs": 250, "decayMs": 900}
FAST = {"attackMs": 30, "decayMs": 350}
HIT = {"attackMs": 0, "decayMs": 180}       # an event's flash
SNAP = {"attackMs": 0, "decayMs": 70}       # extremely fast: a glitch, a glint
HOLD = {"envelope": "peakhold", "envelopeHoldMs": 0, "envelopeFallPerSecond": 3.0}


# ================================================================================================ SDF helpers
def _n(node, name=None, material=None):
    if name:
        node["name"] = name
    if material is not None:
        node["material"] = int(material)
    return node


def sd_sphere(r, name=None, m=None):
    return _n({"kind": "sphere", "radius": float(r)}, name, m)


def sd_box(half, name=None, m=None):
    return _n({"kind": "box", "size": [float(v) for v in half]}, name, m)


def sd_rbox(half, rounding, name=None, m=None):
    return _n({"kind": "roundedBox", "size": [float(v) for v in half], "rounding": float(rounding)}, name, m)


def sd_cyl(r, h, name=None, m=None):
    return _n({"kind": "cylinder", "radius": float(r), "height": float(h)}, name, m)


def sd_capsule(r, h, name=None, m=None):
    return _n({"kind": "capsule", "radius": float(r), "height": float(h)}, name, m)


def sd_torus(R_, r, name=None, m=None):
    return _n({"kind": "torus", "radius": float(R_), "rounding": float(r)}, name, m)


def sd_cone(r, h, name=None, m=None):
    return _n({"kind": "cone", "radius": float(r), "height": float(h)}, name, m)


def sd_plane(axis=(0, 1, 0), offset=0.0, name=None, m=None):
    return _n({"kind": "plane", "axis": [float(v) for v in axis], "offset": float(offset)}, name, m)


def _comb(kind, children, smooth=None, name=None, m=None):
    kids = [c for c in children if c is not None]
    o = {"kind": kind, "children": kids}
    if smooth is not None:
        o["smooth"] = float(smooth)
    return _n(o, name, m)


def sd_union(*c, name=None, m=None):
    kids = [k for k in c if k is not None]
    if len(kids) <= 8:
        return _comb("union", kids, name=name, m=m)
    # at most 8 children per combination: nest
    groups = [kids[i:i + 8] for i in range(0, len(kids), 8)]
    return _comb("union", [_comb("union", g) for g in groups], name=name, m=m)


def sd_smooth(k, *c, name=None, m=None):
    return _comb("smoothUnion", c, smooth=k, name=name, m=m)


def sd_inter(*c, name=None, m=None):
    return _comb("intersection", c, name=name, m=m)


def sd_diff(a, *cuts, name=None, m=None):
    return _comb("difference", (a,) + cuts, name=name, m=m)


def sd_sdiff(k, a, *cuts, name=None, m=None):
    return _comb("smoothDifference", (a,) + cuts, smooth=k, name=name, m=m)


def _unary(kind, child, name=None, m=None, **fields):
    o = {"kind": kind}
    for k, v in fields.items():
        if isinstance(v, (list, tuple)):
            o[k] = [float(x) for x in v]
        else:
            o[k] = v
    o["children"] = [child]
    return _n(o, name, m)


def sd_move(t, child, name=None, m=None):
    return _unary("translate", child, name, m, translation=list(t))


def sd_rot(deg, child, name=None, m=None):
    return _unary("rotate", child, name, m, rotation=list(deg))


def sd_scale(s, child, name=None, m=None):
    return _unary("scale", child, name, m, scale=float(s))


def sd_twist(amount, child, name=None, m=None):
    return _unary("twist", child, name, m, amount=float(amount))


def sd_bend(amount, child, name=None, m=None):
    return _unary("bend", child, name, m, amount=float(amount))


def sd_repeat(size, child, count=0, name=None, m=None):
    return _unary("repeat", child, name, m, size=list(size), count=int(count))


def sd_polar(count, child, name=None, m=None):
    return _unary("polarRepeat", child, name, m, count=int(count))


def sd_mirror(mask, child, name=None, m=None):
    return _unary("mirror", child, name, m, size=list(mask))


def sd_warp(amount, frequency, child, gain=(1, 1, 1), phase=(0, 0, 0), seed=1, name=None, m=None):
    return _unary("warp", child, name, m, amount=float(amount), frequency=float(frequency), size=list(gain),
                  translation=list(phase), seed=int(seed))


def sd_shell(thickness, child, name=None, m=None):
    return _unary("shell", child, name, m, offset=float(thickness))


def sd_noise(amount, frequency, child, speed=0.0, seed=1, name=None, m=None):
    return _unary("displaceNoise", child, name, m, amount=float(amount), frequency=float(frequency),
                  speed=float(speed), seed=int(seed))


def sd_voronoi(amount, frequency, child, seed=1, name=None, m=None):
    return _unary("displaceVoronoi", child, name, m, amount=float(amount), frequency=float(frequency),
                  seed=int(seed))


def sd_wave(amount, frequency, child, axis=(1, 0, 0), speed=0.0, name=None, m=None):
    return _unary("displaceWave", child, name, m, amount=float(amount), frequency=float(frequency),
                  axis=list(axis), speed=float(speed))


def sd_count(node):
    n = 1
    for c in node.get("children", []):
        n += sd_count(c)
    return n


# ================================================================================================ scene builder
class Scene:
    """One live scene: its world (scene file) and its instrument (project file)."""

    def __init__(self, sid, title, design):
        self.id = sid
        self.title = title
        self.design = design  # the sonicScene record: thesis, composition, palette, motion, vocabulary
        self.nodes = []
        self.lights = []
        self.programs = []
        self.entities = []
        self.effects = []
        self.mappings = []        # source "scene"
        self.mappings2 = []       # source "scene2": reads this frame's visual.* from the first
        self.mappings3 = []       # source "scene3": reads both
        self.routes = []
        self.params = {}
        self.tracks = []
        self.palette = None
        self.publish = []
        self.environment = {}
        self.camera = {}
        self.composition = {}
        self.character = None
        self.response = None
        self.regions = []         # the evaluator's screen-space regions (sonicScene.regions), see region()

    # ---------------------------------------------------------------- world
    def proc(self, name, source, distribution=None, material=None, deformers=None, variation=None,
             material_variation=None, transform=None, position=None, rotation=None, scale=None, parent=None,
             hierarchy=None, extra=None):
        p = {"source": source, "distribution": distribution or {"kind": "single"}}
        if transform:
            p["distributionTransform"] = transform
        if material:
            p["material"] = material
        if deformers:
            p["deformers"] = deformers
        if variation:
            p["variation"] = variation
        if material_variation:
            p["materialVariation"] = material_variation
        if hierarchy:
            p["hierarchy"] = hierarchy
        if extra:
            p.update(extra)
        n = {"name": name, "kind": "procedural", "procedural": p}
        if position is not None:
            n["position"] = [float(v) for v in position]
        if rotation is not None:
            n["rotation"] = [float(v) for v in rotation]
        if scale is not None:
            n["scale"] = [float(v) for v in scale]
        if parent:
            n["parent"] = parent
        self.nodes.append(n)
        return n

    def sdf(self, name, root, bounds_min, bounds_max, surfaces=None, look=None, material=None, position=(0, 0, 0),
            rotation=(0, 0, 0), max_steps=128, epsilon=0.001, step_scale=0.9, max_distance=0.0, shadows=False,
            prepass=False, normal_epsilon=0.002, visible=True):
        if sd_count(root) > 96:
            raise SystemExit(f"{self.id}: sdf '{name}' has {sd_count(root)} nodes (limit 96)")
        s = {"tree": {"root": root}, "renderMode": "raymarch", "compile": True,
             "material": material or {"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                                      "roughness": 0.6, "metallic": 0.0},
             "boundsMin": [float(v) for v in bounds_min], "boundsMax": [float(v) for v in bounds_max],
             "castShadows": bool(shadows), "depthPrepass": bool(prepass), "maxSteps": int(max_steps),
             "epsilon": float(epsilon), "stepScale": float(step_scale), "normalEpsilon": float(normal_epsilon)}
        if max_distance:
            s["maxDistance"] = float(max_distance)
        if surfaces:
            s["surfaces"] = surfaces
        if look:
            s["look"] = look
        n = {"name": name, "kind": "sdf", "visible": visible, "sdf": s,
             "position": [float(v) for v in position], "rotation": [float(v) for v in rotation]}
        self.nodes.append(n)
        return n

    def particles(self, name, **fields):
        p = {"enabled": True}
        p.update(fields)
        n = {"name": name, "kind": "particles", "particles": p}
        self.nodes.append(n)
        return n

    def text(self, name, text, size, depth, material, position, rotation=(0, 0, 0), font=None):
        src = {"kind": "text", "text": text, "textSize": float(size), "textDepth": float(depth),
               "font": font or {"family": "Futura", "weight": 0.6, "italic": False}}
        return self.proc(name, src, material=material, position=position, rotation=rotation)

    def light(self, name, ltype, **fields):
        l = {"id": name, "name": name, "type": ltype}
        for k, v in fields.items():
            l[k] = [float(x) for x in v] if isinstance(v, (list, tuple)) else v
        self.lights.append(l)
        return l

    def program(self, prog):
        self.programs.append(prog)
        return prog

    def entity(self, name, node=None, tags=None, **extra):
        # a stable seed: Python's str hash is salted per process, which made every build differ
        e = {"name": name, "node": node or name, "seed": zlib.crc32(name.encode()) % 1000000}
        if tags:
            e["tags"] = tags
        e.update(extra)
        self.entities.append(e)
        return e

    def effect(self, fid, ftype, owner=None, parameters=None, activation="always", timing=None, style="",
               trigger=None, order=0, name=None, extra=None):
        """An Effect Library instance (ADR-702). `owner` is ("world",) or ("entity", name) or ("camera",) or
        ("light", name). `trigger` (a dict, e.g. {"source": "signal", "name": "response.kick", "threshold": 0.3})
        makes it an EVENT activation."""
        own = {"kind": "world"}
        if owner:
            own = {"kind": owner[0]}
            if len(owner) > 1:
                own["name"] = owner[1]
        t = {"delay": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "lifetime": 0.0, "repeatSeconds": 0.0,
             "windowSeconds": 6.0, "windowStart": 0.0}
        if timing:
            t.update(timing)
        e = {"id": fid, "name": name or fid, "type": ftype, "owner": own, "enabled": True, "order": order,
             "activation": activation, "style": style, "timing": t, "parameters": parameters or {}}
        if trigger is not None:
            e["activation"] = "trigger"
            e["trigger"] = trigger
        if extra:
            e.update(extra)       # the shared rows a type uses at its top level: "ground", "flow", ...
        self.effects.append(e)
        return e

    # ---------------------------------------------------------------- instrument
    def map(self, *maps):
        for m in maps:
            if isinstance(m, list):
                self.mappings.extend(m)
            else:
                self.mappings.append(m)

    def map2(self, *maps):
        for m in maps:
            if isinstance(m, list):
                self.mappings2.extend(m)
            else:
                self.mappings2.append(m)

    def map3(self, *maps):
        for m in maps:
            if isinstance(m, list):
                self.mappings3.extend(m)
            else:
                self.mappings3.append(m)

    def places(self, prefix, signal, centres, width, event=None):
        a, b, c = place_bumps(prefix, signal, centres, width, event)
        self.map(*a)
        self.map2(*b)
        self.map3(*c)

    def route(self, *routes):
        for r in routes:
            if isinstance(r, list):
                self.routes.extend(r)
            else:
                self.routes.append(r)

    def param(self, path, value):
        self.params[path] = value

    def params_(self, d):
        self.params.update(d)

    def track(self, target, keys, loop=0.0, comp=-1, mode="replace"):
        self.tracks.append({"target": target, "component": comp, "enabled": True, "timeBase": "seconds",
                            "mode": mode, "loopLength": float(loop), "keys": keys})

    def drift_camera(self, centre, target, period=64.0, amp=(0.8, 0.12, 0.5), tamp=(0.12, 0.05, 0.0), steps=8,
                     phase=0.0):
        """A slow loop round a framing (the live demo's camera: never still, never following the music)."""
        kp, kt = [], []
        for i in range(steps + 1):
            t = period * i / steps
            a = 2.0 * math.pi * i / steps + phase
            dp = (amp[0] * math.sin(a), amp[1] * math.sin(2.0 * a), amp[2] * (1.0 - math.cos(a)) - amp[2] * 0.5)
            dt = (tamp[0] * math.sin(a + 0.6), tamp[1] * math.sin(2.0 * a + 1.0), tamp[2] * math.cos(a))
            kp.append({"time": round(t, 4), "value": [round(centre[k] + dp[k], 4) for k in range(3)],
                       "interp": "smooth"})
            kt.append({"time": round(t, 4), "value": [round(target[k] + dt[k], 4) for k in range(3)],
                       "interp": "smooth"})
        self.track("camera/position", kp, loop=period)
        self.track("camera/target", kt, loop=period)
        self.camera = {"mode": 1, "position": [float(v) for v in centre], "target": [float(v) for v in target],
                       "fov": self.camera.get("fov", 40.0), "orbitSpeed": 0.0}

    def orbit_camera(self, target, radius, height, period=120.0, start_deg=0.0, sweep_deg=360.0, steps=12,
                     tgt_bob=0.0, height_bob=0.0):
        """A camera on a horizontal circle (or arc swept back and forth) round `target`."""
        kp, kt = [], []
        full = abs(sweep_deg) >= 359.9
        for i in range(steps + 1):
            u = i / steps
            if full:
                a = math.radians(start_deg + sweep_deg * u)
            else:  # an arc there and back, smoothly
                a = math.radians(start_deg + sweep_deg * 0.5 * (1.0 - math.cos(2.0 * math.pi * u)))
            p = [target[0] + radius * math.sin(a), height + height_bob * math.sin(2.0 * math.pi * u),
                 target[2] + radius * math.cos(a)]
            tg = [target[0], target[1] + tgt_bob * math.sin(2.0 * math.pi * u + 1.0), target[2]]
            kp.append({"time": round(period * u, 4), "value": [round(v, 4) for v in p], "interp": "smooth"})
            kt.append({"time": round(period * u, 4), "value": [round(v, 4) for v in tg], "interp": "smooth"})
        self.track("camera/position", kp, loop=period)
        self.track("camera/target", kt, loop=period)
        self.camera = {"mode": 1, "position": kp[0]["value"], "target": kt[0]["value"],
                       "fov": self.camera.get("fov", 40.0), "orbitSpeed": 0.0}

    def arc_camera(self, focus, radius, height, period=120.0, centre_deg=0.0, sweep_deg=40.0, steps=16,
                   side=0.0, lift=0.0, height_bob=0.0):
        """A camera on an arc round `focus`, swept there and back, that keeps the focus OFF centre: the target is
        moved `side` metres to the camera's left (so a positive `side` puts the focus on the right of the frame)
        and `lift` metres up. The thirds, held while the view turns."""
        kp, kt = [], []
        for i in range(steps + 1):
            u = i / steps
            a = math.radians(centre_deg + 0.5 * sweep_deg * math.sin(2.0 * math.pi * u))
            p = [focus[0] + radius * math.sin(a), height + height_bob * math.sin(4.0 * math.pi * u),
                 focus[2] + radius * math.cos(a)]
            fwd = [focus[0] - p[0], 0.0, focus[2] - p[2]]
            n = math.hypot(fwd[0], fwd[2]) or 1.0
            right = [-fwd[2] / n, 0.0, fwd[0] / n]  # the camera's right, in the ground plane
            tg = [focus[0] - right[0] * side, focus[1] + lift, focus[2] - right[2] * side]
            kp.append({"time": round(period * u, 4), "value": [round(v, 4) for v in p], "interp": "smooth"})
            kt.append({"time": round(period * u, 4), "value": [round(v, 4) for v in tg], "interp": "smooth"})
        self.track("camera/position", kp, loop=period)
        self.track("camera/target", kt, loop=period)
        self.camera = {"mode": 1, "position": kp[0]["value"], "target": kt[0]["value"],
                       "fov": self.camera.get("fov", 40.0), "orbitSpeed": 0.0}

    # ---------------------------------------------------------------- screen-space regions (the evaluator's)
    def _camera_at(self, t=0.0):
        """The camera's position and target at time t from its tracks (else the scene camera block)."""
        def at(target, fallback):
            for tr in self.tracks:
                if tr["target"] == target and tr["keys"]:
                    keys = tr["keys"]
                    if tr.get("loopLength"):
                        t2 = t % tr["loopLength"]
                    else:
                        t2 = t
                    prev = keys[0]
                    for k in keys:
                        if k["time"] > t2:
                            u = (t2 - prev["time"]) / max(1e-6, k["time"] - prev["time"])
                            return [a + (b - a) * u for a, b in zip(prev["value"], k["value"])]
                        prev = k
                    return list(prev["value"])
            return fallback
        return at("camera/position", self.camera.get("position", [0, 0, 5])), \
            at("camera/target", self.camera.get("target", [0, 0, 0]))

    def project(self, point, t=0.0, aspect=16.0 / 9.0):
        """A world point's screen position (u right, v down, 0..1) and its depth, through the camera at time t: the
        engine's lens (vertical field of view from a 24 mm sensor height and the focal length)."""
        pos, tgt = self._camera_at(t)
        focal = float(self.params.get("camera/lens/focalLength", BASE_PARAMS["camera/lens/focalLength"]))
        fy = 1.0 / math.tan(math.atan(12.0 / focal))
        fx = fy / aspect
        f = [b - a for a, b in zip(pos, tgt)]
        n = math.sqrt(sum(c * c for c in f)) or 1.0
        f = [c / n for c in f]
        r = [-f[2], 0.0, f[0]]                       # cross(f, up) with up = +Y
        rn = math.sqrt(sum(c * c for c in r)) or 1.0
        r = [c / rn for c in r]
        u_ = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]  # cross(r, f)
        d = [b - a for a, b in zip(pos, point)]
        x = sum(a * b for a, b in zip(d, r))
        y = sum(a * b for a, b in zip(d, u_))
        z = sum(a * b for a, b in zip(d, f))
        z = max(z, 1e-3)
        return 0.5 + 0.5 * fx * x / z, 0.5 - 0.5 * fy * y / z, z

    def region(self, rid, centre=None, radius=None, box=None, t=0.0):
        """One evaluator region: `box` [x0, y0, x1, y1] in 0..1 screen units (y down), or a world `centre` and
        `radius` projected through the camera at time t (clamped to the frame)."""
        if box is None:
            u, v, z = self.project(centre, t)
            focal = float(self.params.get("camera/lens/focalLength", BASE_PARAMS["camera/lens/focalLength"]))
            fy = focal / 12.0
            hv = 0.5 * fy * radius / z
            hu = hv * 9.0 / 16.0
            box = [u - hu, v - hv, u + hu, v + hv]
        box = [round(min(1.0, max(0.0, c)), 3) for c in box]
        self.regions.append({"id": rid, "box": box})
        return box

    def region_points(self, rid, points, pad=0.0, t=0.0):
        """One evaluator region: the screen bounding box of world points (corners of a thing, samples round a ring),
        padded by `pad` screen units."""
        uv = [self.project(p, t)[:2] for p in points]
        box = [min(u for u, _ in uv) - pad, min(v for _, v in uv) - pad,
               max(u for u, _ in uv) + pad, max(v for _, v in uv) + pad]
        return self.region(rid, box=box)

    def region_ring(self, rid, centre, radius, n=16, pad=0.0, t=0.0, axis="y"):
        """The screen box of a horizontal (axis y) or vertical (axis z) ring round a world centre."""
        pts = []
        for i in range(n):
            a = 2.0 * math.pi * i / n
            if axis == "y":
                pts.append([centre[0] + radius * math.cos(a), centre[1], centre[2] + radius * math.sin(a)])
            else:
                pts.append([centre[0] + radius * math.cos(a), centre[1] + radius * math.sin(a), centre[2]])
        return self.region_points(rid, pts, pad, t)

    # ---------------------------------------------------------------- output
    def scene_doc(self):
        doc = {"format": "avgen-scene", "version": 1, "name": self.id, "camera": self.camera,
               "environment": self.environment, "nodes": self.nodes}
        if self.lights:
            doc["lights"] = self.lights
        if self.programs:
            doc["materialPrograms"] = self.programs
        if self.entities:
            doc["entities"] = self.entities
        if self.composition:
            doc["composition"] = self.composition
        return doc

    def project_doc(self):
        with open(GARDEN_LIVE) as f:
            live = json.load(f)
        character = self.character or copy.deepcopy(live["sonic"]["character"])
        sonic = {"live": True, "character": character}
        if self.response:
            sonic["response"] = self.response
        sources = []
        if self.mappings:
            sources.append({"kind": "interpret", "name": "scene", "settings": {"mappings": self.mappings}})
        if self.mappings2:
            sources.append({"kind": "interpret", "name": "scene2", "settings": {"mappings": self.mappings2}})
        if self.mappings3:
            sources.append({"kind": "interpret", "name": "scene3", "settings": {"mappings": self.mappings3}})
        params = dict(BASE_PARAMS)
        params.update(self.params)
        doc = {"format": "avgen-project", "version": 4, "app": {"name": "Sonic VFX"},
               "assets": {"scene": {"kind": "composition", "path": self.id + ".scene.json"}},
               "sonic": sonic, "parameters": params, "sources": sources, "routes": self.routes, "presets": [],
               "timeline": {"enabled": True, "cues": [], "tracks": self.tracks},
               "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video",
                          "path": "renders/sonic-vfx-" + self.id + ".mp4"},
               "sonicScene": dict(self.design, id=self.id, title=self.title)}
        if self.regions:
            doc["sonicScene"]["regions"] = self.regions
        if self.effects:
            doc["effects"] = self.effects
        if self.palette:
            doc["palette"] = self.palette
        if self.publish:
            doc["publish"] = self.publish
        return doc

    def write(self, out_dir=OUT_DIR):
        os.makedirs(out_dir, exist_ok=True)
        sp = os.path.join(out_dir, self.id + ".scene.json")
        pp = os.path.join(out_dir, self.id + ".json")
        with open(sp, "w") as f:
            json.dump(self.scene_doc(), f, indent=1)
            f.write("\n")
        with open(pp, "w") as f:
            json.dump(self.project_doc(), f, indent=1)
            f.write("\n")
        return pp, sp


# Live-friendly defaults every scene starts from (a scene overrides what it needs). The fog march runs 24 steps live
# (the offline tier's floor raises it for renders, ADR-919).
BASE_PARAMS = {
    "post/bloom/enabled": True,
    "post/bloom/intensity": 0.35,
    "post/bloom/threshold": 1.0,
    "post/bloom/emissionWeight": 0.7,
    "post/tonemap/chroma-retention": 0.6,
    "post/output/vignette": 0.35,
    "post/output/grain": 0.01,
    "camera/exposure/mode": 0,
    "camera/exposure/compensation": 0.0,
    "camera/lens/useExplicitFov": False,
    "camera/lens/focalLength": 35.0,
    "scene/volumeSteps": 24,
    "scene/volumeJitter": 0.4,
    "temporal/echo/enabled": False,
    "temporal/mosh/enabled": False,
}


def write_index(scenes, category="Sonic VFX"):
    """Lists every scene in the Examples menu (examples/index.json), replacing this category's entries."""
    path = os.path.join(REPO, "examples", "index.json")
    with open(path) as f:
        idx = json.load(f)
    keep = [e for e in idx["examples"] if e.get("category") != category]
    for sc in scenes:
        keep.append({"category": category,
                     "description": sc.design.get("thesis", "") + " Live: opening it turns live input on "
                     "(View > Live). See docs/prototypes/sonic-garden/SCENE-CATALOG.md.",
                     "name": "Sonic VFX - " + sc.title,
                     "project": "sonic-vfx/" + sc.id + ".json"})
    idx["examples"] = keep
    with open(path, "w") as f:
        json.dump(idx, f, indent=2)
        f.write("\n")
