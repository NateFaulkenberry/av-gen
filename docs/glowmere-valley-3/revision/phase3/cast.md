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

## Iteration 2: E4 on the meadow pair; the aliens kept on their own bank (in progress)

**What changed** (this commit):
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

**Running at this checkpoint:** the iteration-2 whole-film trace (`python3 tools/gv3/ufo.py`, about
26 min under this load; log `build/gv3/cast/iter2/ufo-run.log`) and `avgen_character_quality`
(`build/gv3/cast/iter2/quality.json`).

## The aliens float while walking (the coordinator's finding 1): investigation so far

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
  about 2.4× what it gives** (0.18 m): something else doubles their bob. Not yet found.
- A control is running on scratch copies (`build/gv3/cast/float/bob0` with every alien's bounce 0,
  `bobref` unchanged; 60 s each) to prove the bob is the whole cause. Nothing is worked round in
  data.

## For the other streams
- **gv3-cut** (framing and Song Mode peaks): `build/gv3/ufo-beats.json` after each generation; the
  measured beats above (iteration 1), E4's to follow from iteration 2. The re-homed animals: three
  horses in the eastern meadow round (74, 13), two cows in the western one round (-69, 6).
- **The Critic's adapter**: the trace's `atRetire` is taken after the retired body is dropped back to
  the ground (the horse 23 m up the beam reads 5.3 m); the adapter works round it.

## Status and next steps (checkpoint, 2026-09-27 12:00)
1. Read iteration 2's trace: E4's beam and two lifts at bar 57, ember and the others no longer
   pacing, the ADR-910 table; iterate on anything over the bar (no alien still > 8 s unless
   watching, no reversals).
2. Finish the float investigation (the control arm) and report it with evidence.
3. Clips through the GPU lock (960×540): `idle` 44-60 s, `ufo-e4` 95-118 s, `grounded` 18.9-26.3 s,
   `ufo-e5` 166-182 s; the "before" clips are cut from the first pass's final and already judged
   (session `gv3-cast`: `job_1a0e37a89cff758a4`, `job_1a0e37abc8f5686b2`, `job_1a0e37b083f46e32d`,
   `job_1a0e37b26c7aa44a7`); regenerate their inputs with the Critic's new adapter first
   (INTEGRATION_GUIDE §6), then compare.
4. Evidence to `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/cast/`; the final report with
   the measured E1-E5 beats.
