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


MACROS = [("swell", 0.0), ("synth", 0.0), ("lift", 0.0), ("scan", 0.0), ("one", 1.0)]
SCAN_REST = 980.0    # look.SCAN_REST
SCAN_DELAY_MS = 300.0
RAINBOWS = ("rw_rainbow",)
FW_EDGE_FIELDS = []   # build_world's firework fields whose falloff edges ride the front (filled by the build)

# The showcase's world scans (gate 5, the owner: "rainbow world scan only happened once and there was a glitch at the
# end"; 26-world-scan.md). On Feline Footwear the scan fires at the song's big synth swells and section peaks, from
# the stems' own analysis (the synth stem's rms over the four bars after each phrase start against the four before;
# 14-stem-analysis for the sections):
#   47.85 s  the drop: the synth's entry, by far the largest rise (+0.10 rms)
#   111.3 s  section B: the drums go to 16th hats under the full synth bed
#   158.9 s  the synth's brightness climbing to its peak (centroid at 160.9 s)
#   190.7 s  the riser's top: the synth's loudest phrase (0.21 rms), the drums gone
# Irregular (63, 48, 32 s apart), never on a grid. Each is baked into two timelines -- the scan's amount and its
# front -- so a seek lands where a play does, and nothing in the effect can reset part-way through (the gate-4 glitch
# was the live trigger's depth, the synth level, dipping and recovering on the scan's tail).
SCAN_TIMES = (47.85, 111.3, 158.9, 190.7)
SCAN_RISE, SCAN_HOLD, SCAN_FALL, SCAN_RUN = 1.2, 5.3, 3.0, 7.0     # seconds: ease in, hold, ease out; the front's run


def _ease(u):
    u = min(1.0, max(0.0, u))
    return u * u * (3 - 2 * u)


def showcase_scan_sources(song_end):
    """The `scan` (amount 0..1) and `scanFront` (1 = the front at the eye .. 0 = run out to rest) timelines."""
    amt, front = [{"time": 0.0, "value": 0.0, "interp": "linear"}], [{"time": 0.0, "value": 0.0, "interp": "linear"}]
    for t0 in SCAN_TIMES:
        t0 += SCAN_DELAY_MS / 1000.0
        for k in range(1, 9):                                   # eased in
            amt.append({"time": round(t0 + SCAN_RISE * k / 8, 4), "value": round(_ease(k / 8), 4), "interp": "linear"})
        for k in range(0, 13):                                  # held, then eased out
            tt = t0 + SCAN_RISE + SCAN_HOLD + SCAN_FALL * k / 12
            amt.append({"time": round(tt, 4), "value": round(1.0 - _ease(k / 12), 4), "interp": "linear"})
        front.append({"time": round(t0 - 0.001, 4), "value": 0.0, "interp": "step"})
        for k in range(0, 25):                                  # the front leaves the eye and runs out to rest,
            u = k / 24                                          # eased both ways (gate 5: slow enough near the
            front.append({"time": round(t0 + SCAN_RUN * u, 4), "value": round(1.0 - _ease(u), 4),   # eye for the
                          "interp": "linear"})                                                       # trees to spin)
    amt.append({"time": round(song_end, 4), "value": 0.0, "interp": "linear"})
    front.append({"time": round(song_end, 4), "value": 0.0, "interp": "linear"})
    return [{"kind": "timeline", "name": "scan", "settings": {"keys": amt}},
            {"kind": "timeline", "name": "scanFront", "settings": {"keys": front}}]


SCAN_REFRACTORY_MS = 12000.0


def front_targets(fx, scan_programs):
    """Every (target, component) the scan's front pulls in: the land's scan ops and the sparkle's fields."""
    out = []
    for prog in scan_programs:
        for role in ("scan_mask", "scan_hue", "scan_edge_out", "scan_edge_in"):
            for c in (0, 1):
                out.append((op_target(prog, fx[prog], role, "remap", "constant"), c))
    for fld, params in FW_EDGE_FIELDS:
        for prm in params:
            out.append(("field/%s/%s" % (fld, prm), -1))
    return out


def showcase_scan_routes(routes_, fx):
    """The live triggers swapped for the showcase's baked scans (one route per front target, and the amount)."""
    scan_programs = [p for p in fx if "scan_mask" in fx[p]]
    fronts = {t for t, _c in front_targets(fx, scan_programs)}
    out = [r for r in routes_ if r["target"] != "macros/scan" and r["target"] not in fronts]
    for tgt, c in front_targets(fx, scan_programs):
        out.append(route("timeline.scanFront", tgt, -(SCAN_REST - 10.0), component=c))
    out.append(route("timeline.scan", "macros/scan", 1.0))
    return out


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
    # (gate 5, the owner chose 23 item 3: "breathe across their range": it sat above 0.3 for 85% of the song; now
    # only the bass's top half counts and it lets go in 180 ms, so it swings 0-1 within a bar -- median 0.31, a 0.46
    # swing a second -- and each note pushes it)
    out += stem_route("bass", "level", "response.bass", "macros/swell", 1.0,
                      {"gain": 2.4, "offset": -1.4, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 40.0, "decayMs": 180.0},
                      fb_chain={"gain": 2.4, "offset": -1.45, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                "attackMs": 40.0, "decayMs": 180.0})
    out += stem_route("bass", "onset", None, "macros/swell", 0.5,
                      {"envelope": "linearfall", "envelopeFallPerSecond": 4.0, "attackMs": 10.0})
    for nm in swell_nodes:
        for k, wave in enumerate(swell):
            out.append(route("macro.swell", "procedural/%s/deform/%d/amount" % (nm, k + 1), wave[-1]))
    out.append(route("macro.swell", "lights/cloudsea/intensity", 1.2))
    # the bass on the big surfaces (gate 5): the whole palette -- meadow, sky, clouds -- brightens with it a little
    out.append(route("macro.swell", "palette/value", 0.07))
    # the bass as colour energy (19-brief-color-vfx: "bass energy increases the intensity of selected terrain
    # colours"): the palette's own chroma (ADR-1043's palette/saturation, applied to every colour the look binds --
    # meadow, sky, clouds, trees, stones) rises with the bass body. No shader cost: the first version did it with
    # three ops in the ground and grass programs.
    out.append(route("macro.swell", "palette/saturation", 0.18))
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
        if "flow_gain" not in roles:
            continue
        # the clap brightens the flow bands: the backbeat travels along the bow
        out += stem_route("drums", "highMid", "response.snareEnv",
                          op_target(prog, roles, "flow_gain", "constant", "constant"), 2.5,
                          {"offset": -1.55, "gain": 2.5, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                           "attackMs": 4.0, "decayMs": 280.0}, fb_chain={"attackMs": 4.0, "decayMs": 220.0})
        # ---- the beat on the bows (gate 5): the bands' colours turn once a beat, and each beat flashes them
        if "flow_palette" in roles:
            out.append(route("beat.phase", op_target(prog, roles, "flow_palette", "palette", "value"), 1.0,
                             op="replace"))
        out.append(route("beat.pulse", op_target(prog, roles, "flow_gain", "constant", "constant"), 2.5,
                         {"envelope": "linearfall", "envelopeFallPerSecond": 3.5, "attackMs": 5.0}))
        out.append(route("beat.pulse", op_target(prog, roles, "arch_gain", "constant", "constant"), 0.8,
                         {"envelope": "linearfall", "envelopeFallPerSecond": 3.5, "attackMs": 5.0}))
        # ---- synth: the arches' flow and brightness ----------------------------------------------------------------
        out.append(route("macro.synth", op_target(prog, roles, "flow_gain", "constant", "constant"), 3.0))
        out.append(route("macro.synth", op_target(prog, roles, "arch_gain", "constant", "constant"), 1.2))

    # ---- synth: the musical control. Its followed level (the drop's bed sits at 0.47-0.71) conditioned and smoothed
    # over a beat and more, so the kick's sidechain pump does not ripple the light; each synth onset (the intro's
    # falling calls, chord changes) adds a swell that falls over 0.7 s.
    # The level reads against the stem's own adaptive reference (ADR-1510), so the intro's isolated calls read at the
    # top and the drop's steady bed reads mid-range; the window RMS is absolute, so the bed (0.12-0.18) outweighs the
    # calls (0.02-0.06). Both, so the world is brightest where the synth is loudest and still answers each entry.
    # (gate 5, "breathe": it sat above 0.3 for 84% of the song at 0.5-0.8; now the bed sits about 0.3 and the
    # swells and peaks reach 0.6-1 -- the rms counts from 0.11, the level and the notes push on top)
    out += stem_route("synth", "level", "response.sustain", "macros/synth", 0.3,
                      {"gain": 2.2, "offset": -0.95, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 250.0, "decayMs": 900.0},
                      fb_amount=0.9,
                      fb_chain={"gain": 2.0, "offset": -0.75, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                "attackMs": 250.0, "decayMs": 1200.0})
    out += stem_route("synth", "rms", None, "macros/synth", 0.8,
                      {"gain": 7.0, "offset": -0.8, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 250.0, "decayMs": 900.0})
    out += stem_route("synth", "onset", None, "macros/synth", 0.35,
                      {"envelope": "linearfall", "envelopeFallPerSecond": 1.6, "attackMs": 20.0})
    # ---- phrase: every 4 bars (beat.phrasePulse, bars 1 + 4k) the arches flare and the kick's ring is brighter; the
    # flare's depth is the synth (a phrase in the intro, before the bed, flares less)
    for prog in RAINBOWS:
        roles = fx.get(prog, {})
        if "arch_gain" in roles:
            out.append(route("beat.phrasePulse", op_target(prog, roles, "arch_gain", "constant", "constant"), 4.0,
                             {"envelope": "linearfall", "envelopeFallPerSecond": 0.6, "attackMs": 30.0},
                             depthSource="stem.synth.rms", depthMin=0.25, depthMax=5.0, depthDefault=0.15))   # replayable (ADR-901); a stereo session: as a groove
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
            r_.update(depthSource="stem.synth.rms", depthMin=-0.2, depthMax=6.0)   # replayable: not a macro
    out.append(route("macro.lift", "env/sky/sunGlow", 0.35))
    out.append(route("macro.synth", "env/sky/sunGlow", 0.6))    # the synth's light in the sky
    # (the sun's rays swell with the synth too: build_world's sunburst routes)
    out.append(route("macro.lift", "post/bloom/intensity", 0.12))
    # ---- the drop: a world-scale impact, rare by construction --------------------------------------------------------
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
    # The live scan (gate 5, the coordinator: "make the live trigger latch"; 26-world-scan.md section 4). It fires on a
    # SWELL -- the trigger signal's short-term average rising past its long-term one (ADR-1575), so each new swell
    # above the current bed counts, however loud the bed -- and then plays its whole shape whatever the music does
    # meanwhile: the latching envelope (ADR-1574) ignores its input for 12 s once triggered and re-arms only once the
    # swell has passed. Two triggers, each one replay-safe signal (no depth to dip on the tail):
    #   stem   the synth stem's rms, 1.25x its 8 s average (floored at 0.06 rms: the intro's calls cannot qualify)
    #   mix    on a plain stereo input, the mix's sustained (pad/synth) energy, 1.1x its 8 s average (floored at 0.1);
    #          stands aside while a synth stem is active (ADR-1570's fallback shape)
    # The amount holds 6.5 s and falls over 3 s, eased by a spring (ADR-1041); the front runs out over SCAN_RUN on
    # the same latch, so it never resets part-way through.
    def latch(extra, trend, level, hold_off):
        # the swell: the trigger signal's short-term average over its long-term one (ADR-1575; floored so an intro's
        # near-silence cannot spike it), on at `level`, held through the music's own dips for `hold_off` ms; the
        # latch (ADR-1574) then plays the whole scan and re-arms only once the swell has passed
        c = {"trendSeconds": trend[0], "trendFastMs": trend[1], "trendFloor": trend[2],
             "threshold": "binary", "thresholdLevel": level, "attackMs": 0.0, "decayMs": hold_off,
             "envelopeRefractoryMs": SCAN_REFRACTORY_MS, "envelopeRearm": 0.3}
        c.update(extra)
        return c
    amount_env = {"envelope": "peakhold", "envelopeHoldMs": 6500.0, "envelopeFallPerSecond": 0.33,
                  "springHz": 0.5, "springDamping": 1.0}
    front_env = {"envelope": "linearfall", "envelopeFallPerSecond": 1.0 / SCAN_RUN}
    triggers = [("stem.synth.rms", (8.0, 450.0, 0.06), 1.25, 1500.0, {"optional": True}),
                ("response.sustain", (8.0, 1000.0, 0.1), 1.1, 1500.0,
                 {"depthSource": "stem.synth.active", "depthMin": 1.0, "depthMax": 0.0, "depthDefault": 0.0})]
    for src, trend, level, hold_off, extra in triggers:
        out.append(route(src, "macros/scan", 1.0, latch(amount_env, trend, level, hold_off), op="max", **extra))
        for tgt in front_targets(fx, scan_programs):
            out.append(route(src, tgt[0], -rest, latch(front_env, trend, level, hold_off), component=tgt[1], **extra))
    for prog in scan_programs:
        roles = fx[prog]
        out.append(route("macro.scan", op_target(prog, roles, "scan_amount", "constant", "constant"), 1.0))
        out.append(route("macro.scan", op_target(prog, roles, "scan_edge_gain", "constant", "constant"), 4.0))
    if "spark_gain" in fx.get("rw_spark", {}):
        out.append(route("macro.synth", op_target("rw_spark", fx["rw_spark"], "spark_gain", "constant",
                                                  "constant"), 3.0))   # the synth's light in the sparks
    # what the scan touches that has no scan ops -- the trees (masked: ops would cost in the depth prepass) --
    # crossfades its colour ramp to the spectrum, bottom to top, by the same amount (ADR-900's Replace depth). (The
    # clouds' ramp is the palette's, which replaces after the routes: the clouds flare through `cloud_glow` instead.)
    spectrum4 = [(0.90, 0.14, 0.05), (0.95, 0.80, 0.05), (0.10, 0.75, 0.30), (0.25, 0.30, 1.00)]
    pastel4 = [tuple(0.55 * c + 0.45 for c in col) for col in spectrum4]
    for prog, role, cols in [(p_, "canopy_ramp", spectrum4) for p_ in fx if "canopy_ramp" in fx[p_]]:
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
    out += sky_routes(fx)
    return out


SKY_BREATH = "skyBreath"   # build_world's constant field the clouds' scale effector reads (its strength is routed)


def sky_routes(fx):
    """Gate 7 (the owner: "adding a bit of audio reacivity to the sky - pulsating clouds maybe, splashes of color";
    28-twirl-sky-sun.md section 4).
    - the clouds breathe with the bass: every puff swells about its own centre (a scale effector on a constant field
      whose strength the bass body drives) and brightens with it;
    - splashes of colour: on each clap (the drum stem's 2-6 kHz band) patches of saturated colour bloom across the
      clouds and fade within about a beat; each phrase start (bars 1 + 4k) blooms bigger and fades over two beats.
      Patches, never the whole sky (the owner's rule at gate 2: no world sweep)."""
    out = [route("macro.swell", "field/%s/strength" % SKY_BREATH, 1.0)]
    roles = fx.get("rw_cloud", {})
    if "cloud_glow" in roles:
        out.append(route("macro.swell", op_target("rw_cloud", roles, "cloud_glow", "constant", "constant"), 0.3))
    if "splash_amount" not in roles:
        return out
    amt = op_target("rw_cloud", roles, "splash_amount", "constant", "constant")
    out += stem_route("drums", "highMid", "response.snareEnv", amt, 0.7,
                      {"offset": -1.3, "gain": 2.5, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                       "attackMs": 8.0, "decayMs": 420.0}, fb_chain={"attackMs": 8.0, "decayMs": 380.0})
    out.append(route("beat.phrasePulse", amt, 0.6, {"envelope": "linearfall", "envelopeFallPerSecond": 0.9,
                                                      "attackMs": 30.0}))
    out.append(route("beat.phrase", op_target("rw_cloud", roles, "splash_hue", "palette", "value"), 1.0, op="replace"))
    shift = op_target("rw_cloud", roles, "splash_shift", "constant", "constant")
    out.append(route("beat.phrase", shift, 900.0, op="replace", component=0))
    out.append(route("beat.bar", shift, 500.0, op="replace", component=2))
    return out
