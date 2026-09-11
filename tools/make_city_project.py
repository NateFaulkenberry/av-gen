#!/usr/bin/env python3
"""Writes the "Night Shift" music-video project (ADR-089) and its lyric file.

    examples/city/night-shift.json   the project: look, audio routes and the sequence
    examples/city/night-shift.lrc    the lyrics, in the format lyric sites hand out

This is the proof-of-concept the sequencer exists for: five shots cut to a song's own sections, a
character who walks through two districts, a camera that follows and then reveals, lyrics that
arrive on the line, a border, a dusk that becomes night, and four audio-reactive relationships that
are deliberately small.

Generated rather than hand-written for one reason: camera positions. Every shot here is described
the way a cinematographer describes one -- "isometric, 215 metres out, 38 degrees round, 30 up,
looking at the character" -- and `iso()` turns that into the two vectors the engine wants. Writing
those vectors by hand is how a camera ends up inside a building.

The resulting file is an ordinary project. Open it, drag a shot, press play; nothing about it needs
this script again.

Usage: make_city_project.py [--out examples/city]
"""
import argparse
import json
import math
import os

# ---- the song ----------------------------------------------------------------------------------
# Section boundaries, from tools/make_city_score.py. 96 BPM, a bar is 2.5 s.
INTRO, VERSE, CHORUS, VERSE2, BREAK, CHORUS2, OUTRO, END = 0.0, 20.0, 40.0, 60.0, 75.0, 80.0, 100.0, 105.0

# The two districts share the same ground (see make_city_scene.py), so a cut between them changes
# nothing but which node is visible -- and the character walks the same coordinates in both.
DOWNTOWN_X = 0.0


def iso(target, dist, az_deg, el_deg):
    """A camera `dist` away from `target`, `az_deg` round from +X and `el_deg` above the ground.

    The whole camera language of this piece is three numbers and a point. An isometric city shot is
    a long lens at 30 degrees; a tracking shot is a short one at 18; a reveal is the same azimuth
    with the distance and the elevation both climbing. Saying it that way makes those relationships
    visible in the source, which a list of world-space triples never does.
    """
    az, el = math.radians(az_deg), math.radians(el_deg)
    return [round(target[0] + dist * math.cos(el) * math.cos(az), 3),
            round(target[1] + dist * math.sin(el), 3),
            round(target[2] + dist * math.cos(el) * math.sin(az), 3)]


def key(t, position, target, interp="smooth", focal=0.0):
    k = {"time": round(t, 3), "position": [round(v, 3) for v in position],
         "target": [round(v, 3) for v in target], "interp": interp}
    if focal > 0.0:
        k["focalLength"] = focal
    return k


def track(target, keys, interp="smooth", component=-1):
    return {"target": target, "component": component, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "enabled": True,
            "keys": [{"time": round(t, 3),
                      "value": v if isinstance(v, list) else [v],
                      "interp": interp} for t, v in keys]}


# ---- the character's walk ----------------------------------------------------------------------
# Plaza: the street runs along X at z = 0 and the character walks the south lane at z = -3.4.
# Downtown: the avenue is at z = -40 and the fountain is at the origin of that district.
WALK = [
    (0.0,   (-62.0, 0.0, -3.4)),
    (12.0,  (-62.0, 0.0, -3.4)),   # still, through the intro
    (20.0,  (-50.0, 0.0, -3.4)),
    (40.0,  (-8.0, 0.0, -3.4)),
    (52.0,  (26.0, 0.0, -3.4)),    # running through the chorus
    (59.9,  (64.0, 0.0, -5.6)),    # arrives at the lit doorway; Step, because the next key is a cut
    (60.0,  (DOWNTOWN_X - 30.0, 0.0, -16.0)),
    (75.0,  (DOWNTOWN_X - 17.0, 0.0, -13.0)),
    (88.0,  (DOWNTOWN_X - 6.0, 0.0, -9.0)),
    (96.0,  (DOWNTOWN_X + 5.0, 0.0, -7.0)),
    (END,   (DOWNTOWN_X + 5.0, 0.0, -7.0)),
]

CLIPS = [
    (0.0, "Idle", 1.0),
    (12.0, "Walk", 1.0),
    (44.0, "Run", 1.05),
    (56.0, "Walk", 1.0),
    (94.5, "Idle", 1.0),
]


def walker_at(t):
    """Where the character is at `t`, the same way the engine reads its keys: held at the ends,
    interpolated between. Only used to aim cameras at it from this script."""
    if t <= WALK[0][0]:
        return WALK[0][1]
    for (t0, p0), (t1, p1) in zip(WALK, WALK[1:]):
        if t0 <= t <= t1:
            u = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            u = u * u * (3.0 - 2.0 * u)  # the engine's Smooth, near enough for framing
            return tuple(a + (b - a) * u for a, b in zip(p0, p1))
    return WALK[-1][1]


# ---- lyrics ------------------------------------------------------------------------------------
# Written here, so the project carries no third-party words either. Two verses and a repeated
# chorus, short enough to read at a glance -- spec 51 is right that text must not own the frame.
LYRICS = [
    (22.0, "SODIUM LIGHT"),
    (26.0, "ON EVERY STREET"),
    (30.0, "A LITTLE GREEN"),
    (33.5, "BENEATH MY FEET"),
    (41.0, "AND THE CITY"),
    (45.0, "KEEPS ITS OWN TIME"),
    (49.0, "EVERY WINDOW"),
    (52.5, "A DIFFERENT LINE"),
    (62.0, "NOBODY ASKS"),
    (66.0, "WHERE I'M FROM"),
    (70.0, "THE LAMPS COME ON"),
    (73.0, "ONE BY ONE"),
    (81.0, "AND THE CITY"),
    (85.0, "KEEPS ITS OWN TIME"),
    (89.0, "EVERY WINDOW"),
    (92.5, "A DIFFERENT LINE"),
]
LYRIC_GAP = 0.35
LYRIC_HOLD = 3.0


def lyric_cues():
    """The same rule seq::lyricCues applies to an LRC import: a line runs until the next begins,
    less a gap, and the last one holds. Kept identical so the committed project and a fresh import
    of the committed .lrc produce the same timings."""
    cues = []
    for i, (start, text) in enumerate(LYRICS):
        nxt = LYRICS[i + 1][0] if i + 1 < len(LYRICS) else start + LYRIC_HOLD
        end = max(nxt - LYRIC_GAP, start + 0.4)
        cues.append({
            "id": f"lyric{i + 1:03d}",
            "kind": "text",
            "content": text,
            "style": "lyric",
            "start": round(start, 3),
            "end": round(end, 3),
            "order": 10,
            "anchor": [0.5, 0.135],
            "pivot": [0.5, 0.5],
            "size": 1.0,
            "rotation": 0.0,
            "color": [0.97, 0.95, 0.92, 1.0],
            "preset": "fadeInOut",
            "presetSeconds": min(0.32, (end - start) * 0.45),
        })
    return cues


def write_lrc(path):
    with open(path, "w") as f:
        f.write("[ti:Night Shift]\n[ar:AV Gen]\n[al:proof of concept]\n")
        f.write("[re:tools/make_city_project.py]\n\n")
        for start, text in LYRICS:
            m, s = divmod(start, 60.0)
            f.write(f"[{int(m):02d}:{s:05.2f}]{text}\n")


# ---- the shots ---------------------------------------------------------------------------------


def shots():
    out = []

    # 1. INTRO -- a wide isometric of the plaza, pushing slowly in. Dips up from black, because the
    #    piece begins with a pad and nothing to see yet.
    t0, t1 = INTRO, VERSE
    aim0 = [-40.0, 7.0, -1.0]
    aim1 = [-34.0, 6.0, -2.0]
    out.append({
        "name": "First Light", "start": t0, "duration": t1 - t0, "scene": "plaza",
        "in": {"kind": "fadeIn", "seconds": 2.6},
        "camera": {"kind": "keys", "lookAtWeight": 0.0, "samples": 24, "keys": [
            key(0.0, iso(aim0, 142.0, 16.0, 27.0), aim0, "easeOut", focal=46.0),
            key(t1 - t0, iso(aim1, 108.0, 13.0, 24.0), aim1, "smooth"),
        ]},
    })

    # 2. VERSE -- the walk. The camera trails the character down the middle of the road, a little
    #    above it: the one framing where a street reads as a street rather than as two rows of
    #    boxes. The aim is the character itself, so the shot survives the character being retimed.
    t0, t1 = VERSE, CHORUS
    offset = (-38.0, 21.0, 6.5)
    keys = []
    for i in range(5):
        t = t0 + (t1 - t0) * i / 4.0
        p = walker_at(t)
        keys.append(key(t - t0, [p[0] + offset[0], offset[1], p[2] + offset[2]],
                        [p[0], 1.2, p[2]], "linear", focal=42.0 if i == 0 else 0.0))
    out.append({
        "name": "The Walk", "start": t0, "duration": t1 - t0, "scene": "plaza",
        "camera": {"kind": "keys", "lookAtActor": "hero", "lookAtHeight": 1.15,
                   "lookAtWeight": 1.0, "samples": 24, "keys": keys},
    })

    # 3. CHORUS -- the reveal. Up and out, and the aim hands off from the character to the city:
    #    lookAtWeight is 0 here and the targets are authored, because the *subject changes* during
    #    this shot and that is the whole point of it.
    t0, t1 = CHORUS, VERSE2
    start = walker_at(t0)
    mid = walker_at(t0 + (t1 - t0) * 0.45)
    wide = [6.0, 9.0, -2.0]
    out.append({
        "name": "Reveal", "start": t0, "duration": t1 - t0, "scene": "plaza",
        "out": {"kind": "fadeOut", "seconds": 1.4},
        "camera": {"kind": "keys", "lookAtWeight": 0.0, "samples": 32, "keys": [
            key(0.0, [start[0] + offset[0], offset[1], start[2] + offset[2]],
                [start[0], 1.2, start[2]], "easeIn", focal=35.0),
            key((t1 - t0) * 0.40, iso([mid[0], 8.0, mid[2]], 96.0, 20.0, 30.0),
                [mid[0], 5.0, mid[2]], "smooth"),
            key(t1 - t0, iso(wide, 178.0, 26.0, 34.0), wide, "easeOut"),
        ]},
    })

    # 4. VERSE 2 + BREAK -- the cut across town. A different district, a different palette, and the
    #    camera simply travelling: after a reveal, a shot that does less.
    t0, t1 = VERSE2, CHORUS2
    keys = []
    for i in range(4):
        t = t0 + (t1 - t0) * i / 3.0
        p = walker_at(t)
        # The aim sits between the character and the tower bases: this is the shot where the place
        # is the subject and the character is the scale reference, not the other way round.
        keys.append(key(t - t0, [p[0] - 16.0, 11.0, p[2] - 38.0], [p[0] + 10.0, 2.0, p[2] + 30.0],
                        "linear", focal=34.0 if i == 0 else 0.0))
    out.append({
        "name": "Crosstown", "start": t0, "duration": t1 - t0, "scene": "downtown",
        "in": {"kind": "fadeIn", "seconds": 1.2},
        "camera": {"kind": "keys", "lookAtActor": "hero", "lookAtHeight": 1.3,
                   "lookAtWeight": 0.22, "samples": 28, "keys": keys},
    })

    # 5. FINAL CHORUS + OUTRO -- the arrival. The camera circles the fountain while the character
    #    walks into it, then settles; the title lands on the last downbeat and the piece dips out.
    t0, t1 = CHORUS2, END
    centre = [DOWNTOWN_X + 1.0, 6.0, 2.0]
    out.append({
        "name": "Arrival", "start": t0, "duration": t1 - t0, "scene": "downtown",
        "out": {"kind": "fadeOut", "seconds": 4.5},
        "camera": {"kind": "keys", "lookAtActor": "hero", "lookAtHeight": 1.25,
                   "lookAtWeight": 0.55, "samples": 32, "keys": [
                       key(0.0, iso(centre, 74.0, 206.0, 14.0), centre, "easeIn", focal=40.0),
                       key((t1 - t0) * 0.55, iso(centre, 60.0, 250.0, 12.5), centre, "smooth"),
                       key(t1 - t0, iso(centre, 50.0, 292.0, 11.0), centre, "easeOut"),
                   ]},
    })
    return out


# ---- the look, over time ------------------------------------------------------------------------


def look_tracks():
    """Dusk becoming night (spec 17), as ordinary parameter tracks on the sequence.

    Nothing here is a special case in the engine: a sky colour, a fog density and a sun intensity
    are parameters like any other, so the thing that turns evening into night is the same machinery
    that moves the camera. That equivalence is the point of section 17, and this is what it buys.
    """
    return [
        # The sun goes down through the first reveal and is gone by the time we reach downtown.
        track("env/sky/sunIntensity", [(0.0, 9.0), (CHORUS, 6.0), (VERSE2, 1.1), (CHORUS2, 0.25)]),
        track("env/sky/zenithColor", [(0.0, [0.055, 0.080, 0.170]), (VERSE2, [0.022, 0.030, 0.070]),
                                      (CHORUS2, [0.010, 0.014, 0.036])]),
        track("env/sky/horizonColor", [(0.0, [0.42, 0.26, 0.22]), (VERSE2, [0.16, 0.12, 0.17]),
                                       (CHORUS2, [0.055, 0.050, 0.090])]),
        track("env/intensity", [(0.0, 0.55), (VERSE2, 0.24), (CHORUS2, 0.13)]),
        track("scene/keyLight", [(0.0, 1.0), (VERSE2, 0.42), (CHORUS2, 0.16)]),
        # Fog cools and thickens: the far towers should read as far even after the sun has gone.
        track("scene/fogColor", [(0.0, [0.075, 0.070, 0.090]), (VERSE2, [0.035, 0.042, 0.075]),
                                 (CHORUS2, [0.020, 0.028, 0.058])]),
        track("scene/fogDensity", [(0.0, 0.0016), (VERSE2, 0.0024), (CHORUS2, 0.0030)]),
        track("scene/styledSkyAmbient", [(0.0, [0.013, 0.019, 0.040]), (VERSE2, [0.007, 0.010, 0.024]),
                                         (CHORUS2, [0.005, 0.007, 0.018])]),
        track("scene/styledGroundAmbient", [(0.0, [0.006, 0.0055, 0.005]), (CHORUS2, [0.003, 0.0027, 0.0025])]),
        track("env/sky/intensity", [(0.0, 0.22), (VERSE2, 0.13), (CHORUS2, 0.08)]),
        # The practicals come up as the light goes down. One track for the whole plaza's windows
        # would be nicer still; this is the two nodes that carry the most of them.
        track("nodes/plaza/nodes/north-win0/emissiveBoost", [(0.0, 0.35), (CHORUS, 0.8), (VERSE2, 1.25)]),
        track("nodes/plaza/nodes/north-win1/emissiveBoost", [(0.0, 0.30), (CHORUS, 0.9), (VERSE2, 1.35)]),
        track("nodes/plaza/nodes/north-win2/emissiveBoost", [(0.0, 0.25), (CHORUS, 0.7), (VERSE2, 1.20)]),
        track("nodes/plaza/nodes/south-win0/emissiveBoost", [(0.0, 0.40), (CHORUS, 0.95), (VERSE2, 1.30)]),
        track("nodes/plaza/nodes/south-win1/emissiveBoost", [(0.0, 0.30), (CHORUS, 0.85), (VERSE2, 1.25)]),
        track("nodes/plaza/nodes/south-win2/emissiveBoost", [(0.0, 0.25), (CHORUS, 0.75), (VERSE2, 1.15)]),
        track("nodes/plaza/nodes/lamps-n-bulb/emissiveBoost", [(0.0, 0.5), (CHORUS, 1.0), (VERSE2, 1.35)]),
        track("nodes/plaza/nodes/lamps-s-bulb/emissiveBoost", [(0.0, 0.5), (CHORUS, 1.0), (VERSE2, 1.35)]),
        # Tilt-shift is what makes a wide city shot read as a model of a city. It is strongest on
        # the two shots that are actually wide, and off during the tracking shot, where a blurred
        # foreground would just be a blurred foreground.
        track("post/tiltShift/bandWidth",
              [(0.0, 0.16), (VERSE, 0.55), (CHORUS + 8.0, 0.55), (VERSE2 - 1.0, 0.20),
               (CHORUS2, 0.34), (END, 0.34)]),
        track("post/bloom/intensity", [(0.0, 0.22), (CHORUS, 0.30), (CHORUS2, 0.40)]),
        track("post/grade/saturation", [(0.0, 0.96), (CHORUS, 1.02), (CHORUS2, 1.10)]),
    ]


def routes():
    """Audio reactivity, kept deliberately small (spec 49): four relationships, none of them loud.

    A city where every window pulses on the beat is not a city, it is a visualiser wearing a city.
    So: the beat nudges the street lamps, the bass drives one neon sign, the energy opens the sky a
    little, and the drop pushes the bloom. Everything else in the frame is choreographed.
    """
    def route(source, target, amount, **chain):
        return {"source": source, "target": target, "component": -1, "amount": amount,
                "op": "add", "polarity": "unipolar", "enabled": True,
                "chain": {"gain": 1.0, "offset": 0.0, "curve": "linear", "curveAmount": 1.0,
                          "clampEnabled": False, "clampMin": 0.0, "clampMax": 1.0,
                          "threshold": "none", "thresholdLevel": 0.5,
                          "attackMs": chain.get("attackMs", 10.0),
                          "decayMs": chain.get("decayMs", 220.0),
                          "envelope": chain.get("envelope", "none"),
                          "envelopeHoldMs": chain.get("envelopeHoldMs", 0.0),
                          "envelopeFallPerSecond": chain.get("envelopeFallPerSecond", 4.0),
                          "remapEnabled": False, "remapInMin": 0.0, "remapInMax": 1.0,
                          "remapOutMin": 0.0, "remapOutMax": 1.0}}

    return [
        # The beat, on the lamps. A fifth of their brightness, with a slow decay, so it reads as the
        # city breathing rather than as a strobe.
        route("audio.beat", "nodes/plaza/nodes/lamps-n-bulb/emissiveBoost", 0.22,
              attackMs=6.0, decayMs=340.0, envelope="peakhold", envelopeFallPerSecond=3.0),
        route("audio.beat", "nodes/plaza/nodes/lamps-s-bulb/emissiveBoost", 0.22,
              attackMs=6.0, decayMs=340.0, envelope="peakhold", envelopeFallPerSecond=3.0),
        route("audio.beat", "nodes/downtown/nodes/dt-lamps-bulb/emissiveBoost", 0.25,
              attackMs=6.0, decayMs=320.0, envelope="peakhold", envelopeFallPerSecond=3.0),
        # The bass, on one neon sign.
        route("audio.bass", "nodes/downtown/nodes/neon-tall/emissiveBoost", 0.55,
              attackMs=25.0, decayMs=260.0),
        route("audio.treble", "nodes/downtown/nodes/neon-band/emissiveBoost", 0.35,
              attackMs=18.0, decayMs=300.0),
        # Energy, on the sky's own brightness: the section changes are already keyed, so this only
        # has to carry the difference between a loud bar and a quiet one.
        route("audio.rms", "env/sky/intensity", 0.22, attackMs=300.0, decayMs=900.0),
        # The drop, on the bloom. One accent, once, where the arrangement asks for it.
        route("music.drop", "post/bloom/intensity", 0.18,
              envelope="peakhold", envelopeHoldMs=400.0, envelopeFallPerSecond=0.6),
    ]


def sequence():
    cues = lyric_cues()
    cues.append({
        "id": "title", "kind": "text", "content": "NIGHT SHIFT", "style": "title",
        "start": 96.0, "end": 104.0, "order": 20, "anchor": [0.5, 0.70], "pivot": [0.5, 0.5],
        "size": 1.0, "rotation": 0.0, "color": [0.98, 0.94, 0.88, 1.0],
        "preset": "scalePop", "presetSeconds": 0.6,
    })
    cues.append({
        "id": "border", "kind": "shape", "content": "rectangle", "start": 0.0, "end": END,
        "order": -10, "anchor": [0.5, 0.5], "pivot": [0.5, 0.5], "size": 1.0, "rotation": 0.0,
        "color": [0.0, 0.0, 0.0, 0.0], "preset": "none", "presetSeconds": 0.45,
        "extra": {"size": [1.70, 0.93], "strokeWidth": 0.0035,
                  "strokeColor": [0.92, 0.88, 0.82, 0.42]},
    })

    markers = [
        {"time": INTRO, "name": "INTRO", "kind": "section"},
        {"time": VERSE, "name": "VERSE", "kind": "section"},
        {"time": CHORUS, "name": "CHORUS", "kind": "section"},
        {"time": VERSE2, "name": "VERSE 2", "kind": "section"},
        {"time": BREAK, "name": "BREAK", "kind": "section"},
        {"time": CHORUS2, "name": "CHORUS 2", "kind": "section"},
        {"time": OUTRO, "name": "OUTRO", "kind": "section"},
    ]

    actor_keys = []
    for t, p in WALK:
        k = {"time": round(t, 3), "position": [round(v, 3) for v in p], "interp": "smooth"}
        if abs(t - 59.9) < 1e-6:
            # The cut. A Step key here, or the character smears six hundred metres across one frame.
            k["interp"] = "step"
        actor_keys.append(k)

    return {
        "name": "night-shift",
        "duration": END,
        "scenes": [
            {"id": "plaza", "node": "plaza", "file": "plaza.scene.json"},
            {"id": "downtown", "node": "downtown", "file": "downtown.scene.json"},
        ],
        "shots": shots(),
        "actors": [{
            "id": "hero", "node": "walker", "visible": True,
            "keys": actor_keys,
            "clips": [{"time": t, "clip": c, "speed": s} for t, c, s in CLIPS],
        }],
        "overlays": cues,
        "markers": markers,
        "tracks": look_tracks(),
    }


def project():
    return {
        "format": "avgen-project",
        "version": 4,
        "app": {"name": "avgen", "version": "0.1.0"},
        "assets": {
            "audio": {"path": "../../assets/audio/night-shift.wav"},
            "scene": {"kind": "composition", "path": "night-shift.scene.json"},
        },
        "parameters": {
            # A diorama at dusk. Exposure pulled down so the practicals have somewhere to go, a
            # filmic tone curve, and a band of focus across the middle of the frame.
            "camera/exposure/mode": 0,
            "camera/exposure/compensation": -2.1,
            "post/tonemap/operator": 1,
            "post/tonemap/chroma-retention": 0.35,
            "post/bloom/enabled": True,
            "post/bloom/intensity": 0.34,
            "post/bloom/threshold": 0.85,
            "post/bloom/knee": 0.55,
            "post/bloom/radius": 1.15,
            "post/bloom/emissionWeight": 0.55,
            "post/grade/contrast": 1.10,
            "post/grade/saturation": 0.96,
            "post/grade/lift": [0.004, 0.006, 0.012],
            "post/grade/temperature": -0.06,
            "post/output/vignette": 0.30,
            "post/output/grain": 0.012,
            "post/tiltShift/enabled": True,
            "post/tiltShift/centre": [0.5, 0.56],
            "post/tiltShift/bandWidth": 0.16,
            "post/tiltShift/falloff": 0.34,
            "post/tiltShift/maxRadius": 5.5,
            "post/halation/enabled": True,
            "post/halation/intensity": 0.22,
            "post/halation/threshold": 2.4,
            "scene/fogDensity": 0.0016,
            "scene/fogColor": [0.075, 0.070, 0.090],
        },
        "routes": routes(),
        "sources": [],
        "presets": [],
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video",
                   "startSeconds": 0.0, "endSeconds": END},
        "sequence": sequence(),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="examples/city")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    path = os.path.join(args.out, "night-shift.json")
    with open(path, "w") as f:
        json.dump(project(), f, indent=1)
        f.write("\n")
    print(f"{path}: {len(shots())} shots, {len(lyric_cues())} lyric cue(s)")

    lrc = os.path.join(args.out, "night-shift.lrc")
    write_lrc(lrc)
    print(f"{lrc}: {len(LYRICS)} line(s)")


if __name__ == "__main__":
    main()
