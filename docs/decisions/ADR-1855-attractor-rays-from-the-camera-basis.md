# ADR-1855: The Strange Attractor builds its view rays from the camera basis, not from invViewProj

- Status: **Superseded by ADR-1861** (2026-10-28: the ray-march renderer is removed; the canyon is engine geometry, ADR-1857). Was: Accepted, scene/strange-attractor (Gate 2d, 2026-10-27).
- Scope: scene-local (`src/rendering/attractor_renderer.cpp`, `shaders/attractor/world.wgsl`). No engine code changed.

## Problem: the jitter in the rendered video

The owner saw the flythrough jitter. Gate 2c's pose trace was smooth, and it was right: the camera was smooth.

**Measured on the rendered frames.** Textured patches were block-matched forward and backward from every frame,
and each feature's velocity on either side was compared. The reveal (40-46 s) showed **8.3 px median per-feature
jitter** (p90 14 px). The same tracker on a synthetic smooth pan gives 0.1 px.

**Ruled out:**
- **Not render noise.** A frozen camera changes 0.006 of 255 per frame with the river on, and exactly 0 with it off.
- **Not the march, the distance level of detail, or the encoder.** A 10× tighter march and a version without any
  distance-dependent geometry both still showed 8 px. PNG frames show the same jitter as the mp4.
- **Not frame order or the clock.** The time at draw was exactly n/30. One frame at 40.5 s matched the sequence's
  frame byte for byte.

**The cause.** The shader built each pixel's ray as `invViewProj · (ndc, 1) - invViewProj · (ndc, 0)`.
- The flythrough sets the far plane to 32 km over an automatic near plane of 0.1 m.
- At that ratio the far-plane point of the inverse is ill-conditioned in float32. Logged at draw time, the centre ray
  wandered from the camera's true forward by up to 0.35 degrees, differently every frame.
- The whole picture therefore jumped by about 8 px at 1080p.

## Decision

- **The renderer builds the ray basis on the CPU in double precision** from the frame's camera (forward, right ×
  tan(half fov x), up × tan(half fov y)) and passes it in its uniforms. The shader forms
  `rd = normalize(F + x R + y U)`.
- **Depth is still written through viewProj**, which is well-conditioned for z.

**After.** Per-feature jitter on the same section is 0.19 px median (p90 0.32 px).

## Consequences

**Other scenes may share the hazard.** Any Environment that reconstructs rays from `frame.invViewProj` with a very
large far-to-near ratio could see it. `shaders/clouds.wgsl` (ADR-1289) uses the same formula, but its scenes use
ordinary far planes. This ADR changes nothing outside this scene. If an engine-wide fix is wanted, a precise
per-frame ray basis in the frame uniforms is the natural one.

## The water (same pass)

The Gate 2c water is replaced with near-mirror, deep, glassy water. A few broad, slow, low swells drift downstream
along the baked flow, so the reflected cliffs wobble gently but stay crisp. There is a sparse pale line at the banks.
The fine cross-chop and the flecks are gone: they smeared the reflection.
