# ADR-037: Physical camera, exposure and camera behaviours

- Status: Accepted (2026-09-09)
- Research: `docs/research/cinematic-camera-and-color.md`

## Decision
- `scene::Camera` gains a **lens**: focal length (mm), sensor width and height (mm), aperture
  (f-number), focus distance, and shutter angle. Field of view is derived from focal length and
  sensor unless `fovYRadians` is explicitly driven, which keeps every existing scene working.
- **Depth of field** is computed from the lens: the circle of confusion drives the existing post
  effect's radius instead of a hand-set maximum, and focus can track a target (an object, a focal
  point, or a fixed distance) with a configurable speed.
- **Exposure**: an exposure block with manual EV (aperture, shutter, ISO) or automatic metering
  (average or histogram luminance over a weighted window, with speed-up and speed-down limits and
  a compensation offset). The resulting scale multiplies scene-linear colour before tone mapping.
  Deterministic: metering reads the previous frame's reduced luminance, which is part of render
  state and reproduces offline.
- **Camera behaviours** as data (`camera/behaviour/*`): orbit, dolly, push in, pull out, crane,
  track, rail (a spline), look-at, follow, reveal, slow drift and impact. Each is a small set of
  parameters over the existing camera parameters, so they are keyable, modulatable and stackable
  (a slow drift plus an onset-driven impact is two behaviours, not a special case).
- **Shutter** feeds motion blur: blur length is `velocity * shutterAngle / 360`.

## Consequences
- Positive: focus, framing and exposure become directorial choices; the Hyperspace blow-out at the
  core disappears because exposure adapts; behaviours make deliberate camera language reusable.
- Negative: two ways to express field of view (lens or angle) needs a documented precedence rule;
  auto-exposure introduces a one-frame feedback loop that must be part of deterministic state.
