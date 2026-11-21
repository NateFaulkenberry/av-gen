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
TIER_HZ = {"A": 0, "B": 15.0, "C": 4.0}
RING_M = 60.0          # a crowd ring's width (m) and the head wave's speed (m/s): one ring a second
RINGS = 6
BEAT2 = 1.32           # two beats (s): the sway's period
LOOK_LAYER = {"name": "crowd-look", "kind": "aim", "drive": "manual",
              "joints": ["head.x", "Eye_L", "Eye_R", "Mouth", "Antenna"],
              "pivot": "head.x", "maxYaw": 35.0, "maxPitch": 20.0}


def sway_layer(phase):
    return {"name": "crowd-sway", "kind": "secondary", "drive": "manual",
            "joints": ["spine_01.x", "spine_02.x", "spine_03.x", "spine_04.x", "spine_05.x", "neck.x"],
            "degrees": 3.0, "period": BEAT2, "phase": round(phase % 1.0, 4), "spread": 0.04, "stillness": 0.0,
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

    def phase_for(clip):
        return round(clip_len.get(clip, 2.0) * r.randrange(PHASES) / PHASES, 4)

    def ring_of(pos):
        d = math.dist([pos[0], pos[2]], [bell[0], bell[2]])
        return "ring%d" % min(RINGS - 1, int(d // RING_M))

    def anim(clip, tier, listen=None, speed=None, phase=None):
        a = {"state": clip, "blend": 0.3, "speed": speed if speed is not None else r.choice(SPEEDS),
             "phase": phase if phase is not None else phase_for(clip), "updateHz": TIER_HZ.get(tier, 0),
             "nearDistance": 40.0, "farHz": 15.0, "cullDistance": 600.0}
        if listen:
            ring, layers = listen
            a["crowd"] = ring
            a["layers"] = layers
            crowds.add(ring)
        return a

    def listening(sp, pos):
        react = sp.get("react", [])
        layers = []
        if "turn_to_lantern" in react:
            layers.append(dict(LOOK_LAYER))
        if "sway" in react:
            g = group_of.get(sp.get("id"))
            half = (g % 2) * 0.5 if g is not None else 0.5 * r.randrange(2)
            layers.append(sway_layer(beat_phase + half))
        return (ring_of(pos), layers) if layers else (ring_of(pos), [])

    # msg 130: the ground check's fixes (art/ground_fixes.json): spots moved or dropped, paths re-laid, riders on the floor
    gfix = gfix or {}
    gdrop = set(gfix.get("drop", []))

    def moved(sp, table):
        m = gfix.get(table, {}).get(sp["id"])
        if not m:
            return sp
        sp = dict(sp, pos=m["pos"], local_pos=m["pos"])
        if table == "spots":
            sp["bob"] = m.get("bob")
        return sp

    # ---- spots
    for sp in pop["spots"]:
        if sp["id"] in gdrop:
            continue
        sp = moved(sp, "spots")
        clips = sp.get("clips") or ["Idle"]
        clip = clips[0] if r.random() < 0.6 else r.choice(clips)
        hold = sp.get("pose_hold")
        speed = phase = None
        if hold:
            clip, speed = hold["clip"], 0.001
            phase = round(clip_len.get(clip, 1.0) * hold.get("phase", 0.3), 4)
        n = {"name": "alien_" + sp["id"], "kind": "gltf", "asset": aliens + sp["cast"] + ".glb", "position": r3(sp["pos"]),
             "rotation": [0.0, round(sp["facing_deg"], 2), 0.0], "scale": [pop.get("scale", 1.0)] * 3,
             "animation": anim(clip, sp.get("tier", "B"), listening(sp, sp["pos"]), speed, phase)}
        if sp.get("bob"):
            n["bob"] = sp["bob"]
        nodes.append(n)

    # ---- walkers: pingpong, pause (Idle) and turn at each end
    wakes = []
    for p in pop["paths"]:
        if p["id"] in gfix.get("drop_paths", []):
            continue
        if p["id"] in gfix.get("paths", {}):
            p = dict(p, points=gfix["paths"][p["id"]]["points"])
        pts = p["points"]
        clip = p.get("clip", "Walking")
        speed = p.get("speed_mps", 1.2)
        rate = round(speed / WALK_STRIDE.get(clip, 1.34), 3)
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
                    stop_at[i + 1] = (dur, syaw)
                    break
                acc += L
        out_t += sum(d for d, _ in stop_at.values())
        lap = 2 * (out_t + pause)
        for w in range(max(1, int(p.get("walkers", 1)))):
            cast = (p.get("cast") or ["alien-scout"])[w % len(p.get("cast") or ["x"])]
            offset = (p.get("phases_s") or [0.0])[w % len(p.get("phases_s") or [0.0])] % lap
            tier = p.get("tier", "B")
            name = ("walkA_" if tier == "A" else "walk_") + "%s_%d" % (p["id"], w)
            if tier == "A":
                wakes.append(name)
            pk, rk, cues = [], [], []
            t0 = -offset
            while t0 < song + 1:
                for leg, (a_pts, start) in enumerate(((out_pts, t0), (pts[::-1], t0 + out_t + pause))):
                    tt = start
                    for i in range(len(a_pts) - 1):
                        d = [a_pts[i + 1][j] - a_pts[i][j] for j in range(3)]
                        yaw = math.degrees(math.atan2(d[0], d[2]))
                        if i == 0:
                            pk.append({"time": round(tt, 3), "value": r3(a_pts[0]), "interp": "linear"})
                        rk.append({"time": round(tt, 3), "value": yaw, "interp": "linear"})
                        tt += math.dist(a_pts[i], a_pts[i + 1]) / speed
                        pk.append({"time": round(tt, 3), "value": r3(a_pts[i + 1]), "interp": "linear"})
                        rk.append({"time": round(tt - 0.15, 3), "value": yaw, "interp": "linear"})
                        if leg == 0 and (i + 1) in stop_at:
                            dur, syaw = stop_at[i + 1]
                            cues.append({"time": round(tt, 3), "clip": "Idle", "speed": 1.0, "blend": 0.4})
                            rk.append({"time": round(tt + 0.6, 3), "value": syaw, "interp": "linear"})
                            rk.append({"time": round(tt + dur - 0.5, 3), "value": syaw, "interp": "linear"})
                            tt += dur
                            pk.append({"time": round(tt, 3), "value": r3(a_pts[i + 1]), "interp": "linear"})
                            cues.append({"time": round(tt - 0.3, 3), "clip": clip, "speed": rate, "blend": 0.35})
                    # the end: Idle, and a turn on the spot in its last 0.8 s
                    cues.append({"time": round(start, 3), "clip": clip, "speed": rate, "blend": 0.35})
                    cues.append({"time": round(tt, 3), "clip": "Idle", "speed": 1.0, "blend": 0.4})
                    back = math.degrees(math.atan2(a_pts[-2][0] - a_pts[-1][0], a_pts[-2][2] - a_pts[-1][2]))
                    rk.append({"time": round(tt + pause - 0.8, 3), "value": rk[-1]["value"], "interp": "linear"})
                    rk.append({"time": round(tt + pause, 3), "value": back, "interp": "linear"})
                t0 += lap
            pk.sort(key=lambda k: k["time"])
            rk.sort(key=lambda k: k["time"])
            for q in range(1, len(rk)):   # one continuous yaw
                y, py = rk[q]["value"], rk[q - 1]["value"]
                rk[q]["value"] = py + ((y - py + 180) % 360 - 180)
            # keep yaw inside the parameter's +-360 range: shift the whole track by whole turns where it drifts
            shift = 0.0
            for k in rk:
                v = k["value"] + shift
                if v > 300:
                    shift -= 360
                elif v < -300:
                    shift += 360
                k["value"] = [0.0, round(k["value"] + shift, 1), 0.0]
            pk = _clip_start(pk)
            rk = _clip_start(rk)
            cues = sorted([c for c in cues if c["time"] < song], key=lambda c: c["time"])
            before = [c for c in cues if c["time"] <= 0]
            cues = ([dict(before[-1], time=0.0)] if before else []) + [c for c in cues if c["time"] > 0]
            n = {"name": name, "kind": "gltf", "asset": aliens + cast + ".glb", "position": pk[0]["value"],
                 "rotation": rk[0]["value"], "scale": [pop.get("scale", 1.0)] * 3,
                 "animation": anim(cues[0]["clip"] if cues else clip, tier, speed=rate, phase=phase_for(clip))}
            if p.get("bob"):
                n["bob"] = p["bob"]
            nodes.append(n)
            tracks += [track("nodes/%s/position" % name, pk), track("nodes/%s/rotation" % name, rk)]
            actors.append({"id": name, "node": name, "visible": True, "keys": [], "clips": [
                dict(c, playback="loop") for c in cues]})

    # ---- riders, in their cabins
    vehicle_of = {v["spline"]: i for i, v in enumerate(layout["vehicles"])}
    for rd in pop.get("riders", []):
        if rd["id"] in gdrop:
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
             "via": via, "kicks": kicks}
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
                s += 1.0 / 15.0
            ptrack["keys"] = out
        nodes += n
        lights += l
        tracks += t
        actors.append(actor)

    for ring in sorted(crowds):
        tracks.append(track("crowds/%s/lag" % ring, lag_keys()))
        tracks.append(track("crowds/%s/lookTarget" % ring, [{"time": 0.0, "value": r3(bell), "interp": "linear"}]))
    return nodes, tracks, lights, actors, sorted(crowds), wakes


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
