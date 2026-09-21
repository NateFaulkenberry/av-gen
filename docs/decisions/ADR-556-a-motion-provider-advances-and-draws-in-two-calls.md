# ADR-556: A motion provider advances and draws in two calls, because a seek replays 5,400 steps and poses once

**Status:** Accepted
**Date:** 2026-09-20
**Amends:** ADR-541 (a motion provider that remembers lives in the entity tier)
**Related:** ADR-086 (a state stores when it was entered), ADR-182 (a probe that cannot fail proves
nothing), ADR-273 (the seek budget), ADR-300 (a pose layer cannot reach the world), ADR-360 (the
determinism contract), ADR-554, ADR-555
**Implemented by:** `src/entity/motion_provider.hpp`, `motion_chain.hpp`,
`clip_motion_provider.{hpp,cpp}`, `Entity::advanceMotion`, `SkinnedRig::externalPose`
**Tests:** `tests/unit/test_motion_provider.cpp`, `tests/unit/test_alien_foot_lab.cpp` — "the opt-in
body is posed by its provider chain", "the provider's pose actually reaches the drawn rig"

---

## Context

ADR-541 specified one call:

```cpp
evaluate(request, in, skeleton, out /*Pose*/, next)
```

Wiring it into the product found the reason that cannot work, and the reason is a measurement
rather than a preference.

`EntityWorld::seek` reproduces a scrubbed frame by **replaying the simulation at a fixed 1/60
step** — ADR-273 caps it at ninety seconds, which is **5,400 steps per body** — and then poses the
rigs **once**, at the target time. Scrub latency is already the worst interactive cost in this
product. A seam that posed an 89-joint skeleton on every replay step would multiply it by 5,400.

The alternative — evaluating the provider only at the target time — breaks ADR-360 outright: the
memory would not have been advanced through the intervening steps, so a scrubbed frame would show
the clip at a different phase from a played one.

## Decision

**`IMotionProvider` has two methods, split where the work splits.**

| | runs | may touch | contract |
|---|---|---|---|
| `advance` | every step, **including every replay step** | no skeleton | what makes a scrubbed frame reproduce a played one |
| `pose` | once per drawn frame | the skeleton | a **pure function** of the memory `advance` settled |

If `pose` needed anything beyond `(memory, skeleton)`, that thing belongs in `MotionMemory` and
therefore in the replay. That is the whole test for whether the split is being respected.

**This is not a compromise.** It is the shape these algorithms already have: learned motion matching
is a Stepper that advances a latent and a Decompressor that turns it into a pose (Phase 0 §3);
classical motion matching is a search that picks a database frame and a lookup that reads it.
ADR-541's single call was hiding that seam, not simplifying it.

### Three corollaries

1. **`MotionMemory` records which provider settled it.** `pose` returns to the provider that won
   `advance`. A chain that re-selected at pose time could hand a matcher's database frame to the
   clip player, which would read it as a clip index and draw a different animation entirely.
2. **`MotionChain::pose` does not fall back.** If the provider that advanced the memory cannot draw
   it, that is a bug in that provider; quietly asking a different one to interpret another's memory
   turns a visible failure into a wrong pose (§64).
3. **The scene tier receives a pose, not a provider.** `SkinnedRig::externalPose` is a `Pose` and a
   flag. It is consumed rather than latched, so a driver that stops driving hands the body back to
   its clips rather than freezing it. If the rig knew what a provider was, the layer module would
   have a route to the simulation, which ADR-300 exists to prevent.

## Evidence the seam is actually reachable

Code called by nothing is this project's most expensive recurring failure. So:

* **Default-off is inert, measured.** 60 frames of `glowmere-valley-2-multicam` rendered through
  the **project** (ADR-264): `main`'s binary and this branch's give the identical sequence hash
  `94a86f3db7a6198c…`. With the opt-in on for five aliens it is `f4a477b7c5efe1c4…`, so the
  comparison is not vacuous (ADR-182).
* **A near-miss that ADR-170's rule already covers.** The first "before" render came from a second
  worktree and disagreed — the worktree's own asset copies, not the code. Running `main`'s binary
  against *this* worktree's files isolated the binary and gave the identical hash. Change one
  thing, not two.
* **The probes fail against deliberate breaks**, and the second probe was **vacuous until a break
  revealed it**: it asserted the sink's own "I posed it" flag, which stayed true while the pose was
  computed and dropped. Comparing the drawn pose could not catch it either, because a clip provider
  and the clip player agree by design (ADR-541 corollary 1). The fix is a counter incremented by
  the **consumer** — `SkinnedRig::externalPoseFrames` — because a flag set by whoever claims to
  have done the work cannot distinguish a claim from the work.

## Consequences

* Phase C's motion matcher and Phase E's neural provider implement `advance` and `pose` separately,
  which is the factoring both already want.
* A provider whose `pose` is not a pure function of its memory will produce a scrubbed frame that
  differs from a played one. That is now a statable bug with a named cause.
* **Revisit if** a provider appears whose advance genuinely requires a posed skeleton. The answer
  is probably that it needs pose *features* in `MotionMemory`, not that the split is wrong.
