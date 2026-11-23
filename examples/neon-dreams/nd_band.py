"""Neon Dreams iteration 12 (owner msg 149): the band in the garden plaza, from the art's art/layout.json `band` block.

- The astronaut musicians (assets/musicians: gitignored, the owner's licences forbid redistribution; never commit),
  each performer and its props on one transform. Their clips run on the song's grid (85.49 BPM since it15: the Piano
  clip a whole number of beats a cycle; the Drums time-warped onto the played notes) and play only while their part does: a crowd `lag` track slows the drummer's clip to 0.15 when the
  drums rest and the pianist's between the Moog's phrases (ADR-2117: a seek-safe change of speed). A one-beat
  `crowd-sway` Secondary layer on the spine, neck and head is pumped by the drum hits (the drummer) and the Moog notes
  (the pianist), so the bodies hit with the music. The drummer flails on each crash.
- The stage spots: real spot lights on the band (the art's 8) and, for the beams in the air, ADR-2102 cloud beams --
  the 8 floor spots (short, wide) and the 4 halo searchlights (`sky_beams`) -- which scatter in the air and the wisps.
  Floor spots +60 % on the kick, sky beams +40 % on the snare and swaying +-12 deg over two bars, whipping a quarter
  turn on the crashes; the held breath (45.5-47.03 s) dims the floor spots to 30 % and holds the sky beams still.
- The audience: the art's `band_*` spots (nd_cast.py: crowd `band`, heads to the stage) erupt (Crazy) on the 47.03 s
  crash after the held breath.
Every envelope is a timeline track from the measured onsets or a route on a stem: seek-safe.
"""
from __future__ import annotations

import math
import os

import nd_audio as AU

BEAT = AU.BEAT   # it15 (msgs 232/238): the true quarter, 0.7018 s, 85.49 BPM (it13-14: 0.351 s, the eighth note)
E = AU.EVENTS["stems"]
ONSETS = {k: [t for t, _ in v["onsets"]] for k, v in E.items()}
BREATH = AU.BREATH   # it15: (45.5, 46.1, 47.03, 47.43) -- released on the crash
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
                                    "layers": [performer_layer(3.0 if drums else 4.0, kick0)]}})
        nodes.append({"name": m["name"] + "_props", "kind": "gltf", "asset": props, "position": r3(m["pos"]),
                      "rotation": [0.0, m["yaw_deg"], 0.0], "scale": [1.0, 1.0, 1.0]})
        info["crowds"].append(crowd)
        if drums:
            for stem, g in (("kick", 1.0), ("snare", 0.9), ("toms", 0.8)):
                # it13 (msg 178): a 3 deg lean with a 45 ms rise (it12: 7 deg in 6 ms jerked the arms up on every
                # note, 10 cm in a frame, over the clip's own strokes)
                routes.append(AU.route("stem.%s.onset" % stem, "crowds/%s/sway" % crowd, g, attackMs=45.0,
                                       decayMs=260.0))
            # it13 (owner msg 178: "drummer animation playing too fast"): the kit clip is time-warped so each of its
            # stick strokes lands on a played note (drum_warp), no longer looped at 1.03 whatever the drums did (13
            # strokes every 4.55 s through rests and fills alike) with a 5 s Flail thrown in on each crash.
            clips, info["drum_strokes"] = drum_warp(m["clip"], L)
            actors.append({"id": m["name"], "node": m["name"], "visible": True, "keys": [], "clips": clips})
            # no lag track for the drummer (its crowd keeps the sway): the warp's cues are timeline seconds
            nodes[-2]["animation"]["speed"], nodes[-2]["animation"]["phase"] = 1.0, 0.0
            continue
        else:
            sp = active_speed(ONSETS["moog"], 0.35, 1.4, rest=0.2)
            routes.append(AU.route("stem.moog.onset", "crowds/%s/sway" % crowd, 1.0, attackMs=10.0, decayMs=420.0))
        tracks.append(track("crowds/%s/lag" % crowd, lag_keys(sp, song)))

    # the spot lights on the band, and their beams in the air
    def breath_keys(base, floor):
        a, b, c, d = BREATH
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
    # it12c (owner msg 161: "get the drum illuminated as well as the synth player"): a key light on each performer
    # from the halo, above and in front, unless the art's data names its own (`band.key_lights`)
    st = band["stage"]
    fd = st.get("front_dir", [-0.94, 0.0, -0.342])
    art_keys = any("drumkey" in sp["name"] for sp in band["spots"])   # the art's own drummer spots (msg 165)
    for m in band["musicians"]:
        if art_keys or any(k.get("musician") == m["name"] for k in band.get("key_lights", [])):
            continue
        ch = m.get("chest", [m["pos"][0], m["pos"][1] + 1.1, m["pos"][2]])
        src = [ch[0] + fd[0] * 3.5, st.get("halo_y", ch[1] + 6.5), ch[2] + fd[2] * 3.5]
        d = [ch[i] - src[i] for i in range(3)]
        n = math.sqrt(sum(v * v for v in d)) or 1.0
        name = m["name"] + "_key"
        drums = "drum" in m["asset"]
        inten = 1500.0 if drums else 1100.0
        lights.append({"name": name, "type": "spot", "position": r3(src), "direction": r3([v / n for v in d]),
                       "color": pal("moon_disc"), "intensity": inten, "range": 16.0, "innerCone": 9.0,
                       "outerCone": 18.0, "castsShadow": False, "volumetric": 0.0})
        tracks.append(track("lights/%s/intensity" % name, breath_keys(inten, 0.55)))
        stem = "kick" if drums else "moog"
        routes.append(AU.route("stem.%s.onset" % stem, "lights/%s/intensity" % name, round(0.5 * inten, 1),
                               attackMs=10.0, decayMs=220.0))
    for k in band.get("key_lights", []):   # the art's own, when it adds them
        lights.append({"name": k["name"], "type": "spot", "position": r3(k["pos"]), "direction": r3(k["direction"]),
                       "color": pal(k.get("key", "moon_disc")), "intensity": k.get("intensity_cd", 1500.0),
                       "range": k.get("range_m", 16.0), "innerCone": k.get("inner_deg", 9.0),
                       "outerCone": k.get("outer_deg", 18.0), "castsShadow": False, "volumetric": 0.0})
    # it12c (owner msg 161: "speakers and a beat-pulse visual wave effect (visual distortion)"): on every kick a
    # front leaves each speaker stack -- an expanding ring of refraction (the engine's Shockwave, DF) with a faint
    # emissive edge, gone in ~0.3 s. Triggered by sequence markers named "kick" at the kick stem's onsets (TRIGGER's
    # marker source: a pure function of the second, so a seek draws the fronts a play does).
    effects = []
    speakers = band.get("speakers") or _default_speakers(st)
    for i, sp in enumerate(speakers):
        c = sp.get("pulse_centre", sp.get("emitter", sp.get("pos")))
        effects.append({"id": "kick_pulse_%d" % i, "name": "kick pulse %s" % sp.get("name", i), "type": "shockwave",
                        "owner": {"kind": "world"}, "enabled": True, "timing": {"fadeIn": 0.0, "fadeOut": 0.0}, "order": 10 + i, "activation": "trigger",
                        "trigger": {"source": "marker", "name": "kick"},
                        "parameters": {"strength": PULSE["strength"], "maxRadius": PULSE["reach"],
                                       "duration": PULSE["duration"], "thickness": PULSE["thickness"],
                                       "ease": 0, "rimColor": pal(PULSE["key"]), "rimEmission": PULSE["rim"],
                                       "chroma": PULSE["chroma"], "radiusDecay": 0.6, "maxConcurrent": 2.0,
                                       "maxShift": CALM["shift"], "lensNear": CALM["ring_near"], "lensClear": CALM["ring_clear"],
                                       "offsetX": round(c[0], 3), "offsetY": round(c[1], 3), "offsetZ": round(c[2], 3)}})
    # it14 (owner msg 195: "way more visible and impactful -- the wave should pulsate through the entire scene /
    # geometry / characters", visible down the Quay): ADR-2124's Shockwave through the world, released from the
    # stage on each marker -- it pushes the town and the crowd it passes, lights a band on every surface, ripples
    # the clouds and bends the air where it meets the town and along its silhouette.
    c = st.get("halo_y") and [st["centre"][0], st["deck_y"] + 2.5, st["centre"][2]] or st["centre"]
    effects.append({"id": "kick_wave", "name": "kick wave (through the town)", "type": "shockwave",
                    "owner": {"kind": "world"}, "enabled": True, "timing": {"fadeIn": 0.0, "fadeOut": 0.0}, "order": 20, "activation": "trigger",
                    "trigger": {"source": "marker", "name": "kick"},
                    "parameters": {"travel": "world", "strength": WAVE["haze"], "maxRadius": WAVE["reach"],
                                   "duration": round(WAVE["reach"] / WAVE["speed"], 3), "thickness": WAVE["half"],
                                   "ease": 3, "rimColor": pal(WAVE["key"]), "rimEmission": WAVE["rim"],
                                   "chroma": WAVE["chroma"], "radiusDecay": WAVE["decay"], "refRadius": WAVE["ref"],
                                   "maxConcurrent": 8.0, "push": WAVE["push"], "lift": WAVE["lift"],
                                   "flashColor": [round(0.5 + 0.5 * v, 3) for v in pal(WAVE["key"])], "flash": WAVE["flash"], "flare": WAVE["flare"],
                                   "clouds": WAVE["clouds"], "fadeStart": WAVE["fade"][0], "fadeEnd": WAVE["fade"][1],
                                   "maxShift": CALM["shift"], "lensNear": CALM["near"], "lensClear": CALM["clear"],
                                   "offsetX": round(c[0], 3), "offsetY": round(c[1], 3), "offsetZ": round(c[2], 3)}})
    info["effects"] = effects
    # the held breath silences the wave in flight too, and (msg 222: "way too strong -- should not be visible till we
    # get closer") the lens's distance from the stage gates it: nothing while the lens is past CAM_FADE[1] m (the
    # pier and the approach, <= ~30 s), full from CAM_FADE[0] m in (the Quay from ~35 s, the band)
    a, b, c, d = BREATH

    def gate(t):
        def sm(u):
            u = max(0.0, min(1.0, u))
            return u * u * (3 - 2 * u)
        br = 1.0   # it17 (msg 253: "a shockwave is missed here", 46.17 s): the held breath no longer silences the wave
        g = 1.0
        if CAM_AT is not None:
            g = 1.0 - sm((math.dist(CAM_AT(t), c_stage) - CAM_FADE[0]) / (CAM_FADE[1] - CAM_FADE[0]))
        return br * g
    c_stage = st["centre"]
    gk = [(round(i * 0.25, 2), gate(i * 0.25)) for i in range(int(70.2 / 0.25) + 2)]
    for leaf in ("push", "flash", "clouds", "strength", "rimEmission"):
        v = WAVE[{"strength": "haze", "rimEmission": "rim"}.get(leaf, leaf)]
        tracks.append(track("fx/kick_wave/%s" % leaf, [{"time": t, "value": round(v * g, 4), "interp": "linear"}
                                                        for t, g in gk]))
    # it14 (owner msg 204: "make sure the speaker pulse is on quarter notes -- it's following the kick right now"):
    # one front per quarter note of the song's grid (AU.KICK0 + n x AU.BEAT; it15, msg 232: the TRUE quarter, 0.702 s,
    # 85.49 BPM -- it14's 0.351 s fired on every eighth, twice the beat), the whole song but the
    # held breath (45.5 s to the 47.03 s crash, which brings it back)
    # it17 (owner msg 253: "a shockwave is missed here" at 46.17 s, inside the held breath): every quarter of the song
    # gets its front -- the breath keeps the crowd still and the spots dim, but no longer skips the wave
    grid, n = [], 0
    while AU.KICK0 + n * BEAT < 70.18:
        grid.append(AU.KICK0 + n * BEAT)
        n += 1
    info["markers"] = [{"time": round(t, 4), "name": "kick"} for t in grid]
    info["breath_keys"] = breath_keys
    return nodes, lights, beams, tracks, routes, actors, info


# it13 (msg 178): the Drums clip's stick strokes, clip seconds: the lowest points (4 cm or more) of both hands' mean
# height, read off the drawn rig playing the clip alone at rate 1 with no sway layer (avgen_foot_probe --node-joints
# band_drums, two cycles at 1/60 s); 13 a cycle. DRUM_UP: the hands' highest point between each stroke and the next,
# where a wait holds the sticks up.
DRUM_STROKES = [0.167, 0.733, 1.333, 1.533, 1.9, 2.5, 2.7, 3.09, 3.3, 3.667, 3.9, 4.267, 4.5]
DRUM_UP = [0.533, 0.9, 1.467, 1.733, 2.033, 2.6, 2.9, 3.2, 3.467, 3.833, 4.1, 4.4, 4.68]
STROKE_MIN_GAP = 0.24   # s between two strokes: no faster than 1.4x the clip's own pace (its strokes are ~0.34 s apart)
STROKE_HOLD = 1.25      # a note further off than this x the clip's stroke: up stroke, a wait with sticks up, down on it


def drum_notes(min_gap=STROKE_MIN_GAP):
    """The notes the drummer plays: every crash, then kicks and snares, then toms, each where it is at least min_gap
    from those already taken; hi-hats are not strokes (the clip has no hat hand)."""
    sel = []

    def fits(t):
        return all(abs(t - u) >= min_gap for u in sel)
    for group in (ONSETS["crash"], sorted(ONSETS["kick"] + ONSETS["snare"]), ONSETS["toms"]):
        for t in group:
            if fits(t):
                sel.append(t)
    return sorted(sel)


def drum_warp(clip, L, song=70.18):
    """Clip cues that put stroke k of the clip on note k: between two notes the clip covers one stroke interval, at
    the rate that takes (never under 1 / STROKE_HOLD); a longer gap plays the up stroke at rate 1, waits with the sticks
    up (rate 0.04) and comes down at rate 1 onto the note. Returns (cues, the note times the strokes land on)."""
    notes = [t for t in drum_notes() if t < song]
    H = DRUM_STROKES
    n = len(H)
    cues = []

    def cue(t, off, rate):
        cues.append({"time": round(t, 4), "clip": clip, "speed": round(rate, 4), "blend": 0.0, "playback": "loop",
                     "offset": round(off % L, 4)})
    # into the first note at the clip's own pace
    k = 0
    pos = H[0]          # unwrapped clip position at notes[0]
    cue(max(0.0, notes[0] - pos), 0.0, 1.0)
    for j in range(len(notes)):
        t0 = notes[j]
        t1 = notes[j + 1] if j + 1 < len(notes) else song + 1.0
        nxt = H[(k + 1) % n] + (L if (k + 1) % n == 0 else 0.0)
        h = nxt - H[k % n]
        G = t1 - t0
        if G <= STROKE_HOLD * h:
            cue(t0, pos, h / G)
        else:
            # up to the top of the backswing at rate 1, a wait there, down at rate 1 onto the note
            eps = 0.04
            up = DRUM_UP[k % n] - H[k % n]
            down = h - up
            hold = (G - h) / (1.0 - eps)
            cue(t0, pos, 1.0)
            cue(t0 + up, pos + up, eps)
            cue(t0 + up + hold, pos + up + eps * hold, (down - eps * hold) / max(G - up - hold, 1e-3))
        pos += h
        k += 1
    return [c for c in cues if c["time"] < song], notes


# it14 (msg 195): the wave through the town (ADR-2124). 160 m/s: the Quay's lens (130 m) sees it 0.8 s after it leaves
# the stage, eight fronts in the air; a 12 m half-width band so a building takes ~0.2 s to cross it (a thump, not one
# frame; msg 218: 12 m, every front caught alike at 30 fps); push and flash keep full strength to 25 m and weaken as (25 / r) ^ 0.5 (0.44 at the Quay, 0.23 at 470 m).
# msg 222: a front fades out between 140 and 215 m from the stage (the Quay's lens at 38 s is 132 m out) and the lens
# only sees it from CAM_FADE: full within 190 m of the stage, nothing past 260 m (the pier at 10 s is 470 m out)
CAM_FADE = (190.0, 260.0)
CAM_AT = None   # build.py: the baked lens's eye at t
# it16 (owner msg 243: "tone down the colour FX and the shockwave screen distortion itself -- probably 50 % of what it
# is now in terms of screen distortion"): the bend halved (haze 0.3 -> 0.15, chroma 0.12 -> 0.06; the speakers' rings
# 1.6 -> 0.8, chroma 0.25 -> 0.12) and the colour halved with it (flash 0.12 -> 0.06, flare 0.8 -> 0.4, cloud glow
# 0.2 -> 0.1, edge glow 0.1 -> 0.05; the speakers' pink arcs 0.9 -> 0.45); the shove a little less (0.9 -> 0.75 m)
WAVE = {"fade": (140.0, 215.0), "speed": 160.0, "reach": 230.0, "half": 12.0, "haze": 0.15, "rim": 0.05, "decay": 0.5,
        "ref": 25.0, "push": 0.75, "lift": 0.45, "flash": 0.06, "flare": 0.4, "clouds": 0.1, "chroma": 0.06, "key": "sign_a"}
# it17 (owner msg 253: "the shockwaves still feel like they're jerking the camera way too much"): ADR-2126's calm.
# A front shifts no pixel more than CALM["shift"] px (1080p) -- the bend, and the shove of what it passes as seen from
# the lens; nothing within 8 m of the lens bends, glows or is pushed (full from 16 m); and the bend fades as the front's
# radius comes within CALM["clear"] m of the lens's distance from the stage, so a front passing the lens never warps
# the whole frame.
# A/B at 44-51 s (work/engine/it17/jolt): mean tile shift 3.47 px (max 22.9, 83 % of the frame moved) before.
# The speakers' rings (14 m) hold the band's lens: they fade as they pass within 6 m of it, and within 4 m of it.
# it17 attribution at 48.4-48.8 s (one component on at a time vs none; work/engine/it17/look): the shove of the stage
# and the crowd and the haze each moved half the frame by ~2.5 px under a 3 px cap, the speakers' rings ~1 px, the
# light none -- a coherent shift of everything near reads as the lens moving. So 1.5 px, and the young front clears
# the lens by 20 m before it bends anything.
CALM = {"shift": 1.5, "near": 8.0, "clear": 20.0, "ring_near": 4.0, "ring_clear": 6.0}
# it18 (owner msg 264 on it17: "now the wave doesn't feel strong enough, and too fast"; msg 267: "there can be a
# little camera distortion, I don't mind -- just not as radical as before"). it17's front ran 160 m/s with a 12 m
# half-width: a point sat in its band ~0.15 s and the front crossed the frame in a few frames (a flick). Slower, a
# wider band (a point takes ~0.5 s to rise and fall: a roll, not a snap), a harder push and a brighter band in the
# scene, the band's colour the palette's violet (eave) not sign_a's pink, and the per-front cap 1.5 -> 3.5-5 px (it16
# was 22.9 px worst). Variants for the owner (ND_WAVE=A|B|C at build time); the default is the recommended one.
WAVE_VARIANTS = {
    "A": {"wave": {"speed": 70.0, "half": 18.0, "push": 1.1, "flash": 0.09, "flare": 0.6, "clouds": 0.15,
                   "rim": 0.07, "haze": 0.2, "key": "eave"},
          "calm": {"shift": 3.5, "near": 8.0, "clear": 14.0}},
    "B": {"wave": {"speed": 70.0, "half": 20.0, "push": 1.6, "flash": 0.13, "flare": 0.9, "clouds": 0.22,
                   "rim": 0.1, "haze": 0.28, "chroma": 0.08, "key": "eave"},
          "calm": {"shift": 5.0, "near": 6.0, "clear": 10.0}},
    "C": {"wave": {"speed": 110.0, "half": 16.0, "push": 1.6, "flash": 0.13, "flare": 0.9, "clouds": 0.22,
                   "rim": 0.1, "haze": 0.28, "chroma": 0.08, "key": "eave"},
          "calm": {"shift": 5.0, "near": 6.0, "clear": 10.0}},
}
WAVE_VARIANT = os.environ.get("ND_WAVE", "B")
if WAVE_VARIANT in WAVE_VARIANTS:
    WAVE.update(WAVE_VARIANTS[WAVE_VARIANT]["wave"])
    CALM.update(WAVE_VARIANTS[WAVE_VARIANT]["calm"])
PULSE = {"strength": 0.8, "reach": 14.0, "duration": 0.3, "thickness": 0.7, "rim": 0.45, "chroma": 0.12, "key": "sign_a"}


def _default_speakers(st):
    """Until the art's stacks land: one either side of the stage, 2 m outside its rim, 2.5 m over the deck."""
    c, fd, R = st["centre"], st.get("front_dir", [-0.94, 0.0, -0.342]), st.get("radius_m", 6.0)
    side = [-fd[2], 0.0, fd[0]]
    y = st.get("deck_y", c[1] + 1.0) + 2.5
    return [{"name": n, "emitter": [c[0] + k * side[0] * (R + 2.0) + fd[0] * 1.0, y, c[2] + k * side[2] * (R + 2.0)
                                     + fd[2] * 1.0]} for n, k in (("left", 1.0), ("right", -1.0))]


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
        period = 2 * AU.BAR   # it15: two bars, 5.61 s (it12-14: 8 eighths = 2.81 s, one bar), from the downbeat
        ph = i * 0.25 - AU.KICK0 / period
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
