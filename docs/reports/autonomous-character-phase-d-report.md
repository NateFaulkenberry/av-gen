# Phase D report: autonomous characters, world awareness and behavioural motion planning

§75's required report. The branch is `agent/anim-phase-d`. The design record is ADR-670 and ADR-671,
and the per-topic documents are `docs/design/{autonomous-character-architecture,
character-behavior-model, character-perception, navigation-architecture,
interaction-affordances}.md`.

## Architecture

What exists after Phase D, box by box against §78, is tabulated in
`autonomous-character-architecture.md`. In one paragraph: **perception** (`GridPerception`, now
with semantic tags and per-tag salience) feeds an **awareness layer** (`mind.hpp`: attention,
object memory and novelty, personality, heard world events). The awareness layer sits inside the
**decider** (`decide`: a scored option list with dwell, margin, commitment and hold), whose winner
becomes an **action plan** on the entity's queue. The queue's `Move` walks a **NavGrid route** with
local steering and an arrival radius, and publishes **vector intent**. The decider publishes the
intent's *what*. `Entity::advanceMotion` turns intent into a **MotionRequest** for Phase B/C. No
layer above the intent names a clip or writes a transform.

## Behaviour model

Utility selection over data-authored considerers (`idle`, `holdPost`, `investigate`, `interest`,
`route`, and new in Phase D `react`, `social`, `goal`), bent by personality. Hysteresis comes from
four mechanisms, each justified by a measured failure: dwell, margin, commitment to a running
errand, and holding an errand whose proposer went quiet. §19's lifecycle comes from drained-list
serials: completion marks the subject familiar, and failure suppresses it and excludes the option.
`character-behavior-model.md` compares FSM, BT, GOAP, utility and HSM, and explains why this model
was chosen.

## Perception

Vision (range, field of view, occlusion budget, proximity), semantic salience, and hearing of world
events. World events are raised by named action and interaction completions, by the director's beats
and by signal-bus events (§28). Attention is a sum of named factors under Phase B's hysteresis. Its
winner is the look target when nothing else set one (§11).

## Navigation

The NavGrid A* already existed. Phase D added: arrival (§14); failure memory, which removes
unbounded retries (§59); commitment; variety among wander destinations; terrain landmarks refused by
aware deciders. See `navigation-architecture.md`.

## Interaction

Props offer verbs that `require` capabilities, and characters have capabilities. The planner uses a
verb only when both match, and otherwise observes. The executor re-checks. A cow grazing grass runs
the same code an alien uses to inspect a mushroom.

## Motion integration

`Move`/`Face` write `CharacterIntent::desiredVelocity/facing/valid` every step; the gait limiter
rescales the vector with `speed`; `advanceMotion` builds the MotionRequest from it (equal to the old
polar request to 1e-4 m/s, asserted). With `proceduralMotion` off -- every shipping scene -- the pose
comes from the clip player and the seam is read by nothing downstream (ADR-615, unchanged).

## Determinism

- The same scene twice gives the same trace, line for line (tested). Every draw is seeded from an
  index, and every tie breaks on an id.
- **A scrub equals a play**, bit for bit, for every body in the Glowmere film at 30, 45 (mid-beam)
  and 90 s, and a render started mid-film matches one started at zero (ADR-671). Three replay defects
  had to be fixed to get there:
  - the missing t = 0 instant;
  - velocity never measured on the replay path;
  - the acceleration limiter reading the wrong "previous" speed.

  The director is now replayed on seek.
- Limits: exactness holds inside the 90 s replay window; signal-bus history is not replayed.

## Performance

Headless, `autonomy-demo` with N deciding aliens (`avgen_behavior_trace --crowd N`), 20 s at 60 Hz,
minimum of 3 repeats (ADR-170). "Entity step" is `Composition::updateBehaviour`: the director,
perception, attention, decisions, actions and grounding. "Frame" also includes the scene update:
flattening and posing every skinned rig.

| N deciders | entity step ms/frame | per decider | whole frame ms | decisions in 20 s |
|---|---|---|---|---|
| 2 | 0.019 | 0.0094 | 0.31 | 3 |
| 11 | 0.106 | 0.0096 | 1.18 | 41 |
| 51 | 0.538 | 0.0105 | 5.10 | 241 |
| 101 | 1.174 | 0.0116 | 10.0 | 442 |
| 201 | 2.250 | 0.0112 | 20.5 | 808 |
| 501, 1001 | not yet taken | | | |

**Timing caveat.** These minima were taken on 2026-09-21 before the coordinator reported an unlocked
owner render running on this machine; whether it had already started is unknown. Re-take them, and
the 500 and 1,000 rows, on a quiet machine: `avgen_behavior_trace <demo> --crowd N --seconds 20
--repeats 3`.

**The awareness layer costs nothing measurable.** 100 characters without it: 10.20 ms/frame; with it:
10.02. Behaviour cost is linear, with no O(characters × scene) term, because perception is
grid-backed. At 100 characters the frame is dominated by the scene update (posing rigs), not by
behaviour. Allocation: option names and trace lines allocate on changes of mind only. Query and
pathfinding counts: `GridPerception::Counts`, `Selector::Counts` and `AttentionModel::candidatesScored`
are structural counters, and the tool reports decisions. A per-subsystem breakdown (§44) and
threading (§45) are not done: the measured cost does not justify them yet.

## Glowmere

The owner's rulings of 2026-09-21 are applied to `glowmere-valley-2-multicam`: the awareness layer on
all five aliens with distinct personalities, consistent roam ranges, terrain refused as a
destination, and the director replayed on seek. Visible behaviour per alien, and the before/after
numbers, are in `autonomous-character-architecture.md`. The demonstration scene is
`examples/labs/character/autonomy-demo.scene.json`. The second creature is
`examples/labs/character/pasture.scene.json`.

## Limitations

- Behaviour LOD is authored per character (cadence knobs). A camera-keyed tier is rejected because it
  would make decisions depend on the camera (`character-behavior-model.md`). The existing coarse band
  still violates D3.
- No ORCA/RVO; crowds separate but do not plan around each other.
- Hearing is not occluded; vision occlusion is budgeted and off by default.
- Goals are one errand deep; there is no multi-step planning.
- Scrubs past the 90 s window, and signal-driven events, are not reproduced exactly.
- There is no canvas overlay beyond the World editor's route label; §41's report is text.
- §36 cinematic signals and §48's inspector are not built.

## Future neural integration

A learned policy is an `IConsiderer`: it reads `DecisionContext` (percepts, memory, events,
personality, attention) and appends scored `Option`s. It replaces behaviour selection and nothing
else. A learned motion planner sits below `CharacterIntent` as a `MotionChain` provider (Phase C's
seam). An optional LLM goal provider writes `goal` considerers' subjects: high-level goals only,
never in the frame loop. Perception, navigation, IK, motion matching and rendering are untouched by
any of these.
