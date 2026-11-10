# ADR-1967: The key light and the sky's ambient as material inputs

- Status: Accepted (Musical Geography CP-D r2, CPD-R2 addendum "the night clouds", 2026-11-10),
  scene/musical-geography.
- Code: `scene::frameKeyLight` (`src/scene/sky.{hpp,cpp}`); `FrameUniforms::keyLightDirection`, `keyLightColor`,
  `skyAmbient` (`src/rendering/scene_renderer.{hpp,cpp}`, `shaders/common.wgsl`); the `keyLightDirection`,
  `keyLightColor` and `skyAmbient` inputs (`material_program.{hpp,cpp}`, `shaders/material.wgsl`, set in
  `procedural.wgsl` and `pbr_shade.wgsl`).

## Context

The owner on the night clouds: "too much like a mirror reflection of the ground". The art agent fixed what data
could fix. A silver lining needs the moon's direction inside the material program: brightening the faces and edges
that look toward it (`dot(normal, L)`), and forward scatter where the view runs toward it (`dot(viewDirection, L)`).
No input carried a light's direction.

## Decision

Three frame-wide material inputs:

- **`keyLightDirection`**: xyz points toward the frame's key light; w = 1 when one is up.
- **`keyLightColor`**: rgb = its colour × temperature × intensity; w = its luminance.
- **`skyAmbient`**: rgb = the analytic sky's mean of zenith and horizon × its intensity (`resolveSky`, the same
  answer the sky is drawn from); w = 1 when the sky is enabled.

**The key light** is the strongest enabled directional light above the horizon, by luminance. A shadow caster wins
over a light that casts none. With nothing casting up, the strongest of any directional that is up is used.

In Musical Geography, the DayNight curves drive `sun` and `moon` (both casters) and a `fill` (no shadow). So the key is
the sun by day and the moon once the sun has set or dimmed to zero, and never the fill. It is a pure function of the
frame's lights, so seek equals play, and it moves with the arc's keys.

The existing `skyKeyLight` (the sky's sun, the first Key-role directional) is unchanged. It picks the sky's disc, not
the shading key, and it never switches to the moon.

## Consequences

- `FrameUniforms` grows by 48 bytes. `portalClip` stays last.
- Tests:
  - `test_sky` [adr1967]: the sun by day; the moon by night, over a brighter fill; the fill when no caster is up; none
    when nothing is up.
  - `test_material_gpu`: parity of the three inputs.
