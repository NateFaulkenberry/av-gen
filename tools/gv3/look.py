"""The look of Glowmere Valley 3: the base image, the arc across the film, and the modulation.

Three layers, deliberately kept apart because they answer different questions:

  BASE       what the valley looks like -- the lens, the grade, the water, the haze, the air. Static
             parameters, plus the scene's water and wind blocks. This is where the diorama problem is
             fought (docs/glowmere-valley-3/02-art-direction.md): no depth of field on a wide, haze that
             grows with distance, a lens that does not bend the world into a fishbowl.
  ARC        how the look travels with the music, segment by segment: exposure, saturation, fog,
             the valley's own light, the aurora. Timeline keys on the true grid, written from the
             directive table (directives.py) -- a slow envelope, never a beat. The grade stays inside
             the range the film establishes before the drop (the owner: no post-riser style shift).
  MOTIFS     what reacts to what. A few events stay authored here -- the crash's flash, the valley's
             held breath on the four kick gaps, the river's surface on the bass -- and everything
             else is the Director's reactivity proposal, edited and installed by reactivity.py: each
             hero on its own layer of the music, the small mushrooms on the hats and the kick, waves
             of light from the elder on the bar, the wind and the colour on the sections.
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
    # The first pass's final then doubled the frequency and halved the amplitude (F37), because the
    # shader faded ripples by the frame's own pixels; since ADR-915 it counts 1080-row reference
    # pixels, so one setting serves the previews and the finals (they agree within 3-4%), and the
    # final-only values would now smooth the river 2.4x in both (the water stream's measurement).
    "nodes/valley/water/glow": 0.12,
    "nodes/valley/water/rippleScale": 2.6,
    "nodes/valley/water/ripple": 0.1,
    "nodes/valley/water/reflection": 1.0,
    "nodes/valley/water/fresnel": 0.14,
    "nodes/valley/water/roughness": 0.2,
    "nodes/valley/water/sparkle": 0.06,
    "nodes/valley/water/foam": 0.06,
    "nodes/valley/water/shallowColor": [0.03, 0.1, 0.12],
    # The aurora: a steady curtain that answers the music slowly, never frame by frame.
    # Two things read the audio every frame, unsmoothed, inside the aurora's shader
    # (atmosphere_fx.wgsl): the "audio response" terms (bass -> the curtain's lift, low-mid -> waves,
    # mid -> folds, high -> filaments, from the analyser's per-frame bands) and the "spectrum shape"
    # (each bearing's top follows one bin of the raw per-frame spectrum, engine.cpp
    # updateAuroraSpectrum). Together they made the sky jump on every kick: up to 44% between two
    # frames over the arrival's grand wide, on the beat at 76.634 s; +16% to +61% on the beat in the
    # drop (the top fifth of s33 and s36, beat-locked, first pass).
    # Iteration 2 switched off only the first ("audio response" 0). Measured in the drop, that left
    # the kick in the sky (+12% to +16% on the beat, v3) and took away the curtain's brightness and
    # height with it: the sky's mean over the drop fell by 25-57% and the aurora all but vanished
    # (v3-D against before-D, the same engine). So the second goes too, and the average the first
    # pass's audio terms gave is put back as fixed values: the spectrum held a top at about 0.8 of
    # the curtain and the bass lifted it about 1.25x, so a flat top (spectrum shape 0) stands as tall;
    # the high band tripled the filaments' base 0.4 on average (0.4 + 1.6 x 0.7 x ~0.5), so the
    # filaments go 0.95 -> 2.3; the mid band's folds and the low-mid's waves likewise (turbulence
    # 0.45 -> 0.68, wave amplitude 0.30 -> 0.38). The aurora answers the lead instead, slowly (the
    # proposal's `lead.aurora`, x0.94-1.15 over 0.4-1.6 s), the arc keys it with the sections, and its
    # own flow keeps it moving.
    "fx/aurora/audioBeat": 0.0,
    "fx/aurora/audioSensitivity": 0.0,
    "fx/aurora/spectrumShape": 0.0,
    "fx/aurora/filaments": 2.3,
    "fx/aurora/turbulence": 0.68,
    "fx/aurora/waveAmplitude": 0.38,
}

# Effects the film does not use, and why.
REMOVED_EFFECTS = {
    "camera-travel-beam": "a repeating violet streak across the sky every 9 s -- effect soup",
}
REMOVED_EFFECT_TYPES = {
    "groundPulse": "fired only while the Song-mode shot spans spotlit a hero; the cut is authored now",
}


# The valley's water block (the terrain's `water`, ADR-099), beyond what the project's parameters set.
WATER = {
    # Deliberate tears (ADR-916, the owner's reference image): thin stepped seams in which the ripples
    # are packed into dense parallel stripes, on a rigid lattice that runs along the wind, drifts
    # slowly, and tightens where a gust crosses it -- so the seams move with the grass on the bank.
    # The water stream's values for GV3. The step is 3.2 m, not the reference's 1.2 m, because GV3's
    # cameras are tens of metres from the water and at 1.2 m the stripes fade to slick lines. Coverage
    # keeps most of the seam network out: the surface between seams is untouched.
    "tears": 0.35, "tearShear": 6.4, "tearCoverage": 0.8, "tearCell": 3.2, "tearSpacing": 20.0,
    "tearStretch": 4.0, "tearDirection": "wind", "tearDrift": 0.15, "tearWind": 0.6,
    # The shoreline fade. The source wrote it as "shoreFade", which nothing reads, so the banks used
    # the default 0.8 m; 1.6 m is what was meant: the surface thins out over the last 1.6 m of depth.
    "edgeFade": 1.6,
}
WATER_DEAD_KEYS = ("shoreFade",)

# The air (ADR-055). The source's wind reached every plant, but as a 7-20 cm static lean with gusts
# every 9.5 s and a centimetre of flutter: shape, not motion (the audit's replica of wind.cpp). The
# brief asks for subtle, continuous motion with a direction, gusts and variation, and no storm.
WIND = {
    # Down the valley, the way the river runs (the corridor runs along +Z, north to south): a night
    # valley drains its cold air downhill. 0 blows toward +X, pi/2 toward +Z.
    "direction": 1.45,
    # A front every 3.25 s (26 m apart at 8 m/s): quicker than the old 9.5 s, and not the two-bar
    # breath's 3.69 s, so the breath that lifts the gusts (the proposal's gust route) drifts across
    # the fronts instead of pinning strong gusts to fixed lines on the ground.
    "gustScale": 26.0, "gustSpeed": 8.0,
    "gustAmount": 0.9, "gustSharpness": 2.0,   # distinct fronts with a calm between them
    "turbulence": 0.45,                        # eddies turn the local direction; flutter scales with it
}
# Per-species response (VegetationMotion): how far a plant's tip travels at unit wind, as a fraction
# of its height. Grass, ferns and flowers carry the wind in the frame; the trees get a mass that lets
# them sway at about 0.2 Hz instead of resonating at 0.07 Hz, which read as a lean. Mushrooms, bushes
# and fan plants keep the source's stiffness: a fungus that sways is wrong.
MOTION = {
    "grass": {"tipAmplitude": 0.22},
    "ferns": {"tipAmplitude": 0.20},
    "flowers": {"tipAmplitude": 0.18},
    "canopy": {"mass": 6.0}, "canopy-broad": {"mass": 6.0}, "twisted": {"mass": 6.0},
    "twisted-low": {"mass": 6.0}, "pine-upper": {"mass": 9.0}, "pine-rim": {"mass": 9.0},
}


def _terrain(scene):
    return next(n for n in scene["nodes"] if n.get("kind") == "terrain")


def apply_water(scene):
    water = _terrain(scene)["terrain"]["water"]
    for key in WATER_DEAD_KEYS:
        water.pop(key, None)
    water.update(WATER)


def apply_wind(project, scene):
    """The scene's wind block and the project's restatement of it, which overrides the scene
    (ADR-264): set in one and not the other, a value silently reverts."""
    scene["wind"].update(WIND)
    params = project["parameters"]
    for key, value in WIND.items():
        path = "scene/windDirection" if key == "direction" else f"scene/wind/{key}"
        params[path] = value
    for layer in _terrain(scene)["scatter"]:
        if layer.get("name") in MOTION:
            layer["motion"].update(MOTION[layer["name"]])


def apply_base(project, scene):
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
    # Parameters the engine no longer has (it says so at load): the programs' intensities that
    # multiply nothing (ADR-905: the fireflies' glow is a layer's, the painted ground emits nowhere),
    # and an op the tissue program lost.
    for key in [k for k in params if k in DEAD_PARAMETERS or k.startswith(DEAD_PREFIXES)]:
        del params[key]
    apply_water(scene)
    apply_wind(project, scene)


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


# Parameters the engine does not have, which the source project carries and the load reports.
DEAD_PARAMETERS = {
    "material/glowmereFirefliesCrown/emissionIntensity",   # its glow is its layer's (ADR-905)
    "material/glowmereFirefliesScaled/emissionIntensity",  # likewise
    "material/paintedGround2/emissionIntensity",           # the painted ground writes no emission
}
DEAD_PREFIXES = ("material/glowmereTissue/op/9/input/",)   # an op the tissue program no longer has
# Glows that live in a program's layer, at the material files' own intensity. The first pass keyed the
# fireflies' programs' base intensity instead, which multiplies nothing: the crowns never followed
# the valley's light (the route audit's three dead arcs).
LAYER_LIGHTS = {
    "material/glowmereFirefliesCrown/layer/1/fireflies/emissionIntensity": 16.0,
    "material/glowmereFirefliesScaled/layer/1/fireflies/emissionIntensity": 16.0,
}
# The light the glowing plants and fungi cast on what is around them (the scene's `ecologyLight`, a
# parameter since ADR-905): it follows the glow it comes from, so the ground dims with the mushrooms.
ECOLOGY_LIGHT = 1.4


def _light_targets(params):
    """The valley's own light: every bioluminescent material, the crowns' fireflies, the light the
    glowing layers cast, the water's glow, the heroes' spores."""
    out = {k: params[k] for k in params if k.startswith("material/") and k.endswith("/emissionIntensity")}
    out.update(LAYER_LIGHTS)
    out["scene/ecologyLight"] = ECOLOGY_LIGHT
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
# The film's reactivity is the Director's proposal (reactivity.py): one owner per musical layer, each
# hero on its own, the small mushrooms on the fast layers, the world on the sections. What stays
# authored here is what the score itself writes and a detector would place wrong: the crash, the four
# kick gaps, the kicks the elder's heartbeat keeps and the claps the lantern answers (the proposal's
# elder and lantern routes are pointed at them), and the riser's roll.
#
# Every visual accent is led by LEAD_SECONDS: viewers forgive a picture a little late far more than
# one late by a frame, and animators hit early for the same reason (research principle 17).
LEAD_SECONDS = 0.02


def _route(source, target, amount, op="add", chain=None, polarity="unipolar"):
    return {"source": source, "target": target, "component": -1, "amount": amount, "op": op,
            "polarity": polarity, "enabled": True, "chain": chain or {}}


def _pulses(name, times, width=1.0 / 60.0):
    """A scored timeline source: a hit at each time, an event the film's own score writes where the
    analyser's would land wherever its detector decided. A time may be a (seconds, value) pair for a
    hit of another strength. Event mode (ADR-900): each key above zero fires once on the frame whose
    interval holds it, so no hit falls between frames at any frame rate (the value pulses of the first
    pass missed 109 of the 475 kicks at 30 fps). The keys are still written as step pulses, which read
    the same in either mode."""
    keys = [{"time": 0.0, "value": 0.0, "interp": "step"}]
    for item in sorted(times):
        t, value = item if isinstance(item, tuple) else (item, 1.0)
        t = t - LEAD_SECONDS
        keys.append({"time": round(t, 6), "value": value, "interp": "step"})
        keys.append({"time": round(t + width, 6), "value": 0.0, "interp": "step"})
    return {"kind": "timeline", "name": name, "settings": {"loopLength": 0.0, "mode": "event", "keys": keys}}


def claps():
    """The clap, as the track plays it: beats 2 and 4 of every bar with the groove (01-music.md
    section 1.4), the four kick-gap beats included -- the kick drops out there, the clap does not.
    None in the pull-back (bars 15-16), none in the break (89-92: the top is closed, and beats 2/4 are
    no brighter than 1/3 above 8 kHz), none under the riser's roll (93-96, which the fungi carry).
    Measured on the song: above 8 kHz, beats 2/4 rise 5-19 dB more than 1/3 in every groove section.
    Scored, because the analyser's snare band (audio.onsetMid, 2-6 kHz) is the clap only where the
    groove is bare: it fires 1.0-1.1 times a second in the cold open, the riff, groove 2, the lead and
    the suspension (the claps' own 1.08), but 3.4-3.8 in the lift, the arrival, the plateau and the
    drop, where the 16th shakers and the mid synth play (the planner's section profiles), and 1.9-3.0
    in the break and the riser, where there is no clap at all. On that band the lantern flickered at
    3.6 Hz through the film's biggest sections instead of answering the clap."""
    silent = set(range(15, 17)) | set(range(89, 97))
    return [music.beat(b, n) for b in range(1, 123) if b not in silent for n in (2, 4)
            if music.beat(b, n) <= music.LAST_HIT + 1e-3]


def roll():
    """The roll into the drop, as the track plays it (01-music.md section 1.4): 8ths through bars 93-94,
    16ths through 95 and the first three beats of 96, about 32nds on its last beat. The analyser's low
    onsets do not hold it (480 of them, the 475 kicks and five more), so it is scored like the kicks."""
    hits = [music.beat(b, 1 + k / 2) for b in (93, 94) for k in range(8)]
    hits += [music.beat(95, 1 + k / 4) for k in range(16)] + [music.beat(96, 1 + k / 4) for k in range(12)]
    hits += [music.beat(96, 4 + k / 8) for k in range(8)]
    return hits


def apply_motifs(project, scene):
    """The authored events, then the Director's proposal on top of them (reactivity.py). Returns the
    number of routes the film carries."""
    from . import reactivity
    params = project["parameters"]
    sources = [
        _pulses("crash", [music.bar(97)]),
        _pulses("gap", music.KICK_GAPS),
        # Every kick the track plays, as scored (music.kicks): the elder's heartbeat.
        _pulses("kick", music.kicks(), width=1.5 / 60.0),
        # Every clap, as scored: the lantern's flare and the beacons' echo (reactivity.EDITS).
        _pulses("clap", claps()),
        # The riser's roll: the small fungi flicker with it, faster and faster into the drop.
        _pulses("roll", roll()),
    ]
    for key in [k for k in params if k.startswith("sources/breath/")]:
        del params[key]  # the first pass's free-running breath: the proposal's is beat-synced
    params.update({
        # The drop's one camera shake: the impact of the crash felt in the frame, 6 cm and a third of a
        # degree, gone in 0.9 s. The only shake in the film (the research: a shake on every beat is
        # the thing the camera vocabulary refused). Its origin is a parameter, not a timer, so a seek
        # lands on the same frame (ADR-098).
        "camera/shake/start": music.bar(97) - LEAD_SECONDS, "camera/shake/amplitude": 0.06,
        "camera/shake/frequency": 7.0, "camera/shake/decay": 0.9, "camera/shake/rotation": 0.35,
    })
    project["sources"] = [s for s in project.get("sources", []) if s.get("kind") == "control"] + sources

    routes = [
        # The crash: one flash of light on the drop's downbeat, gone in about a third of a second (v2:
        # held 90 ms and falling at 2.2 a second, the drop's first second read as an overexposed shot).
        # An event, not a style: the grade itself holds through the drop (directives.py).
        _route("timeline.crash", "camera/exposure/compensation", 2.0, "add",
               {"envelope": "peakhold", "envelopeHoldMs": 60.0, "envelopeFallPerSecond": 6.0}),
        _route("timeline.crash", "post/bloom/intensity", 2.5, "add",
               {"envelope": "peakhold", "envelopeHoldMs": 80.0, "envelopeFallPerSecond": 3.5}),
        # The kick gaps: on beat 4 of bars 24, 40, 56 and 72 the kick drops out and the valley holds
        # its breath -- every cap and every small mushroom dips for that beat together, the one moment
        # the whole valley answers as one, four times in the film.
        _route("timeline.gap", "material/glowmere2Cap/emissionIntensity", 1.0, "multiply",
               # peak-hold the pulse itself, then invert it with the remap, which runs last in the
               # chain: gain and offset run first, and a peak-hold of an inverted pulse holds its 1.0.
               {"envelope": "peakhold", "envelopeHoldMs": 330.0, "envelopeFallPerSecond": 8.0,
                "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 1.0, "remapOutMax": 0.45}),
        _route("timeline.gap", "material/glowmereTissue/emissionIntensity", 1.0, "multiply",
               {"envelope": "peakhold", "envelopeHoldMs": 330.0, "envelopeFallPerSecond": 8.0,
                "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 1.0, "remapOutMax": 0.45}),
        # The river answers the bass, barely: a rougher surface on the pump, never a pulse. 0.03 on
        # ripples of 0.1 (ADR-915; the final-only 0.015 went with the final-only ripple).
        _route("audio.bass", "nodes/valley/water/ripple", 0.03, "add", {"attackMs": 90.0, "decayMs": 700.0}),
    ]
    project["routes"] = routes
    reactivity.prepare(project, scene)
    print("  " + reactivity.apply(project, scene))
    return len(project["routes"])
