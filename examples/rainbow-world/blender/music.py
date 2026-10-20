"""Rainbow World 2: how the world hears the music (docs/prototypes/rainbow-world/16-effect-mapping.md).

Pure Python, no bpy. `build_world.py` calls `routes(...)` and writes them into the live project and the Feline Footwear
showcase. Three layers, as the brief's section 5.1 asks:

  1. raw features      the engine's signals: `stem.<key>.*` (ADR-1520) when a drum, bass or synth stem exists -- a
                       stem file offline, a mapped input channel live (ADR-1521) -- and the main mix's `response.*`
                       and `beat.*` otherwise;
  2. musical controls  a few macro knobs (`macros/<knob>`, published as `macro.<knob>`) that each stem's routes feed,
                       with that control's own attack and release: swell (bass), synth (the synth's light), lift
                       (the arrangement's slow rise), plus direct routes where one frame of macro latency would
                       show (the kick ring);
  3. targets           material-program constants, deformer amounts, light and post parameters.

Every stem route is `optional` and has a main-mix twin that stands aside while the stem is active (ADR-1570), so the
scene plays on a plain stereo input and follows stems wherever they exist. Nothing here reads a clock but the transport
(routes, `beat.*`, the material `time` input), so a seek lands where a play does.
"""

STEMS = ("drums", "bass", "synth")

# the kick ring's travel: radius = R0 + RING_REACH * beat.phase (metres). One beat is 0.496 s at Feline's 121 BPM, so
# the front runs at about 110 m/s and is gone (faded) before the next kick.
RING_R0 = 18.0
RING_REACH = 70.0


def route(source, target, amount, chain=None, component=-1, op="add", **extra):
    r = {"source": source, "target": target, "amount": round(float(amount), 5), "op": op, "polarity": "unipolar",
         "component": component}
    if chain:
        r["chain"] = chain
    r.update(extra)
    return r


def stem_route(stem, feature, fallback, target, amount, chain=None, fb_amount=None, fb_chain=None, component=-1,
               op="add"):
    """A route on `stem.<stem>.<feature>` (optional: silent when the stem does not exist) and its main-mix fallback on
    `fallback`, which plays at full depth with no such stem and at depth 0 while it is active (ADR-1570)."""
    out = [route("stem.%s.%s" % (stem, feature), target, amount, chain, component, op, optional=True)]
    if fallback:
        out.append(route(fallback, target, amount if fb_amount is None else fb_amount,
                         chain if fb_chain is None else fb_chain, component, op,
                         depthSource="stem.%s.active" % stem, depthMin=1.0, depthMax=0.0, depthDefault=0.0))
    return out


def op_target(program, roles, role, kind, field):
    return "material/%s/op/%d/%s/%s" % (program, roles[role], kind, field)


MACROS = [("swell", 0.0), ("synth", 0.0), ("lift", 0.0)]
RAINBOWS = ("rw_rainbow", "rw_rainbow2")


def routes(fx, swell_nodes, swell):
    """`fx`: look.FX (program -> role -> 1-based op index). `swell_nodes`: the nodes carrying the swell deformers.
    `swell`: look's SWELL list (its last field is metres at a full swell)."""
    out = []
    ring_programs = [p for p in fx if "ring_in" in fx[p]]

    # ---- drums: the kick ring --------------------------------------------------------------------------------------
    # radius from the beat clock (both edges of both clamps move together), so the front starts on the beat and runs
    # outward; brightness from the drum stem's low band, so a beat with no kick draws nothing.
    for prog in ring_programs:
        roles = fx[prog]
        for role, comp_bases in (("ring_in", (0, 1)), ("ring_out", (0, 1))):
            for c in comp_bases:
                out.append(route("beat.phase", op_target(prog, roles, role, "remap", "constant"), RING_REACH,
                                 component=c))
        # the front fades as it travels: x (1 -> 0.12) over the beat
        out.append(route("beat.phase", op_target(prog, roles, "ring_colour", "constant", "constant"), 1.0,
                         {"remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 1.0,
                          "remapOutMax": 0.12}, op="multiply"))
        gain = op_target(prog, roles, "ring_gain", "constant", "constant")
        out += stem_route("drums", "bass", "response.kickEnv", gain, 1.1,
                          {"gain": 3.0, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                           "attackMs": 4.0, "decayMs": 160.0},
                          fb_amount=1.1, fb_chain={"attackMs": 4.0, "decayMs": 120.0})

    # ---- drums: hat glints in the grass ----------------------------------------------------------------------------
    if "glint_gain" in fx.get("rw_grass", {}):
        roles = fx["rw_grass"]
        glint = {"offset": -0.35, "gain": 1.6, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                 "attackMs": 3.0, "decayMs": 110.0}
        out += stem_route("drums", "treble", "response.hatEnv", op_target("rw_grass", roles, "glint_gain", "constant",
                                                                         "constant"), 3.2, glint,
                          fb_chain={"attackMs": 3.0, "decayMs": 90.0})
        out += stem_route("drums", "treble", "response.hatEnv", op_target("rw_grass", roles, "glint_threshold",
                                                                         "threshold", "value"), -0.14, glint,
                          fb_chain={"attackMs": 3.0, "decayMs": 90.0})

    # ---- bass: the swell -------------------------------------------------------------------------------------------
    # the bass body (its followed level, conditioned so the stem's quiet floor reads 0) plus a push on each note
    out += stem_route("bass", "level", "response.bass", "macros/swell", 1.0,
                      {"gain": 1.6, "offset": -0.55, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 60.0, "decayMs": 500.0},
                      fb_chain={"gain": 1.6, "offset": -0.6, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                "attackMs": 60.0, "decayMs": 500.0})
    out += stem_route("bass", "onset", None, "macros/swell", 0.35,
                      {"envelope": "linearfall", "envelopeFallPerSecond": 3.0, "attackMs": 10.0})
    for nm in swell_nodes:
        for k, wave in enumerate(swell):
            out.append(route("macro.swell", "procedural/%s/deform/%d/amount" % (nm, k + 1), wave[-1]))
    out.append(route("macro.swell", "lights/cloudsea/intensity", 0.6))

    # ---- drums: the clap fronts on the arches ----------------------------------------------------------------------
    # both feet to the crown in one beat; bright only where the drum stem's 2-6 kHz band says a clap landed
    for prog in RAINBOWS:
        roles = fx.get(prog, {})
        if "front_gain" not in roles:
            continue
        for role, amt in (("front_l_in", 0.5), ("front_l_out", 0.5), ("front_r_in", -0.5), ("front_r_out", -0.5)):
            for c in (0, 1):
                out.append(route("beat.phase", op_target(prog, roles, role, "remap", "constant"), amt, component=c))
        out += stem_route("drums", "highMid", "response.snareEnv",
                          op_target(prog, roles, "front_gain", "constant", "constant"), 5.0,
                          {"offset": -1.55, "gain": 2.5, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                           "attackMs": 4.0, "decayMs": 280.0}, fb_chain={"attackMs": 4.0, "decayMs": 220.0})
        # ---- synth: the arches' flow and brightness ----------------------------------------------------------------
        out.append(route("macro.synth", op_target(prog, roles, "flow_gain", "constant", "constant"), 3.0))
        out.append(route("macro.synth", op_target(prog, roles, "arch_gain", "constant", "constant"), 1.2))

    # ---- synth: the musical control. Its followed level (the drop's bed sits at 0.47-0.71) conditioned and smoothed
    # over a beat and more, so the kick's sidechain pump does not ripple the light; each synth onset (the intro's
    # falling calls, chord changes) adds a swell that falls over 0.7 s.
    out += stem_route("synth", "level", "response.sustain", "macros/synth", 1.0,
                      {"gain": 2.2, "offset": -0.95, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 250.0, "decayMs": 1200.0},
                      fb_chain={"gain": 2.0, "offset": -0.75, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                "attackMs": 250.0, "decayMs": 1200.0})
    out += stem_route("synth", "onset", None, "macros/synth", 0.3,
                      {"envelope": "linearfall", "envelopeFallPerSecond": 1.4, "attackMs": 20.0})
    # ---- phrase: every 4 bars (beat.phrasePulse, bars 1 + 4k) the arches flare and the kick's ring is brighter; the
    # flare's depth is the synth (a phrase in the intro, before the bed, flares less)
    for prog in RAINBOWS:
        roles = fx.get(prog, {})
        if "arch_gain" in roles:
            out.append(route("beat.phrasePulse", op_target(prog, roles, "arch_gain", "constant", "constant"), 2.0,
                             {"envelope": "linearfall", "envelopeFallPerSecond": 0.6, "attackMs": 30.0},
                             depthSource="macro.synth", depthMin=0.35, depthMax=1.0))
            out.append(route("macro.lift", op_target(prog, roles, "arch_gain", "constant", "constant"), 1.0))
    for prog in ring_programs:
        out.append(route("beat.phrasePulse", op_target(prog, fx[prog], "ring_gain", "constant", "constant"), 1.0,
                         {"envelope": "linearfall", "envelopeFallPerSecond": 2.2}))
    # ---- lift: the synth's brightness over bars (its centroid; the 151-165 s rise, the riser at 183-190 s) lifts
    # the sun's glow, the bloom and the arches together, slowly
    out += stem_route("synth", "centroid", "audio.spectralCentroid", "macros/lift", 1.0,
                      {"gain": 6.0, "offset": -3.75, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 1500.0, "decayMs": 3000.0},
                      fb_chain={"gain": 4.0, "offset": -2.0, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                "attackMs": 1500.0, "decayMs": 3000.0})
    out.append(route("macro.lift", "env/sky/sunGlow", 0.35))
    out.append(route("macro.lift", "post/bloom/intensity", 0.12))
    # ---- the meadow wash: the synth's light reaching the land; a phrase and the drop flare it
    for prog in ring_programs:
        roles = fx[prog]
        if "wash_gain" not in roles:
            continue
        wash = op_target(prog, roles, "wash_gain", "constant", "constant")
        out.append(route("macro.synth", wash, 0.35))
        out.append(route("macro.lift", wash, 0.25))
        out.append(route("beat.phrasePulse", wash, 0.9, {"envelope": "linearfall", "envelopeFallPerSecond": 0.5,
                                                         "attackMs": 60.0},
                         depthSource="macro.synth", depthMin=0.2, depthMax=1.0))
    if "cloud_glow" in fx.get("rw_cloud", {}):
        out.append(route("macro.synth", op_target("rw_cloud", fx["rw_cloud"], "cloud_glow", "constant", "constant"),
                         0.3))
    return out
