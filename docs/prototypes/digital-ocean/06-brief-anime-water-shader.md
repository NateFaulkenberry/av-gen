# AV Gen Art Agent Spec: Digital Ocean — Anime Water Shader

(Owner brief, received 2026-10-15, saved verbatim.)

## 1. Objective

Design and implement a custom, real-time **Anime Water Shader** for Digital Ocean.

The shader should transform the current ocean from a conventional 3D water surface into a beautifully illustrated, continuously animated ocean inspired by hand-painted anime backgrounds, graphic Japanese wave art, and contemporary stylized animation.

The water is the hero of this scene. Its appearance should be defined by bold, flowing shapes, layered color bands, expressive highlights, stylized foam, and musical motion—not by realistic reflections, physically accurate light transport, or dense surface noise.

This is a production art task. The agent must research, prototype, render, inspect, and iterate on the actual AV Gen output.

### Primary goals

1. Establish a distinctive illustrated water style.
2. Produce large, readable, animated wave shapes.
3. Create controlled color bands and stylized lighting.
4. Generate attractive crest highlights and foam patterns.
5. Make wave intensity, color, and surface details respond expressively to audio and MIDI.
6. Integrate with the existing Digital Ocean environment, including islands, boats, dolphins, clouds, and sun rays.
7. Maintain reliable real-time performance on the existing AV Gen rendering architecture.

## 2. Research and technical reconnaissance

Before implementation, investigate the following resources and adapt their useful concepts to AV Gen's actual rendering architecture.

### Reference A — Toon water construction

https://roystan.net/articles/toon-water/

Study:
- Depth-based water coloring.
- Thresholded surface noise.
- Animated UV distortion.
- Shoreline foam.
- Smooth threshold transitions and anti-aliasing.

The most relevant lesson is how procedural noise becomes graphic, illustrated detail through carefully controlled thresholds rather than being rendered as continuous realistic texture.

### Reference B — Procedural water and shoreline effects

https://timcoster.com/2021/09/24/unity-shader-graph-procedural-planet-water-tutorial/

Study:
- Shallow-to-deep color gradients.
- Layered procedural waves.
- Depth-aware foam.
- Combining several simple signals into a richer water material.

### Reference C — Production-oriented stylized water

https://docs.unity.cn/Packages/com.unity.shadergraph%4017.4/manual/Shader-Graph-Sample-Production-Ready-Water.html

Review the distinctions between broad waves, fine surface ripples, depth fog, and different water-material approaches. Adopt only the techniques that contribute to the chosen illustrated style.

### Reference D — Existing stylized-water shader implementation

https://github.com/danielshervheim/unity-stylized-water

Study how depth coloring, Fresnel response, animated surface normals, foam, and specular effects are organized into a configurable shader.

Do not port Unity-specific rendering code directly. The project uses a different rendering stack, and its architecture must remain authoritative.

### Repository reconnaissance

Inspect:
- The existing Digital Ocean scene and current water implementation.
- The Digital Alpine scene for established scene conventions.
- AV Gen's WebGPU/Dawn shader infrastructure.
- Existing wave displacement and procedural geometry systems.
- Signal bus, parameter, modulator, and MIDI integration.
- Available depth textures, normal buffers, render targets, and post-processing.
- Shader compilation, hot reload, validation, and profiling facilities.

Determine which required effects can be implemented directly in the water shader and which require geometry, separate draw calls, or additional rendering passes.

**Do not introduce a new rendering architecture just to implement this feature.** Extend the current pipeline with the smallest coherent set of changes.

## 3. Target artistic style

The desired result is an illustrated anime ocean, not conventional 3D water with a toon filter.

Use the following visual characteristics as the baseline:

- Large, smooth, flowing wave shapes.
- Strong separation between light and shadow regions.
- Broad, deliberately designed color bands.
- Curved highlights that follow the shape and direction of waves.
- Pale foam ribbons and occasional graphic whitewater patterns.
- Restrained gradients supporting the primary shapes.
- Minimal, carefully controlled fine-scale detail.
- A coherent palette that responds to the surrounding sky and lighting.

### Suggested palette

Start with a palette such as:

| Element | Starting color direction |
|---|---|
| Deep ocean | Dark ultramarine or navy |
| Main water | Rich cobalt blue |
| Lit wave faces | Turquoise or cyan |
| Secondary highlights | Pale aqua or mint |
| Foam | Warm cream or pale mint |
| Shadow accents | Indigo or blue-violet |
| Sunlit reflection | Peach, gold, or coral |

These are starting points. Create several actual rendered variations and select the strongest.

The water should harmonize with the island, clouds, and sun rays while retaining enough contrast to remain visually dominant.

### Visual references and inspiration

Investigate Japanese ocean illustration, anime background painting, stylized surf animation, and woodblock-inspired wave composition.

Focus on transferable visual techniques: silhouette, curvature, shape grouping, negative space, limited palettes, and the placement of highlights.

Do not copy an existing artist's specific composition or reproduce a recognizable copyrighted design. Develop an original visual language for Digital Ocean.

## 4. Shader architecture

Organize the implementation into logically separated functions or shader stages. Exact file names and interfaces should follow AV Gen conventions.

Conceptually, the shader should contain these systems:

1. Wave geometry and surface orientation.
2. Base water color and depth variation.
3. Large-scale stylized color bands.
4. Animated directional surface patterns.
5. Crest highlights and foam.
6. Sunlight reflection accents.
7. Audio/MIDI modulation.
8. Distance-aware detail and anti-aliasing.

The final implementation may combine these into fewer shader stages if that is more efficient.

Avoid unnecessary texture samples, excessive procedural noise octaves, redundant coordinate transformations, and large per-fragment branches.

## 5. Wave geometry and motion

The water must have large, readable motion that supports the illustrated appearance.

### Broad waves

Use a small number of directional wave functions, such as sinusoidal or Gerstner-style waves, or reuse an existing AV Gen wave implementation if it is already suitable.

Combine different wavelengths, amplitudes, and directions to avoid a mechanically repeating surface.

The broad wave system should establish:
- Overall swell direction.
- Crest and trough locations.
- Large-scale movement.
- A controllable amplitude and speed.
- Surface normals or gradients where needed for shading.

Avoid uniform vertical oscillation of the entire ocean.

### Secondary motion

Add smaller-scale movement using a restrained combination of procedural noise, directional patterns, or existing textures.

The purpose is to break up the primary waves, not to create a realistic noisy surface.

Use different scales and speeds. Secondary patterns should follow or complement the broad wave direction.

### Geometry and shading must agree

If vertex displacement is used, the shading system must evaluate its color bands and highlights consistently with the displaced surface.

Do not compute wave crests from a completely unrelated animation signal if the result makes highlights slide independently of the geometry.

**Acceptance criterion:** the ocean should look animated and expressive with the base material alone, before foam, bloom, or post-processing are enabled.

## 6. Graphic color bands

This is one of the shader's most important features.

Create several clearly distinguishable color regions across the water.

Possible regions include:
- Deep shadow.
- Main ocean color.
- Illuminated wave faces.
- Bright crest accents.
- Shallow coastal water.

Use the surface's height, slope, orientation, wave phase, depth, or other suitable signals to determine where the bands appear.

Combine these signals deliberately. A band can follow wave geometry while its brightness depends on the sun direction.

### Threshold shaping

Use smooth thresholds or controlled ramps to create defined regions. The transitions should be crisp enough to resemble illustration, but not so hard that the surface develops distracting jagged edges.

Support a configurable band contrast or threshold parameter.

Avoid using an arbitrary quantization of final RGB values as the entire toon effect. The bands should communicate wave shape and lighting, not merely posterize the image.

### Distance treatment

As waves approach the horizon, simplify the shading and reduce excessive high-frequency contrast.

The ocean should form coherent horizontal and diagonal patterns at a distance instead of becoming a noisy carpet.

The far water should remain visually connected to the sky and horizon.

## 7. Stylized wave highlights

Create bright, elongated highlights that follow the curvature and motion of the waves.

Possible forms:
- Thin curved strokes along wave crests.
- Broad ribbons of turquoise on illuminated faces.
- Short, broken dashes of reflected sunlight.
- Isolated bright accents near the foreground.
- Graphic highlights that appear and disappear as crests rise.

Use the wave surface, its orientation, or a coherent wave-phase signal to determine highlight placement.

Combine this with a stylized lighting response, but avoid relying entirely on conventional specular reflection.

Highlights should have a designed shape and controlled width. Their size, intensity, and density should be adjustable.

### Sun reflection

Add an optional sun-glint pattern that connects the sun to the ocean visually.

It might consist of a broken path of warm highlights stretching toward the camera, distorted by the waves.

The pattern should change with the wave motion and selected palette. It should not look like a static white stripe or a realistic reflection pasted onto the surface.

Use the existing sun direction or scene configuration where possible.

## 8. Animated foam and whitewater

Foam is essential to the illustrated appearance, but it must be designed rather than scattered randomly.

### Crest foam

Generate foam in selected wave-crest regions using the broad wave geometry or an appropriate crest signal.

Create broken, elongated shapes that follow the water's motion.

The foam should:
- Appear in coherent patches.
- Move with the wave pattern.
- Break into smaller shapes as appropriate.
- Vary in width and density.
- Avoid uniform coverage across every crest.

### Graphic foam patterns

Use one or more procedural masks to break the foam into stylized shapes.

Potential techniques include:
- Thresholded noise.
- Distorted directional patterns.
- Curved procedural ribbons.
- Small, tapered streaks.
- Hand-authored or generated masks, if suitable assets already exist.

Prefer a small number of well-designed patterns over many layers of noisy detail.

### Shoreline and object interaction

Where depth information is available and useful, use it to create foam near:
- The island shoreline.
- Rocks or coastal geometry.
- Boat hulls.
- Dolphin entry and splash locations.

Do not require a new depth or normals render pass without first measuring its cost and assessing alternatives.

If the scene's geometry or camera setup makes depth-based intersection effects unreliable, implement localized foam ribbons or emitters at known interaction points.

### Foam anti-aliasing

Thresholded foam patterns must retain clean edges at the actual output resolution.

Use appropriate smooth thresholding, derivatives, or other shader anti-aliasing techniques. Avoid visibly flickering one-pixel foam fragments.

## 9. Directional surface patterns

Introduce a small amount of animated graphic surface detail.

Patterns may include:
- Elongated ripples.
- Short parallel streaks.
- Curved contour-like marks.
- Broken, stylized reflections.
- Subtle patterns that reveal the flow direction.

These patterns should follow the broad movement of the ocean.

A combination of two directional patterns at different scales may provide sufficient complexity.

If procedural distortion is used, it should make the pattern feel like it is being carried by the water, not simply sliding across a flat surface.

Do not let small-scale detail compete with the large wave shapes.

## 10. Audio-reactive shading

The shader must integrate with AV Gen's existing signal and modulation architecture.

Do not create a parallel audio-analysis system or sample audio directly inside GPU shader code.

Use the existing CPU-side analysis, signal bus, timeline, modulator, and parameter systems to provide normalized controls to the scene and material.

### Candidate mappings

| Audio signal | Water response |
|---|---|
| Low-frequency energy | Increase broad wave amplitude and deepen shadow contrast |
| Kick transients | Briefly intensify selected crest highlights |
| Snare or percussion | Accentuate foam and short surface streaks |
| High-frequency energy | Increase fine highlights and small ripple contrast |
| Sustained harmonic energy | Gradually shift the water palette |
| Overall energy | Coordinate wave intensity, foam, and highlight strength |

These are candidates rather than mandatory mappings. Tune the actual response by listening to music and watching the output.

### Smoothing and envelopes

Use smoothed signals for continuous parameters and short, controlled envelopes for transient accents.

A strong transient may briefly brighten a wave crest, but the surface should return naturally to its baseline.

Do not make every visual parameter respond to the same signal. Preserve independent behavior across large waves, small ripples, foam, and color.

The shader must look good when audio is absent or quiet.

## 11. MIDI and parameter exposure

Expose a concise set of high-level controls through AV Gen's existing parameter and MIDI infrastructure.

Suggested controls:

| Parameter | Purpose |
|---|---|
| `wave_amplitude` | Overall wave height |
| `wave_speed` | Rate of wave propagation |
| `wave_direction` | Dominant flow direction |
| `band_contrast` | Separation between illustrated color regions |
| `highlight_intensity` | Strength of wave highlights |
| `highlight_width` | Width of bright wave accents |
| `foam_intensity` | Amount and brightness of foam |
| `surface_detail` | Strength of small-scale patterns |
| `palette_blend` | Transition between coordinated palettes |
| `water_energy` | Overall coordinated water response |

Follow existing naming conventions and reuse parameters that already exist.

A MIDI controller should be able to increase wave intensity or blend between palettes across a useful range. A note trigger may create a short-lived highlight or foam accent if that behavior complements the rest of the scene.

Do not expose every shader constant to the performer. Prioritize controls with an obvious artistic effect.

## 12. Palette modulation

Support several coordinated water palettes rather than independently animating individual RGB channels.

At minimum, develop:
- A bright turquoise and cobalt palette.
- A violet and indigo sunset palette.
- A deeper electric-blue palette.

Each palette should define compatible values for the base water, shadow bands, illuminated bands, highlights, and foam.

Blend between palettes smoothly. Use the existing modulation system for musical transitions.

A palette change should affect the whole water material coherently. Avoid abrupt changes or random color cycling.

Where appropriate, allow the sky and sun to provide related color influences, but keep the water's core palette legible.

## 13. Integration with Digital Ocean

The shader must work with the actual scene composition.

Test it with:
- The current sun and sky.
- The animated sun rays.
- The island and shoreline.
- The boats and their wakes.
- The enlarged dolphins.
- The intended camera angle.
- The selected post-processing configuration.

Ensure the shader does not visually flatten the island, erase the boat wakes, or make the dolphins difficult to distinguish from the wave highlights.

If a separate foam or wake system is needed for a boat or dolphin, integrate it with the shader's visual language and palette.

Do not add unnecessary transparency, refraction, or physically based reflection effects simply because they are common in other water shaders. Use them only if they improve the intended illustration.

## 14. Performance and rendering constraints

Digital Ocean is a live audiovisual scene. The shader must be designed for predictable real-time execution.

The existing AV Gen stack uses C++23, WebGPU/Dawn, and Metal on Apple Silicon. Implement the shader using the project's established shader language and compilation pipeline.

Requirements:
- No Unity-specific shader code or rendering dependencies.
- No new renderer or backend.
- No unnecessary full-screen rendering passes.
- No unbounded per-frame allocations.
- No excessive procedural noise layers.
- No unnecessary CPU-generated geometry every frame.
- No costly real-time planar reflection system unless a measured visual benefit justifies it.
- No dependence on unsupported GPU features.

Prefer a compact shader with a few coherent systems rather than an elaborate collection of disconnected effects.

### Performance validation

Measure:
- GPU frame time with the shader enabled.
- GPU frame time with major features disabled individually.
- Performance at the intended live output resolution.
- Performance at 1080p.
- Visual quality at the horizon and in the foreground.

Use AV Gen's existing GPU profiling and render inspection facilities. Identify expensive shader features with controlled comparisons rather than guessing.

Optimize the shader without destroying the illustrated appearance.

## 15. Required implementation sequence

### Pass A — Baseline and style frames

Capture the current water and create several shader prototypes or material variations.

Compare:
- Smooth illustrated color bands.
- Strong graphic wave highlights.
- Stylized crest foam.
- A more painterly, textured treatment.

Select the strongest coherent direction.

### Pass B — Base geometry and color

Implement the broad wave response and primary color bands.

Render the water without foam, particles, or bloom.

Review the silhouette, palette, and wave motion. Fix these before adding detail.

### Pass C — Highlights and foam

Implement crest highlights, broken foam ribbons, and the selected surface patterns.

Ensure all details follow the water rather than appearing to slide independently.

### Pass D — Audio/MIDI integration

Wire the high-level shader controls into the existing parameter system. Test with several kinds of audio and MIDI interaction.

### Pass E — Scene integration

Test the water alongside the island, sun rays, clouds, boats, and dolphins. Tune the color relationships and scale of detail.

### Pass F — Visual critique

Capture representative frames and short sequences. Inspect the actual output for:
- Excessively smooth or generic water.
- Repetitive wave patterns.
- Foam that looks like noise.
- Highlights that do not follow wave shapes.
- Overly sharp or flickering thresholds.
- Color bands that become muddy or washed out.
- Excessive detail at the horizon.
- Inconsistent palette changes.
- Audio reactivity that overwhelms the base animation.

Identify the three most important visual defects, fix them, and repeat the capture.

### Pass G — Performance and validation

Profile the finished shader, verify the real live-rendering path, test repeated audio/MIDI events, and document any remaining limitations.

Do not stop at a successful shader compilation or a single attractive screenshot.

## 16. Deliverables

The final implementation must include:

1. A production-ready anime water shader integrated into Digital Ocean.
2. Coherent illustrated color bands.
3. Procedural wave motion and matching surface shading.
4. Stylized wave highlights and crest foam.
5. Controlled surface patterns and sun-glint accents where appropriate.
6. Audio-reactive controls integrated with AV Gen.
7. MIDI-accessible parameters where supported by the existing scene architecture.
8. Several coordinated water palettes.
9. Actual rendered screenshots showing the final material in context.
10. Performance measurements and a short implementation report.

Document the shader's major systems, exposed parameters, audio mappings, performance characteristics, and known limitations.

## 17. Acceptance criteria

- [ ] The water reads as illustrated or anime-inspired rather than conventionally realistic 3D water.
- [ ] Large waves have attractive silhouettes and coherent motion.
- [ ] Color bands communicate wave shape and lighting.
- [ ] Highlights follow the water's geometry and movement.
- [ ] Foam forms coherent, animated shapes rather than uniform noise.
- [ ] Surface detail is restrained and does not overpower the major forms.
- [ ] The water looks compelling without audio.
- [ ] Audio changes wave intensity, color, or detail in a perceptible and musically useful way.
- [ ] MIDI controls have predictable, meaningful effects.
- [ ] Palette transitions are smooth and coordinated.
- [ ] The material integrates with the complete Digital Ocean scene.
- [ ] The shader runs on the existing rendering architecture.
- [ ] Performance is measured at representative resolutions.
- [ ] Actual output has undergone visual critique and refinement.

## Final creative directive

Think of the water as an animated painting.

Use geometry to establish the waves, color bands to explain their shape, highlights to convey movement, and foam to add expressive accents. Keep the visual language simple enough to read instantly but sophisticated enough to reward sustained viewing.

Let music change the energy and color of the ocean without sacrificing the composition.

**The goal is not to make realistic water look like anime. The goal is to design water that looks as though it was conceived and animated specifically for an illustrated audiovisual world.**
