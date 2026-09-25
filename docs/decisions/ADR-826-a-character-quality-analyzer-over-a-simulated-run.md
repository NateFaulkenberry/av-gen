# ADR-826: A character quality analyzer over a simulated run

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-091 (two-tier determinism), ADR-186/191 (distance detail policy), ADR-545 (measured
velocity), ADR-620/622 (gait acceleration, visible clip rate), ADR-820 to ADR-825 (the motion stack
this measures); Glowmere review build item 9, Phase F8-lite
**Implemented by:**
- `entity::CharacterQualityRecorder`, `CharacterSample`, `CharacterQualityReport`, `toJson`
  (`src/entity/character_quality.hpp/.cpp`);
- `tools/character_quality.cpp` → `avgen_character_quality`.

**Tests:** `tests/unit/test_character_quality.cpp` (`[motion][quality]`)

## Context

Every motion defect found so far was found by somebody watching a film: an alien that teleports,
feet that skate, a character that flickers between two clips, one that leans into a wall for twenty
seconds. Each of those is already visible in a number the engine computes -- the root position from
one step to the next, the gait's authored stride against the ground actually covered, the activity
the gait machine chose -- and nothing compared them. The Glowmere review needs the cast checked
offline, before anyone sits down to watch it, and it needs the check to say *which* thing is wrong.

## Decision

A recorder, fed once per simulated frame, accumulates per-entity metrics; a tool runs the real
`app::Engine` offline over a project or a scene and writes them as JSON.

- **Individual metrics, never a score.** Motion: `travelMetres`, `maxSpeed`, `rootPops` (steps
  faster than 3x the authored run speed, fallback 8 m/s; count and largest in metres),
  `velocityDiscontinuities` (measured horizontal acceleration above 40 m/s² in one step -- about
  five times a gait's authored decel; count and largest), `footSlip` (`Gait::footSlip` on the
  *measured* speed over moving walk/run frames: the fraction outside [1/1.5, 1.5] and the worst, by
  log distance from 1). Behaviour: `stuckSeconds` (intends > 0.3 m/s, covers < 0.05 m/s),
  `idleFraction`, `activityChanges` and per minute, `oscillations` (A→B→A within 1 s),
  `directorSeconds` (a Director-authority action running), `airborneSeconds`. A combined score would
  hide exactly the trade it pretends to measure, and the schema test fails if a key naming a score,
  grade or overall appears.
- **Two doors in, one code path.** `record(EntityWorld, ...)` reads `state()`, `locomotion()`,
  `actions()` and `desc().gait` and writes nothing; it is an adapter onto
  `record(span<CharacterSample>, ...)`, so the arithmetic is tested with hand-made samples rather
  than by persuading a behaviour to get stuck on cue.
- **The root is `locomotion().position`** -- the place the animation layer is handed, including root
  motion -- because that is what a viewer sees pop.
- **The first frame and a body's first appearance are baselines.** Nothing is divided by the first
  frame's zero dt (ADR-521).
- **Deterministic except one labelled number.** The wall-clock milliseconds per frame are filed
  under `machineDependent`; everything else is a function of the scene (ADR-091).
- **The tool lifts the entity distance cull through `Engine::setDetailLimits`.** Setting
  `composition->scene().detailLimits` directly is a silent no-op: `Engine::update` writes its own
  limits over the scene's every frame (ADR-186). The first version of the tool did exactly that, and
  Glowmere's aliens -- `coarseInterval` 0.1 s beyond 120 m -- read as 21 m/s hitches (123 to 481
  velocity discontinuities each) and 6 to 30 seconds of "stuck", which was the LOD, not the cast. The tool now refuses to report if the
  cull is still in force after the first update.

- **Logs go to stderr.** The engine's default logger writes to stdout, where the JSON goes when
  `--out` is not given; the tool installs the stderr logger at Warn first.

## Consequences

- `avgen_character_quality --project examples/world/glowmere-valley-2-multicam.json` gives the
  review a per-alien sheet in about half a minute without a GPU. The first reading, 60 s at 60 fps:
  no root pops on any alien; foot slip out of band on under 0.5% of moving frames; stuck time 0.1 to
  2.2 s; and one outlier worth a look -- Sage changes activity 53 times a minute with 16 A→B→A
  oscillations, where the others do 14 to 28 with at most 2. Two runs give identical motion and
  behaviour numbers.
- A pop is also two velocity discontinuities (in and out), and travel includes the pop's distance.
  The counts answer different questions and are not meant to be summed.
- The thresholds are in the report, so a number can always be read against the line it was
  measured against; they are constants in `CharacterQualityThresholds`, not yet authored per scene.
- It measures what the engine publishes, not the rendered pose: a skinning or IK defect that leaves
  the root alone is invisible here.
