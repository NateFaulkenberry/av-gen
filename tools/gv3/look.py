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
    # (v3-D against before-D, the same engine). So the second goes too (spectrum shape 0), and the
    # first pass's average look is put back as fixed values, calibrated on frames: at first (v4) the
    # filaments 0.95 -> 2.3 on the guess that the high band tripled their base on average, which left
    # the riff's sky 1.75x the first pass's (s04, the top 30%, the same intensity key) while the drop
    # matched; the auto-gained high band is lower than that outside the drop. Now filaments 1.3; the
    # curtain a little lower (2600 -> 2300 m), because a flat top stands taller than the first pass's
    # quieter bearings did; the folds and waves at the first pass's averages (turbulence 0.45 -> 0.68,
    # wave amplitude 0.30 -> 0.38). The glints (sparkle) are the one term the "audio response" does not
    # switch off -- they follow the high band frame by frame, +5% on the off-beats in s04 -- so they
    # are halved (0.5 -> 0.3). The aurora answers the lead instead, slowly (the proposal's
    # `lead.aurora`, x0.94-1.15 over 0.4-1.6 s), the arc keys it with the sections, and its own flow
    # keeps it moving.
    "fx/aurora/audioBeat": 0.0,
    "fx/aurora/audioSensitivity": 0.0,
    "fx/aurora/spectrumShape": 0.0,
    "fx/aurora/filaments": 1.3,
    # engine-6 (ADR-938/939) holds the glints steady instead of flickering with the high band, so their
    # average brightness fell; 0.85 keeps the old average. The effect's own block is set to the same
    # value (AURORA_SPARKLE, apply_base), so the Effects panel and the Parameters panel agree.
    "fx/aurora/sparkle": 0.85,
    "fx/aurora/curtainHeight": 2300.0,
    "fx/aurora/turbulence": 0.68,
    "fx/aurora/waveAmplitude": 0.38,
    # The drummer's gold as the saucer lifts him (E5; the horse's light before the art pass, musicians.py
    # LIFT_GLOW). Its cues -- self-glow 3 and rim 5, raised on the lift -- peak far too bright on a white body
    # (25.7% of the frame clipped on the horse), and no cue keys the Glow's brightness, so halving it halves
    # the peak and keeps the lift: when it rises, how long it holds, its colour.
    "fx/drummer-light/gain": 0.5,
    # One moon. The sky's rotation is the HDRI's, and GV3 has no HDRI: the source (GV2) restated -0.568 rad
    # over the scene's 0. The visible procedural sky is looked up through it (sky_background.wgsl,
    # envRotate), but the skybox's crisp moon is drawn at the sky's own sun direction, un-rotated
    # (skybox.wgsl), so the film had two moons 32.5 deg apart: both in frame in 17.1, 59.1 and 93.1, the soft
    # one alone in seven more shots -- 123 px behind the rising horse in 95.3 (gv3-int r1, moon.py). At 0 the
    # soft disc and its glow sit under the crisp one. What else reads the rotation (the water's and glossy
    # surfaces' reflections of the sky: water.wgsl, pbr_shade.wgsl) reads the same sky, so its moon moves
    # with it; GV3 has no HDRI and `env/lightFromEnvironment` is off, so nothing is lit differently.
    "env/rotation": 0.0,
}

# Effects the film does not use, and why.
REMOVED_EFFECTS = {
    # The owner: the hero pulse on the ten hero mushrooms and the five aliens, "NOT the UFO".
    "visitor-hero-pulse": "the saucer is no hero of the ground; the owner wants the pulse on the mushrooms and aliens only",
}

# The Camera Travel Beam (ADR-207, ADR-702), restored at the owner's request after r1: "the world effect
# that we used to have, the light pulse that swept across the world during camera changes". The first
# pass removed it as "a repeating violet streak across the sky every 9 s": GV2 fired it "always", every
# 9.24 s, whatever the cut did. GV3 fires it on the camera changes themselves. Its own activation for
# that, `cameraTravel`, reads Song-mode shot spans, which an authored cut does not have (an engine gap,
# recorded), so it fires on a trigger instead: a cue marker named TRAVEL_BEAM_MARKER at each cut it
# answers, which the Sequence panel shows and an artist can add, move or delete. It answers the cuts
# that open a four-bar phrase (every 7.4 s or so), not the dark stretch from the suspension through the
# riser, not the drop's crash (the drop's own ring and the flash carry it), not the two set-piece lifts
# it would cross, and not the tail.
TRAVEL_BEAM_ID = "camera-travel-beam"
TRAVEL_BEAM_MARKER = "travel beam"
TRAVEL_BEAM_SILENT = ("suspension", "submerged-break", "riser", "tail")
TRAVEL_BEAM_SKIP_BARS = (1, 37, 57, 97)   # the cold open's first frame, E3's and E4's lifts, the crash
TRAVEL_BEAM_TIMING = {"delay": 0.05, "fadeIn": 0.3, "fadeOut": 1.0, "lifetime": 3.2, "repeatSeconds": 0.0}
# GV2's look, kept, and tuned into GV3's palette: its violet (the fungi's) with a pale lilac edge, rainbow
# off (GV2's project had it on; the fungi and beacons already carry the colour), a little dimmer than GV2's
# 1.9 because GV3's night is darker, and GV2's kick in it, gentler (GV2 +8 on every beat pulse).
TRAVEL_BEAM_PARAMETERS = {
    "fx/camera-travel-beam/rainbow": False,
    "fx/camera-travel-beam/intensity": 1.4,
    "fx/camera-travel-beam/color": [0.58, 0.36, 1.0],
    "fx/camera-travel-beam/edgeColor": [0.96, 0.84, 1.0],
    "fx/camera-travel-beam/repeat": 0.0,
    "fx/camera-travel-beam/lifetime": TRAVEL_BEAM_TIMING["lifetime"],
    "fx/camera-travel-beam/delay": TRAVEL_BEAM_TIMING["delay"],
    "fx/camera-travel-beam/fadeIn": TRAVEL_BEAM_TIMING["fadeIn"],
    "fx/camera-travel-beam/fadeOut": TRAVEL_BEAM_TIMING["fadeOut"],
}
TRAVEL_BEAM_KICK = 2.0   # the scored kick on its intensity while it sweeps
# Its colour through the song (the owner: "tune its colour to the GV3 palette and the section arc"), keyed
# on the timeline at each segment's first cut, so the Timeline panel shows it and the Effects panel's
# colour is the value it starts from. Each is one of the valley's own lights, the edge a paler tint of
# it: at r2b the edge was near-white at intensity 3 and the band read as a white scan across the frame.
# The silent segments keep the last colour (the beam does not fire in them).
TRAVEL_BEAM_EDGE_INTENSITY = 2.0
TRAVEL_BEAM_ARC = {
    #  segment            colour                edge
    "cold-open": ((0.58, 0.36, 1.00), (0.80, 0.66, 1.00)),        # the small mushrooms' violet
    "groove-2": ((0.42, 0.40, 1.00), (0.68, 0.70, 1.00)),         # indigo, the night deepening
    "lift": ((0.07, 0.78, 1.00), (0.55, 0.92, 1.00)),             # the beacons' cyan, E3's column
    "arrival": ((0.08, 0.95, 0.70), (0.62, 1.00, 0.86)),          # the aurora's teal: the valley lit
    "melodic-plateau": ((1.00, 0.60, 0.22), (1.00, 0.84, 0.58)),  # the elder's gold
    "lead-forward": ((0.20, 1.00, 0.55), (0.70, 1.00, 0.82)),     # the aurora carries the lead
    "drop": ((1.00, 0.62, 0.30), (1.00, 0.86, 0.66)),             # the valley rebuilt, gold
}
REMOVED_EFFECT_TYPES = {}

# The Hero Pulse (GV2 multicam's `groundPulse` effects, one per hero: the ten mushrooms and the five
# aliens; the saucer's is removed, REMOVED_EFFECTS): a ring of light that swells out from the hero across the ground and up the
# plants round it. The owner asked for it several times ("the hero pulse that used to swell out from the
# mushroom and across the ground"); the first pass removed it because its activation, `heroFocus`, reads
# Song-mode shot spans, which GV3's authored cut does not have (the travel beam's gap). Until the engine
# reads an authored cut, each fires on a trigger instead: a cue marker "hero pulse <hero>" on every
# downbeat of a shot in which that hero is the subject or stands in the frame within HERO_PULSE_NEAR m.
# GV2's look is kept whole (colour, rings, speed, range, response, sparkle); only the timing changes, so
# the ring leaves the hero on the beat (no delay, a quick fade in) and runs its 58 m (4.5 s at 13 m/s),
# and GV2's pump (`beat.pulse -> intensity` +8) rides the scored kick. The markers are on the Sequence
# panel; each ring is the Effects panel's "Hero Pulse" on its hero.
HERO_PULSE_MARKER = "hero pulse {}"
HERO_PULSE_TIMING = {"delay": 0.0, "fadeIn": 0.15, "fadeOut": 1.2, "lifetime": 4.5, "repeatSeconds": 0.0}
HERO_PULSE_KICK = 8.0
HERO_PULSE_NEAR = 160.0     # metres: a hero farther than this in the frame does not pulse (the grand wides are 120-150 m)
HERO_PULSE_PER_SHOT = 2     # at most this many heroes pulse in one shot (the nearest)
# The art pass's performers (musicians.py) pulse beside the heroes, not instead of them: a performer's rings are
# local (34 m), so they fire when the performer is in frame within this, and they never take one of the two
# places the mushrooms and aliens have -- 18 m from the elder, they would otherwise have taken half its rings.
PERFORMER_PULSE_NEAR = 90.0
PERFORMER_PULSE_KICK = 1.5  # their rings' pump on the scored kick (a mushroom's is HERO_PULSE_KICK, +8)


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
    # The source's direction (0 blows toward +X, pi/2 toward +Z), which the water's tears are laid
    # out on: the seams sit on a lattice in the frame of the wind's steady direction (water.wgsl
    # tearAt), and the water stream placed them for this one -- a seam across the moon's glint by the
    # boat in s08 (3.98% of the frame), a combed band in s12. Iteration 1 turned the wind down the
    # valley (1.45, cold air draining downhill), which rotated the whole lattice about the origin:
    # s08's glint had no seam left in it at 1080p (v4). The tears must follow the wind and read where
    # they were placed, so the wind keeps the direction they were placed for.
    "direction": 0.62,
    # A front every 3.25 s (26 m apart at 8 m/s): quicker than the old 9.5 s, and not the two-bar
    # breath's 3.69 s, so the breath that lifts the gusts (the proposal's gust route) drifts across
    # the fronts instead of pinning strong gusts to fixed lines on the ground.
    "gustScale": 26.0, "gustSpeed": 8.0,
    "gustAmount": 0.9, "gustSharpness": 2.0,   # distinct fronts with a calm between them
    "turbulence": 0.45,                        # eddies turn the local direction; flutter scales with it
}
# Per-species response (VegetationMotion): how far a plant's tip travels at unit wind, as a fraction
# of its height. Grass, ferns and flowers carry the wind in the frame; the trees get a mass that lets
# them sway at about 0.2 Hz instead of resonating at 0.07 Hz, which read as a lean. Mushrooms and
# bushes keep the source's stiffness: a fungus that sways is wrong.
# The fan plants -- the big broad-leaved plants in the foreground of s04, s17, s34 and s36, the most
# visible vegetation in the film -- were effectively rigid: by wind.cpp's oscillator (static bend
# tip x sensitivity / stiffness, 0.4 of each gust at their 0.225 Hz resonance) a 2 m plant swung
# 0.9 cm in a gust and 0.6 cm in flutter, and the static s34 showed them still (iteration 2). Now
# about 4.6 cm and 2.8 cm at the same slow 0.225 Hz (stiffness and mass scaled together): a third of
# the ferns' 13 cm, the heavy leaves moving less and slower than the fronds around them.
MOTION = {
    "grass": {"tipAmplitude": 0.22},
    "ferns": {"tipAmplitude": 0.20},
    "flowers": {"tipAmplitude": 0.18},
    "fan-plants": {"tipAmplitude": 0.20, "windSensitivity": 0.9, "stiffness": 2.4, "mass": 1.2,
                   "gustResponse": 1.2},
    "canopy": {"mass": 6.0}, "canopy-broad": {"mass": 6.0}, "twisted": {"mass": 6.0},
    "twisted-low": {"mass": 6.0}, "pine-upper": {"mass": 9.0}, "pine-rim": {"mass": 9.0},
}


def _terrain(scene):
    return next(n for n in scene["nodes"] if n.get("kind") == "terrain")


# The render stream's values that serve the preview and the final alike (ADR-917-919,
# stream-reports/render.md "How GV3 should use this"). Post radii are counted in pixels of a
# 1080-row reference and scaled to the output, so a 960x540 x2 preview (1080 internal rows) is
# unchanged (0 of 518,400 pixels) and a 4K x2 final now matches it (mean difference 2.20 -> 1.36);
# the motion blur's reach and samples keep up with it; the fog takes the sky's radiance, the aurora
# included, so the distant rim reads as air rather than a dark cut-out (s14's far third 67 -> 81),
# its distance automatic (208-500 m over the density arc). The bloom's 6 levels, the anamorphic
# stretch 10.386 and the motion-blur tile 20 stay as they are: the audit's 8 levels, stretch x2 and
# tile 40 were for a resolution-dependent look and would now scale twice. The final-only values
# (4K, the offline tier) are gv3-world's, in offline.py.
#
# The air at half (gv3-int round 2, the fog A/B `vF`: ten windows stepped through fogSky 1.0 / 0.5 /
# 1.0 at 600 m / 0). At 1.0 the wides were a bright teal haze from the middle distance on (41.1's mean
# luma 0.292; the Critic's "bright areas away from the subject dominate") and the small violet mushrooms
# all but vanished into it (41.1: 17 violet pixels in the frame, 83 at 0.5, 158 at 0). At 0 the first
# pass's navy came back, and with it the river's mouth as a slot at the end of 109.1's crane and the
# elder's light as a hard trapezoid behind it in 71.1. At 0.5 the distance still fades into the aurora,
# the ends stay soft and the valley's own lights read again.
FOG_SKY = 0.5
RENDER_ALIKE = {
    "post/referenceHeight": 1080.0,
    "post/motionBlur/maxRadius": 60.0,
    "post/motionBlur/samples": 32,
    "scene/fogSky": FOG_SKY,
    "scene/fogSkyDistance": 0.0,
}
# The same, where the scene file keeps them (a value set in the project and not the scene is the one
# the project states; both are written so neither reverts the other).
RENDER_ALIKE_SCENE = {("post", "referenceHeight"): 1080.0, ("environment", "fogSky"): FOG_SKY,
                      ("environment", "fogSkyDistance"): 0.0}


def apply_render_alike(project, scene):
    project["parameters"].update(RENDER_ALIKE)
    for (block, key), value in RENDER_ALIKE_SCENE.items():
        scene.setdefault(block, {})[key] = value
    for key, param, value in GLOW_POOLS:
        scene.setdefault("environment", {})[key] = value
        project["parameters"][param] = value
    for e in project.get("effects", []):
        if e.get("id") == "aurora":
            e["parameters"]["appearance"]["sparkle"] = BASE["fx/aurora/sparkle"]


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
    apply_render_alike(project, scene)
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


def travel_beam_cuts(shots):
    """The cuts the travel beam answers: those opening a four-bar phrase (bar 5, 9, 13, ...), outside
    TRAVEL_BEAM_SILENT and TRAVEL_BEAM_SKIP_BARS. [(seconds, shot id, bar)]; the seconds are the cut's
    on-screen instant (a frame ahead of its beat, make_glowmere_valley_3.CUT_LEAD)."""
    out = []
    for i, s in enumerate(sorted(shots, key=lambda s: s.start)):
        if i == 0 or s.span is None or s.segment in TRAVEL_BEAM_SILENT:
            continue
        bar, _, beat = s.span.label.partition(".")
        if not bar.isdigit() or beat not in ("", "1"):
            continue
        b = int(bar)
        if (b - 1) % 4 or b in TRAVEL_BEAM_SKIP_BARS:
            continue
        out.append((s.start - 1.0 / 60.0, s.sid, b))
    return out


def apply_travel_beam(project, shots):
    """The Camera Travel Beam on the chosen cuts (see TRAVEL_BEAM_*): the effect set to fire on its
    markers, the markers written, its parameters and its kick. Returns the cuts it answers."""
    beam = next((e for e in project.get("effects", []) if e.get("id") == TRAVEL_BEAM_ID), None)
    if beam is None:
        raise RuntimeError(f"the source has no '{TRAVEL_BEAM_ID}' effect to restore")
    beam["activation"] = "trigger"
    beam["trigger"] = {"source": "marker", "name": TRAVEL_BEAM_MARKER}
    beam["timing"].update(TRAVEL_BEAM_TIMING)
    beam["parameters"]["appearance"]["rainbow"] = False
    project["parameters"].update(TRAVEL_BEAM_PARAMETERS)
    cuts = travel_beam_cuts(shots)
    seq = project.setdefault("sequence", {})
    markers = [m for m in seq.get("markers", []) if m.get("name") != TRAVEL_BEAM_MARKER]
    markers += [{"kind": "cue", "name": TRAVEL_BEAM_MARKER, "time": round(t, 6)} for t, _, _ in cuts]
    seq["markers"] = sorted(markers, key=lambda m: m["time"])
    project["routes"] = [r for r in project.get("routes", []) if r.get("target") != "fx/camera-travel-beam/intensity"]
    project["routes"].append(_route("timeline.kick", "fx/camera-travel-beam/intensity", TRAVEL_BEAM_KICK, "add",
                                    {"attackMs": 5.0, "decayMs": 240.0}))
    apply_travel_beam_arc(project, shots)
    return cuts


def apply_travel_beam_arc(project, shots):
    """The beam's colour and edge colour stepped at each TRAVEL_BEAM_ARC segment's first cut."""
    starts = {}
    for s in shots:
        starts[s.segment] = min(starts.get(s.segment, s.start), s.start)
    arc = sorted((starts[seg], colours) for seg, colours in TRAVEL_BEAM_ARC.items() if seg in starts)
    first = arc[0][1]
    project["parameters"]["fx/camera-travel-beam/color"] = list(first[0])
    project["parameters"]["fx/camera-travel-beam/edgeColor"] = list(first[1])
    project["parameters"]["fx/camera-travel-beam/edgeIntensity"] = TRAVEL_BEAM_EDGE_INTENSITY
    targets = ("fx/camera-travel-beam/color", "fx/camera-travel-beam/edgeColor")
    tracks = project["timeline"]["tracks"]
    tracks[:] = [t for t in tracks if t.get("target") not in targets]
    for k, target in enumerate(targets):
        keys = [{"time": 0.0, "value": list(first[k]), "interp": "step"}]
        keys += [{"time": round(t - 1.0 / 60.0, 6), "value": list(c[k]), "interp": "step"} for t, c in arc[1:]]
        tracks.append({"target": target, "component": -1, "timeBase": "seconds", "mode": "replace",
                       "loopLength": 0.0, "enabled": True, "keys": keys})


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
# x3 on engine-6 (ADR-945): the lights are chosen by what they add to the frame and each lights a pool
# on the ground, which only the violet fungi cast (GLOW_POOLS' faintest), so every pool pulses with the
# fungi's kick; the arc keys it section by section from this.
ECOLOGY_LIGHT = 4.2
# ADR-945's glow pools, in the scene's environment and restated as the project's parameters:
# (environment key, parameter, value). The farthest pool 300 m (the wides look 100-300 m).
GLOW_POOLS = [("ecologyLightRange", "scene/glow-pools/distance", 300.0),
              ("ecologyPoolFaintest", "scene/glow-pools/faintest", 6.5),
              ("ecologyPoolReach", "scene/glow-pools/reach", 6.0)]


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


# The aurora answers the bass (the owner, 2026-09-28: "the aurora should react to the audio, with a similar
# low-energy bass pulse"). Not the aurora's own per-frame response, which stays off (BASE: it read the analyser
# raw, every frame, and made the sky jump up to 44% between two frames on a kick): two smoothed routes in the
# style of the river's `audio.bass -> ripple`, onto what a viewer reads as the curtains breathing -- their
# brightness (a multiply beside the lead's `lead.aurora`, 1.0 at silence and AURORA_BASS_GAIN at a full bass)
# and a small lift of their tops. The attack is ten frames long, so no frame carries more than a sliver of a
# kick; the fall is slower than a bar's beat, so the swell rides the bass line rather than flickering on it.
AURORA_BASS_GAIN = 1.16    # the curtains' brightness at a full bass, over their lead-driven level
AURORA_BASS_LIFT = 140.0   # metres the curtain tops rise at a full bass, on 2300
AURORA_BASS_BRIGHT = {"attackMs": 160.0, "decayMs": 1100.0, "clampEnabled": True, "clampMin": 0.0,
                      "clampMax": 1.0, "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0,
                      "remapOutMin": 1.0, "remapOutMax": AURORA_BASS_GAIN}
AURORA_BASS_LIFTING = {"attackMs": 240.0, "decayMs": 1500.0, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}


def aurora_bass_routes():
    return [_route("audio.bass", "fx/aurora/intensity", 1.0, "multiply", dict(AURORA_BASS_BRIGHT)),
            _route("audio.bass", "fx/aurora/curtainHeight", AURORA_BASS_LIFT, "add", dict(AURORA_BASS_LIFTING))]


# An effect keeps its values twice: its block (the `parameters` the Effects panel builds it from) and its flat
# `fx/<id>/<field>` parameters, which are applied after the block and so are what renders. The QA pass's state
# audit found twelve that disagreed (docs/qa-pass/gv3-state-audit.md, item 1) -- the aurora's audio response 1.0
# in the block and 0 in what renders, its curtain height 2600 against 2300, among them -- and the owner asked
# for the aurora's to agree. They are resolved for every effect the same way: the block is written from the
# flat value that renders, last, after every look decision. The field -> block path tables are the registry's
# own (`aurora_effect.cpp`, `wave_rows.hpp`); a stored-value type (glow, space warp, ...) keeps the field's own
# name as its block key.
_WAVE_BLOCK = {
    "color": "appearance/color", "intensity": "appearance/intensity", "edgeColor": "appearance/edgeColor",
    "edgeIntensity": "appearance/edgeIntensity", "width": "appearance/width", "rainbow": "appearance/rainbow",
    "sparkle": "sparkle/enabled", "rainbowSpeed": "appearance/rainbowSpeed",
    "rainbowScale": "appearance/rainbowScale", "rainbowSaturation": "appearance/rainbowSaturation",
    "rainbowBrightness": "appearance/rainbowBrightness", "sparkleDensity": "sparkle/density",
    "sparkleSize": "sparkle/size", "sparkleIntensity": "sparkle/intensity", "sparkleSpeed": "sparkle/speed",
    "speed": "propagation/speed", "range": "propagation/range", "frontWidth": "propagation/frontWidth",
    "trailLength": "propagation/trailLength", "falloff": "propagation/falloff",
    "startOffset": "propagation/startOffset", "verticalExtent": "propagation/verticalExtent",
    "ringCount": "propagation/ringCount", "beamRadius": "propagation/beamRadius",
}
_AURORA_BLOCK = {
    "lowColor": "appearance/lowColor", "midColor": "appearance/midColor", "topColor": "appearance/topColor",
    "intensity": "appearance/intensity", "curtainHeight": "shape/curtainHeight", "curtains": "shape/curtainCount",
    "flowSpeed": "shape/flowSpeed", "audioSensitivity": "audio/sensitivity", "spectrumShape": "audio/spectrumShape",
    "rainbow": "rainbow/enabled", "radius": "shape/radius", "layerSpacing": "shape/layerSpacing",
    "baseHeight": "shape/baseHeight", "waveAmplitude": "shape/waveAmplitude", "waveScale": "shape/waveScale",
    "turbulence": "shape/turbulence", "complexity": "shape/complexity", "driftSpeed": "shape/driftSpeed",
    "verticalSpeed": "shape/verticalSpeed", "emission": "appearance/emission", "opacity": "appearance/opacity",
    "edgeBrightness": "appearance/edgeBrightness", "filaments": "appearance/filaments",
    "sparkle": "appearance/sparkle", "horizonGlow": "appearance/horizonGlow", "audioBass": "audio/bass",
    "audioLowMid": "audio/lowMid", "audioMid": "audio/mid", "audioHigh": "audio/high",
    "audioGlints": "audio/glints", "audioBeat": "audio/beat", "rainbowSpeed": "rainbow/speed",
    "rainbowScale": "rainbow/scale", "rainbowHue": "rainbow/hueOffset", "rainbowSaturation": "rainbow/saturation",
    "rainbowBrightness": "rainbow/brightness",
}
BLOCK_PATHS = {"aurora": _AURORA_BLOCK, "travelBeam": _WAVE_BLOCK, "groundPulse": _WAVE_BLOCK}


def _differs(a, b):
    if isinstance(a, list) and isinstance(b, list):
        return len(a) != len(b) or any(_differs(x, y) for x, y in zip(a, b))
    if isinstance(a, bool) or isinstance(b, bool):
        return a != b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(a - b) > 1e-5 * max(1.0, abs(b))
    return a != b


def sync_effect_blocks(project):
    """Write every effect block value that has a flat `fx/` parameter from that parameter (see above).
    Returns [(effect id, block path, was, now)] for what changed."""
    params = project["parameters"]
    changed = []
    for e in project.get("effects", []):
        block = e.get("parameters")
        if not isinstance(block, dict):
            continue
        table = BLOCK_PATHS.get(e.get("type"))
        items = table.items() if table else [(k, k) for k, v in block.items() if not isinstance(v, dict)]
        for name, path in items:
            key = f"fx/{e['id']}/{name}"
            if key not in params:
                continue
            node, parts = block, path.split("/")
            for part in parts[:-1]:
                node = node.get(part) if isinstance(node, dict) else None
            if not isinstance(node, dict) or parts[-1] not in node:
                continue
            if _differs(node[parts[-1]], params[key]):
                changed.append((e["id"], path, node[parts[-1]], params[key]))
                node[parts[-1]] = params[key]
    return changed


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
    section 1.4). None in the pull-back (bars 15-16), none in the break (89-92: the top is closed, and
    beats 2/4 are no brighter than 1/3 above 8 kHz), none under the riser's roll (93-96, which the
    fungi carry). The clap does play on the four kick-gap beats, but the picture leaves them out: that
    beat is the valley's held breath, every cap and small mushroom dimming together, the one moment
    it answers as one (apply_motifs), and a lantern flaring through it would break it.
    Measured on the song: above 8 kHz, beats 2/4 rise 5-19 dB more than 1/3 in every groove section.
    Scored, because the analyser's snare band (audio.onsetMid, 2-6 kHz) is the clap only where the
    groove is bare: it fires 1.0-1.1 times a second in the cold open, the riff, groove 2, the lead and
    the suspension (the claps' own 1.08), but 3.4-3.8 in the lift, the arrival, the plateau and the
    drop, where the 16th shakers and the mid synth play (the planner's section profiles), and 1.9-3.0
    in the break and the riser, where there is no clap at all. On that band the lantern flickered at
    3.6 Hz through the film's biggest sections instead of answering the clap."""
    silent = set(range(15, 17)) | set(range(89, 97))
    held = {round(t, 6) for t in music.KICK_GAPS}
    return [music.beat(b, n) for b in range(1, 123) if b not in silent for n in (2, 4)
            if music.beat(b, n) <= music.LAST_HIT + 1e-3 and round(music.beat(b, n), 6) not in held]


def lane(kind):
    """A drum lane the heroes and the small mushrooms answer (reactivity.HERO_LANES, SCATTER_PULSES),
    taken from the kicks the track plays, so every lane is silent where the drums are -- the pull-back,
    the four held beats -- and at half strength in the muffled break: `downbeat` beat 1 of each bar,
    `beat3` beat 3, `offbeat` the "and" after beats 2 and 4 (the hats' off-beats)."""
    out = []
    for t, strength in music.kicks():
        n = int(round((t - music.FIRST_DOWNBEAT) / music.BEAT)) % 4
        if kind == "downbeat" and n == 0:
            out.append((t, strength))
        elif kind == "beat3" and n == 2:
            out.append((t, strength))
        elif kind == "offbeat" and n in (1, 3):
            out.append((t + 0.5 * music.BEAT, strength))
    return out


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
        # The heroes' and the small mushrooms' other drum lanes (reactivity.HERO_LANES): the valley
        # answers the drums across the frame, each hero on its own hit.
        _pulses("downbeat", lane("downbeat")),
        _pulses("beat3", lane("beat3")),
        _pulses("offbeat", lane("offbeat")),
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
    # After the proposal, not before it: the proposer leaves alone a target something already routes, so a
    # bass route authored first would cost the aurora the lead's slow response (`lead.aurora`), which the
    # owner asked to keep. Both now ride the aurora's brightness, the lead slowly and the bass as a swell.
    project["routes"] += aurora_bass_routes()
    return len(project["routes"])


def _hero_positions(scene):
    from .musicians import GROUPS
    performers = {g["name"] for g in GROUPS}
    out = {}
    for n in scene["nodes"]:
        if n.get("name", "").endswith("-cap") and n.get("kind") == "procedural":
            out[n["name"]] = n["position"]
        elif n.get("name") in performers:  # the art pass's performers pulse wherever they are in frame, too
            out[n["name"]] = n["position"]
    return out


def pulse_blackouts():
    """{hero: (from, to)}: seconds in which a hero's pulse never fires. The abduction (E5, the beam on bar 93 to the
    drop on bar 97) keeps the rings it had: the art pass's two new mushrooms and the performers stay out of it,
    so no new ring crosses the beam. The drummer's lasts until the set piece puts him back behind his kit
    (ufo.plan.json's `returnSeconds` after the beam goes out, ADR-984): no ring from an empty seat."""
    import json
    import pathlib
    plan = json.loads((pathlib.Path(__file__).resolve().parent / "ufo.plan.json").read_text())
    e5 = next(p for p in plan["setPieces"] if p["key"] == "e5-centrepiece")
    back = float(e5.get("set", {}).get("returnSeconds", 0.0))
    lift = (music.bar(93) - 0.05, music.bar(97) + 0.05)
    out = {name: [lift] for name in ("opal-cap", "sail-cap", "keyboardist", "drummer")}
    out["drummer"] = [(lift[0], music.bar(97) + back + 0.2)]
    # The cold open (s01, to bar 5) is the elder's nocturne: the performers play in it, lit only by their
    # suits, and their rings start with the riff.
    for name in ("keyboardist", "drummer"):
        out[name].append((-1.0, music.bar(5) - 0.05))
    return out


def hero_pulse_plan(project, scene, shots):
    """[(time, hero)]: the downbeats on which each hero's pulse fires (see HERO_PULSE_*)."""
    from .framing import keyed, project as proj
    from .musicians import GROUPS
    performers = {g["name"] for g in GROUPS}
    blackout = pulse_blackouts()
    effects = [e for e in project.get("effects", []) if e.get("type") == "groundPulse"]
    owners = {e["owner"]["name"] for e in effects}
    caps = _hero_positions(scene)
    downbeats = [t - LEAD_SECONDS for t, _ in lane("downbeat")]
    plan = []
    for s in shots:
        rig = s.rig
        who = {}
        for node in (rig.follow, rig.aim):
            if node in owners:
                who[node] = 0.0
        if not rig.follow:
            mid = 0.5 * (s.start + s.end)
            eye = keyed(rig, "position", mid, rig.position)
            aim = keyed(rig, "target", mid, rig.target)
            if rig.aim in caps:
                aim = caps[rig.aim]
            for name, pos in caps.items():
                if name not in owners:
                    continue
                p, z = proj(eye, aim, rig.focal, [pos[0], pos[1] + 2.0, pos[2]])
                near = PERFORMER_PULSE_NEAR if name in performers else HERO_PULSE_NEAR
                if p is not None and abs(p[0]) <= 0.95 and abs(p[1]) <= 0.95 and z <= near:
                    who[name] = min(who.get(name, z), z)
        for t in downbeats:
            if s.start - 0.02 <= t < s.end - 0.3:
                # The nearest heroes in frame that may pulse now (a blacked-out one gives up its place), and
                # the performers in frame beside them.
                live = [n for n in who if not any(a <= t <= b for a, b in blackout.get(n, ()))]
                heroes = [n for n in live if n not in performers]
                plan += [(t, n) for n in sorted(heroes, key=lambda n: who[n])[:HERO_PULSE_PER_SHOT]]
                plan += [(t, n) for n in sorted(n for n in live if n in performers)]
    return plan


def apply_hero_pulses(project, scene, shots):
    """GV2's Hero Pulse on GV3's authored cut: each effect on its markers, the markers, the kick's pump."""
    effects = [e for e in project.get("effects", []) if e.get("type") == "groundPulse"]
    for e in effects:
        e["activation"] = "trigger"
        e["trigger"] = {"source": "marker", "name": HERO_PULSE_MARKER.format(e["owner"]["name"])}
        e["timing"].update(HERO_PULSE_TIMING)
        for k, v in HERO_PULSE_TIMING.items():
            if k != "repeatSeconds":
                project["parameters"][f"fx/{e['id']}/{k}"] = v
        project["parameters"][f"fx/{e['id']}/repeat"] = 0.0
    plan = hero_pulse_plan(project, scene, shots)
    seq = project.setdefault("sequence", {})
    names = {HERO_PULSE_MARKER.format(e["owner"]["name"]) for e in effects}
    markers = [m for m in seq.get("markers", []) if m.get("name") not in names]
    markers += [{"kind": "cue", "name": HERO_PULSE_MARKER.format(n), "time": round(t, 6)} for t, n in plan]
    seq["markers"] = sorted(markers, key=lambda m: m["time"])
    from .musicians import GROUPS
    performers = {f"fx/{g['name']}-hero-pulse/intensity" for g in GROUPS}
    targets = {f"fx/{e['id']}/intensity" for e in effects}
    project["routes"] = [r for r in project.get("routes", []) if r.get("target") not in targets]
    for t in sorted(targets):
        pump = PERFORMER_PULSE_KICK if t in performers else HERO_PULSE_KICK
        project["routes"].append(_route("timeline.kick", t, pump, "add", {"attackMs": 5.0, "decayMs": 200.0}))
    return plan
