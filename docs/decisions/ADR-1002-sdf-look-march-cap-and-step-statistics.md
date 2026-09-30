# ADR-1002: The SDF pass gets a look block, a march cap and step statistics

- Status: Accepted (2026-09-29), proto/procedural-space
- Extends ADR-027. Implemented in `src/scene/sdf_object.*`, `src/rendering/sdf_renderer.*` and
  `shaders/sdf_raymarch.wgsl`.

## Decision

- **`maxDistance`** caps the march in tree-local units; 0 means the bounds alone decide. The lit pass
  and the depth prepass both apply it through `sdfMarchEnd`, so the two stay equal.
- **`look`** holds three field-evaluated terms. Each is off at 0, costs nothing when off, and is a
  parameter (`look/...`).
  - Ambient occlusion: 5 taps along the normal.
  - Edge emission: the angle between the surface normal and a tetrahedron normal taken at
    `edgeWidth` (a first version used the field's Laplacian, which turned coincident faces and
    bound-only kinks into speckle; ADR-1003). It is added to both the colour and the emission target.
  - A soft shadow: a Quilez penumbra march towards `shadowDirection`, bounded by `shadowSteps`.

  This is a cheap version. The occlusion and the shadow scale the whole shaded colour, emission and
  fog included; they are not applied to the diffuse and specular terms alone. The edge emission is
  added after them and is never occluded.
- **March settings as parameters:** `march/{maxSteps, epsilon, stepScale, maxDistance}`.
- **Step statistics:** the lit pass samples every 4th pixel in x and y and accumulates the sum and the
  maximum of the steps, the ray count, the hit count and the count of rays that ran out of steps,
  using atomics in a 32-byte storage buffer. The buffer is cleared before the pass and copied to a
  3-slot MapRead ring, which is read a few frames late. `SdfStats` carries the average steps, the
  maximum steps, the hit ratio and the out-of-steps ratio. They are shown in the Render stats lines and
  printed as medians by the headless benchmark (`--headless --frames N`).
- **`depthPrepass` and `castShadows`** are per-object switches, both true by default, which is the
  ADR-027 behaviour. Each depth-only pass is a full second march over the object's rect. With a switch
  off, the lit pass still writes depth, so composition with meshes stays correct. What is lost is the
  object in the passes that read the prepass before the lit pass (GTAO, the screen-space shadow mask,
  contact shadows) and, for `castShadows`, its shadow-map shadow.
