# ADR-1043: The project palette: named states blended in OKLab, written after the routes

- Status: Accepted (2026-09-30), proto/liminal-space
- Implemented in `src/params/palette.*` and `src/app/engine.*` (load, save, attach on scene swaps, apply).
- Tests: `tests/integration/test_liminal_journey.cpp` (`[liminal][palette][adr1043]`).

## Context

§7 and the addendum's colour script: coherent palette states and smooth transitions (cool and muted,
saturated, receding, expanding, constrained, open), no rainbow cycling, with saturation and value
controllable, across architecture, lights, fog, emissive elements and post.

## Decision

A project block `palette`: ordered `states` (role -> linear RGB colour, role -> scalar) and `bindings`
(role -> parameter, optional component, gain, replace or multiply). Three parameters drive it:
`palette/position` (a float through the states), `palette/saturation` (OKLCh chroma) and
`palette/value` (OKLab lightness). Colours blend on a straight line in OKLab, so a change passes through a
quieter middle instead of round the hue wheel. The palette writes its targets' finals after the timeline
and the routes, so the timeline keys its parameters and a route may move them; it is a pure function of
the frame's finals, so seek-exact whenever they are. A role a state lacks takes the nearest state's.

## Consequences

- The director keys `palette/position` on the timeline (a hold is two equal keys; a phrase-long change is
  two keys a phrase apart); music can move saturation or value through routes.
- Replace bindings own their targets: a route on a bound colour is overwritten. Route the palette's own
  parameters, or bind with `multiply`.
- Unknown targets are skipped and logged once.
