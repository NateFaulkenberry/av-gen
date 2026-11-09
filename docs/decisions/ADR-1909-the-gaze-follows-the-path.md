# ADR-1909: The gaze follows the path

- Status: Proposed (Art Pass 2, M3g), scene/strange-attractor.
- Code:
  - `PortalBlock::{lookAhead, lookDown, climbLook}` (portal keys `lookAhead`, `lookDown`, `climbLook`) and their use
    in the portal camera, in `src/scene/composition.{hpp,cpp}` and `composition_portal.cpp`;
  - `AttractorGen::{flightLookAhead, flightLookDown}` (attractorWorld keys), in `src/scene/attractor_world.{hpp,cpp}`.

## Context

The owner's standing rule (54- §1): the camera looks where it goes, and at the next portal, never at a character. A
per-frame trace of the angle between the camera's forward vector and its velocity found these leads on the curves
and dives, all fixed in the engine:
- the portal flights looked at the path 1.2 s ahead, leading by up to 60°;
- the canyon's flythrough looked 2.5 s ahead and 12% down, leading by up to 41°.

## Decision

Each is authored, and every default is the old value:
- A portal block's `lookAhead` (seconds, 1.2), `lookDown` (fraction of the look distance, 0.1) and `climbLook` (how
  much of a climb is looked up at, 0.25). Each is written only when set.
- An attractor world's `flightLookAhead` (2.5) and `flightLookDown` (0.12). They are hashed only when set, so every
  existing world keeps its key.

## Consequences

The Strange Attractor sets 0.35 s, 0.02 and 1.0 on every portal, and 0.6 s and 0.03 on the canyon. Its p95 angle per
leg is now under 15°. The only spans over 25° are the half-second arrivals out of a portal, where the dive's
velocity hands over to the new line.
