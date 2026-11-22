"""Neon Dreams iteration 14 (owner msg 206: "we're missing audio reactivity on the crash hits -- add a fireworks animation
that shoots up from places in the city on the crash notes: big vibrant fireworks that rain down rainbow particles,
illuminating the environment").

What AV Gen has: Rainbow World's fireworks (examples/rainbow-world/build_world.py, gates 5-7) are a technique, not a
system -- instanced spark meshes pushed out and down by scene fields that ride its scan front. Here the shells are
events in time, not a front in space, so the same recipe (a white-hot ignition, sparks flying out and slowing (drag),
arcing down (gravity), cooling from white through the look's colours, crackling as they die) is built on the engine's
GPU particle systems, which already have every term: a one-frame `burst`, full-sphere `spread`, `drag`, `gravity`,
colour and size over life (`colorCurve`, `sizeCurve`), `pulseRate` (the crackle) and `velocityStretch` (streaks).

Per crash (the Crash stem's onsets at their attacks, AU.EVENTS): 3-5 shells, each BURSTING on the crash; the rocket
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
LIGHT = {"intensity": 2400.0, "range": 190.0}
STAR = {
    "capacity": 2048, "shape": "sphere", "extent": [0.4, 0.4, 0.4], "spawnRate": 0.0, "burst": 0.0,
    "lifetimeMin": 1.6, "lifetimeMax": 2.7, "direction": [0.0, 1.0, 0.0], "spread": 1.0,
    "speedMin": 15.0, "speedMax": 27.0, "gravity": [0.0, -7.5, 0.0], "drag": 1.15, "turbulence": 0.6,
    "turbulenceScale": 0.08, "turbulenceSpeed": 0.4, "sizeStart": 0.7, "sizeEnd": 0.12, "emissive": 16.0,
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
    "lifetimeMin": 0.16, "lifetimeMax": 0.28, "direction": [0.0, 1.0, 0.0], "spread": 1.0,
    "speedMin": 0.5, "speedMax": 2.0, "gravity": [0.0, 0.0, 0.0], "drag": 0.5, "turbulence": 0.0,
    "sizeStart": 9.0, "sizeEnd": 14.0, "colorStart": [1.0, 0.97, 0.92, 1.0], "colorEnd": [1.0, 0.8, 0.9, 0.0],
    "emissive": 10.0, "blend": "additive", "softness": 0.0}


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


def sites_for(t, strength, cam_at, layout, rng):
    """Burst points in view of the lens at second t."""
    eye, d, _roll, fov = cam_at(t)
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
        dist = rng.uniform(240.0, 330.0) if veil else rng.uniform(110.0, 240.0)
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
    """(nodes, lights, tracks, info) -- the pools' particle nodes and lights, and their keyed tracks."""
    rng = random.Random(206)
    nodes, lights, tracks, shells = [], [], [], []
    plan = {}   # (pool, site) -> list of shells
    for k, (t, v) in enumerate(crashes()):
        for s, site in enumerate(sites_for(t, v, cam_at, layout, rng)):
            sh = dict(site, t=round(t, 4), crash=k, hue=round(rng.random(), 3))
            plan.setdefault((k % POOLS, s), []).append(sh)
            shells.append(sh)
    for (pool, s), uses in sorted(plan.items()):
        tag = "fw%d%d" % (pool, s)
        first = uses[0]
        # the rocket: its emitter rides the climb, a trail of sparks while it rises
        nodes.append({"name": tag + "_rocket", "kind": "particles", "position": first["launch"],
                      "particles": dict(ROCKET, seed=11 + 7 * pool + s)})
        nodes.append({"name": tag + "_flash", "kind": "particles", "position": first["burst"],
                      "particles": dict(FLASH, seed=13 + 7 * pool + s)})
        stars = []
        for j in range(3):
            nm = "%s_star%d" % (tag, j)
            stars.append(nm)
            nodes.append({"name": nm, "kind": "particles", "position": first["burst"],
                          "particles": dict(STAR, seed=101 + 31 * pool + 7 * s + j, **star_curves(first["hue"] + j / 3.0))})
        light = tag + "_light"
        lights.append({"name": light, "type": "point", "position": first["burst"], "color": [1.0, 0.85, 0.95],
                       "intensity": LIGHT["intensity"], "range": LIGHT["range"], "castsShadow": False, "volumetric": 0.0})
        rp, rr, sp, lp, li, lc, fb, sb = [], [], [], [], [], [], [], []
        lp.append({"time": 0.0, "value": first["burst"], "interp": "step"})
        rr.append({"time": 0.0, "value": 0.0, "interp": "step"})
        li.append({"time": 0.0, "value": 0.0, "interp": "linear"})
        for u in uses:
            tb, L, B = u["t"], u["launch"], u["burst"]
            t0 = tb - RISE
            # the climb: ease-out (fast off the roof, slowing into the burst)
            for i in range(13):
                x = i / 12.0
                e = 1.0 - (1.0 - x) ** 2
                p = [round(L[c] + (B[c] - L[c]) * e, 3) for c in range(3)]
                rp.append({"time": round(t0 + RISE * x, 4), "value": p, "interp": "linear"})
            rr += [{"time": round(t0 - 0.001, 4), "value": 0.0, "interp": "step"},
                   {"time": round(t0, 4), "value": 260.0, "interp": "step"},
                   {"time": round(tb - 0.02, 4), "value": 0.0, "interp": "step"}]
            sp.append({"time": round(tb - 0.5, 4), "value": B, "interp": "step"})
            # one frame of burst (the window holds exactly one 30 fps frame's instant)
            one = 1.0 / 30.0 - 0.002
            fb += [{"time": round(tb, 4), "value": 40.0, "interp": "step"},
                   {"time": round(tb + one, 4), "value": 0.0, "interp": "step"}]
            sb += [{"time": round(tb, 4), "value": float(STARS_PER_SYSTEM), "interp": "step"},
                   {"time": round(tb + one, 4), "value": 0.0, "interp": "step"}]
            col = [round(sum(hue(u["hue"] + j / 3.0)[c] for j in range(3)) / 3.0 * 0.6 + 0.4, 3) for c in range(3)]
            lp.append({"time": round(tb - 0.5, 4), "value": B, "interp": "step"})
            lc.append({"time": round(tb - 0.5, 4), "value": col, "interp": "step"})
            I = LIGHT["intensity"]
            li += [{"time": round(tb - 0.002, 4), "value": 0.0, "interp": "linear"},
                   {"time": round(tb + 0.05, 4), "value": I, "interp": "linear"},
                   {"time": round(tb + 0.35, 4), "value": round(0.45 * I, 1), "interp": "linear"},
                   {"time": round(tb + 1.0, 4), "value": round(0.15 * I, 1), "interp": "linear"},
                   {"time": round(tb + 1.9, 4), "value": 0.0, "interp": "linear"}]
        rp.sort(key=lambda k: k["time"])
        tracks.append(_track("nodes/%s_rocket/position" % tag, rp))
        tracks.append(_track("particles/%s_rocket/spawnRate" % tag, [{"time": 0.0, "value": 0.0, "interp": "step"}] + rr))
        tracks.append(_track("nodes/%s_flash/position" % tag, [{"time": 0.0, "value": first["burst"], "interp": "step"}] + sp))
        tracks.append(_track("particles/%s_flash/burst" % tag, [{"time": 0.0, "value": 0.0, "interp": "step"}] + fb))
        for nm in stars:
            tracks.append(_track("nodes/%s/position" % nm, [{"time": 0.0, "value": first["burst"], "interp": "step"}] + sp))
            tracks.append(_track("particles/%s/burst" % nm, [{"time": 0.0, "value": 0.0, "interp": "step"}] + sb))
        tracks.append(_track("lights/%s/position" % light, lp))
        if lc:
            tracks.append(_track("lights/%s/color" % light, [{"time": 0.0, "value": lc[0]["value"], "interp": "step"}] + lc))
        tracks.append(_track("lights/%s/intensity" % light, li))
    return nodes, lights, tracks, {"shells": shells}
