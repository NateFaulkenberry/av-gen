# Characters stream (ADRs 907–910): the agent's final report

Reported on 2026-09-27. Branch `agent/characters`, final commit `66ab894e`, with main `a6598157`
merged in. **Not merged yet:** one CPU failure, #1712, exposes a latent ADR-911 follow-camera defect.
A fix agent is working on top of this branch. This is the agent's report, lightly trimmed.
The recipe and data are in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/characters-work/`:
`make_variant.py` (the exact tuned recipe), the meadow checks, `tables.py` and the measurement JSON.

## The owner's hard requirements, on GV3 with the recommended settings
- **Animals:**
  - 0 reversals (from 27);
  - 3 turns over 90° at a stop (from 129);
  - 5 s standing on steep ground (from 480 s);
  - 0 s facing uphill (from 96 s).
- **Aliens:** the longest still stretch is 3.2–7.2 s for four aliens and 14.8 s for sage (it was 67.3 s).

## What was built
- **ADR-907:**
  - `wander` draws destinations in a forward cone and leans toward home, with a leash;
  - a per-entity `maxSlope`, **12° by default**, with a fallback to the gentlest ground ahead and never uphill;
  - eased stops through `arrival`.
- **ADR-908:**
  - walk-through turns, with `turnRadius` on `wander` and the gait;
  - the gait's `turnRate` replaces the hard-coded 2.45 rad/s;
  - pivots only from rest, with a heading check along the way actually walked;
  - a `move` off the walkable set walks back onto it first;
  - single-clip animals play their clip while pivoting.
- **ADR-909:**
  - `maxStillSeconds` limits standing, with a restless `stroll` when nothing else walks;
  - the loop memory refuses A→B→A walks;
  - `holdPost` gains a `duration`;
  - `speedRange` on interests and reactions, and reactions hurry by urgency.
- **ADR-910:** the quality analyzer and its measurement tables.

## Where the controls are in the UI
Every control is a labelled parameter.
- **Parameters panel:** entity → `<name>/wander`, `/gait`, `/decide` or `/decide/<considerer>`. `entity/` now shows on the Intermediate layer.
- **World panel Inspector:** click the character, then the new section **"How <name> moves and behaves"**.
- **Examples:**
  - "turn radius (m, 0 = from speed and turn rate)";
  - "steepest ground it walks on (deg, 0 = any)";
  - "longest it stands still (s, 0 = no limit)";
  - "slowest pace (x walk speed)".

## Suites (on `66ab894e`'s code)
- **CPU:** 3,721 tests, 3,701 passed, 19 skipped, 1 failed, exit 8.
  - The failure is #1712, "a chase that rises over its character…".
  - A performance teleports rook 21.8 m down onto its mark at 90.00 s. The ADR-911 follow filter reads rook's height history across the jump, so the camera is 8.6 m above rook at 90.5 s.
  - It is not re-baselined, because that would hide the defect.
- **GPU:** 521 cases, 520 passed, 1 skipped.

## Changes to existing scenes
- **Every `wander` scene:**
  - the scenes: the GV2 family, glowmere-atmospherics, the tractor-beam labs, `_pre-defects`, the motion-matching labs and alien-wander;
  - forward-cone destinations, the 12° slope limit, eased stops, and walk-through turns.
- **Every decider scene** (GV2-multicam, GV3, the labs, the autonomy demo): the loop memory, the half-tolerance return, reactions hurrying (`urgentSpeed` 1.5), and the refuge walk.

| | before (main) | after |
|---|---|---|
| GV2-multicam animals: reversals | 28 | 0 |
| GV2-multicam animals: standing on steep ground | 463 s | 250 s |
| GV2-multicam aliens: A→B→A revisits | 8 | 2 |
| GV2-multicam: sage's longest stand | 97.9 s | 49.0 s |
| GV2-multicam: vane's longest stand | 8.5 s | 27.2 s |
| GV3 animals: reversals | 27 | 0 |
| GV3 animals: standing on steep ground | 480 s | 270 s |
| GV3 aliens: A→B→A revisits | 22 | 8 |

Vane's longer stand in GV2 is because the frozen file cannot set `maxStillSeconds`. GV3's aliens
improve once the scene opts in (below).

## Defects found, not fixed
- The #1712 follow-filter issue (being fixed).
- Decider considerers have no slope limit.
- Multi-leg `move` errands brake at every waypoint.
- `test_glowmere_multicam.cpp:639` compares depth to the wade depth with no margin.
- The audit's 19 m graze `homeRadius` for sage is too small; use 30.

## How GV3 should use this
Add these through `tools/gv3/cast.py`. Where the project already holds a key (`homeRadius`,
`maxRange`, `grove/weight`), update or delete it there too.

**Aliens:**
- **Gait, every alien:** `turnRadius` 1.5, `turnRate` 100.
- **Decider, every alien:** `mind.memory.eventSeconds` 20; `react` beam `speedRange` [0.9, 1.1]; clips `inspect`: `Take_from_floor` and `tinker`: `Button_push`.

| Alien | `maxStillSeconds` | interest `speedRange` | Other |
|---|---|---|---|
| rook | 7 | roam [0.9, 1.3] | roam `activity` inspect, `dwell` 2.4 |
| tide | 8 | roam [0.8, 1.2] | `minRange` 14, `dwell` 3.5, `runEnter`/`runExit` 4.8/3.1 |
| sage | 10 | graze [0.7, 1.05] | grove `weight` 0.1, `duration` 4; graze `homeRadius` 30; `runEnter`/`runExit` 4.8/3.1 |
| ember | 6 | roam [0.9, 1.35] | roam `activity` tinker, `dwell` 1.6 |
| vane | 9 | watch [0.8, 1.15] | watch `dwell` 3.5, `maxRange` 60 |

**The beam:** raise the `abduction/beam` event `radius` from 150 to 250.

**Animals:**
- `wander.maxSlope` 10 for every animal.

| Species | `wander.turnRadius` | `gait.pivotRadius` |
|---|---|---|
| horse | 2.2 | 1.2 |
| cow | 1.8 | 1.0 |
| bull | 2.0 | 1.1 |

**Re-home the flank animals onto the flat meadows,** each with `homeRadius` 16 and `maxRange` 12:
- the 5.1 ha meadow: horse-2 at (74, 13), horse-20 at (90, 26), horse-22 at (60, 0);
- the flat southern half of the 3.9 ha meadow: cow-12 at (−66, 0), cow-23 at (−78, 6). Its northern edge, by (−62, 36), is a bank.

**Resolution** does not matter, as long as previews and finals both render at 60 fps.
