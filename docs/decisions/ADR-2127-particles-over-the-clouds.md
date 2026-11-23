# ADR-2127: particle systems drawn over the clouds (`overClouds`)

- Status: Accepted (2026-11-23), avagent/neon-dreams (Neon Dreams, iteration 17; owner msg 253 at 59.27 s:
  "fireworks still clipping behind clouds"; msg 258 at 36.97 s: "firework also appears behind cloud")
- Code: `scene::ParticleSystem::overClouds` (`particles.hpp`; `particle_io.cpp` reads it and writes it only when on);
  `ParticleRenderer::draw(pass, scene, overClouds)` and `anyOverClouds` (`particle_renderer.{hpp,cpp}`); the pass
  "particles-over-clouds" after the Environment seam (`scene_renderer.cpp`).
- Tests: `tests/unit/test_particle_io.cpp` `[adr2127]`.

## Context

Neon Dreams' fireworks were placed by a visibility test (it16) that summed the cloud between the lens and each burst
-- and the shells the owner marked had none in front (optical depth 0.03 at 58.97 s). They sank anyway, their lower
halves cut along the contour of the cloud bank BEHIND them. The cause is the frame's order: additive particles draw
in the scene pass and write no depth, and the clouds (an Environment, ADR-1200) are marched afterwards against the
depth buffer -- at a spark's pixel they see sky depth, march the cloud behind the spark, and composite it over it.
A spark in clear air in front of a cloud bank is drawn under that bank.

## Decision

A particle system may ask to be drawn after the Environment renderers: `overClouds: true`. The renderer opens one
pass after the seam on the scene pass's own targets (loaded) and its depth (tested, not written) and draws those
systems there; the scene pass skips them. No such system, no pass: every other scene is drawn exactly as before.

## Consequences

- A system over the clouds is drawn over cloud IN FRONT of it too (it has no depth for the march to stop at). That is
  right for sparks in clear air and wrong for a system inside or behind cloud, so it is opt-in and the scene keeps its
  sight-line test for the cloud in front (Neon Dreams: the heart's optical depth <= 0.35, the stars' ring and their
  fall <= 0.5).
- The volume (fog) composite still runs after it, so the sparks keep their fog.
- A proper fix would give the march a depth for translucent particles (a front-most particle depth target); this is
  the cheap, exact-for-clear-air version.
