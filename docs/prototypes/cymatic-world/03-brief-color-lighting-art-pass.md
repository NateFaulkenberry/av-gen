# AV Gen — Cymatic World: High-Intensity Color, Lighting & Resonance Art Pass

(Owner brief, received 2026-10-16, saved verbatim. Applies to the terrain-only Cymatic World; Cymatic Crystals and Cymatic Creature were cut.)

## Mission

Perform a substantial visual art pass on the existing Cymatic World live scene.

The underlying resonant shapes are already working and should be preserved. The current weakness is the presentation: the colors and lighting are too subdued, the visual energy is insufficient, and the resonance doesn't yet feel powerful enough as a fully realized audiovisual experience.

**The objective is to make the existing world dramatically more vivid, luminous, contrast-rich, and musically expressive without compromising its geometric identity or turning it into a generic neon visualizer.**

This is an art-direction and rendering-quality task, not a request to redesign the resonance system from scratch.

Study the current implementation, inspect actual renders, identify the causes of the weak visual presentation, and make a coordinated set of improvements across materials, lighting, color modulation, tone mapping, and audio-driven visual behavior.

Work autonomously. Use the existing AV Gen architecture, rendering infrastructure, scene parameters, audio-analysis pipeline, and diagnostic tooling.

## 1. Scope and non-goals

The current Cymatic World direction is focused on resonant terrain, sculptural geometry, and the shared resonance field.

**Do not reintroduce the previously considered crystal and organism concepts.** Do not expand the scene with unrelated entities or decorative objects to compensate for the current visual weakness.

Concentrate on the existing world.

The art pass should address:

- Color palette and color distribution.
- Material response and surface differentiation.
- Lighting direction, contrast, and colored illumination.
- Resonance-driven emission and color modulation.
- Geometric readability and microstructure visibility.
- Atmospheric depth and environmental illumination.
- Reflections, specular response, and optical effects where supported.
- HDR emission, bloom, and tone mapping.
- Audio-driven intensity, temporal behavior, and dynamic range.
- Camera composition and visual hierarchy where needed.

Do not change the scene's core geometry, field model, or audio architecture unless inspection reveals a concrete problem that must be addressed. If changes to those systems are necessary, keep them targeted and explain why.

## 2. Art direction: spectacular but controlled

The target is a sophisticated experimental audiovisual artwork with the intensity of an immersive light installation and the spatial richness of high-end real-time VFX.

The world should feel as if its resonant structures are generating and transforming light.

Think in terms of:

- Deep, nearly black indigo and blue-violet shadow regions.
- Saturated electric blue, ultramarine, violet, and ultraviolet-adjacent purples.
- Brilliant cyan and turquoise accents.
- Selective hot magenta, fuchsia, and pink.
- Occasional spectral white, icy blue, or pale lavender highlights.
- Optional restrained green, teal, amber, or warm-white accents where they improve the composition.

These are ingredients, not a requirement to use every hue simultaneously.

The result should have a strong visual identity rather than evenly distributing the entire rainbow across the scene.

### Desired visual qualities

**High chromatic intensity:** Colors should be visibly richer and more deliberate than in the current scene.

**Strong luminance contrast:** Dark areas must remain dark enough to frame luminous geometry.

**Bright, structured highlights:** Resonance ridges, intersections, crests, and selected geometric features should produce compelling highlights without turning the whole scene into an emissive outline.

**Rich material variation:** Surfaces should have different responses to light, rather than appearing uniformly coated in one color.

**Spatial depth:** Foreground, middle ground, and distant structures should have different visual weights and lighting conditions.

**Musical dynamism:** The world should become more visually intense as the music excites it, but it must remain compelling during quieter passages.

**Restraint:** Not every surface should glow. Not every frequency should trigger a hue change. Not every transient should produce a flash.

The scene should feel like a designed world, not a collection of effects.

## 3. Visual research and references

Use these references to inform the art pass. Study the underlying visual principles rather than copying any particular implementation.

### A. Physical cymatics and low-angle illumination

Reference:
https://cymatics.refocus.de/experiments/water-cymatics

The useful lesson is how directional illumination reveals the topography of a resonant surface. Side-lighting creates strong light-and-shadow structure, whereas overly broad overhead illumination can wash out fine detail.

Apply this principle to the existing terrain and sculptural surfaces.

Use grazing light to reveal ridges, folds, depressions, and fine geometric structure. Experiment with opposing colored lights so that the same surface has different highlights and shadow colors depending on its orientation.

Do not simply illuminate every surface from the camera direction.

### B. Perceptually informed color modulation

References:

https://colorcet.com/userguide/index.html

https://matplotlib.org/stable/tutorials/colors/colormaps.html

These references explain how color mappings communicate different kinds of information.

Use the ideas selectively:

- Sequential mappings for quantities such as local resonance energy or excitation intensity.
- Cyclic mappings for phase-like or periodic quantities.
- Diverging mappings where positive and negative deformation or another meaningful signed quantity should be distinguished.
- Controlled hue and luminance variation to reveal spatial structure.

These are useful design principles, not a mandate to use scientific visualization palettes literally.

The goal is to make color modulation visually rich while retaining structural readability.

Avoid arbitrary full-spectrum hue cycling as the primary color system.

### C. Resonant mode visualization

References:

https://github.com/evoluteur/cymatics

https://github.com/redsquidleader/mpro-cymatics-labs

Use these to understand how standing-wave patterns, interference, frequency changes, and particle arrangements can produce recognizable visual structures.

The new scene should go beyond the appearance of a conventional cymatics simulator. Its existing three-dimensional geometry, material response, lighting, and spatial depth should all contribute to the result.

### D. Color and lighting in real-time rendering

Inspect AV Gen's existing material shaders, lighting implementation, HDR render targets, bloom, and AgX tone-mapping pipeline.

Work with the renderer's actual capabilities and limitations. Do not introduce a new rendering architecture just to implement an art pass.

If a desired visual effect is already supported by the renderer, use that capability rather than approximating it with redundant effects.

## 4. Color system: build a coherent palette, not a rainbow effect

Develop a deliberate color language for the entire scene.

The current presentation needs a significant increase in color richness and visual differentiation. However, a simple increase in saturation or a rapidly changing HSV hue is not sufficient.

### 4.1 Establish a dominant palette

Start by testing a small number of related palettes.

**Palette A — Ultraviolet resonance**

- Near-black indigo shadows.
- Deep ultramarine base color.
- Electric blue and violet structural illumination.
- Cyan-white highlights.
- Selective magenta accents.

This is the primary direction to test first.

**Palette B — Plasma interference**

- Deep navy and blue-black surfaces.
- Brilliant cyan and turquoise.
- Violet and fuchsia.
- Occasional pale spectral highlights.
- Highly selective pink-white accents at the most intense resonant features.

This should feel more energetic and chromatically aggressive than Palette A.

**Palette C — Spectral heat**

- Deep violet and blue shadows.
- Electric blue and cyan.
- Controlled transitions through magenta and hot pink.
- Small areas of amber, coral, or warm white for contrast.

This is an alternate direction for evaluating whether warm accents make the scene more dimensional and visually memorable.

These palettes are starting points. Compare them using actual renders and select the strongest direction. Do not blend all three into an indiscriminate rainbow.

### 4.2 Use multiple sources of color information

Investigate using several independent properties to influence the final surface color:

- Base material identity.
- Local resonance energy.
- Modal contribution or frequency region.
- Surface orientation and curvature.
- Geometric crests and troughs.
- Local strain or deformation magnitude, if available.
- Reflected environmental light.
- Emission intensity.

The result should have spatially meaningful color variation even when the music is quiet.

For example, a surface could have a deep blue-violet base, cyan highlights on selected ridges, and magenta coloration where another resonant mode contributes strongly.

As the music changes, the relative contributions can shift. The geometry and material identity should remain recognizable.

### 4.3 Avoid color banding and arbitrary segmentation

Do not map a smooth field into a handful of hard color bands unless the geometry or a deliberate artistic effect justifies it.

Avoid sudden hue boundaries that appear unrelated to the underlying resonance.

Use continuous palette transitions, carefully shaped thresholds, or localized accents to reveal the field's structure.

Keep sufficient luminance contrast to distinguish neighboring features.

A saturated palette is useful only if the viewer can still read the geometry.

### 4.4 Color modulation must be musically meaningful

Color should respond to the underlying resonance, not simply to elapsed time.

Investigate mappings such as:

- Bass energy increasing the intensity or spatial extent of a deep blue or violet response.
- Midrange excitation shifting the balance between violet, magenta, and cyan contributions.
- High-frequency excitation revealing fine-scale accents or sharper highlights.
- Sustained excitation building a stable color presence.
- Transients producing localized, brief increases in brightness or spectral intensity.
- Mode interference changing the color distribution where multiple responses overlap.

These are examples to test, not rigid frequency-to-color assignments. Choose mappings that make sense with the existing field and the actual audio source.

Do not make every kick flash white, every snare turn pink, or every high-frequency event trigger a rainbow.

The important property is that the colors feel like different expressions of the same physical system.

## 5. Lighting: reveal the geometry and amplify the color

Lighting is likely one of the biggest opportunities for improving the current scene.

Inspect the actual lighting setup before adding more lights. Determine whether the present weakness comes from light placement, insufficient contrast, excessive ambient illumination, material roughness, exposure, tone mapping, or a combination of these factors.

### 5.1 Establish a deliberate lighting hierarchy

Experiment with the following roles:

**Primary directional or key light**

A strong light source that reveals the principal geometry and establishes the dominant direction of illumination.

**Colored fill light**

A complementary colored light that introduces richness into shadow regions without eliminating their depth.

**Grazing or rim illumination**

Lighting that catches ridges, edges, folds, and sculptural contours. This is particularly important for revealing the complexity of the resonant terrain.

**Localized resonance illumination**

Where appropriate, use existing geometry, emission, or a small number of localized lights to make highly excited regions appear more luminous.

**Environmental and distant illumination**

Subtle illumination that gives depth to distant surfaces and keeps the environment from collapsing into a completely black background.

These roles do not necessarily require separate physical lights. Use the most efficient combination of existing lights, material shading, emission, and renderer capabilities.

### 5.2 Use complementary colored light

Test deliberate combinations such as:

- Blue key light with violet or magenta fill.
- Cyan highlights against deep indigo shadow.
- Violet illumination with selective warm-white accents.
- Opposing cool hues illuminating different orientations of the same structure.

The aim is to create rich color relationships across the actual geometry.

Avoid uniformly bathing everything in purple. That may be atmospheric, but it will not solve the lack of visual differentiation.

### 5.3 Preserve shadow detail and geometric relief

The scene needs deep shadows, but not large featureless black regions that hide important shapes.

Likewise, brighter illumination must not flatten the geometry.

Tune the balance between direct lighting, ambient or environmental illumination, material response, and emission.

Pay particular attention to grazing angles and the way the existing terrain normals interact with colored lights.

If the geometry looks substantially more compelling in a grayscale lighting diagnostic, use that information to improve the lighting before adding more color.

### 5.4 Make lighting react to resonance selectively

Investigate whether the shared field can influence localized lighting or material illumination.

Potential behaviors include:

- Strong excitation increasing the illumination of selected ridges.
- Interference regions producing localized highlights.
- Sustained resonance maintaining a luminous region.
- Excitation propagating across a surface through a coordinated sequence of lighting changes.
- Quiet passages allowing the scene to return to its underlying ambient appearance.

Prefer localized, spatially coherent responses to uniform global brightness pulses.

Do not add hundreds of dynamic point lights. Use the most efficient available mechanism, whether that is existing lighting, emissive materials, shader evaluation, or a supported lighting approximation.

## 6. Material response: make surfaces feel rich and dimensional

The world should not look like a collection of matte objects with neon outlines.

Inspect the current material implementation and identify opportunities for more sophisticated surface response.

### 6.1 Base material differentiation

Use a limited family of complementary material responses.

Possible treatments include:

- Dark satin or mineral-like surfaces that reveal their geometry through directional lighting.
- Smooth, reflective regions that carry broad colored highlights.
- Fine-grained surfaces with subtle roughness variation.
- Selected glossy or wet-looking areas where they reinforce the scene's visual identity.
- Emissive features integrated into the geometry rather than simply drawn over it.

These are alternatives to evaluate, not a requirement to introduce every material type.

Preserve the underlying shapes. The materials should enhance their structure, not turn them into unrelated objects.

### 6.2 Roughness and specular response

Investigate roughness, specular intensity, metallic response, and the availability of environment reflections in the existing renderer.

Use roughness variation to create a mix of broad highlights and sharper reflections.

Avoid making every surface mirror-polished. Uniformly glossy surfaces can produce a generic synthetic appearance and make the scene visually noisy.

If the renderer's reflection capabilities are limited, prioritize well-controlled direct highlights and material shading rather than introducing an expensive new reflection system for this art pass.

### 6.3 Emission should reveal resonance

Use emission to emphasize selected geometric structures and field-derived features.

Emission should have a meaningful spatial distribution. It should reinforce the geometry and underlying resonance rather than becoming an independent outline effect.

Consider separate contributions for:

- A restrained base emission.
- Resonance-dependent emission.
- High-intensity localized accents.
- Optional transient highlights.

Keep the brightest contributions sparse enough that the viewer can identify the focal regions.

### 6.4 Preserve the surface's three-dimensional appearance

Avoid shader treatments that make the scene look like a flat, glowing wireframe.

Where the existing geometry supports it, combine solid surface shading, real geometric relief, specular response, and selectively illuminated details.

Fine features should complement the larger structures, not overwhelm them.

## 7. HDR, emission, bloom, and tone mapping

Investigate the entire path from material output through the HDR render target, bloom, exposure, and final tone mapping.

The existing AV Gen renderer uses an HDR pipeline and AgX tone mapping. Work within that pipeline and verify the actual output rather than assuming that higher emissive values will automatically produce a better image.

### 7.1 Make the brightest regions genuinely bright

The scene needs a stronger sense of luminous energy.

Create a meaningful separation between:

- Dark base surfaces.
- Ordinary illuminated surfaces.
- Bright resonant features.
- The most intense emission and highlight accents.

Use the HDR range intentionally.

### 7.2 Tune bloom rather than maximizing it

Bloom should produce a controlled halo around the most luminous structures.

Tune the threshold, intensity, radius, and any supported quality settings in relation to the actual scene.

Too much bloom will merge neighboring patterns, erase fine detail, flatten the geometry, and make the image look blurry.

The desired result is intense light surrounded by a subtle atmospheric response, with the underlying shapes remaining crisp.

### 7.3 Inspect the effects of AgX tone mapping

Check whether high-saturation emissive colors are losing their intended identity through tone mapping or exposure.

Compare the material output and final rendered result.

Adjust emission, exposure, and supported color-processing settings to retain rich colors and readable highlights without clipping large regions into featureless white.

Do not bypass the existing tone-mapping pipeline without a demonstrated reason.

### 7.4 Maintain highlight detail

A few nearly white highlights can be valuable, but they should be deliberate.

Avoid turning all high-energy regions into white blobs. Preserve the color identity of the strongest blue, cyan, violet, and magenta features wherever possible.

## 8. Introduce a coherent audio-driven intensity model

The scene should feel powerful when the music excites it, without behaving like a screen-wide strobe.

Use the existing resonance field and audio-analysis system. Do not build a second independent audio-reactivity framework.

### 8.1 Separate persistent energy from transient accents

Investigate a layered response:

- A slowly evolving base color state.
- A persistent resonance-energy contribution.
- Faster localized responses to transients.
- A high-intensity accent response for exceptional moments.

These responses should have different time constants.

Sustained notes should establish a visual presence. Short transients should add definition and impact without resetting the entire palette.

### 8.2 Make color and brightness respond independently

Do not treat color, luminance, and emission as the same parameter.

A resonance change could alter hue without producing a large brightness jump. Another event could increase brightness while preserving the current hue.

This separation is essential for preventing a chaotic visual result.

### 8.3 Use spatial localization

Where the field supports it, let the strongest responses occur where the relevant resonant mode is strongest.

For example, an excitation could illuminate a particular set of ridges or reveal a region of constructive interference while the rest of the terrain remains comparatively subdued.

Prefer effects that have a meaningful relationship to the spatial field over uniform screen-wide pulses.

### 8.4 Smooth the response

Use existing smoothing, damping, or temporal filtering mechanisms where appropriate.

Avoid visible color flicker, rapid hue jitter, abrupt changes caused by noisy FFT bins, and emission values that fluctuate uncontrollably.

Do not smooth everything into sluggishness. The system should retain transient definition while preventing unstable visual noise.

### 8.5 Quiet passages must remain beautiful

The scene should have a carefully designed low-energy state.

When the music becomes quiet, the world should retain its material identity, atmospheric depth, subtle illumination, and recognizable structure.

Do not allow the environment to disappear into blackness whenever the audio drops.

Likewise, do not keep everything at maximum intensity all the time. The visual range between quiet and loud is part of the artistic experience.

## 9. Post-processing and optical accents

Explore additional post-processing only after improving the base lighting, materials, and color distribution.

Potential candidates include:

- Subtle resonance-correlated optical distortion.
- Controlled chromatic separation at extreme highlights, if supported and artistically justified.
- Reflections that reinforce the underlying surface structure.
- Selective atmospheric haze or depth effects.
- Carefully tuned bloom.
- Other existing renderer effects that improve the result without obscuring the geometry.

Every effect must serve a clear visual purpose.

Do not add multiple full-screen effects merely to make the scene more intense. Avoid generic heat-haze distortion, excessive chromatic aberration, persistent lens flares, or a permanent glowing fog.

The actual resonant geometry must remain the primary visual subject.

## 10. Camera and composition

Review the existing camera framing alongside the lighting and material changes.

The camera should reveal the world as a three-dimensional environment rather than a flat technical visualization.

Check:

- Foreground and background separation.
- The visibility of large-scale resonant structures.
- Whether highlights guide the eye toward important regions.
- Whether the scene has a clear focal hierarchy.
- Whether the brightest areas are distributed intentionally.
- Whether distant structures retain depth without competing with the foreground.
- Whether the camera angle reveals the terrain's geometric relief.

Do not solve composition problems by adding unrelated objects.

If a small camera adjustment substantially improves the readability of the existing shapes, make it. Preserve the scene's established identity.

## 11. Implementation workflow

Follow this order to avoid layering effects on top of unresolved problems.

### Pass A — Diagnose the existing image

Capture representative screenshots of the current scene.

Inspect:

- Base material colors.
- Lighting-only output, where supported.
- Emission contribution.
- Final tone-mapped output.
- Normal and depth diagnostics, where available.
- Representative audio states.

Determine why the scene currently looks boring. Distinguish between insufficient chromatic range, weak lighting direction, flat material response, inadequate geometric readability, and tone-mapping or bloom problems.

Record a baseline for performance.

### Pass B — Establish the visual identity

Test the candidate palettes using the actual existing geometry.

Choose the strongest direction based on spatial readability, color richness, and visual impact.

Do not immediately introduce complex audio-driven modulation.

First produce a static or low-energy render that already looks excellent.

### Pass C — Rebuild the lighting and material response

Improve light direction, colored fill, shadow depth, material roughness, specular response, and selective emission.

Capture and inspect the result before proceeding.

The scene should look significantly better even without strong audio excitation.

### Pass D — Integrate resonance-driven color and intensity

Connect the visual response to the existing field.

Implement meaningful spatial and temporal variation, ensuring that color, brightness, and emission remain independently controllable.

Test quiet passages, sustained tones, transient-heavy audio, and dense passages.

### Pass E — Tune HDR and post-processing

Only after the base scene is strong, tune bloom, tone mapping, exposure, and any additional optical effects.

Inspect fine details at the intended live resolution.

### Pass F — Iterate on actual renders

Capture the new scene, inspect it, identify the remaining weaknesses, and make targeted changes.

Do not stop after the first successful implementation.

Compare against the original baseline and verify that the improvements are visible in the final output.

## 12. Visual acceptance criteria

The art pass is not complete until the rendered scene satisfies the following criteria.

**Color**

- The world has a distinctive and coherent color identity.
- The palette is substantially richer than the baseline.
- Multiple hues contribute meaningfully to the geometry.
- The result does not resemble a uniformly purple scene or an indiscriminate rainbow shader.

**Lighting**

- Grazing illumination reveals the resonant geometry.
- Colored highlights and shadows provide depth.
- Surface orientation visibly affects illumination.
- The scene has a clear luminance hierarchy.

**Materials**

- The existing forms have richer and more differentiated surface responses.
- Emission enhances the geometry instead of replacing it.
- Fine details remain visible around bright regions.

**Musical behavior**

- Sustained energy and transient accents have distinct visual responses.
- Color, brightness, and emission can evolve independently.
- Spatial responses are related to the resonance field.
- Quiet and loud passages are both visually compelling.

**Rendering quality**

- Bloom is controlled.
- HDR highlights preserve useful detail.
- Tone mapping does not destroy the intended palette.
- There is no distracting flicker, excessive clipping, or pervasive visual blur.

**Artistic quality**

- The scene feels substantially more intense and polished.
- The underlying shapes remain the central subject.
- The result is recognizable as a coherent world rather than a collection of effects.
- The visual improvement is obvious when comparing before and after screenshots.

## 13. Performance and regression requirements

This remains a live AV Gen scene.

Preserve the existing rendering backend and scene architecture. Do not introduce a new graphics API or a separate rendering path for this art pass.

Measure the CPU and GPU costs of the changes using the existing profiling tools.

Avoid unnecessary dynamic lights, redundant full-screen passes, per-frame allocations, expensive new reflection systems, and repeated evaluation of the same field.

Prefer the most efficient existing mechanism capable of achieving the intended result.

Retest the scene at its intended live resolution and at 1080p where applicable. Record any material performance regression and the reason for it.

Verify that the existing cymatic plate and unrelated scenes remain unaffected.

Update scene parameters, tests, or documentation only where the implementation actually changes those systems.

## 14. Final report

Provide:

1. A concise explanation of the diagnosed weaknesses in the original presentation.
2. The selected palette and the alternatives evaluated.
3. The lighting and material changes implemented.
4. The audio-to-color and audio-to-emission mappings.
5. The HDR, bloom, and tone-mapping adjustments.
6. Before-and-after screenshots captured from the actual AV Gen renderer.
7. Representative screenshots during quiet, sustained, and transient-heavy audio.
8. CPU and GPU performance measurements.
9. Any remaining visual shortcomings and the next most valuable art direction experiment.

Be explicit about which changes produced the greatest improvement.

## Final artistic principle

**Make the resonance itself feel luminous.**

The world should not merely become brighter when the music gets louder. Its resonant structure should reveal itself through color, illumination, material response, and spatially coherent changes in intensity.

Preserve the shapes that are already working. Give them extraordinary color, dramatically better light, rich surface response, and a meaningful relationship between the music and the world.

The desired result is a sophisticated, high-intensity audiovisual environment that remains geometrically readable, visually intentional, and compelling enough to serve as one of AV Gen's flagship live scenes.
