# ADR-2118: a cloud beam's yaw is a parameter

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 10; owner msg 112, the art agent's audio
  idea 4: "beams keep the bar, whip on the section").
- Code: `clouds/beam/<i>/yaw` in `registerCloudParameters` / `applyCloudParameters` (`src/scene/clouds.cpp`).
- Tests: the `[clouds]` cases (parameter layout); exercised by the Neon Dreams render.

## Context

ADR-2102's lighthouse beams turn at a fixed `rpm` from a fixed `yaw`. Only their intensity was a parameter, so a beam
could flare on a crash but never move with the music. The art asked for the sweep to lock to the bar clock and to whip
half a turn on each section crash.

## Decision

Each beam registers `clouds/beam/<i>/yaw` (degrees, default the authored yaw) beside its intensity, and the live beam
takes it. The direction stays `yaw + rpm x t`, so:

- a timeline track on it is an exactly seek-safe change of phase (Neon Dreams: +180 degrees on each crash, eased over
  0.82 s, so the next arm takes the place of the last -- the sweep whips forward without a single-frame flash);
- a route on it is a wobble or a nudge that returns (a route is a modulation around the base, not a step).

The parameter list grows by one per beam; the indices after the beams (the bobs, the wisps) moved with it.

## Consequences

The bar lock itself is data: Neon Dreams' build.py sets every beam's `rpm` to one turn per 8 bars (5.357 rpm at the
1.4 s bar) and their yaws a quarter bar (11.25 degrees) apart.
