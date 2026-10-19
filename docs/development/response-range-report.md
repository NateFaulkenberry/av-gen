# Response range: the levels on real, mastered music (ADR-1510)

Branch `eng/response-range`, from main 99d45f13, 2026-10-19. The owner: "The engine's smoothed bass, level and
sustain signals stay pinned - fix this."

Media: `~/Desktop/av-gen-review/71-response-range/` (00 tables, 01-04 plots, `stills/` before/after sheets,
`clips/` side-by-side clips).

## 1. The defect and its cause

`response.bass`, `level`, `sustain`, `levelSlow` and `intensity` sat at 1 on 80-98% of the frames of Feline Footwear,
Trench and Hyperborea at the default Response controls (the coordinator's `--sonic-trace` table, reproduced exactly).

The levels were read on a fixed window: the analyser's band power in dBFS (a full-scale sine is 0 dB), -60 reads 0,
-12 reads 1, Sensitivity moves the floor +-18 dB. A temporary dump of the analyser's `levelDb` and `bassDb` showed
where mastered music sits:

| track | levelDb p10 / p50 / p90 | bassDb p10 / p50 / p90 |
|---|---|---|
| Feline Footwear | -15.2 / -8.8 / -5.1 | -26.7 / -11.9 / -5.5 |
| Trench | -13.5 / -9.8 / -7.2 | -22.4 / -11.6 / -7.7 |
| Hyperborea | -17.8 / -7.1 / -4.1 | -33.7 / -9.3 / -5.5 |

So the music lives above the window's top, and lowering Sensitivity only slid the window part of the way. Two further
facts shaped the fix:

- **The window was also far too wide.** The followed level moves about 6 dB between p10 and p90 on these masters,
  and the 3 s and 12 s averages about 2 dB outside the breakdowns. A correctly placed 48 dB window would still have
  used a fifth of the range. The Sonic Abstract scenes' `-44 / 42` override (Sonic Live, Digital Mosh Live and THE
  RIFT LIVE too) topped out at -2 dBFS, read 0.85 +- 0.05 and was as narrow in effect.
- **Nothing disagreed.** The response tests used a moderate synthetic kit that sits inside the window, and the
  scenes had been tuned against the pinned values (docs/testing.md, entry 43).

## 2. The fix

ADR-1510. One loudness reference, per-signal windows below it, all inside `ResponseModel::step` (no new subsystem):

- **Reference:** the gated level's power over 3 s; the plain average of what has been heard until 3 s has been
  (the first 6 s with no lag on top), then followed up in 2 s and down over 60 s; silence below -60 dBFS does not
  move it; never below -40 dBFS.
- **Followers:** bass and level in dB, with releases of 1.2 and 1.0 s (were 0.20 and 0.25 s) so the body holds
  between two hits; levelSlow (3 s) and intensity (12 s) as power; sustain as before (the instant level less its
  percussive share).
- **Windows** (1 at reference + top, 0 a span below): bass 6.5 / 15 dB, level 6.5 / 12, sustain 4 / 9, levelSlow
  1.5 / 6, intensity 1.5 / 6. Project-settable (`sonic.response.windows`).
- **Controls:** Sensitivity moves a window by 0.375 of its span and curves it (the old proportion); Transients,
  Sustain, Attack, Release unchanged; Input gain no longer scales the response levels (it still sets the meter, the
  gate, the hits and the Sonic Character). Panel tooltips and LIVE-QUICKSTART updated.
- `floorDb`, `rangeDb`, `sensitivityDb` retired, warned and ignored; the -44 / 42 overrides stripped from 12 projects
  and their generators.

**Alternatives measured and rejected:** a retuned fixed window (one input level only; no span suits both Trench's 6 dB
and Hyperborea's 14 dB), mean and deviation per signal (skewed by the quiet tails, slow levels at 0.45-0.65),
stochastic quantile trackers (collapse, cannot climb out of a fade-in), fast floor and ceiling followers (pump).

**A correction made on the way:** the first version kept the 0.20 / 0.25 s releases. Inside a 10-12 dB window that
made bass and level fall to 0 and rise to 0.8 on every beat (Feline bass ripple 0.70, above kickEnv's 0.59): a second
kick envelope. The coordinator caught it in the time-series plot. The releases now hold the body, and a ripple bound
is in the CI test and the checker.

## 3. Before and after

Default Response controls; every frame from 10 s in to 10 s before the end (`avgen --sonic-trace`). p10 / p50 / p90
(share of frames above 0.9; beat ripple = median over 0.5 s windows of the range inside each). The -12 and -24 dB
inputs are the same records attenuated before analysis.

| track | signal | before (main 99d45f13) | after | after, -12 dB input | after, -24 dB input |
|---|---|---|---|---|---|
| Feline | bass | 0.88 / 1.00 / 1.00 (88%; 0.09) | 0.52 / 0.68 / 0.82 (3%; 0.18) | 0.52 / 0.68 / 0.82 (3%; 0.18) | 0.52 / 0.68 / 0.81 (1%; 0.18) |
| Feline | level | 0.99 / 1.00 / 1.00 (98%; 0.00) | 0.54 / 0.65 / 0.77 (1%; 0.14) | 0.54 / 0.65 / 0.76 (1%; 0.14) | 0.54 / 0.65 / 0.76 (0%; 0.14) |
| Feline | sustain | 0.74 / 0.89 / 0.92 (29%; 0.01) | 0.40 / 0.52 / 0.58 (0%; 0.08) | 0.40 / 0.52 / 0.58 (0%; 0.08) | 0.39 / 0.51 / 0.58 (0%; 0.08) |
| Feline | levelSlow | 0.91 / 1.00 / 1.00 (93%; 0.00) | 0.64 / 0.72 / 0.77 (1%; 0.03) | 0.64 / 0.72 / 0.77 (0%; 0.03) | 0.64 / 0.72 / 0.76 (0%; 0.03) |
| Feline | intensity | 0.80 / 1.00 / 1.00 (81%; 0.00) | 0.63 / 0.71 / 0.73 (0%; 0.01) | 0.62 / 0.71 / 0.73 (0%; 0.01) | 0.58 / 0.71 / 0.73 (0%; 0.01) |
| Trench | bass | 0.91 / 1.00 / 1.00 (91%; 0.02) | 0.47 / 0.67 / 0.80 (2%; 0.14) | 0.47 / 0.67 / 0.80 (2%; 0.14) | 0.47 / 0.67 / 0.80 (2%; 0.14) |
| Trench | level | 1.00 / 1.00 / 1.00 (99%; 0.00) | 0.48 / 0.62 / 0.76 (2%; 0.13) | 0.48 / 0.62 / 0.76 (2%; 0.13) | 0.48 / 0.62 / 0.76 (2%; 0.13) |
| Trench | sustain | 0.87 / 0.90 / 0.94 (55%; 0.02) | 0.38 / 0.51 / 0.58 (0%; 0.07) | 0.38 / 0.51 / 0.58 (0%; 0.07) | 0.38 / 0.51 / 0.58 (0%; 0.07) |
| Trench | levelSlow | 0.97 / 1.00 / 1.00 (99%; 0.00) | 0.54 / 0.70 / 0.79 (0%; 0.02) | 0.54 / 0.70 / 0.79 (0%; 0.02) | 0.54 / 0.70 / 0.79 (0%; 0.02) |
| Trench | intensity | 0.89 / 0.99 / 1.00 (89%; 0.00) | 0.57 / 0.66 / 0.71 (0%; 0.01) | 0.57 / 0.66 / 0.71 (0%; 0.01) | 0.57 / 0.66 / 0.71 (0%; 0.01) |
| Hyperborea | bass | 0.81 / 1.00 / 1.00 (84%; 0.07) | 0.00 / 0.53 / 0.68 (2%; 0.11) | 0.00 / 0.53 / 0.68 (2%; 0.11) | 0.00 / 0.53 / 0.68 (2%; 0.11) |
| Hyperborea | level | 0.97 / 1.00 / 1.00 (96%; 0.01) | 0.28 / 0.53 / 0.67 (2%; 0.10) | 0.28 / 0.53 / 0.67 (2%; 0.10) | 0.26 / 0.52 / 0.67 (2%; 0.10) |
| Hyperborea | sustain | 0.81 / 0.91 / 0.96 (55%; 0.02) | 0.18 / 0.49 / 0.67 (1%; 0.06) | 0.18 / 0.49 / 0.67 (1%; 0.06) | 0.16 / 0.49 / 0.67 (1%; 0.06) |
| Hyperborea | levelSlow | 0.96 / 0.98 / 1.00 (95%; 0.00) | 0.19 / 0.65 / 0.81 (4%; 0.03) | 0.19 / 0.65 / 0.81 (4%; 0.03) | 0.13 / 0.65 / 0.81 (4%; 0.03) |
| Hyperborea | intensity | 0.85 / 0.97 / 0.99 (86%; 0.00) | 0.35 / 0.60 / 0.66 (0%; 0.01) | 0.35 / 0.60 / 0.66 (0%; 0.01) | 0.34 / 0.60 / 0.66 (0%; 0.01) |

- **Hits unchanged:** every column of the trace other than these five (the hits and their envelopes, `transient`,
  `flux`, `hatRate`, `melodic`, `pitch`, `sonic.*`, `notes.*`, `visual.*`) is bit-identical before and after on all
  three tracks. kickEnv ripple 0.51-0.59, hatEnv 0.62-0.67.
- **Sensitivity** (Feline, level p50): 0.30 at 0.2, 0.48 at 0.35, 0.65 at 0.5, 0.79 at 0.65, 0.91 at 0.8. Sustain 0
  turns the levels off; Transients does not move them.
- **Live:** a scene switch carries the reference (the live session is not restarted when the switcher opens another
  live project; pinned by `test_response_live.cpp`). Across a switch on the real tracks the reference moves under
  0.1 dB and the five levels' 3 s means by under 0.16 (Hyperborea's switch lands in its quiet passage). A switch in
  the synthetic song's breakdown: the carried session reads level 0.00 there; a restarted one would read 0.33.
  The first 6 s of a set reach 0.92 without holding at 1. Limitations: a sparse kick-only intro (Feline 0-10 s)
  lets its hits reach 1; a drop after a quiet intro (Hyperborea 16 s) holds levelSlow at 1 for about 4 s while the
  reference rises.
- **Palette `advance.change` (ADR-1421):** reads `sonic.energy`, `brightness`, `density` (bit-identical) and every
  palette's primary is `beat.phrasePulse`: it fires exactly as before.

Plots: `01-before-after-timeseries.png`, `02-first-20s.png`, `03-distributions.png`, `04-live-scene-switch.png`;
tables `00-distributions.txt` (the checker's output, main and this branch, 0 / -12 / -24 dB) and `00-table.md`.

## 4. The checks that keep it fixed

- `tests/support/mastered_song.hpp`: the drum kit's parts as a verse, a drumless breakdown and a louder drop with a
  held, kick-ducked sub, gained to -8 dBFS RMS (measured -8.3) and brickwall-limited at -0.3 dBFS; generated, no audio
  file committed.
- `tests/unit/test_response_range.cpp` (`[adr1510]`): on that song at 0 and -24 dB, each level keeps p90 - p10 >
  0.2, p50 0.2-0.85, under 25% of frames above 0.9, the breakdown 0.2 below the drop, and a ripple bound (bass and
  level < 0.2, sustain < 0.15, slow < 0.1); the -24 dB p50 and p90 within 0.05 of 0 dB; Sensitivity moves the levels
  and Sustain 0 turns them off; the gate and the reference floor. **Against main's response.cpp it fails 2 of 2
  cases, 28 of 77 assertions** (level above 0.9 on 77% of the frames); the ripple bound fails the 0.2 / 0.25 s
  releases (0.25-0.28).
- `tests/unit/test_response_live.cpp`: the switch carries the reference; the first bars; a second live project keeps
  the session.
- `tools/check_response_range.py`: runs `avgen --sonic-trace` over the three tracks (skips cleanly where absent;
  `--gain -24` attenuates them; `--avgen` picks a binary), prints p10 / p50 / p90, the share above 0.9 and the ripple
  beside kickEnv's, and exits 1 on a missed target. Main: 63 targets missed, exit 1. This branch: all met at 0, -12
  and -24 dB. Documented in docs/testing.md, entry 43 ("A moderate test signal hid a saturated one, and downstream
  tuning hid it again"), as the check to run before handing back audio-reactivity work.

## 5. Scene impact

**Inventory.** 42 example projects route the five levels (rows: references to each signal; `where`: routes,
interpret sources, the sonic scene's vocabulary):

| example | bass | level | sustain | levelSlow | intensity | where |
|---|---|---|---|---|---|---|
| bioluminescent/rift-live.json | 1 | 3 |  |  |  | sources 4 |
| cymatic-world/cymatic-world.json | 3 |  | 5 |  | 1 | routes 9 |
| digital-mosh/digital-mosh-live.json | 8 | 3 |  |  |  | sources 4, routes 7 |
| experiments/flow-meadow/flow-meadow.json | 1 |  |  |  |  | routes 1 |
| experiments/flow-storm/flow-storm.json | 1 |  | 1 |  |  | routes 2 |
| experiments/flow-streams/flow-streams.json | 1 |  |  |  |  | routes 1 |
| experiments/osc-flaps/osc-flaps.json | 1 |  | 1 |  | 1 | routes 3 |
| experiments/osc-membrane/osc-membrane.json | 2 |  | 1 |  | 1 | routes 4 |
| experiments/phase-wall/phase-wall.json | 1 | 1 | 1 |  | 1 | routes 4 |
| experiments/rd-bloom/rd-bloom.json | 1 |  | 1 |  |  | routes 2 |
| experiments/rd-pins/rd-pins.json |  |  | 1 |  |  | routes 1 |
| experiments/rd-relief/rd-relief.json | 1 |  | 1 |  |  | routes 2 |
| rd-pins/rd-pins.json | 1 |  | 1 |  |  | routes 2 |
| resonant-silk/resonant-silk.json | 1 |  | 1 |  | 1 | routes 3 |
| sonic-abstract/bit-ocean.json | 19 |  |  |  | 9 | routes 26, sonicScene 2 |
| sonic-abstract/cel-dream.json | 18 |  | 3 |  | 3 | routes 21, sonicScene 3 |
| sonic-abstract/chromatic-topography.json | 4 | 16 |  |  | 16 | routes 33, sonicScene 3 |
| sonic-abstract/color-geometry.json | 4 |  |  |  | 4 | routes 6, sonicScene 2 |
| sonic-abstract/digital-alpine.json | 4 | 13 |  |  | 7 | routes 21, sonicScene 3 |
| sonic-abstract/digital-ocean.json | 5 | 60 | 7 |  | 19 | sources 1, routes 86, sonicScene 4 |
| sonic-abstract/glitch-signal.json | 5 |  |  |  | 3 | routes 6, sonicScene 2 |
| sonic-abstract/neon-vector.json | 7 | 1 |  |  | 13 | routes 19, sonicScene 2 |
| sonic-abstract/organic-garden.json | 18 |  | 5 |  | 7 | routes 27, sonicScene 3 |
| sonic-abstract/sacred-flight.json | 45 |  |  |  | 10 | routes 53, sonicScene 2 |
| sonic-garden/sonic-live.json |  | 1 |  | 1 |  | sources 2 |
| sonic-vfx/abyssal-bloom.json | 3 |  | 3 |  |  | routes 4, sonicScene 2 |
| sonic-vfx/aurora-tundra.json | 3 |  | 6 |  |  | routes 7, sonicScene 2 |
| sonic-vfx/breathing-deep.json | 14 |  | 2 |  |  | sources 1, routes 13, sonicScene 2 |
| sonic-vfx/corrupted-cathedral.json | 5 |  | 2 |  |  | sources 1, routes 4, sonicScene 2 |
| sonic-vfx/cymatic-plate.json | 4 |  | 1 |  |  | sources 1, routes 3, sonicScene 1 |
| sonic-vfx/datascape.json | 3 | 4 | 3 |  |  | routes 7, sonicScene 3 |
| sonic-vfx/ember-forest.json | 10 |  | 4 |  |  | routes 12, sonicScene 2 |
| sonic-vfx/event-horizon.json | 5 |  | 2 |  |  | sources 1, routes 4, sonicScene 2 |
| sonic-vfx/feedback-mirror.json | 3 |  | 2 |  |  | routes 3, sonicScene 2 |
| sonic-vfx/ferrofluid-crown.json | 6 |  |  |  |  | routes 5, sonicScene 1 |
| sonic-vfx/lantern-lake.json | 4 |  | 5 |  |  | routes 7, sonicScene 2 |
| sonic-vfx/salt-flat-mirage.json | 3 |  | 1 |  |  | sources 1, routes 2, sonicScene 1 |
| sonic-vfx/silk-theatre.json | 3 |  | 2 |  |  | routes 3, sonicScene 2 |
| sonic-vfx/stellar-nursery.json | 2 |  | 2 |  |  | routes 2, sonicScene 2 |
| sonic-vfx/storm-cell.json | 5 |  | 3 |  |  | routes 6, sonicScene 2 |
| sonic-vfx/tesla-choir.json | 5 |  |  |  |  | routes 4, sonicScene 1 |
| waveform-dimension/waveform-dimension.json | 5 | 1 | 4 |  | 5 | routes 15 |

About 440 routes in all, almost all additive (`add`), 24 `multiply` on field strengths. What a route sees: on the
Sonic VFX and Resonator scenes (the default window) a level that sat at 1.0 now sits around 0.6 with the music's
shape on it; on the Sonic Abstract, Sonic Live, Digital Mosh Live and RIFT LIVE scenes (the -44 / 42 window) a level
that sat at 0.85 +- 0.05 now does the same.

**Check.** Stills of 39 scenes (all but RD Pins, RD Pins (R&D) and Phase Hall), main and this branch, at Feline 40 s
and 150 s and Hyperborea 70 s (its quiet passage): `stills/{sonic-abstract,sonic-vfx,resonator-generative,live}-after.jpg`.
In the loud passages the two are close in every family; nothing is blown out. In the quiet passage two scenes went
dead -- Sonic Live (its silence world) and Tesla Choir (a dark hall) -- and are retuned; glitch-signal,
corrupted-cathedral and organic-garden are calmer there but alive. `stills/retuned-scenes.jpg` shows main, after
the engine change, and retuned for the four retuned scenes.

**Retunes** (a commit each):

| scene | change | why |
|---|---|---|
| Digital Mosh Live (`examples/digital-mosh/build.py`) | heard gains x the Trench mean ratios (bass 1.21, level 1.38); presence knee 1 at 0.45, 0 at 0.05 (was 0.65 / 0.25) | gains were 1 / the old mean; the old knee sat below the new window. The arc's seek check at 45 s on Trench was 0.297, now above 0.3 |
| THE RIFT LIVE (`examples/bioluminescent/build.py`) | heard gains x 1.38 (level), 1.21 (bass) | the same calibration |
| Sonic Live (`tools/sonic_live_project.py`) | loud / loudSlow gains 1.22 / 1.12 (were 0.89 / 0.94); the silence mapping also hears `sonic.energy.slow` | the same calibration; a quiet passage read as silence |
| Tesla Choir (`tools/sonic_vfx/scenes/tesla_choir.py`) | the four bass routes get offset 0.35 | the hall's light was bass x 150 on a base of 20 |

Tests changed with them: `test_live_response.cpp` "Sensitivity drives the arc" now asks keen >= neutral (a steady
groove reads as a mix's body, which is full presence), and the quiet take reads as the loud one (within 25%) where
ADR-1361 had it read quieter. "Sonic Live: the garden's loudness and strikes are the Response's" now asks that
Sensitivity 0 turns the garden's loudness off and that the waiting world holds on a silent input (it held at
Sensitivity 0 before; silence is now the music's, not the slider's).

**Not edited (another agent is rebuilding them), for that agent:** Phase Hall (`experiments/phase-wall`) routes
sustain -> `grid/net/coupling` x3, bass -> `grid/net/frequency` x0.25, intensity -> `grid/net/phaseLag` x0.35 and
level -> `scene/bands/rotation` x0.15 integrated (the bands would turn about 40% slower); RD Pins and RD Pins (R&D)
route sustain -> `field/regimeDrift/strength` x0.5 (RD Pins also bass). Each now sees about 0.6 where it saw 1.0:
scale the amounts by about 1.5 to keep the old average, or add an offset of about 0.35 for a floor in quiet passages.

**Clips** (`clips/`, 18 s, main left, this branch right, 720p each, with audio): 01 Organic Garden (Sonic Abstract),
Feline 100-118 s; 02 Breathing Deep (Sonic VFX), Feline 120-138 s; 03 Waveform Dimension (Resonator), Feline
60-78 s; 04 Tesla Choir, Trench 80-98 s (the breakdown into the drop). Mean frame-to-frame change (160x90 grey) is
unchanged in each pair (4.74 / 4.68, 0.63 / 0.66, 4.12 / 4.07, 1.68 / 1.69): nothing twitches more. The visible
difference is modest; these scenes' level routes are a small share of their motion.

## 6. Limitations

- A track that holds one loudness (Feline, Trench) reads its levels around 0.5-0.7 for most of its length; the range
  appears at breakdowns, drops and the beat. That is the music, not the engine, but a scene that wants more swing
  must ask for it (a wider route depth, or Sensitivity).
- The reference forgets over about a minute: a breakdown longer than that slowly reads as normal, and a set that
  moves to a permanently quieter song takes one to two minutes to re-learn it.
- The first seconds of a piece are read against themselves (sparse intro hits can reach 1; a drop after a quiet intro
  pins levelSlow for about 4 s).
- Input gain no longer changes the response levels; a performer who used it to tame the visuals now uses
  Sensitivity or Sustain. The Live Scene Guide needs the same note (the coordinator's, after the merge).
- Scenes were checked on stills and four clips, not watched end to end; tuning judgement beyond "obviously wrong"
  (dead or blown out) is left to the art passes.

## 7. Final checks

After `cmake --preset release --fresh`, a clean full build (exit 0) and every commit above:

| check | exit | counts |
|---|---|---|
| `tools/ci/run-suite.sh cpu-assets` (under tools/gpu-lock.sh) | 0 | 4333 passed, 0 failed, 19 skipped, 1 expected failure (the shouldfail case), 4353 / 4353 ran |
| `tools/ci/run-suite.sh gpu` | 0 | 675 passed, 0 failed, 1 skipped, 676 / 676 ran |
| `python3 tools/review_examples.py --out build/resp/review` | 0 | 108 ok, 1 "not generated" (Biovalley - Macro: its generated scene is absent from this worktree, not a render failure) |
| `python3 tools/check_project_integrity.py` | 0 | 0 problems |
| `python3 tools/check_response_range.py` (0 / -12 / -24 dB) | 0 / 0 / 0 | every level within its targets; main: exit 1 |

The first cpu-assets run failed one case ("Sonic Live: the garden's loudness and strikes are the Response's": the
waiting world no longer holds at Sensitivity 0, by the silence change in section 5); the test was updated to the new
meaning and the suite rerun in full.
