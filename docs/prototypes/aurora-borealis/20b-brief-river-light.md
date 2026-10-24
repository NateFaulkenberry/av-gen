# Aurora Borealis pass 3: the owner's river light brief (verbatim, 2026-10-24)

Addendum to 20-brief-sabertooth.md; it governs the Kick and Snare river effects (brief §7-8).

---

### Aurora Borealis — River Light VFX Reference Study

Before further developing the Kick and Snare river effects, research and use the following visual and technical references:

- NVIDIA GPU Gems, Rendering Water Caustics: https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics
- GLSL Caustics from Overlapping Wave Normal Maps: https://salivity.github.io/glsl/article/glsl-caustics-from-overlapping-wave-normal-maps
- GPU Water Interaction and Ripples: https://zlz-studio.github.io/env/water/water-interaction/
- NVIDIA Ray-Guided Water Caustics: https://developer.nvidia.com/blog/generating-ray-traced-caustic-effects-in-unreal-engine-4-part-2/

Use these as concrete visual and technical references, not as instructions to port another engine's implementation.

**Desired visual:** a brilliant, narrow, saturated light filament traveling rapidly downstream along the actual winding river centerline. It should appear embedded beneath the reflective surface. Give it a concentrated core, soft halo, and short trailing wake. As it passes, it briefly illuminates irregular caustic-like patterns on the riverbed and nearby facets. Add a few short upward light rays and expanding surface disturbances that distort the aurora and mountain reflections.

Kick and Snare should use the same underlying implementation but have distinct colors and temporal profiles. Their attacks must remain tightly synchronized to their respective stems.

Do not use the existing perpendicular line effect. Do not simply draw a straight world-space beam or a flat colored line over the water.

Implement and review the effect in stages:

1. Correct the path-following beam and validate its movement with deterministic test events.
2. Add the localized light wake using an inexpensive shader-based approximation.
3. Add surface ripples and sparse upward rays, reusing the existing water and rendering systems where possible.
4. Test Kick and Snare together against the actual Sabertooth track.
5. Profile the combined effect at 1920 × 1080 against the 30 FPS live target.

Before adding complexity, render several variations and capture comparison clips. Select the most legible and visually integrated result rather than assuming the most physically realistic technique will look best.

Preserve the existing water reflections, mountain facets, aurora palette, and scene composition. Avoid a full fluid simulation, real-time ray-traced caustics, a new volumetric rendering system, or any expensive feature that is not necessary to achieve the desired look.

The final result should read as light energy racing through reflective water, refracting into the surrounding landscape, and briefly disturbing the surface — not as a glowing line sliding over a flat mirror.
