# ADR-380: The motes leave the tree and the vortex takes them

- Status: Accepted (2026-09-19)
- Phase 7, and the moving half of Phase 11. Extends ADR-370 (the measured canopy emitter),
  ADR-379 (the static half of the same connection).

## Decision

Phase 7 needed almost no new engine. `scene::ParticleSystem` already has an attractor (radial pull
toward a point) and an `orbit` (tangential force around it), which together *are* a vortex force;
ADR-370 already measures an emitter from a named node's canopy; ADR-370 already couples particles to
the wind. A system with those three and a long lifetime is the brief's "tiny fragments of cosmic
energy" that "eventually become influenced by the cosmic vortex".

What was missing is the one thing that makes it §11 rather than decoration: **the attractor has to
BE the vortex, not a copy of its coordinates.** `vortexAttractor` on a particle node takes the
position from `environment.vortex` at bake and scales the radius by `vortexReach`. §9 asks for the
vortex's relationship to the island to survive the island moving; a hand-typed attractor stops
describing the thing it was copied from the moment anything moves, which is the same argument
ADR-370 made for the canopy emitter and ADR-376 made for sharing the wind body's frame.

## The measurement, and the property it exposed

Over 51 s of continuous playback, tracking the added luminance by frame band against an identical
arm with the attractor switched off:

| | above the tree | around it | below the island |
|---|---|---|---|
| drift (no attractor) | 55.67 | 60.96 | 50.13 |
| **pull** | **45.11** | **62.68** | **52.69** |

Consistent in all three bands: light leaves the region above the tree and arrives around and below
it. That is the flow the brief asks for, measured rather than asserted.

**And it takes 30 to 50 seconds of playback to develop.** The motes are born around the canopy at
y ≈ 100 and the vortex mouth is at y = −70, so they have 170 m to travel. My first four arms were
rendered over five seconds and showed **nothing** below the island — not because the force was
wrong but because no particle had had time to arrive. ADR-367 recorded that pools start empty after
a seek and at the head of a render; this is the first effect where that is not a detail. A short
render cannot show this, and neither can a scrub.

That is worth stating plainly rather than filing as a limitation: an effect whose evidence needs a
minute of playback is one that will be judged as broken by anyone who checks it the way everything
else in this branch has been checked.

## Density

Shipped at `spawnRate` 42 with `emissive` 1.5. At 120 and 2.2 the motes read as a swarm that
competes with the tree — §23 again, and the third time in this branch that the first tuning of a
particle system has been too dense. The rejected arm is not subtle-and-wrong; it is genuinely
visible and genuinely worse, which is the failure mode that needs a picture rather than a metric.

## The measurement

`tests/unit/test_vortex_attractor.cpp`, 2 cases, 13 assertions, no GPU:

- A bound system's attractor is the vortex's centre and `radius × reach`, from a scene file.
- **Control:** a system in the same scene that did not ask keeps what it authored. Without it the
  test passes equally against a pass that rewrote every attractor in the scene.
- **Second control:** asking to be bound when there is no vortex leaves the authored value alone
  and does not clamp it to the origin — which is the state of every scene in the repository that
  has no vortex.

## Consequences

- The motes are additive and emissive, so they interact with bloom. At the shipped brightness that
  is what makes them read as energy rather than dust; turned up it is the first thing that will
  blow out.
- `vortexReach` 6.0 means the pull is felt 1.2 km out. That is deliberate — particles need to feel
  it well before arriving or they fall past the mouth instead of spiralling in — but it also means
  any particle system that opts in is affected across most of this scene.
