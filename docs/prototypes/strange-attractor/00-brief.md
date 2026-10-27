# AV Gen Scene Specification: THE STRANGE ATTRACTOR
## Research-led procedural surrealist world generation

**Project:** AV Gen — C++ audiovisual 3D engine  
**Scene:** The Strange Attractor  
**Primary objective:** Build a visually distinctive, generative 3D world inspired by fractal mathematics, nonlinear dynamical systems, recursive geometry, procedural landscape generation, and surrealist painting.

**Performance target:** Ideally 1080p at 30 fps for live audiovisual performance. This is a soft requirement, not a hard acceptance gate. If a more computationally expensive approach produces substantially better imagery, preserve it as an offline-rendering option rather than sacrificing the artistic result to hit the live budget.

**Working principle:** Research deeply, inspect the engine, establish an artistic direction, build a working vertical slice, evaluate actual rendered results, and iterate until the scene feels like a coherent surrealist world rather than a generic procedural terrain demo.

---

# 1. Mission and artistic intent

Create a navigable, mathematically generated landscape that feels like a surrealist painting whose underlying geometry was discovered inside an unfamiliar dynamical system.

The world should be strange, organic, structured, and compositionally deliberate. It should combine recognizable landscape forms with impossible geometry, recursive structures, warped spatial relationships, and nonlinear patterns.

The scene must be interesting to look at even when no audio is playing. Audio reactivity should reveal, distort, illuminate, and transform the world rather than compensate for weak underlying art direction.

The desired result sits somewhere between:

- A surrealist landscape painting translated into 3D.
- An alien geological environment generated through mathematical rules.
- An impossible landscape containing recursive monuments and distorted topography.
- A physical manifestation of a strange attractor or nonlinear system.
- A handcrafted environment whose forms reveal self-similarity at different scales.

These are reference directions, not instructions to reproduce a particular artist, game, or existing image.

## 1.1 What the scene must not become

Avoid the following failure modes:

- A generic Perlin-noise landscape with a fancy material.
- A conventional mountainous game environment with procedural placement.
- A literal 3D Mandelbrot set or a math-visualization screensaver.
- Endless randomly generated spikes, crystals, cubes, or floating rocks.
- A uniform field of equally detailed fractal geometry.
- Generic cinematic fog, teal-and-orange grading, or a default rainbow shader.
- An empty black void with a handful of glowing objects.
- Constant camera rotation, arbitrary object spinning, or unrelated pulsing.
- Excessive emissive outlines, bloom, transparency, and post-processing used to conceal weak geometry.
- A technical demonstration of every mathematical technique in one frame.

The scene should have visual hierarchy, a recognizable world silhouette, deliberate negative space, and one or two powerful focal formations.

The underlying mathematical complexity should support the composition rather than dominate it.

---

# 2. Research phase — mandatory before substantial implementation

Do not jump straight to coding terrain noise and instanced rocks. Conduct independent technical and artistic research first.

Use at least two parallel research workstreams if the available agent orchestration supports them. They should investigate different parts of the problem rather than produce redundant summaries.

## 2.1 Research workstream A: procedural mathematics

Investigate practical applications of the following techniques in modern real-time and offline graphics.

### A. Fractal terrain and multiscale noise

Research:

- Fractional Brownian motion (fBm).
- Ridged multifractal noise.
- Hybrid multifractal terrain functions.
- Domain warping and nested domain warping.
- Height-field terrain synthesis.
- Multiscale erosion-inspired shaping and ridge formation.
- Spectral characteristics of different noise families.
- The relationship between noise frequency, terrain scale, and visual readability.

Determine which combinations produce recognizable, art-directable geological formations rather than uniformly noisy terrain.

Investigate whether simple height fields are sufficient for the first prototype or whether selected regions benefit from displaced meshes, signed-distance fields, or other representations.

Do not implement every technique simply because it exists.

### B. Strange attractors and nonlinear dynamical systems

Investigate several well-established systems, potentially including:

- Lorenz attractor.
- Rössler attractor.
- Thomas cyclically symmetric attractor.
- Aizawa attractor.
- Halvorsen attractor.
- Other bounded nonlinear systems that generate visually distinctive trajectories.

Determine which systems offer the best combination of recognizable structure, controllable parameters, numerical stability, and useful geometric output.

Research appropriate numerical integration methods, including fixed-step Runge–Kutta integration where justified. Establish how transient trajectories should be discarded, how curves should be normalized, and how degenerate or unstable parameter combinations should be handled.

The objective is not to render a familiar attractor as a floating wireframe. Explore ways to turn trajectories into:

- Thickened organic ribbons.
- Tubular geological formations.
- Branching or nested structures.
- Embedded ridgelines.
- Contour-like paths running through terrain.
- Repeated geometric formations distributed along selected trajectory regions.

Use attractors where their geometry contributes to the world. Avoid adding them as disconnected decorations simply to demonstrate the algorithm.

### C. Recursive and self-similar geometry

Investigate:

- Recursive subdivision.
- Branching systems and L-systems.
- Space-colonization-inspired growth where appropriate.
- Recursive ridge and arch generation.
- Hierarchical instancing.
- Self-similar forms at different spatial scales.
- Procedural mesh generation with bounded recursion and explicit budgets.

Find an approach that can generate a small number of distinctive landmarks with large-scale silhouettes and meaningful secondary detail.

A recursively subdivided rock should remain recognizably the same underlying formation across multiple scales. It should not become a collection of unrelated geometric fragments.

### D. Spatial distortion and surrealism

Research practical ways to violate conventional spatial expectations without destroying readability.

Explore:

- Coordinate-domain warping.
- Nonlinear coordinate transforms.
- Nested spatial motifs.
- Controlled scale transitions.
- Folded surfaces and impossible-looking connections.
- Repeated structures that change apparent scale with depth.
- Deliberate perspective manipulation through geometry and composition.

Distinguish real geometric distortion from optical illusions created through camera perspective, lighting, occlusion, and scale cues.

Prefer techniques that generate visually convincing results within AV Gen's rendering architecture.

### E. Relevant precedents

Research procedural-world systems and environment-generation techniques from game development, demoscene graphics, technical art, and offline procedural rendering.

Useful starting points include:

- The original work on fractal terrain and multifractal noise.
- Primary literature on strange attractors and nonlinear dynamics.
- Technical papers or documentation describing procedural environment generation.
- Game-engine and graphics-programming resources explaining practical implementation trade-offs.
- Technical art breakdowns of stylized, non-photorealistic environments.
- Examples of procedural geometry that successfully combines repetition with composition.

Prioritize primary research, implementation details, reproducible demonstrations, and technically credible sources over generic AI-generated summaries.

## 2.2 Research workstream B: AV Gen architecture and visual feasibility

Inspect the actual repository and determine how this scene should be built using existing systems.

Read the relevant architecture documentation and inspect:

- Scene registration and scene lifecycle.
- Existing procedural geometry generators.
- Mesh creation and buffer-upload paths.
- Instancing support and instance-data formats.
- Materials, shaders, and shader compilation conventions.
- Lighting, shadows, fog, and atmospheric rendering.
- Environment and sky rendering.
- Post-processing and tone mapping.
- Audio-analysis and modulation interfaces.
- Parameter exposure and scene defaults.
- Camera control and time-dependent animation.
- Resource lifetime and cleanup.
- Profiling and GPU timing tools.
- Offline rendering, if available.
- Scene tests and end-to-end validation.
- Existing examples of complex scenes that render reliably.

Determine which components can be reused and which require new code.

Do not assume the engine supports a feature just because a similar feature exists in another engine.

Investigate the best representation for each major element. A fractal terrain mesh, an instanced recursive formation, and an attractor-derived ribbon may need different construction strategies.

Identify the real limitations of the renderer, but do not prematurely eliminate an ambitious approach because it might be too expensive for live rendering.

## 2.3 Research workstream C: artistic direction and visual experiments

If parallel subagents are available, use a third workstream for artistic exploration.

Investigate several distinct visual interpretations of the same core concept:

1. Mineral and geological surrealism.
2. Dark, dreamlike landscapes with unusual spatial geometry.
3. Painterly chromatic environments with bold color-region separation.
4. Recursive monumental formations embedded in terrain.
5. Attractor-inspired topologies that form coherent landscapes.

Explore relevant visual references, procedural art studies, and environment-design principles.

The research should answer:

- What creates a compelling large-scale silhouette?
- How can a world look mathematically generated without looking sterile?
- What makes repeated geometry feel organic instead of mechanically duplicated?
- How can a limited palette produce rich visual variation?
- How should foreground, midground, and background differ?
- Which forms remain legible at a 1080p output resolution?
- How can the world feel uncanny without relying on familiar science-fiction clichés?

Do not spend the entire research budget collecting reference images. Produce actionable recommendations and, where feasible, small rendered experiments.

## 2.4 Research deliverables

Before substantial implementation, produce a concise research report containing:

- The most promising mathematical techniques and why they were selected.
- At least two viable world-generation architectures and their trade-offs.
- A recommended representation for each major geometric element.
- A proposed artistic direction and palette.
- Relevant AV Gen systems that can be reused.
- Major implementation risks.
- Live-rendering and offline-rendering considerations.
- A prioritized implementation plan.

Include source links for important external findings. Clearly distinguish established findings from artistic hypotheses.

Do not let research continue indefinitely. Once there is enough evidence to make a defensible technical and artistic decision, move into implementation.

---

# 3. World-generation architecture

Build a hybrid procedural environment rather than forcing every visual element through a single algorithm.

The recommended conceptual architecture has six layers.

## 3.1 Layer 1: deterministic world identity

Define a world seed and a set of explicit generation parameters.

The same seed and parameters must produce the same world geometry, placement, and initial material distribution.

Use deterministic random-number generation. Avoid dependence on frame timing, unordered iteration, or incidental global random state.

The seed should influence the generated world in meaningful ways:

- Terrain topology and ridge placement.
- Relative size and orientation of major formations.
- Recursive branching decisions.
- Attractor selection or initial conditions where appropriate.
- Material-region placement.
- The arrangement of secondary details.

Different seeds should create genuinely different compositions, not merely change the positions of identical objects.

Provide a way to regenerate the world from a seed without restarting the entire application if the engine lifecycle permits it.

Separate deterministic world generation from time-dependent animation and audio modulation.

## 3.2 Layer 2: primary terrain

Generate a compact, visually coherent landscape.

Begin with a height-field representation unless research demonstrates a compelling reason to use another representation.

The terrain should contain:

- Three to five large-scale geological masses or ridge systems.
- Distinct highlands and lower valleys.
- Broad, readable slopes.
- Secondary ridge structures.
- Smaller-scale surface detail.
- Deliberate areas of negative space.

Use a small number of complementary noise fields with different spatial frequencies and clearly defined roles.

For example:

- Low-frequency fields determine major landmass placement.
- Ridged fields determine the primary geological silhouette.
- Mid-frequency fields produce secondary formations.
- High-frequency detail adds subtle surface variation.

Domain warping may alter the sampling coordinates to produce twisted, flowing, or folded formations.

Do not add all frequencies at maximum strength. High-frequency noise should not obscure the main silhouette.

### Terrain requirements

- World dimensions and scale must be explicit.
- Height amplitude and spatial frequency must be independently controllable.
- Terrain resolution must be bounded.
- Normals must be computed consistently with the chosen geometry representation.
- Terrain must not contain accidental holes, NaNs, or degenerate geometry.
- Edge behavior must be intentional.
- The camera must not reveal a conspicuous abrupt terrain boundary during normal viewing.
- Geometry should have enough resolution for its intended silhouette and camera distance.

Do not build a complete infinite-world streaming system for this prototype.

Use a finite environment with an intentionally composed horizon. If an infinite-looking world is needed, achieve it through fog, distant silhouettes, atmospheric blending, and carefully designed terrain boundaries.

## 3.3 Layer 3: recursive monumental formations

Generate two or three major structures that make the scene recognizable.

These should be the primary landmarks, not arbitrary decorative objects.

Possible forms include:

- A mountain ridge that branches into nested ridges.
- A monumental arch whose interior contains smaller versions of its own shape.
- A folded geological slab that appears to turn back through space.
- A cluster of connected rock masses with progressively smaller repeated forms.
- A large looping formation derived from a selected attractor trajectory.

Choose a small subset and execute it well.

Use recursive construction with explicit limits on depth, node count, mesh count, and generated vertices.

Each major formation should have:

- A distinctive silhouette visible from the main camera.
- A clear relationship to the terrain.
- A controlled degree of self-similarity.
- Large-scale surface variation.
- A deliberate material assignment.
- Enough asymmetry to avoid an obvious repeated procedural stamp.

Use shared meshes, instancing, or hierarchical geometry where appropriate.

Avoid excessive recursion in regions that contribute little to the final image.

The strongest landmarks should occupy distinct regions of the composition and should not all compete for the same focal point.

## 3.4 Layer 4: branching rivers, fissures, and connective structures

Introduce a branching system that visually connects the major landscape forms.

This may represent a river, luminous mineral vein, geological fissure, or abstract path through the environment.

Generate a primary path and derive secondary branches using deterministic rules.

The resulting structure should:

- Follow the terrain where appropriate.
- Branch into progressively smaller paths.
- Vary in curvature and branch spacing.
- Avoid perfectly uniform Y-shaped splits.
- Maintain coherent width and tapering.
- Avoid obvious intersections and disconnected fragments unless intentional.
- Contribute to the viewer's understanding of the world.

Do not require a physically accurate hydraulic simulation.

A branching curve system, constrained by the terrain field, is a reasonable initial approach.

Use a mesh ribbon, tube, terrain decal, or another renderer-compatible representation based on the intended appearance.

The system should support alternative material treatments: reflective mineral, dark river, pale stone, or subtle emissive fissure.

Emissive intensity should be controlled and should not turn the entire landscape into a neon circuit board.

## 3.5 Layer 5: attractor-derived structures

Generate one or more secondary structures from selected nonlinear dynamical systems.

Treat these as part of the world, not an overlay.

Investigate whether attractor trajectories work best as:

- Tubular meshes.
- Ribbons with varying width.
- Embedded geological paths.
- Large suspended loops.
- Secondary formations emerging from major ridges.
- Repeated instances distributed along selected parts of a trajectory.

The selected representation must have sufficient geometric stability and visual thickness to remain legible in the rendered scene.

Normalize trajectory bounds, avoid numerical singularities, and impose explicit limits on the number of samples and generated vertices.

If a system generates excessive self-intersection or visually noisy geometry, investigate alternate parameters or another attractor before abandoning the concept.

Avoid showing several unrelated attractors simultaneously in the first prototype. One well-integrated system is preferable to a gallery of floating mathematical shapes.

## 3.6 Layer 6: material and atmospheric composition

The world needs a clear artistic treatment.

Use a deliberately constrained palette and assign colors by meaningful spatial or material regions rather than simply mapping height to a rainbow.

Recommended starting palette:

- Deep indigo and violet for shadowed terrain.
- Muted mauve for secondary slopes.
- Ochre, sandstone, or aged bronze for major ridges.
- Restrained rust or coral for fissures and selected landmarks.
- Pale mineral or dusty rose for atmospheric highlights.

These are starting points, not mandatory colors. Test at least one darker variation and one more chromatic variation.

Use lighting to reveal geometry, not flatten it.

The scene should have:

- A directional key light or an equivalent clearly defined lighting source.
- Broad tonal separation between foreground, middle distance, and background.
- Controlled ambient illumination.
- Atmospheric depth that does not erase distant silhouettes.
- Subtle material roughness variation.
- Restrained bloom where supported.
- A sky or background that belongs to the same visual world.
- A consistent output through AV Gen's existing tone-mapping pipeline.

Avoid excessive material complexity if it does not materially improve the image.

Do not rely on heavy post-processing to make weak procedural geometry interesting.

---

# 4. Composition and camera

The scene is an environment artwork, not merely a collection of generated objects.

Establish a deliberate composition before adding large amounts of detail.

## 4.1 Initial camera

Begin with a fixed or very slowly moving camera at a viewpoint that reveals:

- A strong foreground formation.
- A clear middle-distance valley or path.
- Several major silhouettes.
- A distant ridge system.
- At least one recursive landmark.
- The relationship between the terrain and the branching connective structure.

A slightly elevated viewpoint may work well, but do not assume an aerial perspective is automatically best.

Test a small number of camera placements and select the one that gives the strongest sense of spatial depth and impossible geography.

Use a perspective camera unless research and visual tests establish that another projection is more effective.

Avoid aggressive camera movement during the first art pass.

## 4.2 Camera behavior

Once the static composition is compelling, add a slow cinematic drift if appropriate.

Camera motion should be smooth, restrained, and independent of the frame rate.

Avoid continuous spinning, rapid zooming, aggressive shaking, or camera movement that makes it difficult to understand the geometry.

If the scene eventually supports user-controlled camera movement, ensure that the environment remains coherent from more than one viewpoint. However, do not delay the initial artwork to implement a complete free-flight navigation system.

---

# 5. Audio-reactive behavior

Audio reactivity should feel like the music is changing the rules of the world.

Do not map every available signal to every available parameter. Establish a few expressive relationships with clear cause and effect.

Inspect the actual AV Gen audio-analysis and modulation interfaces before implementing these mappings. Use existing general-purpose analysis signals and available stem-specific signals. Do not depend on inferred kick, snare, or hi-hat classifications.

All mappings must use supported, documented engine mechanisms. Do not invent a parallel audio system or bypass the normal scene parameter path.

## 5.1 Initial mapping proposal

### Bass energy → geological deformation

Bass energy should control a bounded, low-frequency deformation field.

Potential effects:

- Expand or contract broad ridge structures.
- Alter the amplitude of large-scale domain warping.
- Slightly change the shape of selected valleys.
- Increase the apparent depth of selected fissures.
- Modulate the scale of a few recursive formations.

The effect should be visible at moderate audio levels without causing constant violent movement.

Prefer deformation of selected fields or shader parameters over regenerating the complete world every frame.

### Transient strength → brief structural events

Use an appropriate transient or onset signal to trigger occasional visual accents.

Potential effects:

- A brief highlight traveling along a ridge.
- A short-lived fracture-like illumination.
- A subtle displacement of an attractor-derived structure.
- A rapid color accent on selected geological features.
- A localized wave traveling through the branching network.

Events should have appropriate attack and decay behavior.

Do not simply flash the whole screen on every onset.

### Synth or sustained harmonic energy → atmospheric transformation

Use a sustained energy signal to control gradual environmental changes.

Potential effects:

- Shift the relative balance of the material palette.
- Reveal additional secondary detail.
- Change atmospheric color.
- Modulate the apparent depth of the landscape.
- Gradually alter the visual prominence of recursive structures.

Avoid introducing high-frequency flicker from a slow-changing musical signal.

### Overall energy → scene intensity

Overall audio energy may influence:

- Key-light intensity within safe limits.
- Atmospheric contrast.
- Material emissive intensity.
- The amplitude of selected secondary effects.

This mapping should not overwhelm the more specific musical relationships.

### Silence or low energy → compositional clarity

During quieter passages, the world should become calmer and easier to read.

Allow deformation to settle, emissive details to recede, and the underlying color regions and silhouettes to become more prominent.

Do not require the world to return to a completely static default state after every musical event.

## 5.2 Modulation requirements

- Use the existing AV Gen parameter and modulation architecture.
- Smooth continuous parameters appropriately.
- Use bounded output ranges.
- Avoid unbounded accumulation or long-term numerical drift.
- Separate instantaneous transients from sustained energy.
- Ensure that the world remains visually coherent under sustained loud input.
- Ensure that no single mapping makes the scene unusable.
- Provide meaningful defaults.
- Make parameters available through the existing scene-control interface where supported.
- Keep the world-generation seed and structural parameters separate from continuous audio modulation.

Prefer smooth, same-frame or otherwise correctly synchronized parameter updates consistent with the engine's existing architecture. Do not introduce avoidable audio-to-render latency.

Do not rebuild all terrain meshes in response to every audio update.

---

# 6. Scene controls and generation parameters

Expose a focused set of useful parameters rather than dozens of low-level noise controls.

Exact parameter-registration mechanisms and naming conventions must follow the existing engine.

Recommended controls:

| Parameter | Purpose |
|---|---|
| World Seed | Deterministic variation between worlds |
| Terrain Scale | Overall size of geological features |
| Terrain Roughness | Amount of secondary surface detail |
| Ridge Strength | Prominence of major terrain formations |
| Domain Warp | Degree of spatial twisting |
| Recursive Depth | Detail level of selected recursive structures |
| Formation Scale | Size of major landmarks |
| Attractor Influence | Visual prominence of attractor-derived geometry |
| Branching Density | Complexity of the connective path system |
| Palette | Selection between art-directed color schemes |
| Atmospheric Depth | Distance and contrast of atmospheric blending |
| Bass Deformation | Strength of audio-driven terrain movement |
| Transient Response | Strength of short-lived visual events |
| Audio Color Influence | Strength of music-driven color shifts |
| Motion Amount | Overall animation intensity |

Use sensible ranges and defaults.

If the scene-control system supports grouping, organize controls into:

1. World Generation
2. Geometry
3. Materials and Atmosphere
4. Audio Response
5. Animation and Camera
6. Quality and Rendering

Do not expose every internal implementation detail as a user-facing control.

---

# 7. Rendering quality and offline rendering

Treat 1080p/30 fps as a preferred live-performance target, not an absolute constraint.

The visual quality of the scene is the primary objective. A slower but significantly more compelling implementation is acceptable if AV Gen can render it offline.

## 7.1 Quality tiers

Investigate a small set of practical rendering-quality configurations.

### Live

Aim for stable 1080p/30 fps on the target hardware.

Possible trade-offs:

- Lower terrain mesh density.
- Reduced recursive detail at distance.
- Fewer secondary instances.
- Lower-cost material shading.
- Simplified or reduced-resolution secondary effects.
- Reduced shadow cost where visually acceptable.
- Bounded deformation complexity.

The live tier must retain the scene's defining silhouette, recursive structures, palette, and audio-responsive identity.

### High quality

Use for higher-quality preview or slower rendering where appropriate.

Allow increased geometric detail, denser recursive structures, improved shadowing, and more detailed material effects where the renderer supports them.

### Offline

If an existing offline-rendering path is available, investigate using it. Otherwise, determine what would be required to support the scene through the project's existing rendering architecture.

Potential options include:

- Higher terrain resolution.
- Greater recursive depth.
- Denser attractor trajectories.
- More detailed meshes.
- Higher-quality shadow settings.
- More expensive shading or secondary effects.
- Additional sampling or temporal accumulation if supported and justified.

Do not assume that offline rendering automatically means path tracing, or that a particular offline renderer exists.

Offline rendering should preserve the same world-generation seed and scene parameters so that live and offline variants remain recognizably the same scene.

## 7.2 Performance investigation

Profile the actual scene on representative hardware.

Measure:

- CPU scene-update time.
- CPU procedural-generation time.
- GPU frame time.
- Draw-call count.
- Mesh and vertex counts.
- Instance counts.
- Memory use.
- Shader and buffer initialization costs.
- Any recurring allocations or synchronization stalls.

Separate initialization costs from recurring frame costs.

A world that takes several seconds to generate once may still be acceptable. A world that regenerates all of its geometry every frame probably is not.

Do not optimize prematurely by removing the scene's defining visual features.

If performance falls short, first identify the actual bottleneck. Consider lower-cost representations, instancing, reduced update frequency, shader-based deformation, level-of-detail strategies, or different quality tiers before discarding a promising mathematical technique.

If the target frame rate remains unattainable, document the measured limitation and preserve the highest-quality viable implementation for offline rendering.

Do not claim the scene meets the target without a reproducible measurement.

---

# 8. Implementation plan

Follow these stages in order, but allow evidence from rendered results to change the plan.

## Stage 0 — repository audit and research

Inspect the architecture, run the relevant baseline tests, and complete the research phase.

Produce the research report and a short implementation plan before making substantial changes.

Establish a clean working branch or worktree using the repository's normal conventions.

## Stage 1 — visual skeleton

Implement the simplest complete environment that can validate the artistic direction.

It should include:

- A deterministic terrain generator.
- Three major terrain masses.
- A deliberate camera composition.
- Basic materials and lighting.
- A clear horizon and atmospheric depth.
- A way to regenerate the world from a seed.

Render the result immediately.

Evaluate the silhouette, depth, color, and overall composition before adding secondary geometry.

Do not proceed merely because the terrain generator works mathematically. The rendered image must be visually promising.

## Stage 2 — recursive landmarks

Add two or three major recursive formations.

Experiment with at least two construction strategies if the first produces generic or overly mechanical forms.

Integrate the formations into the terrain and composition.

Render from the intended camera and adjust their scale, placement, orientation, and silhouette.

The objective is to establish the scene's recognizable identity.

## Stage 3 — branching structure and attractor geometry

Add a branching path system and one attractor-derived formation.

Integrate them into the world rather than placing them randomly.

Test several attractor choices or parameterizations if needed.

Keep only the geometry that improves the composition.

If the attractor technique does not produce an interesting result, investigate alternatives rather than forcing a poor implementation into the scene.

## Stage 4 — art direction

Establish the final initial palette, material behavior, lighting, and atmosphere.

Test different seeds and compositions.

Check whether the scene remains visually coherent across several generated worlds.

Avoid over-detailing the scene before the major forms and colors are correct.

## Stage 5 — audio integration

Connect the scene to supported AV Gen analysis and modulation signals.

Start with one mapping at a time.

Test:

- Silence.
- Quiet passages.
- Sustained high energy.
- Transient-heavy material.
- Bass-heavy passages.
- Gradual changes in energy.
- Different seeds and world configurations.

Verify that every mapping has a visible and musically meaningful effect without destabilizing the world.

Use repeatable audio input where available.

## Stage 6 — quality tiers and profiling

Measure the actual rendering cost.

Introduce quality controls only where they produce useful, measurable trade-offs.

Preserve an offline-capable configuration if the live target cannot be met without substantially degrading the result.

Do not allow performance work to become an excuse to abandon the scene's visual identity.

## Stage 7 — polish and final verification

Resolve visual artifacts, geometry errors, unstable parameters, lifecycle issues, resource leaks, and inconsistent audio mappings.

Run relevant automated tests and perform a real end-to-end rendering test.

Update documentation and provide an honest account of what works, what remains incomplete, and what performance has actually been measured.

---

# 9. Testing requirements

Add tests consistent with AV Gen's existing test architecture.

Do not create a large testing framework just for this scene.

## 9.1 Deterministic generation

Verify that:

- Identical seeds and parameters produce equivalent generated results.
- Different seeds produce meaningful variation.
- Regeneration does not leak resources.
- Invalid or extreme parameter values are handled safely.
- Generated coordinates and vertex data remain finite.
- Recursive-generation budgets are respected.

## 9.2 Geometry correctness

Verify, where practical:

- Mesh indices remain valid.
- Generated buffers have valid dimensions.
- Normals and tangents are finite where required.
- Curves do not contain NaNs or infinities.
- Degenerate segments are handled.
- Generation terminates within configured bounds.
- Empty or failed generation paths do not crash the renderer.

## 9.3 Audio behavior

Verify that:

- Audio modulation remains within configured ranges.
- Continuous parameters respond smoothly.
- Transient events decay correctly.
- Sustained high input does not create unbounded deformation.
- Silence produces a stable baseline.
- Modulation does not trigger unintended full-world regeneration.
- Scene parameters are correctly registered and connected.

## 9.4 Rendering and lifecycle

Verify:

- The scene initializes successfully.
- Generated resources are released correctly.
- Regeneration does not leave stale GPU resources.
- Scene entry and exit work through the normal application lifecycle.
- The scene can render continuously without accumulating errors.
- Available headless or automated rendering infrastructure is used where appropriate.

A successful unit-test run does not replace visual verification.

---

# 10. Visual evaluation and iteration

This scene cannot be judged solely by inspecting code or proving that a fractal algorithm is mathematically correct.

Render the actual scene and inspect the images.

If Blender or another suitable offline tool is available, use it for rapid experiments with geometry, composition, and lighting where that is more efficient than repeated full application builds. Keep experiments separate from the final AV Gen implementation unless the resulting assets are intentionally being imported.

Use small, controlled experiments to answer specific questions:

- Does this terrain algorithm create a better silhouette?
- Does the recursive formation look geological or mechanical?
- Does the attractor geometry integrate with the landscape?
- Does domain warping create compelling folds or just noise?
- Does the palette preserve readable depth?
- Does the composition survive a different seed?
- Does audio deformation improve the image or merely make it unstable?

Keep the best experiments and record the conclusions that inform the implementation.

If a major visual component looks weak, change the design rather than adding unrelated effects to compensate.

## Minimum visual acceptance criteria

The final scene should:

1. Look like a coherent, deliberately composed surrealist environment.
2. Have recognizable large-scale terrain and distinctive landmarks.
3. Exhibit meaningful self-similarity or recursive structure.
4. Use nonlinear or fractal mathematics to shape the actual world, not merely its surface color.
5. Contain at least one integrated branching or attractor-derived structure.
6. Have sufficient spatial depth and contrast to remain readable at 1080p.
7. Produce visibly different, deterministic world variants.
8. Respond meaningfully to music while remaining visually coherent.
9. Render through AV Gen's actual scene and rendering pipeline.
10. Have a documented live-performance cost and an offline-quality fallback where feasible.

These criteria are more important than implementing every technique mentioned in the research phase.

---

# 11. Documentation and maintainability

Follow existing AV Gen naming, architecture, and documentation conventions.

Document:

- The mathematical techniques actually used.
- Why the chosen algorithms were selected.
- World-generation parameters and their ranges.
- Determinism and seed behavior.
- Geometry-generation limits.
- Audio signal mappings.
- Rendering-quality options.
- Known limitations and performance measurements.

Do not create excessive documentation for hypothetical features that were never implemented.

Do not retain abandoned experiments, redundant implementations, unused parameters, or dead tests simply because they were part of an earlier approach.

Keep the implementation modular enough that terrain generation, recursive formations, attractor geometry, material assignment, and audio modulation can be understood independently.

However, avoid turning a single scene into a generic procedural-world framework unless research demonstrates that reusable infrastructure is genuinely valuable to AV Gen.

Do not add backwards-compatibility layers for hypothetical previous versions of this scene.

Do not rewrite unrelated engine systems or expand the scope beyond what is necessary to deliver a compelling, functioning scene.

---

# 12. Final deliverables

When implementation is complete, provide:

1. **The working scene** registered and accessible through AV Gen's normal scene workflow.
2. **The research summary** with source links and the reasoning behind the selected algorithms.
3. **The architectural summary** describing the major scene components and how they interact.
4. **The parameter reference** including the world-generation controls and audio mappings.
5. **Automated tests** covering deterministic generation, geometry safety, modulation behavior, and applicable lifecycle paths.
6. **Visual evidence** from the actual AV Gen renderer, including the primary composition and several distinct seeds.
7. **Performance results** showing measured CPU and GPU costs, resolution, hardware, and quality settings.
8. **Offline-rendering assessment** describing the best available high-quality configuration and any remaining limitations.
9. **Documentation updates** consistent with the repository's existing conventions.
10. **A final implementation report** listing completed work, deviations from the original plan, known limitations, tests executed, and any remaining technical debt.

Do not report a visual feature as complete just because its code compiles. Verify that it contributes to the actual rendered scene.

Do not report a performance target as achieved without measuring it.

---

# 13. Agent operating instructions

Act as a technical artist, graphics researcher, and experienced engine developer working together.

Research first, but remain practical. Read primary sources when useful, inspect the actual engine, test competing approaches, and make decisions based on rendered evidence.

Use parallel agents where available for independent research or review. Keep ownership of the main implementation clear, reconcile conflicting recommendations, and review any contributed code before integrating it.

Build incrementally. Render early. Evaluate actual images. Refine the mathematical systems and art direction together.

Do not blindly follow this specification when experiments reveal a better solution. The listed algorithms are a starting vocabulary, not a mandatory collection of features.

If a technique proves visually weak, expensive, unstable, or incompatible with the renderer, investigate alternatives and document the decision.

Prefer the simplest implementation that achieves the intended visual result, but do not prematurely simplify the scene into generic noise terrain merely to meet a live-performance target.

**The ultimate objective is not to demonstrate fractals or chaos theory. It is to create an extraordinary world that could only plausibly emerge from a mathematical system—and that an audience would want to keep looking at.**

Deliver a scene that is visually compelling before it is audio-reactive, musically expressive once connected, deterministic when requested, and capable of higher-quality offline rendering if live performance cannot accommodate its full visual ambition.
