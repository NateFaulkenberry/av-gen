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

JUMP_TAKEOFF = 0.567      # Jumping: the feet leave the ground
JUMP_TO_HOVER = 0.62      # the cue into Floating (an upright hover, the body 0.4 m up) just after the feet leave
LAND_TOUCHDOWN = 0.267    # Landing: the feet reach the ground
LAND_LEN = 1.067
KEY_HZ = 15.0             # flight keys a second (linear between: smooth at 30 fps)
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


def flight(f, r3, song, light_color, flame_color):
    """One flight: nodes (the flyer, its flame), lights, timeline tracks, and a sequence actor (clip cues)."""
    name = f["name"]
    a, b = f["from"], f["to"]
    t0, td = f["t0"], f["td"]
    t_lift = t0 + JUMP_TAKEOFF
    t_land = td - LAND_TOUCHDOWN                      # the Landing cue: its feet reach B at td
    alt = f.get("alt", 8.0)
    via = [list(v) for v in f.get("via", [])]
    up_a = [a["pos"][0], a["pos"][1] + alt * 0.6, a["pos"][2]]
    up_b = [b["pos"][0], b["pos"][1] + alt * 0.45, b["pos"][2]]
    if not via:
        mid = [(a["pos"][i] + b["pos"][i]) / 2 for i in range(3)]
        mid[1] = max(a["pos"][1], b["pos"][1]) + alt
        via = [mid]
    curve, cum = _curve([a["pos"], up_a] + via + [up_b, b["pos"]])
    total = cum[-1]
    fly = t_land - t_lift
    yaw_a = a["yaw"]
    yaw_b = b.get("yaw", None)

    def s_of(t):
        return total * _ease((t - t_lift) / fly)

    def pos_at(t):
        return _at(curve, cum, s_of(t))

    def t_of(s_target):
        lo, hi = t_lift, t_land
        for _ in range(40):
            mid = (lo + hi) / 2
            if s_of(mid) < s_target:
                lo = mid
            else:
                hi = mid
        return lo
    # the climb and the descent are the curve's vertical ends: Floating there, Flying_jet (prone) in between
    climb_len = next(cum[k] for k in range(len(cum)) if curve[k][1] >= up_a[1] - 0.3)
    desc_len = next(cum[k] for k in range(len(cum) - 1, -1, -1) if curve[k][1] >= up_b[1] - 0.3)
    t_cruise = t_of(climb_len)
    t_desc = t_of(desc_len)
    cues = [(t0, "Jumping", 0.25), (t0 + JUMP_TO_HOVER, "Floating", 0.4)]
    if t_desc - t_cruise > 1.5:
        cues += [(t_cruise - 0.3, "Flying_jet", 0.7), (t_desc - 0.6, "Floating", 0.7)]
    cues += [(t_land, "Landing", 0.2)]

    def bob_w(t):
        return max(0.0, min(1.0, (t - t_lift) / 1.2, (t_land - t) / 1.2))
    n = max(2, int(fly * KEY_HZ))
    prev_yaw = yaw_a
    samples = []
    for k in range(n + 1):
        t = t_lift + fly * k / n
        p = pos_at(t)
        p[1] += 0.14 * math.sin(2 * math.pi * 0.65 * (t - t_lift)) * bob_w(t)
        q = pos_at(min(t_land, t + 0.12))
        o = pos_at(max(t_lift, t - 0.12))
        vx, vz = (q[0] - o[0]) / 0.24, (q[2] - o[2]) / 0.24
        hs = math.hypot(vx, vz)
        yaw = math.degrees(math.atan2(vx, vz)) if hs > 0.05 else prev_yaw
        while yaw - prev_yaw > 180:
            yaw -= 360
        while yaw - prev_yaw < -180:
            yaw += 360
        w = min(1.0, hs / 1.5)                      # standing climb: keep the facing it took off with
        samples.append((t, p, yaw, w, hs))
        prev_yaw = yaw
    yaws = []
    for t, p, yaw, w, hs in samples:
        y0 = yaw_a + ((yaw - yaw_a + 180) % 360 - 180) * w
        if yaw_b is not None:
            wb = max(0.0, min(1.0, 1.0 - (t_land - t) / 1.6))
            y0 = y0 + ((yaw_b - y0 + 180) % 360 - 180) * wb * wb
        yaws.append(y0)
    pk, rk = [], []
    for i, (t, p, yaw, w, hs) in enumerate(samples):
        dy = (yaws[min(i + 1, len(yaws) - 1)] - yaws[max(i - 1, 0)]) / max(2.0 / KEY_HZ, 1e-6)  # deg/s
        upright = _clip_weights(cues, t).get("Floating", 0.0)
        pitch = max(-6.0, min(14.0, hs * 1.6)) * upright      # an upright hover leans into its travel; prone does not
        roll = max(-30.0, min(30.0, -dy * hs * 0.03))          # bank into the turn
        pk.append({"time": round(t, 3), "value": r3(p), "interp": "linear"})
        rk.append({"time": round(t, 3), "value": [round(v, 2) for v in _euler_zyx(yaws[i], pitch, roll)], "interp": "linear"})
    rot_a = [0.0, round(yaw_a, 1), 0.0]
    rot_b = [0.0, round(yaws[-1], 1), 0.0]
    t_end = round(min(song, td + 0.5), 3)
    pk = [{"time": 0.0, "value": r3(a["pos"]), "interp": "linear"},
          {"time": round(t_lift - 1e-3, 3), "value": r3(a["pos"]), "interp": "linear"}] + pk[1:] + \
         [{"time": t_end, "value": r3(b["pos"]), "interp": "linear"}]
    rk = [{"time": 0.0, "value": rot_a, "interp": "linear"},
          {"time": round(t_lift - 1e-3, 3), "value": rot_a, "interp": "linear"}] + rk[1:] + \
         [{"time": t_end, "value": rot_b, "interp": "linear"}]
    # the thruster: the flame (scale, and its place and direction on the jet, which move with the clip) and the light
    flame, lamp = name + "_flame", name + "_jet"
    sk = [{"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
          {"time": round(t_lift - 0.25, 3), "value": [0.0, 0.0, 0.0], "interp": "linear"}]
    lk = [{"time": 0.0, "value": 0.0, "interp": "linear"}, {"time": round(t_lift - 0.25, 3), "value": 0.0, "interp": "linear"}]
    fpk, frk, lpk = [], [], []
    kicks = f.get("kicks", [])
    t = t_lift - 0.25
    while t < td + 0.45:
        ramp = max(0.0, min(1.0, (t - (t_lift - 0.25)) / 0.35, (td + 0.05 - t) / 0.3))
        boost = 1.0 + 0.6 * max(0.0, 1.0 - abs(t - t_lift) / 0.8) + 0.4 * max(0.0, 1.0 - abs(t - t_land) / 0.8)
        wts = _clip_weights(cues, t)
        hover = wts.get("Floating", 0.0) / (sum(wts.values()) or 1.0)
        env = 0.0
        for k in kicks:
            if k - PUFF[2] <= t <= k + 5 * PUFF[3]:
                env = max(env, min(1.0, (t - k + PUFF[2]) / PUFF[2]) * math.exp(-max(0.0, t - k) / PUFF[3]))
        gain = 1.0 + PUFF[0] * env * hover
        fl = _flicker(name, t) * ramp * boost
        g = min(fl, 1.2) * ramp
        sk.append({"time": round(t, 3), "value": r3([g, fl * (1.0 + PUFF[1] * env * hover), g]), "interp": "linear"})
        lk.append({"time": round(t, 3), "value": round(f.get("light", 14.0) * fl * gain, 3), "interp": "linear"})
        tot = sum(wts.values()) or 1.0
        nz = [sum(NOZZLE[c][0][i] * wv for c, wv in wts.items()) / tot for i in range(3)]
        dr = [sum(NOZZLE[c][1][i] * wv for c, wv in wts.items()) / tot for i in range(3)]
        fpk.append({"time": round(t, 3), "value": r3(nz), "interp": "linear"})
        frk.append({"time": round(t, 3), "value": [round(v, 2) for v in _down_to(dr)], "interp": "linear"})
        # the light sits half a metre down the thrust (it lights what the jet blows on)
        lpk.append({"time": round(t, 3), "value": r3([nz[i] + 0.5 * dr[i] for i in range(3)]), "interp": "linear"})
        t += 1.0 / 30.0
    sk.append({"time": round(td + 0.5, 3), "value": [0.0, 0.0, 0.0], "interp": "linear"})
    lk.append({"time": round(td + 0.5, 3), "value": 0.0, "interp": "linear"})

    def track(target, keys):
        return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
                "loopLength": 0.0, "keys": keys}
    nodes = [
        {"name": name, "kind": "gltf", "asset": f["asset"], "position": r3(a["pos"]), "rotation": rot_a,
         "scale": [1.0, 1.0, 1.0],
         "animation": {"state": "Idle", "blend": 0.3, "phase": f.get("phase", 0.0), "updateHz": 0,
                       "nearDistance": 40.0, "farHz": 15.0, "cullDistance": 300.0}},
        {"name": flame, "kind": "procedural", "parent": name, "position": NOZZLE["Jumping"][0],
         "scale": [0.0, 0.0, 0.0], "procedural": {
             "source": {"kind": "mesh", "asset": PLUME},
             "distribution": {"kind": "points", "points": [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]]},
             "castsShadow": False,
             "partMaterials": {"jet_flame": {"emissiveColor": flame_color, "emissiveIntensity": 26.0},
                               "jet_collar": {"emissiveColor": flame_color, "emissiveIntensity": 9.0}}}},
    ]
    lights = [{"name": lamp, "type": "point", "node": name, "position": NOZZLE["Jumping"][0],
               "color": light_color, "intensity": 0.0, "range": 9.0, "castsShadow": False, "volumetric": 0.0}]
    tracks = [track("nodes/%s/position" % name, pk), track("nodes/%s/rotation" % name, rk),
              track("nodes/%s/scale" % flame, sk), track("nodes/%s/position" % flame, fpk),
              track("nodes/%s/rotation" % flame, frk), track("lights/%s/intensity" % lamp, lk),
              track("lights/%s/position" % lamp, lpk)]
    clips = []
    for time, clip, blend in cues:
        c = {"time": round(time, 3), "clip": clip, "speed": 1.0, "blend": blend,
             "playback": "once" if clip in ("Jumping", "Landing") else "loop"}
        if clip == "Landing":
            c["then"] = "Idle"
        clips.append(c)
    actor = {"id": name, "node": name, "visible": True, "keys": [], "clips": clips}
    return nodes, lights, tracks, actor
