# ADR-133: A material tier is three rungs selected by a per-draw uniform

**Status:** Accepted
**Date:** 2026-09-13

## Context

Phase D's purpose is "reduce per-invocation cost for pixels that do not deserve full PBR", and its
named risk is **shader variant explosion — 36 WGSL shaders is already a lot to keep correct**
(risk 7). Phase B leaves it a target: after every arm that phase could pull, **~8.4 ms of opaque
shading remains that no arm removes**, and that is the largest block left in the frame.

Two constraints shape the mechanism before any of the content is decided.

**ADR-118 says the branch must be wave-uniform.** Phase B implemented a bit-exact skip of the
contact march for fragments whose answer provably could not change — 0 of 1,024,000 pixels differed
— and measured it **4.4% slower**, in 7 of 8 interleaved pairs run in both orders. The branch it
added was lane-varying, and a lane-varying branch around a twelve-iteration loop with a dependent
texture load in it costs more than the loop it skips. In a fragment shader, saving work per lane
saves nothing unless the whole wave skips. So a per-*fragment* material tier is a pessimisation by
measurement, and only a per-*draw* tier is worth building.

**Risk 7 says the rungs must not be pipeline variants.** A tier per material per path is a
combinatorial pipeline table. A tier read from a uniform inside one shader is not a variant at all.

## Decision

**Three rungs, in one shader, selected by a uniform that is constant across a draw.**

| rung | what it removes |
| --- | --- |
| `Full` | nothing — byte for byte the pre-ADR-133 shader |
| `ReducedLights` | the clustered local-light loop capped at a budget; no contact march on local lights |
| `Flat` | every shadow term (no mask read, no cascade lookup, no blocker search, no contact march), specular, the AO sample, the IBL lookup, the normal map, and a tighter local-light cap |

Four things about the shape are deliberate.

**Directional lights are never capped.** There are at most three of them, they reach every fragment,
and removing one is a lighting change rather than a cost reduction. The budget applies to the
clustered local lights, which is where the per-fragment count multiplies — Glowmere has 222 of them.

**`ReducedLights` keeps the contact march for directional lights and drops it for local ones.** The
directional contact term is what grounds a plant on the terrain; the local one is 222 lights' worth
of the most expensive loop in the renderer. Same lever, different value.

**The ladder is monotone.** Every rung removes work the rung above it did and adds nothing. That is
what lets `worstTier` bound it with one comparison instead of a second band table, and what makes
"bigger is never cheaper" an assertable property.

**The tier's *table* lives in `QualitySettings` and the tier's *assignment* in
`MaterialTierPolicy`.** The shader reads a budget out of the frame uniform and the CPU reads the
same budget out of `QualitySettings::localLightBudget`; one function each side, reconciled by
`QualityPolicy::forTier` (ADR-134).

## What each rung is worth

Measured, and reported in full in ADR-136. Glowmere, 1280×800, realtime, one session, interleaved
pairs under `tools/gpu-lock.sh`: the flat rung reaches **5.77 ms of GPU frame (+40.9%)**, about
5.9 ms of the scene pass, so it is the first thing in this project to reach a majority of Phase B's
8.4 ms residual. The reduced rung reaches **0.79 ms (+5.6%)**.

Those are **ceilings**, taken with the rung forced on every draw. What a shipping frame realizes
depends on how much of the frame is small enough to assign it to, and ADR-138 bounds that.

## Consequences

* Two new A/B arms (`matreduced`, `matflat`) and one bound arm (`matlights0`). Each is a claim that
  the setting is worth pricing; ADR-117's warning that an arm nobody reads is an arm that rots
  applies.
* `Full` must stay byte-identical. Every new branch is `tier < N` on a value that is zero at Full,
  and the visual gate compared the baseline arm's capture against the pre-change frame.
* The shader's tier and the CPU's live in two languages and cannot share a header. The mitigation is
  that each has exactly one accessor — `materialTierLocalLights` in `common.wgsl`,
  `QualitySettings::localLightBudget` in C++ — and the tier tables are next to each other in the
  same commit.
* Nothing assigns a tier yet: the tier is frame-global, for the reason ADR-135 records.
