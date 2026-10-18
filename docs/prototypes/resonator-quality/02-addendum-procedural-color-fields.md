# Addendum — Advanced Procedural Color Fields & Resonant Gradient Shading

(Owner addendum to 00-brief.md, received 2026-10-17, saved verbatim. Coordinator note: the "supplied gradient reference images" were not attached to the message; if the owner supplies them they will be placed in ~/Desktop/av-gen-review/64-resonator-quality/gradient-references/.)

## Objective

Upgrade the color rendering in Cymatic World, Wave Form Dimension, and Resonant Silk to achieve exceptionally rich, smooth, spatially organized color gradients comparable to high-end procedural VFX and generative art.

The current scenes have promising geometry and resonant motion, but their color often reads as a relatively simple material color combined with dark shadows and bright specular highlights. We want the actual surfaces to contain much more beautiful, nuanced color.

Use the supplied gradient reference images as artistic direction.

**The central requirement is that multiple saturated colors should blend seamlessly across the surface, forming broad, flowing, continuously evolving color regions.** The transitions should feel intrinsic to the geometry, not like a color overlay or a sequence of material presets.

This is a shader and color-field design task, not merely a lighting adjustment.

Preserve each scene's existing geometry, resonant behavior, and identity. Build on the existing rendering infrastructure wherever possible.

---

## 1. Stop treating the material as a single color

Inspect the existing material shaders and identify whether the current appearance is primarily driven by:

- A nearly uniform base color.
- A small palette of discrete material colors.
- A simple gradient based on position or height.
- Audio-driven color changes applied uniformly across the scene.
- Specular reflections that obscure the underlying base color.
- Emission and bloom compensating for an otherwise dark material.

Replace or extend simplistic color assignment with a spatially varying procedural color field.

The color field must be evaluated continuously across the surface, with smooth transitions between colors.

Avoid assigning one color per lobe, cell, object, vertex region, or resonant mode. Individual structures may influence the color field, but they should not create obvious disconnected color islands unless that is an intentional artistic result.

The desired outcome is a surface where indigo flows into cobalt, cobalt into violet, violet into fuchsia, and selected regions into cyan — with intermediate colors emerging naturally from the blending process.

---

## 2. Build a layered procedural color field

Implement a small number of complementary color-field components rather than a large collection of unrelated shader tricks.

### Layer A — Broad base gradient

Create a large-scale color field that establishes the overall palette and visual organization.

Possible inputs include:

- World-space position.
- Surface-space coordinates.
- The existing resonant field.
- Low-order spatial mode contributions.
- Surface orientation, used selectively.
- A slowly evolving phase.

The base gradient should establish broad regions of color that remain visually coherent across the geometry.

It should not depend solely on world-space height or a simple vertical gradient. Such gradients often look like conventional material ramps and can disconnect from the resonant structures.

Prefer a combination of spatial coordinates and the resonant field so color flows across the actual forms.

### Layer B — Secondary interference field

Add a second, independently tunable spatial field that modifies the broad gradient.

Possible sources include:

- A second group of spatial modes.
- Phase-shifted resonant components.
- Low-frequency trigonometric fields.
- Smooth combinations of curved or radial coordinates.
- Existing resonant simulation data.
- A slowly evolving procedural field.

Combine the fields to create overlapping bands, curved transitions, and interference regions.

The secondary field should introduce visual complexity without producing noisy speckles or excessive high-frequency striping.

It must remain continuous and temporally stable.

### Layer C — Fine color variation

Introduce a restrained fine-scale contribution only where it improves the result.

It may create delicate variations within broad blue, violet, or magenta regions, but it must not dominate the color composition.

Avoid using high-frequency noise as a substitute for a designed color field.

Fine variation should never cause visible color chatter or interfere with the geometry's primary resonant patterns.

### Layer D — Geometry-aware modulation

Allow the resonant surface to influence the color field.

Potential inputs include:

- Signed field value.
- Nodal proximity.
- Curvature.
- Surface slope.
- Mode amplitude.
- Mode energy.
- Local excitation.

Use these signals to make color transitions follow or complement the geometry.

Do not force every color boundary to align with every geometric ridge. Some of the most interesting results should come from the color field crossing the geometry at different angles, creating secondary patterns and unexpected color relationships.

---

## 3. Use excellent gradient interpolation

Color interpolation is a major part of the desired visual quality.

Inspect the current interpolation method and replace any crude or discontinuous palette transitions.

Requirements:

- Smooth transitions between palette colors.
- No visible banding or abrupt hue changes.
- No excessive white or grey contamination between saturated colors.
- Stable results across the full range of the field.
- Consistent behavior in HDR and the final tone-mapped output.

Evaluate appropriate interpolation in a perceptually useful color space, or another technique that preserves saturated intermediate hues.

Do not assume that simply blending arbitrary RGB values between palette stops will always produce the desired appearance.

Where the renderer uses HDR color, keep the distinction between base-color pigmentation and emitted radiance clear. The palette should remain vivid before emission and bloom are applied.

Use enough precision and gradient smoothness to avoid visible quantization.

If banding occurs, investigate the underlying precision, interpolation, and output path rather than hiding it with excessive noise.

---

## 4. Establish deliberate, scene-specific palettes

Do not give all three scenes one identical gradient or one generic rainbow shader.

Build reusable color-field functionality where appropriate, but provide distinct art direction for each scene.

### Cymatic World — Chromatic resonance

The surface should express large, interlocking regions of saturated color.

Primary direction:

- Deep indigo and midnight blue.
- Electric cobalt and ultramarine.
- Rich violet.
- Fuchsia and saturated magenta.
- Restrained icy cyan highlights.

The color should form flowing regions around and across the resonant cells.

As the geometric modes evolve, color regions should stretch, bend, merge, and reorganize. The image should sometimes produce strong blue-violet structures with narrow fuchsia regions, and sometimes develop broader magenta or cyan fields.

Do not turn every ridge into a cyan outline. The actual body of the surface must carry rich color.

Retain the existing palette families already supported by the scene, including green, bronze, silver, and other intended variations. Improve the gradient quality within each palette rather than forcing all variants into the blue-magenta direction.

### Wave Form Dimension — Traveling chromatic waves

Use gradients that reinforce the scene's own waveform-inspired structure.

Color may form elongated bands, broad oscillating regions, and overlapping waves that intersect and change as the underlying geometry evolves.

Prefer flowing transitions between neighboring colors over sharply separated bands.

The color field should make the geometry's direction and spatial rhythm easier to perceive.

Explore phase offsets between the geometric field and color field so that the brightest color boundaries do not always coincide with the largest geometric peaks.

Preserve this scene's own visual identity rather than copying Cymatic World's cellular color distribution.

### Resonant Silk — Iridescent flowing gradients

Use broad, luxurious transitions across the folds.

Primary direction:

- Deep cobalt and indigo.
- Blue-violet.
- Violet and magenta.
- Selective cyan-blue reflections.
- Optional secondary palette families such as emerald, bronze, or pearl where already supported.

The color should travel smoothly across folds and troughs.

Avoid assigning colors to separate strips of fabric or creating abrupt changes at fold boundaries.

The material should appear richly pigmented beneath its specular reflections. The highlights should enhance the gradient, not replace it.

A subtle orientation-dependent iridescent contribution may be used as a secondary effect, but the main color should remain beautiful and recognizable when specular and emission contributions are temporarily disabled.

---

## 5. Make color evolve through the resonant system

The color field should develop over time in response to the scene's existing resonant state.

It must not be a static gradient with a hue slider attached.

Use persistent spatial phase, mode contributions, and smoothed excitation signals to evolve the pattern.

Possible behaviors:

- Broad color regions stretch as low-order modes change.
- Secondary interference bands emerge as medium-scale modes become active.
- Magenta and violet regions move relative to the dominant blue field.
- Cyan highlights emerge in selected areas during strong excitation.
- Color boundaries slowly migrate across the surface.
- New combinations appear when several modes interfere.

Use continuous transitions rather than abrupt switches between color presets.

Audio should influence the development of the field without directly controlling every color channel on every frame.

Do not reset color phase when the dominant frequency changes. Preserve temporal continuity.

Keep modulation bounded and stable so that color does not flicker or jump between unrelated patterns.

### Audio response

Reuse the existing signal bus, analysis, and modulation infrastructure.

Use existing smoothed envelopes where possible.

Potential mappings include:

- Low-frequency energy influences the broad color field's deformation or phase.
- Midrange energy changes the balance between primary and secondary color fields.
- High-frequency energy adds restrained fine-scale color variation.
- Transients briefly intensify or reveal selected color regions.

These are starting points, not rigid mappings. Tune them against real audio and retain only behaviors that look visually compelling.

The scene should remain attractive during silence, quiet passages, and sustained notes.

---

## 6. Preserve saturated midtones and visible surface color

The reference direction relies on color that is visible throughout the material, not just in its brightest reflections.

Tune the full shading response to achieve this.

Inspect:

- Base-color values.
- Diffuse response.
- Fill illumination.
- Specular intensity and roughness.
- Emission.
- HDR exposure.
- Tone mapping.
- Bloom threshold and radius.

Keep the base color vivid and readable before bloom.

Do not brighten the entire image by increasing exposure alone.

Do not increase emission until every surface becomes self-illuminated.

Do not let white specular highlights consume the saturated blue, violet, and magenta regions.

Maintain a hierarchy of dark but colored areas, rich saturated midtones, and controlled bright highlights.

The surface should remain legible when bloom is disabled.

---

## 7. Treat lighting as a complement to the color field

Once the color field looks excellent on its own, integrate it with the existing lighting.

Use lighting to reveal curvature, emphasize selected ridges, and provide dimensionality.

Do not rely on lighting alone to produce the desired color variation.

Evaluate a neutral diffuse render, a base-color-only render, and the final shaded render to verify that the gradient is genuinely present in the material.

Use subtle geometry-aware highlights where useful, but do not outline every contour.

Keep emission localized and controlled.

Bloom should enhance the brightest portions of the field while preserving the underlying color transitions.

Avoid generic lens effects, heavy blur, and excessive glow.

---

## 8. Temporal smoothness and spatial coherence

Color-field quality must hold during animation.

Avoid:

- Per-frame random color changes.
- Discontinuous palette-index selection.
- Unstable high-frequency modulation.
- Color patterns that jump when audio modes change.
- Excessive UV distortion.
- Visible field discontinuities at geometry boundaries.
- Rapid global hue cycling.
- High-frequency noise that produces shimmering.
- Gradient patterns that crawl unpredictably across the surface.

Use continuous functions and persistent phase.

If the field is derived from geometry coordinates, ensure those coordinates remain suitable as the geometry deforms. Avoid accidental texture swimming caused by unstable coordinate systems.

Compare the result at normal playback speed and under representative audio.

A beautiful still frame is not sufficient if the gradient flickers or becomes chaotic in motion.

---

## 9. Debugging and iteration

Implement and tune the color field independently before combining every lighting effect.

Capture the following diagnostic variants:

1. Base gradient only.
2. Base gradient plus secondary interference.
3. Complete color field without specular lighting.
4. Complete color field with normal lighting.
5. Final render with emission and bloom.
6. Multiple palette families.
7. Representative quiet and high-energy audio.

Use the captures to determine whether each layer adds useful structure.

Prefer a small number of excellent field components over a complicated shader whose contribution cannot be evaluated.

Keep temporary debug controls consistent with existing AV Gen conventions.

---

## 10. Performance

Measure the incremental cost of the color field and any added shader work.

Reuse existing resonant field data wherever possible.

Avoid evaluating the same expensive procedural function repeatedly in different shader stages.

Use existing GPU buffers, textures, or shader interfaces where appropriate.

Do not introduce expensive simulation or additional render passes solely to generate gradient variation if a lightweight analytic field produces an equally good result.

Validate at the target live resolution and quality settings.

Report CPU/GPU frame times before and after, especially if the new shading affects an already GPU-limited scene.

---

## 11. Definition of done

The implementation is successful when:

- The actual material contains rich, continuous color gradients.
- Multiple colors blend seamlessly across large spatial regions.
- Intermediate hues are beautiful rather than muddy or desaturated.
- The gradients form organized, curved, and interference-like patterns.
- Color evolves continuously alongside the resonant geometry and music.
- Each scene has a distinct color-field treatment.
- The existing palette variations remain available and visually coherent.
- Surfaces remain vividly colored between specular highlights.
- Lighting enhances the color rather than substituting for it.
- Bloom does not obscure the gradients.
- No visible banding, discontinuities, or temporal color chatter are introduced.
- Performance remains appropriate for live rendering.
- Before-and-after stills and short motion captures demonstrate the improvement.

## Final artistic direction

The target is not simply a more colorful shader.

The target is a surface in which color behaves like a second resonant medium: broad gradients establish the composition, overlapping fields create intricate interference, geometry gives the patterns structure, and music gradually transforms their relationships.

The result should feel like a carefully art-directed, high-end procedural VFX material — richly saturated, continuously blended, sculptural, and visually surprising over time.

Prioritize the quality of the gradients themselves before adding more effects. A beautiful color field with restrained lighting is preferable to a mediocre color field buried under bloom and post-processing.
