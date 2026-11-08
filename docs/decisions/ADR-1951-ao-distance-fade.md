# ADR-1951: An AO distance fade, so a far field of steps does not read as creases

- Status: Accepted (Musical Geography Phase 2, 2026-11-08), scene/musical-geography.
- Code:
  - `Environment::aoFadeStart` / `aoFadeEnd` (scene JSON `environment.aoFadeStart` / `aoFadeEnd`, view depth in
    metres; both 0 by default, off), in `src/scene/scene_types.hpp` and `src/scene/composition.cpp`;
  - `AoUniforms::fade` and `aoDistanceFade` (the CPU twin), in `src/rendering/ao_renderer.{hpp,cpp}`; the fade of the
    visibility, in `fs_gtao`, `shaders/gtao.wgsl`;
  - `FrameUniforms::aoFade` and the fade of the bend, in `sampleAmbientOcclusion`, `shaders/shadows.wgsl`.

## Context

GTAO rebuilds normals from depth, so the risers between Musical Geography's stepped tiles read as creases. At reveal
altitude a riser is under a pixel and the creases turn into the "blue dashes". Turning AO off removes them, but it also
takes the AO off the near ground, where the creases are the look.

## Decision

An optional band of view depth (`aoFadeStart` to `aoFadeEnd`, smoothstep) over which AO eases to none. It works in two
places, and both are needed:

1. **The visibility** goes to 1 in the GTAO pass.
2. **The bent normal** goes back to the surface's own shading normal where the lit pass samples the AO. The pass
   builds its bent normal from the normal it rebuilt from depth. Even where nothing is occluded, that normal is the
   depth's facet (the riser) and not the shading normal, and the ambient term reads it at 0.9 strength. With the
   visibility faded alone, a third of the far AO contrast remained (`eng-r6-revealC-ao-fade-visibility-only.png`).

With `end <= start` (the default) nothing changes: a GPU test checks that a disabled band hashes identical to no band.

## Consequences

- In the art agent's revealC with a band of 200 to 420 m, the AO contrast against AO off, as a mean per channel over the
  ground, is:

  | Region | AO on | Faded |
  |---|---|---|
  | Far rows | 9.33 | 0.41 |
  | Mid rows | 5.28 | 3.49 |
  | Near rows | 3.35 | 3.34 |

  The near rows are identical to AO on (a difference of 0.01).
- The fade does not remove the faint lit remainder of the dashes, which AO off shows too.
- One vec4 in the frame uniforms.
