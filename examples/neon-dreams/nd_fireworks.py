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
at that second (azimuth -24..+24 deg, 110-240 m out, 6-17 deg over the lens's pitch, never closer than 35 m to each
other); at 56.85 s the lens is still inside the plug, so its shells burst 240-330 m ahead, where the lens comes out of
the cloud a moment later (a muffled glow in the cloud first). Two pools of sites alternate between crashes (no shell's
stars are still in the air when its pool is reused: the closest reuse is 12.6 s).

Everything is a timeline track keyed on the measured times: the render steps the particle simulation frame by frame
from the start, so a play and a full render agree; a still started in the middle of a shell's life does not have the
earlier frames' stars (as for every particle system in the scene).
"""
from __future__ import annotations

import math
import random

import nd_audio as AU

RISE = 1.05            # s from launch to burst
STARS_PER_SYSTEM = 420  # three systems a shell
POOLS = 2
SITES = 5              # per pool
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


def crashes():
    return [(t, v) for t, v in AU.EVENTS["stems"]["crash"]["onsets"]]


def ground_at(layout, x, z, r=26.0):
    """The highest layout instance base within r metres of (x, z), or None over open cloud."""
    best = None
    for inst in layout["instances"]:
        p = inst["pos"]
        if abs(p[0] - x) < r and abs(p[2] - z) < r and math.hypot(p[0] - x, p[2] - z) < r:
            best = p[1] if best is None else max(best, p[1])
    return best


def sites_for(t, strength, cam_at, layout, rng, n=None):
    """Burst points in view of the lens at second t."""
    eye, d, _roll, fov = cam_at(t)
    if n is None:
        n = 5 if strength >= 1.3 else (4 if strength >= 1.0 else 3)
    yaw = math.atan2(d[2], d[0])
    pitch = math.asin(max(-1.0, min(1.0, d[1])))
    half_v = math.radians(fov) * 0.5
    half_h = math.atan(math.tan(half_v) * 16.0 / 9.0)
    veil = 55.0 <= t <= 56.9
    out = []
    tries = 0
    while len(out) < n and tries < 400:
        tries += 1
        k = len(out)
        az = (-1.0 + 2.0 * (k + 0.5) / n) * 0.62 * half_h + rng.uniform(-0.06, 0.06) * half_h
        dist = rng.uniform(240.0, 330.0) if veil else rng.uniform(130.0, 200.0)
        # in the plug the lens is still climbing: its bursts sit level with it, where it looks once out of the cloud
        el = (rng.uniform(0.05, 0.3) * half_v) if veil else (pitch + rng.uniform(0.25, 0.68) * half_v)
        hx, hz = math.cos(yaw + az), math.sin(yaw + az)
        horiz = dist * math.cos(el)
        p = [eye[0] + hx * horiz, eye[1] + dist * math.sin(el), eye[2] + hz * horiz]
        if any(math.dist(p, q["burst"]) < 35.0 for q in out):
            continue
        g = ground_at(layout, p[0], p[2])
        base = (g + 4.0) if g is not None else p[1] - 75.0
        if p[1] - base < 30.0:   # at least a 30 m climb
            p[1] = base + 30.0 + rng.uniform(0.0, 20.0)
        out.append({"burst": [round(v, 2) for v in p], "launch": [round(p[0] + rng.uniform(-4, 4), 2), round(base, 2),
                                                                 round(p[2] + rng.uniform(-4, 4), 2)]})
    return out


def build(cam_at, layout, song=70.18):
    """(nodes, lights, tracks, info). it14 (owner msg 218: "only shoot up ONE firework each cymbal hit, and time the
    EXPLOSION with the cymbal hit"): one shell per crash, bursting on its attack; msg 220: each its own type."""
    rng = random.Random(206)
    order = list(range(len(TYPES)))
    random.Random(220).shuffle(order)
    nodes, lights, tracks, shells = [], [], [], []
    one = 1.0 / 30.0 - 0.002   # a burst window holding exactly one 30 fps frame's instant
    for k, (t, v) in enumerate(crashes()):
        site = sites_for(t, 0.0, cam_at, layout, rng, n=1)[0]
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
            {"time": 0.0, "value": 0.0, "interp": "step"}, {"time": tb, "value": 12.0, "interp": "step"},
            {"time": round(tb + one, 4), "value": 0.0, "interp": "step"}]))
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
                {"time": 0.0, "value": 0.0, "interp": "step"}, {"time": tj, "value": float(sysd["count"]), "interp": "step"},
                {"time": round(tj + one, 4), "value": 0.0, "interp": "step"}]))
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
