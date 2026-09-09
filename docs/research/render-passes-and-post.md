# Render passes, selective post and temporal stability

Status: research (2026-09-09). Decisions: ADR-035, ADR-039, ADR-040.

## 1. What the engine renders today

One HDR colour target plus depth, then post. There is no normal, velocity, emission or identifier
buffer, which blocks ambient occlusion, contact shadows, screen-space reflections, object motion
blur, selective post and most debugging.

## 2. The minimum useful set

| Pass | Format | Enables |
|---|---|---|
| Depth (exists) | Depth24Plus | fog, DOF, soft particles, contact shadows, AO, reflections |
| Normal + roughness | RGBA8 or RG16F octahedral + R8 | GTAO, SSR, bent normals, debug |
| Velocity | RG16F | motion blur, temporal reprojection |
| Emission | RGBA16F (or a bloom mask channel) | selective bloom, halation |
| Identifier | R32U packed object and material id | selective post, masks, debug, picking |

Octahedral normal encoding halves the normal buffer without visible loss (Cigolle et al., "A Survey
of Efficient Representations for Independent Unit Vectors", JCGT 2014). Writing them from the same
shading pass avoids a second geometry pass, which matters because instanced and raymarched geometry
would otherwise need duplicate paths.

## 3. Selective post

Once an identifier and emission buffer exist, post effects can be masked: bloom weighted by
emission rather than by luminance alone, halation only on warm highlights, sharpening only on the
hero object, grain excluded from the fog. That is precisely the brief's requirement that post
should enhance rather than compensate, and it is what stops "everything glows".

## 4. Temporal stability

The failure modes to avoid are named in the brief: flickering volumetrics, noisy reflections,
shimmering procedural texture, popping LOD. The standard toolkit:

- Deterministic per-pixel noise indexed by frame (a scrambled low-discrepancy sequence), never a
  wall-clock random, so offline renders reproduce.
- Temporal reprojection with a velocity buffer and neighbourhood clamping for the noisy passes
  (volumetrics, AO, reflections). Reprojection needs the previous view-projection and the velocity
  buffer, which is another argument for auxiliary passes first.
- Blue-noise dithered ray offsets for the volume march instead of white noise; the artefact becomes
  a fine stable grain rather than a boiling pattern.
- LOD hysteresis and dithered transitions to stop popping.

## 5. Quality tiers

Preview, realtime, high and offline differ only in counts: shadow resolution and cascade count,
volume steps, AO samples, reflection steps, temporal history length, particle capacity. The scene
stays identical, which the brief requires for offline parity. The tier is a parameter like any
other, so a render job can raise it without touching the world.
