# ADR-1906: A fair, loud shadow budget, and local shadows biased in their own depth

- Status: Proposed (Art Pass 2, M3e), scene/strange-attractor.
- Research: `docs/prototypes/strange-attractor/48-night-lighting-research.md`, findings F1 and F3, fixes B1 and B2.
- Code:
  - `ShadowRenderer::update` in `src/rendering/shadow_renderer.cpp`, with `ShadowStats::dropped` and `cascadesYielded`;
  - `shadowVisibility` in `shaders/shadows.wgsl`.
- Tests: `[gpu][shadows][point][adr1906]` in `tests/rendering/test_shadows_gpu.cpp`.

## Context

At the night camp the campfire cast no shadow at all, and nothing said so. The atlas holds 8 views. The moon took 4
cascades, the fire's cube needed 6, and the cube was skipped silently. So the fire glowed through a Sasquatch and
grounded nothing.

Fixing that alone would not have been enough. A spot or cube view stores hyperbolic depth, but its bias was one constant
in that depth: (2 far-plane texels + 5 mm) / (f − n). In world terms that grows as z² / (f n): about 0.45 m at 4 m from
the fire and 1.8 m at 8 m. A low caster's shadow therefore detached, or vanished entirely.

## Decision

1. **A light that gives no light takes no shadow views.** A light at intensity 0 takes none, for example the day's
   campfire, which a look turns off.
2. **The cascades yield to local lights.** When the shadowed points and spots would not fit beside the directional
   light's cascades, the cascades drop, to no fewer than two, so that they fit. This is logged once. Two cascades
   suffice at night, where a look shortens the moon's range.
3. **Never silent.** A point or spot that still does not fit is warned once, and counted in `ShadowStats::dropped`.
4. **Perspective views are biased in their own depth.**
   - A spot or cube view's uniform carries minus its near distance in `params.z` (1 still marks a cascade).
   - The shader takes the receiver's view-space depth z (the clip w) and uses the texel at z, not at the far plane, for
     the normal offset.
   - It converts the world bias, 2 texels + 5 mm, into NDC through the depth's local slope, f n / ((f − n) z²).

## Consequences

- A scene whose cascades and local cubes fit in 8 views is unchanged.
- A scene that overflowed gets its local shadows back, at the cost of the far cascades' reach. The yield is logged.
- Local shadows now touch their casters at every distance (the GPU test measures a slab 0.3 m over a floor 11 m from the
  lamp: lit 0.88 → shaded under the slab; with the old bias the floor stayed 0.88).
- The CPU report's `depthBias` for a perspective view remains the far-plane value. The shader rescales it per fragment.
