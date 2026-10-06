# ADR-1167: The sky can mirror itself below the horizon

- Status: Accepted (proto/digital-mosh)
- Amends ADR-036 (the procedural sky) and ADR-345 (the analytic background).
- Found by: DIGITAL MOSH pass 6. The owner's correction asked for a still mirror to every horizon. Below the horizon,
  the procedural sky was a flat `groundColor`, and the engine has no screen-space or planar reflection.

## Decision

`SkySettings::mirror` (JSON `sky.mirror`, parameter `env/sky/mirror`, range 0-1, default 0). With `m > 0`, the
radiance of a direction below the horizon gains `m × (1 − band) × (skyAbove(d′) − ground)`.

- `d′` is the direction reflected about the horizon, `(d.x, −d.y, d.z)`.
- `skyAbove` is the gradient plus the sun disc and aureole, without the horizon band.

At `m = 1`, a world with no ground looks down into its own sky, sun included. The same formula is applied in three
places, which are transliterations of each other:

- `scene::skyRadiance` (CPU);
- `environment.wgsl` (the cube, and so the IBL);
- `sky_background.wgsl` (the background pass; the value travels in a new appended `FrameUniforms::skyMirror`).

The mirror enters the sky's hash and its rebuild tolerance only when it is non-zero, so every existing sky keeps its
hash and its cube.

## Consequences

- Objects are not reflected; only the sky is. DIGITAL MOSH builds each object's reflection as SDF geometry
  (a `mirror` across y = 0). This lets a reflection be something else: the eye's reflection is a moon.
- Test: `sky mirror reflects the sky above the horizon below it` (`[adr1167]`) checks five things:
  - below equals above at m = 1;
  - the ground colour is unchanged at m = 0;
  - the sun's reflection is as bright as the sun;
  - nothing changes above the horizon;
  - the hash is unchanged when the mirror is unused.
