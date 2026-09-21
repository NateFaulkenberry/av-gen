# ADR-615: The character-motion seam is staged and dark — one lab scene enables it, and that is one fact rather than six findings

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-541 (the provider seam), ADR-545 (vector intent), ADR-556 (advance and pose),
ADR-612 (amended — the beneficiary it named could not have benefited), ADR-613, ADR-441 (cut
effects outright), Phase B §33–§39, Phase C §34
**Implemented by:** nothing — this records a reachability fact and a decision not to act on it
**Decision owner:** the project owner, who has the wire-or-delete question open

---

## The fact, stated once

> **Exactly one scene in this repository sets `proceduralMotion`, and it is a lab:**
> `examples/labs/footik/alien-foot-lab.scene.json`.
>
> ```
> grep -rn "proceduralMotion" --include='*.json' .
> ```
> — one hit.

Everything below follows from that one line. **A defect on a path only a lab reaches is a
documentation problem, not a product one**, and six separate findings that all reduce to "this is
not reachable" are one finding about reachability.

## What is staged and dark, together

Each of these is built, tested, and reached by nothing in `src/` or `tools/`:

| what | where | the tell |
|---|---|---|
| `MatchMotionProvider` | `src/entity/match_motion_provider.*` | constructed nowhere; `buildChain` adds only `ClipMotionProvider` |
| the motion **controller** — `stepMotion`, `MotionState`, `MotionLimits` | `src/entity/motion_controller.*` | `Entity::motionState_` is touched in one place in the whole tree: `.reset()` |
| vector intent — `CharacterIntent` | `src/entity/character_intent.hpp`, `behavior.hpp:125` | **no producer anywhere**; `entity.cpp:873`'s `if (state_.intent.valid)` is dead, so every character takes the polar path |
| the trajectory seam — `sampleTrajectory`, `MotionRequest::futureSeconds` | `src/entity/trajectory.*`, `motion_provider.hpp:108` | `futureSeconds` has **one** reference in the tree: its own declaration |
| root-motion adaptation — `adaptRootMotion` | `src/entity/root_motion_adapt.*` | callers in `tests/` only |
| trajectory prediction — `predictTrajectory` | `src/entity/trajectory_prediction.*` | its only caller is itself test-only |

**They are dark together and for one reason**, which is why this is an ADR and not six code
comments with six separate explanations.

## The decision: document, do not wire, do not delete

**Do not wire.** Wiring is a product change with a per-frame cost — a search per character per
frame for the matcher, an integrator step for the controller — and **nothing in Phase C or D
requires it**. Phase C §34 established that the only consumers of provider output in `src/` are
diagnostics. Making that change here would be scope the owner did not ask for. Same ruling, same
reasons, as the one already made for `buildChain` (ADR-613).

**Do not delete.** ADR-441 says cut effects outright rather than leaving aliases — and that rule is
about **cut** features. These are **unfinished** ones: the spec asked for them, the spec is still
being worked, and deleting them would destroy work that is correct and waiting for a consumer.
Applying ADR-441 here would be applying it to the wrong category.

**So: document at the symptom.** Each item above carries a comment where a person would *meet* the
symptom, not where the phase log records it — the ADR-612 treatment. Someone asking "why does my
character ignore the velocity I set on its intent" should find the answer at the intent field, not
by reading a motion-matching phase log.

## The shape this added to the catalogue

`LocomotionState::grounded` is written on both paths and read by nobody — ordinary enough. What is
new is that **its comment still says "written by neither and read by nobody"**. The write half was
fixed; the comment was left describing the state before the fix.

> **A stale description of a repaired thing misleads in the opposite direction from every other
> entry on this list.** The nine shapes before it are all "the code does less than it claims". This
> one is "the code does more than it claims", and it is invisible to every technique that finds the
> others, because those hunt for dead symbols and this symbol is alive.

Worth a specific watch: a comment saying a thing is broken is exactly the comment nobody re-reads
after fixing it.

## Consequences

- **The reachability fact is cited, not restated.** Anything that needs it points here.
- **The wire-or-delete question is open with the owner**, with this context attached. Until it is
  ruled on, everything stays built and documented.
- **A reader finding any of these unused should read this ADR before concluding it does not work.**
  Several are well tested — that is the point of the defect family, not a mitigation.
