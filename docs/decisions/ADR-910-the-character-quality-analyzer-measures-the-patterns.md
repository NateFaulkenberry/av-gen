# ADR-910: The character quality analyzer measures the patterns a viewer reads as a mechanism

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-826 (the character quality analyzer); the GV3 character audit (`measure.py`, `slopes.py`)
**Implemented by:** `CharacterQualityRecorder` and `toJson` (`src/entity/character_quality.{hpp,cpp}`); the
`avgen_character_quality` tool reports them unchanged
**Tests:** `tests/unit/test_character_quality.cpp` (four hand-built cases for the new metrics, each with a
control); the regression gates for ADR-907, 908 and 909 read these metrics

## Context

The GV3 character audit measured the cast's patterns by hand, with a Python script over a cast trace:
- the longest still stretch;
- stops walked out of the way they were walked into;
- A->B->A revisits;
- yaw turned below 0.3 m/s;
- the turn radius while moving;
- the slope under standing bodies, and time facing uphill.

None of these was a number the engine computed, so none could be a test, and ADR-907 to 909 each claim
to move one of them. A claim about behaviour that no number can contradict is the shape of defect this
repository keeps shipping (`docs/testing.md`, "Twelve ways a green suite has lied").

## Decision

The ADR-826 recorder gains three groups. Each is argued in the source and pinned by its own threshold
in the report.

- **Behaviour.**
  - `stillFraction` and `longestStillSeconds`: measured horizontal speed under `stillSpeed`, 0.1 m/s,
    the audit's "still".
  - `stops`: still stretches of at least `stopSeconds`, 0.25 s, so a corner's dip is not a stop.
  - `measured`: stops with a heading in and out, each taken over `headingMetres` (1.5 m) of
    straight-line travel.
  - `reversals` (out more than 150 degrees from the way in) and `turnsOver90`.
  - `revisits`: a stop within 4 m of the stop before last (A->B->A).
- **Motion.**
  - `turning.yawDegrees`, and `pivotYawDegrees` / `pivotYawFraction`: yaw turned while slower than
    0.3 m/s.
  - `turnRadiusMedian` and `turnRadiusP10`: speed over turn rate, sampled while faster than 0.5 m/s on
    both ends of the step and turning faster than 10 deg/s.
- **Ground.** Over standing seconds on a surface the body reported (`state().groundNormal` while
  `hasGroundPlane`):
  - `slopeMeanDegrees` and `slopeMaxDegrees`;
  - `steepSeconds` (steeper than 12 degrees), and `facingUphillSeconds` (within 45 degrees of
    straight uphill on such ground).

The facing is `locomotion().yaw`, and the surface is the one `ground` resolved the body against, not a
fresh terrain query at a point that could disagree with what the body is drawn standing on. The
plain-struct door (`CharacterSample`) carries the new facts, so each metric is tested by hand-built
bodies:
- walked in and out of a stop at 180, 120 and 160 degrees;
- stopped at A, B and back at A (and at A, B, C);
- turned on the spot, against one that walked a 4 m circle;
- stood on a 20-degree slope facing up, across and down it, and on 8 degrees.

Individual metrics, never a score: the schema test's ban on aggregate keys covers the new keys too.

## Consequences

- **ADR-907 to 909 have regression gates** that read these numbers off real behaviours:
  `test_forward_wander.cpp`, `test_walk_through_turns.cpp` and `test_decider_habits.cpp`.
- **`avgen_character_quality` reports the new metrics** for any scene or project with no change to the
  tool, so a GV3 iteration can be checked for the owner's patterns without a script.
- **Measured on the films** (the stream's evidence for ADR-907 to 909). Each film is simulated for
  226 s at 60 fps, the render's own step, through the real engine: the analyzer's numbers come from
  `avgen_character_quality`, the speed spread and path grades from `avgen_cast_trace` at 20 Hz through
  the audit's `measure.py`. **Before** is main at `808f32e4` with this ADR's recorder added and
  nothing else; **after** is this branch; **tuned** is this branch on a scratch copy of GV3 with the
  settings the stream report recommends (GV3's own files are untouched). Cells read before → after
  (→ tuned). horse-11 is abducted at 170 s, and the staging owns it from then.

**GV2-multicam: aliens (before → after)**

| | longest still | still | stops (reversals) | A→B→A revisits | yaw turned on the spot | turn radius (median) | speed p10/med/p90 (trace) |
|---|---|---|---|---|---|---|---|
| rook | 3.7 s → 8.1 s | 16% → 21% | 21 (4) → 16 (1) | 0 → 0 | 25% → 26% | 0.9 m → 0.9 m | 1.74/3.07/3.07 → 1.97/3.07/3.57 |
| tide | 30.5 s → 16.3 s | 57% → 56% | 22 (1) → 24 (2) | 2 → 2 | 32% → 36% | 0.7 m → 0.7 m | 0.82/3.06/3.06 → 0.78/3.06/3.06 |
| sage | 97.9 s → 49.0 s | 65% → 56% | 16 (6) → 15 (1) | 1 → 0 | 59% → 53% | 0.5 m → 0.6 m | 1.10/3.06/3.06 → 1.44/3.06/3.06 |
| ember | 9.5 s → 17.7 s | 44% → 36% | 33 (4) → 21 (0) | 5 → 0 | 32% → 26% | 0.7 m → 0.7 m | 1.07/3.07/3.07 → 1.57/3.07/3.07 |
| vane | 8.5 s → 27.2 s | 38% → 53% | 16 (2) → 12 (3) | 0 → 0 | 26% → 23% | 0.7 m → 0.7 m | 1.80/3.07/3.07 → 1.70/3.07/3.58 |

**GV2-multicam: animals (before → after)**

| | stops (reversals, turns >90°) | A→B→A | yaw on the spot | turn radius | standing on >12° | facing uphill | path >15° (trace) |
|---|---|---|---|---|---|---|---|
| bull-1 | 26 (0, 9) → 25 (0, 5) | 2 → 2 | 28% → 25% | 1.3 m → 1.5 m | 43 s → 4 s | 6 s → 0 s | 23% → 11% |
| bull-10 | 28 (0, 11) → 25 (0, 0) | 1 → 0 | 28% → 4% | 1.4 m → 1.7 m | 37 s → 0 s | 0 s → 0 s | 11% → 0% |
| bull-18 | 21 (0, 8) → 23 (0, 1) | 2 → 0 | 34% → 9% | 1.2 m → 1.5 m | 30 s → 0 s | 4 s → 0 s | 13% → 0% |
| bull-21 | 24 (1, 12) → 27 (0, 0) | 3 → 0 | 33% → 4% | 1.2 m → 1.5 m | 11 s → 1 s | 0 s → 0 s | 9% → 0% |
| cow-12 | 17 (2, 8) → 19 (0, 8) | 2 → 6 | 40% → 34% | 0.8 m → 0.9 m | 55 s → 45 s | 7 s → 1 s | 58% → 9% |
| cow-19 | 22 (5, 13) → 22 (0, 2) | 0 → 1 | 37% → 13% | 0.7 m → 0.8 m | 3 s → 0 s | 0 s → 0 s | 0% → 0% |
| cow-23 | 23 (5, 13) → 25 (0, 5) | 2 → 2 | 40% → 27% | 0.7 m → 0.8 m | 52 s → 60 s | 14 s → 24 s | 75% → 69% |
| cow-3 | 23 (6, 12) → 24 (0, 1) | 2 → 0 | 38% → 11% | 0.7 m → 0.8 m | 6 s → 0 s | 5 s → 0 s | 3% → 0% |
| horse-11 | 24 (3, 12) → 21 (0, 0) | 3 → 0 | 34% → 7% | 1.0 m → 1.2 m | 0 s → 0 s | 0 s → 0 s | 0% → 0% |
| horse-2 | 27 (3, 13) → 26 (0, 12) | 5 → 18 | 35% → 28% | 0.7 m → 0.8 m | 80 s → 70 s | 20 s → 14 s | 54% → 15% |
| horse-20 | 24 (1, 8) → 23 (0, 2) | 3 → 1 | 29% → 21% | 0.7 m → 0.9 m | 71 s → 3 s | 16 s → 0 s | 30% → 3% |
| horse-22 | 22 (2, 7) → 23 (0, 8) | 1 → 3 | 34% → 35% | 1.0 m → 1.2 m | 76 s → 67 s | 20 s → 0 s | 51% → 10% |

**GV3: aliens (before → after → tuned)**

| | longest still | still | stops (reversals) | A→B→A revisits | yaw turned on the spot | turn radius (median) | speed p10/med/p90 (trace) |
|---|---|---|---|---|---|---|---|
| rook | 26.2 s → 6.1 s → 4.3 s | 26% → 16% → 18% | 15 (0) → 17 (3) → 18 (1) | 1 → 1 → 0 | 34% → 23% → 21% | 0.8 m → 0.8 m → 1.6 m | 0.47/3.07/3.07 → 2.01/3.07/3.07 → 2.41/3.07/3.73 |
| tide | 12.8 s → 12.8 s → 3.9 s | 38% → 50% → 21% | 22 (1) → 24 (1) → 16 (0) | 4 → 2 → 0 | 24% → 32% → 14% | 0.6 m → 1.2 m → 1.6 m | 0.96/3.06/3.06 → 0.83/3.06/3.06 → 1.63/3.04/3.39 |
| sage | 67.3 s → 84.7 s → 26.1 s | 45% → 79% → 54% | 23 (16) → 7 (1) → 12 (5) | 12 → 0 → 3 | 47% → 51% → 51% | 0.7 m → 0.6 m → 5.2 m | 1.17/3.06/3.06 → 1.45/3.06/3.06 → 1.83/3.06/3.06 |
| ember | 9.7 s → 9.1 s → 7.2 s | 31% → 23% → 21% | 25 (8) → 22 (7) → 22 (0) | 5 → 4 → 0 | 22% → 19% → 9% | 0.8 m → 1.0 m → 1.6 m | 1.14/3.07/3.07 → 1.23/3.07/3.07 → 1.62/3.07/3.76 |
| vane | 22.3 s → 22.5 s → 4.2 s | 37% → 43% → 20% | 15 (1) → 16 (4) → 14 (0) | 0 → 1 → 0 | 30% → 23% → 13% | 0.7 m → 0.7 m → 1.6 m | 1.90/3.07/3.07 → 1.29/3.07/3.07 → 2.47/3.03/3.38 |

**GV3: animals (before → after → tuned)**

| | stops (reversals, turns >90°) | A→B→A | yaw on the spot | turn radius | standing on >12° | facing uphill | path >15° (trace) |
|---|---|---|---|---|---|---|---|
| bull-1 | 25 (0, 8) → 25 (0, 3) → 24 (0, 1) | 0 → 2 → 0 | 27% → 14% → 18% | 1.3 m → 1.5 m → 2.0 m | 43 s → 4 s → 4 s | 6 s → 0 s → 0 s | 22% → 12% → 11% |
| bull-10 | 28 (0, 12) → 25 (0, 0) → 26 (0, 0) | 1 → 0 → 0 | 29% → 4% → 1% | 1.4 m → 1.7 m → 2.0 m | 37 s → 0 s → 0 s | 0 s → 0 s → 0 s | 11% → 0% → 0% |
| bull-18 | 21 (0, 9) → 22 (0, 1) → 21 (0, 0) | 2 → 0 → 0 | 32% → 11% → 7% | 1.2 m → 1.5 m → 2.0 m | 30 s → 0 s → 0 s | 4 s → 0 s → 0 s | 13% → 0% → 0% |
| bull-21 | 24 (1, 12) → 27 (0, 0) → 25 (0, 2) | 3 → 0 → 2 | 33% → 4% → 16% | 1.2 m → 1.5 m → 2.0 m | 11 s → 1 s → 1 s | 0 s → 0 s → 0 s | 9% → 0% → 0% |
| cow-12 | 16 (3, 9) → 19 (0, 7) → 20 (0, 0) | 2 → 6 → 0 | 41% → 34% → 7% | 0.8 m → 0.9 m → 1.8 m | 56 s → 53 s → 0 s | 8 s → 1 s → 0 s | 62% → 10% → 0% |
| cow-19 | 22 (5, 13) → 24 (0, 1) → 22 (0, 0) | 0 → 0 → 0 | 37% → 10% → 11% | 0.7 m → 0.9 m → 1.8 m | 3 s → 0 s → 0 s | 0 s → 0 s → 0 s | 0% → 0% → 0% |
| cow-23 | 23 (5, 14) → 24 (0, 7) → 24 (0, 0) | 2 → 3 → 0 | 41% → 29% → 7% | 0.7 m → 0.8 m → 1.8 m | 54 s → 59 s → 0 s | 14 s → 20 s → 0 s | 74% → 44% → 0% |
| cow-3 | 22 (5, 11) → 25 (0, 0) → 23 (0, 0) | 1 → 0 → 0 | 37% → 6% → 4% | 0.8 m → 0.8 m → 1.8 m | 6 s → 0 s → 0 s | 5 s → 0 s → 0 s | 3% → 0% → 0% |
| horse-11 | 24 (2, 11) → 21 (0, 1) → 22 (0, 0) | 3 → 0 → 0 | 32% → 10% → 3% | 1.0 m → 1.2 m → 2.2 m | 0 s → 0 s → 0 s | 0 s → 0 s → 0 s | 0% → 0% → 0% |
| horse-2 | 26 (3, 14) → 24 (0, 11) → 21 (0, 0) | 5 → 12 → 0 | 35% → 29% → 4% | 0.7 m → 0.8 m → 2.2 m | 83 s → 78 s → 0 s | 23 s → 15 s → 0 s | 49% → 27% → 0% |
| horse-20 | 24 (1, 8) → 23 (0, 2) → 22 (0, 0) | 3 → 1 → 0 | 27% → 17% → 16% | 0.7 m → 0.9 m → 2.2 m | 74 s → 3 s → 0 s | 16 s → 0 s → 0 s | 29% → 3% → 0% |
| horse-22 | 21 (2, 8) → 20 (0, 4) → 21 (0, 0) | 1 → 1 → 0 | 36% → 29% → 4% | 1.0 m → 1.2 m → 2.2 m | 82 s → 72 s → 0 s | 20 s → 17 s → 0 s | 52% → 28% → 0% |


What the tables say, and what they do not:

- **The animals' reversals are gone, in both films, with nothing authored**: GV2-multicam's 28 and
  GV3's 27 (the owner's walk, stop, turn round, walk back) are 0 in both. Turns over 90 degrees at a
  stop fell by two thirds; the yaw turned on the spot fell for every animal on open ground (to 4-13%
  from 28-41%). The ones still turning on the spot are the flank animals, whose way ahead is often a
  slope they will not climb.
- **The default slope limit halves the standing on steep ground** (GV2 463 s -> 250 s, GV3 480 s ->
  270 s), and the animals that were placed on the valley floor stop standing on it at all. The five
  GV3 animals anchored on 17-25 degree flanks cannot be fixed by a destination rule -- a territory
  that is all hillside offers only hillside -- which is why the tuned column re-homes them onto the
  coordinator's two flat meadows, and there they stand on steep ground for 0 s.
- **The aliens' A->B->A revisits fall** (GV2 8 -> 2, GV3 22 -> 8) with nothing authored. Their
  longest stands do not fall by themselves: GV2-multicam's file is frozen and cannot opt in to
  `maxStillSeconds`, and there the loop veto turns some walks back into waits (vane 8.5 s -> 27.2 s,
  ADR-909). With the recommended settings (tuned) four of the five aliens' longest stands are 4-7 s.
  Sage's 26 s in this column is the case ADR-909 §3a (a restless decider's walk of its own) now
  covers; the column predates it, and the re-measurement follows.
- **Speed now varies** where a scene asks (`speedRange`): the tuned aliens' paces spread over about
  a fifth either side of their walk, and their reactions hurry.
