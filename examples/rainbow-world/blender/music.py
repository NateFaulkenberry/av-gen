"""Rainbow World 2: how the world hears the music (docs/prototypes/rainbow-world/16-effect-mapping.md).

Pure Python, no bpy. `build_world.py` calls `routes(...)` and writes them into the live project and the Feline Footwear
showcase. Three layers, as the brief's section 5.1 asks:

  1. raw features      the engine's signals: `stem.<key>.*` (ADR-1520) when a drum, bass or synth stem exists -- a
                       stem file offline, a mapped input channel live (ADR-1521) -- and the main mix's `response.*`
                       and `beat.*` otherwise;
  2. musical controls  conditioned signals (`signal.<name>`, ADR-1602): each musical idea's chain written once and
                       published the same frame as the music it follows -- swell (bass), synth (the synth's light),
                       lift (the arrangement's slow rise), scan (the drop's latch) and its front, and every stem
                       feature with its main-mix fallback (`firstActive`: the stem while it is active, else the mix);
  3. targets           material-program constants, deformer amounts, light and post parameters, routed from the
                       signals with no chain of their own.

The Living Prism P0-3 port (ADR-1602): these were macro knobs (`macros/<k>` written by routes, read back one frame late
as `macro.<k>`) and stem routes doubled by main-mix twins (ADR-1570); the same chains now live in the signals, so every
macro-routed effect lands one frame earlier and each twin pair is one route. Nothing here reads a clock but the
transport (routes, `beat.*`, the material `time` input), so a seek lands where a play does.
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


# ---- the conditioned signals (ADR-1602) --------------------------------------------------------------------------
# Filled by `routes()`, in dependency order (a signal reading another is listed after it); build_world writes them into
# the project's sources.
SIGNALS = []


def inp(source, chain=None, amount=1.0, **extra):
    """One input of a signal: a route's front half (its chain, amount and depth) on `source`."""
    i = {"source": source}
    if amount != 1.0:
        i["amount"] = round(float(amount), 5)
    if chain:
        i["chain"] = chain
    i.update(extra)
    return i


def signal(name, inputs, combine="firstActive", chain=None):
    """Declares `signal.<name>` and returns its bus name."""
    s = {"combine": combine, "inputs": inputs}
    if chain:
        s["chain"] = chain
    SIGNALS.append({"kind": "signal", "name": name, "settings": s})
    return "signal." + name


UNIT = {"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}   # a macro knob's own range, kept by the signals that
                                                                  # replaced one


def stem_signal(name, stem, feature, fallback, chain, fb_chain=None):
    """`stem.<stem>.<feature>` through `chain` while that stem is active (its `.active`), else `fallback` through
    `fb_chain`: what a stem route and its main-mix twin (ADR-1570) did to their target, as one signal. No `fallback`:
    the stem's alone (nothing on a mix)."""
    inputs = [inp("stem.%s.%s" % (stem, feature), chain)]
    if fallback is not None:
        inputs.append(inp(fallback, chain if fb_chain is None else fb_chain))
    return signal(name, inputs)


def op_target(program, roles, role, kind, field):
    return "material/%s/op/%d/%s/%s" % (program, roles[role], kind, field)


SCAN_REST = 980.0    # look.SCAN_REST
RAINBOWS = ("rw_rainbow",)
FW_EDGE_FIELDS = []   # build_world's firework fields whose falloff edges ride the front (filled by the build)

SCAN_RUN = 7.0     # seconds: the front's run from the eye out to rest (139 m/s: the twirl and fireworks are timed at
                   # this speed; the gate-2 6 s was reverted with the gate-8 envelope, 29 section 9)


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


def scan_hooks(fx):
    """What a song-specific project needs to bake its own scans over the live trigger (the Feline Footwear video,
    29-brief-feline-video.md). Since ADR-1602 the scan is two signals, `signal.scan` (the amount, 0..1) and
    `signal.scanFront` (the front, 1 = at the eye, 0 = run out to rest): a project replaces their inputs with its baked
    timelines (`{"source": "timeline.scan"}`) and keeps every route. The front targets and their amount
    (-(rest - 10) per unit front) are listed for a project that routes a front of its own."""
    scan_programs = [p for p in fx if "scan_mask" in fx[p]]
    return {"amountSignal": "scan", "frontSignal": "scanFront", "restRadius": SCAN_REST,
            "frontAmount": -(SCAN_REST - 10.0), "frontTargets": [[t, c] for t, c in front_targets(fx, scan_programs)]}


def routes(fx, swell_nodes, swell):
    """`fx`: look.FX (program -> role -> 1-based op index). `swell_nodes`: the nodes carrying the swell deformers.
    `swell`: look's SWELL list (its last field is metres at a full swell)."""
    out = []
    SIGNALS.clear()
    scan_programs = [p for p in fx if "scan_mask" in fx[p]]

    # ---- drums: the kick, locally (the owner at gate 2: no world sweep on every quarter note) ----------------------
    # the grass round the camera pops its tips on each kick, ON the beat: a pulse cut from the beat clock's own phase,
    # (1 - phase)^6 -- full on the beat, a tenth by a third of the beat -- its depth the drum stem's level (no drums,
    # no pops: the end) and, with no drum stem, full while the beat clock runs. The owner (29 section 9: "the
    # ground lights look completely off time"): read from the drum stem's low band they peaked 100 ms after the kick
    # (the band's analysis latency plus the chain), measured against the grid; the phase lands within 10 ms of it.
    if "kick_gain" in fx.get("rw_grass", {}):
        kick = op_target("rw_grass", fx["rw_grass"], "kick_gain", "constant", "constant")
        # (29 section 10, the owner: "make sure theyre clearing flashing on and off to the beat"): a square flash, not
        # a ramp -- on at full the frame the phase turns over, held for the first eighth of the beat (62 ms), then
        # gone in 45 ms: dark for the rest of the beat, so the eye finds the hit
        pulse = {"gain": -1.0, "offset": 1.0, "threshold": "binary", "thresholdLevel": 0.875, "attackMs": 0.0,
                 "decayMs": 45.0}
        # depth = -0.5 + 2.4 x the drums' level: none below 0.21 (the drums gone), 1 at 0.62, 1.4 at full drive; with
        # no drum stem the default reads depth 0 and the fallback below plays instead
        # (ADR-1602: the twin pair as one signal: the drum stem's level while it is active, else full)
        out.append(route(signal("beatPop", [inp("beat.phase", pulse, depthSource="stem.drums.level", depthMin=-0.5,
                                             depthMax=1.9, gate="stem.drums.active"),
                                         inp("beat.phase", pulse)]), kick, 3.6))

    # ---- drums: glints in the grass (the drum stem's highs; nothing on a mix) ----------------------------------------
    if "glint_gain" in fx.get("rw_grass", {}):
        roles = fx["rw_grass"]
        glint = {"offset": -0.35, "gain": 1.6, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                 "attackMs": 3.0, "decayMs": 110.0}
        hats = stem_signal("glint", "drums", "treble", None, glint)
        out.append(route(hats, op_target("rw_grass", roles, "glint_gain", "constant", "constant"), 3.2))
        out.append(route(hats, op_target("rw_grass", roles, "glint_threshold", "threshold", "value"), -0.14))

    # ---- bass: the swell -------------------------------------------------------------------------------------------
    # the bass body (its followed level, conditioned so the stem's quiet floor reads 0) plus a push on each note
    # (gate 5, the owner chose 23 item 3: "breathe across their range": it sat above 0.3 for 85% of the song; now
    # only the bass's top half counts and it lets go in 180 ms, so it swings 0-1 within a bar -- median 0.31, a 0.46
    # swing a second -- and each note pushes it; 29 section 11: it takes each note in 10 ms, not 40, so the clouds'
    # breathing lands with the note -- measured on the video, they followed the bass by a steady +33 ms at 10, 45, 90
    # and 150 s: no drift)
    # 29 sections 12-13 (the owner: "hitting with real bass note hits ... some important bass notes are now being
    # missed"): with a bass stem the swell is its NOTES -- each onset one breath, up in 25 ms and down over about
    # 0.4 s, the note's level its size -- not its continuous level, whose wobbles between and within the notes made
    # the clouds (and the palette and the cloud sea) undulate. A re-trigger within 130 ms (the same note) is latched
    # out (ADR-1574); notes 0.124 s apart (the line's closest) both breathe. Without a bass stem, the mix's bass level
    # as before.
    # (ADR-1602: `bassNote` is the stem's notes, unclamped, as the clouds read them; `swell` is the knob it replaced,
    # 0..1, the notes while the bass stem is active and the mix's bass body otherwise)
    signal("bassNote", [inp("stem.bass.onset", NOTE_CHAIN, depthSource="stem.bass.level", depthMin=-0.3, depthMax=1.9,
                            depthDefault=0.3 / 2.2)])
    swell_ = signal("swell", [inp("signal.bassNote", gate="stem.bass.active"),
                              inp("response.bass", {"gain": 2.4, "offset": -1.45, "clampEnabled": True, "clampMin": 0.0,
                                                    "clampMax": 1.0, "attackMs": 10.0, "decayMs": 180.0})], chain=UNIT)
    for nm in swell_nodes:
        for k, wave in enumerate(swell):
            out.append(route(swell_, "procedural/%s/deform/%d/amount" % (nm, k + 1), wave[-1]))
    out.append(route(swell_, "lights/cloudsea/intensity", 1.2))
    # the bass on the big surfaces (gate 5): the whole palette -- meadow, sky, clouds -- brightens with it a little
    out.append(route(swell_, "palette/value", 0.07))
    # the bass as colour energy (19-brief-color-vfx: "bass energy increases the intensity of selected terrain
    # colours"): the palette's own chroma (ADR-1043's palette/saturation, applied to every colour the look binds --
    # meadow, sky, clouds, trees, stones) rises with the bass body. No shader cost: the first version did it with
    # three ops in the ground and grass programs.
    out.append(route(swell_, "palette/saturation", 0.18))
    # shimmer across the flower fields: the synth's light, lifted by the drum stem's highs
    if "shimmer_gain" in fx.get("rw_flower", {}):
        sh = op_target("rw_flower", fx["rw_flower"], "shimmer_gain", "constant", "constant")
        out.append(route("signal.synth", sh, 1.6))
        out.append(route(stem_signal("shimmer", "drums", "treble", None,
                                     {"offset": -0.35, "gain": 1.6, "clampEnabled": True, "clampMin": 0.0,
                                      "clampMax": 1.0, "attackMs": 3.0, "decayMs": 160.0}), sh, 1.2))

    # ---- drums: the clap fronts on the arches ----------------------------------------------------------------------
    # both feet to the crown in one beat; bright only where the drum stem's 2-6 kHz band says a clap landed (on a mix,
    # any attack)
    for prog in RAINBOWS:
        roles = fx.get(prog, {})
        if "flow_gain" not in roles:
            continue
        # the clap brightens the flow bands: the backbeat travels along the bow
        out.append(route(stem_signal("clap", "drums", "highMid", "response.onsetEnv",
                                     {"offset": -1.55, "gain": 2.5, "clampEnabled": True, "clampMin": 0.0,
                                      "clampMax": 1.0, "attackMs": 4.0, "decayMs": 280.0},
                                     {"attackMs": 4.0, "decayMs": 220.0}),
                         op_target(prog, roles, "flow_gain", "constant", "constant"), 2.5))
        # ---- the beat on the bows (gate 5): the bands' colours turn once a beat, and each beat flashes them
        if "flow_palette" in roles:
            out.append(route("beat.phase", op_target(prog, roles, "flow_palette", "palette", "value"), 1.0,
                             op="replace"))
        out.append(route("beat.pulse", op_target(prog, roles, "flow_gain", "constant", "constant"), 2.5,
                         {"envelope": "linearfall", "envelopeFallPerSecond": 3.5, "attackMs": 5.0}))
        out.append(route("beat.pulse", op_target(prog, roles, "arch_gain", "constant", "constant"), 0.8,
                         {"envelope": "linearfall", "envelopeFallPerSecond": 3.5, "attackMs": 5.0}))
        # ---- synth: the arches' flow and brightness ----------------------------------------------------------------
        out.append(route("signal.synth", op_target(prog, roles, "flow_gain", "constant", "constant"), 3.0))
        out.append(route("signal.synth", op_target(prog, roles, "arch_gain", "constant", "constant"), 1.2))

    # ---- synth: the musical control. Its followed level (the drop's bed sits at 0.47-0.71) conditioned and smoothed
    # over a beat and more, so the kick's sidechain pump does not ripple the light; each synth onset (the intro's
    # falling calls, chord changes) adds a swell that falls over 0.7 s.
    # The level reads against the stem's own adaptive reference (ADR-1510), so the intro's isolated calls read at the
    # top and the drop's steady bed reads mid-range; the window RMS is absolute, so the bed (0.12-0.18) outweighs the
    # calls (0.02-0.06). Both, so the world is brightest where the synth is loudest and still answers each entry.
    # (gate 5, "breathe": it sat above 0.3 for 84% of the song at 0.5-0.8; now the bed sits about 0.3 and the
    # swells and peaks reach 0.6-1 -- the rms counts from 0.11, the level and the notes push on top)
    # (ADR-1602: the stem's three parts summed in `synthStem`; `synth` is that while the synth stem is active, else the
    # mix's sustained energy, in the knob's 0..1)
    signal("synthStem", [inp("stem.synth.level", {"gain": 2.2, "offset": -0.95, "clampEnabled": True, "clampMin": 0.0,
                                                  "clampMax": 1.0, "attackMs": 250.0, "decayMs": 900.0}, 0.3),
                         inp("stem.synth.rms", {"gain": 7.0, "offset": -0.8, "clampEnabled": True, "clampMin": 0.0,
                                                "clampMax": 1.0, "attackMs": 250.0, "decayMs": 900.0}, 0.8),
                         inp("stem.synth.onset", {"envelope": "linearfall", "envelopeFallPerSecond": 1.6,
                                                  "attackMs": 20.0}, 0.35)], combine="sum")
    signal("synth", [inp("signal.synthStem", gate="stem.synth.active"),
                     inp("response.sustain", {"gain": 2.0, "offset": -0.75, "clampEnabled": True, "clampMin": 0.0,
                                              "clampMax": 1.0, "attackMs": 250.0, "decayMs": 1200.0}, 0.9)],
           chain=UNIT)
    # ---- phrase: every 4 bars (beat.phrasePulse, bars 1 + 4k) the arches flare and the kick's ring is brighter; the
    # flare's depth is the synth (a phrase in the intro, before the bed, flares less)
    for prog in RAINBOWS:
        roles = fx.get(prog, {})
        if "arch_gain" in roles:
            out.append(route("beat.phrasePulse", op_target(prog, roles, "arch_gain", "constant", "constant"), 4.0,
                             {"envelope": "linearfall", "envelopeFallPerSecond": 0.6, "attackMs": 30.0},
                             depthSource="stem.synth.rms", depthMin=0.25, depthMax=5.0, depthDefault=0.15))   # replayable (ADR-901); a stereo session: as a groove
            out.append(route("signal.lift", op_target(prog, roles, "arch_gain", "constant", "constant"), 1.0))
    # ---- lift: the synth's brightness over bars (its centroid; the 151-165 s rise, the riser at 183-190 s) lifts
    # the sun's glow, the bloom and the arches together, slowly
    # (gated by the synth's light: a silent stem's centroid is its noise floor's, and read high)
    signal("lift", [inp("stem.synth.centroid", {"gain": 6.0, "offset": -3.75, "clampEnabled": True, "clampMin": 0.0,
                                                "clampMax": 1.0, "attackMs": 1500.0, "decayMs": 3000.0},
                        depthSource="stem.synth.rms", depthMin=-0.2, depthMax=6.0),
                    inp("audio.spectralCentroid", {"gain": 4.0, "offset": -2.0, "clampEnabled": True, "clampMin": 0.0,
                                                   "clampMax": 1.0, "attackMs": 1500.0, "decayMs": 3000.0})],
           chain=UNIT)
    out.append(route("signal.lift", "env/sky/sunGlow", 0.35))
    out.append(route("signal.synth", "env/sky/sunGlow", 0.6))    # the synth's light in the sky
    # (the sun's rays swell with the synth too: build_world's sunburst routes)
    out.append(route("signal.lift", "post/bloom/intensity", 0.12))
    # ---- the drop: a world-scale impact, rare by construction --------------------------------------------------------
    impact = {"envelope": "linearfall", "envelopeFallPerSecond": 0.25, "attackMs": 20.0}   # four seconds down
    def hit(target, amount, component=-1):
        # the impact rides the scan's own latch: everything flares together and relaxes together
        out.append(route("signal.scan", target, amount, None, component))
    for prog in RAINBOWS:
        if "arch_gain" in fx.get(prog, {}):
            hit(op_target(prog, fx[prog], "arch_gain", "constant", "constant"), 14.0)
            hit(op_target(prog, fx[prog], "flow_gain", "constant", "constant"), 10.0)
    # ---- the world scan: the drop's showpiece (and the synth's biggest lifts), rainbow over everything it passes ---
    # The front's radius R: pulled in to the eye on every phrase pulse and let run out (970 m in 4 s); invisible
    # unless `signal.scan` -- held about six seconds, then relaxing over three -- says this phrase deserves it, which
    # the surge (the drop) does, and a big lift (the synth at its brightest) does a little.
    rest = SCAN_REST - 10.0
    # (ADR-1602: the amount is `signal.scan`, in the knob's 0..1, and the front `signal.scanFront`, each one signal
    # over the two triggers below -- the stem's while the synth stem is active, else the mix's -- so the front is one
    # route per target, not two.)
    # The live scan (gate 5, the coordinator: "make the live trigger latch"; 26-world-scan.md section 4). It fires on a
    # SWELL -- the trigger signal's short-term average rising past its long-term one (ADR-1575), so each new swell
    # above the current bed counts, however loud the bed -- and then plays its whole shape whatever the music does
    # meanwhile: the latching envelope (ADR-1574) ignores its input for 12 s once triggered and re-arms only once the
    # swell has passed. Two triggers, each one replay-safe signal (no depth to dip on the tail):
    #   stem   the synth stem's rms, 1.25x its 8 s average (floored at 0.06 rms: the intro's calls cannot qualify)
    #   mix    on a plain stereo input, the mix's sustained (pad/synth) energy, 1.1x its 8 s average (floored at 0.1);
    #          stands aside while a synth stem is active (ADR-1570's fallback shape)
    # The amount holds 6.5 s and falls over 3 s, eased by a spring (ADR-1041); the front runs out over SCAN_RUN on
    # the same latch, so it never resets part-way through. (The owner-approved gate-8 envelope: the Feline gate-2
    # shortening -- 5 s, 2 s, a 6 s run -- was reverted, 29 section 9.)
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
    triggers = [("stem.synth.rms", (8.0, 450.0, 0.06), 1.25, 1500.0),
                ("response.sustain", (8.0, 1000.0, 0.1), 1.1, 1500.0)]
    signal("scan", [inp(src, latch(amount_env, trend, level, hold_off)) for src, trend, level, hold_off in triggers],
           chain=UNIT)
    front = signal("scanFront", [inp(src, latch(front_env, trend, level, hold_off))
                                 for src, trend, level, hold_off in triggers])
    for tgt in front_targets(fx, scan_programs):
        out.append(route(front, tgt[0], -rest, component=tgt[1]))
    for prog in scan_programs:
        roles = fx[prog]
        out.append(route("signal.scan", op_target(prog, roles, "scan_amount", "constant", "constant"), 1.0))
        out.append(route("signal.scan", op_target(prog, roles, "scan_edge_gain", "constant", "constant"), 4.0))
    if "spark_gain" in fx.get("rw_spark", {}):
        out.append(route("signal.synth", op_target("rw_spark", fx["rw_spark"], "spark_gain", "constant",
                                                   "constant"), 3.0))   # the synth's light in the sparks
    # what the scan touches that has no scan ops -- the trees (masked: ops would cost in the depth prepass) --
    # crossfades its colour ramp to the spectrum, bottom to top, by the same amount (ADR-900's Replace depth). (The
    # clouds' ramp is the palette's, which replaces after the routes: the clouds flare through `cloud_glow` instead.)
    spectrum4 = [(0.90, 0.14, 0.05), (0.95, 0.80, 0.05), (0.10, 0.75, 0.30), (0.25, 0.30, 1.00)]
    pastel4 = [tuple(0.55 * c + 0.45 for c in col) for col in spectrum4]
    one = signal("one", [{"constant": 1.0}])
    for prog, role, cols in [(p_, "canopy_ramp", spectrum4) for p_ in fx if "canopy_ramp" in fx[p_]]:
        if role not in fx.get(prog, {}):
            continue
        for k, field in enumerate(("constant", "constant2", "constant3", "constant4")):
            for c in range(3):
                out.append(route(one, op_target(prog, fx[prog], role, "oklabRamp", field), cols[k][c],
                                 component=c, op="replace", depthSource="signal.scan", depthMin=0.0, depthMax=1.0))
    if "cloud_glow" in fx.get("rw_cloud", {}):
        hit(op_target("rw_cloud", fx["rw_cloud"], "cloud_glow", "constant", "constant"), 0.6)
    hit("env/sky/sunGlow", 1.2)
    hit("post/bloom/intensity", 0.35)
    hit("post/grade/saturation", 0.25)
    if "cloud_glow" in fx.get("rw_cloud", {}):
        out.append(route("signal.synth", op_target("rw_cloud", fx["rw_cloud"], "cloud_glow", "constant", "constant"),
                         0.3))
    out += sky_routes(fx)
    return out


SKY_BREATH = "skyBreath"
# a bass note's breath (29 sections 12-13): at its peak on the note's own frame -- no attack: the owner, "the animation
# time it takes to reach that size ... makes it feel a bit laggy" -- then a fall over about 0.4 s; a re-trigger within
# 130 ms (the same note) latched out (ADR-1574)
NOTE_CHAIN = {"envelope": "linearfall", "envelopeFallPerSecond": 2.4, "attackMs": 0.0, "envelopeRefractoryMs": 130.0,
              "envelopeRearm": 0.05}   # build_world's constant field the clouds' scale effector reads (its strength is routed)


def sky_routes(fx):
    """Gate 7 (the owner: "adding a bit of audio reacivity to the sky - pulsating clouds maybe, splashes of color";
    28-twirl-sky-sun.md section 4).
    - the clouds breathe with the bass: every puff swells about its own centre (a scale effector on a constant field
      whose strength the bass body drives) and brightens with it;
    - splashes of colour: on each clap (the drum stem's 2-6 kHz band) patches of saturated colour bloom across the
      clouds and fade within about a beat; each phrase start (bars 1 + 4k) blooms bigger and fades over two beats.
      Patches, never the whole sky (the owner's rule at gate 2: no world sweep)."""
    # the clouds breathe on the bass's notes, straight from the stem's onsets (`signal.bassNote`: unclamped, as the
    # macro's 0..1 is not); with no bass stem, the swell (ADR-1602: `breath`, which replaced a route pair per target)
    roles = fx.get("rw_cloud", {})
    glow = op_target("rw_cloud", roles, "cloud_glow", "constant", "constant") if "cloud_glow" in roles else None
    out = []
    breath = signal("breath", [inp("signal.bassNote", gate="stem.bass.active"), inp("signal.swell")])
    for tgt, amt_ in [("field/%s/strength" % SKY_BREATH, 1.0)] + ([(glow, 0.3)] if glow else []):
        out.append(route(breath, tgt, amt_))
    if "splash_amount" not in roles:
        return out
    amt = op_target("rw_cloud", roles, "splash_amount", "constant", "constant")
    # (smoothed for the owner's "cloud color popping", 29 section 7: the claps' splashes bloom over 150 ms and fade
    # over a beat and a half, not 8 ms and 420 ms; the phrase's blooms over 400 ms)
    out.append(route(stem_signal("splash", "drums", "highMid", "response.onsetEnv",
                                 {"offset": -1.3, "gain": 2.5, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                                  "attackMs": 150.0, "decayMs": 750.0}, {"attackMs": 150.0, "decayMs": 700.0}),
                     amt, 0.6))
    out.append(route("beat.phrasePulse", amt, 0.6, {"envelope": "linearfall", "envelopeFallPerSecond": 0.7,
                                                      "attackMs": 400.0}))
    out.append(route("beat.phrase", op_target("rw_cloud", roles, "splash_hue", "palette", "value"), 1.0, op="replace"))
    shift = op_target("rw_cloud", roles, "splash_shift", "constant", "constant")
    out.append(route("beat.phrase", shift, 900.0, op="replace", component=0))
    out.append(route("beat.bar", shift, 500.0, op="replace", component=2))
    return out
