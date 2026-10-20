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


MACROS = [("swell", 0.0), ("synth", 0.0), ("lift", 0.0), ("surge", 0.0), ("scantrig", 0.0), ("scan", 0.0), ("one", 1.0)]
SCAN_REST = 980.0    # look.SCAN_REST
SCAN_DELAY_MS = 300.0
RAINBOWS = ("rw_rainbow", "rw_rainbow2")


def routes(fx, swell_nodes, swell):
    """`fx`: look.FX (program -> role -> 1-based op index). `swell_nodes`: the nodes carrying the swell deformers.
    `swell`: look's SWELL list (its last field is metres at a full swell)."""
    out = []
    scan_programs = [p for p in fx if "scan_mask" in fx[p]]

    # ---- drums: the kick, locally (the owner at gate 2: no world sweep on every quarter note) ----------------------
    # the grass round the camera pops its tips on each kick: the drum stem's low band, fast attack, short decay
    if "kick_gain" in fx.get("rw_grass", {}):
        out += stem_route("drums", "bass", "response.kickEnv",
                          op_target("rw_grass", fx["rw_grass"], "kick_gain", "constant", "constant"), 2.2,
                          {"gain": 3.0, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                           "attackMs": 4.0, "decayMs": 140.0},
                          fb_chain={"attackMs": 4.0, "decayMs": 110.0})

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
    # the bass as colour energy (19-brief-color-vfx: "bass energy increases the intensity of selected terrain
    # colours"): the meadow's saturation and value rise with the bass body
    for prog in [p_ for p_ in fx if "bass_sat" in fx[p_]]:
        out.append(route("macro.swell", op_target(prog, fx[prog], "bass_sat", "saturate", "value"), 0.3))
        out.append(route("macro.swell", op_target(prog, fx[prog], "bass_gain", "constant", "constant"), 0.1))
    # shimmer across the flower fields: the synth's light, lifted on the hats
    if "shimmer_gain" in fx.get("rw_flower", {}):
        sh = op_target("rw_flower", fx["rw_flower"], "shimmer_gain", "constant", "constant")
        out.append(route("macro.synth", sh, 1.6))
        out += stem_route("drums", "treble", "response.hatEnv", sh, 1.2,
                          {"offset": -0.35, "gain": 1.6, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                           "attackMs": 3.0, "decayMs": 160.0}, fb_chain={"attackMs": 3.0, "decayMs": 120.0})

    # ---- drums: the clap fronts on the arches ----------------------------------------------------------------------
    # both feet to the crown in one beat; bright only where the drum stem's 2-6 kHz band says a clap landed
    for prog in RAINBOWS:
        roles = fx.get(prog, {})
        if "front_gain" not in roles:
            continue
        for role, amt in (("front_l_in", 0.4), ("front_l_out", 0.4), ("front_r_in", -0.4), ("front_r_out", -0.4)):
            for c in (0, 1):
                out.append(route("beat.phase", op_target(prog, roles, role, "remap", "constant"), amt, component=c))
        out += stem_route("drums", "highMid", "response.snareEnv",
                          op_target(prog, roles, "front_gain", "constant", "constant"), 10.0,
                          {"offset": -1.55, "gain": 2.5, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                           "attackMs": 4.0, "decayMs": 280.0}, fb_chain={"attackMs": 4.0, "decayMs": 220.0})
        # ---- synth: the arches' flow and brightness ----------------------------------------------------------------
        out.append(route("macro.synth", op_target(prog, roles, "flow_gain", "constant", "constant"), 3.0))
        out.append(route("macro.synth", op_target(prog, roles, "arch_gain", "constant", "constant"), 1.2))

    # ---- synth: the musical control. Its followed level (the drop's bed sits at 0.47-0.71) conditioned and smoothed
    # over a beat and more, so the kick's sidechain pump does not ripple the light; each synth onset (the intro's
    # falling calls, chord changes) adds a swell that falls over 0.7 s.
    # The level reads against the stem's own adaptive reference (ADR-1510), so the intro's isolated calls read at the
    # top and the drop's steady bed reads mid-range; the window RMS is absolute, so the bed (0.12-0.18) outweighs the
    # calls (0.02-0.06). Both, so the world is brightest where the synth is loudest and still answers each entry.
    out += stem_route("synth", "level", "response.sustain", "macros/synth", 0.4,
                      {"gain": 2.2, "offset": -0.95, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 250.0, "decayMs": 1200.0},
                      fb_amount=0.9,
                      fb_chain={"gain": 2.0, "offset": -0.75, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                "attackMs": 250.0, "decayMs": 1200.0})
    out += stem_route("synth", "rms", None, "macros/synth", 0.7,
                      {"gain": 4.5, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 300.0, "decayMs": 1200.0})
    out += stem_route("synth", "onset", None, "macros/synth", 0.3,
                      {"envelope": "linearfall", "envelopeFallPerSecond": 1.4, "attackMs": 20.0})
    # ---- phrase: every 4 bars (beat.phrasePulse, bars 1 + 4k) the arches flare and the kick's ring is brighter; the
    # flare's depth is the synth (a phrase in the intro, before the bed, flares less)
    for prog in RAINBOWS:
        roles = fx.get(prog, {})
        if "arch_gain" in roles:
            out.append(route("beat.phrasePulse", op_target(prog, roles, "arch_gain", "constant", "constant"), 4.0,
                             {"envelope": "linearfall", "envelopeFallPerSecond": 0.6, "attackMs": 30.0},
                             depthSource="macro.synth", depthMin=0.35, depthMax=1.0))
            out.append(route("macro.lift", op_target(prog, roles, "arch_gain", "constant", "constant"), 1.0))
    # ---- lift: the synth's brightness over bars (its centroid; the 151-165 s rise, the riser at 183-190 s) lifts
    # the sun's glow, the bloom and the arches together, slowly
    # (gated by the synth's light: a silent stem's centroid is its noise floor's, and read high)
    out += stem_route("synth", "centroid", "audio.spectralCentroid", "macros/lift", 1.0,
                      {"gain": 6.0, "offset": -3.75, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 1500.0, "decayMs": 3000.0},
                      fb_chain={"gain": 4.0, "offset": -2.0, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                "attackMs": 1500.0, "decayMs": 3000.0})
    for r_ in out[-2:]:
        if r_["target"] == "macros/lift" and r_["source"].startswith("stem."):
            r_.update(depthSource="macro.synth", depthMin=0.0, depthMax=1.0)
    out.append(route("macro.lift", "env/sky/sunGlow", 0.35))
    out.append(route("macro.lift", "post/bloom/intensity", 0.12))
    # ---- the drop: a world-scale impact, rare by construction --------------------------------------------------------
    # `surge` is the synth's absolute loudness (rms) times how far its level stands above its own recent past (the
    # level reads against an adaptive reference, ADR-1510, so a loud entry after a quiet stretch reads at the top for a
    # few seconds): high only when a LOUD synth ARRIVES -- the drop at 47.85 s. The intro's calls are quiet (low rms),
    # the groove's bed is not new (level mid-range). A phrase boundary with a surge behind it is the impact.
    out += stem_route("synth", "rms", None, "macros/surge", 1.0,
                      {"gain": 8.0, "offset": -0.4, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 40.0, "decayMs": 5000.0})   # slow release: the scan's hold reads it as its depth
    out[-1].update(depthSource="stem.synth.level", depthMin=-2.0, depthMax=1.0)
    impact = {"envelope": "linearfall", "envelopeFallPerSecond": 0.25, "attackMs": 20.0}   # four seconds down
    def hit(target, amount, component=-1):
        # the impact rides the scan's own latch: everything flares together and relaxes together
        out.append(route("macro.scan", target, amount, None, component))
    for prog in RAINBOWS:
        if "arch_gain" in fx.get(prog, {}):
            hit(op_target(prog, fx[prog], "arch_gain", "constant", "constant"), 14.0)
            hit(op_target(prog, fx[prog], "flow_gain", "constant", "constant"), 10.0)
    # ---- the world scan: the drop's showpiece (and the synth's biggest lifts), rainbow over everything it passes ---
    # The front's radius R: pulled in to the eye on every phrase pulse and let run out (970 m in 4 s); invisible
    # unless `macro.scan` -- held about six seconds, then relaxing over three -- says this phrase deserves it, which
    # the surge (the drop) does, and a big lift (the synth at its brightest) does a little.
    rest = SCAN_REST - 10.0
    front = {"envelope": "linearfall", "envelopeFallPerSecond": 1.0 / 4.0, "delayMs": SCAN_DELAY_MS}
    for prog in scan_programs:
        roles = fx[prog]
        for role in ("scan_mask", "scan_hue", "scan_edge_out", "scan_edge_in"):
            for c in (0, 1):
                out.append(route("beat.phrasePulse", op_target(prog, roles, role, "remap", "constant"), -rest, front,
                                 component=c))
        out.append(route("macro.scan", op_target(prog, roles, "scan_amount", "constant", "constant"), 1.0))
        out.append(route("macro.scan", op_target(prog, roles, "scan_edge_gain", "constant", "constant"), 4.0))
    # One shot per qualifying phrase: the phrase pulse, read SCAN_DELAY_MS late (the synth enters on the drop's
    # downbeat, so its surge is only measurable a beat-fraction after it), scaled by the surge at that instant, is a
    # one-frame trigger; past 0.6 it latches the scan for six seconds, which then relaxes over three.
    out.append(route("beat.phrasePulse", "macros/scantrig", 1.0,
                     {"delayMs": SCAN_DELAY_MS, "envelope": "peakhold", "envelopeHoldMs": 60.0,
                      "envelopeFallPerSecond": 20.0},
                     depthSource="macro.surge", depthMin=0.0, depthMax=1.0))
    out.append(route("macro.scantrig", "macros/scan", 1.0,
                     {"threshold": "binary", "thresholdLevel": 0.6, "envelope": "peakhold", "envelopeHoldMs": 6000.0,
                      "envelopeFallPerSecond": 0.33, "attackMs": 250.0}))
    # what the scan touches that has no scan ops -- the trees (masked: ops would cost in the depth prepass) and the
    # clouds -- crossfades its colour ramp to the spectrum, bottom to top, by the same amount (ADR-900's Replace depth)
    spectrum4 = [(0.90, 0.14, 0.05), (0.95, 0.80, 0.05), (0.10, 0.75, 0.30), (0.25, 0.30, 1.00)]
    pastel4 = [tuple(0.55 * c + 0.45 for c in col) for col in spectrum4]
    for prog, role, cols in [(p_, "canopy_ramp", spectrum4) for p_ in fx if "canopy_ramp" in fx[p_]] + \
                            [("rw_cloud", "cloud_ramp", pastel4)]:
        if role not in fx.get(prog, {}):
            continue
        for k, field in enumerate(("constant", "constant2", "constant3", "constant4")):
            for c in range(3):
                out.append(route("macro.one", op_target(prog, fx[prog], role, "oklabRamp", field), cols[k][c],
                                 component=c, op="replace", depthSource="macro.scan", depthMin=0.0, depthMax=1.0))
    if "cloud_glow" in fx.get("rw_cloud", {}):
        hit(op_target("rw_cloud", fx["rw_cloud"], "cloud_glow", "constant", "constant"), 0.6)
    hit("env/sky/sunGlow", 1.2)
    hit("post/bloom/intensity", 0.35)
    hit("post/grade/saturation", 0.25)
    if "cloud_glow" in fx.get("rw_cloud", {}):
        out.append(route("macro.synth", op_target("rw_cloud", fx["rw_cloud"], "cloud_glow", "constant", "constant"),
                         0.3))
    return out
