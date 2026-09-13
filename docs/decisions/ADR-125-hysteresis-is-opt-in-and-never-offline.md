# ADR-125: Hysteresis is opt-in, and offline never gets it

**Status:** Accepted
**Date:** 2026-09-13

## Problem

A representation selector that compares a metric against a threshold every frame, with no memory,
produces two artefacts — and together they are what the brief calls popping:

- a drawable whose metric sits on a threshold flips state every time the camera breathes;
- every drawable at a given size crosses its threshold on the *same frame*, so a whole band of the
  world changes at once.

ADR-082 established both the artefacts and their two separate fixes for scattered instances on the
GPU: a **dead zone** (memory) for the first, a per-instance **spread** (decorrelation) for the
second. The CPU selector needs the same two, and needs to reach the same answers — an entity and a
scattered copy of the same asset standing beside it must not disagree.

But hysteresis is not free of consequence. It reads the previous frame, so with it on **what you see
depends on how the camera arrived and not only on where it is**. This engine promises
frame-independence, and §5.9 is explicit that an offline render must not silently inherit a realtime
compromise.

## Alternatives

1. **Hysteresis on by default in realtime**, off offline. Rejected: it makes the realtime image
   camera-history-dependent by default, and ADR-082 already decided this exact question the other way
   for the GPU ladder. Two subsystems with opposite defaults for one property is worse than either
   default.
2. **No hysteresis; rely on spread alone.** Rejected: spread decorrelates a population but does
   nothing for a single object sitting on its own threshold, and an authored entity is a population
   of one. The oscillation test demonstrates the artefact directly.
3. **Off by default everywhere, forced off offline, exposed as policy.** Chosen.

## Decision

`RepresentationPolicy::hysteresis` defaults to **0.0 in every tier**, including realtime. Turning it
on is an explicit act by whoever is willing to trade frame-independence for stability, exactly as
`LodSettings::lodHysteresis` already is.

`QualityTier::Offline` sets `forceTopRepresentation`, which returns `FullMesh` / LOD 0 for every
drawable whatever its screen size, and pins hysteresis and spread to zero. Offline therefore has no
representation decision to be unstable about.

The dead zone is **one-sided, in the direction that matters**: it takes a larger metric to come back
than it took to leave. Transcribed from `cull.wgsl`'s `minScreenRadius` test rather than reinvented,
including its `instanceHash` / `ladderHash` sequence, so the two systems place their offset
thresholds identically.

`RepresentationChoice::held` reports whether the dead zone is what produced this frame's answer,
computed by running the same decision with the dead zone removed and comparing. "Is hysteresis doing
anything" is then observable rather than inferred.

## Consequences

- **Nothing in the frame path consumes the selector yet.** This is deliberate and is stated here
  because it is the reason the canonical frames are unchanged: the CPU decision exists, is tested and
  is calibrated, and the wiring into submission rebases onto the fragment agent's work per the wave
  ordering. A selector that is built and not yet connected is a smaller risk than one connected
  before the transition question (C2) is settled.
- **C2 is an assumption here, not a decision.** This work assumes no temporal AA is or will be
  available to the selector, so hysteresis and spread are the only stabilisers it has, and it must
  not depend on cross-frame blending. If C2 concludes that temporal rendering becomes a prerequisite,
  the cross-fade attaches to `RepresentationChoice::changed` and nothing in this ADR moves.
- The selector's memory is keyed on the drawable's index, so a caller whose indices moved must
  `reset()`. That invalidation rule is stated on the method and tested; a previous choice read
  against a different object is the derived-copy defect this engine has nine of.

## Rejected alternatives

- **Deriving the dead zone from screen velocity** (more hysteresis for a fast camera). Plausible and
  unmeasured. Adding an unmeasured heuristic to a mechanism whose whole risk is "the image depends on
  history" makes the dependence harder to reason about, not easier.
- **A different hash from `cull.wgsl`'s.** A spread that is merely similar puts an entity and its
  scattered twin on different rungs in exactly the band where the spread matters.

## Revisit when

C2 reports. If a transition mechanism lands that makes a representation change visually free, the
default may be worth revisiting — a dead zone is a way of avoiding a visible change, and a change
that is not visible does not need avoiding.
