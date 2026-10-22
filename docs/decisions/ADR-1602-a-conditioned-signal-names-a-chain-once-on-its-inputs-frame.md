# ADR-1602: A conditioned signal names a chain once, on its inputs' frame

- Status: Accepted (eng/living-prism-p0, The Living Prism P0-3)
- Extends ADR-900 (the route chain, the rack's second pass), ADR-1570 (the stem/mix fallback), ADR-1168/1573 (the
  control replay). Additive: no existing route or source changes meaning.
- Code:
  - `src/signals/conditioned_signal.{hpp,cpp}`: `ConditionedSignalSource`, kind `"signal"`, output `signal.<name>`;
  - `src/signals/source.cpp`: the rack's factory;
  - `src/app/engine.cpp`: `Engine::controlReplayNeeded()` is true whenever a signal source exists;
  - `src/app/sonic_trace_cli.cpp`: `signal.*` columns, and `--trace-finals` (every route target's final value, one
    column per component) so two projects' routing can be compared frame by frame;
  - `examples/rainbow-world/blender/music.py`, `build_world.py`: Rainbow World's control layer on signals.
- Tests:
  - `tests/integration/test_conditioned_signal.cpp` (`[adr1602]`): a one-input signal equals its route's chain bit for
    bit over 900 frames, for a chain with every stateful stage (delay, ramps, a latching envelope, a spring) and for a
    normalise/trend/peak-hold/integral chain, with and without a depth; in the engine, a route on the signal equals
    the same chain routed straight from the input on every frame; seek equals play (`[adr1573]`'s pattern; it fails
    with `AVGEN_NO_CONTROL_REPLAY=1`); firstActive follows a stem and falls back to the mix, and a stem the project
    lacks plays the mix throughout; sum/mean/max over the active inputs; event mode fires with the selected input;
    settings round-trip, through the rack, and malformed settings fail;
  - `tests/unit/test_rainbow_world_music.cpp`: the committed projects read every stem feature through a signal and no
    route reads a stem, the mix's response or a replaced knob.

## Problem

A conditioned musical signal -- "a bass breath", "a drum hit, else the mix's onset", "the swell that fires the scan" --
could not be named once and reused (02 §3.3, §12 P0-1). Scenes worked round it four ways, each with a cost:

- a route into a macro knob, read back as `macro.<k>`: one frame late by construction (`MacroSource` publishes the
  finals its routes wrote the frame before; 33 ms at the 30 fps live target);
- the same chain copied onto each target: N chain states computing one idea (Rainbow World's bass-note latch on
  `macros/swell`, the sky breath and the cloud glow);
- every stem route doubled by a main-mix twin with `depthSource: stem.<k>.active` (ADR-1570): 2 routes per idea;
- the routes generated in Python, so a scene's 405 routes encoded about 15 musical ideas.

`interpret` (`visual.*`) is same-frame but stateless (no envelope, latch, trend or integral) and cannot pick "the first
active of these".

## Decision

A source kind, `signal`. Each input is exactly a route's front half -- polarity, the `ProcessorChain` routes run (the
same code and the same state type), `amount`, ADR-900's depth with ADR-1570's `depthDefault` -- applied as an `add` onto
a target at 0, so a one-input signal publishes bit for bit what that route adds. A constant input is allowed. The inputs
combine:

- `firstActive` (the default): the first ACTIVE input's value and event -- the stem/mix fallback as one signal;
- `sum`, `mean`, `max`: over the active inputs (0 with none).

An input is active when its source is on the bus and its gate reads above 0.5. The gate is `gate` when given -- and a
declared gate the bus does not carry makes the input inactive, because what it gates is absent -- else the source's own
`<prefix>.active` (`stem.bass.onset` -> `stem.bass.active`) when the bus has one, else always. Every input's chain runs
every frame, active or not, as a twin route's did, so a switch never starts a chain cold. An optional output chain runs
after the combination; `event: true` publishes an event that fires with the selected input.

It updates in the rack's second pass (`readsTriggers`), after every publisher and in list order with envelope, random
and interpret, so routes, state triggers and effect signal-triggers see it on its inputs' frame; a signal reading
another (or an envelope) is listed after it. It registers no parameters.

It holds chain state, so a seek must rebuild it: `controlReplayNeeded()` includes any signal source, and ADR-1168's
control replay steps it with the rest of the rack from zero on the play's grid. (The ADR-901 signal replay's
checkpoints carry pure-in-time routes only; whenever a signal source exists the control replay runs after it and
rebuilds every route and source, so there is no second path to keep consistent.)

Alternatives (02 §12.2): same-frame macros (changes what a knob means to MIDI and states, and entangles the finals reset
with the rack order); `target: "signal:x"` routes (scatters one definition across routes); keep generating routes.

## Acceptance: Rainbow World ported

`music.py` and `build_world.py` now write 14 signals: `kick`, `glint`, `bassNote`, `swell`, `shimmer`, `clap`,
`synthStem`, `synth`, `lift`, `scan`, `scanFront`, `one`, `breath`, `splash`. The swell, synth, lift and scan knobs and
`macro.one` are gone (the live project has no macro source; the offline one keeps a bank with only `distort`); every
stem/mix twin pair is one route from a signal; the scan's front is one route per target instead of two.

| | before | after |
|---|---|---|
| live project routes | 405 (179 `macro.*`, 75 `stem.*`, 70 `response.*`, 1 `audio.*`) | **325** (247 `signal.*`; none on a stem, the mix's response or a replaced knob) |
| offline project routes | 633 | 553 |
| chain states for the bass-note latch | 3 (the swell knob, the sky breath, the cloud glow) | 1 (`signal.bassNote`) |
| `flat-chain` liveness warnings at load (02 §3.2's trend false positive) | 63 (mix) / 126 (stems) | 0 (the chains are in signals; see Consequences) |

Traces (`--sonic-trace --trace-finals`, 30 fps, the whole of Feline Footwear, 6,317 frames; the live project played from
the mix alone, and with the three stems):

- every replaced knob against its signal: `macro.X(k + 1) == signal.X(k)` to the trace's precision on every frame, for
  swell, synth, lift, scan and one, in both variants (max |difference| 0.0000), while `macro.X(k)` against
  `signal.X(k)` differs by up to 1.0: the signals are exactly the knobs, one frame earlier;
- every route target's final value (382 parameter components): 184/185 identical on every frame (the twins collapsed
  into signals), 178/173 exactly one frame earlier (fed only through a replaced knob), and the remaining 20/24 (fed by
  both, e.g. a bow's flow gain from `beat.pulse` and `signal.synth`) reconstruct as before plus each knob route's
  amount x (signal - knob) to a worst residual of 8.9e-5 (the trace prints 4 decimals). Nothing else moved.

## Consequences

- One frame earlier on every macro-routed effect (33 ms at the live target) and about 80 fewer routes in Rainbow World.
- The route liveness rules (ADR-902) look at routes, not at a signal's chains: a dead chain inside a signal is not
  reported yet. (The `flat-chain` warnings that disappeared were the trend false positive, not real findings.)
- A song-specific project re-sources Rainbow World's scan and bass by replacing the inputs of `signal.scan`,
  `signal.scanFront` and `signal.bassNote` (and pointing `signal.swell` and `signal.breath` at the notes alone) and keeps
  every route; the video hooks name the two scan signals (`amountSignal`, `frontSignal`) where they named
  `macros/scan`. The owner's Feline Footwear `make_project.py` (outside the repository) was updated this way (the
  previous script kept beside it as `make_project.py.bak-pre-signal`) and its project regenerated: 600 -> 586 routes;
  traced against the previous script's project on the same scene, the 11 scans start exactly one frame earlier
  (`macro.scan(k + 1) == signal.scan(k)`), the bass breaths on the sky field are identical frame for frame, 429 route
  targets are identical, 169 one frame earlier, and every mixed target reconstructs to < 1e-4 except one below.
- The one behavioural difference beyond the frame: while a stem exists but reads inactive, a firstActive signal plays
  its fallback alone, where the twin routes summed both (the grass's kick twin gave half a pulse: its stem route's
  depth went to -0.5). Feline's stems are active on every frame of the song; it shows only on the two frames after
  its end (210.5 s).
- The signal scope (P1-2) can show a named signal; P2's retirement of `EnvelopeSource` can point its users here.
