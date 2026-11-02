# ADR-1898: Portal rim particles and the ring emitter

- Status: **Withdrawn** (M3c: the owner did not like the rim particles; the code and its tests are removed, ADR-441).
  Was: Proposed (Art Pass 2, M3b), scene/strange-attractor.
- Code: `CompositionNode::portalRim` (`src/scene/composition.*`), `ParticleSystem::innerRadius` (`particles.hpp`,
  `particle_io.cpp`, `particle_renderer.cpp`, `shaders/particles.wgsl`).
- Tests: `[particles][pass2][adr1898]`, `[composition][pass2][adr1898]`.

## Context

The owner asked for particles on every portal's inner rim: chaotic on the approach, the destination still readable
through the centre, clearing as the camera passes through. They must follow whichever portal is live (the canyon has
three, one per leg), and a scrub must show the same frame as play.

## Decision

- **A ring emitter.** A disc emitter's `innerRadius` (0 to 0.99 of its radius) spawns only in the ring between it and
  the edge, uniform in area. The centre stays clear by construction.
- **A rim node.** A particle node marked `portalRim` is placed each frame on the live portal: at its focus, half its
  depth in front, facing along its axis, sized by its width and height and the opening's progress. Its spawn rate
  follows the field's live amount and the opening, and falls to zero as the camera comes within the opening's last
  stretch (its larger side, over a second stretch as long). Everything it reads is a function of the camera's distance and the portal's
  state at that time, so it is seek-safe; the particles' own motion is the particle system's, as for every emitter.

## Consequences

- One rim node per world serves every portal of that world; a world with no live portal hides it.
- The ring is turbulent and streaked (`portal_rim.py`): a broken, fizzing edge, not a hoop of sparks.
