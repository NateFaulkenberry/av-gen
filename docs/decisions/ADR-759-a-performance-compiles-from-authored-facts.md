# ADR-759: A scripted performance compiles to a `seq::Actor` from authored facts only

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-758 (the handoff), ADR-755/756 (Plan, validator, compiler); spec §23–§26, §30, §52
**Implemented by:** `src/directing/performance.*` (`beatCompilable`, `performanceStart`,
`compilePerformance`); the performance section of `validatePlan`; `compilePlan` (actors, computed
event markers, cues `on` plan events); `CharacterMark` in `SceneFacts`
**Tests:** `tests/unit/test_directing_performance.cpp`; the `rook_run_past_umbra` golden plan

## Decision

- **Beats compiled now:** `run_to`, `walk_to`, `run_past`, `walk_past`, `run`, `walk`, `hold`,
  `look_at` (spec §26's first set).
  - An action the character *has* but this build does not compile (`jump`, `land`, `fall`) is
    `UNSUPPORTED`, naming Slice 3. One it lacks (`backflip`) is `CAPABILITY_UNAVAILABLE`, as before.
  - Moving toward another character is `UNSUPPORTED`: a character's position is not a plan-time fact
    (ADR-758).
- **Start:** the first beat's `at`, else the start of the plan's first shot about the same subject.
  With neither, the performance is refused. A start that is not a cut is a warning: the body would
  visibly jump (ADR-758).
- **The mark is placed from authored facts only.** It sits on the line from the character's
  authored anchor (its node's *base* position) toward the first target, `speed × lead` back
  (default 2 s):
  - for `*_to`, back from the stopping point, which is 1 m short of the target's radius;
  - for `*_past`, back from the pass point.
  The same plan against the same scene gives the same mark, whenever it is compiled.
- **Geometry:**
  - `*_to` stops 1 m short of the target's radius.
  - `*_past` takes **the tangent**: the straight line that passes the target at `radius + 1.5 m`,
    keeping it on the performer's left, then continues. Its closest approach is the pass point, and
    that is the event's time.
  - The first version aimed at the centre and stepped sideways. That bent the path, so the true
    closest approach (3.69 m) fell 0.14 s after the event (the test caught it).
  - `look_at` is a pair of rotation keys. `performerFor` honours explicit headings only between two
    consecutive rotated keys, and faces the direction of travel elsewhere.
- **Speeds are the card's:** 95% of the run speed, or the walk speed.
- **No clip cues.** The gait reads the performance's exact speed (ADR-758), so walk, run and idle
  follow the path by construction. Clip cues are for one-shots, which are the motion lead's domain
  (Slice 3).
- **Events** (spec §30): each beat's `emits` becomes a Cue marker at the compiled time: arrival for
  `*_to`, closest approach for `*_past`, the end for timed beats, the start for `look_at`. A cue
  `on` a plan event compiles to a baked Time event at that time. The model never supplies these
  times.
- **One actor per character,** keyed by the entity's name. The validator refuses a second
  performance of the same character in a plan, or one onto an actor this plan did not make.
- **Chase and follow cameras need nothing new** (spec §52): a follow rig on the character's node
  follows the performed body, which is a pure function of time.

## Consequences

On the benchmark, "at 1:30 Rook runs past Umbra, holds, and looks at it", played from zero:
- he is on the compiled path, on the terrain, running;
- the computed marker is his actual closest approach, with the flash cue on it;
- he faces Umbra during the look;
- he is handed back at the end mark;
- it is one undo.

Proven red: event time, cue-on-event, terrain height, and the post-behaviour re-assertion.
