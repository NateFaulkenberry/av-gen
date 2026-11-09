# ADR-1907: A look sets the attractor key's shadow bias, contact march and casting

- Status: Proposed (Art Pass 2, M3e), scene/strange-attractor.
- Research: `48-night-lighting-research.md` F2, B3 and B5.
- Code:
  - `AttractorLook::{sunShadowBias, sunContact, sunCasts}` in `src/scene/attractor_world.{hpp,cpp}`, read and written
    under `look`, and registered as live parameters;
  - the key light's per-frame update in `src/scene/composition.cpp`.
- Tests: `[attractor-world][pass2][adr1907]` in `tests/unit/test_attractor_world.cpp`.

## Context

The attractor world's key light (`attractor.sun`) takes the block's `sunShadowBias` once, at build. The canyon needs
3.2 m by day, against acne on kilometres of rock under a low sun. At night the same offset looks every receiver up
3.2 m off its surface. So the bank behind the camp showed a smeared, merged moon-shadow of the figures, the "black
capsule" (owner, M3e §9). Its contact march, on by default, also could not be turned off for the moon alone.

## Decision

The look (ADR-1886's snapshots) carries three more keys:
- `sunShadowBias`, in metres; below 0 means the block's own;
- `sunContact`, 1 or 0, the key's screen-space contact march;
- `sunCasts`, 1 or 0, whether the key casts at all.

The key light reads them every frame, like its direction and colour. The defaults (−1, 1, 1) are the light as it was,
and they are written only when set.

## Consequences

- The night look sets a small bias (0.2 m) and turns the moon's contact march off. The fire's own contact march grounds
  the camp instead.
- Every existing scene is unchanged.
