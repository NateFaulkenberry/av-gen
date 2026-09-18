# ADR-267: The scrub already matches the play, and what breaks it is the frame rate and where the camera is

**Status:** Accepted
**Date:** 2026-09-17

The character-intelligence brief treats determinism as the hard problem: an autonomous agent with
memory and accumulated state is, by default, exactly the thing that makes a scrub differ from a
play. The assignment called it the central architectural question.

It is not the hard problem, and the reason is worth writing down, because everyone who has looked
at this — including ADR-091, which conceded it — has assumed it was.

---

## 1. The measurement

`tools/charai_probe.cpp`, 8 `explore` characters on `glowmere-valley-2` with the real navigator and
the real 23,716-cell grid. Worst position difference across the eight bodies at t = 30 s. Load
average 3.75.

| comparison | worst difference | |
|---|---|---|
| play(60 Hz) vs play(60 Hz) again | 0.000000 m | control: must be 0 |
| play(60 Hz) vs play(30 Hz) | 0.955805 m | control: must **not** be 0 |
| seek(60 Hz step) vs seek(60 Hz step) again | 0.000000 m | |
| **play(60 Hz) vs seek(60 Hz step)** | **0.000022 m** | the claim under test |
| play(jittered 45–90 Hz) vs seek(60 Hz step) | 0.094877 m | what a real session plays |
| play(LOD on, camera at origin) vs (camera at 200 m) | **50.263096 m** | |
| play(LOD on, near) vs play(LOD on, past the cull band) | 110.622116 m | a culled body never moves |

Twenty-two micrometres, over half a minute, for eight characters that each planned routes round
1,238 solids, replanned on a throttle, steered locally, separated from each other and snapped to a
noise-function ground. The entity simulation is already a deterministic replay. It has been for as
long as `EntityWorld::seek` has existed, and nothing said so because nothing had compared the two.

---

## 2. What the probe got wrong first, twice

Both wrong versions are recorded because the wrong version is the instructive one (ADR-182).

**Every arm read 0.000000 m, including the control that must not be zero.** The harness built
entities with no `NodeBinding`, so every anchor was the origin, eight explorers started stacked on
one point, and none ever travelled. A measurement of a simulation that did not run is not a
measurement of a cheap simulation. Every scaling arm now reports the furthest distance any body
actually walked, as its own control: the no-behaviour arm must read 0 m and the others must not.

**The LOD arm read 0.000000 m and looked like a result.** It agreed with `src/entity/action.hpp`,
which claims that behaviour LOD "does not change the answer". It could not have read anything else:
the synthetic entities left `fullDetailDistance` and `cullDistance` at their default 0, and 0
disables the band. Given Glowmere's own numbers — 120 m to coarse, 340 m to culled — the same arm
reports 50.263 m.

---

## 3. What actually breaks it

Three things, none of which is about autonomy.

**The frame rate.** Play integrates the real frame delta (`scene/composition.cpp:2049`); seek forces
1/60 (`entity/entity.cpp:721`). Identical inputs, different integrations. A jittered 45–90 Hz
session — which is what an interactive session actually delivers — diverges 0.094877 m over 30 s.

**Behaviour LOD.** The coarse band accumulates dt and takes one 0.1 s step where full detail takes
six of 1/60, through a steering function that is not linear in dt. And the band is chosen from
`EntityUpdate::viewPosition`, so **the simulation depends on where somebody was looking**. 50.263 m
at 30 s between a camera at the origin and one 200 m away. `action.hpp`'s claim may well be true of
the action queue, which is what it was written about; it is measurably false of behaviours.

**Four defects in `EntityWorld::seek` itself**, each small, each real:

* `BehaviorContext::self` is never assigned during a seek (`entity.cpp:747-753` omits the line
  `entity.cpp:1049` has), so it stays 0 and every body excludes entity 0 from crowd separation
  instead of excluding itself.
* The crowd field is never rebuilt during a seek — `crowd_.clear()/add()/build()` happen only in
  `update()` — so a seek separates against a snapshot left by whatever frame last played. State
  surviving across the one call whose whole job is to remove state.
* The action queue and the schedule are not re-simulated at all. ADR-091's Cinematic Action tier is
  reset to the authored list and then integrated by nothing.
* The cull test inside the seek loop reads `viewPosition`, so a seek is a function of (time,
  camera).

---

## 4. Decision

**The AI is a pure function of (seed, the ordered sequence of fixed simulation steps).** Not of
(seed, time).

It cannot be a function of (seed, time): a route round an obstacle has no closed form, and
`entity/entity.hpp:601-606` already says so. The sequencer's camera bake is not a model to copy —
it works because a `seq::Actor`'s position is `positionAt(t)`, and `sequence.hpp:145-150` already
excludes "a node moved by an entity behaviour" from that class in as many words.

Three obligations follow, and they are the whole of the determinism work:

1. **A fixed simulation step with an accumulator.** The entity world advances in whole 1/60 steps;
   the frame rate decides how many, never how big. Perhaps sixty lines, and it retires the 0.094877 m.
2. **Level of detail selects which stages run, never the integration step.** A far body integrates
   the same 1/60 steps with a cheaper mind: perception at 1 Hz instead of 4, decisions at 0.5 Hz
   instead of 2, replanning suppressed. It does not take bigger steps, and it is never culled to
   motionlessness in a frame that will be rendered.
3. **Fix the four seek defects**, which are the residual and are cheap.

And two rules on everything built on top, stated in `src/entity/character_ai.hpp` §1 as D2 and D4:

* **Draw from a seed and an index, never from a stream,** for anything a *decision* depends on.
  `app/cinematic.cpp:1555-1587` is the model and states the reason: a PRNG stream makes every later
  choice depend on how many earlier ones were made, so one extra rejection in `pickDestination`
  re-casts everything after it. `Entity::rng_` stays where it is for the eleven existing
  behaviours; nothing new takes a draw from it.
* **Memory is bounded and reconstructed by the replay, not persisted.** A memory only a saved file
  could restore is a memory that makes a scrub differ from a play.

---

## 5. Checkpointing is an optimisation, not a correctness mechanism

ADR-091 reserved checkpointing as future work for the live tier, and
`docs/investigations/ui-responsiveness.md` §S1 proposed it as the fix for the seek stall. Both are
still right about the *latency*; neither is needed for *correctness*, because §1 shows the replay is
already exact.

What it would buy, from the measured per-character costs: one `EntityWorld::seek(90 s)` is 5,400
steps, so at 89 µs per `explore` character that is **4.8 s at 10 characters, 24 s at 50, 48 s at
100**. That is what caps the cast size in an editor — not the frame cost, which is 9 ms at 100.

Two cheaper things to try first, in this order:

* **`maxSeconds = 90.0` is a literal and it is the wrong unit.** It should be a budget in
  entity-steps, so a 23-body scene keeps its 90 s of history and a 250-body scene keeps what it can
  afford. One line of policy where there is now one line of constant.
* **Move the re-simulation off the main thread.** It is 99.999% of a seek and it runs synchronously
  inside `SequencePanel::draw`, draining no input while it runs, 52 times for one drag gesture.

Checkpointing is Phase 8 and is priced against those two, not assumed ahead of them.
