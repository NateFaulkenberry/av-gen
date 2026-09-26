"""The look of Glowmere Valley 3: the base image, the arc across the film, and the modulation.

Three layers, deliberately kept apart because they answer different questions:

  BASE       what the valley looks like -- the lens, the grade, the water, the haze. Static
             parameters. This is where the diorama problem is fought (docs/glowmere-valley-3/
             03-art-direction.md): no depth of field on a wide, haze that grows with distance, a
             lens that does not bend the world into a fishbowl.
  ARC        how the look travels with the music, segment by segment: exposure, saturation, fog,
             the valley's own light, the aurora. Timeline keys on the true grid, written from the
             directive table below -- a slow envelope, never a beat.
  MOTIFS     what reacts to what, with a hierarchy: a few small things on the beat, the valley's
             breathing on the two-bar bass glide, the sparkle on the top of the mix, and one flash
             on the drop. Routes from phase-locked LFOs and from the audio. Terrain, trees, the
             river's flow and the camera never react.
"""

from . import music

# ---- BASE ----------------------------------------------------------------------------------------
BASE = {
    # The lens. The source's physical depth of field was on for every shot, focused at 8 m, which
    # blurs a distant valley into a model railway (research principle 1); off, and a wide stays
    # deep. The fishbowl distortion, the heavy vignette and the colour fringing all read "lens
    # effect" before they read "place" -- pulled back to seasoning.
    "post/dof/enabled": False,
    "post/lens/chromaticAberration": 0.03,
    "post/lens/distortion": -0.08,
    "post/output/vignette": 0.6,
    "post/anamorphic/intensity": 0.25,
    # The grade. Iteration 0 took contrast to 1.25 and the navy mid-ground went grey; 1.4 keeps the
    # night's value structure (the source had 1.5). The arc drives saturation.
    "post/grade/contrast": 1.4,
    "post/grade/saturation": 1.05,
    # Haze that grows with distance -- toward the NIGHT, not toward a pale sky. Iteration 0 had it at
    # 0.16 with the source's pale tint and every hillside became a flat lilac wall, a daylight fog:
    # the opposite of the cue it was for (05-quality.md F11). Near the source's strength, with the
    # tint taken from the fog colour, it separates the far ridges without lifting them.
    "post/look/atmospheric": 0.035,
    "post/look/atmosphericDistance": 450.0,
    "post/look/atmosphericTint": [0.1, 0.17, 0.3],
    "scene/volumeDensity": 0.02,
    "camera/exposure/compensation": -1.25,
    # The water was the brightest, most saturated thing in most frames: a bright cyan sheet under
    # large glowing patches -- a bathtub. Iteration 0's first pass (glow 0.35) was not enough. Dark
    # water that carries reflections of the lights is what a night river looks like: little glow.
    # Iteration 1 then showed the ripples were the problem: at 0.55 and 1.2 m cells they swung each
    # cell's reflection between the aurora's bright horizon and the black zenith, and the river read
    # as a cobbled road. Faint, fine ripples leave a calm surface carrying the sky, with detail only
    # near the lens, where the shader's footprint fade lets the fine layers resolve (water.wgsl).
    # Chosen from three variants on four stills (06-iterations, iteration 2) as 0.1 at 2.6 cycles/m.
    # That fade is in pixels, and the final renders at twice the previews' internal resolution
    # (1080p at 2x supersampling), where those ripples resolved across the whole far river again. So
    # the frequency is doubled for the final and the amplitude halved: the slope, which is what the
    # eye reads, stays; the distance at which the ripples dissolve into a mirror is the preview's.
    "nodes/valley/water/glow": 0.12,
    "nodes/valley/water/rippleScale": 5.2,
    "nodes/valley/water/ripple": 0.05,
    "nodes/valley/water/reflection": 1.0,
    "nodes/valley/water/fresnel": 0.14,
    "nodes/valley/water/roughness": 0.2,
    "nodes/valley/water/sparkle": 0.06,
    "nodes/valley/water/foam": 0.06,
    "nodes/valley/water/shallowColor": [0.03, 0.1, 0.12],
    # The aurora keeps its slow response to the bands but no longer pulses on every beat.
    "fx/aurora/audioBeat": 0.0,
}

# Effects the film does not use, and why.
REMOVED_EFFECTS = {
    "camera-travel-beam": "a repeating violet streak across the sky every 9 s -- effect soup",
}
REMOVED_EFFECT_TYPES = {
    "groundPulse": "fired only while the Song-mode shot spans spotlit a hero; the cut is authored now",
}


def apply_base(project):
    params = project["parameters"]
    params.update(BASE)
    kept = []
    for e in project.get("effects", []):
        if e["id"] in REMOVED_EFFECTS or e.get("type") in REMOVED_EFFECT_TYPES:
            for key in [k for k in params if k.startswith(f"fx/{e['id']}/")]:
                del params[key]
            continue
        kept.append(e)
    project["effects"] = kept
    # Every route of the source goes: 18 of its 40 made every hero and effect pulse on every beat,
    # and seven more were event routes whose attack swallowed the one-frame event and did nothing.
    project["routes"] = []
    project["sources"] = [s for s in project.get("sources", []) if s.get("kind") == "control"]


# ---- ARC -----------------------------------------------------------------------------------------
# How the look enters each segment -- the shape of the change at its first downbeat. Hard edges where
# the music has one (the pull-back, the break, the drop), a short swell where a layer arrives, and
# the two long gestures of the set-piece: the drain across the suspension as the saucer comes in,
# and the four-bar ramp of the riser.
ENTRY = {
    "cold-open": ("step", 0.0),        # out of black on the first kick
    "riff-groove": ("ease", music.beats(2)),
    "first-pullback": ("step", 0.0),
    "groove-2": ("step", 0.0),
    "lift": ("ease", music.beats(1)),
    "arrival": ("rise", music.beats(1)),
    "melodic-plateau": ("ease", music.beats(4)),
    "lead-forward": ("ease", music.beats(4)),
    "suspension": ("drain", None),     # a ramp across the whole segment
    "submerged-break": ("step", 0.0),
    "riser": ("ramp", None),           # accelerating across the whole segment
    "drop": ("step", 0.0),
    "tail": ("step", 0.0),
}
BLACK = -20.0  # exposure compensation that is black for all practical purposes


def _light_targets(params):
    """The valley's own light: every bioluminescent material, the water's glow, the heroes' spores."""
    out = {k: params[k] for k in params if k.startswith("material/") and k.endswith("/emissionIntensity")}
    out["nodes/valley/water/glow"] = params["nodes/valley/water/glow"]
    for k in params:
        if k.startswith("particles/") and k.endswith("-spores/emissive"):
            out[k] = params[k]
    return out


def _sparkle_targets(params):
    """The shimmer made visible: ambient spores, fireflies, river motes, and how much the heroes shed."""
    out = {}
    for name in ("spores", "tree-fireflies", "river-motes"):
        out[f"particles/{name}/spawnRate"] = params[f"particles/{name}/spawnRate"]
        out[f"particles/{name}/emissive"] = params[f"particles/{name}/emissive"]
    for k in params:
        if k.startswith("particles/") and k.endswith("-spores/spawnRate"):
            out[k] = params[k]
    return out


def arc_values(params):
    """{parameter: [value per segment]} from the directive table."""
    from .directives import DIRECTIVES
    ev0 = BASE["camera/exposure/compensation"]
    fog0 = BASE["scene/volumeDensity"]
    series = {
        "camera/exposure/compensation": [ev0 + d["look"]["ev"] for d in DIRECTIVES],
        "post/grade/saturation": [d["look"]["sat"] for d in DIRECTIVES],
        "post/grade/temperature": [d["look"]["temp"] for d in DIRECTIVES],
        "scene/volumeDensity": [fog0 * d["look"]["fog"] for d in DIRECTIVES],
        "fx/aurora/intensity": [d["look"]["aurora"] for d in DIRECTIVES],
    }
    for k, base in _light_targets(params).items():
        series[k] = [base * d["look"]["light"] for d in DIRECTIVES]
    for k, base in _sparkle_targets(params).items():
        series[k] = [base * d["look"]["sparkle"] for d in DIRECTIVES]
    return series


def arc_keys(values):
    """Timeline keys for one parameter's per-segment values, shaped by ENTRY."""
    from .directives import DIRECTIVES
    keys = []  # (time, value, interp of the span that follows)
    prev = None
    for d, v in zip(DIRECTIVES, values):
        start, end = music.segment(d["segment"])
        kind, span = ENTRY[d["segment"]]
        if prev is None:
            keys.append((0.0, v, "linear"))
        elif kind == "step":
            keys.append((start - 0.001, prev, "linear"))
            keys.append((start, v, "linear"))
        elif kind == "ease":
            keys.append((start - span / 2, prev, "easeInOut"))
            keys.append((start + span / 2, v, "linear"))
        elif kind == "rise":
            keys.append((start, prev, "easeInOut"))
            keys.append((start + span, v, "linear"))
        elif kind == "drain":
            keys.append((start, prev, "linear"))
            keys.append((end - 0.002, v, "linear"))
        elif kind == "ramp":
            keys.append((start, prev, "easeIn"))
            keys.append((end - 0.002, v, "linear"))
        prev = v
    keys.append((music.TAIL_END, prev, "linear"))
    return keys


def track(target, keys, mode="replace"):
    return {"target": target, "component": -1, "timeBase": "seconds", "mode": mode, "loopLength": 0.0,
            "enabled": True,
            "keys": [{"time": round(t, 6), "value": [round(float(v), 6)], "interp": i} for t, v, i in keys]}


def apply_arc(project):
    params = project["parameters"]
    tracks = []
    for target, values in arc_values(params).items():
        keys = arc_keys(values)
        if target == "camera/exposure/compensation":
            # Black through the silent pre-roll, cut in on the first kick; black again on the last hit.
            first = keys[0][1]
            keys = [(0.0, BLACK, "linear"), (music.FIRST_DOWNBEAT - 0.001, BLACK, "linear"),
                    (music.FIRST_DOWNBEAT, first, "linear")] + keys[1:]
            last = keys[-1][1]
            keys = keys[:-1] + [(music.LAST_HIT - 0.001, last, "linear"), (music.LAST_HIT, BLACK, "linear"),
                                (music.TAIL_END, BLACK, "linear")]
        tracks.append(track(target, keys))
    # Black through the pre-roll and after the last hit. Exposure alone is not black: the sky and
    # the bloom are added after it, and iteration 0's first and last frames showed the skyline.
    # The grade's gain multiplies everything.
    one, zero = [1.0, 1.0, 1.0], [0.0, 0.0, 0.0]
    gain = [(0.0, zero), (music.FIRST_DOWNBEAT - 0.001, zero), (music.FIRST_DOWNBEAT, one),
            (music.LAST_HIT - 0.001, one), (music.LAST_HIT, zero), (music.TAIL_END, zero)]
    tracks.append({"target": "post/grade/gain", "component": -1, "timeBase": "seconds", "mode": "replace",
                   "loopLength": 0.0, "enabled": True,
                   "keys": [{"time": round(t, 6), "value": v, "interp": "linear"} for t, v in gain]})
    project["timeline"]["tracks"].extend(tracks)
    return len(tracks)


# ---- MOTIFS ------------------------------------------------------------------------------------------
# One visual owner per musical layer, kept for the whole film (research principle 12), and a
# hierarchy of how strongly each reacts (principle 13): a few small things on the beat, the valley's
# breathing on the two-bar bass glide, the sparkle on the top of the mix, one flash on the crash.
#
# The clocks are free-running LFOs locked to the measured grid rather than the engine's beat clock,
# whose bars start on beat 4 of this track (its first tracked beat is counted 1, not 0). An LFO's
# position is frac(t * rate + phase), so the phase that puts cycle zero on the first downbeat is
# frac(-0.480 * rate). Every visual accent is led by LEAD_SECONDS: viewers forgive a picture a
# little late far more than one late by a frame, and animators hit early for the same reason
# (research principle 17).
LEAD_SECONDS = 0.02


def _lfo_phase(rate, offset_cycles=0.0):
    return (-(music.FIRST_DOWNBEAT - LEAD_SECONDS) * rate + offset_cycles) % 1.0


def _route(source, target, amount, op="add", chain=None, polarity="unipolar"):
    return {"source": source, "target": target, "component": -1, "amount": amount, "op": op,
            "polarity": polarity, "enabled": True, "chain": chain or {}}


def _pulses(name, times, width=1.0 / 60.0):
    """A timeline source that is 1 for one frame at each time and 0 elsewhere -- an event the
    film's own score writes, where the analyser's would land wherever its detector decided. A time
    may be a (seconds, value) pair for a pulse of another height."""
    keys = [{"time": 0.0, "value": 0.0, "interp": "step"}]
    for item in sorted(times):
        t, value = item if isinstance(item, tuple) else (item, 1.0)
        t = t - LEAD_SECONDS
        keys.append({"time": round(t, 6), "value": value, "interp": "step"})
        keys.append({"time": round(t + width, 6), "value": 0.0, "interp": "step"})
    return {"kind": "timeline", "name": name, "settings": {"loopLength": 0.0, "keys": keys}}


def apply_motifs(project):
    params = project["parameters"]
    beat_rate = music.BPM / 60.0
    breath_rate = beat_rate / 8.0  # two bars
    sources = [
        {"kind": "lfo", "name": "breath", "settings": {"shape": "sine"}},
        _pulses("crash", [music.bar(97)]),
        _pulses("gap", music.KICK_GAPS),
        # A frame and a half wide, so every kick lands on at least one frame at 60 fps however its
        # time rounds.
        _pulses("kick", music.kicks(), width=1.5 / 60.0),
    ]
    params.update({
        # The sine peaks at half a cycle; the glide is on beat 4 of the second bar, 7/8 of the way round.
        "sources/breath/rate": breath_rate, "sources/breath/phase": _lfo_phase(breath_rate, 0.5 - 0.875),
        "sources/breath/beatSync": False,
        # The drop's one camera shake: the impact of the crash felt in the frame, 6 cm and a third of a
        # degree, gone in 0.9 s. The only shake in the film (the research: a shake on every beat is
        # the thing the camera vocabulary refused). Its origin is a parameter, not a timer, so a seek
        # lands on the same frame (ADR-098).
        "camera/shake/start": music.bar(97) - LEAD_SECONDS, "camera/shake/amplitude": 0.06,
        "camera/shake/frequency": 7.0, "camera/shake/decay": 0.9, "camera/shake/rotation": 0.35,
    })
    project["sources"] = [s for s in project.get("sources", []) if s.get("kind") == "control"] + sources

    routes = [
        # The heartbeat: the elder's gold flares on every kick the track plays and falls away within a
        # beat -- none in the pull-back or on a gap beat, half in the muffled break. The only thing in
        # the valley that moves with the kick. It multiplies the warm tissue's program emission, which
        # is the elder's alone (its gills and the underside around them) and the only handle on it:
        # a program that writes emission owns it (ADR-179), and a node's emissiveBoost never reaches
        # a procedural node (F23). Its size follows the valley's light, because the arc scales the
        # same emission: nearly gone in the break, full at the drop.
        _route("timeline.kick", "material/glowmere2TissueWarm/emissionIntensity", 1.0, "multiply",
               {"attackMs": 0.0, "decayMs": 110.0, "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0,
                "remapOutMin": 1.0, "remapOutMax": 1.6}),
        # The breath: every cap's glow swells a tenth on the two-bar bass glide -- slow, felt more than seen.
        _route("lfo.breath", "material/glowmere2Cap/emissionIntensity", 1.0, "multiply",
               {"remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 0.9, "remapOutMax": 1.12}),
        _route("lfo.breath", "material/glowmere2TissueCool/emissionIntensity", 1.0, "multiply",
               {"remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 0.92, "remapOutMax": 1.1}),
        _route("lfo.breath", "material/glowmere2TissueWarm/emissionIntensity", 1.0, "multiply",
               {"remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 0.92, "remapOutMax": 1.1}),
        # The sparkle follows the top of the mix -- multiplied onto the arc's level, so where the arc
        # has taken the shimmer away the hats cannot bring it back.
        _route("audio.treble", "particles/spores/emissive", 1.0, "multiply",
               {"attackMs": 20.0, "decayMs": 180.0, "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0,
                "remapOutMin": 0.7, "remapOutMax": 1.35}),
        _route("audio.treble", "particles/tree-fireflies/emissive", 1.0, "multiply",
               {"attackMs": 30.0, "decayMs": 260.0, "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0,
                "remapOutMin": 0.75, "remapOutMax": 1.3}),
        # The crash: one flash of light on the drop's downbeat. v2: it was held 90 ms and fell at 2.2 a
        # second, so the first second of the drop read as an overexposed shot rather than a flash; now
        # it is gone in about a third of a second.
        _route("timeline.crash", "camera/exposure/compensation", 2.0, "add",
               {"envelope": "peakhold", "envelopeHoldMs": 60.0, "envelopeFallPerSecond": 6.0}),
        _route("timeline.crash", "post/bloom/intensity", 2.5, "add",
               {"envelope": "peakhold", "envelopeHoldMs": 80.0, "envelopeFallPerSecond": 3.5}),
        # The kick gaps: on beat 4 of bars 24, 40, 56 and 72 the kick drops out and the valley holds
        # its breath -- its glow dips for that beat and comes back on the next downbeat.
        _route("timeline.gap", "material/glowmere2Cap/emissionIntensity", 1.0, "multiply",
               # peak-hold the pulse itself, then invert it with the remap, which runs last in the
               # chain: gain and offset run first, and a peak-hold of an inverted pulse holds its 1.0.
               {"envelope": "peakhold", "envelopeHoldMs": 330.0, "envelopeFallPerSecond": 8.0,
                "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 1.0, "remapOutMax": 0.45}),
        _route("timeline.gap", "material/glowmereTissue/emissionIntensity", 1.0, "multiply",
               # peak-hold the pulse itself, then invert it with the remap, which runs last in the
               # chain: gain and offset run first, and a peak-hold of an inverted pulse holds its 1.0.
               {"envelope": "peakhold", "envelopeHoldMs": 330.0, "envelopeFallPerSecond": 8.0,
                "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 1.0, "remapOutMax": 0.45}),
        # The river answers the bass, barely: a rougher surface on the pump, never a pulse.
        _route("audio.bass", "nodes/valley/water/ripple", 0.015, "add", {"attackMs": 90.0, "decayMs": 700.0}),
    ]
    project["routes"] = routes
    return len(routes)
