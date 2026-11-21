"""Neon Dreams iteration 12 (owner msg 149): the band in the garden plaza, from the art's art/layout.json `band` block.

- The astronaut musicians (assets/musicians: gitignored, the owner's licences forbid redistribution; never commit),
  each performer and its props on one transform. Their clips run on the song's grid (171.4 BPM: Drums 13 beats a
  cycle, Piano 47) and play only while their part does: a crowd `lag` track slows the drummer's clip to 0.15 when the
  drums rest and the pianist's between the Moog's phrases (ADR-2117: a seek-safe change of speed). A one-beat
  `crowd-sway` Secondary layer on the spine, neck and head is pumped by the drum hits (the drummer) and the Moog notes
  (the pianist), so the bodies hit with the music. The drummer flails on each crash.
- The stage spots: real spot lights on the band (the art's 8) and, for the beams in the air, ADR-2102 cloud beams --
  the 8 floor spots (short, wide) and the 4 halo searchlights (`sky_beams`) -- which scatter in the air and the wisps.
  Floor spots +60 % on the kick, sky beams +40 % on the snare and swaying +-12 deg over two bars, whipping a quarter
  turn on the crashes; the held breath (45.5-47.15 s) dims the floor spots to 30 % and holds the sky beams still.
- The audience: the art's `band_*` spots (nd_cast.py: crowd `band`, heads to the stage) erupt (Crazy) on the 47.15 s
  crash after the held breath.
Every envelope is a timeline track from the measured onsets or a route on a stem: seek-safe.
"""
from __future__ import annotations

import math
import os

import nd_audio as AU

BEAT = 60.0 / 171.4
E = AU.EVENTS["stems"]
ONSETS = {k: [t for t, _ in v["onsets"]] for k, v in E.items()}
BREATH = (45.5, 46.1, 47.15, 47.55)
PERFORMER_JOINTS = ["spine_01", "spine_02", "spine_03", "neck_01", "head"]


def track(target, keys):
    return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "keys": keys}


def active_speed(onsets, before, after, rest=0.15, ramp=0.25, song=70.18, step=0.05):
    """A clip speed (rest .. 1) that is 1 from `before` s ahead of an onset to `after` s past it, ramping over `ramp`."""
    def s(t):
        best = 0.0
        for o in onsets:
            if o - before - ramp <= t <= o + after + ramp:
                if o - before <= t <= o + after:
                    return 1.0
                d = (o - before - t) if t < o - before else (t - o - after)
                best = max(best, 1.0 - d / ramp)
        return rest + (1.0 - rest) * best
    return s


def lag_keys(speed, song=70.18, step=0.05):
    """crowds/<c>/lag = the integral of (1 - speed) (ADR-2117): keys where it changes."""
    keys = [{"time": 0.0, "value": 0.0, "interp": "linear"}]
    lag, t, last = 0.0, 0.0, 0.0
    while t < song:
        lag += (1.0 - 0.5 * (speed(t) + speed(t + step))) * step
        t += step
        if abs(lag - last) > 1e-4 or t >= song:
            keys.append({"time": round(t, 3), "value": round(lag, 4), "interp": "linear"})
            last = lag
    # drop interior keys on straight runs (the file stays small)
    out = [keys[0]]
    for i in range(1, len(keys) - 1):
        a, b, c = out[-1], keys[i], keys[i + 1]
        u = (b["time"] - a["time"]) / max(c["time"] - a["time"], 1e-9)
        if abs(a["value"] + (c["value"] - a["value"]) * u - b["value"]) > 2e-4:
            out.append(b)
    out.append(keys[-1])
    return out


def performer_layer(degrees, kick0):
    return {"name": "crowd-sway", "kind": "secondary", "drive": "manual", "joints": PERFORMER_JOINTS,
            "degrees": degrees, "period": BEAT, "phase": round((-kick0 / BEAT) % 1.0, 4), "spread": 0.02,
            "stillness": 0.0, "weight": 0.0, "axis": [1.0, 0.0, 0.0]}


def build(layout, scene_dir, root, pal, r3, clip_len_of, song=70.18):
    """Returns (nodes, lights, beams, tracks, routes, actors, beam_index_base callback)."""
    band = layout.get("band")
    if not band:
        return [], [], [], [], [], [], {}
    kick0 = ONSETS["kick"][0]
    nodes, lights, beams, tracks, routes, actors = [], [], [], [], [], []
    info = {"crowds": []}
    for m in band["musicians"]:
        asset = os.path.relpath(os.path.join(root, m["asset"]), scene_dir)
        props = os.path.relpath(os.path.join(root, m["props"]), scene_dir)
        L = clip_len_of(os.path.join(root, m["asset"]), m["clip"])
        n = max(1, int(L / BEAT + 1e-6))
        rate = round(L / (n * BEAT), 4)
        crowd = m["name"]
        drums = "drum" in m["asset"]
        nodes.append({"name": m["name"], "kind": "gltf", "asset": asset, "position": r3(m["pos"]),
                      "rotation": [0.0, m["yaw_deg"], 0.0], "scale": [1.0, 1.0, 1.0],
                      "animation": {"state": m["clip"], "blend": 0.3, "speed": rate,
                                    "phase": round((rate * (-kick0)) % L, 4), "updateHz": 0, "farHz": 0.0,
                                    "cullDistance": 0.0, "crowd": crowd,
                                    "layers": [performer_layer(7.0 if drums else 4.0, kick0)]}})
        nodes.append({"name": m["name"] + "_props", "kind": "gltf", "asset": props, "position": r3(m["pos"]),
                      "rotation": [0.0, m["yaw_deg"], 0.0], "scale": [1.0, 1.0, 1.0]})
        info["crowds"].append(crowd)
        if drums:
            hits = sorted(ONSETS["kick"] + ONSETS["snare"] + ONSETS["toms"] + ONSETS["hh"])
            sp = active_speed(hits, 0.2, 0.9)
            for stem, g in (("kick", 1.0), ("snare", 0.9), ("toms", 0.8)):
                routes.append(AU.route("stem.%s.onset" % stem, "crowds/%s/sway" % crowd, g, attackMs=6.0,
                                       decayMs=220.0))
            # a flail on each crash, back to the kit a second later
            clips = []
            for c in ONSETS["crash"]:
                clips += [{"time": round(c - 0.04, 3), "clip": "Flail", "speed": 1.0, "blend": 0.12, "playback": "loop"},
                          {"time": round(c + 1.0, 3), "clip": m["clip"], "speed": rate, "blend": 0.35, "playback": "loop"}]
            actors.append({"id": m["name"], "node": m["name"], "visible": True, "keys": [], "clips": clips})
        else:
            sp = active_speed(ONSETS["moog"], 0.35, 1.4, rest=0.2)
            routes.append(AU.route("stem.moog.onset", "crowds/%s/sway" % crowd, 1.0, attackMs=10.0, decayMs=420.0))
        tracks.append(track("crowds/%s/lag" % crowd, lag_keys(sp, song)))

    # the spot lights on the band, and their beams in the air
    a, b, c, d = BREATH
    def breath_keys(base, floor):
        return [{"time": 0.0, "value": base, "interp": "linear"}, {"time": a, "value": base, "interp": "smooth"},
                {"time": b, "value": round(base * floor, 3), "interp": "smooth"},
                {"time": c, "value": round(base * floor, 3), "interp": "smooth"},
                {"time": d, "value": base, "interp": "smooth"}]
    for s in band["spots"]:
        col = pal(s["key"])
        lights.append({"name": s["name"], "type": "spot", "position": r3(s["pos"]), "direction": r3(s["direction"]),
                       "color": col, "intensity": s["intensity_cd"], "range": s["range_m"],
                       "innerCone": s["inner_deg"], "outerCone": s["outer_deg"], "castsShadow": False,
                       "volumetric": 0.0})
        tracks.append(track("lights/%s/intensity" % s["name"], breath_keys(s["intensity_cd"], 0.3)))
        routes.append(AU.route("stem.kick.onset", "lights/%s/intensity" % s["name"], round(0.6 * s["intensity_cd"], 1),
                               attackMs=15.0, decayMs=250.0))
        dx, dy, dz = s["direction"]
        beams.append({"name": s["name"] + "_beam", "origin": r3(s["pos"]), "count": 1,
                      "yaw": round(math.degrees(math.atan2(-dz, dx)), 2),
                      "elevation": round(math.degrees(math.asin(max(-1.0, min(1.0, dy)))), 2), "rpm": 0.0,
                      "length": round(s["range_m"] * 1.2, 1), "halfAngle": round(0.5 * s["outer_deg"], 2),
                      "radius": 0.12, "color": col, "intensity": 0.9, "_kind": "floor"})
    for s in band["sky_beams"]:
        beams.append({"name": s["name"], "origin": r3(s["origin"]), "count": s.get("count", 1), "yaw": s["yaw_deg"],
                      "elevation": s["elevation_deg"], "rpm": s.get("rpm", 0.0), "length": s["length_m"],
                      "halfAngle": s["half_angle_deg"], "radius": 0.25, "color": pal(s["key"]),
                      "intensity": round(1.6 * s.get("intensity", 1.0), 3), "_kind": "sky"})
    info["breath_keys"] = breath_keys
    return nodes, lights, beams, tracks, routes, actors, info


def beam_tracks(beams, base_index, crashes, breath_keys, song=70.18):
    """Per band beam (cloud beam index base_index + i): the floor beams' breath dip and kick pulse, the sky beams'
    two-bar sway (+-12 deg), a quarter-turn whip on each crash, held still through the breath; snare +40 %."""
    tracks, routes = [], []
    a, b, c, d = BREATH
    for i, bm in enumerate(beams):
        k = base_index + i
        if bm["_kind"] == "floor":
            tracks.append(track("clouds/beam/%d/intensity" % k, breath_keys(bm["intensity"], 0.3)))
            routes.append(AU.route("stem.kick.onset", "clouds/beam/%d/intensity" % k, round(0.6 * bm["intensity"], 3),
                                   attackMs=15.0, decayMs=250.0))
            continue
        keys, t, whip = [], 0.0, 0.0
        period = 8 * BEAT
        ph = i * 0.25
        while t <= song:
            # a quarter turn on each crash, over 0.82 s from 120 ms before it (AU.CRASH is already the onset - 0.12)
            w = sum(90.0 * _ease((t - cr) / 0.82) for cr in crashes)
            hold = 1.0
            if a <= t <= d:   # the breath: the sway eases to a stop and back
                hold = 0.0 if b <= t <= c else (1.0 - _ease((t - a) / (b - a)) if t < b else _ease((t - c) / (d - c)))
            sway = 12.0 * math.sin(2 * math.pi * (t / period + ph)) * hold
            keys.append({"time": round(t, 3), "value": round(bm["yaw"] + sway + w, 2), "interp": "linear"})
            t += 0.1
        tracks.append(track("clouds/beam/%d/yaw" % k, keys))
        routes.append(AU.route("stem.snare.onset", "clouds/beam/%d/intensity" % k, round(0.4 * bm["intensity"], 3),
                               attackMs=10.0, decayMs=180.0))
        routes.append(AU.route("stem.crash.onset", "clouds/beam/%d/intensity" % k, round(1.0 * bm["intensity"], 3),
                               attackMs=150.0, decayMs=2200.0))
    return tracks, routes


def _ease(u):
    u = max(0.0, min(1.0, u))
    return u * u * (3 - 2 * u)
