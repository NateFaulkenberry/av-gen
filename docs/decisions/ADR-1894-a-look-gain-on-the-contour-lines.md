# ADR-1894: A look gain on the contour lines

- Status: Proposed (Art Pass 2, M1b), scene/strange-attractor.
- Code: `AttractorLook::contourGain`, the parameter `attractorWorld/contourGain` and the look key `contourGain`
  (`src/scene/attractor_world.*`); the terrain chunks' emission (`src/scene/composition.cpp`).
- Tests: `[adr1894]`.

## Context

The canyon's gold contour lines are driven by an audio route (`response.high` -> `attractorWorld/contour`). By day they
are part of the Pass 1 look. At dusk and night they glowed hard against the dark walls and took the frame from the
encounters. A look (ADR-1886) cannot quiet a routed parameter, because the route replaces the final after the look.

## Decision

A gain, `contourGain` (0..1, default 1), multiplies the contour on its way into the terrain program's emission. Looks
set it: dusk uses 0.25. The route is untouched and still drives the lines' rhythm.

## Consequences

- By day nothing changes (gain 1).
- Any later time of day can set its own gain.
