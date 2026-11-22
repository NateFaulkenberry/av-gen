"""Neon Dreams iteration 10 (owner msg 117): jetpack flights for the Glowmere cast.

A flight is data: who flies (a cast GLB with a jetpack: alien-pilot or alien-ranger carry `Backpack_Jet`), where
from (a spot: position and facing), where to, when the jump starts and when the feet touch down, and optional
via points. Everything is written as timeline tracks and sequence clip cues -- pure functions of the timeline second,
so a seek lands where a play does:

  idle at A  ->  Jumping (once; the clip leaves the ground at 0.567 s)  ->  Flying_jet (loop) while the node climbs,
  cruises on a Catmull-Rom curve through the via points with an eased speed, leans into its speed, banks into its
  turns and bobs  ->  Landing (once, touchdown at 0.267 s of the clip)  ->  Idle at B.

The thruster: a small emissive flame (a stretched sphere, parented to the flyer at the jet's nozzle) and a point light
under it, both keyed on with the lift-off and off at touchdown, with a deterministic flicker.
The clip times are the engine's own measurements (`avgen_motion semantics assets/aliens/alien-pilot.glb`).
"""
from __future__ import annotations

import math

import nd_audio as AU

JUMP_TAKEOFF = 0.567      # Jumping: the feet leave the ground
JUMP_TO_HOVER = 0.62      # the cue into Floating (an upright hover, the body 0.4 m up) just after the feet leave
LAND_TOUCHDOWN = 0.267    # Landing: the feet reach the ground
LAND_LEN = 1.067
KEY_HZ = 30.0             # flight keys a second (it12: 30, one a frame; 15 kinked the path every second frame)
# it12 (msg 149): the loops on the song's grid (171.4 BPM): Floating 6.5 s = 18 beats, Flying_jet 2.9 s = 8 beats
BEAT_RATE = {"Floating": round(6.5 / (18 * AU.BEAT), 4), "Flying_jet": round(2.9 / (8 * AU.BEAT), 4)}   # it13: measured beat
# The jet's nozzle (the bottom of Backpack_Jet, through the Backpack joint) and its thrust direction in the flyer's
# own frame, per clip, measured from alien-pilot.glb's clips (work/engine/it10/fly/nozzle.py in the studio project):
# Jumping and Landing upright, Floating upright and raised, Flying_jet prone (the jet blows back along -z).
NOZZLE = {"Idle": ([0.133, 0.764, -0.251], [0.12, -0.95, -0.28]),
          "Jumping": ([0.081, 0.822, -0.256], [0.08, -0.99, -0.10]),
          "Floating": ([0.072, 1.22, -0.204], [0.07, -0.77, -0.63]),
          "Flying_jet": ([-0.006, 0.984, -0.107], [0.01, -0.11, -0.99]),
          "Landing": ([0.177, 1.46, -0.227], [0.36, -0.70, -0.62])}
# iteration 10: the art's jet_plume.glb (two chains of lantern bulbs from the nozzles, its -y along the thrust) is
# placed on the art's socket (joint Backpack, (0, 0.31, 0.01)), which the table above is measured at, per clip.
PLUME = "../meshes/props/jet_plume.glb"
# kick puffs while hovering (the art's audio idea 1): +60 % gain, +15 % length, 15 ms in, 220 ms out
PUFF = (0.6, 0.15, 0.015, 0.22)


def _catmull(p0, p1, p2, p3, u):
    u2, u3 = u * u, u * u * u
    return [0.5 * ((2 * p1[i]) + (-p0[i] + p2[i]) * u + (2 * p0[i] - 5 * p1[i] + 4 * p2[i] - p3[i]) * u2 +
                   (-p0[i] + 3 * p1[i] - 3 * p2[i] + p3[i]) * u3) for i in range(3)]


def _curve(points, per=40):
    pts = [points[0]] + points + [points[-1]]
    out = []
    for k in range(1, len(pts) - 2):
        for j in range(per):
            out.append(_catmull(pts[k - 1], pts[k], pts[k + 1], pts[k + 2], j / per))
    out.append(points[-1])
    cum = [0.0]
    for k in range(1, len(out)):
        cum.append(cum[-1] + math.dist(out[k - 1], out[k]))
    return out, cum


def _at(curve, cum, s):
    s = max(0.0, min(cum[-1], s))
    lo, hi = 0, len(cum) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if cum[mid] <= s:
            lo = mid
        else:
            hi = mid
    d = cum[hi] - cum[lo]
    u = (s - cum[lo]) / d if d > 0 else 0.0
    return [curve[lo][i] + (curve[hi][i] - curve[lo][i]) * u for i in range(3)]


def _ease(u):
    """Speed eases in and out (smootherstep of arc length over time)."""
    u = max(0.0, min(1.0, u))
    return u * u * u * (u * (6 * u - 15) + 10)


def _euler_zyx(yaw, pitch, roll):
    """Degrees for glm::quat(vec3) (Rz * Ry * Rx) equal to Ry(yaw) * Rx(pitch) * Rz(roll): the body yaws, then leans
    forward (pitch about its own x), then banks (roll about its own forward z)."""
    def rx(a):
        c, s = math.cos(a), math.sin(a)
        return [[1, 0, 0], [0, c, -s], [0, s, c]]

    def ry(a):
        c, s = math.cos(a), math.sin(a)
        return [[c, 0, s], [0, 1, 0], [-s, 0, c]]

    def rz(a):
        c, s = math.cos(a), math.sin(a)
        return [[c, -s, 0], [s, c, 0], [0, 0, 1]]

    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    m = mul(mul(ry(math.radians(yaw)), rx(math.radians(pitch))), rz(math.radians(roll)))
    # m = Rz(c) Ry(b) Rx(a): m[2][0] = -sin b, m[2][1] = cos b sin a, m[2][2] = cos b cos a, m[1][0] = sin c cos b
    b = math.asin(max(-1.0, min(1.0, -m[2][0])))
    a = math.atan2(m[2][1], m[2][2])
    c = math.atan2(m[1][0], m[0][0])
    return [math.degrees(a), math.degrees(b), math.degrees(c)]


def _flicker(name, t):
    """A deterministic thruster flicker in [0.8, 1.15]: three incommensurate sines, phased by the flyer's name."""
    h = sum(ord(ch) * (k + 1) for k, ch in enumerate(name)) % 997
    return 0.97 + 0.09 * math.sin(31.0 * t + h) + 0.05 * math.sin(53.0 * t + 2.1 * h) + 0.04 * math.sin(97.0 * t + h * 0.7)


def _mat_to_euler(m):
    """Degrees for glm::quat(vec3) (Rz * Ry * Rx) from a rotation matrix (rows)."""
    b = math.asin(max(-1.0, min(1.0, -m[2][0])))
    a = math.atan2(m[2][1], m[2][2])
    c = math.atan2(m[1][0], m[0][0])
    return [math.degrees(a), math.degrees(b), math.degrees(c)]


def _down_to(d):
    """The rotation taking local -y onto direction d (degrees, glm order): the flame hangs along the thrust."""
    n = math.sqrt(sum(v * v for v in d))
    d = [v / n for v in d]
    src = [0.0, -1.0, 0.0]
    ax = [src[1] * d[2] - src[2] * d[1], src[2] * d[0] - src[0] * d[2], src[0] * d[1] - src[1] * d[0]]
    s = math.sqrt(sum(v * v for v in ax))
    c = sum(src[i] * d[i] for i in range(3))
    if s < 1e-6:
        return [0.0, 0.0, 0.0] if c > 0 else [180.0, 0.0, 0.0]
    x, y, z = [v / s for v in ax]
    ang = math.atan2(s, c)
    C, S, T = math.cos(ang), math.sin(ang), 1 - math.cos(ang)
    m = [[T * x * x + C, T * x * y - S * z, T * x * z + S * y],
         [T * x * y + S * z, T * y * y + C, T * y * z - S * x],
         [T * x * z - S * y, T * y * z + S * x, T * z * z + C]]
    return _mat_to_euler(m)


def _clip_weights(cues, t):
    """Each clip's weight at t under the cue list's cross-fades (each cue fades in over its blend, linearly)."""
    w = {}
    for time, clip, blend in cues:
        if t < time:
            break
        u = 1.0 if blend <= 0 else min(1.0, (t - time) / blend)
        for k in w:
            w[k] *= (1.0 - u)
        w[clip] = w.get(clip, 0.0) + u
    return w


# ---- iteration 10b (owner msg 121: "the flight path looks crazy", "make the exhaust glow look more realistic") ----
G = 9.81
BANK_MAX = 20.0
# iteration 11 (msg 130): the plume's soft envelope and the deck's sparks and dust (additive / alpha particles)
SOFT = {"rate": 420.0}
SOFT_SYSTEM = {"enabled": True, "capacity": 512, "shape": "sphere", "position": [0.0, 0.0, 0.0],
               "extent": [0.07, 0.07, 0.07], "spawnRate": 0.0, "burst": 0.0, "lifetimeMin": 0.12, "lifetimeMax": 0.26,
               "direction": [0.0, -1.0, 0.0], "spread": 0.06, "speedMin": 3.2, "speedMax": 5.5, "gravity": [0.0, 0.0, 0.0],
               "drag": 2.5, "turbulence": 0.9, "turbulenceScale": 2.5, "turbulenceSpeed": 1.5, "sizeStart": 0.06,
               "sizeEnd": 0.2, "colorStart": [0.62, 0.78, 1.0, 0.85], "colorEnd": [1.0, 0.32, 0.07, 0.0],
               "emissive": 4.0, "blend": "additive", "softness": 1.0}
SPARKS = {"_rate": 520.0, "enabled": True, "capacity": 512, "shape": "disc", "position": [0.0, 0.0, 0.0],
          "extent": [0.25, 0.25, 0.25], "spawnRate": 0.0, "burst": 0.0, "lifetimeMin": 0.35, "lifetimeMax": 0.9,
          "direction": [0.0, 1.0, 0.0], "spread": 0.75, "speedMin": 1.5, "speedMax": 4.2, "gravity": [0.0, -9.8, 0.0],
          "drag": 0.6, "turbulence": 0.0, "sizeStart": 0.022, "sizeEnd": 0.008, "colorStart": [1.0, 0.82, 0.5, 1.0],
          "colorEnd": [1.0, 0.28, 0.05, 0.0], "emissive": 9.0, "blend": "additive", "velocityStretch": 0.7,
          "stretchMax": 0.12}
DUST = {"_rate": 260.0, "enabled": True, "capacity": 512, "shape": "disc", "position": [0.0, 0.0, 0.0],
        "extent": [0.35, 0.35, 0.35], "spawnRate": 0.0, "burst": 0.0, "lifetimeMin": 0.7, "lifetimeMax": 1.5,
        "direction": [0.0, 1.0, 0.0], "spread": 0.95, "speedMin": 0.8, "speedMax": 2.4, "gravity": [0.0, 0.25, 0.0],
        "drag": 2.2, "turbulence": 0.5, "turbulenceScale": 1.5, "turbulenceSpeed": 0.6, "sizeStart": 0.12,
        "sizeEnd": 0.55, "colorStart": [0.42, 0.36, 0.5, 0.32], "colorEnd": [0.3, 0.26, 0.4, 0.0], "emissive": 0.6,
        "blend": "alpha", "softness": 1.0}
PLUME_SPLIT = 0.095        # each nozzle's offset from the pack's centre line (m, the jet mesh: +-0.163 wide)


def _chaikin(pts, n=3):
    """Corner cutting: the via polyline becomes a smooth, curvature-limited path that keeps its ends."""
    for _ in range(n):
        out = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            out.append([0.75 * a[i] + 0.25 * b[i] for i in range(len(a))])
            out.append([0.25 * a[i] + 0.75 * b[i] for i in range(len(a))])
        out.append(pts[-1])
        pts = out
    return pts


def _poly(pts):
    cum = [0.0]
    for k in range(1, len(pts)):
        cum.append(cum[-1] + math.dist(pts[k - 1], pts[k]))
    return pts, cum


def _poly_at(pts, cum, s):
    s = max(0.0, min(cum[-1], s))
    lo, hi = 0, len(cum) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if cum[mid] <= s:
            lo = mid
        else:
            hi = mid
    d = cum[hi] - cum[lo]
    u = (s - cum[lo]) / d if d > 0 else 0.0
    return [pts[lo][i] + (pts[hi][i] - pts[lo][i]) * u for i in range(len(pts[0]))]


def _cruise(u, length):
    """Iteration 11 (art request 5, msg 136): the share of the route covered at time share u. A short hop keeps the
    smootherstep (peak 1.9x its mean speed: the motion msg 132 liked); a long cruise (> 40 m) eases in and out over its
    first and last 18 % and flies the middle at a constant speed (peak 1.22x the mean), so a flyer keeps pace with the
    camera instead of racing ahead in the middle."""
    u = max(0.0, min(1.0, u))
    if length <= 40.0:
        return _S(u)
    e = 0.18
    v = 1.0 / (1.0 - e)                 # the middle's speed (area under the trapezoid = 1)
    if u < e:
        return 0.5 * v * u * u / e
    if u > 1.0 - e:
        w = 1.0 - u
        return 1.0 - 0.5 * v * w * w / e
    return 0.5 * v * e + v * (u - e)


def _S(u):
    u = max(0.0, min(1.0, u))
    return u * u * u * (u * (6 * u - 15) + 10)


def _noise1(seed, t):
    """A smooth deterministic noise in about [-1, 1]: incommensurate sines (no state, seek-safe)."""
    return (0.5 * math.sin(7.3 * t + seed) + 0.3 * math.sin(13.1 * t + 2.3 * seed) + 0.2 * math.sin(29.7 * t + 0.7 * seed))


def _smooth(vals, k):
    out = []
    for i in range(len(vals)):
        lo, hi = max(0, i - k), min(len(vals), i + k + 1)
        out.append(sum(vals[lo:hi]) / (hi - lo))
    return out


def plume_program(name):
    """A hot blue-white core shading to amber and a dull red tip, fading down its length with a turbulent flicker running
    down it; a thin tapered tube from local (0,0,0) to (0,-1,0), cut where it has faded (mask: the procedural pass has no
    blending), its softness from the bloom round a thin bright core."""
    ops = [   # eight registers (0..7)
        {"kind": "input", "input": "localPosition", "dst": 0},
        {"kind": "gradient", "dst": 1, "srcA": 0, "constant": [0.0, -1.0, 0.0, 0.0], "value": 1.0},      # v: 0 nozzle, 1 tip
        {"kind": "input", "input": "time", "dst": 2},
        {"kind": "constant", "dst": 3, "constant": [0.0, 9.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
        {"kind": "add", "dst": 2, "srcA": 0, "srcB": 2},                                                  # flows down
        {"kind": "noise", "dst": 3, "srcA": 2, "value": 9.0, "seed": 3},
        {"kind": "mix", "dst": 1, "srcA": 1, "srcB": 3, "value": 0.25},                                    # a ragged length
        {"kind": "oklabRamp", "dst": 4, "srcA": 1, "constant": [0.75, 0.88, 1.0, 1.0], "constant2": [1.0, 0.9, 0.62, 1.0],
         "constant3": [1.0, 0.48, 0.12, 1.0], "constant4": [0.55, 0.1, 0.02, 1.0], "value": 1.0},
        {"kind": "gradient", "dst": 5, "srcA": 1, "constant": [-1.0, 0.0, 0.0, 1.0], "value": 1.0},      # 1 - v
        {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.0, 1.0, 0, 0]},
        {"kind": "power", "dst": 5, "srcA": 5, "value": 2.2},
        {"kind": "gradient", "dst": 6, "srcA": 3, "constant": [0.6, 0.0, 0.0, 0.7], "value": 1.0},       # flicker 0.7-1.3
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "constant", "dst": 7, "constant": [0.0, 0.0, 0.0, 1.0]},
    ]
    return {"name": name, "ops": ops, "baseColor": 7, "metallic": -1, "roughness": -1, "emission": 4,
            "emissionIntensity": 16.0, "opacity": 5}


def flight(f, r3, song, light_color, flame_color):
    """One flight: nodes (the flyer, two plumes), a light, timeline tracks, a sequence actor; f["_samples"] gets the
    path (every 0.1 s, before any formation bob) for the clearance check."""
    name = f["name"]
    a, b = f["from"], f["to"]
    t0, td = f["t0"], f["td"]
    t_lift = t0 + JUMP_TAKEOFF
    t_land = td - LAND_TOUCHDOWN                       # the Landing cue: its feet reach B at td
    A, B = a["pos"], b["pos"]
    via = [list(v) for v in f.get("via", [])]
    if not via:
        cy = max(A[1], B[1]) + f.get("alt", 8.0)
        via = [[(A[0] + B[0]) / 2, cy, (A[2] + B[2]) / 2]]
    # over B it arrives at the route's top plus 2 m and comes straight down (open sky, checked): a glide in on a slant
    # passes through whatever stands round a deck (the Great Lantern's strings and gallery, msg 123)
    y1, yN = max(via[0][1], A[1] + 3.0), max(max(v[1] for v in via) + 2.0, B[1] + 3.0)
    h_up, h_down = y1 - A[1], yN - B[1]
    T_up = max(1.4, min(3.0, h_up / 2.6))
    T_dn = max(1.6, min(3.2, h_down / 2.2))
    total = t_land - t_lift
    if T_up + T_dn + 1.0 > total:                      # a short hop: the climb and descent share the time
        k = total / (T_up + T_dn + 1.0)
        T_up, T_dn = T_up * k, T_dn * k
    t_hover = t_lift + T_up                            # at height
    t_desc = t_land - T_dn                             # leaving height
    # the route in 3D (the art's ray-checked cruise, heights and all), from over A to over B, corner-cut smooth
    route = [[A[0], y1, A[2]]] + via + [[B[0], yN, B[2]]]
    hp, hc = _poly(_chaikin(route, 2))
    L = hc[-1]
    h0 = min(t_hover - 0.35, t_lift + 0.75 * T_up)     # it starts drifting while still rising: one motion, no corner
    h1 = t_desc + 0.3 * T_dn                           # and is over the deck early in the descent: it comes down upright

    def pos_at(t):
        u = _cruise((t - h0) / max(h1 - h0, 1e-3), L)
        r = _poly_at(hp, hc, L * u)
        c = _S((t - t_lift) / T_up)
        d = _S((t - t_desc) / T_dn)
        y = A[1] * (1 - c) + r[1] * c * (1 - d) + B[1] * d
        w = min(1.0, max(0.0, (t - t_hover + 0.5) / 1.0), max(0.0, (t_desc + 0.3 - t) / 1.0))
        y += 0.12 * math.sin(2 * math.pi * 0.55 * (t - t_lift)) * w      # a gentle hover bob
        return [r[0], y, r[2]]

    dt = 1.0 / 30.0
    ts = [t_lift + k * dt for k in range(int(total / dt) + 1)] + [t_land]
    P = [pos_at(t) for t in ts]
    V = [[(P[min(i + 1, len(P) - 1)][j] - P[max(i - 1, 0)][j]) / (2 * dt) for j in range(3)] for i in range(len(P))]
    Acc = [[(V[min(i + 1, len(V) - 1)][j] - V[max(i - 1, 0)][j]) / (2 * dt) for j in range(3)] for i in range(len(V))]
    # facing: the take-off facing turns toward the route while rising, then follows the horizontal velocity
    tan0 = [hp[min(3, len(hp) - 1)][0] - hp[0][0], hp[min(3, len(hp) - 1)][2] - hp[0][2]]
    yaw_route0 = math.degrees(math.atan2(tan0[0], tan0[1])) if math.hypot(*tan0) > 1e-6 else a["yaw"]
    yaws, prev = [], a["yaw"]
    for i, t in enumerate(ts):
        hs = math.hypot(V[i][0], V[i][2])
        vel_yaw = math.degrees(math.atan2(V[i][0], V[i][2])) if hs > 0.05 else prev
        turn_in = _S((t - t_lift) / max(T_up, 0.5))
        y_pre = a["yaw"] + ((yaw_route0 - a["yaw"] + 180) % 360 - 180) * turn_in
        w = _S(hs / 2.0)
        y = y_pre + ((vel_yaw - y_pre + 180) % 360 - 180) * w
        if b.get("yaw") is not None:
            wb = _S((t - (t_land - 1.2)) / 1.2)
            y = y + ((b["yaw"] - y + 180) % 360 - 180) * wb
        while y - prev > 180:
            y -= 360
        while y - prev < -180:
            y += 360
        yaws.append(y)
        prev = y
    yaws = _smooth(yaws, 4)
    pitches, rolls = [], []
    for i in range(len(ts)):
        hs = math.hypot(V[i][0], V[i][2])
        yr = math.radians(yaws[i])
        fwd = [math.sin(yr), math.cos(yr)]
        a_fwd = Acc[i][0] * fwd[0] + Acc[i][2] * fwd[1]
        a_side = Acc[i][0] * fwd[1] - Acc[i][2] * fwd[0]
        pitches.append(max(-10.0, min(22.0, 1.6 * hs + math.degrees(math.atan2(a_fwd, G)))))
        rolls.append(max(-BANK_MAX, min(BANK_MAX, math.degrees(math.atan2(a_side, G)))))
    pitches, rolls = _smooth(pitches, 5), _smooth(rolls, 5)
    pk, rk = [], []
    for i in range(0, len(ts), 1):                     # it12 (msg 149): 30 Hz keys (it10: 15 Hz, a kink every 2nd frame)
        pk.append({"time": round(ts[i], 3), "value": r3(P[i]), "interp": "linear"})
        rk.append({"time": round(ts[i], 3), "value": [round(v, 2) for v in _euler_zyx(yaws[i], pitches[i], rolls[i])],
                   "interp": "linear"})
    f["_samples"] = [[round(ts[i], 2), r3(P[i])] for i in range(0, len(ts), 3)]
    f["_lift"] = (h_up, h_down)
    rot_a = [0.0, round(a["yaw"], 1), 0.0]
    rot_b = [0.0, round(yaws[-1], 1), 0.0]
    t_end = round(min(song, td + 0.5), 3)
    pk = [{"time": 0.0, "value": r3(A), "interp": "linear"}, {"time": round(t_lift - 1e-3, 3), "value": r3(A), "interp": "linear"}] + \
        pk[1:] + [{"time": round(t_land, 3), "value": r3(B), "interp": "linear"}, {"time": t_end, "value": r3(B), "interp": "linear"}]
    pk = sorted({k["time"]: k for k in pk}.values(), key=lambda k: k["time"])
    rk = [{"time": 0.0, "value": rot_a, "interp": "linear"}, {"time": round(t_lift - 1e-3, 3), "value": rot_a, "interp": "linear"}] + \
        rk[1:] + [{"time": t_end, "value": rot_b, "interp": "linear"}]
    rk = sorted({k["time"]: k for k in rk}.values(), key=lambda k: k["time"])
    # it12 (msg 149, the 4.09 s "glitching/skipping"): the ZYX decomposition flips a component between +180 and -180
    # (fly_pier_isle at 3.92 s: X 180 -> -179.99); keys interpolate component-wise, so the body spun through a whole
    # turn in one frame. Unwrap each component to the nearest equivalent of the key before.
    # Each key also has a second solution, (x + 180, 180 - y, z + 180): the one nearer the key before is taken (the
    # take-off key (0, 157, 0) and the first flight key (-180, 23, -180) are one rotation, not a half turn apart).
    for q in range(1, len(rk)):
        pv, v = rk[q - 1]["value"], rk[q]["value"]
        best = None
        for cand in (v, [v[0] + 180.0, 180.0 - v[1], v[2] + 180.0]):
            u = [pv[c] + ((cand[c] - pv[c] + 180.0) % 360.0 - 180.0) for c in range(3)]
            dist = sum(abs(u[c] - pv[c]) for c in range(3))
            if best is None or dist < best[0]:
                best = (dist, u)
        rk[q]["value"] = [round(x, 2) for x in best[1]]
    cues = [(t0, "Jumping", 0.25), (t0 + JUMP_TO_HOVER, "Floating", 0.45), (t_land, "Landing", 0.25)]

    # ---- the thrust: what the jets must do to make this motion (gravity plus the vertical acceleration), so the plumes
    # are longest at lift-off and in the braking before touchdown, and short in the hover
    def thrust_at(t):
        if t < t_lift - 0.2 or t > td + 0.05:
            return 0.0
        i = max(0, min(len(ts) - 1, int(round((t - t_lift) / dt))))
        ay = Acc[i][1] if t_lift <= t <= t_land else 0.0
        on = _S((t - (t_lift - 0.2)) / 0.25) * (1.0 - _S((t - (td - 0.25)) / 0.3))
        push = 0.9 * math.exp(-max(0.0, t - t_lift) / 0.35) if t >= t_lift - 0.2 else 0.0   # the push-off burst
        return on * max(0.4, (1.0 + ay / G)) + push
    kicks = f.get("kicks", [])
    flames = [name + "_plumeL", name + "_plumeR"]
    lamp = name + "_jet"
    sk = {n: [] for n in flames}
    fpk = {n: [] for n in flames}
    frk, lk, lpk = [], [], []
    t = max(0.0, t_lift - 0.4)
    sk0 = [{"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"}]
    rate = [{"time": 0.0, "value": 0.0, "interp": "linear"}]   # it11: the soft plume's particles a second
    npk = []
    lk0 = [{"time": 0.0, "value": 0.0, "interp": "linear"}]
    seed = sum(ord(c) for c in name)
    while t < td + 0.6:
        th = thrust_at(t)
        wts = _clip_weights(cues, t)
        hover = wts.get("Floating", 0.0) / (sum(wts.values()) or 1.0)
        env = 0.0
        for k in kicks:
            if k - PUFF[2] <= t <= k + 5 * PUFF[3]:
                env = max(env, min(1.0, (t - k + PUFF[2]) / PUFF[2]) * math.exp(-max(0.0, t - k) / PUFF[3]))
        tot = sum(wts.values()) or 1.0
        nz = [sum(NOZZLE.get(c, NOZZLE["Floating"])[0][i] * wv for c, wv in wts.items()) / tot for i in range(3)]
        dr = [sum(NOZZLE.get(c, NOZZLE["Floating"])[1][i] * wv for c, wv in wts.items()) / tot for i in range(3)]
        rot = [round(v, 2) for v in _down_to(dr)]
        frk.append({"time": round(t, 3), "value": rot, "interp": "linear"})
        for side, n in zip((-1, 1), flames):
            nn = _noise1(seed + side * 17, t)
            length = th * (0.36 + 0.06 * nn) * (1.0 + PUFF[1] * env * hover)
            length = min(0.85, length)
            width = 0.0 if th <= 0 else 1.0 + 0.08 * _noise1(seed + side * 5, 1.7 * t)
            sk[n].append({"time": round(t, 3), "value": r3([width, max(length, 0.0), width]), "interp": "linear"})
            fpk[n].append({"time": round(t, 3), "value": r3([nz[0] + side * PLUME_SPLIT, nz[1], nz[2]]), "interp": "linear"})
        lk.append({"time": round(t, 3), "value": round(f.get("light", 55.0) * th * (0.85 + 0.15 * _noise1(seed, 2 * t))
                                                       * (1.0 + PUFF[0] * env * hover), 3), "interp": "linear"})
        lpk.append({"time": round(t, 3), "value": r3([nz[i] + 0.6 * dr[i] for i in range(3)]), "interp": "linear"})
        rate.append({"time": round(t, 3), "value": round(SOFT["rate"] * min(th, 1.6) * (1.0 + PUFF[1] * env * hover), 1),
                     "interp": "linear"})
        npk.append({"time": round(t, 3), "value": r3([nz[i] + 0.05 * dr[i] for i in range(3)]), "interp": "linear"})
        t += 1.0 / 30.0
    end = round(td + 0.7, 3)

    def track(target, keys):
        return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
                "loopLength": 0.0, "keys": keys}
    nodes = [{"name": name, "kind": "gltf", "asset": f["asset"], "position": r3(A), "rotation": rot_a,
              "scale": [1.0, 1.0, 1.0],
              "animation": {"state": "Idle", "blend": 0.3, "phase": f.get("phase", 0.0), "updateHz": 0,
                            "nearDistance": 40.0, "farHz": 0.0, "cullDistance": 300.0}}]   # it12: posed every frame
    tracks = [track("nodes/%s/position" % name, pk), track("nodes/%s/rotation" % name, rk)]
    for side, n in zip((-1, 1), flames):
        nodes.append({"name": n, "kind": "procedural", "parent": name, "position": NOZZLE["Jumping"][0],
                      "scale": [0.0, 0.0, 0.0], "procedural": {
                          "source": {"kind": "tube", "tubeRadius": 0.045, "tubeTaper": 0.1, "tubeSides": 10,
                                     "tubeSegments": 12, "tubeTwist": 0.0, "tubeCaps": False,
                                     "curve": {"kind": "catmullRom", "generator": "points", "closed": False,
                                               "points": [{"position": [0.0, -0.01 * k, 0.0]} for k in (0, 50, 100)]}},
                          "distribution": {"kind": "points", "points": [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]]},
                          "castsShadow": False,
                          "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndJetPlume",
                                       "alphaMode": "mask", "alphaCutoff": 0.06}}})
        tracks += [track("nodes/%s/scale" % n, sk0 + sk[n] + [{"time": end, "value": [0.0, 0.0, 0.0], "interp": "linear"}]),
                   track("nodes/%s/position" % n, fpk[n]), track("nodes/%s/rotation" % n, frk)]
    # iteration 11 (msg 130): the soft plume -- additive particles from the nozzles (soft-edged, they fade and widen
    # down the jet; the thin core tube stays the hot line), and sparks and dust kicked off the deck at the push-off
    # and at touchdown. The flight motion is unchanged (owner msg 132).
    soft = name + "_soft"
    nodes.append({"name": soft, "kind": "particles", "parent": name, "position": npk[0]["value"] if npk else [0, 1, 0],
                  "particles": dict(SOFT_SYSTEM, seed=seed % 997)})
    tracks += [track("nodes/%s/position" % soft, npk),
               track("particles/%s/spawnRate" % soft, rate + [{"time": end, "value": 0.0, "interp": "linear"}])]
    for tag, P0, tt, bob in (("off", A, t_lift, f.get("bob_from")), ("on", B, td, f.get("bob_to"))):
        for kind, sysd in (("spark", SPARKS), ("dust", DUST)):
            n = "%s_%s%s" % (name, kind, tag)
            ps = dict(sysd, seed=(seed + len(n)) % 997)
            if kind == "spark" and not bob:
                ps.update({"collision": "bounce", "collisionHeight": round(P0[1] + 0.02, 3), "collisionRestitution": 0.35})
            node = {"name": n, "kind": "particles", "position": r3([P0[0], P0[1] + 0.06, P0[2]]), "particles": ps}
            if bob:
                node["parent"] = "bob_" + bob   # a deck on a bobbing isle: the burst rides it (ADR-2112's group node)
            nodes.append(node)
            a0, a1 = (tt - 0.12, tt + 0.28) if tag == "off" else (tt - 0.22, tt + 0.12)
            peak = sysd["_rate"]
            tracks.append(track("particles/%s/spawnRate" % n, [
                {"time": 0.0, "value": 0.0, "interp": "linear"}, {"time": round(a0, 3), "value": 0.0, "interp": "linear"},
                {"time": round(a0 + 0.05, 3), "value": peak, "interp": "linear"},
                {"time": round(a1 - 0.05, 3), "value": peak * 0.6, "interp": "linear"},
                {"time": round(a1, 3), "value": 0.0, "interp": "linear"}]))
            node["particles"].pop("_rate", None)
    lights = [{"name": lamp, "type": "point", "node": name, "position": NOZZLE["Jumping"][0],
               "color": light_color, "intensity": 0.0, "range": 12.0, "castsShadow": False, "volumetric": 0.0}]
    tracks += [track("lights/%s/intensity" % lamp, lk0 + lk + [{"time": end, "value": 0.0, "interp": "linear"}]),
               track("lights/%s/position" % lamp, lpk)]
    clips = []
    for time, clip, blend in cues:
        c = {"time": round(time, 3), "clip": clip, "speed": BEAT_RATE.get(clip, 1.0), "blend": blend,
             "playback": "once" if clip in ("Jumping", "Landing") else "loop"}
        if clip == "Landing":
            c["then"] = "Idle"
        clips.append(c)
    actor = {"id": name, "node": name, "visible": True, "keys": [], "clips": clips}
    return nodes, lights, tracks, actor
