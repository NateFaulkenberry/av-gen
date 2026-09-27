# Phase 3, gv3-cast: the aliens, the animals and the UFO events

The stream's iteration log (brief §17). Each iteration names what changed, what was measured and
what was decided. Branch `gv3/cast` in `~/Documents/GitHub/av-gen-gv3-cast`, from `gv3/production`
`ecdc3cb4` (the first pass, the Phase 3 foundation, and main with set pieces). Engine: the second
shared build, `av-gen-engine-2` at `ec515c8b` (main, set pieces, and characters with the ADR-911
placement fix).

Evidence (clips, sheets, the measurement JSON) is in
`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/cast/`. The Critic session is `gv3-cast`, one
track per category (`idle`, `grounded`, `turns`, `ufo`).

**The owner's requirements this stream answers** (brief, the character animation assessment and
§9-12):
- aliens: purposeful, never long idles, never walk → stop → turn 180° → walk back;
- animals: grounded on flat valley ground, walking through curved turns;
- UFO events: five, varied, building to the riser's centrepiece.

## How the cast is made now

- **`tools/gv3/cast.py`** writes the cast into the scene (and into the project wherever the project
  restates a value, because the project's parameters are applied over the scene):
  - the aliens' habits, one set per alien: the longest it stands (`maxStillSeconds`), its range of
    paces, a 1.5 m turning circle, its two unused clips as activities, and its ears for E3-E5;
  - every animal's slope limit (10°) and walk-through turns (`turnRadius`, `pivotRadius`), and the
    five flank animals re-homed onto the two flat meadows;
  - the `scout`: the saucer's model at 0.6 scale, with its own beam, entity and staging actor;
  - the horse's Glow, dark at rest.
- **`tools/gv3/ufo.py`** is the generator's last step. It compiles `tools/gv3/ufo.plan.json` (E1-E5,
  a Director plan) with `avgen_cast_trace --plan --save-project`, takes into the generated project
  only what the plan produced, then traces the whole film and writes each set piece's measured
  moments to `build/gv3/ufo-beats.json`. See its docstring for why the save is not taken whole.

## Iteration 0: the baseline

The first pass's delivered final (iteration 0 in `04-iterations.md`) and the characters stream's
measurements on the same scene data (ADR-910's tables):

| | first pass (old engine) | first-pass data on the characters engine | characters' tuned scratch copy |
|---|---|---|---|
| sage's longest still | 67.3 s | 84.7 s | 14.8 s |
| rook / tide / ember / vane | 26.2 / 12.8 / 9.7 / 22.3 s | 6.1 / 12.8 / 9.1 / 22.5 s | 3.2 / 4.1 / 7.2 / 4.2 s |
| aliens' A→B→A revisits | 22 | 8 | 4 |
| animals' reversals | 27 | 0 | 0 |
| animals' turns over 90° at a stop | 129 | 37 | 3 |
| animals standing on ground over 12° | 480 s | 270 s | 5 s |
| animals facing uphill | 96 s | 53 s | 0 s |
| UFO events | one abduction, hand-written | — | — |

## Iteration 1: the two stream reports' recipes, as given

**What changed** (commit `c436d600`):
- `cast.py` applies `stream-reports/characters.md` "How GV3 should use this" exactly
  (`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/characters-work/make_variant.py`): per-alien
  `maxStillSeconds` (rook 7, tide 8, sage 10, ember 6, vane 9 s), interest pace ranges, the gait's
  1.5 m turning circle and `turnRate` 100, `eventSeconds` 20, the react pace [0.9, 1.1], the
  `inspect`/`tinker` clips (rook's and ember's roam activities), tide's `minRange` 14 and dwell 3.5,
  sage's grove weight 0.1 and duration 4 and graze `homeRadius` 30, vane's watch dwell 3.5 and
  `maxRange` 60, sage's and tide's run band 4.8/3.1; every animal `maxSlope` 10 and the per-species
  `turnRadius`/`pivotRadius` (horse 2.2/1.2, cow 1.8/1.0, bull 2.0/1.1); horse-2, horse-20 and
  horse-22 re-homed to (74, 13), (90, 26), (60, 0) and cow-12 and cow-23 to (-66, 0), (-78, 6), each
  with `homeRadius` 16 and `maxRange` 12 (the project's restated keys updated too).
- The `scout` (the saucer at 0.6 scale; its beam a copy of the saucer's as it runs, so the owner's
  0.42 m beam mouth is copied from the project's `particles/visitor-beam/extent`), its own actor.
- The hand-written `abduction` scenario, its `staging/abduction/*` project parameters and the horse
  light's timeline keys are gone; the horse's Glow stays, dark, for the plan's cues.
- `ufo.py` compiles `ufo.plan.json` (the set-pieces stream's validated plan, unchanged).

**Measured** (whole film, 226 s at 60 fps, `avgen_cast_trace` + `avgen_character_quality` on the
spliced project; figures from ADR-910's recorder):

| alien | longest still | still | stops (reversals, turns > 90°) | A→B→A | yaw on the spot | turn radius |
|---|---|---|---|---|---|---|
| rook | 3.2 s | 11% | 15 (1, 7) | 2 | 17% | 1.6 m |
| tide | 4.1 s | 18% | 15 (0, 3) | 1 | 16% | 1.5 m |
| sage | 10.3 s | 35% | 12 (1, 4) | 0 | 26% | 1.7 m |
| ember | 7.2 s | 25% | 29 (3, 11) | 4 | 26% | 1.6 m |
| vane | 4.2 s | 20% | 14 (0, 3) | 0 | 13% | 1.6 m |

Animals: 273 stops, **0 reversals, 5 turns over 90°** (bull-1 1, bull-10 2, bull-21 2), **5.2 s
standing on ground over 12°** (bull-1 3.7 s, bull-21 1.5 s, both left on their flanks by the
characters stream), **0 s facing uphill**; yaw turned on the spot 3-18% (the bulls highest). The
re-homed five stand on 1.4-5.5° at worst.

Set pieces, measured: E1 beam 13.900, sweep 15.033, depart 22.633 (bar 13); E2 cross 26.650; E3 beam
65.617, lift 66.967 (bar 37), bull-10 retired 71.883; **E4 failed**; E5 beam 170.350 (bar 93), lift
172.783, horse-11 retired 177.717, depart 177.733, the saucer held within 0.354 m.

**Found:**
1. **E4 cannot play where the plan put it.** Its region (centre (-55, 40), radius 30) holds only
   cow-19 after the re-homing; the scout waited in its hidden transit until cow-19 wandered in
   (110.4 s), came in, found no second animal within 25 m, and left without beaming. The re-homed
   cows graze as a pair 4-11 m apart round (-69, 6) (at 93.8 s: cow-12 (-64, 11), cow-23 (-70, 1)).
2. **Ember paces the river bank through the drop: walk → stop → 180° → walk back, eight times in
   40 s (176-216 s).** It heard E5's beam at 170.35 s and hurried toward it; the beam is across the
   river. The cause is in the engine: the action tier's route (`NavigatorPath::route`,
   `src/entity/action.cpp`) checks only that the goal is navigable and returns a straight line --
   `(void)from` -- so a goal on another connected piece of walkable ground (the nav grid reports 4)
   is "Ready". The body walks into the river to the wade limit, stalls, the move gives up, ADR-908
   walks it back onto the walkable set, and the reaction wins again, because an urgent option is
   exempt from ADR-909's loop and turn-back vetoes. The same trap made ember dither on that bank at
   22-52 s after roam targets across the water. **Reported for an engine fix** (check
   `NavGrid` regions in `NavigatorPath::route` and refuse, or route to the nearest reachable point).
   Worked round for GV3 in iteration 2 by not sending aliens across the river (below).
3. **A headless `--save-project` photographs the run.** The compile's save changed 745 more
   parameters (every registered value, rounded to float), wrote back the scene's value where the
   project's was not in effect (sage's `interest` dwell 1.2-3.4 s → 3-6 s), clamped
   `nodes/valley/water/rippleScale` 5.2 → 4.0, and dropped 9 unregistered parameters. That is why
   `ufo.py` takes only what the plan produced (`staging`, `directingPlans`, its markers and cue
   events) into the generated project.
4. **Sage's `interest` behaviour stops its feet** (it runs after the decider), 55% of the time in
   3-6 s spells, which chain past `maxStillSeconds`; sage's four longest stands (9-10 s) are these.

## Iteration 2: E4 on the meadow pair; the aliens kept on their own bank

**What changed** (commit `0ff69eea`):
- **E4** placed on the western meadow's two cows: region centre (-69, 6), radius 20 (the plan
  compiles with no finding; nominal: approach 93.871, beam 102.504, lift 103.871 (bar 57),
  depart 111.904).
- **The aliens' reactions split in two** (`REACTIONS` in `cast.py`): `beam` hears only E4 (radius
  80 m: go and see, approach 18 m); a new `centrepiece` hears E5 (radius 250 m) with `approach` 250,
  so every alien that hears it stops, faces the beam and watches for 6 s where it stands (the cautious
  sage steps back 10 m first). E3 is no longer heard: every alien is 250-330 m from it.
- **Sage**: `maxStillSeconds` 8; its `interest` glances 25% of the time for 1.2-3.4 s (scene and
  project).
- **The scout has a hero record** (the saucer's, scaled 0.6: radius 4.92 m, height 4.26 m,
  importance halved), so the Critic's adapter no longer assumes the saucer's 8.2 m.

**Measured** (whole film, 226 s at 60 fps: `python3 tools/gv3/ufo.py`, 21 min 55 s, log
`build/gv3/cast/iter2/ufo-run.log`; `avgen_character_quality`, `build/gv3/cast/iter2/quality.json`;
the scene and project that ran are beside them). Iteration 1's figures in brackets:

| alien | longest still | still | stops (reversals, turns > 90°) | A→B→A | yaw on the spot | turn radius | stuck |
|---|---|---|---|---|---|---|---|
| rook | **7.7 s** [3.2] | 21% [11] | 19 (0, 2) [15 (1, 7)] | 0 [2] | 21% [17] | 1.6 m | **6.7 s** [0.4] |
| tide | **8.4 s** [4.1] | 25% [18] | 18 (1, 5) [15 (0, 3)] | 0 [1] | 20% [16] | 1.6 m | **7.0 s** [1.9] |
| sage | 8.2 s [10.3] | 35% [35] | 14 (1, 5) [12 (1, 4)] | 0 [0] | 28% [26] | 1.6 m | 0.6 s [1.0] |
| ember | 7.2 s [7.2] | 27% [25] | 24 (1, 5) [29 (3, 11)] | 0 [4] | 16% [26] | 1.6 m | 3.3 s [2.6] |
| vane | 4.2 s [4.2] | 20% [20] | 14 (0, 3) [14 (0, 3)] | 0 [0] | 13% [13] | 1.6 m | 1.0 s [1.0] |

("Stuck" is ADR-910's: meaning to move faster than 0.3 m/s while moving slower than 0.05.)

Animals: 273 stops, **0 reversals, 6 turns over 90°** (bull-1 1, bull-10 2, bull-21 2, cow-23 1),
**5.2 s standing on ground over 12°** (bull-1 3.7 s, bull-21 1.5 s, unchanged), **0 s facing uphill**.

**All five set pieces played** (measured; each craft held within 0.35-0.42 m while beaming):

| | craft | approach | beam | sweep / lift | depart | taken |
|---|---|---|---|---|---|---|
| E1 survey | scout | 6.233 | 13.900 | sweep 15.033 | 22.633 | — |
| E2 flyby | saucer | cross 26.650 | — | — | 29.883 | — |
| E3 far lift | scout | 55.967 | 65.617 | 66.967 | 71.900 | bull-10 71.883 |
| **E4 river pair** | scout | 93.883 | **102.533** | **103.883** | 111.933 | **cow-23, cow-12 111.917** |
| E5 centrepiece | saucer | 148.317 | 170.350 | 172.783 | 177.733 | horse-11 177.717 |

E4 lands one or two frames after the plan's nominal moments (lift 103.883 against 103.871).

**The aliens' reactions** (facing within 5° of the craft, standing):
- **E4:** ember (17-22 m away) turned and watched 4.0 s (105.2-109.2 s); sage (31 m) stepped back to
  40 m and watched 4.1 s. Rook and tide (49-57 m) and vane (78-90 m) carried on.
- **E5:** ember watched 5.0 s (171.7-176.7 s), sage stepped back 8 m and watched 3.5 s, rook 2.9 s,
  tide 0.8 s (it had stood since 164.3 s for a roam look, so its `maxStillSeconds` of 8 walked it off
  at 172.7 s, before the watch's pose began), vane not at all (100 m away, facing off).
- **Ember no longer paces the bank through the drop:** stops turning 120° or more, 176-216 s: seven
  in iteration 1, none in iteration 2 (one at 192.5 s).

**Found:**
1. **A taken animal is only hidden: it keeps walking, invisible, and still blocks and is seen.**
   Rook "walked" in place for 5.25 s (205.65-210.85 s at (-66.2, -5.2)): the walk clip at 0.51 m/s,
   no travel, on flat dry meadow, with the invisible cow-23 3.0 m away and cow-12 4.3 m away. After
   E4 both cows graze on in the meadow the aliens roam until the film ends (cow-12 at (-63.9, -1.5)
   at 206 s, walking). The engine's `retire` step hides the body and hands it back to its own
   behaviours (`stage/staging.cpp`, `StepKind::Retire`); the crowd the bodies separate against and
   the perception index are built from every active entity, visible or not (`entity/entity.cpp`,
   the crowd build and `buildBodyIndex`). So a taken animal stays an obstacle and something to walk
   up to and look at. An engine defect, **reported to the coordinator**; nothing in the scene can
   remove a body at a moment only the set piece knows.
2. **Tide "walked" in place for 4.4 s** (211.80-216.25 s at (-36.1, -31.1), 0.37 m/s, no travel),
   on flat dry ground with no other body within 6 m. The move's own stuck clock (4 s without
   progress) ended it. Cause not found.
3. **Ember still stands on the river bank early in the film:** 4.9 s at 22.7 s and 7.2 s at 31.0 s,
   at (-54.6, 81.2), on a 26° bank at the water's edge. It stops dead from 2.97 m/s in one sample
   (a roam target across the river): iteration 1's finding 2, now from `roam` rather than a reaction.
   The navfix engine stream (ADR-932, 933) owns it.
4. **Sage's stands end at its cap:** its `graze` looks last 6 s (`dwell` 6) and it decides at
   0.6 Hz, so five stands run 7.2-8.2 s, each ended by `maxStillSeconds` 8.
5. **Rook's and tide's longest stills are regressions from iteration 1** (3.2 → 7.7 s, 4.1 → 8.4 s),
   but each is one of the episodes above (1, and tide's E5 watch cut short), not a pattern of long
   stands: rook's next longest is 4.2 s and tide's 4.5 s.

**Decided (iteration 3):** drop ember's and vane's bass reaction on the bounce (below); shorten
sage's `graze` look from 6 to 4.5 s; report finding 1. Findings 2 and 3 wait for the engine.

## The aliens float while walking (the coordinator's finding 1)

On iteration 1's trace, the drawn root's height above the engine's own ground (`WorldMap::height`,
probed through `tools/gv3/ground.py`), on dry ground, by speed:

| alien | bounce, stride | standing | walking 2.5-3.3 m/s, flat ground: mean / p90 / max | faster |
|---|---|---|---|---|
| ember | 0.32, 5.35 m | +0.007 | +0.180 / +0.354 / +0.452 m | max +0.711 |
| vane | 0.32, 5.35 m | +0.007 | +0.172 / +0.334 / +0.399 m | max +0.477 |
| rook | 0.22, 5.78 m | +0.005 | +0.056 / +0.109 / +0.121 m | max +0.279 |
| tide | 0.14, 5.76 m | +0.001 | +0.037 / +0.072 / +0.102 m | max +0.096 |

- It is one-sided, zero at rest and grows with the `liveliness` bounce: the stride bob (ADR-895's
  `0.5 (1 - cos 2φ) · bounce · min(speed / stride, 2)`, a rise from ground contact, written into the
  traced position as `state.position + motion.position`).
- **Rook and tide match that formula** (peaks 0.117 and 0.075 m at a walk). **Ember and vane rise
  about 2.4× what it gives** (0.18 m): something else doubles their bob.

**The control arms** (scratch copies of iteration 1's data in `build/gv3/cast/float/`, the film's
first 60 s traced; the same measurement, walking 0.1-3.3 m/s):

| arm | ember mean / p90 / max | vane | rook | tide | sage |
|---|---|---|---|---|---|
| `bobref`: as iteration 1 | +0.139 / +0.311 / +0.396 | +0.160 / +0.329 / +0.406 | +0.050 / +0.109 / +0.133 | +0.032 / +0.071 / +0.096 | +0.045 / +0.093 / +0.142 |
| `bob0`: every alien's `bounce` 0 | **+0.071 / +0.161 / +0.228** | **+0.083 / +0.172 / +0.223** | +0.003 / +0.009 / +0.036 | +0.003 / +0.009 / +0.055 | +0.005 / +0.036 / +0.051 |

(`bob0band`, the same with the run band written exactly, measured identically.)

- **The bob is the whole cause for rook, tide and sage:** with no bounce they sit on the ground to
  within the ground probe's error.
- **Ember and vane still rise 0.22 m with no bounce at all.** The extra is an entity reaction both
  carry from the multicam: `audio.bass -> liveliness/bounce`, depth 0.45, a 60 ms attack. It adds up
  to 0.45 to a bounce of 0.32 on every bass note, so their bob peaks at up to 2.4× what was authored.
- **Decided:** drop that reaction (`ALIEN_REACTIONS_DROPPED` in `cast.py`), keep the authored bounce.
  Iteration 3's trace checks it. The aliens still answer the music, through what they do rather
  than a lift off the ground: ember and vane keep `audio.rms` on their interest's weight and
  `music.drop` on how often they decide, rook keeps `audio.rms` on its roam, sage `music.drop` on its
  graze, and tide `audio.lowMid` on its sway (the brief's §3 asks for more reactivity, and its
  research list for none of the "everything pulses to the beat" kind).
- **What remains after the drop is ADR-895's authored bob**, whose peak at a 3.07 m/s walk is
  0.184 m for ember and vane, 0.117 m for rook, 0.075 m for tide and about 0.085 m for sage. The
  Critic's grounding rule (`critic/analyzers/entities.py`, `synthesis.py`) is the root's height over
  the ground: "floats" when more than 10% of an on-screen body's track is over 0.1 m, "medium" when
  the worst is over 0.2 m. It reads the root, not the feet, so it may still flag ember, vane and
  rook for the authored bob.

## For the other streams
- **gv3-cut** (framing and Song Mode peaks): `build/gv3/ufo-beats.json` after each generation; the
  measured beats are in iteration 2's table (E4 now plays: beam 102.533, lift 103.883, both cows
  taken at 111.917). The re-homed animals: three horses in the eastern meadow round (74, 13), two
  cows in the western one round (-69, 6). E4's pair graze 4-11 m apart there until the lift.
- **The Critic's adapter**: the trace's `atRetire` is taken after the retired body is dropped back to
  the ground (the horse 23 m up the beam reads 5.3 m); the adapter works round it.

## Status and next steps (checkpoint, 2026-09-27 13:00)
1. Iteration 3: the bass reaction dropped, sage's look 4.5 s; the whole-film trace and the ADR-910
   table; the float measured again on that trace (height above the ground while walking, per alien).
2. Clips through the GPU lock (960×540): `idle` 44-60 s, `ufo-e4` 95-118 s, `grounded` 18.9-26.3 s,
   `ufo-e5` 166-182 s. The "before" jobs, regenerated with the Critic's new adapter, are in session
   `gv3-cast` under `build/gv3/cast/critic/before2/`: idle `job_1a0e3a2ef6ddda6d0`, e4
   `job_1a0e3a2f166a5dbb7`, horse/grounded `job_1a0e3a2f362969799`, e5 `job_1a0e3a2f56977afcb`.
3. If the Critic still reads the aliens as floating after the drop, tell the coordinator with numbers.
4. Evidence to `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/cast/`; the final report with
   the measured E1-E5 beats.

The helper scripts (`cast_report.py`, `clips.sh`, `float.py`, `stops.py`, `where.py`, and this
session's `mstills.py`, `stuck.py`, `episode.py`, `reacts.py`, `pathmap.py`) are in
`build/gv3/cast/tools/` (not committed: `build/` is the stream's scratch).
