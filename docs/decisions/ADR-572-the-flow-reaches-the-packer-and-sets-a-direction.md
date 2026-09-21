# ADR-572: the flow reaches the packer, and it sets a direction

Status: accepted. Date: 2026-09-21. The fog brief's §17, completing Phase E.

## Context

ADR-571 gave the fog bank a drift and left §17 -- *"optionally respond to a procedural flow field:
directional wind, curl noise, world-space flow, vertical convection, terrain-following drift.
Important: flow affects movement, not basic existence"* -- explicitly unbuilt, with the reason
stated: the drift is computed in the **packer**, and `EffectResolve::pack` was handed the effect
and an envelope and nothing else. `buildAtmosphericFrame` had `rv.flow` and `rv.flowInfluence`
sitting on the line above the call.

Two routes were open and one of them was a trap.

**The rejected route: sample the wind inside `fog.wgsl`.** The march already reaches the global
wind -- `common.wgsl` includes `wind.wgsl`, which offers `windSampleAt(p, t)` off the frame
uniform -- so it would have been free. It was rejected because **`fog.wgsl` declares no bindings on
purpose**: that is what lets `test_fog_parity_gpu.cpp` compile the file alone against its CPU twin,
and reaching for `frame` inside it trades a testable unit for a convenience. The CPU twin would
then have had no way to know what the wind was, and the pair would have stopped being a pair.

## Decision

### `EffectResolve::pack` takes the resolved flow, and every implementor changed in one commit

    void (*pack)(const AtmosphericEffect&, float envelope, const MediumFlowInput& flow,
                 MediumSlot& out);

`MediumFlowInput` is a struct rather than two parameters so the next thing the air knows can be
added without touching every kind again -- this ADR paid that cost once. `influence` is 0 whenever
the effect is unsubscribed, names a dead field, or set its own subscription to 0, so a packer that
multiplies by it needs no branch, and a default-constructed value is exactly that state, which is
what lets a test pack a medium without inventing a flow.

No overload was added beside the old hook. ADR-441 is explicit that this engine takes no
compatibility shims while it is in heavy development, and **a half-converted hook is the state in
which the two versions disagree about which is authoritative.** The vortex names the parameter and
ignores it, which is correct: a vortex is a static field, and ADR-387 already gives it the only
flow response it wants -- the §68 lean that moves its centre.

One answer to *"what is the air doing here"*, two consumers: the §68 lean uses it to move **where**
the medium is, the packer to set **which way its structure travels**. That is ADR-387's whole
argument, and it is why the flow is resolved once on `ResolvedAtmospheric` rather than sampled
twice.

### The flow sets the DIRECTION and never the speed

This is the load-bearing decision and it is about **units**, not about simplicity.

`fields::FlowSample::units` exists because the two publishers genuinely differ: the wind's `speed`
is documented as a dimensionless strength (0 calm, 1 a fresh breeze) and a vortex's is metres per
second of real medium. A subscriber that integrates a displacement **must** check it. This codebase
has produced the same unit bug four separate times by not checking -- ADR-374's density, ADR-379's
spill, ADR-381's comet-on-fog, ADR-389's coefficient.

**A direction is unit-free.** Taking only the direction is therefore correct against both
publishers with no branch on `units` at all, and it makes it impossible for one artist control to
mean two things depending on which field answered. The metres a second stay the artist's, in
`driftSpeed`, where they are legible and where a slider's range means something.

`driftWind` (0..1) blends the drift direction from the bank's own long axis toward the flow's,
multiplied by the subscription's influence, and the blend is **renormalised** -- a mix of two unit
vectors is not a unit vector, and skipping that would have made the speed dip toward zero at
half-coupling for two opposed directions.

## Consequences

- **The property is tested, not argued.** `test_fog_flow.cpp` asserts that the drift's magnitude is
  exactly `driftSpeed` for flow magnitudes of 0.01, 1 and 40 at couplings of 0, 0.25, 0.5 and 1 --
  and that a `Normalised` flow of 0.3 and a `MetresPerSecond` flow of 22 in the same direction
  produce the **same** drift. A version that read the magnitude would differ there by a factor of
  the magnitude, silently.
- **Break demonstration**: taking the magnitude from the flow gives 4.51 and 3.02 where the artist
  asked for 6.0, at flow magnitude 0.01 and couplings 0.25 and 0.5 -- the control quietly becoming
  a brake.
- **Zero coupling and zero influence are both exactly inert**, which is what stops this being a
  trap for the overwhelming majority of banks that subscribe to nothing.
- **The fog block is fourteen stored rows**, counted from `grep -c '    storedFloat("'` (13) plus
  `grep -c 'storedChoice("'` (1) rather than read off the failure. Third time that distinction has
  caught something.
- **`test_fog_bank.cpp`'s nine direct `resolve.pack` calls now go through `packMediumSlot`**, which
  is ADR-566's one writer. They had to change for the signature anyway; routing them through the
  one writer instead of adding `{}` to each is the version that cannot drift again.

## Revisit when

- **A medium wants the flow's speed.** It is available and deliberately unused. The honest way to
  take it is to branch on `units` and say so at the branch, not to read `flow` and hope.
- **§16's remaining controls land** -- Turbulence, Curl, Swirl, Dissipation, Expansion,
  Contraction. Curl in particular wants a flow sampled *per position* rather than once per medium,
  which is a different shape of change: this ADR samples the field at the effect's anchor, so the
  whole bank drifts coherently, and a per-sample flow would shear the structure, which is
  turbulence rather than drift.
