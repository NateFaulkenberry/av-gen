# ADR-138: What tier assignment can realize on Glowmere is bounded by terrain coverage

**Status:** Accepted
**Date:** 2026-09-13

## Problem

ADR-136 measures the flat rung's ceiling at **5.77 ms of GPU frame, 41%**, taken with the rung
forced on every draw. A ceiling is not a saving. The saving is the ceiling times the share of the
frame's *fragment work* that an importance rule may legitimately assign the rung to, and it is worth
writing that number down before somebody builds the assignment and is disappointed by it.

## The bound

§4.9 measured Glowmere's coverage from the CPU, at 1280×800 on the canonical camera:

| | triangles | coverage | share of coverage |
| --- | --- | --- | --- |
| authored entities (terrain, heroes) | 36,286 | 2.59 Mpx | **77%** |
| procedural scatter (the ecology) | 171,441 | 0.77 Mpx | **23%** |

The terrain is a single near object filling most of the frame. No importance rule may demote it: its
projected radius is the frame, it is what every contact shadow lands on, and flattening it is what
the `matflat` capture shows as the near-field damage the gate rejects. The heroes are heroes.

**So the assignable share of Glowmere's coverage is at most 23%, and the realizable saving of the
flat rung is therefore at most about 1.4 ms** — 23% of 5.9 ms of scene pass — *if every scattered
instance qualified for it*. They do not: the scatter includes the foreground ferns and grass, which
are the largest and nearest things in the picture after the terrain.

The reduced rung is bounded the same way and starts from 0.79 ms, so its realizable share is a few
tenths of a millisecond. **On this scene it is not worth wiring on its own.**

## What this does and does not say

**It does not say Phase D was wrong.** The measurement it produced is the most valuable thing in the
phase regardless of what ships: 70% of the 8.4 ms "no arm removes" is now attributed, and the
attribution says the money is in per-light shadow terms and per-fragment ambient work rather than in
light count. That redirects Phase C's and any future phase's effort.

**It does not generalise to other scenes.** The bound is a property of Glowmere's *composition* — a
near terrain filling three quarters of the coverage — and not of the renderer. A scene whose frame
is mostly mid-distance ecology, a crowd, a city block, or Constellation's very different mix would
have a different assignable share and could realize much more of the ceiling. The correct reading is
"measure the assignable share per scene", not "material tiers are worth 1.4 ms".

**It does say what to build first, if this is built.** Not the entity path. Glowmere's assignable
coverage is entirely procedural scatter, and scatter already has an importance answer: the LOD level
`cull.wgsl` assigned it. The natural wiring is a tier lane in `ProceduralUniforms`, per LOD level —
LOD 0/1 Full, the billboard rungs Flat — which reuses the existing importance notion rather than
adding a second one, needs no change to the object layout (ADR-135), and is uniform per draw by
construction. The entity path needs the `ObjectUniforms` lane and buys less, because Glowmere's
authored entities are the terrain and the heroes.

## The experiment that would settle it

One arm: the flat tier applied to procedural draws only, with entities at Full. Interleaved against
the baseline in one session, exactly as ADR-136's arms were. That measures the assignable share
directly instead of estimating it from a coverage table, and it costs one uniform lane and one
afternoon. It is the first thing the next agent on this should do, and it should be done **before**
the assignment machinery, not after — the same ordering that produced ADR-136.

## Decision

**Assignment is not wired, and the reason is a number rather than a schedule.** `MaterialTierSelector`
and `QualityPolicy` ship complete and tested; the arms ship; the tier bands in the tier table are
placeholders that nothing reads. Turning any of it on waits on the procedural-only arm above.
