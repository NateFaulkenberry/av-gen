# ADR-1908: Spectral distance fog

- Status: Proposed (Art Pass 2, M3g plan), scene/strange-attractor. The tests come with the full pass.
- Code:
  - `Environment::fogSpectral` (scene key `environment.fogSpectral`, parameter `scene/fogSpectral`), in
    `src/scene/scene_types.hpp` and `src/scene/composition.cpp`;
  - `frame.fogPool.yzw`, in `src/rendering/scene_renderer.cpp`;
  - `applyFog`, in `shaders/common.wgsl`.

## Context

Real water absorbs by wavelength: red is gone within a few metres of path, then green, and the image tends to
blue-green with distance (Pope and Fry, 1997). The surface fog has one grey extinction, so distance only fades
everything evenly toward the fog's colour. The owner's ocean "looks very CG" (51- §2c).

## Decision

`fogSpectral` is per-channel weights on the surface fog's optical depth: `f_c = exp(-depth * w_c)`, mixed toward the
fog's colour per channel. Its default (0, 0, 0) leaves the grey fog exactly as it was. It rides in the three unused
lanes of `frame.fogPool`, so the frame uniform does not grow.

## Consequences

- Every lit surface that goes through `applyFog` gets it: terrain, entities, programs.
- The volume march stays grey. A scene that wants colour loss everywhere keeps its march short (`volumeMaxDistance`)
  and leaves the distance to the spectral fog. The ocean does this, with 45 m.
- Particles keep the grey fog: they have their own fog block.
