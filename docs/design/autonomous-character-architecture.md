# Autonomous character architecture — Phase D

Phase D §1 asks for a repository-wide audit before implementation, and for this document plus an
ADR before large production changes. **The audit's headline is that most of Phase D already
exists**, built across ADR-269, ADR-270, ADR-290 and ADR-333, and that the honest contribution is
one seam rather than a new subsystem.

## The audit: Phase D against what is already here

| Phase D asks for | already in the engine | verdict |
|---|---|---|
| §5 utility / priority behaviour selection | `entity::decision` — `Option`, `IConsiderer`, a scored option list, selector with **dwell and margin** (ADR-269, ADR-333) | **exists**, and its hysteresis is the rule `GaitSettings::minDwell` already proved |
| §6 data-driven behaviour definitions | `BehaviorDesc` in scene JSON; `entityProfiles`; the Glowmere cast authors `decide` with `hertz`, `dwellTicks`, `margin`, `memorySeconds`, `novelty` | **exists** |
| §7–§9 world query / perception | `IPerception`, `GridPerception`, `ScriptedPerception`, `PerceptionIndex` (ADR-270, ADR-290) | **exists**, with a bounded working set (D4) |
| §10 attention | `LocomotionState::lookTarget` + `PoseLayerDrive::Look` (ADR-300) | **exists**, and §11's "attention ≠ head look" is already the split |
| §12–§13 navigation | `NavGrid`, `navPath_`, `PathStatus` | **exists** |
| §16–§19 interaction, capabilities, lifecycle | `ActionQueue`, `ActionDesc`, `Authority`, `ActionResult` (ADR-091, ADR-096) | **exists** |
| §21 character memory | the decision layer's novelty/visited memory, carried as a **tick index** rather than an accumulator | **exists** |
| §33 determinism | D1–D4 in `character_ai.hpp`, enforced by `EntityWorld::seek`'s fixed-step replay | **exists** |
| §15 **character movement must become vector-based** | — | **the gap** |
| §2.5 **`MotionRequest` as the behaviour→motion seam** | `MotionRequest` exists (Phase B.C) but was synthesised inside `Entity` rather than published by a behaviour | **the gap** |

Phase D §1 is explicit: *do not duplicate an existing system; if AV Gen already has a subsystem
that can serve as the foundation, extend it.* Nine of the eleven rows above say the foundation is
there. Building a second decision layer, a second perception filter or a second navigator would
have been the largest and least defensible thing this phase could do.

## The gap, and why it is the one worth closing

**Every behaviour in this engine expresses movement as `EntityState::speed` along
`EntityState::yaw`** — a scalar on a heading. Measured in `behaviors.cpp`: `state.speed = ...` at
six sites, `state.yaw` beside them, and nothing else.

A scalar on a heading can describe a body walking where it looks, and it cannot describe anything
else. ADR-545 already had to add a **measured** velocity vector to the seam for exactly this
reason — the animation tier could not tell a strafe from a walk. That correction was made on the
*measurement* side and never on the *intent* side, so the behaviour tier still has one direction to
put in two places.

§15 calls this out by name. It is the gap.

## What was built

`src/entity/character_intent.hpp` — `CharacterIntent`, published on `EntityState`, consumed by
`Entity::advanceMotion` when it turns intent into a `MotionRequest`.

* **Vector `desiredVelocity`**, plus a `facing` that is independent of it. A body circling a target
  while watching it has one of each and they disagree.
* **`steering` added, never blended** (§14/§39): Phase D supplies the avoidance correction and
  Phase B executes it. Measured in the test: 3 m/s forward plus 3 m/s sideways is 4.24 m/s on a
  diagonal, not a blend back to 3.
* **`targetPosition` as a position, not a handle.** The motion tier must not be able to follow a
  reference into the world — the same line ADR-300 draws one tier further down.
* **No clip name, ever** (R4). The struct has no field one could go in.

### `valid` defaults to false, and that is the load-bearing decision

Every behaviour that exists writes the polar pair and nothing else. An intent defaulting to *valid,
zero* would have stopped the entire shipping cast dead on the frame this merged. So **false means
"no vector intent was published", not "stand still"**, and `advanceMotion` reconstructs the
velocity from `speed` and `yaw` — which is exactly what every character gets today.

That is why adding this seam changed no existing character's motion, and the compatibility arm of
the test asserts it rather than assuming it.

## Cut from Phase D, and named

The mandate pre-authorises scope cuts inside a stage provided they are named. These are the cuts:

* **§24 social behaviour, §28 audio-reactive characters, §29 personality** — not built. Each is a
  new *considerer* on the existing decision layer, which is the extension point ADR-269 built for;
  none needs new architecture, and none is visible in a render of the Glowmere valley tonight.
* **§37–§39 the three vertical slices** — not built as scripted scenarios. The Glowmere integration
  that follows this phase is the slice being reviewed, and building a second one first would have
  spent the night on a fixture.
* **§40–§41 debugging visualization and "why is this character doing that"** — partly. The data is
  exposed (`Composition::motionDebug`, `intentTypeName`, the chain's `provider`/`fellThrough`
  counters, `MatchMotionProvider::Counters`) but there is no editor panel; §18 of the Phase A
  brief still holds, so no UI was added.
* **§30–§31 behaviour budgeting and LOD** — already exist as `SeekBudget` and the entity LOD bands
  (ADR-273, ADR-186), and were not extended.

## What is unproven

The intent seam is **reachable but not yet used by a behaviour**: `advanceMotion` consumes it and
the tests drive it directly, but no behaviour in `behaviors.cpp` publishes one yet, because every
one of them works today through the polar path and rewriting them was not the way to spend the
night. The seam exists, is tested on both arms, and the first behaviour to want a strafe has
somewhere to put it. **That is a smaller claim than "Phase D is complete" and it is the true one.**
