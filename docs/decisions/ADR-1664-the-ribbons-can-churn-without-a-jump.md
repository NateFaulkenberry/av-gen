# ADR-1664: The aurora ribbons can churn without a jump

- Status: Accepted (scene/aurora-borealis)
- Extends ADR-1641 (ribbons). Uses the free lane `AuroraGpu::audio3.z`, so no struct grows.
- Asked for by the Aurora Borealis owner's pass-4 notes: "the aurora should act much more wildly ... something that
  makes the aurora ribbons twist and turn more chaotically".
- Code:
  - `src/world/atmospherics.{hpp,cpp}`: `AuroraShape::ribbonChurn`, its validation, and the packing into `audio3.z`
    while ribbons are on.
  - `src/world/effects/kinds/aurora_effect.cpp`: the row (Ribbons > Churn), written only when set.
  - `shaders/atmosphere_fx.wgsl`: `churn = flow.y + audio3.z` in both ribbon marches.
- Tests: `tests/unit/test_aurora_churn.cpp` (`[adr1664]`: the lane, the conditional JSON, the curtains untouched,
  validation); `tests/rendering/test_aurora_ribbons_gpu.cpp` (`[adr1664]`: 0 is the ribbons' hash; 12 changes the
  sky).

## Context

The ribbons' folds churn with `driftSpeed x elapsed`. Routing the speed multiplies the whole elapsed time, so the
pattern jumps (E4 in pass 2). A swell that should twist the sheets has no jump-free control on the folds' motion; swirl
and width change their shape, but not their motion.

## Decision

An offset added to the churn phase, in its own units. Routed from a smoothed swell, the folds churn forward as it rises
and back as it relaxes, continuously: a function of the signal, not of its history, so seek equals play. At 0 the march
adds 0.0 and is the ribbons hash for hash.

## Rejected

- **Integrating a routed speed** into a phase: it needs state over time, so a seek would replay or diverge.
- **Routing `ribbonScale`:** it swims sheets kilometres away (B, pass 3).
