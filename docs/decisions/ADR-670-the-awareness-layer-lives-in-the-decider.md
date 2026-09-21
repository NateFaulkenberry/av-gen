# ADR-670: Phase D's awareness layer lives inside the decider, and the plan's end is an event it can see

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-269/ADR-333 (the decision layer), ADR-270/ADR-290 (perception), ADR-360 (a scrub
equals a play), ADR-541, ADR-545 (measured velocity), ADR-615 (the dark motion seam), Phase B §22
(attention), Phase D §1, §3, §10, §19–§22, §24–§29, §33, §63, §78
**Implemented by:** `src/entity/mind.{hpp,cpp}`, the `decide` behaviour's `"mind"` block
(`src/entity/behaviors.cpp`), `ReactConsiderer` and `SocialConsiderer` (`src/entity/decision.*`),
`ActionQueue::Drained`, `Selector::hold`, `EntityWorld::worldEvents`, `src/entity/behavior_trace.*`,
`tools/behavior_trace.cpp`
**Tests:** `tests/unit/test_phase_d_autonomy.cpp`

---

## Context

Phase D §78 draws the target stack as WORLD STATE + WORLD EVENTS → PERCEPTION → ATTENTION →
MEMORY/NOVELTY → GOALS/BEHAVIOURS → CHARACTER INTENT → NAVIGATION → STEERING → MOTION REQUEST.
The audit (`docs/design/autonomous-character-architecture.md`) found perception (ADR-270/290),
utility selection (ADR-269/333), the action tier (ADR-091/096) and navigation (ADR-193 onward)
built, and **four boxes with nothing in them**: world events, attention above head-look, per-object
memory and novelty, and personality. It also found that nothing ever told a decider how its errand
ended, so §19's lifecycle existed per *action* and not per *behaviour*.

## Decision

1. **The awareness layer is a per-character object held by the `decide` behaviour**, opted into by
   the presence of a `"mind"` block in the scene file. It is not a new `CharacterController`
   class beside the decider and it is not new state on `Entity`.
2. **The plan's end is recorded where it happens.** `ActionQueue` gives every list a tier is handed
   a serial and records, when a list drains, which serial drained and whether any step failed.
   The decider compares serials. Completion marks the subject *investigated*; failure marks it
   *suppressed*; both free the selector to choose again at once.
3. **World events are world state.** Any *named* `ActionEvent` (an action's or an interaction's
   `onComplete`) is raised as a `WorldEvent` at the raising entity's simulation position, by the
   same code on `update` and `seek`. Characters hear events strictly earlier than the current
   step, so hearing is independent of entity order.
4. **Semantics are interned bits.** Entity tags and the five interest-kind names intern to a
   64-bit vocabulary; percepts and interest points carry masks; filters are one AND.
5. **Personality is nine numbers with 0.5 neutral**, entering scores only as `(0.5 + trait)^w`, so
   a neutral or absent personality changes nothing.
6. **A running plan whose proposer goes quiet is held at the score it was committed at** until
   something beats that by the margin — but only while its subject is still known, so a target that
   is genuinely lost still ends the behaviour.

## Alternatives considered

**A `CharacterController` owning perception, memory, attention, behaviour, navigation and
steering (§55's diagram, literally).** Rejected. Every one of those except attention and memory
already has an owner, and the entity step is duplicated between `EntityWorld::update` and
`EntityWorld::seek` (testing.md #31 has cost this project four defects). State held by a behaviour
is stepped by *both* paths for free, because both run the behaviour loop; state held anywhere else
needs a third copy of the step. §55's controller exists — it is `decide` plus the queue — and the
diagram's boxes are its members.

**Attention on `Entity`, computed in `perceiveOne`.** Rejected for the same reason, and because
attention needs the committed subject (relevance), which only the decider knows.

**A behaviour tree or a lifecycle enum on the decider.** Rejected, as ADR-269 rejected them: the
queue already has `Completed/Failed/Cancelled` per action. What was missing was *visibility* of the
list's end, which is one serial.

**Inferring the end from `pending() == 0`.** Rejected: an empty tier is also a tier that was never
given anything, and the decider could not tell "finished" from "idle" — the ambiguity ADR-296 names.

**Events from the signal bus directly.** Deferred, not rejected. A seek does not replay the bus's
history, so a bus-driven event would be heard on a play and not on a scrub. Action-raised events
replay exactly. §28's audio→semantic mapping needs the bus evaluated per replayed instant first.

**Giving every stock considerer personality code.** Rejected in favour of a wrapper applied when a
considerer declares `"traits"`, so the six stock considerers carry none.

## Consequences

- **Byte-identical for every decider without a `"mind"` block** — which is every decider in the
  shipping Glowmere scene. The one exception is intentional and scene-independent: `seek` now
  replays the t = 0 instant and measures velocity (see below), which changes scrubbed frames toward
  the played ones.
- **Two replay defects found and fixed on the way** (Phase D §63): `seek` never measured
  ADR-545's velocity (a replayed body reported zero for the whole replay), and it skipped the
  t = 0 instant a play integrates with `deltaTime = 0`, so every decider committed one step late
  and ran every errand one step behind. Measured on the autonomy demo at t = 75 s: a scout 111 m
  from where the play left it. Both are teeth-checked in the scrub test.
- **Vector intent has a producer** (ADR-615's "no producer" row): `Move` and `Face` write the HOW,
  the decider the WHAT. The gait limiter rescales the vector as it limits `speed`, so the request
  built from intent equals the polar one exactly (asserted to 1e-4 m/s).
- **Memory is bounded and reconstructed**, never persisted (D4): 24 objects and 8 events by
  default, eviction never drops a subject inside its suppression or recovery window.
- **Signal-bus events and occlusion-tested hearing are not built.** Recorded in the phase log.
