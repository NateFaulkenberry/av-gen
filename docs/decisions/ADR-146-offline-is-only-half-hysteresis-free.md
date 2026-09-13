# ADR-146: Offline is only half hysteresis-free, and the half that is not is measured

**Status:** Accepted (a finding, not a change)
**Date:** 2026-09-13
**Amends:** ADR-125, whose title states a contract the renderer keeps on one of its two ladders

## What was checked

Phase G's G5 asks whether an offline render and an interactive one agree. Most of that question was
already answered and is not re-asked here: `test_composition_gpu.cpp` proves the offline job's frame
is byte-identical to the interactive path's for the same second, that a second reached by different
routes replays identically, and that Glowmere replays after a seek away and back. Those pass. They
are the §41 determinism contract and they are intact.

What none of them can see is **representation**, because representation is the thing this upgrade is
adding. ADR-125's title is the contract: *hysteresis is opt-in, and offline never gets it.* §5.9 is
the reason: an offline render is a deliverable and must not silently inherit a realtime compromise.
Hysteresis is the only mechanism in this renderer that reads the previous frame, so where it is on,
what you see depends on how the camera arrived.

## The finding

**There are two hysteresis mechanisms and the offline tier only governs one.**

| | what it is | offline |
|---|---|---|
| `RepresentationPolicy::hysteresis` | the CPU representation selector (ADR-122/123/125) | `forTier(Offline)` sets `forceTopRepresentation` and pins hysteresis and spread to zero ✅ |
| `LodSettings::lodHysteresis` | the GPU instance ladder's dead zone (ADR-082), authored per procedural object | passed straight to `cull.wgsl` (`procedural_renderer.cpp:1704`), clamped to 0.5, **not consulted about the tier** ❌ |

Measured, `tests/rendering/test_phase_g_certification.cpp`: one procedural object, 600 instances on
a three-rung screen-size ladder, `lodSpread` zero so nothing decorrelates the population,
`lodHysteresis` 0.3, `QualityTier::Offline`. The same camera position is reached twice — once
approached from 600 m in, once from 6 m out — and both are then settled for twelve further frames at
the destination, so a dead zone that merely had not released yet has ample time to.

| arrival | LOD0 | LOD1 | LOD2 |
|---|---:|---:|---:|
| from far | 0 | 0 | 582 |
| from near | 0 | **30** | **552** |

**30 of 582 visible instances (5.2%) are on a different rung in the offline render depending on how
the camera got there.**

The control settles what caused it: the identical experiment with `lodHysteresis` at zero produces
byte-identical histograms from both arrivals. The difference is the dead zone and nothing else.

The LOD histogram is a deterministic counter, not a timing, so this carries no noise floor and needs
no repeats to be believed. It reproduces exactly.

## Why this is recorded rather than fixed

Three reasons, in order of weight.

**It is not this agent's code.** `src/rendering/lod*`, the representation unit and the LOD metadata
are the `repr` agent's, and this wave's topology is disjoint file ownership. A certification agent
that fixes what it certifies is not certifying anything.

**The fix is a decision, not a line.** There are at least three shapes and they are not equivalent:
zero `lodHysteresis` at the renderer when the tier is Offline (simple, but silently overrides an
authored value, which is the kind of thing §49 is about); refuse to load a non-zero
`lod/hysteresis` offline and say so (loud, and probably correct); or extend
`RepresentationPolicy::forTier` to carry a ladder policy the procedural renderer also reads, which
is the version that leaves one place where "offline gets no memory" is written down. ADR-125's own
reasoning — that two subsystems with opposite defaults for one property is worse than either
default — argues for the third.

**Nothing shipped is affected today.** `lodHysteresis` defaults to zero, ADR-082 made that default
deliberate, and no scene in `examples/` sets it. The defect is a trap for the first scene that opts
in, which is exactly the kind of thing that should be found by a test before it is found by a
render.

## Consequences

- `tests/rendering/test_phase_g_certification.cpp` has a **failing** test, deliberately. Its
  assertion is a transcription of ADR-125's title; weakening it to green the suite would be deleting
  the contract rather than meeting it. It is tagged `[gpu][certification][offline]` and is not
  hidden — a contract violation that only runs when somebody remembers to ask is not a check.
- ADR-125 stands as written. It decided what `RepresentationPolicy` does and it did that correctly.
  Its title claims slightly more than its decision delivers, and this is the record of the gap.

## Verified vs assumed

**Verified:** every number above, under `tools/gpu-lock.sh`, deterministic counters, with a control
arm that isolates the cause. That `forTier` returns zero hysteresis for all four tiers and
`forceTopRepresentation` for Offline, by unit test with no device.

**Assumed:** that the offline *render job* path reaches the procedural renderer with the same tier
the test sets directly via `SceneRenderer::setQuality`. The test drives the renderer, not
`app::RenderJob`, so what is proven is a property of the renderer at that tier. If some layer above
already zeroes `lodHysteresis` before a real offline render, the defect is narrower than stated —
nothing was found that does, and the plumbing was read rather than instrumented.
