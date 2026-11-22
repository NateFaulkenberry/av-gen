"""Neon Dreams iteration 10 (owner msgs 112, 117): the populated Moorings, from the art's art/population.json.

- Spots (480): a node each, its clip (the first suggested is the default), an opening phase and a rate from a few
  buckets (ADR-2116: rigs in step share a pose), its pose rate by LOD tier (A every frame, B 15 Hz, C 4 Hz), the
  formation it stands on (`bob`), a held crouch for `sit_edge` (`pose_hold`).
- Walkers (95 on 62 paths): pingpong with an Idle pause and a turn on the spot at each end (clip cues in the sequence),
  the walk rate matched to the stride (1.34 m/s at rate 1), each at its own `phases_s`.
- Riders (10): parented to their gondola cabins (vehicle<i>, layout.vehicles order).
- Flyers (6): nd_flight.py on the art's take-off, landing, times and cruise route; a formation's bob is added to the
  flight keys where an end stands on one (the flyer leaves the pier still and lands on a bobbing isle).
- Listening (ADR-2117): standing figures with `turn_to_lantern` carry a `crowd-look` Aim layer (head, 35 deg), those
  with `sway` a `crowd-sway` Secondary layer (spine, 1.32 s = two beats, on the beat); they are in crowds `ring0..5`
  by distance from the Lantern, so one bass route delayed per ring turns heads outward as a wave. The held breath
  (tone-block rest 45.5 s, released on the 47.15 s crash) is the crowds' `lag` track.
"""
from __future__ import annotations

import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
WALK_STRIDE = {"Walking": 1.34, "Walking_low_grav": 1.0}   # m/s a stance foot sweeps back at clip rate 1
TURN_RATE = 225.0
SPEEDS = (0.92, 1.0, 1.08)
PHASES = 8
TIER_HZ = {"A": 0, "B": 0, "C": 0}   # it12 (msg 149): every figure posed every frame (it10: B 15 Hz, C 4 Hz stepped)
FAR_HZ = 0.0           # it12 (msg 149, the 4.09 s flyer "glitching/skipping"): no distance rate either (it10: 15 Hz past 40 m)
# it12 (owner msg 149: "time animations to quarter notes, this looks too slow motion"): the clips run on the song's
# grid. Sabertooth is 171.4 BPM (the kick every 0.70 s from 0.05 s): a looping clip plays a whole number of beats a
# cycle at a rate of at least 1 (never slower than authored); its opening phase puts a cycle's start on a beat; a walk
# steps on every quarter note (Walking: 2 steps a cycle = 2 beats, 2.04 m/s) and a low-gravity bound on every bar.
BEAT = 60.0 / 171.4
WALK_BEATS = {"Walking": 2, "Walking_low_grav": 8}
RING_M = 60.0          # a crowd ring's width (m) and the head wave's speed (m/s): one ring a second
RINGS = 6
BEAT2 = 4 * 60.0 / 171.4   # it12: four beats (s), a lean each way on alternate kicks (it10: 1.32 s, off the tempo)
LOOK_LAYER = {"name": "crowd-look", "kind": "aim", "drive": "manual",
              "joints": ["head.x", "Eye_L", "Eye_R", "Mouth", "Antenna"],
              "pivot": "head.x", "maxYaw": 35.0, "maxPitch": 20.0}


def sway_layer(phase, degrees=3.0):
    return {"name": "crowd-sway", "kind": "secondary", "drive": "manual",
            "joints": ["spine_01.x", "spine_02.x", "spine_03.x", "spine_04.x", "spine_05.x", "neck.x"],
            "degrees": round(degrees, 2), "period": BEAT2, "phase": round(phase % 1.0, 4), "spread": 0.04, "stillness": 0.0,
            "weight": 0.0, "axis": [0.0, 0.0, 1.0]}


# the held breath: clip speed 1 -> 0.15 over 0.6 s from the tone block's rest, back over 0.4 s on the crash
BREATH = (45.5, 46.1, 47.15, 47.55, 0.15)


def breath_speed(t):
    a, b, c, d, lo = BREATH
    if t <= a or t >= d:
        return 1.0
    if t < b:
        u = (t - a) / (b - a)
        return 1.0 - (1.0 - lo) * u * u * (3 - 2 * u)
    if t <= c:
        return lo
    u = (t - c) / (d - c)
    return lo + (1.0 - lo) * u * u * (3 - 2 * u)


def lag_keys():
    """crowds/<ring>/lag: the integral of (1 - speed); a seek-safe slow-down (ADR-2117)."""
    a, _, _, d, _ = BREATH
    keys = [{"time": 0.0, "value": 0.0, "interp": "linear"}]
    lag, t, dt = 0.0, a, 1.0 / 60.0
    keys.append({"time": round(a, 4), "value": 0.0, "interp": "linear"})
    while t < d:
        lag += (1.0 - 0.5 * (breath_speed(t) + breath_speed(t + dt))) * dt
        t += dt
        if abs((t - a) * 15 - round((t - a) * 15)) < 1e-6 or t >= d:
            keys.append({"time": round(t, 4), "value": round(lag, 5), "interp": "linear"})
    return keys


def bob_offset(bob, t):
    """ADR-2112's cloudBobOffset (liftMax 0 here, so no lift), for keys that must ride a formation part of the time."""
    if bob is None:
        return [0.0, 0.0, 0.0]
    tau = 2 * math.pi
    p = max(bob["period"], 0.5)
    ph = bob["phase"]
    amp = bob["amplitude"]
    return [math.sin(tau * (t / (p * 1.618) + ph * 0.7 + 0.31)) * amp[0],
            math.sin(tau * (t / p + ph)) * amp[1],
            math.sin(tau * (t / (p * 1.83) + ph * 1.3 + 0.67)) * amp[2]]


def _interp(keys, t):
    if t <= keys[0]["time"]:
        return list(keys[0]["value"])
    for k in range(1, len(keys)):
        if keys[k]["time"] >= t:
            a, b = keys[k - 1], keys[k]
            u = (t - a["time"]) / max(b["time"] - a["time"], 1e-9)
            return [a["value"][i] + (b["value"][i] - a["value"][i]) * u for i in range(3)]
    return list(keys[-1]["value"])


def track(target, keys):
    return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "keys": keys}


def apply_fixes(via, fixes):
    """msg 123: move each via point by the pushes found near it (within 4 m, fading out), from the clearance check."""
    out = []
    for v in via:
        d = [0.0, 0.0, 0.0]
        for q, push in fixes:
            w = max(0.0, 1.0 - math.dist(v, q) / 4.0)
            for i in range(3):
                d[i] += push[i] * w
        out.append([v[i] + d[i] for i in range(3)])
    return out


def build(pop, layout, design, aliens, clip_len, bell, song, r3, flight_fn, kicks, fixes=None, gfix=None):
    """Returns (nodes, tracks, lights, sequence actors, crowd names, wake names)."""
    r = random.Random(1012)
    bobs = {b["name"]: b for b in design.get("bobs", [])}
    nodes, tracks, lights, actors = [], [], [], []
    crowds = set()
    kick0 = kicks[0] if kicks else 0.0
    beat_phase = (-kick0 / BEAT2) % 1.0
    group_of = {}
    for gi, g in enumerate(pop.get("groups", [])):
        for m in g["members"]:
            group_of[m] = gi

    def beat_rate(clip):
        """(rate, beats a cycle): a whole number of beats, never slower than authored."""
        L = clip_len.get(clip, 2.0)
        n = max(1, int(L / BEAT + 1e-6))
        return round(L / (n * BEAT), 4), n

    rv = random.Random(170)   # it12 (owner msg 170): pose variety, on its own stream

    def phase_for(clip, rate=None):
        """Clip seconds at timeline 0. msg 170: anywhere in the cycle (it12 first put every cycle's start on one of the
        clip's n beats, so a crowd on one clip held at most n poses -- the band's audience moved as one). The rate
        still makes a cycle a whole number of beats."""
        L = clip_len.get(clip, 2.0)
        r.randrange(PHASES)   # (the same draws as it11's buckets: the spots keep their clips)
        return round(rv.random() * L, 4)

    def on_grid(t):
        return kick0 + round((t - kick0) / BEAT) * BEAT

    def ring_of(pos):
        d = math.dist([pos[0], pos[2]], [bell[0], bell[2]])
        return "ring%d" % min(RINGS - 1, int(d // RING_M))

    def anim(clip, tier, listen=None, speed=None, phase=None):
        if speed is None:
            r.choice(SPEEDS)   # it11's rate draw, kept so the random stream (and every spot's clip) is unchanged
        a = {"state": clip, "blend": 0.3, "speed": speed if speed is not None else beat_rate(clip)[0],
             "phase": phase if phase is not None else phase_for(clip, speed), "updateHz": TIER_HZ.get(tier, 0),
             "nearDistance": 40.0, "farHz": FAR_HZ, "cullDistance": 600.0}
        if listen:
            ring, layers = listen
            a["crowd"] = ring
            a["layers"] = layers
            crowds.add(ring)
        return a

    def listening(sp, pos):
        react = sp.get("react", [])
        layers = []
        if sp.get("look_at"):   # it12 (msg 149): the band's audience -- heads to the stage, its own crowd
            band_look[0] = sp["look_at"]
            layers.append(dict(LOOK_LAYER))
            if "sway" in react:
                g = group_of.get(sp.get("id"))
                half = (g % 2) * 0.5 if g is not None else 0.5 * r.randrange(2)
                layers.append(sway_layer(beat_phase + half + rv.uniform(-0.08, 0.08), rv.uniform(2.2, 4.0)))
            return ("band", layers)
        if "turn_to_lantern" in react:
            layers.append(dict(LOOK_LAYER))
        if "sway" in react:
            g = group_of.get(sp.get("id"))
            half = (g % 2) * 0.5 if g is not None else 0.5 * r.randrange(2)
            layers.append(sway_layer(beat_phase + half + rv.uniform(-0.08, 0.08), rv.uniform(2.2, 4.0)))
        return (ring_of(pos), layers) if layers else (ring_of(pos), [])

    # msg 130: the ground check's fixes (art/ground_fixes.json): spots moved or dropped, paths re-laid, riders on the floor
    gfix = gfix or {}

    def same(a, b):   # a fix applies only to the figure it was made for (an art edit since makes it stale)
        if a is None or b is None:
            return False
        if isinstance(a[0], list):
            return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
        return math.dist(a, b) < 0.01

    def dropped(x, key="pos"):
        d = gfix.get("drop", {}).get(x["id"])
        return d is not None and same(d.get("from"), x.get(key))

    def moved(sp, table):
        m = gfix.get(table, {}).get(sp["id"])
        if not m or not same(m.get("from"), sp.get("pos" if table == "spots" else "local_pos")):
            return sp
        sp = dict(sp, pos=m["pos"], local_pos=m["pos"])
        if table == "spots":
            sp["bob"] = m.get("bob")
        return sp

    band_look = [None]
    band_ids = []
    # ---- spots
    for sp in pop["spots"]:
        if dropped(sp):
            continue
        sp = moved(sp, "spots")
        clips = sp.get("clips") or ["Idle"]
        clip = clips[0] if r.random() < 0.6 else r.choice(clips)
        hold = sp.get("pose_hold")
        speed = phase = None
        if hold:
            clip, speed = hold["clip"], 0.001
            phase = round(clip_len.get(clip, 1.0) * hold.get("phase", 0.3), 4)
        # msg 170: the art's per-figure overrides (clip seconds at 0, clip rate), when it sets them
        if sp.get("anim_clip") and not hold:
            clip = sp["anim_clip"]
        if sp.get("anim_rate") is not None and not hold:
            speed = sp["anim_rate"]
        if sp.get("anim_phase") is not None and not hold:
            phase = round(sp["anim_phase"], 4)
        n = {"name": "alien_" + sp["id"], "kind": "gltf", "asset": aliens + sp["cast"] + ".glb", "position": r3(sp["pos"]),
             "rotation": [0.0, round(sp["facing_deg"], 2), 0.0], "scale": [pop.get("scale", 1.0)] * 3,
             "animation": anim(clip, sp.get("tier", "B"), listening(sp, sp["pos"]), speed, phase)}
        if sp.get("bob"):
            n["bob"] = sp["bob"]
        nodes.append(n)
        if sp.get("look_at") and not hold:
            band_ids.append((n["name"], clip, n["animation"]["speed"]))

    # it12 (msg 149): the audience erupts on the crash that ends the held breath (47.15 s): Crazy once, a little
    # staggered, then back to its own clip
    for name, clip, speed in band_ids:
        d = round(r.uniform(0.0, 0.18), 3) + round(rv.uniform(0.0, 0.3), 3)   # msg 170: a looser eruption
        actors.append({"id": name, "node": name, "visible": True, "keys": [], "clips": [
            {"time": round(BREATH[2] + d, 3), "clip": "Crazy", "speed": 1.0, "blend": 0.15, "playback": "once",
             "then": clip}]})

    # ---- walkers: pingpong, pause (Idle) and turn at each end
    wakes = []
    for p in pop["paths"]:
        dp = gfix.get("drop_paths", {}).get(p["id"])
        if dp is not None and same(dp.get("from"), p["points"]):
            continue
        fp = gfix.get("paths", {}).get(p["id"])
        if fp is not None and same(fp.get("from"), p["points"]):
            p = dict(p, points=fp["points"])
        pts = p["points"]
        clip = p.get("clip", "Walking")
        # it12 (msg 149): the walk on the grid -- its rate from the beats a cycle, the speed from its stride
        rate = round(clip_len.get(clip, 1.0667) / (WALK_BEATS.get(clip, 2) * BEAT), 4)
        speed = WALK_STRIDE.get(clip, 1.34) * rate
        idle_rate = beat_rate("Idle")[0]
        pause = p.get("pause_s", 2.5)
        seg = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
        out_t = sum(seg) / speed
        # msg 125: a stop on the way out (a shopfront): the out leg is split there, Idle for its seconds, facing it
        stops = p.get("stops", [])
        out_pts, stop_at = list(pts), {}
        for frac, dur, syaw in stops:
            target, acc = frac * sum(seg), 0.0
            for i in range(len(out_pts) - 1):
                L = math.dist(out_pts[i], out_pts[i + 1])
                if acc + L >= target and L > 0:
                    u = (target - acc) / L
                    q = [out_pts[i][j] + (out_pts[i + 1][j] - out_pts[i][j]) * u for j in range(3)]
                    out_pts.insert(i + 1, q)
                    # it12: the walk resumes on a beat (the stop lengthened to the next one)
                    walked = (acc + (target - acc)) / speed
                    dur = math.ceil((walked + dur) / BEAT - 1e-6) * BEAT - walked
                    stop_at[i + 1] = (dur, syaw)
                    break
                acc += L
        out_t += sum(d for d, _ in stop_at.values())
        # it12: each leg starts on a beat (the pause at the end lengthened to the next one)
        pause = math.ceil((out_t + pause) / BEAT - 1e-6) * BEAT - out_t
        lap = 2 * (out_t + pause)
        for w in range(max(1, int(p.get("walkers", 1)))):
            cast = (p.get("cast") or ["alien-scout"])[w % len(p.get("cast") or ["x"])]
            offset = (p.get("phases_s") or [0.0])[w % len(p.get("phases_s") or [0.0])] % lap
            offset = -on_grid(-offset)   # it12: t0 = -offset on the grid
            tier = p.get("tier", "B")
            name = ("walkA_" if tier == "A" else "walk_") + "%s_%d" % (p["id"], w)
            if tier == "A":
                wakes.append(name)
            pk, rk, cues = [], [], []
            t0 = -offset
            while t0 < song + 1:
                for leg, (a_pts, start) in enumerate(((out_pts, t0), (pts[::-1], t0 + out_t + pause))):
                    tt = start
                    yaw = None
                    cum = [0.0]
                    for i in range(len(a_pts) - 1):
                        cum.append(cum[-1] + math.dist(a_pts[i], a_pts[i + 1]))

                    def at_s(sv, a_pts=a_pts, cum=cum):
                        sv = max(0.0, min(cum[-1], sv))
                        for k in range(len(cum) - 1):
                            if cum[k + 1] >= sv:
                                u = (sv - cum[k]) / max(cum[k + 1] - cum[k], 1e-9)
                                return [a_pts[k][j] + (a_pts[k + 1][j] - a_pts[k][j]) * u for j in range(3)]
                        return a_pts[-1]
                    for i in range(len(a_pts) - 1):
                        # it12: the heading over 1.6 m of the path round the segment's middle, so the ground check's
                        # small detours (walk_007: 0.2 m zigzags) do not spin the walker
                        sm = 0.5 * (cum[i] + cum[i + 1])
                        qa, qb = at_s(sm - 0.8), at_s(sm + 0.8)
                        d = [qb[j] - qa[j] for j in range(3)]
                        # it12: a zero-length segment (a stop's split point) keeps the heading (it read as yaw 0: a
                        # snap); each corner turns inside both segments' own time, so the keys stay in order at the
                        # quicker quarter-note walk (it11's fixed 0.15 s ran keys backwards on short segments)
                        if math.hypot(d[0], d[2]) > 1e-3 or yaw is None:
                            yaw = math.degrees(math.atan2(d[0], d[2]))
                        if i == 0:
                            pk.append({"time": round(tt, 3), "value": r3(a_pts[0]), "interp": "linear"})
                        dur_seg = math.dist(a_pts[i], a_pts[i + 1]) / speed
                        win = min(0.15, 0.45 * dur_seg)
                        if dur_seg > 1e-3 or i == 0:   # it12: no keys for a zero-length segment (one time, many yaws)
                            rk.append({"time": round(tt + (win if i > 0 else 0.0), 3), "value": yaw, "interp": "linear"})
                        tt += dur_seg
                        if dur_seg > 1e-3 or i == 0:
                            pk.append({"time": round(tt, 3), "value": r3(a_pts[i + 1]), "interp": "linear"})
                            rk.append({"time": round(tt - win, 3), "value": yaw, "interp": "linear"})
                        if leg == 0 and (i + 1) in stop_at:
                            dur, syaw = stop_at[i + 1]
                            cues.append({"time": round(tt, 3), "clip": "Idle", "speed": idle_rate, "blend": 0.4})
                            rk.append({"time": round(tt + 0.6, 3), "value": syaw, "interp": "linear"})
                            rk.append({"time": round(tt + dur - 0.5, 3), "value": syaw, "interp": "linear"})
                            tt += dur
                            pk.append({"time": round(tt, 3), "value": r3(a_pts[i + 1]), "interp": "linear"})
                            cues.append({"time": round(tt - 0.3, 3), "clip": clip, "speed": rate, "blend": 0.35})
                    # the end: Idle, and a turn on the spot in its last 0.8 s
                    cues.append({"time": round(start, 3), "clip": clip, "speed": rate, "blend": 0.35})
                    cues.append({"time": round(tt, 3), "clip": "Idle", "speed": idle_rate, "blend": 0.4})
                    qe, qf = at_s(cum[-1]), at_s(cum[-1] - 1.6)   # it12: back along the smoothed path (no 0,0 heading)
                    back = math.degrees(math.atan2(qf[0] - qe[0], qf[2] - qe[2]))
                    rk.append({"time": round(tt + pause - 0.8, 3), "value": rk[-1]["value"], "interp": "linear"})
                    rk.append({"time": round(tt + pause, 3), "value": back, "interp": "linear"})
                t0 += lap
            pk.sort(key=lambda k: k["time"])
            rk.sort(key=lambda k: k["time"])
            for q in range(1, len(rk)):   # one continuous yaw
                y, py = rk[q]["value"], rk[q - 1]["value"]
                rk[q]["value"] = py + ((y - py + 180) % 360 - 180)
            # keep yaw inside the parameter's +-360 range: shift the whole track by whole turns where it drifts
            rk = _rate_limited(rk, 0.1, 320.0)   # it12: no walker turns faster than 320 deg/s
            # it12: when the shift changes, the key before is repeated (same instant) in the new turn, so the segment
            # between interpolates the short way (it11 jumped 348 deg across one segment: a whole spin in 0.3 s)
            shift = 0.0
            out_rk = []
            for k in rk:
                v = k["value"] + shift
                if v > 300 or v < -300:
                    shift += -360 if v > 300 else 360
                    if out_rk:
                        pv = out_rk[-1]
                        out_rk.append({"time": pv["time"], "value": [0.0, round(pv["value"][1] + (-360 if v > 300 else 360), 1),
                                                                     0.0], "interp": "linear"})
                out_rk.append(dict(k, value=[0.0, round(k["value"] + shift, 1), 0.0]))
            rk = out_rk
            pk = _clip_start(pk)
            rk = _clip_start(rk)
            cues = sorted([c for c in cues if c["time"] < song], key=lambda c: c["time"])
            before = [c for c in cues if c["time"] <= 0]
            cues = ([dict(before[-1], time=0.0)] if before else []) + [c for c in cues if c["time"] > 0]
            n = {"name": name, "kind": "gltf", "asset": aliens + cast + ".glb", "position": pk[0]["value"],
                 "rotation": rk[0]["value"], "scale": [pop.get("scale", 1.0)] * 3,
                 "animation": anim(cues[0]["clip"] if cues else clip, tier, speed=rate, phase=phase_for(clip, rate))}
            if p.get("bob"):
                n["bob"] = p["bob"]
            nodes.append(n)
            tracks += [track("nodes/%s/position" % name, pk), track("nodes/%s/rotation" % name, rk)]
            actors.append({"id": name, "node": name, "visible": True, "keys": [], "clips": [
                dict(c, playback="loop") for c in cues]})

    # ---- riders, in their cabins
    vehicle_of = {v["spline"]: i for i, v in enumerate(layout["vehicles"])}
    for rd in pop.get("riders", []):
        if dropped(rd, "local_pos"):
            continue
        rd = moved(rd, "riders")
        vi = vehicle_of.get(rd["vehicle_spline"])
        if vi is None:
            continue
        clip = (rd.get("clips") or ["Idle"])[0]
        nodes.append({"name": "alien_" + rd["id"], "kind": "gltf", "parent": "vehicle%d" % vi,
                      "asset": aliens + rd["cast"] + ".glb", "position": r3(rd["local_pos"]),
                      "rotation": [0.0, round(rd["local_yaw_deg"], 2), 0.0], "scale": [pop.get("scale", 1.0)] * 3,
                      "animation": anim(clip, rd.get("tier", "B"))})

    # ---- flyers
    for fdat in pop.get("flyers", []):
        if "flyer_" + fdat["id"] in (fixes or {}).get("_drop", []):
            continue   # msg 123: no clear route found (listed in art/flight_fixes.json)
        keys = fdat["keys"]
        cruise = [k for k in keys if k.get("clip") == "Flying_jet"]
        # the art's ray-checked cruise, one via point a second (fewer cut the loops its route makes round the bell)
        step = max(1, int(round(0.4 / max(1e-3, (cruise[1]["t"] - cruise[0]["t"]))))) if len(cruise) > 1 else 1
        via = [k["pos"] for k in cruise[::step]] if cruise else []
        # the art's glide down to the deck becomes my vertical descent: a diagonal glide passed through the Lantern's
        # gallery (msg 123's check); the via ends where the route starts to come down
        if len(via) > 2:
            top = max(v[1] for v in via)
            while len(via) > 2 and via[-1][1] < top - 3.0:
                via.pop()
        via = apply_fixes(via, (fixes or {}).get("flyer_" + fdat["id"], []))
        lift = (fixes or {}).get("_lift", {}).get("flyer_" + fdat["id"], 0.0)   # msg 123: a cruise raised clear
        if lift:
            top = max(v[1] for v in via) + lift
            via = [[v[0], top, v[2]] for v in via]
        ta, tb = fdat["takeoff"], fdat["landing"]
        td = fdat.get("on_beat", {}).get("touchdown_kick_s", fdat["t_land"])
        f = {"name": "flyer_" + fdat["id"], "asset": aliens + fdat["cast"] + ".glb",
             "from": {"pos": ta["pos"], "yaw": keys[0].get("yaw_deg", 0.0)},
             "to": {"pos": tb["pos"], "yaw": keys[-1].get("yaw_deg")},
             "t0": fdat["t_takeoff"], "td": td, "alt": max(4.0, fdat.get("cruise_height_m", 0) - max(ta["pos"][1], tb["pos"][1])),
             "via": via, "kicks": kicks, "bob_from": ta.get("bob"), "bob_to": tb.get("bob")}
        n, l, t, actor = flight_fn(f)
        # ride the formations: still at A's, blending to B's over the flight, B's after it
        ba, bb = bobs.get(ta.get("bob")), bobs.get(tb.get("bob"))
        if ba or bb:
            ptrack = next(x for x in t if x["target"] == "nodes/%s/position" % f["name"])
            src = sorted(ptrack["keys"], key=lambda k: k["time"])
            t_lift, t_land = f["t0"] + 0.567, td
            out, s = [], 0.0
            while s <= song + 1e-6:
                v = _interp(src, s)
                u = min(1.0, max(0.0, (s - t_lift) / max(t_land - t_lift, 1e-3)))
                w = u * u * (3 - 2 * u)
                oa, ob = bob_offset(ba, s), bob_offset(bb, s)
                out.append({"time": round(s, 3), "value": r3([v[i] + oa[i] * (1 - w) + ob[i] * w for i in range(3)]),
                            "interp": "linear"})
                s += 1.0 / 30.0   # it12: 30 Hz (it10: 15)
            ptrack["keys"] = out
        nodes += n
        lights += l
        tracks += t
        actors.append(actor)

    for ring in sorted(crowds):
        tracks.append(track("crowds/%s/lag" % ring, lag_keys()))
        target = band_look[0] if ring == "band" and band_look[0] else bell
        tracks.append(track("crowds/%s/lookTarget" % ring, [{"time": 0.0, "value": r3(target), "interp": "linear"}]))
        if ring == "band":   # the audience watches the stage all song
            tracks.append(track("crowds/band/look", [{"time": 0.0, "value": 1.0, "interp": "linear"}]))
    return nodes, tracks, lights, actors, sorted(crowds), wakes


def _rate_limited(keys, step, rate):
    """it12 (msg 149): a scalar yaw track resampled every `step` s and limited to `rate` deg/s (a forward and a
    backward pass, averaged, so a turn is neither late nor early): a path's tight wiggles no longer spin a walker."""
    if len(keys) < 2:
        return keys
    t0, t1 = keys[0]["time"], keys[-1]["time"]
    ts, t = [], t0
    while t < t1:
        ts.append(t)
        t += step
    ts.append(t1)
    vals, j = [], 0
    for t in ts:
        while j + 1 < len(keys) - 1 and keys[j + 1]["time"] <= t:
            j += 1
        a, b = keys[j], keys[min(j + 1, len(keys) - 1)]
        u = 0.0 if b["time"] <= a["time"] else min(1.0, max(0.0, (t - a["time"]) / (b["time"] - a["time"])))
        vals.append(a["value"] + (b["value"] - a["value"]) * u)
    f, bk = list(vals), list(vals)
    for i in range(1, len(f)):
        m = rate * (ts[i] - ts[i - 1])
        f[i] = min(max(f[i], f[i - 1] - m), f[i - 1] + m)
    for i in range(len(bk) - 2, -1, -1):
        m = rate * (ts[i + 1] - ts[i])
        bk[i] = min(max(bk[i], bk[i + 1] - m), bk[i + 1] + m)
    return [{"time": round(t, 3), "value": round(0.5 * (x + y), 2), "interp": "linear"} for t, x, y in zip(ts, f, bk)]


def _clip_start(keys):
    """Keys from t = 0 (the first key interpolated there)."""
    out = [k for k in keys if k["time"] >= 0]
    before = [k for k in keys if k["time"] < 0]
    if before and out:
        a, b = before[-1], out[0]
        u = -a["time"] / max(b["time"] - a["time"], 1e-9)
        va = a["value"] if isinstance(a["value"], list) else [a["value"]]
        vb = b["value"] if isinstance(b["value"], list) else [b["value"]]
        v = [round(x + (y - x) * u, 3) for x, y in zip(va, vb)]
        out.insert(0, {"time": 0.0, "value": v if isinstance(a["value"], list) else v[0], "interp": "linear"})
    return out or keys[-1:]
