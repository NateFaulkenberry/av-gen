#!/usr/bin/env python3
"""Writes CHRONOS, The Devourer's Gate D2 scene and projects: the procession (Test B) and a single soul consumed
(Test C). docs/prototypes/chronos/12-devourer-recon-plan.md §5-§6.

The creature is the D1c head in its `devour` state (the jaw hangs open and gulps every 3 s; the head is turned toward
the procession). Fifteen souls ride one path in from the right, low and far, sweep toward the camera and curve up into
the mouth, 3 s apart, so one arrives at every gulp. Each soul is a node with its own `temporalSlot` (ADR-1934), and
its stage is a pure function of where it is on the path:

  A  far:        intact (trail 0)
  B  nearer:     echoes -- a short trail broken into copies (`echo` frames apart), the copies closing up
  C  nearer:     the stretch -- a long continuous trail along the path
  D  close:      fracture -- strips across the body at different moments, over the stretch
  E  at the lips: the squeeze -- the node itself narrows into a ribbon and is drawn into the throat, which hides it

Everything is keyed on the timeline as a function of time (scrub-safe): positions, orientations and scales of the
nodes, and the slot parameters. The souls (build_souls.py) and the creature (build_scan_creature.py) are built into
assets/chronos/build/, gitignored. Audio by relative path, never committed. Out of examples/index.json.

    python3 examples/chronos/devourer/build_d2.py
"""
import json
import math
import os
import random

import build_scene as base

HERE = os.path.dirname(os.path.abspath(__file__))
N = 15              # souls, one slot each
GAP = 3.0           # seconds between souls = the gulp period of the `devour` clip
LOOP = N * GAP      # 45 s
TRAVEL = 20.0       # seconds a soul spends on the path; parked off frame for the rest
GULP = 2.2          # the gulp's peak within each 3 s cycle of the clip (jaw keys 2.0-2.4 s)
DT = 0.25           # key spacing
FPS = 30.0
STRIDE = 1
LAYERS = 64
SPAN = 60.0         # frames (2 s at 30 fps)

# The path, in the scene's metres (glTF frame, Y up). The mouth is at about (1.2, 25.6, 12) with the head turned
# toward the camera in the devour state; the last point is inside the throat, behind the teeth.
# Laid out in the master camera's screen space and solved back to the world: in from beyond the right edge, far
# beside and behind the head, sweeping toward the camera in front of the neck and across to the lips.
PATH = [(24.0, 27.5, -92.0), (20.2, 26.8, -74.9), (13.5, 25.9, -31.7), (14.4, 26.0, -3.9), (14.7, 26.9, 10.0), (13.3, 27.8, 16.7), (9.6, 27.5, 16.7), (5.3, 26.8, 14.1), (2.1, 25.9, 10.8), (-1.0, 25.0, 6.9), (-4.7, 23.8, 2.2)]
LIPS = 8            # the path point at the lips


def catmull(points, n=400):
    out = []
    pts = [points[0]] + points + [points[-1]]
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for k in range(n // (len(pts) - 3)):
            t = k / (n // (len(pts) - 3))
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in range(3)))
    out.append(points[-1])
    return out


CURVE = catmull(PATH)
ARC = [0.0]
for a, b in zip(CURVE, CURVE[1:]):
    ARC.append(ARC[-1] + math.dist(a, b))
ARC = [x / ARC[-1] for x in ARC]
# where on the arc the lips are
MOUTH_S = ARC[min(range(len(CURVE)), key=lambda i: math.dist(CURVE[i], PATH[LIPS]))]


def at(s):
    s = min(max(s, 0.0), 1.0)
    for i in range(1, len(ARC)):
        if ARC[i] >= s:
            f = (s - ARC[i - 1]) / max(ARC[i] - ARC[i - 1], 1e-9)
            a, b = CURVE[i - 1], CURVE[i]
            return tuple(a[j] + (b[j] - a[j]) * f for j in range(3)), tuple(b[j] - a[j] for j in range(3))
    return CURVE[-1], tuple(CURVE[-1][j] - CURVE[-2][j] for j in range(3))


def progress(u):
    """Arc fraction for the fraction u of the travel time: the procession slows as it nears the lips (the stages have
    time to read), then each soul is pulled into the throat, faster and faster."""
    k = 0.86
    if u < k:
        x = u / k
        return MOUTH_S * (0.3 * (1.0 - (1.0 - x) ** 1.8) + 0.7 * x)
    return MOUTH_S + (1.0 - MOUTH_S) * ((u - k) / (1.0 - k)) ** 1.6


def norm(v):
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(x / n for x in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def euler_zyx(m):
    """(x, y, z) degrees with R = Rz(z) Ry(y) Rx(x) -- glm::quat(vec3)'s order, the engine's node rotation."""
    y = math.asin(max(-1.0, min(1.0, -m[2][0])))
    x = math.atan2(m[2][1], m[2][2])
    z = math.atan2(m[1][0], m[0][0])
    return [math.degrees(x), math.degrees(y), math.degrees(z)]


def orient(tangent, roll):
    """The soul's local +Y (its head) along the travel, local +Z (its face) up, turned by `roll` about the travel."""
    yv = norm(tangent)
    up = (0.0, 1.0, 0.0)
    zv = norm(tuple(up[i] - yv[i] * sum(up[k] * yv[k] for k in range(3)) for i in range(3)))
    xv = cross(yv, zv)
    c, s = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    xr = tuple(c * xv[i] + s * zv[i] for i in range(3))
    zr = tuple(-s * xv[i] + c * zv[i] for i in range(3))
    m = [[xr[0], yv[0], zr[0]], [xr[1], yv[1], zr[1]], [xr[2], yv[2], zr[2]]]
    return euler_zyx(m)


def unwrap(prev, cur):
    return [c + 360.0 * round((p - c) / 360.0) for p, c in zip(prev, cur)]


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def stage(s):
    """The slot's parameters at arc fraction s (MOUTH_S is the lips)."""
    m = MOUTH_S
    b = smooth(0.45 * m, 0.58 * m, s)          # echoes appear
    c = smooth(0.62 * m, 0.78 * m, s)          # ...close up into the stretch
    d = smooth(0.80 * m, 0.94 * m, s)          # fracture
    trail = 16.0 * b + 30.0 * c
    echo = 8.0 * b * (1.0 - c) + 1.0 * c       # two copies 8 frames apart, closing to continuous
    fade = 0.75 - 0.25 * c
    fracture = 0.3 * d
    return trail, echo if b > 0 else 0.0, fade, fracture


def squeeze(s, k):
    """Stage E, in the node's own scale: a ribbon along the travel, drawn thin and then to nothing in the throat."""
    m = MOUTH_S
    e = smooth(0.93 * m, m, s)
    gone = smooth(m, m + 0.6 * (1.0 - m), s)
    w = (1.0 - 0.7 * e) * (1.0 - 0.97 * gone)
    length = (1.0 + 0.9 * e) * (1.0 - 0.9 * gone)
    return [w * k, length * k, w * k]


def soul_tracks(index, single, arrival=None):
    rng = random.Random(1934 + index)
    slot = index + 1
    name = f"soul{slot}"
    roll = rng.uniform(-35.0, 35.0)
    size = rng.uniform(2.8, 3.3)   # hand-sized against the 33 m head, as Saturn's victim is in Goya
    # soul `index` reaches the lips at the gulp of cycle `index`
    arrive = GULP + GAP * (index if arrival is None else arrival)
    start = arrive - TRAVEL * (MOUTH_S ** (1.0 / 1.0))   # approximately; refined below by searching the time warp
    # find u_lips: progress(u) == MOUTH_S
    lo, hi = 0.0, 1.0
    for _ in range(50):
        mid = 0.5 * (lo + hi)
        lo, hi = (mid, hi) if progress(mid) < MOUTH_S else (lo, mid)
    start = arrive - TRAVEL * lo
    pos, rot, scl = [], [], []
    trail, echo, fade, frac = [], [], [], []
    prev = None
    steps = int(round(LOOP / DT))
    for i in range(steps + 1):
        t = i * DT
        u = ((t - start) % LOOP) / TRAVEL
        on = u <= 1.0
        s = progress(u) if on else 0.0
        p, tan = at(s)
        if not on:
            p, tan = at(0.0)
        e = orient(tan, roll)
        e = unwrap(prev, e) if prev else e
        prev = e
        k = squeeze(s, size) if on else [size, size, size]
        st = stage(s) if on else (0.0, 0.0, 0.65, 0.0)
        pos.append((t, [round(v, 3) for v in p]))
        rot.append((t, [round(v, 2) for v in e]))
        scl.append((t, [round(v, 4) for v in k]))
        trail.append((t, round(st[0], 2)))
        echo.append((t, round(st[1], 2)))
        fade.append((t, round(st[2], 3)))
        frac.append((t, round(st[3], 3)))

    def track(target, keys, comp=-1):
        return {"target": target, "component": comp, "enabled": True, "timeBase": "seconds", "mode": "replace",
                "loopLength": LOOP, "keys": [{"time": t, "value": v, "interp": "linear"} for t, v in keys]}

    sl = f"temporal/slit/slot{slot}/"
    return [track(f"nodes/{name}/position", pos), track(f"nodes/{name}/rotation", rot),
            track(f"nodes/{name}/scale", scl), track(sl + "trail", trail), track(sl + "echo", echo),
            track(sl + "fade", fade), track(sl + "fracture", frac)]


def scene():
    sc = base.scene()
    sc["name"] = "chronos-devourer-d2"
    sc["_note"] = ("CHRONOS, The Devourer, Gate D2: the procession and the consumption. Built by "
                   "examples/chronos/devourer/build_d2.py; the creature and souls GLBs are in assets/chronos/build/.")
    sc["nodes"][0]["animation"] = {"state": "devour", "blend": 0.0}
    # a cold, low light along the procession so the pale souls read against the dark before the mouth's crimson
    # (narrow, and aimed down the far procession: a wider cone reached the face and flattened its chiaroscuro)
    sc["lights"].append(base.spot("procession", (60.0, 40.0, 20.0), (16.0, 24.0, -30.0), [0.62, 0.66, 0.78], 1500.0,
                                  12.0, 22.0, False, "fill"))
    for i in range(N):
        sc["nodes"].append({"name": f"soul{i + 1}", "kind": "gltf",
                            "asset": f"../../../assets/chronos/build/souls/soul-{i % 8 + 1}.glb",
                            "position": list(PATH[0]), "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0],
                            "temporalSlot": i + 1})
    return sc


def project(single):
    pr = base.project("three-quarter")
    pr["assets"]["scene"]["path"] = "devourer-d2.scene.json"
    params = pr["parameters"]
    params.update({"temporal/slit/enabled": True, "temporal/slit/amount": 1.0, "temporal/slit/span": SPAN,
                   "temporal/slit/gate": 1.0, "temporal/history/capacity": float(LAYERS),
                   "temporal/history/stride": float(STRIDE)})
    for i in range(N):
        a = 160.0  # the travel's direction on screen (uv, y down): right to left and a little down
        params[f"temporal/slit/slot{i + 1}/strips"] = 26.0
        params[f"temporal/slit/slot{i + 1}/angle"] = round(a, 1)
    tracks = pr["timeline"]["tracks"]
    for i in range(N):
        if single and i != 0:
            params[f"nodes/soul{i + 1}/visible"] = False
            continue
        # Test C: the one soul arrives at the gulp of cycle 6 (20.2 s), so a 24 s clip holds its whole journey
        tracks.extend(soul_tracks(i, single, 6 if single else None))
    pr["render"]["path"] = "renders/devourer-d2-" + ("single" if single else "procession") + ".mp4"
    return pr


if __name__ == "__main__":
    with open(os.path.join(HERE, "devourer-d2.scene.json"), "w") as f:
        json.dump(scene(), f, indent=1)
    for single, name in ((False, "procession"), (True, "single")):
        with open(os.path.join(HERE, f"devourer-d2-{name}.json"), "w") as f:
            json.dump(project(single), f, indent=None, separators=(",", ":"))
    print("wrote devourer-d2.scene.json, devourer-d2-procession.json, devourer-d2-single.json; lips at arc", MOUTH_S)
