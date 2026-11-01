# ADR-1886: Looks: parameter snapshots that start at a crossing

- Status: Proposed (Art Pass 2, Milestone 1), scene/strange-attractor.
- Code: the scene key `looks`; `Composition::setLooks`, `journeyCrossings`, `lookIndexAt`, `applyLook`
  (`src/scene/composition_portal.cpp`, `src/scene/composition.cpp`).
- Tests: `[portal][pass2][adr1886]`.

## Context

With one canyon for the whole journey (ADR-1885), dusk, night and dawn must be the same world under other parameters.
The change has to land exactly at a crossing (the camera is in another world while it happens), on seek as on play.

## Decision

A scene may carry `"looks": [{"name", "from", "params": {path: value}}]`. `from` is a crossing index along the
journey (0 = the start, 1 = after the first crossing, ...). At the start of each frame the composition finds the look
in force from the journey's crossing times and writes its values to each parameter's base (an authored value, as a preset sets it; the final is reset to the new
base, so a look is not an influence the Inspector has to name). The first time a
parameter is touched its authored value is captured, so leaving a look restores it.

- The look is a function of time only: a seek lands on the same look as play.
- When the crossing times are not yet known on a frame (the child's crossing is found on its first update), the index
  is undecidable; the frame re-runs its update once at the end.
- Routes still run after the look and may modulate its values (an audio route on a parameter replaces its final).

## Consequences

- The dusk canyon is 30 lines of parameter values in `build_journey.py`, including the Sasquatches' and the campfire's
  `visible`.
- An audio route on a parameter beats the look's value for it (the canyon's contour lines keep their `response.high`
  route at dusk). A look that must quiet a routed parameter needs a scale on the route; none does yet.
